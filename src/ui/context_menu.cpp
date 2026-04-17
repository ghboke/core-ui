#include "context_menu.h"
#include "ui_context.h"
#include <algorithm>
#include <windowsx.h>
#include <dwmapi.h>

#ifndef GetDpiForWindow
static inline UINT GetDpiForWindow(HWND hwnd) {
    HMODULE hModule = LoadLibraryW(L"user32.dll");
    if (hModule) {
        typedef UINT(WINAPI* PFN_GetDpiForWindow)(HWND);
        PFN_GetDpiForWindow pfn = (PFN_GetDpiForWindow)GetProcAddress(hModule, "GetDpiForWindow");
        if (pfn) {
            UINT dpi = pfn(hwnd);
            FreeLibrary(hModule);
            return dpi;
        }
        FreeLibrary(hModule);
    }
    HDC hdc = GetDC(hwnd);
    UINT dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(hwnd, hdc);
    return dpi ? dpi : 96;
}
#endif

namespace ui {

bool ContextMenu::popupClassRegistered_ = false;

// ---- Build ----

void ContextMenu::AddItem(int id, const std::wstring& text) {
    MenuItem item;
    item.id = id;
    item.text = text;
    items_.push_back(std::move(item));
}

void ContextMenu::AddItemEx(int id, const std::wstring& text,
                             const std::wstring& shortcut, const std::string& svg,
                             Renderer* r) {
    MenuItem item;
    item.id = id;
    item.text = text;
    item.shortcut = shortcut;
    if (!svg.empty() && r) {
        item.icon = r->ParseSvgIcon(svg);
        item.hasIcon = item.icon.valid;
    }
    items_.push_back(std::move(item));
}

void ContextMenu::AddSeparator() {
    MenuItem item;
    item.isSeparator = true;
    items_.push_back(std::move(item));
}

void ContextMenu::AddSubmenu(const std::wstring& text, ContextMenuPtr submenu) {
    MenuItem item;
    item.text = text;
    item.submenu = std::move(submenu);
    item.id = -1;
    items_.push_back(std::move(item));
}

void ContextMenu::SetEnabled(int id, bool enabled) {
    for (auto& item : items_) {
        if (item.id == id) item.enabled = enabled;
    }
}

// ---- Show / Hide ----

void ContextMenu::Show(float x, float y, const D2D1_RECT_F& viewport) {
    float w = MenuWidth();
    float h = MenuHeight();
    if (x + w > viewport.right) x = viewport.right - w;
    if (y + h > viewport.bottom) y = viewport.bottom - h;
    if (x < viewport.left) x = viewport.left;
    if (y < viewport.top) y = viewport.top;
    x_ = x; y_ = y;
    visible_ = true;
    hoveredIndex_ = -1;
    openSubmenuIndex_ = -1;
    clickedId_ = -1;
}

void ContextMenu::ShowPopup(HWND parentHwnd, int screenX, int screenY) {
    parentHwnd_ = parentHwnd;

    // Get DPI scale from parent window
    UINT dpi = GetDpiForWindow(parentHwnd);
    float dpiScale = (float)dpi / 96.0f;

    int pw = (int)(MenuWidth() * dpiScale);
    int ph = (int)(MenuHeight() * dpiScale);

    // Clamp to screen
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    if (screenX + pw > sw) screenX = sw - pw;
    if (screenY + ph > sh) screenY = sh - ph;
    if (screenX < 0) screenX = 0;
    if (screenY < 0) screenY = 0;

    x_ = 0; y_ = 0;
    visible_ = true;
    hoveredIndex_ = -1;
    openSubmenuIndex_ = -1;
    clickedId_ = -1;

    CreatePopupWindow(parentHwnd, screenX, screenY);
}

void ContextMenu::Close() {
    visible_ = false;
    hoveredIndex_ = -1;
    if (openSubmenuIndex_ >= 0 && openSubmenuIndex_ < (int)items_.size()) {
        auto& sub = items_[openSubmenuIndex_].submenu;
        if (sub) sub->Close();
    }
    openSubmenuIndex_ = -1;
    DestroyPopupWindow();
}

// ---- Popup Window ----

void ContextMenu::CreatePopupWindow(HWND parent, int screenX, int screenY) {
    if (!popupClassRegistered_) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = PopupWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
        wc.lpszClassName = L"UiCore_MenuPopup";
        RegisterClassExW(&wc);
        popupClassRegistered_ = true;
    }

    UINT dpi = parent ? GetDpiForWindow(parent) : 96;
    float dpiScale = (float)dpi / 96.0f;
    int w = (int)(MenuWidth() * dpiScale);
    int h = (int)(MenuHeight() * dpiScale);

    popupHwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        L"UiCore_MenuPopup", L"",
        WS_POPUP | WS_THICKFRAME,
        screenX, screenY, w, h,
        parent, nullptr, GetModuleHandleW(nullptr), this);

    if (!popupHwnd_) return;

    // DWM shadow (same as main window)
    MARGINS margins = {1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(popupHwnd_, &margins);

    // Round corners + dark mode (Windows 11)
    BOOL useDark = (theme::CurrentMode() == theme::Mode::Dark) ? TRUE : FALSE;
    DwmSetWindowAttribute(popupHwnd_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &useDark, sizeof(useDark));
    int cornerPref = 3; // DWMWCP_ROUNDSMALL
    DwmSetWindowAttribute(popupHwnd_, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &cornerPref, sizeof(cornerPref));

    // Adjust window size to account for WS_THICKFRAME NC area being removed
    // After WM_NCCALCSIZE returns 0, client = window rect, but the initial
    // CreateWindow size included NC. We need to set the correct size.
    SetWindowPos(popupHwnd_, nullptr, 0, 0, w, h,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    // Init renderer with shared factories
    auto& ctx = GetContext();
    popupRenderer_.Init(ctx.D2DFactory(), ctx.DWFactory(), ctx.WICFactory());
    popupRenderer_.CreateRenderTarget(popupHwnd_);

    // Paint BEFORE showing to avoid white flash
    PaintPopup();
    ShowWindow(popupHwnd_, SW_SHOWNOACTIVATE);
    SetTimer(popupHwnd_, 1, 50, nullptr);
}

void ContextMenu::DestroyPopupWindow() {
    if (popupHwnd_) {
        DestroyWindow(popupHwnd_);
        popupHwnd_ = nullptr;
    }
}

void ContextMenu::PaintPopup() {
    if (!popupHwnd_ || !popupRenderer_.RT()) return;
    popupRenderer_.BeginDraw();
    popupRenderer_.Clear(hasBgColor_ ? bgColor_ : theme::kToolbarBg());
    Draw(popupRenderer_);
    popupRenderer_.EndDraw();
}

LRESULT CALLBACK ContextMenu::PopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ContextMenu* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<ContextMenu*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<ContextMenu*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_PAINT: {
        self->PaintPopup();
        ValidateRect(hwnd, nullptr);
        return 0;
    }
    case WM_MOUSEMOVE: {
        // Convert physical pixels to DIPs
        UINT dpi = GetDpiForWindow(hwnd);
        float scale = (float)dpi / 96.0f;
        float x = (float)GET_X_LPARAM(lParam) / scale;
        float y = (float)GET_Y_LPARAM(lParam) / scale;
        self->HandleMouseMove(x, y);
        InvalidateRect(hwnd, nullptr, FALSE);

        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        self->hoveredIndex_ = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        UINT dpi = GetDpiForWindow(hwnd);
        float scale = (float)dpi / 96.0f;
        float x = (float)GET_X_LPARAM(lParam) / scale;
        float y = (float)GET_Y_LPARAM(lParam) / scale;
        self->HandleMouseDown(x, y);
        return 0;
    }
    case WM_LBUTTONUP: {
        UINT dpi = GetDpiForWindow(hwnd);
        float scale = (float)dpi / 96.0f;
        float x = (float)GET_X_LPARAM(lParam) / scale;
        float y = (float)GET_Y_LPARAM(lParam) / scale;
        if (self->HandleMouseUp(x, y)) {
            int clickedId = self->ClickedItemId();
            // Post message to parent so it can handle the callback
            if (clickedId >= 0 && self->parentHwnd_) {
                PostMessage(self->parentHwnd_, WM_APP + 100, (WPARAM)clickedId, 0);
            }
            self->Close();
        }
        return 0;
    }
    case WM_NCCALCSIZE:
        if (wParam) {
            // Remove all non-client area (WS_THICKFRAME border) — keep only DWM shadow
            return 0;
        }
        break;
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_SIZE:
        if (self->popupRenderer_.RT()) {
            self->popupRenderer_.Resize(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_TIMER:
        // Poll: if mouse is pressed outside menu, close it
        if (wParam == 1) {
            if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
                POINT pt;
                GetCursorPos(&pt);
                RECT rc;
                GetWindowRect(hwnd, &rc);
                if (pt.x < rc.left || pt.x >= rc.right || pt.y < rc.top || pt.y >= rc.bottom) {
                    // Check if also outside any open submenu
                    bool inSubmenu = false;
                    if (self->openSubmenuIndex_ >= 0 && self->openSubmenuIndex_ < (int)self->items_.size()) {
                        auto& sub = self->items_[self->openSubmenuIndex_].submenu;
                        if (sub && sub->popupHwnd_) {
                            RECT src;
                            GetWindowRect(sub->popupHwnd_, &src);
                            if (pt.x >= src.left && pt.x < src.right && pt.y >= src.top && pt.y < src.bottom)
                                inSubmenu = true;
                        }
                    }
                    if (!inSubmenu) {
                        KillTimer(hwnd, 1);
                        self->Close();
                        return 0;
                    }
                }
            }
            // Also check if another window got focus
            HWND fg = GetForegroundWindow();
            if (fg && fg != hwnd && fg != self->parentHwnd_) {
                // Check it's not a submenu
                bool isSubmenu = false;
                if (self->openSubmenuIndex_ >= 0 && self->openSubmenuIndex_ < (int)self->items_.size()) {
                    auto& sub = self->items_[self->openSubmenuIndex_].submenu;
                    if (sub && sub->popupHwnd_ == fg) isSubmenu = true;
                }
                if (!isSubmenu) {
                    KillTimer(hwnd, 1);
                    self->Close();
                    return 0;
                }
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---- Geometry ----

bool ContextMenu::HasAnyIcon() const {
    for (auto& item : items_) {
        if (item.hasIcon) return true;
    }
    return false;
}

float ContextMenu::MenuWidth() const {
    float maxText = 0;
    float maxShortcut = 0;
    bool hasSubmenu = false;
    for (auto& item : items_) {
        if (item.isSeparator) continue;
        float tw = item.text.length() * 7.5f;
        if (tw > maxText) maxText = tw;
        float sw = item.shortcut.length() * 6.5f;
        if (sw > maxShortcut) maxShortcut = sw;
        if (item.submenu) hasSubmenu = true;
    }
    float iconCol = HasAnyIcon() ? kIconColWidth : 12.0f;
    float shortcutCol = maxShortcut > 0 ? maxShortcut + 24.0f : 0;
    float arrowCol = hasSubmenu ? kSubmenuArrowWidth : 0;
    float w = iconCol + maxText + shortcutCol + arrowCol + kPadding * 2 + 16.0f;
    return std::max(w, kMinWidth);
}

float ContextMenu::MenuHeight() const {
    float h = kPadding * 2;
    for (auto& item : items_) {
        h += item.isSeparator ? kSepHeight : kItemHeight;
    }
    return h;
}

D2D1_RECT_F ContextMenu::Bounds() const {
    return {x_, y_, x_ + MenuWidth(), y_ + MenuHeight()};
}

bool ContextMenu::Contains(float x, float y) const {
    auto b = Bounds();
    return x >= b.left && x < b.right && y >= b.top && y < b.bottom;
}

D2D1_RECT_F ContextMenu::ItemRect(int index) const {
    float y = y_ + kPadding;
    for (int i = 0; i < index && i < (int)items_.size(); i++) {
        y += items_[i].isSeparator ? kSepHeight : kItemHeight;
    }
    float h = items_[index].isSeparator ? kSepHeight : kItemHeight;
    return {x_, y, x_ + MenuWidth(), y + h};
}

int ContextMenu::HitTest(float x, float y) const {
    if (!Contains(x, y)) return -1;
    float iy = y_ + kPadding;
    for (int i = 0; i < (int)items_.size(); i++) {
        float h = items_[i].isSeparator ? kSepHeight : kItemHeight;
        if (y >= iy && y < iy + h) return i;
        iy += h;
    }
    return -1;
}

// ---- Draw ----

void ContextMenu::Draw(Renderer& r) {
    if (!visible_) return;

    float w = MenuWidth();
    float h = MenuHeight();
    bool hasIcon = HasAnyIcon();
    float iconCol = hasIcon ? kIconColWidth : 12.0f;

    // When drawing in popup window, no shadow needed (OS handles it via CS_DROPSHADOW)
    if (!popupHwnd_) {
        // Shadow (only for inline overlay mode)
        D2D1_RECT_F shadowRect = {x_ + kShadowOffset, y_ + kShadowOffset,
                                   x_ + w + kShadowOffset, y_ + h + kShadowOffset};
        r.FillRoundedRect(shadowRect, kCornerRadius, kCornerRadius, {0, 0, 0, 0.25f});

        // Background
        D2D1_RECT_F bgRect = {x_, y_, x_ + w, y_ + h};
        r.FillRoundedRect(bgRect, kCornerRadius, kCornerRadius, hasBgColor_ ? bgColor_ : theme::kToolbarBg());
        r.DrawRoundedRect(bgRect, kCornerRadius, kCornerRadius, theme::kDivider(), 0.5f);
    }

    // Items
    float iy = y_ + kPadding;
    for (int i = 0; i < (int)items_.size(); i++) {
        auto& item = items_[i];

        if (item.isSeparator) {
            float sepY = iy + kSepHeight / 2.0f;
            r.DrawLine(x_ + kPadding + 4, sepY, x_ + w - kPadding - 4, sepY,
                       theme::kDivider());
            iy += kSepHeight;
            continue;
        }

        // Hover highlight (grey, Windows style)
        if (i == hoveredIndex_ && item.enabled) {
            D2D1_RECT_F hlRect = {x_ + kPadding, iy + 1,
                                   x_ + w - kPadding, iy + kItemHeight - 1};
            D2D1_COLOR_F hlColor = (theme::CurrentMode() == theme::Mode::Dark)
                ? D2D1_COLOR_F{1.0f, 1.0f, 1.0f, 0.10f}   // 深色：白色 10%
                : D2D1_COLOR_F{0.0f, 0.0f, 0.0f, 0.06f};   // 浅色：黑色 6%
            r.FillRoundedRect(hlRect, 4.0f, 4.0f, hlColor);
        }

        // Text color
        D2D1_COLOR_F textColor = item.enabled ? theme::kBtnText()
                                               : D2D1_COLOR_F{0.5f, 0.5f, 0.5f, 0.6f};

        // Icon (16x16, centered in icon column)
        if (item.hasIcon) {
            constexpr float kIconSize = 16.0f;
            float iconX = x_ + kPadding + (iconCol - kIconSize) / 2.0f;
            float iconY = iy + (kItemHeight - kIconSize) / 2.0f;
            D2D1_RECT_F iconRect = {iconX, iconY, iconX + kIconSize, iconY + kIconSize};
            r.DrawSvgIcon(item.icon, iconRect, textColor);
        }

        // Text
        float textLeft = x_ + kPadding + iconCol;
        D2D1_RECT_F textRect = {textLeft, iy, x_ + w - kPadding - 8, iy + kItemHeight};
        r.DrawText(item.text, textRect, textColor, theme::kFontSizeNormal);

        // Shortcut
        if (!item.shortcut.empty()) {
            D2D1_COLOR_F shortcutColor = {0.5f, 0.5f, 0.55f, item.enabled ? 0.8f : 0.4f};
            D2D1_RECT_F scRect = {x_ + w - kPadding - 80, iy,
                                   x_ + w - kPadding - 8, iy + kItemHeight};
            r.DrawText(item.shortcut, scRect, shortcutColor, theme::kFontSizeSmall,
                       DWRITE_TEXT_ALIGNMENT_TRAILING);
        }

        // Submenu arrow
        if (item.submenu) {
            D2D1_RECT_F arrowRect = {x_ + w - kPadding - 16, iy,
                                      x_ + w - kPadding - 2, iy + kItemHeight};
            r.DrawText(L"\u25B8", arrowRect, textColor, theme::kFontSizeSmall,
                       DWRITE_TEXT_ALIGNMENT_CENTER);
        }

        iy += kItemHeight;
    }

    // Draw open submenu (only in overlay mode, popup submenus have their own window)
    if (!popupHwnd_ && openSubmenuIndex_ >= 0 && openSubmenuIndex_ < (int)items_.size()) {
        auto& sub = items_[openSubmenuIndex_].submenu;
        if (sub && sub->IsVisible()) {
            sub->Draw(r);
        }
    }
}

// ---- Event Handling ----

bool ContextMenu::HandleMouseMove(float x, float y) {
    if (!visible_) return false;

    // Check submenu first
    if (openSubmenuIndex_ >= 0 && openSubmenuIndex_ < (int)items_.size()) {
        auto& sub = items_[openSubmenuIndex_].submenu;
        if (sub && sub->IsVisible()) {
            if (sub->popupHwnd_) {
                // Submenu has its own window — it handles its own events
            } else if (sub->Contains(x, y)) {
                sub->HandleMouseMove(x, y);
                return true;
            }
        }
    }

    int hit = HitTest(x, y);
    hoveredIndex_ = hit;

    // Open/close submenus on hover
    if (hit >= 0 && hit < (int)items_.size() && !items_[hit].isSeparator) {
        if (items_[hit].submenu && items_[hit].enabled) {
            if (openSubmenuIndex_ != hit) {
                // Close previous submenu
                if (openSubmenuIndex_ >= 0 && openSubmenuIndex_ < (int)items_.size()) {
                    auto& prevSub = items_[openSubmenuIndex_].submenu;
                    if (prevSub) prevSub->Close();
                }
                // Open new submenu
                openSubmenuIndex_ = hit;
                auto& sub = items_[hit].submenu;
                if (popupHwnd_) {
                    // Open submenu as popup window too
                    RECT rc;
                    GetWindowRect(popupHwnd_, &rc);
                    D2D1_RECT_F ir = ItemRect(hit);
                    // rc is in physical pixels, ir is in DIPs
                    int subX = rc.right;  // right edge of parent menu
                    UINT subDpi = GetDpiForWindow(popupHwnd_);
                    float subScale = (float)subDpi / 96.0f;
                    int subY = rc.top + (int)(ir.top * subScale);
                    sub->ShowPopup(parentHwnd_ ? parentHwnd_ : popupHwnd_, subX, subY);
                } else {
                    D2D1_RECT_F ir = ItemRect(hit);
                    D2D1_RECT_F vp = Bounds();
                    vp.right += 400; vp.bottom += 400;
                    sub->Show(ir.right - 4, ir.top, vp);
                }
            }
        } else {
            if (openSubmenuIndex_ >= 0 && openSubmenuIndex_ < (int)items_.size()) {
                auto& prevSub = items_[openSubmenuIndex_].submenu;
                if (prevSub) prevSub->Close();
                openSubmenuIndex_ = -1;
            }
        }
    }

    return Contains(x, y);
}

bool ContextMenu::HandleMouseDown(float x, float y) {
    if (!visible_) return false;

    if (openSubmenuIndex_ >= 0 && openSubmenuIndex_ < (int)items_.size()) {
        auto& sub = items_[openSubmenuIndex_].submenu;
        if (sub && sub->IsVisible() && !sub->popupHwnd_ && sub->Contains(x, y)) {
            return sub->HandleMouseDown(x, y);
        }
    }

    return Contains(x, y);
}

bool ContextMenu::HandleMouseUp(float x, float y) {
    if (!visible_) return false;

    // Check inline submenu
    if (openSubmenuIndex_ >= 0 && openSubmenuIndex_ < (int)items_.size()) {
        auto& sub = items_[openSubmenuIndex_].submenu;
        if (sub && sub->IsVisible() && !sub->popupHwnd_ && sub->Contains(x, y)) {
            bool handled = sub->HandleMouseUp(x, y);
            if (handled && sub->ClickedItemId() >= 0) {
                clickedId_ = sub->ClickedItemId();
                Close();
                return true;
            }
            return handled;
        }
    }

    int hit = HitTest(x, y);
    if (hit >= 0 && hit < (int)items_.size()) {
        auto& item = items_[hit];
        if (!item.isSeparator && !item.submenu && item.enabled) {
            clickedId_ = item.id;
            Close();
            return true;
        }
    }

    if (!Contains(x, y)) {
        Close();
        return true;
    }

    return false;
}

} // namespace ui
