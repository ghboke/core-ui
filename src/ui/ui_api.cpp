/*
 * ui_api.cpp — C API bridge implementation
 *
 * Every extern "C" function: look up handle → cast → call C++ method.
 */
#include "ui_context.h"
#include "ui_window.h"
#include "ui.h"        // pulls in all widget/control/builder headers
#include "../../include/ui_core.h"

#include <windows.h>
#include <string>

// ---- Conversion helpers ----

static inline D2D1_COLOR_F ToD2D(UiColor c) { return {c.r, c.g, c.b, c.a}; }
static inline UiColor FromD2D(const D2D1_COLOR_F& c) { return {c.r, c.g, c.b, c.a}; }
static inline D2D1_RECT_F ToD2DRect(UiRect r) { return {r.left, r.top, r.right, r.bottom}; }

static thread_local std::wstring g_textInputRetBuf;
static thread_local std::wstring g_textAreaRetBuf;

static ui::Context& Ctx() { return ui::GetContext(); }

static ui::Widget* W(UiWidget h) { return Ctx().handles.LookupRaw(h); }

template<typename T>
static T* As(UiWidget h) { return dynamic_cast<T*>(W(h)); }

static ui::UiWindowImpl* Win(UiWindow h) { return Ctx().GetWindow(h); }

// Helper: create a widget, insert into handle table, return handle
static UiWidget Reg(ui::WidgetPtr w) {
    return Ctx().handles.Insert(std::move(w));
}

// ================================================================
// Initialization / shutdown / message loop
// ================================================================

extern "C" {

// ---- Version ----

UI_API void ui_core_version(int* major, int* minor, int* patch) {
    if (major) *major = UI_CORE_VERSION_MAJOR;
    if (minor) *minor = UI_CORE_VERSION_MINOR;
    if (patch) *patch = UI_CORE_VERSION_PATCH;
}

UI_API int ui_core_version_build(void) {
    return UI_CORE_VERSION_BUILD;
}

UI_API const char* ui_core_version_string(void) {
    return UI_CORE_VERSION_STRING;
}

UI_API int ui_init(void) {
    return Ctx().Init() ? 0 : -1;
}

UI_API int ui_init_with_theme(UiThemeMode mode) {
    int r = ui_init();
    if (r == 0) {
        theme::SetMode(mode == UI_THEME_LIGHT ? theme::Mode::Light : theme::Mode::Dark);
    }
    return r;
}

UI_API void ui_shutdown(void) {
    Ctx().Shutdown();
}

UI_API int ui_run(void) {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

UI_API void ui_quit(int exit_code) {
    PostQuitMessage(exit_code);
}

UI_API void ui_flush_gpu(UiWindow win) {
    auto* wn = Win(win);
    if (wn) wn->GetRenderer().FlushAndTrimGpu();
}

// ================================================================
// Window management
// ================================================================

UI_API UiWindow ui_window_create(const UiWindowConfig* config) {
    if (!config) return UI_INVALID;

    auto win = std::make_unique<ui::UiWindowImpl>();
    win->skipOpenAnimation_ = (config->skip_animation != 0);
    int initX = (config->x != 0 || config->y != 0) ? config->x : CW_USEDEFAULT;
    int initY = (config->x != 0 || config->y != 0) ? config->y : CW_USEDEFAULT;
    if (!win->Create(
            config->title ? config->title : L"",
            config->width > 0 ? config->width : 800,
            config->height > 0 ? config->height : 600,
            config->system_frame == 0,   /* 0 = borderless (default) */
            config->resizable != 0,
            config->accept_files != 0,
            initX, initY,
            config->tool_window != 0)) {
        return UI_INVALID;
    }

    // Set window icon from RGBA pixel data
    if (config->icon_pixels && config->icon_width > 0 && config->icon_height > 0) {
        win->SetIconFromPixels(
            static_cast<const uint8_t*>(config->icon_pixels),
            config->icon_width, config->icon_height);
    } else {
        // 未传 icon_pixels 时，自动从 exe 嵌入资源加载图标（rc 文件 ID=1）
        HINSTANCE hInst = GetModuleHandleW(nullptr);
        HICON big   = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                        GetSystemMetrics(SM_CXICON),
                                        GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
        HICON small = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                        GetSystemMetrics(SM_CXSMICON),
                                        GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
        HWND h = win->Handle();
        if (big)   SendMessageW(h, WM_SETICON, ICON_BIG,   (LPARAM)big);
        if (small) SendMessageW(h, WM_SETICON, ICON_SMALL, (LPARAM)small);
    }

    uint64_t id = Ctx().RegisterWindow(std::move(win));
    Ctx().GetWindow(id)->windowId = id;
    return id;
}

UI_API void ui_window_destroy(UiWindow win) {
    auto* w = Win(win);
    if (w && w->Handle()) DestroyWindow(w->Handle());
}

UI_API void ui_window_show(UiWindow win) {
    auto* w = Win(win);
    if (w) w->Show();
}

UI_API void ui_window_show_immediate(UiWindow win) {
    auto* w = Win(win);
    if (w) w->ShowImmediate();
}

UI_API void ui_window_prepare_rt(UiWindow win) {
    auto* w = Win(win);
    if (w) w->PrepareRT();
}

UI_API void ui_window_hide(UiWindow win) {
    auto* w = Win(win);
    if (w) w->Hide();
}

UI_API void ui_window_set_root(UiWindow win, UiWidget root) {
    auto* w = Win(win);
    if (!w) return;
    auto widget = Ctx().handles.Lookup(root);
    if (widget) w->SetRoot(widget);
}

UI_API void ui_window_set_title(UiWindow win, const wchar_t* title) {
    auto* w = Win(win);
    if (w && title) w->SetTitle(title);
}

UI_API void ui_window_invalidate(UiWindow win) {
    auto* w = Win(win);
    if (w) w->Invalidate();
}

UI_API void ui_window_relayout(UiWindow win) {
    auto* w = Win(win);
    if (w) { w->LayoutRoot(); w->Invalidate(); }
}

UI_API void* ui_window_hwnd(UiWindow win) {
    auto* w = Win(win);
    return w ? (void*)w->Handle() : nullptr;
}

// ================================================================
// Window callbacks
// ================================================================

UI_API void ui_window_on_close(UiWindow win, UiWindowCloseCallback cb, void* userdata) {
    auto* w = Win(win);
    if (!w) return;
    if (!cb) { w->onClose = nullptr; return; }
    uint64_t id = win;
    w->onClose = [cb, userdata, id]() { cb(id, userdata); };
}

UI_API void ui_window_on_resize(UiWindow win, UiWindowResizeCallback cb, void* userdata) {
    auto* w = Win(win);
    if (!w) return;
    if (!cb) { w->onResize = nullptr; return; }
    uint64_t id = win;
    w->onResize = [cb, userdata, id](int width, int height) { cb(id, width, height, userdata); };
}

UI_API void ui_window_on_drop(UiWindow win, UiWindowDropCallback cb, void* userdata) {
    auto* w = Win(win);
    if (!w) return;
    if (!cb) { w->onDrop = nullptr; return; }
    uint64_t id = win;
    w->onDrop = [cb, userdata, id](const std::wstring& path) { cb(id, path.c_str(), userdata); };
}

UI_API void ui_window_on_key(UiWindow win, UiWindowKeyCallback cb, void* userdata) {
    auto* w = Win(win);
    if (!w) return;
    if (!cb) { w->onKey = nullptr; return; }
    uint64_t id = win;
    w->onKey = [cb, userdata, id](int vk) { cb(id, vk, userdata); };
}

// ================================================================
// Context Menu
// ================================================================

UI_API UiMenu ui_menu_create(void) {
    auto menu = std::make_shared<ui::ContextMenu>();
    return Ctx().RegisterMenu(menu);
}

UI_API void ui_menu_destroy(UiMenu menu) {
    Ctx().RemoveMenu(menu);
}

UI_API void ui_menu_add_item(UiMenu menu, int id, const wchar_t* text) {
    auto m = Ctx().GetMenu(menu);
    if (m && text) m->AddItem(id, text);
}

UI_API void ui_menu_add_item_ex(UiMenu menu, int id, const wchar_t* text,
                                 const wchar_t* shortcut, const char* svg) {
    auto m = Ctx().GetMenu(menu);
    if (!m || !text) return;
    // Need a renderer for SVG parsing.
    ui::Renderer* r = nullptr;
    if (auto* win = Ctx().FirstWindow()) {
        r = &win->GetRenderer();
    }
    m->AddItemEx(id, text, shortcut ? shortcut : L"", svg ? svg : "", r);
}

UI_API void ui_menu_add_separator(UiMenu menu) {
    auto m = Ctx().GetMenu(menu);
    if (m) m->AddSeparator();
}

UI_API void ui_menu_add_submenu(UiMenu menu, const wchar_t* text, UiMenu submenu) {
    auto m = Ctx().GetMenu(menu);
    auto sub = Ctx().GetMenu(submenu);
    if (m && sub && text) m->AddSubmenu(text, sub);
}

UI_API void ui_menu_set_enabled(UiMenu menu, int id, int enabled) {
    auto m = Ctx().GetMenu(menu);
    if (m) m->SetEnabled(id, enabled != 0);
}

UI_API void ui_menu_set_bg_color(UiMenu menu, UiColor color) {
    auto m = Ctx().GetMenu(menu);
    if (m) m->SetBgColor({color.r, color.g, color.b, color.a});
}

UI_API void ui_menu_show(UiWindow win, UiMenu menu, float x, float y) {
    auto* w = Win(win);
    auto m = Ctx().GetMenu(menu);
    if (w && m) w->ShowMenu(m, x, y);
}

UI_API void ui_menu_close(UiWindow win) {
    auto* w = Win(win);
    if (w) w->CloseMenu();
}

UI_API void ui_toast(UiWindow win, const wchar_t* text, int duration_ms) {
    auto* w = Win(win);
    if (w && text) w->ShowToast(text, duration_ms > 0 ? duration_ms : 2000, 0, 0);
}

UI_API void ui_toast_at(UiWindow win, const wchar_t* text, int duration_ms, int position) {
    auto* w = Win(win);
    if (w && text) w->ShowToast(text, duration_ms > 0 ? duration_ms : 2000, position, 0);
}

UI_API void ui_toast_ex(UiWindow win, const wchar_t* text, int duration_ms, int position, int icon) {
    auto* w = Win(win);
    if (w && text) w->ShowToast(text, duration_ms > 0 ? duration_ms : 2000, position, icon);
}

UI_API void ui_window_on_menu(UiWindow win, UiMenuCallback cb, void* userdata) {
    auto* w = Win(win);
    if (!w) return;
    if (!cb) { w->onMenuItemClick = nullptr; return; }
    uint64_t id = win;
    w->onMenuItemClick = [cb, userdata, id](int itemId) { cb(id, itemId, userdata); };
}

UI_API void ui_window_on_right_click(UiWindow win, UiRightClickCallback cb, void* userdata) {
    auto* w = Win(win);
    if (!w) return;
    if (!cb) { w->onRightClick = nullptr; return; }
    uint64_t id = win;
    w->onRightClick = [cb, userdata, id](float x, float y) { cb(id, x, y, userdata); };
}

// ================================================================
// Layout containers
// ================================================================

UI_API UiWidget ui_vbox(void) {
    return Reg(std::make_shared<ui::VBoxWidget>());
}

UI_API UiWidget ui_hbox(void) {
    return Reg(std::make_shared<ui::HBoxWidget>());
}

UI_API UiWidget ui_spacer(float size) {
    return Reg(std::make_shared<ui::SpacerWidget>(size));
}

UI_API UiWidget ui_panel(UiColor bg) {
    return Reg(std::make_shared<ui::PanelWidget>(ToD2D(bg)));
}

UI_API UiWidget ui_panel_themed(int theme_color_id) {
    auto p = std::make_shared<ui::PanelWidget>();
    switch (theme_color_id) {
    case 0: p->bgColorFn = []{ return theme::kSidebarBg(); }; break;
    case 1: p->bgColorFn = []{ return theme::kToolbarBg(); }; break;
    case 2: p->bgColorFn = []{ return theme::kContentBg(); }; break;
    default: p->bgColorFn = []{ return theme::kSidebarBg(); }; break;
    }
    return Reg(p);
}

// ================================================================
// Controls
// ================================================================

UI_API UiWidget ui_label(const wchar_t* text) {
    return Reg(std::make_shared<ui::LabelWidget>(text ? text : L""));
}

UI_API UiWidget ui_button(const wchar_t* text) {
    return Reg(std::make_shared<ui::ButtonWidget>(text ? text : L""));
}

UI_API UiWidget ui_checkbox(const wchar_t* text) {
    return Reg(std::make_shared<ui::CheckBoxWidget>(text ? text : L""));
}

UI_API UiWidget ui_slider(float min_val, float max_val, float value) {
    return Reg(std::make_shared<ui::SliderWidget>(min_val, max_val, value));
}

UI_API UiWidget ui_separator(void) {
    return Reg(std::make_shared<ui::SeparatorWidget>(false));
}

UI_API UiWidget ui_vseparator(void) {
    return Reg(std::make_shared<ui::SeparatorWidget>(true));
}

UI_API UiWidget ui_text_input(const wchar_t* placeholder) {
    return Reg(std::make_shared<ui::TextInputWidget>(placeholder ? placeholder : L""));
}

UI_API UiWidget ui_text_area(const wchar_t* placeholder) {
    return Reg(std::make_shared<ui::TextAreaWidget>(placeholder ? placeholder : L""));
}

UI_API UiWidget ui_combobox(const wchar_t** items, int count) {
    if (count < 0) return UI_INVALID;
    if (count > 0 && !items) return UI_INVALID;

    std::vector<std::wstring> vec;
    vec.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; i++) {
        vec.push_back(items[i] ? items[i] : L"");
    }
    return Reg(std::make_shared<ui::ComboBoxWidget>(std::move(vec)));
}

UI_API UiWidget ui_radio_button(const wchar_t* text, const char* group) {
    return Reg(std::make_shared<ui::RadioButtonWidget>(
        text ? text : L"", group ? group : ""));
}

UI_API UiWidget ui_toggle(const wchar_t* text) {
    return Reg(std::make_shared<ui::ToggleWidget>(text ? text : L""));
}

UI_API UiWidget ui_progress_bar(float min_val, float max_val, float value) {
    return Reg(std::make_shared<ui::ProgressBarWidget>(min_val, max_val, value));
}

UI_API UiWidget ui_tab_control(void) {
    return Reg(std::make_shared<ui::TabControlWidget>());
}

UI_API UiWidget ui_scroll_view(void) {
    return Reg(std::make_shared<ui::ScrollViewWidget>());
}

UI_API UiWidget ui_dialog(void) {
    return Reg(std::make_shared<ui::DialogWidget>());
}

// ================================================================
// Dialog
// ================================================================

UI_API void ui_dialog_show(UiWidget dialog, UiWindow win,
                           const wchar_t* title, const wchar_t* message,
                           UiDialogCallback cb, void* userdata) {
    auto* d = As<ui::DialogWidget>(dialog);
    auto* w = Win(win);
    if (!d || !w) return;
    uint64_t dh = dialog;
    d->onHide_ = [w, d]() {
        if (w->activeDialog_ == d) w->activeDialog_ = nullptr;
    };
    d->Show(title ? title : L"", message ? message : L"",
        [cb, userdata, dh](bool confirmed) {
            if (cb) cb(dh, confirmed ? 1 : 0, userdata);
        });
    w->activeDialog_ = d;
    w->LayoutRoot();
    w->Invalidate();
}

UI_API void ui_dialog_hide(UiWidget dialog, UiWindow win) {
    auto* d = As<ui::DialogWidget>(dialog);
    auto* w = Win(win);
    if (d) d->Hide();
    if (w && w->activeDialog_ == d) w->activeDialog_ = nullptr;
    if (w) { w->LayoutRoot(); w->Invalidate(); }
}

UI_API void ui_dialog_set_ok_text(UiWidget dialog, const wchar_t* text) {
    auto* d = As<ui::DialogWidget>(dialog);
    if (d && text) d->SetOkText(text);
}

UI_API void ui_dialog_set_cancel_text(UiWidget dialog, const wchar_t* text) {
    auto* d = As<ui::DialogWidget>(dialog);
    if (d && text) d->SetCancelText(text);
}

UI_API void ui_dialog_set_show_cancel(UiWidget dialog, int show) {
    auto* d = As<ui::DialogWidget>(dialog);
    if (d) d->SetShowCancel(show != 0);
}

// ================================================================
// ImageView
// ================================================================

UI_API UiWidget ui_image_view(void) {
    return Reg(std::make_shared<ui::ImageViewWidget>());
}

UI_API void ui_image_load_file(UiWidget w, UiWindow win, const wchar_t* path) {
    auto* iv = As<ui::ImageViewWidget>(w);
    auto* wn = Win(win);
    if (iv && wn && path) {
        /* 若当前为 tile 模式，先退出 tile 模式以便新 bitmap 生效 */
        if (iv->IsTiled()) iv->ClearTiles();
        iv->LoadFromFile(path, wn->GetRenderer());
    }
}

UI_API void ui_image_set_pixels(UiWidget w, UiWindow win,
                                 const void* pixels, int width, int height, int stride) {
    auto* iv = As<ui::ImageViewWidget>(w);
    auto* wn = Win(win);
    if (iv && wn && pixels) {
        /* 若当前为 tile 模式，先退出 tile 模式以便新 bitmap 生效 */
        if (iv->IsTiled()) iv->ClearTiles();
        iv->SetBitmapFromPixels(pixels, width, height, stride, wn->GetRenderer());
    }
}

UI_API void ui_image_clear(UiWidget w) {
    auto* iv = As<ui::ImageViewWidget>(w);
    if (!iv) return;
    iv->SetBitmap(nullptr);
    if (iv->IsTiled()) iv->ClearTiles();
}

/* 从 ImageView 的当前 bitmap 读回像素到 CPU 内存。
 * 返回 1 成功（*out_pixels 由 malloc 分配，调用方用 ui_image_free_pixels 释放）。
 * tile 模式 / 无 bitmap / 回读失败时返回 0。
 * 像素格式：BGRA (premultiplied alpha)，stride = w*4。*/
UI_API int ui_image_get_pixels(UiWidget w, UiWindow win,
                                void** out_pixels, int* out_w, int* out_h) {
    if (out_pixels) *out_pixels = nullptr;
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;

    auto* iv = As<ui::ImageViewWidget>(w);
    auto* wn = Win(win);
    if (!iv || !wn || !out_pixels || !out_w || !out_h) return 0;
    if (iv->IsTiled()) return 0;

    ID2D1Bitmap* src = iv->GetBitmap();
    if (!src) return 0;

    ID2D1DeviceContext* ctx = wn->GetRenderer().RT();
    if (!ctx) return 0;

    auto pxSz = src->GetPixelSize();
    int w2 = (int)pxSz.width, h2 = (int)pxSz.height;
    if (w2 <= 0 || h2 <= 0) return 0;

    /* 创建 CPU_READ 中转 bitmap */
    D2D1_BITMAP_PROPERTIES1 cpuProps = {};
    cpuProps.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    cpuProps.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    ComPtr<ID2D1Bitmap1> cpuBmp;
    HRESULT hr = ctx->CreateBitmap(D2D1::SizeU(w2, h2), nullptr, 0, cpuProps, &cpuBmp);
    if (FAILED(hr) || !cpuBmp) return 0;

    D2D1_POINT_2U pt = {0, 0};
    D2D1_RECT_U  rc = {0, 0, (UINT32)w2, (UINT32)h2};
    hr = cpuBmp->CopyFromBitmap(&pt, src, &rc);
    if (FAILED(hr)) return 0;

    D2D1_MAPPED_RECT mapped;
    hr = cpuBmp->Map(D2D1_MAP_OPTIONS_READ, &mapped);
    if (FAILED(hr)) return 0;

    size_t bytes = (size_t)w2 * (size_t)h2 * 4;
    void* buf = malloc(bytes);
    if (!buf) { cpuBmp->Unmap(); return 0; }

    /* 逐行拷贝：源 stride 可能 != w*4 */
    const BYTE* srcP = mapped.bits;
    BYTE* dstP = (BYTE*)buf;
    for (int y = 0; y < h2; ++y) {
        memcpy(dstP + (size_t)y * w2 * 4, srcP + (size_t)y * mapped.pitch, (size_t)w2 * 4);
    }
    cpuBmp->Unmap();

    *out_pixels = buf;
    *out_w = w2;
    *out_h = h2;
    return 1;
}

UI_API void ui_image_free_pixels(void* pixels) {
    free(pixels);
}

UI_API float ui_image_get_zoom(UiWidget w) {
    auto* iv = As<ui::ImageViewWidget>(w);
    return iv ? iv->Zoom() : 1.0f;
}

UI_API void ui_image_set_zoom(UiWidget w, float zoom) {
    auto* iv = As<ui::ImageViewWidget>(w);
    if (iv) iv->SetZoom(zoom);
}

UI_API void ui_image_fit(UiWidget w) {
    auto* iv = As<ui::ImageViewWidget>(w);
    if (iv) iv->FitToView();
}

UI_API void ui_image_reset(UiWidget w) {
    auto* iv = As<ui::ImageViewWidget>(w);
    if (iv) iv->ResetView();
}

UI_API void ui_image_get_pan(UiWidget w, float* out_x, float* out_y) {
    auto* iv = As<ui::ImageViewWidget>(w);
    if (iv) {
        if (out_x) *out_x = iv->PanX();
        if (out_y) *out_y = iv->PanY();
    }
}

UI_API void ui_image_set_pan(UiWidget w, float x, float y) {
    auto* iv = As<ui::ImageViewWidget>(w);
    if (iv) iv->SetPan(x, y);
}

UI_API int ui_image_width(UiWidget w) {
    auto* iv = As<ui::ImageViewWidget>(w);
    return iv ? iv->ImageWidth() : 0;
}

UI_API int ui_image_height(UiWidget w) {
    auto* iv = As<ui::ImageViewWidget>(w);
    return iv ? iv->ImageHeight() : 0;
}

UI_API void ui_image_set_checkerboard(UiWidget w, int on) {
    auto* iv = As<ui::ImageViewWidget>(w);
    if (iv) iv->SetCheckerboard(on != 0);
}

UI_API void ui_image_set_antialias(UiWidget w, int on) {
    auto* iv = As<ui::ImageViewWidget>(w);
    if (iv) iv->SetAntialias(on != 0);
}

UI_API int ui_image_get_antialias(UiWidget w) {
    auto* iv = As<ui::ImageViewWidget>(w);
    return (iv && iv->Antialias()) ? 1 : 0;
}

UI_API void ui_image_set_zoom_range(UiWidget w, float min_zoom, float max_zoom) {
    auto* iv = As<ui::ImageViewWidget>(w);
    if (iv) iv->SetZoomRange(min_zoom, max_zoom);
}

UI_API void ui_image_on_viewport_changed(UiWidget w, UiViewportCallback cb, void* userdata) {
    auto* iv = As<ui::ImageViewWidget>(w);
    if (!iv) return;
    if (!cb) { iv->onViewportChanged = nullptr; return; }
    uint64_t handle = w;
    iv->onViewportChanged = [cb, userdata, handle](float z, float px, float py) {
        cb(handle, z, px, py, userdata);
    };
}

UI_API void ui_image_on_mouse_down(UiWidget w, UiImageMouseDownCallback cb, void* userdata) {
    auto* iv = As<ui::ImageViewWidget>(w);
    if (!iv) return;
    if (!cb) { iv->onMouseDownHook = nullptr; return; }
    uint64_t handle = w;
    iv->onMouseDownHook = [cb, userdata, handle](float x, float y, int btn) -> bool {
        return cb(handle, x, y, btn, userdata) != 0;
    };
}

UI_API void ui_image_on_mouse_move(UiWidget w, UiImageMouseMoveCallback cb, void* userdata) {
    auto* iv = As<ui::ImageViewWidget>(w);
    if (!iv) return;
    if (!cb) { iv->onMouseMoveHook = nullptr; return; }
    uint64_t handle = w;
    iv->onMouseMoveHook = [cb, userdata, handle](float x, float y) -> bool {
        return cb(handle, x, y, userdata) != 0;
    };
}

// Rotation
UI_API void ui_image_set_rotation(UiWidget w, int angle) {
    auto* iv = As<ui::ImageViewWidget>(w);
    if (iv) iv->SetRotation(angle);
}
UI_API int ui_image_get_rotation(UiWidget w) {
    auto* iv = As<ui::ImageViewWidget>(w);
    return iv ? iv->Rotation() : 0;
}

// Loading spinner
UI_API void ui_image_set_loading(UiWidget w, int loading) {
    auto* iv = As<ui::ImageViewWidget>(w);
    if (iv) iv->SetLoading(loading != 0);
}
UI_API int ui_image_get_loading(UiWidget w) {
    auto* iv = As<ui::ImageViewWidget>(w);
    return iv ? (iv->IsLoading() ? 1 : 0) : 0;
}

// Tiled rendering
UI_API void ui_image_set_tiled(UiWidget w, UiWindow win, int full_width, int full_height, int tile_size) {
    auto* iv = As<ui::ImageViewWidget>(w);
    auto* wn = Win(win);
    if (iv && wn) iv->SetTiled(full_width, full_height, tile_size, wn->GetRenderer());
}
UI_API void ui_image_set_tile(UiWidget w, UiWindow win, int tile_x, int tile_y,
                               const void* pixels, int width, int height, int stride) {
    auto* iv = As<ui::ImageViewWidget>(w);
    auto* wn = Win(win);
    if (iv && wn) iv->SetTile(tile_x, tile_y, pixels, width, height, stride, wn->GetRenderer());
}
UI_API void ui_image_set_tile_preview(UiWidget w, UiWindow win,
                                       const void* pixels, int width, int height, int stride) {
    auto* iv = As<ui::ImageViewWidget>(w);
    auto* wn = Win(win);
    if (iv && wn) {
        auto bmp = wn->GetRenderer().CreateBitmapFromPixels(pixels, width, height, stride);
        iv->SetTilePreview(bmp, width, height);
    }
}
UI_API void ui_image_clear_tiles(UiWidget w) {
    auto* iv = As<ui::ImageViewWidget>(w);
    if (iv) iv->ClearTiles();
}
UI_API int ui_image_is_tiled(UiWidget w) {
    auto* iv = As<ui::ImageViewWidget>(w);
    return iv ? (iv->IsTiled() ? 1 : 0) : 0;
}

// Raw dimensions
UI_API int ui_image_raw_width(UiWidget w) {
    auto* iv = As<ui::ImageViewWidget>(w);
    return iv ? iv->RawImageWidth() : 0;
}
UI_API int ui_image_raw_height(UiWidget w) {
    auto* iv = As<ui::ImageViewWidget>(w);
    return iv ? iv->RawImageHeight() : 0;
}

// ================================================================
// IconButton
// ================================================================

UI_API UiWidget ui_icon_button(const char* svg, int ghost) {
    return Reg(std::make_shared<ui::IconButtonWidget>(svg ? svg : "", ghost != 0));
}

UI_API void ui_icon_button_set_svg(UiWidget w, const char* svg) {
    auto* ib = As<ui::IconButtonWidget>(w);
    if (ib && svg) ib->SetSvg(svg);
}

UI_API void ui_icon_button_set_ghost(UiWidget w, int ghost) {
    auto* ib = As<ui::IconButtonWidget>(w);
    if (ib) ib->SetGhost(ghost != 0);
}

UI_API void ui_icon_button_set_icon_color(UiWidget w, UiColor color) {
    auto* ib = As<ui::IconButtonWidget>(w);
    if (ib) ib->SetIconColor(ToD2D(color));
}

UI_API void ui_icon_button_set_icon_padding(UiWidget w, float padding) {
    auto* ib = As<ui::IconButtonWidget>(w);
    if (ib) ib->SetIconPadding(padding);
}

// ================================================================
// TitleBar
// ================================================================

UI_API UiWidget ui_titlebar(const wchar_t* title) {
    return Reg(std::make_shared<ui::TitleBarWidget>(title ? title : L""));
}

UI_API void ui_titlebar_set_title(UiWidget w, const wchar_t* title) {
    auto* tb = As<ui::TitleBarWidget>(w);
    if (tb && title) tb->SetTitle(title);
}

UI_API void ui_titlebar_add_widget(UiWidget titlebar, UiWidget custom_widget) {
    auto* tb = As<ui::TitleBarWidget>(titlebar);
    auto cw = Ctx().handles.Lookup(custom_widget);
    if (tb && cw) tb->AddCustomWidget(cw);
}

UI_API void ui_titlebar_show_buttons(UiWidget titlebar, int showMin, int showMax, int showClose) {
    auto* tb = As<ui::TitleBarWidget>(titlebar);
    if (!tb) return;
    if (tb->MinBtn()) tb->MinBtn()->visible = (showMin != 0);
    if (tb->MaxBtn()) tb->MaxBtn()->visible = (showMax != 0);
    if (tb->CloseBtn()) tb->CloseBtn()->visible = (showClose != 0);
}

UI_API void ui_titlebar_show_icon(UiWidget titlebar, int show) {
    auto* tb = As<ui::TitleBarWidget>(titlebar);
    if (tb) tb->SetShowIcon(show != 0);
}

UI_API void ui_titlebar_set_bg_color(UiWidget titlebar, UiColor color) {
    auto* tb = As<ui::TitleBarWidget>(titlebar);
    if (tb) tb->SetCustomBgColor({color.r, color.g, color.b, color.a});
}

// ================================================================
// Widget tree operations
// ================================================================

UI_API void ui_widget_add_child(UiWidget parent, UiWidget child) {
    auto p = Ctx().handles.Lookup(parent);
    auto c = Ctx().handles.Lookup(child);
    if (p && c) p->AddChild(c);
}

UI_API void ui_widget_remove_child(UiWidget parent, UiWidget child) {
    auto* p = W(parent);
    auto* c = W(child);
    if (p && c) p->RemoveChild(c);
}

UI_API void ui_widget_destroy(UiWidget widget) {
    Ctx().handles.Remove(widget);
}

UI_API UiWidget ui_widget_find_by_id(UiWidget root, const char* id) {
    auto* r = W(root);
    if (!r || !id) return UI_INVALID;
    auto* found = r->FindById(id);
    if (!found) return UI_INVALID;
    // 若已在 handle table 里就复用；否则新插入一个句柄（markup 构建的
    // widget 默认不在 handle table 里，直接 FindHandle 会返回 0）。
    uint64_t h = Ctx().handles.FindHandle(found);
    if (h) return h;
    return Ctx().handles.Insert(found->shared_from_this());
}

// ================================================================
// Widget common properties
// ================================================================

UI_API void ui_widget_set_id(UiWidget w, const char* id) {
    auto* p = W(w); if (p && id) p->id = id;
}

UI_API void ui_widget_set_width(UiWidget w, float width) {
    auto* p = W(w); if (p) p->fixedW = width;
}

UI_API void ui_widget_set_height(UiWidget w, float height) {
    auto* p = W(w); if (p) p->fixedH = height;
}

UI_API void ui_widget_set_size(UiWidget w, float width, float height) {
    auto* p = W(w); if (p) { p->fixedW = width; p->fixedH = height; }
}

UI_API void ui_widget_set_expand(UiWidget w, int expand) {
    auto* p = W(w); if (p) p->expanding = (expand != 0);
}

UI_API void ui_widget_set_padding(UiWidget w, float left, float top, float right, float bottom) {
    auto* p = W(w);
    if (p) { p->padL = left; p->padT = top; p->padR = right; p->padB = bottom; }
}

UI_API void ui_widget_set_padding_uniform(UiWidget w, float pad) {
    auto* p = W(w);
    if (p) { p->padL = p->padT = p->padR = p->padB = pad; }
}

UI_API void ui_widget_set_gap(UiWidget w, float gap) {
    auto* p = W(w); if (p) p->Gap(gap);
}

UI_API void ui_widget_set_visible(UiWidget w, int visible) {
    auto* p = W(w); if (p) p->visible = (visible != 0);
}

UI_API void ui_widget_set_opacity(UiWidget w, float opacity) {
    auto* p = W(w);
    if (p) {
        if (opacity < 0.0f) opacity = 0.0f;
        if (opacity > 1.0f) opacity = 1.0f;
        p->opacity = opacity;
    }
}

UI_API float ui_widget_get_opacity(UiWidget w) {
    auto* p = W(w);
    return p ? p->opacity : 1.0f;
}

UI_API void ui_widget_set_enabled(UiWidget w, int enabled) {
    auto* p = W(w); if (p) p->enabled = (enabled != 0);
}

UI_API void ui_widget_set_bg_color(UiWidget w, UiColor color) {
    auto* p = W(w); if (p) p->bgColor = ToD2D(color);
}

UI_API void ui_widget_set_tooltip(UiWidget w, const wchar_t* text) {
    auto* p = W(w); if (p) p->tooltip = text ? text : L"";
}

UI_API int ui_widget_get_visible(UiWidget w) {
    auto* p = W(w); return p ? (p->visible ? 1 : 0) : 0;
}

UI_API int ui_widget_get_enabled(UiWidget w) {
    auto* p = W(w); return p ? (p->enabled ? 1 : 0) : 0;
}

UI_API UiRect ui_widget_get_rect(UiWidget w) {
    auto* p = W(w);
    if (!p) return {0, 0, 0, 0};
    return {p->rect.left, p->rect.top, p->rect.right, p->rect.bottom};
}

UI_API void ui_widget_set_rect(UiWidget w, UiRect rect) {
    auto* p = W(w);
    if (p) {
        p->rect.left = rect.left;
        p->rect.top = rect.top;
        p->rect.right = rect.right;
        p->rect.bottom = rect.bottom;
    }
}

// ================================================================
// Label
// ================================================================

UI_API void ui_label_set_text(UiWidget w, const wchar_t* text) {
    auto* lbl = As<ui::LabelWidget>(w);
    if (lbl && text) lbl->SetText(text);
}

UI_API void ui_label_set_font_size(UiWidget w, float size) {
    auto* p = W(w); if (p) p->FontSize(size);
}

UI_API void ui_label_set_bold(UiWidget w, int bold) {
    auto* p = W(w); if (p && bold) p->Bold();
}

UI_API void ui_label_set_wrap(UiWidget w, int wrap) {
    auto* p = W(w);
    if (p) {
        auto* lbl = dynamic_cast<ui::LabelWidget*>(p);
        if (lbl) lbl->SetWrap(wrap != 0);
    }
}

UI_API void ui_label_set_max_lines(UiWidget w, int maxLines) {
    auto* p = W(w);
    if (p) {
        auto* lbl = dynamic_cast<ui::LabelWidget*>(p);
        if (lbl) lbl->SetMaxLines(maxLines);
    }
}

UI_API void ui_label_set_text_color(UiWidget w, UiColor color) {
    auto* p = W(w); if (p) p->TextColor(ToD2D(color));
}

UI_API void ui_label_set_align(UiWidget w, int align) {
    auto* p = W(w); if (p) p->Align(align);
}

// ================================================================
// Button
// ================================================================

UI_API void ui_button_set_font_size(UiWidget w, float size) {
    auto* p = W(w); if (p) p->FontSize(size);
}

UI_API void ui_button_set_type(UiWidget w, int type) {
    auto* btn = As<ui::ButtonWidget>(w);
    if (btn) btn->SetType(type == 1 ? ui::ButtonType::Primary : ui::ButtonType::Default);
}

UI_API void ui_button_set_text_color(UiWidget w, UiColor color) {
    auto* btn = As<ui::ButtonWidget>(w);
    if (btn) btn->SetTextColor({color.r, color.g, color.b, color.a});
}

UI_API void ui_button_set_bg_color(UiWidget w, UiColor color) {
    auto* btn = As<ui::ButtonWidget>(w);
    if (btn) btn->SetCustomBgColor({color.r, color.g, color.b, color.a});
}

// ================================================================
// CheckBox
// ================================================================

UI_API int ui_checkbox_get_checked(UiWidget w) {
    auto* cb = As<ui::CheckBoxWidget>(w);
    return cb ? (cb->Checked() ? 1 : 0) : 0;
}

UI_API void ui_checkbox_set_checked(UiWidget w, int checked) {
    auto* cb = As<ui::CheckBoxWidget>(w);
    if (cb) cb->SetChecked(checked != 0);
}

// ================================================================
// Slider
// ================================================================

UI_API float ui_slider_get_value(UiWidget w) {
    auto* sl = As<ui::SliderWidget>(w);
    return sl ? sl->Value() : 0.0f;
}

UI_API void ui_slider_set_value(UiWidget w, float value) {
    auto* sl = As<ui::SliderWidget>(w);
    if (sl) sl->SetValue(value);
}

// ================================================================
// TextInput
// ================================================================

UI_API const wchar_t* ui_text_input_get_text(UiWidget w) {
    auto* ti = As<ui::TextInputWidget>(w);
    if (!ti) return L"";
    g_textInputRetBuf = ti->Text();
    return g_textInputRetBuf.c_str();
}

UI_API void ui_text_input_set_text(UiWidget w, const wchar_t* text) {
    auto* ti = As<ui::TextInputWidget>(w);
    if (ti && text) ti->SetText(text);
}

UI_API void ui_text_input_set_read_only(UiWidget w, int read_only) {
    auto* ti = As<ui::TextInputWidget>(w);
    if (ti) ti->readOnly = (read_only != 0);
}

// ================================================================
// TextArea
// ================================================================

UI_API const wchar_t* ui_text_area_get_text(UiWidget w) {
    auto* ta = As<ui::TextAreaWidget>(w);
    if (!ta) return L"";
    g_textAreaRetBuf = ta->Text();
    return g_textAreaRetBuf.c_str();
}

UI_API void ui_text_area_set_text(UiWidget w, const wchar_t* text) {
    auto* ta = As<ui::TextAreaWidget>(w);
    if (ta && text) ta->SetText(text);
}

UI_API void ui_text_area_set_read_only(UiWidget w, int read_only) {
    auto* ta = As<ui::TextAreaWidget>(w);
    if (ta) ta->readOnly = (read_only != 0);
}

// ================================================================
// ComboBox
// ================================================================

UI_API int ui_combobox_get_selected(UiWidget w) {
    auto* cb = As<ui::ComboBoxWidget>(w);
    return cb ? cb->SelectedIndex() : -1;
}

UI_API void ui_combobox_set_selected(UiWidget w, int index) {
    auto* cb = As<ui::ComboBoxWidget>(w);
    if (cb) cb->SetSelectedIndex(index);
}

// ================================================================
// RadioButton
// ================================================================

UI_API int ui_radio_get_selected(UiWidget w) {
    auto* rb = As<ui::RadioButtonWidget>(w);
    return rb ? (rb->Selected() ? 1 : 0) : 0;
}

UI_API void ui_radio_set_selected(UiWidget w, int selected) {
    auto* rb = As<ui::RadioButtonWidget>(w);
    if (rb) rb->SetSelectedImmediate(selected != 0);
}

// ================================================================
// Toggle
// ================================================================

UI_API int ui_toggle_get_on(UiWidget w) {
    auto* tg = As<ui::ToggleWidget>(w);
    return tg ? (tg->On() ? 1 : 0) : 0;
}

UI_API void ui_toggle_set_on(UiWidget w, int on) {
    auto* tg = As<ui::ToggleWidget>(w);
    if (tg) tg->SetOn(on != 0);
}

/* 立即设置状态，不走动画（用于窗口初始化时设默认值，避免「先关后开」的视觉跳动） */
UI_API void ui_toggle_set_on_immediate(UiWidget w, int on) {
    auto* tg = As<ui::ToggleWidget>(w);
    if (tg) tg->SetOnImmediate(on != 0);
}

// ================================================================
// ProgressBar
// ================================================================

UI_API float ui_progress_get_value(UiWidget w) {
    auto* pb = As<ui::ProgressBarWidget>(w);
    return pb ? pb->Value() : 0.0f;
}

UI_API void ui_progress_set_value(UiWidget w, float value) {
    auto* pb = As<ui::ProgressBarWidget>(w);
    if (pb) pb->SetValue(value);
}

// ================================================================
// TabControl
// ================================================================

UI_API void ui_tab_add(UiWidget tab_control, const wchar_t* title, UiWidget content) {
    auto* tc = As<ui::TabControlWidget>(tab_control);
    auto contentWidget = Ctx().handles.Lookup(content);
    if (tc && title && contentWidget) tc->AddTab(title, contentWidget);
}

UI_API int ui_tab_get_active(UiWidget tab_control) {
    auto* tc = As<ui::TabControlWidget>(tab_control);
    return tc ? tc->ActiveIndex() : 0;
}

UI_API void ui_tab_set_active(UiWidget tab_control, int index) {
    auto* tc = As<ui::TabControlWidget>(tab_control);
    if (tc) tc->SetActiveIndex(index);
}

// ================================================================
// ScrollView
// ================================================================

UI_API void ui_scroll_set_content(UiWidget scroll_view, UiWidget content) {
    auto* sv = As<ui::ScrollViewWidget>(scroll_view);
    auto contentWidget = Ctx().handles.Lookup(content);
    if (sv && contentWidget) sv->SetContent(contentWidget);
}

// ================================================================
// Widget callbacks
// ================================================================

UI_API void ui_widget_on_click(UiWidget w, UiClickCallback cb, void* userdata) {
    auto* p = W(w);
    if (!p) return;
    if (!cb) { p->onClick = nullptr; return; }
    uint64_t handle = w;
    p->onClick = [cb, userdata, handle]() { cb(handle, userdata); };
}

UI_API void ui_checkbox_on_changed(UiWidget w, UiValueCallback cb, void* userdata) {
    auto* p = W(w);
    if (!p) return;
    if (!cb) { p->onValueChanged = nullptr; return; }
    uint64_t handle = w;
    p->onValueChanged = [cb, userdata, handle](bool val) { cb(handle, val ? 1 : 0, userdata); };
}

UI_API void ui_slider_on_changed(UiWidget w, UiFloatCallback cb, void* userdata) {
    auto* p = W(w);
    if (!p) return;
    if (!cb) { p->onFloatChanged = nullptr; return; }
    uint64_t handle = w;
    p->onFloatChanged = [cb, userdata, handle](float val) { cb(handle, val, userdata); };
}

UI_API void ui_toggle_on_changed(UiWidget w, UiValueCallback cb, void* userdata) {
    auto* p = W(w);
    if (!p) return;
    if (!cb) { p->onValueChanged = nullptr; return; }
    uint64_t handle = w;
    p->onValueChanged = [cb, userdata, handle](bool val) { cb(handle, val ? 1 : 0, userdata); };
}

UI_API void ui_combobox_on_changed(UiWidget w, UiSelectionCallback cb, void* userdata) {
    auto* combo = As<ui::ComboBoxWidget>(w);
    if (!combo) return;
    if (!cb) { combo->onSelectionChanged = nullptr; return; }
    uint64_t handle = w;
    combo->onSelectionChanged = [cb, userdata, handle](int idx) { cb(handle, idx, userdata); };
}

// ================================================================
// Theme
// ================================================================

UI_API void ui_theme_set_mode(UiThemeMode mode) {
    theme::SetMode(mode == UI_THEME_LIGHT ? theme::Mode::Light : theme::Mode::Dark);
    Ctx().InvalidateAllWindows();
}

UI_API UiThemeMode ui_theme_get_mode(void) {
    return theme::CurrentMode() == theme::Mode::Light ? UI_THEME_LIGHT : UI_THEME_DARK;
}

UI_API UiColor ui_theme_bg(void)         { return FromD2D(theme::kWindowBg()); }
UI_API UiColor ui_theme_content_bg(void) { return FromD2D(theme::kContentBg()); }
UI_API UiColor ui_theme_sidebar_bg(void) { return FromD2D(theme::kSidebarBg()); }
UI_API UiColor ui_theme_toolbar_bg(void) { return FromD2D(theme::kToolbarBg()); }
UI_API UiColor ui_theme_accent(void)     { return FromD2D(theme::kAccent()); }
UI_API UiColor ui_theme_text(void)       { return FromD2D(theme::kBtnText()); }
UI_API UiColor ui_theme_divider(void)    { return FromD2D(theme::kDivider()); }

// ================================================================
// CustomWidget
// ================================================================

UI_API UiWidget ui_custom(void) {
    auto w = std::make_shared<ui::CustomWidget>();
    UiWidget h = Reg(w);
    w->apiHandle = h;
    return h;
}

#define CUSTOM_SET_CB(field, cbField, udField) \
    auto* cw = As<ui::CustomWidget>(w); \
    if (!cw) return; \
    cw->cbField = (decltype(cw->cbField))cb; \
    cw->udField = ud;

UI_API void ui_custom_on_draw(UiWidget w, UiCustomDrawCallback cb, void* ud) {
    CUSTOM_SET_CB(drawCb, drawCb, drawUd)
}
UI_API void ui_custom_on_mouse_down(UiWidget w, UiCustomMouseCallback cb, void* ud) {
    CUSTOM_SET_CB(mouseDownCb, mouseDownCb, mouseDownUd)
}
UI_API void ui_custom_on_mouse_move(UiWidget w, UiCustomMouseCallback cb, void* ud) {
    CUSTOM_SET_CB(mouseMoveCb, mouseMoveCb, mouseMoveUd)
}
UI_API void ui_custom_on_mouse_up(UiWidget w, UiCustomMouseCallback cb, void* ud) {
    CUSTOM_SET_CB(mouseUpCb, mouseUpCb, mouseUpUd)
}
UI_API void ui_custom_on_mouse_wheel(UiWidget w, UiCustomWheelCallback cb, void* ud) {
    CUSTOM_SET_CB(mouseWheelCb, mouseWheelCb, mouseWheelUd)
}
UI_API void ui_custom_on_key_down(UiWidget w, UiCustomKeyCallback cb, void* ud) {
    CUSTOM_SET_CB(keyDownCb, keyDownCb, keyDownUd)
}
UI_API void ui_custom_on_char(UiWidget w, UiCustomCharCallback cb, void* ud) {
    CUSTOM_SET_CB(charCb, charCb, charUd)
}
UI_API void ui_custom_on_layout(UiWidget w, UiCustomLayoutCallback cb, void* ud) {
    CUSTOM_SET_CB(layoutCb, layoutCb, layoutUd)
}

#undef CUSTOM_SET_CB

UI_API void ui_custom_set_focused(UiWidget w, int focused) {
    auto* cw = As<ui::CustomWidget>(w);
    if (cw) cw->focused = (focused != 0);
}

UI_API int ui_custom_get_focused(UiWidget w) {
    auto* cw = As<ui::CustomWidget>(w);
    return cw ? (int)cw->focused : 0;
}

// ================================================================
// Drawing API (used inside UiCustomDrawCallback)
// ================================================================

static ui::Renderer* R(UiDrawCtx ctx) { return (ui::Renderer*)ctx; }

UI_API void ui_draw_fill_rect(UiDrawCtx ctx, UiRect rect, UiColor color) {
    if (auto* r = R(ctx)) r->FillRect(ToD2DRect(rect), ToD2D(color));
}

UI_API void ui_draw_rect(UiDrawCtx ctx, UiRect rect, UiColor color, float width) {
    if (auto* r = R(ctx)) r->DrawRect(ToD2DRect(rect), ToD2D(color), width);
}

UI_API void ui_draw_fill_rounded_rect(UiDrawCtx ctx, UiRect rect, float rx, float ry, UiColor color) {
    if (auto* r = R(ctx)) r->FillRoundedRect(ToD2DRect(rect), rx, ry, ToD2D(color));
}

UI_API void ui_draw_rounded_rect(UiDrawCtx ctx, UiRect rect, float rx, float ry, UiColor color, float width) {
    if (auto* r = R(ctx)) r->DrawRoundedRect(ToD2DRect(rect), rx, ry, ToD2D(color), width);
}

UI_API void ui_draw_line(UiDrawCtx ctx, float x1, float y1, float x2, float y2, UiColor color, float width) {
    if (auto* r = R(ctx)) r->DrawLine(x1, y1, x2, y2, ToD2D(color), width);
}

UI_API void ui_draw_text(UiDrawCtx ctx, const wchar_t* text, UiRect rect, UiColor color, float fontSize) {
    if (auto* r = R(ctx)) r->DrawText(text ? text : L"", ToD2DRect(rect), ToD2D(color), fontSize);
}

UI_API void ui_draw_text_ex(UiDrawCtx ctx, const wchar_t* text, UiRect rect, UiColor color,
                             float fontSize, int align, int bold) {
    if (auto* r = R(ctx)) {
        r->DrawText(text ? text : L"", ToD2DRect(rect), ToD2D(color), fontSize,
                    (DWRITE_TEXT_ALIGNMENT)align,
                    bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
    }
}

UI_API float ui_draw_measure_text(UiDrawCtx ctx, const wchar_t* text, float fontSize) {
    auto* r = R(ctx);
    if (!r || !text) return 0;
    return r->MeasureTextWidth(text, fontSize);
}

UI_API void ui_draw_bitmap(UiDrawCtx ctx, const uint8_t* pixels,
                             int width, int height, int stride, UiRect dest) {
    auto* r = R(ctx);
    if (!r || !pixels || width <= 0 || height <= 0) return;

    auto bitmap = r->CreateBitmapFromPixels(pixels, width, height, stride);
    if (bitmap) {
        r->DrawBitmap(bitmap.Get(), ToD2DRect(dest), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
}

UI_API void ui_draw_push_clip(UiDrawCtx ctx, UiRect rect) {
    if (auto* r = R(ctx)) r->PushClip(ToD2DRect(rect));
}

UI_API void ui_draw_pop_clip(UiDrawCtx ctx) {
    if (auto* r = R(ctx)) r->PopClip();
}

// ================================================================
// Debug / Inspector
// ================================================================

} // close extern "C" temporarily for C++ include
#include "ui_debug.h"
extern "C" {

static char* dupStr(const std::string& s) {
    char* p = (char*)malloc(s.size() + 1);
    if (p) { memcpy(p, s.c_str(), s.size() + 1); }
    return p;
}

UI_API char* ui_debug_dump_tree(UiWindow win) {
    auto* w = Win(win);
    if (!w) return dupStr("null");
    auto root = w->Root();
    if (!root) return dupStr("null");
    return dupStr(ui::DebugDumpTree(root.get(), &w->GetRenderer()));
}

UI_API char* ui_debug_dump_widget(UiWidget widget) {
    auto* w = W(widget);
    if (!w) return dupStr("null");
    return dupStr(ui::DebugDumpTree(w));
}

UI_API void ui_debug_free(char* ptr) {
    free(ptr);
}

UI_API void ui_debug_highlight(UiWindow win, const char* widget_id) {
    auto* w = Win(win);
    if (w) w->SetDebugHighlight(widget_id);
}

UI_API int ui_debug_screenshot(UiWindow win, const wchar_t* outPath) {
    auto* w = Win(win);
    if (!w) return -1;
    return w->Screenshot(outPath);
}

// ================================================================
// Debug / Simulation — event injection
// ================================================================

static bool WidgetCenter(ui::Widget* w, float& x, float& y) {
    if (!w) return false;
    const auto& r = w->rect;
    if (r.right <= r.left || r.bottom <= r.top) return false;
    x = (r.left + r.right) * 0.5f;
    y = (r.top + r.bottom) * 0.5f;
    return true;
}

UI_API int ui_debug_widget_center(UiWidget w, float* outX, float* outY) {
    auto* p = W(w); if (!p) return -1;
    float cx, cy;
    if (!WidgetCenter(p, cx, cy)) return -1;
    if (outX) *outX = cx;
    if (outY) *outY = cy;
    return 0;
}

UI_API int ui_debug_widget_is_visible(UiWidget w) {
    auto* p = W(w);
    return (p && p->visible) ? 1 : 0;
}

// ---- Mouse ----

UI_API int ui_debug_click_at(UiWindow win, float x, float y) {
    auto* wi = Win(win); if (!wi) return -1;
    wi->SimMouseMove(x, y);
    wi->SimMouseDown(x, y);
    wi->SimMouseUp(x, y);
    wi->Invalidate();
    return 0;
}

UI_API int ui_debug_click(UiWindow win, UiWidget w) {
    auto* wi = Win(win); if (!wi) return -1;
    auto* p = W(w); if (!p) return -1;
    float cx, cy;
    if (!WidgetCenter(p, cx, cy)) return -1;
    return ui_debug_click_at(win, cx, cy);
}

UI_API int ui_debug_double_click(UiWindow win, UiWidget w) {
    int r1 = ui_debug_click(win, w);
    if (r1 != 0) return r1;
    return ui_debug_click(win, w);
}

UI_API int ui_debug_right_click_at(UiWindow win, float x, float y) {
    auto* wi = Win(win); if (!wi) return -1;
    wi->SimRightClick(x, y);
    return 0;
}

UI_API int ui_debug_right_click(UiWindow win, UiWidget w) {
    auto* p = W(w); if (!p) return -1;
    float cx, cy;
    if (!WidgetCenter(p, cx, cy)) return -1;
    return ui_debug_right_click_at(win, cx, cy);
}

UI_API int ui_debug_hover(UiWindow win, UiWidget w) {
    auto* wi = Win(win); if (!wi) return -1;
    auto* p = W(w); if (!p) return -1;
    float cx, cy;
    if (!WidgetCenter(p, cx, cy)) return -1;
    wi->SimMouseMove(cx, cy);
    wi->Invalidate();
    return 0;
}

UI_API int ui_debug_mouse_move(UiWindow win, float x, float y) {
    auto* wi = Win(win); if (!wi) return -1;
    wi->SimMouseMove(x, y);
    wi->Invalidate();
    return 0;
}
UI_API int ui_debug_mouse_down(UiWindow win, float x, float y) {
    auto* wi = Win(win); if (!wi) return -1;
    wi->SimMouseDown(x, y);
    wi->Invalidate();
    return 0;
}
UI_API int ui_debug_mouse_up(UiWindow win, float x, float y) {
    auto* wi = Win(win); if (!wi) return -1;
    wi->SimMouseUp(x, y);
    wi->Invalidate();
    return 0;
}

UI_API int ui_debug_drag_to(UiWindow win, float x1, float y1, float x2, float y2) {
    auto* wi = Win(win); if (!wi) return -1;
    wi->SimMouseMove(x1, y1);
    wi->SimMouseDown(x1, y1);
    // 几步中间插值，给动画 / slider 一些过程值
    const int steps = 6;
    for (int i = 1; i < steps; i++) {
        float t = (float)i / steps;
        wi->SimMouseMove(x1 + (x2 - x1) * t, y1 + (y2 - y1) * t);
    }
    wi->SimMouseMove(x2, y2);
    wi->SimMouseUp(x2, y2);
    wi->Invalidate();
    return 0;
}

UI_API int ui_debug_drag(UiWindow win, UiWidget w, float dx, float dy) {
    auto* p = W(w); if (!p) return -1;
    float cx, cy;
    if (!WidgetCenter(p, cx, cy)) return -1;
    return ui_debug_drag_to(win, cx, cy, cx + dx, cy + dy);
}

UI_API int ui_debug_wheel_at(UiWindow win, float x, float y, float delta) {
    auto* wi = Win(win); if (!wi) return -1;
    wi->SimMouseWheel(x, y, delta);
    wi->Invalidate();
    return 0;
}

UI_API int ui_debug_wheel(UiWindow win, UiWidget w, float delta) {
    auto* p = W(w); if (!p) return -1;
    float cx, cy;
    if (!WidgetCenter(p, cx, cy)) return -1;
    return ui_debug_wheel_at(win, cx, cy, delta);
}

// ---- Focus / Keyboard ----

UI_API int ui_debug_focus(UiWindow win, UiWidget w) {
    auto* wi = Win(win); if (!wi) return -1;
    auto* p = W(w); if (!p) return -1;
    wi->SetFocus(p);
    wi->Invalidate();
    return 0;
}

UI_API int ui_debug_blur(UiWindow win) {
    auto* wi = Win(win); if (!wi) return -1;
    wi->ClearFocus();
    wi->Invalidate();
    return 0;
}

UI_API int ui_debug_key(UiWindow win, int vk) {
    auto* wi = Win(win); if (!wi) return -1;
    wi->SimKeyDown(vk);
    return 0;
}

UI_API int ui_debug_type_char(UiWindow win, unsigned int ch) {
    auto* wi = Win(win); if (!wi) return -1;
    wi->SimKeyChar((wchar_t)ch);
    return 0;
}

UI_API int ui_debug_type_text(UiWindow win, const wchar_t* text) {
    auto* wi = Win(win); if (!wi || !text) return -1;
    for (const wchar_t* p = text; *p; p++) {
        wi->SimKeyChar(*p);
    }
    wi->Invalidate();
    return 0;
}

// ---- 高层控件操作 ----

static void FireValueChanged(ui::Widget* p, bool v) {
    if (p && p->onValueChanged) p->onValueChanged(v);
}

UI_API int ui_debug_checkbox_set(UiWindow win, UiWidget w, int checked) {
    auto* cb = As<ui::CheckBoxWidget>(w); if (!cb) return -1;
    cb->SetChecked(checked != 0);
    FireValueChanged(cb, checked != 0);
    auto* wi = Win(win); if (wi) wi->Invalidate();
    return 0;
}
UI_API int ui_debug_checkbox_toggle(UiWindow win, UiWidget w) {
    auto* cb = As<ui::CheckBoxWidget>(w); if (!cb) return -1;
    return ui_debug_checkbox_set(win, w, cb->Checked() ? 0 : 1);
}

UI_API int ui_debug_toggle_set(UiWindow win, UiWidget w, int on) {
    auto* tg = As<ui::ToggleWidget>(w); if (!tg) return -1;
    tg->SetOn(on != 0);
    FireValueChanged(tg, on != 0);
    auto* wi = Win(win); if (wi) wi->Invalidate();
    return 0;
}

UI_API int ui_debug_radio_select(UiWindow win, UiWidget w) {
    auto* rb = As<ui::RadioButtonWidget>(w); if (!rb) return -1;
    rb->SetSelected(true);
    FireValueChanged(rb, true);
    auto* wi = Win(win); if (wi) wi->Invalidate();
    return 0;
}

UI_API int ui_debug_combo_select(UiWindow win, UiWidget w, int index) {
    auto* combo = As<ui::ComboBoxWidget>(w); if (!combo) return -1;
    if (index < 0 || index >= combo->ItemCount()) return -1;
    combo->SetSelectedIndex(index);
    if (combo->onSelectionChanged) combo->onSelectionChanged(index);
    combo->Close();
    auto* wi = Win(win); if (wi) wi->Invalidate();
    return 0;
}

UI_API int ui_debug_combo_open(UiWidget w) {
    // 直接合成一次 click 效果：通过 SimMouseDown 走正常路径会更贴近真实，
    // 但这里只需要 open_ 状态。最简单是提供一个专门的 API —— 但 open_ 是 private。
    // 回退方案：走 widget 事件路径 OnMouseDown。
    auto* combo = As<ui::ComboBoxWidget>(w); if (!combo) return -1;
    if (combo->IsOpen()) return 0;
    ui::MouseEvent e{(combo->rect.left + combo->rect.right) * 0.5f,
                     (combo->rect.top + combo->rect.bottom) * 0.5f, 0, true};
    combo->OnMouseDown(e);
    return 0;
}

UI_API int ui_debug_combo_close(UiWidget w) {
    auto* combo = As<ui::ComboBoxWidget>(w); if (!combo) return -1;
    combo->Close();
    return 0;
}

UI_API int ui_debug_slider_set(UiWindow win, UiWidget w, float value) {
    auto* sl = As<ui::SliderWidget>(w); if (!sl) return -1;
    sl->SetValue(value);
    if (sl->onFloatChanged) sl->onFloatChanged(sl->Value());
    auto* wi = Win(win); if (wi) wi->Invalidate();
    return 0;
}

UI_API int ui_debug_number_set(UiWindow win, UiWidget w, float value) {
    auto* nb = As<ui::NumberBoxWidget>(w); if (!nb) return -1;
    nb->SetValue(value);
    if (nb->onFloatChanged) nb->onFloatChanged(nb->Value());
    auto* wi = Win(win); if (wi) wi->Invalidate();
    return 0;
}

UI_API int ui_debug_tab_set(UiWidget w, int index) {
    auto* tc = As<ui::TabControlWidget>(w); if (!tc) return -1;
    tc->SetActiveIndex(index);
    return 0;
}

UI_API int ui_debug_expander_set(UiWidget w, int expanded) {
    auto* ex = As<ui::ExpanderWidget>(w); if (!ex) return -1;
    ex->SetExpanded(expanded != 0);
    if (ex->onExpandedChanged) ex->onExpandedChanged(expanded != 0);
    return 0;
}

UI_API int ui_debug_splitview_set(UiWidget w, int open) {
    auto* sv = As<ui::SplitViewWidget>(w); if (!sv) return -1;
    sv->SetPaneOpen(open != 0);
    if (sv->onPaneChanged) sv->onPaneChanged(open != 0);
    return 0;
}

UI_API int ui_debug_flyout_show(UiWidget flyout, UiWidget anchor) {
    auto* fw = As<ui::FlyoutWidget>(flyout); if (!fw) return -1;
    auto* a  = W(anchor); if (!a) return -1;
    fw->Show(a);
    return 0;
}

UI_API int ui_debug_flyout_hide(UiWidget flyout) {
    auto* fw = As<ui::FlyoutWidget>(flyout); if (!fw) return -1;
    fw->Hide();
    return 0;
}

UI_API int ui_debug_text_set(UiWidget w, const wchar_t* text) {
    if (!text) return -1;
    if (auto* ti = As<ui::TextInputWidget>(w)) { ti->SetText(text); return 0; }
    if (auto* ta = As<ui::TextAreaWidget>(w))  { ta->SetText(text); return 0; }
    if (auto* lbl = As<ui::LabelWidget>(w))    { lbl->SetText(text); return 0; }
    return -1;
}

UI_API int ui_debug_scroll_set(UiWidget w, float y) {
    auto* sv = As<ui::ScrollViewWidget>(w); if (!sv) return -1;
    sv->SetScrollY(y);
    sv->DoLayout();
    return 0;
}

// ---- Context menu ----

UI_API int ui_debug_menu_is_open(UiWindow win) {
    auto* wi = Win(win); if (!wi) return 0;
    auto m = wi->ActiveMenu();
    return (m && m->IsVisible()) ? 1 : 0;
}

UI_API int ui_debug_menu_item_count(UiWindow win) {
    auto* wi = Win(win); if (!wi) return -1;
    auto m = wi->ActiveMenu(); if (!m) return -1;
    return m->ItemCount();
}

UI_API int ui_debug_menu_click_index(UiWindow win, int index) {
    auto* wi = Win(win); if (!wi) return -1;
    auto m = wi->ActiveMenu(); if (!m) return -1;
    return m->SimulateClickIndex(index) ? 0 : -1;
}

UI_API int ui_debug_menu_click_id(UiWindow win, int item_id) {
    auto* wi = Win(win); if (!wi) return -1;
    auto m = wi->ActiveMenu(); if (!m) return -1;
    int idx = m->FindIndexById(item_id);
    if (idx < 0) return -1;
    return m->SimulateClickIndex(idx) ? 0 : -1;
}

UI_API int ui_debug_menu_close(UiWindow win) {
    auto* wi = Win(win); if (!wi) return -1;
    wi->CloseMenu();
    return 0;
}

UI_API int ui_debug_menu_item_count_at(UiWindow win, const int* path, int depth) {
    auto* wi = Win(win); if (!wi) return -1;
    auto m = wi->ActiveMenu(); if (!m) return -1;
    return m->ItemCountAtPath(path, depth);
}

UI_API int ui_debug_menu_item_id_at(UiWindow win, const int* path, int depth) {
    auto* wi = Win(win); if (!wi) return -1;
    auto m = wi->ActiveMenu(); if (!m) return -1;
    return m->ItemIdAtPath(path, depth);
}

UI_API int ui_debug_menu_has_submenu_at(UiWindow win, const int* path, int depth) {
    auto* wi = Win(win); if (!wi) return 0;
    auto m = wi->ActiveMenu(); if (!m) return 0;
    return m->HasSubmenuAtPath(path, depth) ? 1 : 0;
}

UI_API int ui_debug_menu_click_path(UiWindow win, const int* path, int depth) {
    auto* wi = Win(win); if (!wi) return -1;
    auto m = wi->ActiveMenu(); if (!m) return -1;
    return m->SimulateClickPath(path, depth) ? 0 : -1;
}

UI_API void ui_debug_set_menu_autoclose(int enabled) {
    // enabled=0 → 抑制自动关闭；=非0 → 恢复默认
    ui::ContextMenu::g_debugSuppressAutoClose = (enabled == 0);
}

UI_API void ui_window_invoke_sync(UiWindow win, void (*fn)(void* ud), void* ud) {
    auto* wi = Win(win); if (!wi || !fn) return;
    wi->InvokeSync(fn, ud);
}

// ---- Frameless canvas mode ----

UI_API void ui_window_set_min_size(UiWindow win, int w_dip, int h_dip) {
    auto* wi = Win(win); if (!wi) return;
    /* 负值当 0 处理（= 恢复默认） */
    wi->SetMinSize(w_dip < 0 ? 0 : w_dip, h_dip < 0 ? 0 : h_dip);
}

UI_API void ui_window_set_background_mode(UiWindow win, int mode) {
    auto* wi = Win(win); if (!wi) return;
    wi->SetBackgroundMode(mode);
    wi->Invalidate();
}

UI_API void ui_widget_set_drag_window(UiWidget w, int enable) {
    auto* p = W(w); if (!p) return;
    p->dragWindow = (enable != 0);
}

// ---- 窗口几何（DIP-native） ----

UI_API void ui_window_set_rect(UiWindow win, int x_screen, int y_screen,
                                int w_dip, int h_dip) {
    auto* wi = Win(win); if (!wi) return;
    wi->SetWindowRect(x_screen, y_screen, w_dip, h_dip);
}

UI_API void ui_window_set_size(UiWindow win, int w_dip, int h_dip) {
    auto* wi = Win(win); if (!wi) return;
    wi->SetWindowSize(w_dip, h_dip);
}

UI_API void ui_window_set_position(UiWindow win, int x_screen, int y_screen) {
    auto* wi = Win(win); if (!wi) return;
    wi->SetWindowPosition(x_screen, y_screen);
}

UI_API void ui_window_get_rect_screen(UiWindow win,
                                       int* out_x, int* out_y,
                                       int* out_w_dip, int* out_h_dip) {
    auto* wi = Win(win); if (!wi) return;
    wi->GetWindowRectScreen(out_x, out_y, out_w_dip, out_h_dip);
}

UI_API void ui_window_resize_with_anchor(UiWindow win,
                                          int w_dip, int h_dip,
                                          float client_x_dip, float client_y_dip,
                                          int screen_x, int screen_y) {
    auto* wi = Win(win); if (!wi) return;
    wi->ResizeWithAnchor(w_dip, h_dip, client_x_dip, client_y_dip, screen_x, screen_y);
}

UI_API void ui_window_enable_canvas_mode(UiWindow win, int enable) {
    auto* wi = Win(win); if (!wi) return;
    wi->EnableCanvasMode(enable != 0);
}

// ---- Font / Text rendering (since 1.3.0) ----

static theme::TextRenderMode ToThemeMode(UiTextRenderMode m) {
    switch (m) {
    case UI_TEXT_RENDER_SMOOTH:     return theme::TextRenderMode::Smooth;
    case UI_TEXT_RENDER_CLEARTYPE:  return theme::TextRenderMode::ClearType;
    case UI_TEXT_RENDER_SHARP:      return theme::TextRenderMode::Sharp;
    case UI_TEXT_RENDER_GRAY_SHARP: return theme::TextRenderMode::GraySharp;
    case UI_TEXT_RENDER_ALIASED:    return theme::TextRenderMode::Aliased;
    }
    return theme::TextRenderMode::Smooth;
}
static UiTextRenderMode FromThemeMode(theme::TextRenderMode m) {
    switch (m) {
    case theme::TextRenderMode::Smooth:    return UI_TEXT_RENDER_SMOOTH;
    case theme::TextRenderMode::ClearType: return UI_TEXT_RENDER_CLEARTYPE;
    case theme::TextRenderMode::Sharp:     return UI_TEXT_RENDER_SHARP;
    case theme::TextRenderMode::GraySharp: return UI_TEXT_RENDER_GRAY_SHARP;
    case theme::TextRenderMode::Aliased:   return UI_TEXT_RENDER_ALIASED;
    }
    return UI_TEXT_RENDER_SMOOTH;
}

// 全局默认设完之后，刷新所有已存在窗口（让新字体 / 新模式立刻生效）
static void RefreshAllWindowsAfterGlobalChange() {
    /* 轻量刷新：flush 每个 Renderer 的 TextFormat 缓存 + 重建 fallback +
     * 应用新 TextRenderMode。 */
    auto& ctx = Ctx();
    if (auto* w = ctx.FirstWindow()) (void)w;  /* anchor to suppress unused-func warn */
    ctx.InvalidateAllWindows();
}

UI_API void ui_theme_set_default_font(const wchar_t* family) {
    theme::SetDefaultFontFamily(family);
    RefreshAllWindowsAfterGlobalChange();
}
UI_API const wchar_t* ui_theme_get_default_font(void) {
    return theme::DefaultFontFamily();
}

UI_API void ui_theme_set_cjk_font(const wchar_t* latin, const wchar_t* cjk) {
    theme::SetCjkFonts(latin, cjk);
    RefreshAllWindowsAfterGlobalChange();
}
UI_API const wchar_t* ui_theme_get_cjk_latin_font(void) { return theme::LatinFontFamily(); }
UI_API const wchar_t* ui_theme_get_cjk_cjk_font(void)   { return theme::CjkFontFamily(); }

UI_API void ui_theme_set_text_render_mode(UiTextRenderMode mode) {
    theme::SetTextRenderMode(ToThemeMode(mode));
    RefreshAllWindowsAfterGlobalChange();
}
UI_API UiTextRenderMode ui_theme_get_text_render_mode(void) {
    return FromThemeMode(theme::GetTextRenderMode());
}

UI_API void ui_window_set_default_font(UiWindow win, const wchar_t* family) {
    auto* wi = Win(win); if (!wi) return;
    wi->GetRenderer().SetDefaultFontFamily(family);
    wi->Invalidate();
}
UI_API void ui_window_set_cjk_font(UiWindow win, const wchar_t* latin, const wchar_t* cjk) {
    auto* wi = Win(win); if (!wi) return;
    wi->GetRenderer().SetCjkFonts(latin, cjk);
    wi->Invalidate();
}
UI_API void ui_window_set_text_render_mode(UiWindow win, UiTextRenderMode mode) {
    auto* wi = Win(win); if (!wi) return;
    wi->GetRenderer().SetTextRenderMode(ToThemeMode(mode));
    wi->Invalidate();
}
UI_API void ui_window_clear_font_override(UiWindow win) {
    auto* wi = Win(win); if (!wi) return;
    auto& r = wi->GetRenderer();
    r.SetDefaultFontFamily(nullptr);
    r.SetCjkFonts(nullptr, nullptr);
    /* SetTextRenderMode 没法传"清除"，直接把 override 标志清掉需要加一个方法 */
    r.SetTextRenderMode(theme::GetTextRenderMode());
    wi->Invalidate();
}

// ---- Dialog ----

UI_API int ui_debug_dialog_confirm(UiWindow win) {
    auto* wi = Win(win); if (!wi) return -1;
    auto* dlg = wi->activeDialog_; if (!dlg || !dlg->IsActive()) return -1;
    return dlg->OnKeyDown(VK_RETURN) ? 0 : -1;
}

UI_API int ui_debug_dialog_cancel(UiWindow win) {
    auto* wi = Win(win); if (!wi) return -1;
    auto* dlg = wi->activeDialog_; if (!dlg || !dlg->IsActive()) return -1;
    return dlg->OnKeyDown(VK_ESCAPE) ? 0 : -1;
}

// ---- HWND channel (Win32 PostMessage) ----

static HWND WinHwnd(UiWindow win) {
    auto* wi = Win(win);
    return wi ? wi->Handle() : nullptr;
}

static LPARAM PackXY(float x, float y, float dpiScale) {
    int px = (int)(x * dpiScale);
    int py = (int)(y * dpiScale);
    return MAKELPARAM(px & 0xFFFF, py & 0xFFFF);
}

UI_API int ui_debug_post_click(UiWindow win, float x, float y) {
    auto* wi = Win(win); if (!wi || !wi->Handle()) return -1;
    LPARAM lp = PackXY(x, y, wi->DpiScale());
    PostMessageW(wi->Handle(), WM_LBUTTONDOWN, MK_LBUTTON, lp);
    PostMessageW(wi->Handle(), WM_LBUTTONUP,   0,          lp);
    return 0;
}
UI_API int ui_debug_post_right_click(UiWindow win, float x, float y) {
    auto* wi = Win(win); if (!wi || !wi->Handle()) return -1;
    LPARAM lp = PackXY(x, y, wi->DpiScale());
    PostMessageW(wi->Handle(), WM_RBUTTONDOWN, MK_RBUTTON, lp);
    PostMessageW(wi->Handle(), WM_RBUTTONUP,   0,          lp);
    return 0;
}
UI_API int ui_debug_post_mouse_move(UiWindow win, float x, float y) {
    auto* wi = Win(win); if (!wi || !wi->Handle()) return -1;
    LPARAM lp = PackXY(x, y, wi->DpiScale());
    PostMessageW(wi->Handle(), WM_MOUSEMOVE, 0, lp);
    return 0;
}
UI_API int ui_debug_post_key(UiWindow win, int vk) {
    HWND h = WinHwnd(win); if (!h) return -1;
    PostMessageW(h, WM_KEYDOWN, (WPARAM)vk, 0);
    PostMessageW(h, WM_KEYUP,   (WPARAM)vk, 0);
    return 0;
}
UI_API int ui_debug_post_char(UiWindow win, unsigned int ch) {
    HWND h = WinHwnd(win); if (!h) return -1;
    PostMessageW(h, WM_CHAR, (WPARAM)ch, 0);
    return 0;
}

UI_API int ui_debug_pump(void) {
    int count = 0;
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        count++;
    }
    return count;
}

} // extern "C"
