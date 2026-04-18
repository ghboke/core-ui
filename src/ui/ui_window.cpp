#include "ui_window.h"
#include "ui_context.h"
#include "controls.h"
#include "theme.h"
#include "animation.h"
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <wincodec.h>
#include <cmath>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shlwapi.lib")

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

#ifndef GetSystemMetricsForDpi
static inline int GetSystemMetricsForDpi(int index, UINT dpi) {
    HMODULE hModule = LoadLibraryW(L"user32.dll");
    if (hModule) {
        typedef int(WINAPI* PFN_GetSystemMetricsForDpi)(int, UINT);
        PFN_GetSystemMetricsForDpi pfn = (PFN_GetSystemMetricsForDpi)GetProcAddress(hModule, "GetSystemMetricsForDpi");
        if (pfn) {
            int value = pfn(index, dpi);
            FreeLibrary(hModule);
            return value;
        }
        FreeLibrary(hModule);
    }
    return GetSystemMetrics(index);
}
#endif

namespace ui {

// 放在最上方供 WM_APP+120 case 引用（InvokeSync 的 SendMessage 携带的载荷）
struct UiInvokeReq {
    void (*fn)(void*);
    void* ud;
};

bool UiWindowImpl::classRegistered_ = false;

#ifndef DWMWA_TRANSITIONS_FORCEDISABLED
#define DWMWA_TRANSITIONS_FORCEDISABLED 3
#endif

namespace {
constexpr UINT kMsgStartupReveal = WM_APP + 101;
constexpr UINT_PTR kCaretBlinkTimerId = 0xCA11;
constexpr UINT_PTR kToggleAnimTimerId = 0xCA12;
constexpr UINT_PTR kWindowOpenAnimTimerId = 0xCA13;
constexpr UINT_PTR kWindowCloseAnimTimerId = 0xCA14;
constexpr UINT kToggleAnimIntervalMs = 16;
constexpr UINT kWindowAnimIntervalMs = 16;
constexpr UINT_PTR kToastFadeTimerId = 0xCA15;
constexpr UINT kToastFadeIntervalMs = 16;

constexpr float kPopPeakScale = 1.02f;       // 关闭动画微弹峰值
constexpr float kClosePeakPhase = 0.18f;
constexpr float kCloseEndScale = 0.88f;
constexpr float kCloseFadeStart = 0.40f;
constexpr int   kOpenSlideOffset = 12;       // 开场上滑偏移（像素）

static float Clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static float EaseOutCubic(float t) {
    float p = Clamp01(t);
    return 1.0f - std::pow(1.0f - p, 3.0f);
}

static float EaseInCubic(float t) {
    float p = Clamp01(t);
    return p * p * p;
}

static float PopBounceScale(float t, float startScale, float peakScale, float endScale, float peakPhase) {
    float p = Clamp01(t);
    float phase = Clamp01(peakPhase);
    if (phase <= 0.0f) return endScale;
    if (phase >= 1.0f) return startScale;

    if (p < phase) {
        float local = EaseOutCubic(p / phase);
        return startScale + (peakScale - startScale) * local;
    }

    float local = EaseOutCubic((p - phase) / (1.0f - phase));
    return peakScale + (endScale - peakScale) * local;
}

static void SetWindowScaleAtCenter(HWND hwnd, float centerX, float centerY,
                                   float targetWidth, float targetHeight, float scale) {
    float currentWidth = targetWidth * scale;
    float currentHeight = targetHeight * scale;
    int currentX = (int)(centerX - currentWidth / 2.0f);
    int currentY = (int)(centerY - currentHeight / 2.0f);
    SetWindowPos(hwnd, nullptr, currentX, currentY, (int)currentWidth, (int)currentHeight,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}
}

// ---- Helper: tree traversal ----
static void ForEachWidget(Widget* w, const std::function<void(Widget*)>& fn) {
    fn(w);
    for (auto& c : w->Children()) ForEachWidget(c.get(), fn);
}

static void ForEachToggleWidget(Widget* w, const std::function<void(ToggleWidget*)>& fn) {
    auto* tw = dynamic_cast<ToggleWidget*>(w);
    if (tw) fn(tw);
    for (auto& c : w->Children()) ForEachToggleWidget(c.get(), fn);
}

UiWindowImpl::UiWindowImpl() = default;
UiWindowImpl::~UiWindowImpl() {
    if (hwnd_) DestroyWindow(hwnd_);
    if (hIcon_) DestroyIcon(hIcon_);
}

bool UiWindowImpl::RegisterWindowClass() {
    if (classRegistered_) return true;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"UiCore_Window";
    if (!RegisterClassExW(&wc)) return false;

    classRegistered_ = true;
    return true;
}

bool UiWindowImpl::Create(const wchar_t* title, int width, int height,
                           bool borderless, bool resizable, bool acceptFiles,
                           int x, int y, bool toolWindow) {
    if (!RegisterWindowClass()) return false;

    borderless_ = borderless;
    resizable_ = resizable;
    toolWindow_ = toolWindow;
    title_ = title ? title : L"";
    configWidth_ = width;
    configHeight_ = height;

    DWORD exStyle = WS_EX_COMPOSITED | WS_EX_LAYERED;
    if (toolWindow) exStyle |= WS_EX_TOOLWINDOW;
    else            exStyle |= WS_EX_APPWINDOW;
    if (acceptFiles) exStyle |= WS_EX_ACCEPTFILES;

    DWORD style;
    if (borderless) {
        style = WS_POPUP | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;
        if (resizable) style |= WS_THICKFRAME;
    } else {
        style = WS_OVERLAPPEDWINDOW;
        if (!resizable) style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }

    // Scale logical size to physical pixels using system DPI
    UINT sysDpi = 96;
    {
        HDC hdc = GetDC(nullptr);
        if (hdc) { sysDpi = (UINT)GetDeviceCaps(hdc, LOGPIXELSX); ReleaseDC(nullptr, hdc); }
    }
    int physW = MulDiv(width,  (int)sysDpi, 96);
    int physH = MulDiv(height, (int)sysDpi, 96);

    int winX, winY;
    if (x != CW_USEDEFAULT && y != CW_USEDEFAULT) {
        winX = MulDiv(x, (int)sysDpi, 96);
        winY = MulDiv(y, (int)sysDpi, 96);
    } else {
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        winX = (screenW - physW) / 2;
        winY = (screenH - physH) / 2;
    }

    windowTargetWidth_ = (float)physW;
    windowTargetHeight_ = (float)physH;
    windowTargetX_ = winX;
    windowTargetY_ = winY;

    /* 不需要开场动画时不加 WS_EX_LAYERED，避免首次 ShowWindow 500ms+ 延迟 */
    if (!skipOpenAnimation_)
        exStyle |= WS_EX_LAYERED;

    hwnd_ = CreateWindowExW(exStyle, L"UiCore_Window", title_.c_str(), style,
                            winX, winY, physW, physH,
                            nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd_) return false;

    if (!skipOpenAnimation_) {
        SetLayeredWindowAttributes(hwnd_, 0, 0, LWA_ALPHA);
    }
    ShowWindow(hwnd_, SW_HIDE);

    BOOL disableTransitions = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_TRANSITIONS_FORCEDISABLED,
                          &disableTransitions, sizeof(disableTransitions));

    dpi_ = GetDpiForWindow(hwnd_);
    dpiScale_ = (float)dpi_ / 96.0f;

    if (borderless) {
        MARGINS margins = {0, 0, 1, 0};
        DwmExtendFrameIntoClientArea(hwnd_, &margins);
    }

    auto& ctx = GetContext();
    if (!renderer_.Init(ctx.D2DFactory(), ctx.DWFactory(), ctx.WICFactory())) return false;
    // CreateRenderTarget deferred to Show() — ensures client rect is final

    return true;
}

void UiWindowImpl::SetIconFromPixels(const uint8_t* rgba, int w, int h) {
    if (!hwnd_ || !rgba || w <= 0 || h <= 0) return;

    // Create DIB section with BGRA pixel data (Windows expects BGRA)
    BITMAPV5HEADER bi = {};
    bi.bV5Size        = sizeof(bi);
    bi.bV5Width       = w;
    bi.bV5Height      = -h;  // top-down
    bi.bV5Planes      = 1;
    bi.bV5BitCount    = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask     = 0x00FF0000;
    bi.bV5GreenMask   = 0x0000FF00;
    bi.bV5BlueMask    = 0x000000FF;
    bi.bV5AlphaMask   = 0xFF000000;

    HDC dc = GetDC(nullptr);
    uint8_t* bits = nullptr;
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS,
                                     (void**)&bits, nullptr, 0);
    if (!color || !bits) { ReleaseDC(nullptr, dc); return; }

    // Convert RGBA → BGRA with premultiplied alpha
    for (int i = 0; i < w * h; i++) {
        uint8_t r = rgba[i*4+0], g = rgba[i*4+1], b = rgba[i*4+2], a = rgba[i*4+3];
        bits[i*4+0] = (uint8_t)(b * a / 255);  // B
        bits[i*4+1] = (uint8_t)(g * a / 255);  // G
        bits[i*4+2] = (uint8_t)(r * a / 255);  // R
        bits[i*4+3] = a;                        // A
    }

    HBITMAP mask = CreateBitmap(w, h, 1, 1, nullptr);
    ICONINFO ii = {};
    ii.fIcon    = TRUE;
    ii.hbmMask  = mask;
    ii.hbmColor = color;
    HICON icon = CreateIconIndirect(&ii);

    DeleteObject(color);
    DeleteObject(mask);
    ReleaseDC(nullptr, dc);

    if (icon) {
        if (hIcon_) DestroyIcon(hIcon_);
        hIcon_ = icon;
        SendMessage(hwnd_, WM_SETICON, ICON_BIG,   (LPARAM)hIcon_);
        SendMessage(hwnd_, WM_SETICON, ICON_SMALL,  (LPARAM)hIcon_);
    }
}

void UiWindowImpl::Show() {
    if (!hwnd_) return;

    /* 外部可能先 ShowWindow(SW_MAXIMIZE) 了 —— 这里检测一次，
     * 如果已经是最大化，跳过 SetWindowPos 写死尺寸的路径 +
     * 跳过 slide 动画（动画用 SW_SHOWNOACTIVATE 会把最大化态覆盖掉）。*/
    bool preMaximized = IsZoomed(hwnd_) != 0;

    /* 从当前窗口位置同步 target（外部可能已通过 SetWindowPos 改了位置） */
    {
        RECT wr; GetWindowRect(hwnd_, &wr);
        windowTargetX_ = wr.left;
        windowTargetY_ = wr.top;
        windowTargetWidth_ = (float)(wr.right - wr.left);
        windowTargetHeight_ = (float)(wr.bottom - wr.top);
    }

    BOOL disableTransitions = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_TRANSITIONS_FORCEDISABLED,
                          &disableTransitions, sizeof(disableTransitions));

    LONG_PTR exStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    if ((exStyle & WS_EX_LAYERED) == 0) {
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
    }

    startupRevealPending_ = false;
    startupRevealPosted_ = false;

    if (preMaximized) {
        /* 最大化路径：只触发 NCCALCSIZE 让 borderless 客户区生效，不改位置/尺寸 */
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        if (!renderer_.RT()) renderer_.CreateRenderTarget(hwnd_);
        LayoutRoot();
        OnPaint();
        ValidateRect(hwnd_, nullptr);
        /* 直接以最大化态显示，不走 slide 动画 */
        SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
        ShowWindow(hwnd_, SW_SHOWMAXIMIZED);
        if (!toolWindow_) SetForegroundWindow(hwnd_);
        return;
    }

    // Force WM_NCCALCSIZE so client rect reflects our borderless override
    SetWindowPos(hwnd_, nullptr, windowTargetX_, windowTargetY_,
                 (int)windowTargetWidth_, (int)windowTargetHeight_,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    // Now create render target — client rect is accurate after NCCALCSIZE
    if (!renderer_.RT()) renderer_.CreateRenderTarget(hwnd_);

    LayoutRoot();
    OnPaint();
    ValidateRect(hwnd_, nullptr);

    StartWindowOpenAnimation();
}

void UiWindowImpl::PrepareRT() {
    if (!hwnd_) return;

    BOOL disableTransitions = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_TRANSITIONS_FORCEDISABLED,
                          &disableTransitions, sizeof(disableTransitions));

    LONG_PTR exStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    if (exStyle & WS_EX_LAYERED)
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);

    {
        RECT wr; GetWindowRect(hwnd_, &wr);
        windowTargetX_ = wr.left;
        windowTargetY_ = wr.top;
        windowTargetWidth_ = (float)(wr.right - wr.left);
        windowTargetHeight_ = (float)(wr.bottom - wr.top);
    }

    SetWindowPos(hwnd_, nullptr, windowTargetX_, windowTargetY_,
                 (int)windowTargetWidth_, (int)windowTargetHeight_,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    if (!renderer_.RT()) renderer_.CreateRenderTarget(hwnd_);
    LayoutRoot();
}

void UiWindowImpl::ShowImmediate() {
    if (!hwnd_) return;

    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    if (!renderer_.RT()) renderer_.CreateRenderTarget(hwnd_);

    LayoutRoot();
    /* 首帧 Present 不等 vsync，避免 ShowWindow 触发的 WM_PAINT 阻塞 100ms+ */
    renderer_.skipVSync = true;
    OnPaint();
    ValidateRect(hwnd_, nullptr);

    /* SW_SHOWNA 显示但不激活 → 不触发 WM_ACTIVATE 的 DWM 首帧同步（部分机器 200-300ms） */
    ShowWindow(hwnd_, SW_SHOWNA);
    if (!toolWindow_) {
        BringWindowToTop(hwnd_);
        SetForegroundWindow(hwnd_);
    }
}

void UiWindowImpl::Hide() {
    ShowWindow(hwnd_, SW_HIDE);
}

void UiWindowImpl::Invalidate() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---- 窗口几何（DIP-native） ----

void UiWindowImpl::SetWindowRect(int xScreen, int yScreen, int wDip, int hDip) {
    if (!hwnd_) return;
    int pw = (int)(wDip * dpiScale_);
    int ph = (int)(hDip * dpiScale_);
    /* SetWindowPos 之后强制立刻重绘。对画布模式（bgMode=1）很关键 —— 扩大窗口
     * 时新暴露区域没有 Clear 介入，如果不主动 UpdateWindow 一把，那段时间会看到
     * 前一帧甚至系统默认色的残留。SWP_NOCOPYBITS 防止 Windows 把旧客户区内容
     * 整块位移，强迫全量重绘。 */
    SetWindowPos(hwnd_, nullptr, xScreen, yScreen, pw, ph,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    InvalidateRect(hwnd_, nullptr, FALSE);
    UpdateWindow(hwnd_);
}

void UiWindowImpl::SetWindowSize(int wDip, int hDip) {
    if (!hwnd_) return;
    RECT r; GetWindowRect(hwnd_, &r);
    SetWindowRect(r.left, r.top, wDip, hDip);
}

void UiWindowImpl::SetWindowPosition(int xScreen, int yScreen) {
    if (!hwnd_) return;
    /* 只移动不改尺寸 */
    SetWindowPos(hwnd_, nullptr, xScreen, yScreen, 0, 0,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
}

void UiWindowImpl::GetWindowRectScreen(int* x, int* y, int* wDip, int* hDip) const {
    if (!hwnd_) return;
    RECT r; GetWindowRect(hwnd_, &r);
    if (x) *x = r.left;
    if (y) *y = r.top;
    if (wDip) *wDip = (int)((r.right - r.left) / dpiScale_);
    if (hDip) *hDip = (int)((r.bottom - r.top) / dpiScale_);
}

void UiWindowImpl::ResizeWithAnchor(int wDip, int hDip,
                                     float anchorClientXDip, float anchorClientYDip,
                                     int anchorScreenX, int anchorScreenY) {
    /* 要让 client(acx, acy) 落在屏幕 (sx, sy)：
     *   new_window_left = sx - acx * dpiScale
     *   new_window_top  = sy - acy * dpiScale
     * 注意：这里假设无边框或 client-area 覆盖整个窗口（画布模式成立）。
     * 有系统边框的窗口 client 区相对窗口偏移一个标题栏 + 边框，不适用此 API。 */
    int newLeft = anchorScreenX - (int)(anchorClientXDip * dpiScale_);
    int newTop  = anchorScreenY - (int)(anchorClientYDip * dpiScale_);
    SetWindowRect(newLeft, newTop, wDip, hDip);
}

void UiWindowImpl::EnableCanvasMode(bool enable) {
    if (enable) {
        SetMinSize(32, 32);
        SetBackgroundMode(1);
        if (root_) root_->dragWindow = true;
        /* 隐藏根下第一个 TitleBar（如果有） */
        std::function<bool(Widget*)> hideTitleBar = [&](Widget* w) -> bool {
            if (!w) return false;
            if (dynamic_cast<TitleBarWidget*>(w)) { w->visible = false; return true; }
            for (auto& c : w->Children()) if (hideTitleBar(c.get())) return true;
            return false;
        };
        if (root_) hideTitleBar(root_.get());
    } else {
        SetMinSize(0, 0);
        SetBackgroundMode(0);
        if (root_) root_->dragWindow = false;
        /* 恢复 TitleBar visible */
        std::function<void(Widget*)> showTitleBar = [&](Widget* w) {
            if (!w) return;
            if (dynamic_cast<TitleBarWidget*>(w)) { w->visible = true; return; }
            for (auto& c : w->Children()) showTitleBar(c.get());
        };
        if (root_) showTitleBar(root_.get());
    }
    if (root_) LayoutRoot();
    Invalidate();
}

static void UpdateMaxButtonIcon(Widget* w, bool maximized) {
    if (!w) return;
    auto* tb = dynamic_cast<TitleBarWidget*>(w);
    if (tb) {
        auto* btn = dynamic_cast<CaptionButtonWidget*>(tb->MaxBtn());
        if (btn) btn->icon = maximized ? L"\xE923" : L"\xE922";
        return;
    }
    for (auto& c : w->Children()) UpdateMaxButtonIcon(c.get(), maximized);
}

static void WireTitleBar(Widget* w, HWND hwnd) {
    auto* tb = dynamic_cast<TitleBarWidget*>(w);
    if (tb) {
        tb->windowHandle = (void*)hwnd;
        if (tb->CloseBtn()) {
            tb->CloseBtn()->onClick = [hwnd]() { PostMessage(hwnd, WM_CLOSE, 0, 0); };
        }
        if (tb->MaxBtn()) {
            tb->MaxBtn()->onClick = [hwnd]() {
                ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
            };
        }
        if (tb->MinBtn()) {
            tb->MinBtn()->onClick = [hwnd]() { ShowWindow(hwnd, SW_MINIMIZE); };
        }
    }
    // Wire MenuBar to window HWND for popup menus
    auto* mb = dynamic_cast<MenuBarWidget*>(w);
    if (mb) {
        mb->SetHwnd(hwnd);
    }
    if (tb) return;
    for (auto& c : w->Children()) WireTitleBar(c.get(), hwnd);
}

void UiWindowImpl::SetRoot(WidgetPtr root) {
    root_ = std::move(root);
    // Auto-wire any TitleBarWidget to this window's HWND
    if (root_ && hwnd_) WireTitleBar(root_.get(), hwnd_);
    LayoutRoot();
    UpdateCaretBlinkTimer();
    UpdateToggleAnimTimer();
    Invalidate();
}

void UiWindowImpl::StartWindowOpenAnimation() {
    if (!hwnd_) return;

    windowAnimating_ = true;
    windowClosing_ = false;
    windowAnimProgress_ = 0.0f;

    // 窗口直接放在目标尺寸，不缩放，仅用透明度 + Y 偏移做动画
    SetWindowPos(hwnd_, nullptr, windowTargetX_, windowTargetY_ + kOpenSlideOffset,
                 (int)windowTargetWidth_, (int)windowTargetHeight_,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetLayeredWindowAttributes(hwnd_, 0, 0, LWA_ALPHA);

    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    QueryPerformanceCounter(&windowAnimStartTick_);
    KillTimer(hwnd_, kWindowOpenAnimTimerId);
    if (!SetTimer(hwnd_, kWindowOpenAnimTimerId, kWindowAnimIntervalMs, nullptr)) {
        windowAnimating_ = false;
        SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
        SetWindowPos(hwnd_, nullptr, windowTargetX_, windowTargetY_,
                     (int)windowTargetWidth_, (int)windowTargetHeight_,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        ShowOwnedPopups(hwnd_, TRUE);
        if (!toolWindow_) SetForegroundWindow(hwnd_);
        return;
    }

    PostMessageW(hwnd_, WM_TIMER, kWindowOpenAnimTimerId, 0);
}

void UiWindowImpl::StartWindowCloseAnimation() {
    if (!hwnd_) return;

    windowClosing_ = true;
    windowAnimating_ = true;
    windowAnimProgress_ = 0.0f;

    RECT rect;
    GetWindowRect(hwnd_, &rect);
    windowTargetWidth_ = (float)(rect.right - rect.left);
    windowTargetHeight_ = (float)(rect.bottom - rect.top);
    windowCloseStartX_ = rect.left;
    windowCloseStartY_ = rect.top;

    QueryPerformanceCounter(&windowAnimStartTick_);
    KillTimer(hwnd_, kWindowCloseAnimTimerId);
    if (!SetTimer(hwnd_, kWindowCloseAnimTimerId, kWindowAnimIntervalMs, nullptr)) {
        windowClosing_ = false;
        windowAnimating_ = false;
        DestroyWindow(hwnd_);
    }
}

void UiWindowImpl::UpdateToggleAnimTimer() {
    if (!hwnd_) return;

    bool needsTimer = false;
    if (root_) {
        ForEachToggleWidget(root_.get(), [&](ToggleWidget* tw) {
            if (tw->animating_) {
                needsTimer = true;
            }
        });

        ForEachWidget(root_.get(), [&](Widget* w) {
            auto* cb = dynamic_cast<CheckBoxWidget*>(w);
            if (cb && cb->animating_) {
                needsTimer = true;
            }
            auto* rb = dynamic_cast<RadioButtonWidget*>(w);
            if (rb && rb->animating_) {
                needsTimer = true;
            }
            auto* pb = dynamic_cast<ProgressBarWidget*>(w);
            if (pb && (pb->animating_ || pb->IsIndeterminate())) {
                needsTimer = true;
            }
            auto* sl = dynamic_cast<SliderWidget*>(w);
            if (sl && std::abs(sl->thumbScaleCurrent_ - sl->thumbScaleTarget_) > 0.001f) {
                needsTimer = true;
            }
            auto* sv = dynamic_cast<SplitViewWidget*>(w);
            if (sv && sv->animating_) {
                needsTimer = true;
            }
            auto* ex = dynamic_cast<ExpanderWidget*>(w);
            if (ex && ex->animating_) {
                needsTimer = true;
            }
        });
    }

    if (needsTimer && !toggleAnimTimerRunning_) {
        if (SetTimer(hwnd_, kToggleAnimTimerId, kToggleAnimIntervalMs, nullptr) != 0) {
            toggleAnimTimerRunning_ = true;
        }
    } else if (!needsTimer && toggleAnimTimerRunning_) {
        KillTimer(hwnd_, kToggleAnimTimerId);
        toggleAnimTimerRunning_ = false;
    }
}

void UiWindowImpl::SetTitle(const std::wstring& title) {
    title_ = title;
    if (hwnd_) SetWindowTextW(hwnd_, title_.c_str());
}

Renderer* g_activeRenderer = nullptr;

void UiWindowImpl::LayoutRoot() {
    if (!renderer_.RT() || !root_) return;
    D2D1_SIZE_F size = renderer_.RT()->GetSize();
    root_->rect = {0, 0, size.width, size.height};
    Viewport() = root_->rect;
    g_activeRenderer = &renderer_;
    root_->DoLayout();
    g_activeRenderer = nullptr;
}

// ---- WndProc ----

LRESULT CALLBACK UiWindowImpl::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    UiWindowImpl* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<UiWindowImpl*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<UiWindowImpl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT UiWindowImpl::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT:
    case WM_DISPLAYCHANGE:
        OnPaint(); ValidateRect(hwnd_, nullptr); return 0;

    case WM_ENTERSIZEMOVE:
        isMoving_ = true;
        isResizing_ = false;  // 先假设是移动，WM_SIZING 会修正
        CloseMenu();
        break;

    case WM_SIZING:
        // 收到 WM_SIZING 说明是 resize，不是纯移动
        if (!isResizing_) {
            isResizing_ = true;
            ReleaseDragCache();  // 清除可能已创建的缓存
        }
        break;

    case WM_MOVING:
        /* 强制 DWM 同步合成，消除拖动果冻延迟 */
        DwmFlush();
        break;

    case WM_EXITSIZEMOVE:
        isMoving_ = false;
        isResizing_ = false;
        ReleaseDragCache();
        Invalidate();
        break;

    case WM_SIZE:
        // 动画期间跳过 resize/layout/repaint，DWM 会自动缩放已有内容
        if (windowAnimating_) {
            maximized_ = (wParam == SIZE_MAXIMIZED);
            return 0;
        }
        OnResize(LOWORD(lParam), HIWORD(lParam));
        maximized_ = (wParam == SIZE_MAXIMIZED);
        // 更新最大化按钮图标：最大化 → 还原图标，非最大化 → 最大化图标
        UpdateMaxButtonIcon(root_.get(), maximized_);
        if (borderless_) {
            MARGINS margins = maximized_ ? MARGINS{0, 0, 0, 0} : MARGINS{0, 0, 1, 0};
            DwmExtendFrameIntoClientArea(hwnd_, &margins);
        }
        // 立即重绘并 Present，不等 WM_PAINT —— 消除 DWM 拉伸旧帧的果冻效果
        renderer_.skipVSync = true;  // resize 期间不等 VSync，减少延迟
        OnPaint();
        ValidateRect(hwnd_, nullptr);
        return 0;

    case WM_DPICHANGED:
        OnDpiChanged(HIWORD(wParam), reinterpret_cast<const RECT*>(lParam)); return 0;

    case WM_GETMINMAXINFO:
        OnGetMinMaxInfo(reinterpret_cast<MINMAXINFO*>(lParam)); return 0;

    case WM_MOUSEMOVE:
        OnMouseMove((float)GET_X_LPARAM(lParam), (float)GET_Y_LPARAM(lParam)); return 0;
    case WM_MOUSELEAVE:
        if (hoveredWidget_) { hoveredWidget_->hovered = false; hoveredWidget_ = nullptr; }
        tooltipVisible_ = false; tooltipWidget_ = nullptr;
        Invalidate(); return 0;
    case WM_LBUTTONDOWN:
        OnMouseDown((float)GET_X_LPARAM(lParam), (float)GET_Y_LPARAM(lParam)); return 0;
    case WM_LBUTTONUP:
        OnMouseUp((float)GET_X_LPARAM(lParam), (float)GET_Y_LPARAM(lParam)); return 0;
    case WM_TIMER:
        if (wParam == kTooltipTimerId) {
            KillTimer(hwnd_, kTooltipTimerId);
            tooltipTimerId_ = 0;
            if (hoveredWidget_ && !hoveredWidget_->tooltip.empty() && !tooltipVisible_) {
                tooltipVisible_ = true;
                tooltipWidget_ = hoveredWidget_;
                tooltipX_ = mouseX_;
                tooltipY_ = mouseY_;
                Invalidate();
            }
            return 0;
        }
        if (wParam == kToastFadeTimerId) {
            switch (toastPhase_) {
                case 1: /* slideIn */
                    toastSlide_ += 0.08f;
                    if (toastSlide_ >= 1.0f) {
                        toastSlide_ = 1.0f;
                        toastPhase_ = 2;
                        holdElapsed_ = 0;
                    }
                    break;
                case 2: /* hold */
                    holdElapsed_ += kToastFadeIntervalMs;
                    if (holdElapsed_ >= holdDurationMs_) {
                        toastPhase_ = 3;
                        toastSlide_ = 1.0f;
                    }
                    break;
                case 3: /* slideOut */
                    toastSlide_ -= 0.06f;
                    if (toastSlide_ <= 0.0f) {
                        toastSlide_ = 0.0f;
                        toastPhase_ = 0;
                        toastText_.clear();
                        KillTimer(hwnd_, kToastFadeTimerId);
                        toastTimerId_ = 0;
                    }
                    break;
            }
            Invalidate();
            return 0;
        }
        if (wParam == kCaretBlinkTimerId) {
            if (HasFocusedTextInput()) {
                Invalidate();
            } else {
                UpdateCaretBlinkTimer();
            }
            return 0;
        }
        if (wParam == kToggleAnimTimerId) {
            if (root_) {
                bool anyAnimating = false;
                ForEachToggleWidget(root_.get(), [&](ToggleWidget* tw) {
                    if (tw->animating_) {
                        tw->UpdateAnimation();
                    }
                    if (tw->animating_) anyAnimating = true;
                });

                ForEachWidget(root_.get(), [&](Widget* w) {
                    auto* cb = dynamic_cast<CheckBoxWidget*>(w);
                    if (cb && cb->animating_) {
                        cb->UpdateAnimation();
                        if (cb->animating_) anyAnimating = true;
                    }
                    auto* rb = dynamic_cast<RadioButtonWidget*>(w);
                    if (rb && rb->animating_) {
                        rb->UpdateAnimation();
                        if (rb->animating_) anyAnimating = true;
                    }
                    auto* pb = dynamic_cast<ProgressBarWidget*>(w);
                    if (pb && pb->animating_) {
                        pb->UpdateAnimation();
                        if (pb->animating_) anyAnimating = true;
                    }
                    if (pb && pb->IsIndeterminate()) {
                        anyAnimating = true;  // keep timer alive for continuous animation
                    }
                    auto* sl = dynamic_cast<SliderWidget*>(w);
                    if (sl && std::abs(sl->thumbScaleCurrent_ - sl->thumbScaleTarget_) > 0.001f) {
                        anyAnimating = true;
                    }
                    auto* sv = dynamic_cast<SplitViewWidget*>(w);
                    if (sv && sv->animating_) {
                        sv->UpdateAnimation();
                        sv->DoLayout();
                        if (sv->animating_) anyAnimating = true;
                    }
                    auto* ex = dynamic_cast<ExpanderWidget*>(w);
                    if (ex && ex->animating_) {
                        ex->UpdateAnimation();
                        LayoutRoot();  // Expander size changed, re-layout parent chain
                        if (ex->animating_) anyAnimating = true;
                    }
                });

                Invalidate();

                if (!anyAnimating && toggleAnimTimerRunning_) {
                    KillTimer(hwnd_, kToggleAnimTimerId);
                    toggleAnimTimerRunning_ = false;
                }
            }
            return 0;
        }
        if (wParam == kWindowOpenAnimTimerId && windowAnimating_ && !windowClosing_) {
            LARGE_INTEGER now, freq;
            QueryPerformanceCounter(&now);
            QueryPerformanceFrequency(&freq);
            float elapsedMs = static_cast<float>(now.QuadPart - windowAnimStartTick_.QuadPart)
                              * 1000.0f / static_cast<float>(freq.QuadPart);
            windowAnimProgress_ = elapsedMs / kWindowOpenAnimDurationMs;
            if (windowAnimProgress_ >= 1.0f) {
                windowAnimProgress_ = 1.0f;
                windowAnimating_ = false;
                KillTimer(hwnd_, kWindowOpenAnimTimerId);
            }

            float t = EaseOutCubic(windowAnimProgress_);

            // 透明度：0 → 255
            BYTE alpha = static_cast<BYTE>(255.0f * t);
            SetLayeredWindowAttributes(hwnd_, 0, alpha, LWA_ALPHA);

            // Y 偏移：kOpenSlideOffset → 0（上滑入位），窗口尺寸不变
            int offsetY = static_cast<int>(kOpenSlideOffset * (1.0f - t));
            SetWindowPos(hwnd_, nullptr,
                         windowTargetX_, windowTargetY_ + offsetY,
                         (int)windowTargetWidth_, (int)windowTargetHeight_,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);

            if (!windowAnimating_) {
                ShowOwnedPopups(hwnd_, TRUE);
                if (!toolWindow_) SetForegroundWindow(hwnd_);
                Invalidate();
            }
            return 0;
        }
        if (wParam == kWindowCloseAnimTimerId && windowAnimating_ && windowClosing_) {
            LARGE_INTEGER now, freq;
            QueryPerformanceCounter(&now);
            QueryPerformanceFrequency(&freq);
            float elapsedMs = static_cast<float>(now.QuadPart - windowAnimStartTick_.QuadPart)
                              * 1000.0f / static_cast<float>(freq.QuadPart);
            windowAnimProgress_ = elapsedMs / kWindowCloseAnimDurationMs;
            if (windowAnimProgress_ >= 1.0f) {
                KillTimer(hwnd_, kWindowCloseAnimTimerId);
                windowAnimating_ = false;
                windowClosing_ = false;
                DestroyWindow(hwnd_);
                return 0;
            }

            float t = windowAnimProgress_;
            float scale = 1.0f;
            if (t < kClosePeakPhase) {
                float local = EaseOutCubic(t / kClosePeakPhase);
                scale = 1.0f + (kPopPeakScale - 1.0f) * local;
            } else {
                float local = EaseInCubic((t - kClosePeakPhase) / (1.0f - kClosePeakPhase));
                scale = kPopPeakScale + (kCloseEndScale - kPopPeakScale) * local;
            }

            float fadeP = Clamp01((t - kCloseFadeStart) / (1.0f - kCloseFadeStart));
            BYTE alpha = (BYTE)(255.0f * (1.0f - fadeP));
            SetLayeredWindowAttributes(hwnd_, 0, alpha, LWA_ALPHA);

            float centerX = windowCloseStartX_ + windowTargetWidth_ / 2.0f;
            float centerY = windowCloseStartY_ + windowTargetHeight_ / 2.0f;
            SetWindowScaleAtCenter(hwnd_, centerX, centerY, windowTargetWidth_, windowTargetHeight_, scale);
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        if (root_) {
            ForEachWidget(root_.get(), [&](Widget* w) {
                auto* ti = dynamic_cast<TextInputWidget*>(w);
                if (ti) ti->focused = false;
                auto* ta = dynamic_cast<TextAreaWidget*>(w);
                if (ta) ta->focused = false;
                auto* cw = dynamic_cast<CustomWidget*>(w);
                if (cw) cw->focused = false;
            });
        }
        UpdateCaretBlinkTimer();
        Invalidate();
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd_, &pt);
        OnMouseWheel((float)pt.x, (float)pt.y, GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;
    }
    case WM_RBUTTONUP: {
        float dx = (float)GET_X_LPARAM(lParam) / dpiScale_;
        float dy = (float)GET_Y_LPARAM(lParam) / dpiScale_;
        if (onRightClick) onRightClick(dx, dy);
        Invalidate();
        return 0;
    }

    case WM_CHAR:
        // Don't forward Tab char (handled in WM_KEYDOWN)
        if (wParam == '\t') return 0;
        // Forward to focused widget
        if (focusedWidget_ && focusedWidget_->OnKeyChar((wchar_t)wParam)) {
            Invalidate();
            return 0;
        }
        break;

    case WM_KEYDOWN: {
        int vk = (int)wParam;
        if (DispatchKeyDown(vk)) return 0;
        if (onKey) onKey(vk);
        break;
    }

    case WM_DROPFILES: OnDropFiles(reinterpret_cast<HDROP>(wParam)); return 0;

    case WM_SETCURSOR:
        switch (LOWORD(lParam)) {
        case HTCLIENT: {
            // Splitter: resize cursor during drag or hover over bar
            if (root_) {
                bool splitterCursor = false;
                std::function<bool(Widget*)> checkSplitter = [&](Widget* w) -> bool {
                    auto* sp = dynamic_cast<SplitterWidget*>(w);
                    if (sp) {
                        LPCTSTR cursor = sp->IsVertical() ? IDC_SIZENS : IDC_SIZEWE;

                        // Always show resize cursor while dragging
                        if (sp->IsDragging()) { SetCursor(LoadCursor(nullptr, cursor)); return true; }

                        // Check hover over bar
                        POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd_, &pt);
                        float mx = (float)pt.x / dpiScale_, my = (float)pt.y / dpiScale_;
                        if (!sp->IsVertical()) {
                            float barX = sp->ContentLeft() + (sp->ContentWidth() - 5.0f) * sp->Ratio();
                            if (mx >= barX && mx <= barX + 5.0f) { SetCursor(LoadCursor(nullptr, cursor)); return true; }
                        } else {
                            float barY = sp->ContentTop() + (sp->ContentHeight() - 5.0f) * sp->Ratio();
                            if (my >= barY && my <= barY + 5.0f) { SetCursor(LoadCursor(nullptr, cursor)); return true; }
                        }
                    }
                    for (auto& c : w->Children())
                        if (checkSplitter(c.get())) return true;
                    return false;
                };
                splitterCursor = checkSplitter(root_.get());
                if (splitterCursor) return TRUE;
            }
            if (hoveredWidget_) {
                if (dynamic_cast<TextInputWidget*>(hoveredWidget_)) {
                    SetCursor(LoadCursor(nullptr, IDC_IBEAM)); return TRUE;
                }
                auto* ta = dynamic_cast<TextAreaWidget*>(hoveredWidget_);
                if (ta) {
                    // Arrow cursor on scrollbar area, I-beam on text area
                    POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd_, &pt);
                    float mx = (float)pt.x / dpiScale_;
                    if (ta->NeedsScrollbar() && mx >= ta->rect.right - 8) {
                        SetCursor(LoadCursor(nullptr, IDC_ARROW)); return TRUE;
                    }
                    SetCursor(LoadCursor(nullptr, IDC_IBEAM)); return TRUE;
                }
            }
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            return TRUE;
        }
        case HTLEFT:
        case HTRIGHT:
            SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
            return TRUE;
        case HTTOP:
        case HTBOTTOM:
            SetCursor(LoadCursor(nullptr, IDC_SIZENS));
            return TRUE;
        case HTTOPLEFT:
        case HTBOTTOMRIGHT:
            SetCursor(LoadCursor(nullptr, IDC_SIZENWSE));
            return TRUE;
        case HTTOPRIGHT:
        case HTBOTTOMLEFT:
            SetCursor(LoadCursor(nullptr, IDC_SIZENESW));
            return TRUE;
        default:
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            return TRUE;
        }

    case WM_ERASEBKGND: return 1;

    case WM_APP + 100: {
        // Menu item clicked (from popup window)
        int itemId = (int)wParam;
        activeMenu_ = nullptr;  // menu already closed itself
        if (onMenuItemClick) onMenuItemClick(itemId);
        Invalidate();
        return 0;
    }
    case WM_APP + 120: {
        // InvokeSync: 跨线程 SendMessage 来的 "在 UI 线程上执行 fn(ud)" 请求
        auto* req = reinterpret_cast<UiInvokeReq*>(lParam);
        if (req && req->fn) req->fn(req->ud);
        return 0;
    }
    case kMsgStartupReveal: {
        if (startupRevealPending_) {
            SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);

            if (borderless_) {
                MARGINS margins = maximized_ ? MARGINS{0, 0, 0, 0} : MARGINS{0, 0, 1, 0};
                DwmExtendFrameIntoClientArea(hwnd_, &margins);
            }

            SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            InvalidateRect(hwnd_, nullptr, FALSE);
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            startupRevealPending_ = false;
        }
        startupRevealPosted_ = false;
        return 0;
    }

    case WM_CLOSE:
        if (onClose) onClose();
        DestroyWindow(hwnd_);
        return 0;

    case WM_DESTROY: {
        if (caretBlinkTimerRunning_) {
            KillTimer(hwnd_, kCaretBlinkTimerId);
            caretBlinkTimerRunning_ = false;
        }
        HWND oldHwnd = hwnd_;
        hwnd_ = nullptr;
        auto& ctx = GetContext();
        // 先保存 id 并重置成员，再 RemoveWindow（RemoveWindow 会析构 this）。
        // 不能在 RemoveWindow 之后访问任何成员，否则是 use-after-free。
        uint64_t id = windowId;
        windowId = 0;
        // 清掉 HWND 的 userdata，防止 WM_NCDESTROY 再拿到已释放的 self
        if (oldHwnd) SetWindowLongPtrW(oldHwnd, GWLP_USERDATA, 0);
        if (!ctx.IsShuttingDown()) {
            if (id) ctx.RemoveWindow(id);          // this 在此之后失效
            if (!ctx.HasWindows()) PostQuitMessage(0);
        }
        return 0;
    }
    }

    // Borderless NC handling
    if (borderless_) {
        switch (msg) {
        case WM_NCCALCSIZE: return OnNcCalcSize(wParam, lParam);
        case WM_NCACTIVATE: return DefWindowProcW(hwnd_, WM_NCACTIVATE, wParam, -1);
        case WM_NCPAINT: return 0;
        case WM_NCHITTEST: return OnNcHitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        case WM_NCLBUTTONDBLCLK:
            if (wParam == HTCAPTION) {
                ShowWindow(hwnd_, maximized_ ? SW_RESTORE : SW_MAXIMIZE);
                return 0;
            }
            break;
        }
    }

    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

// ---- Paint ----

void UiWindowImpl::OnPaint() {
    if (!renderer_.RT()) return;

    // 窗口开/关动画期间不重绘，DWM 负责缩放已有内容
    if (windowAnimating_) {
        ValidateRect(hwnd_, nullptr);
        return;
    }

    // 移动期间正常渲染（不跳过）

    // Tick property animations
    bool animsRunning = Animations().Tick();

    // 正常绘制
    renderer_.BeginDraw();
    if (bgMode_ == 0) {
        renderer_.Clear(theme::kWindowBg());
    } else {
        /* 无背景擦除模式：透明清空，依赖 widget 自己画满整个客户区。
         * SetWindowPos 扩大窗口时不会闪主题色。 */
        renderer_.Clear({0, 0, 0, 0});
    }

    if (root_) {
        Viewport() = root_->rect;
        root_->DrawTree(renderer_);
        root_->DrawOverlays(renderer_);
    }

    // Debug highlight: draw red border around widget with matching ID
    if (!debugHighlightId_.empty() && root_) {
        Widget* target = root_->FindById(debugHighlightId_);
        if (target) {
            D2D1_COLOR_F red = {1.0f, 0.0f, 0.0f, 0.9f};
            D2D1_RECT_F rc = target->rect;
            renderer_.DrawRect(rc, red, 2.0f);
            /* 内边距 1px 再画一圈，确保显眼 */
            D2D1_RECT_F inner = {rc.left + 2, rc.top + 2, rc.right - 2, rc.bottom - 2};
            D2D1_COLOR_F yellow = {1.0f, 1.0f, 0.0f, 0.5f};
            renderer_.DrawRect(inner, yellow, 1.0f);
        }
    }

    // Tooltip rendering
    if (tooltipVisible_ && tooltipWidget_ && !tooltipWidget_->tooltip.empty()) {
        const auto& text = tooltipWidget_->tooltip;
        float fontSize = theme::kFontSizeSmall;
        float padH = 10.0f, padV = 5.0f;

        /* 在 BeginDraw 之外测量文字宽度（避免 RT 状态影响） */
        float textW = fontSize * 0.65f * static_cast<float>(text.size());  /* 估算 */
        /* 用 DWrite 精确测量 */
        {
            IDWriteFactory* dwf = renderer_.DWFactory();
            IDWriteTextFormat* fmt = nullptr;
            if (dwf) {
                dwf->CreateTextFormat(theme::kFontFamily, nullptr,
                    DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL, fontSize, L"", &fmt);
                if (fmt) {
                    IDWriteTextLayout* layout = nullptr;
                    dwf->CreateTextLayout(text.c_str(), (UINT32)text.size(),
                        fmt, 1000.0f, 100.0f, &layout);
                    if (layout) {
                        DWRITE_TEXT_METRICS metrics = {};
                        layout->GetMetrics(&metrics);
                        textW = metrics.width;
                        layout->Release();
                    }
                    fmt->Release();
                }
            }
        }

        float tipW = textW + padH * 2;
        float tipH = fontSize + padV * 2 + 4.0f;

        /* 在 widget 下方居中显示 */
        float cx = (tooltipWidget_->rect.left + tooltipWidget_->rect.right) * 0.5f;
        float tipX = cx - tipW * 0.5f;
        float tipY = tooltipWidget_->rect.bottom + 4.0f;

        /* 确保不超出窗口 */
        D2D1_SIZE_F winSize = renderer_.RT()->GetSize();
        if (tipX + tipW > winSize.width - 4) tipX = winSize.width - 4 - tipW;
        if (tipX < 4) tipX = 4;
        if (tipY + tipH > winSize.height - 4) {
            tipY = tooltipWidget_->rect.top - tipH - 4.0f;
        }

        D2D1_RECT_F bg = {tipX, tipY, tipX + tipW, tipY + tipH};
        /* 跟随主题：深色模式深底，浅色模式浅底 */
        D2D1_COLOR_F bgColor, borderColor;
        if (theme::IsDark()) {
            bgColor     = {0.15f, 0.15f, 0.18f, 0.95f};
            borderColor = {0.30f, 0.30f, 0.35f, 0.80f};
        } else {
            bgColor     = {0.98f, 0.98f, 0.98f, 0.97f};
            borderColor = {0.70f, 0.70f, 0.72f, 0.80f};
        }
        renderer_.FillRoundedRect(bg, 4.0f, 4.0f, bgColor);
        renderer_.DrawRoundedRect(bg, 4.0f, 4.0f, borderColor, 0.5f);

        D2D1_RECT_F textRect = {tipX, tipY + padV, tipX + tipW, tipY + tipH - padV};
        renderer_.DrawText(text, textRect, theme::kBtnText(), fontSize,
                           DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    if (borderless_ && !maximized_) {
        D2D1_SIZE_F size = renderer_.RT()->GetSize();
        D2D1_RECT_F border = {0, 0, size.width, size.height};
        renderer_.DrawRect(border, theme::kWindowBorder(), theme::kBorderWidth);
    }

    // Toast notification
    DrawToast(renderer_);

    HRESULT hr = renderer_.EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) renderer_.CreateRenderTarget(hwnd_);

    // Keep painting while property animations are active
    if (animsRunning) Invalidate();

    // Ensure timer is running for continuous animations (indeterminate progress, etc.)
    UpdateToggleAnimTimer();

    /* loading spinner 持续动画：检查是否有 ImageView 处于 loading 状态 */
    if (root_) {
        bool anyLoading = false;
        ForEachWidget(root_.get(), [&](Widget* w) {
            auto* iv = dynamic_cast<ImageViewWidget*>(w);
            if (iv && iv->IsLoading()) anyLoading = true;
        });
        if (anyLoading) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    if (startupRevealPending_ && hr == S_OK && !startupRevealPosted_) {
        startupRevealPosted_ = true;
        PostMessageW(hwnd_, kMsgStartupReveal, 0, 0);
    }
}

// 创建窗口拖动缓存
void UiWindowImpl::CreateDragCache() {
    if (!renderer_.RT()) return;

    auto* mainCtx = renderer_.RT();
    D2D1_SIZE_F size = mainCtx->GetSize();

    // 获取当前渲染目标的 DPI
    float dpiX, dpiY;
    mainCtx->GetDpi(&dpiX, &dpiY);

    // 保存 DPI 和尺寸
    dragCacheDpi_ = dpiX;
    dragCacheWidth_ = size.width;
    dragCacheHeight_ = size.height;

    // 创建兼容渲染目标
    ComPtr<ID2D1BitmapRenderTarget> bitmapRenderTarget;
    D2D1_SIZE_F desiredSize = {size.width, size.height};
    HRESULT hr = mainCtx->CreateCompatibleRenderTarget(
        &desiredSize,
        nullptr,
        nullptr,
        D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE,
        bitmapRenderTarget.GetAddressOf());

    if (FAILED(hr) || !bitmapRenderTarget) return;

    // 设置 DPI
    bitmapRenderTarget->SetDpi(dpiX, dpiY);

    // 获取兼容渲染目标的 DeviceContext 接口
    ComPtr<ID2D1DeviceContext> cacheCtx;
    if (FAILED(bitmapRenderTarget->QueryInterface(cacheCtx.GetAddressOf()))) return;

    // 保存原始渲染目标
    ComPtr<ID2D1DeviceContext> originalCtx(renderer_.RT());

    // 临时切换 Renderer 到兼容渲染目标
    renderer_.SetTarget(cacheCtx.Get());

    // 绘制窗口内容到兼容渲染目标
    cacheCtx->BeginDraw();
    if (bgMode_ == 0) {
        cacheCtx->Clear(theme::kWindowBg());
    } else {
        cacheCtx->Clear(D2D1::ColorF(0, 0, 0, 0));
    }

    if (root_) {
        D2D1_RECT_F originalViewport = Viewport();
        Viewport() = root_->rect;
        root_->DrawTree(renderer_);
        Viewport() = originalViewport;
    }

    cacheCtx->EndDraw();

    // 恢复原始渲染目标
    renderer_.SetTarget(originalCtx.Get());

    // 从兼容渲染目标获取位图
    bitmapRenderTarget->GetBitmap(dragCacheBitmap_.GetAddressOf());
}

// 释放窗口拖动缓存
void UiWindowImpl::ReleaseDragCache() {
    dragCacheBitmap_.Reset();
    dragCacheWidth_ = 0.0f;
    dragCacheHeight_ = 0.0f;
    dragCacheDpi_ = 96.0f;
}

void UiWindowImpl::OnResize(UINT width, UINT height) {
    renderer_.Resize(width, height);
    LayoutRoot();
    if (onResize) onResize((int)width, (int)height);
}

void UiWindowImpl::OnDpiChanged(UINT dpi, const RECT* suggested) {
    dpi_ = dpi;
    dpiScale_ = (float)dpi / 96.0f;
    renderer_.CreateRenderTarget(hwnd_);
    SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                 suggested->right - suggested->left, suggested->bottom - suggested->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

// ---- Mouse events ----

void UiWindowImpl::ShowMenu(ContextMenuPtr menu, float x, float y) {
    if (activeMenu_) activeMenu_->Close();
    activeMenu_ = std::move(menu);
    if (activeMenu_ && hwnd_) {
        // Convert widget coords to screen coords
        POINT pt = {(LONG)(x * dpiScale_), (LONG)(y * dpiScale_)};
        ClientToScreen(hwnd_, &pt);
        activeMenu_->ShowPopup(hwnd_, pt.x, pt.y);
    }
}

void UiWindowImpl::CloseMenu() {
    if (activeMenu_) { activeMenu_->Close(); activeMenu_ = nullptr; }
    Invalidate();
}

void UiWindowImpl::OnMouseMove(float x, float y) {
    float dx = x / dpiScale_, dy = y / dpiScale_;

    // If a popup menu is open, clicking in the main window should close it
    // (the popup's WM_ACTIVATE handler closes it when focus shifts)

    if (pressedWidget_) {
        MouseEvent e{dx, dy, 0, true};
        pressedWidget_->OnMouseMove(e);
        Invalidate();
        return;
    }

    if (root_) {
        auto* hit = root_->HitTest(dx, dy);
        if (hoveredWidget_ != hit) {
            // Notify previous widget it lost hover
            if (hoveredWidget_) {
                hoveredWidget_->hovered = false;
                MouseEvent leaveE{dx, dy};
                hoveredWidget_->OnMouseMove(leaveE);
            }
            if (hit) hit->hovered = true;
            hoveredWidget_ = hit;

            // Tooltip: reset on widget change, schedule timer for delayed show
            tooltipVisible_ = false;
            tooltipWidget_ = nullptr;
            if (tooltipTimerId_) { KillTimer(hwnd_, tooltipTimerId_); tooltipTimerId_ = 0; }
            if (hit && !hit->tooltip.empty()) {
                hoverStartTick_ = GetTickCount();
                tooltipTimerId_ = SetTimer(hwnd_, kTooltipTimerId, kTooltipDelayMs, nullptr);
            }
        }

        mouseX_ = dx; mouseY_ = dy;

        // Forward move to hovered widget (for internal tracking like tab hover)
        if (hit) {
            MouseEvent e{dx, dy};
            hit->OnMouseMove(e);
        }

        ForEachWidget(root_.get(), [&](Widget* w) {
            auto* cb = dynamic_cast<ComboBoxWidget*>(w);
            if (cb && cb->IsOpen()) {
                MouseEvent e{dx, dy};
                cb->OnMouseMove(e);
            }
        });

        // Update ScrollView scrollbar hover state (overlay scrollbar)
        ForEachWidget(root_.get(), [&](Widget* w) {
            auto* sv = dynamic_cast<ScrollViewWidget*>(w);
            if (sv && sv->visible && sv->NeedsScrollbar()) {
                MouseEvent e{dx, dy};
                sv->OnMouseMove(e);
            }
        });
    }
    Invalidate();
    UpdateToggleAnimTimer();  // start timer if Slider thumb needs animation

    TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
    TrackMouseEvent(&tme);
}

void UiWindowImpl::OnMouseDown(float x, float y) {
    float dx = x / dpiScale_, dy = y / dpiScale_;

    // Hide tooltip on click
    tooltipVisible_ = false; tooltipWidget_ = nullptr;
    if (tooltipTimerId_) { KillTimer(hwnd_, tooltipTimerId_); tooltipTimerId_ = 0; }

    // Close any open context menu on left click
    if (activeMenu_) CloseMenu();

    if (!root_) return;

    // Set focus to clicked focusable widget, or clear focus
    // Mouse click hides focus ring (only keyboard Tab shows it)
    {
        showFocusRing_ = false;
        ShowFocusRing() = false;  // mouse click hides focus ring
        Widget* hit = root_->HitTest(dx, dy);
        Widget* target = hit;
        while (target && !target->focusable) target = target->Parent();
        SetFocus(target);

        // Clear NumberBox focus when clicking elsewhere
        ForEachWidget(root_.get(), [&](Widget* w) {
            auto* nb = dynamic_cast<NumberBoxWidget*>(w);
            if (nb && nb != target && nb != hit) {
                if (nb->focused) { nb->focused = false; }
            }
        });
    }

    // Check open ComboBox dropdowns
    {
        bool handledByDropdown = false;
        ForEachWidget(root_.get(), [&](Widget* w) {
            if (handledByDropdown) return;
            auto* cb = dynamic_cast<ComboBoxWidget*>(w);
            if (!cb || !cb->IsOpen()) return;

            auto dr = cb->DropdownRect();
            bool inDropdown = (dx >= dr.left && dx < dr.right &&
                               dy >= dr.top && dy < dr.bottom);
            bool inCombo = cb->Contains(dx, dy);

            if (inDropdown) {
                int idx = (int)((dy - dr.top) / cb->ItemHeight());
                if (idx >= 0 && idx < cb->ItemCount()) {
                    cb->SetSelectedIndex(idx);
                    if (cb->onSelectionChanged) cb->onSelectionChanged(idx);
                }
                cb->Close();
                handledByDropdown = true;
            } else if (inCombo) {
                cb->Close();
                handledByDropdown = true;
            } else {
                cb->Close();
            }
        });

        if (handledByDropdown) {
            UpdateCaretBlinkTimer();
            Invalidate();
            return;
        }
    }

    // Check open Flyouts — dismiss on outside click, forward on inside click
    {
        bool handledByFlyout = false;
        ForEachWidget(root_.get(), [&](Widget* w) {
            if (handledByFlyout) return;
            auto* fw = dynamic_cast<FlyoutWidget*>(w);
            if (!fw || !fw->IsOpen()) return;

            MouseEvent e{dx, dy, 0, true};
            if (fw->OnMouseDown(e)) {
                handledByFlyout = true;
                pressedWidget_ = fw;  // so OnMouseUp reaches the flyout
            }
        });

        if (handledByFlyout) {
            UpdateCaretBlinkTimer();
            Invalidate();
            return;
        }
    }

    // Check ScrollView scrollbar clicks (overlay scrollbar area)
    {
        bool handledByScrollbar = false;
        ForEachWidget(root_.get(), [&](Widget* w) {
            if (handledByScrollbar) return;
            auto* sv = dynamic_cast<ScrollViewWidget*>(w);
            if (!sv || !sv->visible || !sv->NeedsScrollbar()) return;
            if (!sv->Contains(dx, dy)) return;
            // Only intercept clicks in the scrollbar track region
            if (dx < sv->rect.right - 10) return;

            MouseEvent e{dx, dy, 0, true};
            if (sv->OnMouseDown(e)) {
                handledByScrollbar = true;
                pressedWidget_ = sv;
                SetCapture(hwnd_);
            }
        });

        if (handledByScrollbar) {
            UpdateCaretBlinkTimer();
            Invalidate();
            return;
        }
    }

    // Hit test and dispatch
    auto* hit = root_->HitTest(dx, dy);

    // Check if a Splitter bar was clicked (Splitter intercepts before children)
    {
        Widget* w = hit;
        while (w) {
            auto* sp = dynamic_cast<SplitterWidget*>(w);
            if (sp) {
                MouseEvent e{dx, dy, 0, true};
                if (sp->OnMouseDown(e)) {
                    pressedWidget_ = sp;
                    SetCapture(hwnd_);
                    Invalidate();
                    return;
                }
            }
            w = w->Parent();
        }
    }

    if (hit) {
        hit->pressed = true;
        pressedWidget_ = hit;

        if (dynamic_cast<SliderWidget*>(hit) ||
            dynamic_cast<ScrollViewWidget*>(hit) ||
            dynamic_cast<ImageViewWidget*>(hit)) {
            SetCapture(hwnd_);
        }

        MouseEvent e{dx, dy, 0, true};
        hit->OnMouseDown(e);
    }
    UpdateCaretBlinkTimer();
    Invalidate();
}

void UiWindowImpl::OnMouseUp(float x, float y) {
    float dx = x / dpiScale_, dy = y / dpiScale_;

    if (pressedWidget_) {
        pressedWidget_->pressed = false;

        if (pressedWidget_->Contains(dx, dy) && pressedWidget_->onClick) {
            pressedWidget_->onClick();
        }

        MouseEvent e{dx, dy, 0, false};
        pressedWidget_->OnMouseUp(e);

        if (dynamic_cast<SliderWidget*>(pressedWidget_) ||
            dynamic_cast<ScrollViewWidget*>(pressedWidget_) ||
            dynamic_cast<ImageViewWidget*>(pressedWidget_) ||
            dynamic_cast<SplitterWidget*>(pressedWidget_)) {
            ReleaseCapture();
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
        }

        pressedWidget_ = nullptr;
    }

    UpdateToggleAnimTimer();
    Invalidate();
}

void UiWindowImpl::OnMouseWheel(float x, float y, int delta) {
    float dx = x / dpiScale_, dy = y / dpiScale_;

    if (root_) {
        // HitTest to find deepest widget, then bubble up looking for scroll handler
        Widget* hit = root_->HitTest(dx, dy);
        bool handled = false;
        MouseEvent e{dx, dy, (float)delta};

        for (Widget* w = hit; w && !handled; w = w->Parent()) {
            if (auto* ta = dynamic_cast<TextAreaWidget*>(w)) {
                if (ta->visible && ta->NeedsScrollbar()) {
                    handled = ta->OnMouseWheel(e);
                }
            }
            else if (auto* iv = dynamic_cast<ImageViewWidget*>(w)) {
                if (iv->visible) {
                    handled = iv->OnMouseWheel(e);
                }
            }
            else if (auto* sv = dynamic_cast<ScrollViewWidget*>(w)) {
                if (sv->visible) {
                    handled = sv->OnMouseWheel(e);
                    if (handled) sv->DoLayout();
                }
            }
        }
        if (handled) Invalidate();
    }
}

// ---- Debug simulation (DIP coords → pixel → existing private handlers) ----

void UiWindowImpl::SimMouseMove(float dipX, float dipY) {
    OnMouseMove(dipX * dpiScale_, dipY * dpiScale_);
}
void UiWindowImpl::SimMouseDown(float dipX, float dipY) {
    OnMouseDown(dipX * dpiScale_, dipY * dpiScale_);
}
void UiWindowImpl::SimMouseUp(float dipX, float dipY) {
    OnMouseUp(dipX * dpiScale_, dipY * dpiScale_);
}
void UiWindowImpl::SimMouseWheel(float dipX, float dipY, float delta) {
    OnMouseWheel(dipX * dpiScale_, dipY * dpiScale_, (int)delta);
}
void UiWindowImpl::SimRightClick(float dipX, float dipY) {
    // Matches WM_RBUTTONUP: just fires onRightClick callback.
    if (onRightClick) onRightClick(dipX, dipY);
    Invalidate();
}
void UiWindowImpl::SimKeyDown(int vk) {
    DispatchKeyDown(vk);
}

// 共用的 WM_KEYDOWN 分发逻辑。返回 true 表示事件被消费。
bool UiWindowImpl::DispatchKeyDown(int vk) {
    // 0. Active dialog intercepts all keys
    if (activeDialog_ && activeDialog_->IsActive()) {
        if (activeDialog_->OnKeyDown(vk)) { Invalidate(); return true; }
    }

    // 1. Tab / Shift+Tab → focus traversal (only when enabled)
    if (vk == VK_TAB && tabNavigationEnabled_) {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        showFocusRing_ = true;
        ShowFocusRing() = true;
        FocusNext(shift);
        Invalidate();
        return true;
    }

    // 2. Shortcuts (Ctrl+Key, Alt+Key) —— GetKeyState 读真实键盘状态，
    //    sim 注入时若没有同时按 Ctrl/Alt 这条分支会跳过，这是期望行为。
    if (!shortcuts_.empty()) {
        int mods = 0;
        if (GetKeyState(VK_CONTROL) & 0x8000) mods |= 1;  // MOD_CTRL
        if (GetKeyState(VK_SHIFT)   & 0x8000) mods |= 2;  // MOD_SHIFT
        if (GetKeyState(VK_MENU)    & 0x8000) mods |= 4;  // MOD_ALT
        for (auto& sc : shortcuts_) {
            if (sc.vk == vk && sc.modifiers == mods) {
                sc.callback();
                Invalidate();
                return true;
            }
        }
    }

    // 3. Enter / Space → activate focused widget
    if (focusedWidget_ && (vk == VK_RETURN || vk == VK_SPACE)) {
        if (auto* cb = dynamic_cast<CheckBoxWidget*>(focusedWidget_)) {
            if (vk == VK_SPACE) {
                cb->SetChecked(!cb->Checked());
                if (cb->onValueChanged) cb->onValueChanged(cb->Checked());
                UpdateToggleAnimTimer();
                Invalidate();
                return true;
            }
        }
        if (auto* tg = dynamic_cast<ToggleWidget*>(focusedWidget_)) {
            if (vk == VK_SPACE) {
                tg->SetOn(!tg->On());
                if (tg->onValueChanged) tg->onValueChanged(tg->On());
                UpdateToggleAnimTimer();
                Invalidate();
                return true;
            }
        }
        if (focusedWidget_->onClick) {
            focusedWidget_->onClick();
            Invalidate();
            return true;
        }
    }

    // 4. Arrow keys for Slider (left/right)
    if (focusedWidget_ && (vk == VK_LEFT || vk == VK_RIGHT)) {
        if (auto* sl = dynamic_cast<SliderWidget*>(focusedWidget_)) {
            float step = (vk == VK_RIGHT) ? 1.0f : -1.0f;
            sl->SetValue(sl->Value() + step);
            if (sl->onFloatChanged) sl->onFloatChanged(sl->Value());
            Invalidate();
            return true;
        }
    }

    // 5. Arrow keys for RadioButton group (up/down)
    if (focusedWidget_ && (vk == VK_UP || vk == VK_DOWN)) {
        if (auto* rb = dynamic_cast<RadioButtonWidget*>(focusedWidget_)) {
            Widget* parent = rb->Parent();
            if (parent) {
                std::vector<RadioButtonWidget*> group;
                for (auto& c : parent->Children()) {
                    auto* r = dynamic_cast<RadioButtonWidget*>(c.get());
                    if (r && r->Group() == rb->Group()) group.push_back(r);
                }
                int cur = 0;
                for (int i = 0; i < (int)group.size(); i++)
                    if (group[i] == rb) { cur = i; break; }
                int next = vk == VK_DOWN ? (cur + 1) % (int)group.size()
                                         : (cur - 1 + (int)group.size()) % (int)group.size();
                group[next]->SetSelected(true);
                if (group[next]->onValueChanged) group[next]->onValueChanged(true);
                SetFocus(group[next]);
                UpdateToggleAnimTimer();
                Invalidate();
                return true;
            }
        }
    }

    // 6. Escape → close ComboBox dropdown
    if (focusedWidget_ && vk == VK_ESCAPE) {
        if (auto* combo = dynamic_cast<ComboBoxWidget*>(focusedWidget_)) {
            if (combo->IsOpen()) { combo->Close(); Invalidate(); return true; }
        }
    }

    // 7. Forward to focused widget's generic OnKeyDown
    if (focusedWidget_ && focusedWidget_->OnKeyDown(vk)) {
        Invalidate();
        return true;
    }

    return false;
}

// ---- UI thread marshaling ----
void UiWindowImpl::InvokeSync(void (*fn)(void*), void* ud) {
    if (!fn) return;
    if (!hwnd_) { fn(ud); return; }
    // 若已在 UI 线程（拥有该 HWND 的线程），直接调用——避免 SendMessage 死锁。
    DWORD hwndTid = GetWindowThreadProcessId(hwnd_, nullptr);
    if (hwndTid == GetCurrentThreadId()) {
        fn(ud);
        return;
    }
    UiInvokeReq req{fn, ud};
    SendMessageW(hwnd_, WM_APP + 120, 0, reinterpret_cast<LPARAM>(&req));
}
void UiWindowImpl::SimKeyChar(wchar_t ch) {
    if (focusedWidget_ && focusedWidget_->OnKeyChar(ch)) {
        Invalidate();
    }
}

void UiWindowImpl::OnDropFiles(HDROP hDrop) {
    wchar_t path[MAX_PATH]{};
    UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < count; i++) {
        if (DragQueryFileW(hDrop, i, path, MAX_PATH)) {
            if (onDrop) onDrop(path);
        }
    }
    DragFinish(hDrop);
}

// ---- NC handling (borderless) ----

LRESULT UiWindowImpl::OnNcCalcSize(WPARAM wParam, LPARAM lParam) {
    if (!wParam) return DefWindowProcW(hwnd_, WM_NCCALCSIZE, wParam, lParam);
    /* 无边框：客户区 = 整个窗口（不减非客户区）。
     * 最大化尺寸由 WM_GETMINMAXINFO 限制到工作区。 */
    return 0;
}

LRESULT UiWindowImpl::OnNcHitTest(int sx, int sy) {
    POINT pt = {sx, sy};
    ScreenToClient(hwnd_, &pt);
    float x = (float)pt.x / dpiScale_;
    float y = (float)pt.y / dpiScale_;
    D2D1_SIZE_F size = renderer_.RT() ? renderer_.RT()->GetSize() : D2D1::SizeF(0, 0);
    float w = size.width, h = size.height;
    float border = theme::kResizeBorder;

    if (!maximized_ && resizable_) {
        bool l = x < border, r = x >= w - border, t = y < border, b = y >= h - border;

        // 四角和顶部/左右边缘始终优先 resize
        if (t && l) return HTTOPLEFT;    if (t && r) return HTTOPRIGHT;
        if (b && l) return HTBOTTOMLEFT; if (b && r) return HTBOTTOMRIGHT;
        if (t) return HTTOP;
        if (l) return HTLEFT;

        if (r) return HTRIGHT;

        // 底部边缘：先检查是否命中交互控件，是则让给控件
        if (b) {
            if (root_) {
                auto* hit = root_->HitTest(x, y);
                if (hit && hit != root_.get() && !dynamic_cast<TitleBarWidget*>(hit))
                    return HTCLIENT;
            }
            return HTBOTTOM;
        }
    }

    // Check if hitting a widget
    if (root_) {
        auto* hit = root_->HitTest(x, y);
        if (hit && hit != root_.get()) {
            if (dynamic_cast<TitleBarWidget*>(hit)) return HTCAPTION;
            /* dragWindow 属性：任意 widget 都能标记为"点击即拖动窗口"，
             * 无边框画布模式下可以给根 Panel 打上这个标让整个画布可拖。
             * 注意 HitTest 返回的是最深层子节点，所以内部的 Button 等
             * 交互控件不会被波及（除非它们自己也标了 dragWindow）。 */
            if (hit->dragWindow) return HTCAPTION;
            return HTCLIENT;
        }
    }

    // Title bar region fallback (first 36px)
    if (y < theme::kTitleBarHeight) return HTCAPTION;
    return HTCLIENT;
}

void UiWindowImpl::OnGetMinMaxInfo(MINMAXINFO* mmi) {
    int minW, minH;
    if (minWOverride_ > 0) {
        /* 用户显式覆盖（无边框画布等场景，可以小于主题默认） */
        minW = minWOverride_;
    } else {
        minW = (configWidth_ > 0 && configWidth_ < theme::kMinWidth) ? configWidth_ : theme::kMinWidth;
    }
    if (minHOverride_ > 0) {
        minH = minHOverride_;
    } else {
        minH = (configHeight_ > 0 && configHeight_ < theme::kMinHeight) ? configHeight_ : theme::kMinHeight;
    }
    mmi->ptMinTrackSize.x = (LONG)(minW * dpiScale_);
    mmi->ptMinTrackSize.y = (LONG)(minH * dpiScale_);

    /* 无边框窗口：限制最大化尺寸到工作区（扣除任务栏）。
     * 这样 WM_NCCALCSIZE 的 orig rect 就已经是工作区大小。 */
    if (borderless_ && hwnd_) {
        HMONITOR mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfoW(mon, &mi)) {
            mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
            mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
            mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
            mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
        }
    }
}

bool UiWindowImpl::HasFocusedTextInput() const {
    if (!root_) return false;

    bool focused = false;
    ForEachWidget(root_.get(), [&](Widget* w) {
        if (focused) return;
        auto* ti = dynamic_cast<TextInputWidget*>(w);
        if (ti && ti->focused) { focused = true; return; }
        auto* ta = dynamic_cast<TextAreaWidget*>(w);
        if (ta && ta->focused) { focused = true; return; }
        auto* cw = dynamic_cast<CustomWidget*>(w);
        if (cw && cw->focused) { focused = true; return; }
    });
    return focused;
}

void UiWindowImpl::UpdateCaretBlinkTimer() {
    if (!hwnd_) return;

    bool needTimer = HasFocusedTextInput();
    if (needTimer && !caretBlinkTimerRunning_) {
        UINT blinkMs = TextInputWidget::EffectiveCaretBlinkMs();
        if (SetTimer(hwnd_, kCaretBlinkTimerId, blinkMs, nullptr) != 0) {
            caretBlinkTimerRunning_ = true;
        }
    } else if (!needTimer && caretBlinkTimerRunning_) {
        KillTimer(hwnd_, kCaretBlinkTimerId);
        caretBlinkTimerRunning_ = false;
    }
}

// ---- Focus management ----

void UiWindowImpl::SetFocus(Widget* w) {
    if (focusedWidget_ == w) return;
    if (focusedWidget_) {
        focusedWidget_->SetFocused(false);
        // Sync legacy focused field for TextInput/TextArea
        if (auto* ti = dynamic_cast<TextInputWidget*>(focusedWidget_)) ti->focused = false;
        if (auto* ta = dynamic_cast<TextAreaWidget*>(focusedWidget_)) ta->focused = false;
    }
    focusedWidget_ = w;
    if (w) {
        w->SetFocused(true);
        if (auto* ti = dynamic_cast<TextInputWidget*>(w)) { ti->focused = true; ti->ResetCaretBlink(); }
        if (auto* ta = dynamic_cast<TextAreaWidget*>(w)) { ta->focused = true; ta->ResetCaretBlink(); }
    }
    UpdateCaretBlinkTimer();
}

void UiWindowImpl::ClearFocus() {
    SetFocus(nullptr);
}

void UiWindowImpl::FocusNext(bool reverse) {
    if (!root_ || !tabNavigationEnabled_) return;
    std::vector<Widget*> chain;
    root_->CollectFocusable(chain);
    if (chain.empty()) return;

    // Sort by tabIndex if specified (stable sort preserves tree order for equal/unset)
    std::stable_sort(chain.begin(), chain.end(), [](Widget* a, Widget* b) {
        int ta = a->tabIndex < 0 ? 10000 : a->tabIndex;
        int tb = b->tabIndex < 0 ? 10000 : b->tabIndex;
        return ta < tb;
    });

    if (!focusedWidget_) {
        SetFocus(reverse ? chain.back() : chain.front());
        return;
    }

    // Find current index
    int cur = -1;
    for (int i = 0; i < (int)chain.size(); i++) {
        if (chain[i] == focusedWidget_) { cur = i; break; }
    }

    int next;
    if (reverse) {
        next = (cur <= 0) ? (int)chain.size() - 1 : cur - 1;
    } else {
        next = (cur < 0 || cur >= (int)chain.size() - 1) ? 0 : cur + 1;
    }
    SetFocus(chain[next]);
}

// ---- Shortcut registration ----

void UiWindowImpl::RegisterShortcut(int modifiers, int vk, std::function<void()> cb) {
    shortcuts_.push_back({modifiers, vk, std::move(cb)});
}

// ---- Toast ----

void UiWindowImpl::ShowToast(const std::wstring& text, int durationMs, int position, int icon) {
    toastText_ = text;
    toastPos_ = position;
    toastIcon_ = icon;
    toastSlide_ = 0.0f;
    toastAlpha_ = 1.0f;
    toastPhase_ = 1;  /* slideIn */

    if (toastTimerId_) KillTimer(hwnd_, toastTimerId_);
    toastTimerId_ = SetTimer(hwnd_, kToastFadeTimerId, kToastFadeIntervalMs, nullptr);
    toastFading_ = false;

    /* 保存 hold 时长 */
    holdDurationMs_ = durationMs;
    holdElapsed_ = 0;
    Invalidate();
}

void UiWindowImpl::DrawToast(Renderer& r) {
    if (toastText_.empty() || toastPhase_ == 0) return;

    auto* rt = r.RT();
    if (!rt) return;

    D2D1_SIZE_F size = rt->GetSize();
    float fontSize = theme::kFontSizeNormal;

    float textW = r.MeasureTextWidth(toastText_, fontSize);
    float iconSize = fontSize + 2.0f;
    float iconGap = (toastIcon_ > 0) ? 8.0f : 0.0f;
    float iconSpace = (toastIcon_ > 0) ? (iconSize + iconGap) : 0.0f;
    float padH = 20.0f, padV = 10.0f;
    float boxW = textW + iconSpace + padH * 2;
    float boxH = fontSize + padV * 2;
    float cx = size.width / 2.0f;

    /* 计算目标 Y 和滑入方向偏移 */
    float targetY = 0, hideOffset = 0;
    if (toastPos_ == 0) {
        /* 上方：从顶部滑入 */
        targetY = 50.0f;
        hideOffset = -(boxH + 60.0f);
    } else if (toastPos_ == 1) {
        /* 中间 */
        targetY = (size.height - boxH) / 2.0f;
        hideOffset = 0;
    } else {
        /* 下方：从底部滑入 */
        targetY = size.height - boxH - 60.0f;
        hideOffset = boxH + 60.0f;
    }

    float slide = toastSlide_;
    /* ease out cubic */
    float t = 1.0f - (1.0f - slide) * (1.0f - slide) * (1.0f - slide);
    float y = targetY + hideOffset * (1.0f - t);
    float alpha = (toastPhase_ == 3) ? slide : 1.0f;  /* 滑出时同时淡出 */

    D2D1_RECT_F boxRect = { cx - boxW / 2, y, cx + boxW / 2, y + boxH };
    D2D1_COLOR_F bgColor = {0.15f, 0.15f, 0.18f, 0.92f * alpha};
    D2D1_COLOR_F textColor = {0.95f, 0.95f, 0.97f, alpha};

    r.FillRoundedRect(boxRect, 8.0f, 8.0f, bgColor);

    /* 绘制图标 */
    if (toastIcon_ > 0) {
        float ix = boxRect.left + padH;
        float iy = y + (boxH - iconSize) / 2.0f;
        float icx = ix + iconSize / 2.0f;
        float icy = iy + iconSize / 2.0f;
        float rad = iconSize / 2.0f;

        if (toastIcon_ == 1) {
            /* 绿色圆圈 + ✓ */
            D2D1_COLOR_F green = {0.3f, 0.85f, 0.4f, alpha};
            D2D1_RECT_F circleR = {icx - rad, icy - rad, icx + rad, icy + rad};
            r.DrawRoundedRect(circleR, rad, rad, green, 1.5f);
            r.DrawLine(icx - rad*0.3f, icy + rad*0.05f, icx - rad*0.0f, icy + rad*0.35f, green, 2.0f);
            r.DrawLine(icx - rad*0.0f, icy + rad*0.35f, icx + rad*0.4f, icy - rad*0.25f, green, 2.0f);
        } else if (toastIcon_ == 2) {
            /* 红色圆圈 + ✕ */
            D2D1_COLOR_F red = {0.95f, 0.35f, 0.35f, alpha};
            D2D1_RECT_F circleR = {icx - rad, icy - rad, icx + rad, icy + rad};
            r.DrawRoundedRect(circleR, rad, rad, red, 1.5f);
            float d = rad * 0.3f;
            r.DrawLine(icx - d, icy - d, icx + d, icy + d, red, 2.0f);
            r.DrawLine(icx + d, icy - d, icx - d, icy + d, red, 2.0f);
        } else if (toastIcon_ == 3) {
            /* 黄色三角 + ! */
            D2D1_COLOR_F yellow = {1.0f, 0.82f, 0.2f, alpha};
            r.DrawLine(icx, icy - rad*0.5f, icx - rad*0.5f, icy + rad*0.4f, yellow, 1.8f);
            r.DrawLine(icx - rad*0.5f, icy + rad*0.4f, icx + rad*0.5f, icy + rad*0.4f, yellow, 1.8f);
            r.DrawLine(icx + rad*0.5f, icy + rad*0.4f, icx, icy - rad*0.5f, yellow, 1.8f);
            r.DrawLine(icx, icy - rad*0.15f, icx, icy + rad*0.15f, yellow, 2.0f);
            r.DrawLine(icx, icy + rad*0.28f, icx + 0.1f, icy + rad*0.32f, yellow, 2.0f);
        }
    }

    /* 文字区域（图标右边） */
    D2D1_RECT_F textRect = {
        boxRect.left + padH + iconSpace, boxRect.top,
        boxRect.right - padH, boxRect.bottom
    };
    r.DrawText(toastText_, textRect, textColor, fontSize,
               DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_FONT_WEIGHT_NORMAL,
               DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

// ---- Debug: Highlight ----
void UiWindowImpl::SetDebugHighlight(const char* widgetId) {
    debugHighlightId_ = widgetId ? widgetId : "";
    Invalidate();
}

// ---- Debug: Screenshot ----
int UiWindowImpl::Screenshot(const wchar_t* outPath) {
    if (!renderer_.RT() || !root_) return -1;

    auto* ctx = renderer_.RT();
    auto pixelSize = ctx->GetPixelSize();
    int w = (int)pixelSize.width, h = (int)pixelSize.height;
    if (w <= 0 || h <= 0) return -2;

    /* 创建 CPU 可读位图 */
    D2D1_BITMAP_PROPERTIES1 cpuProps = {};
    cpuProps.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    cpuProps.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    ComPtr<ID2D1Bitmap1> cpuBmp;
    HRESULT hr = ctx->CreateBitmap(D2D1::SizeU(w, h), nullptr, 0, cpuProps, &cpuBmp);
    if (FAILED(hr)) return -3;

    /* 从 swap chain 回读 */
    ComPtr<ID2D1Bitmap1> target;
    ctx->GetTarget(reinterpret_cast<ID2D1Image**>(target.GetAddressOf()));
    if (!target) return -4;

    D2D1_POINT_2U pt = {0, 0};
    D2D1_RECT_U rc = {0, 0, (UINT32)w, (UINT32)h};
    hr = cpuBmp->CopyFromBitmap(&pt, target.Get(), &rc);
    if (FAILED(hr)) return -5;

    D2D1_MAPPED_RECT mapped;
    hr = cpuBmp->Map(D2D1_MAP_OPTIONS_READ, &mapped);
    if (FAILED(hr)) return -6;

    /* 用 WIC 编码为 PNG */
    auto& gctx = GetContext();
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IWICStream> stream;

    hr = gctx.WICFactory()->CreateStream(&stream);
    if (FAILED(hr)) { cpuBmp->Unmap(); return -7; }
    hr = stream->InitializeFromFilename(outPath, GENERIC_WRITE);
    if (FAILED(hr)) { cpuBmp->Unmap(); return -8; }
    hr = gctx.WICFactory()->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr)) { cpuBmp->Unmap(); return -9; }
    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) { cpuBmp->Unmap(); return -10; }
    hr = encoder->CreateNewFrame(&frame, nullptr);
    if (FAILED(hr)) { cpuBmp->Unmap(); return -11; }
    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) { cpuBmp->Unmap(); return -12; }
    hr = frame->SetSize(w, h);
    if (FAILED(hr)) { cpuBmp->Unmap(); return -13; }
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&fmt);
    if (FAILED(hr)) { cpuBmp->Unmap(); return -14; }
    hr = frame->WritePixels(h, mapped.pitch, mapped.pitch * h, mapped.bits);
    cpuBmp->Unmap();
    if (FAILED(hr)) return -15;
    hr = frame->Commit();
    if (FAILED(hr)) return -16;
    hr = encoder->Commit();
    if (FAILED(hr)) return -17;

    return 0; /* success */
}

} // namespace ui
