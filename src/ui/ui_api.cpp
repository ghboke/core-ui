/*
 * ui_api.cpp — C API bridge implementation
 *
 * Every extern "C" function: look up handle → cast → call C++ method.
 */
#include "msgbox.h"
#include "draw_api_context.h"
#include "display_list.h"
#include "ui_context.h"
#include "ui_window.h"
#include "ui.h"        // pulls in all widget/control/builder headers
#include "image_view_gdi.h"  // GDI-based image view (for images > D2D texture limit)
#include "image_view_plus.h" // forward decl needed for GhImgView 临近的 ImageViewPlus 引用
#include "gh_img_view.h"     // 通用瓦块画布
#include "ocr_img_view.h"    // 图片划词 (OCR 文本选择画布)
#include "css/value.h"       // ParseColor for ui_theme_set_accent_hex
#include "page/compiler.h"   // ui::page::RefreshAllPageThemes — runtime theme push
#include "../../include/ui_core.h"

#include <windows.h>
#include <atomic>
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
    /* 启动加速: 共享 D3D11 设备后台预热 — 与 uix 编译/窗口创建并行,
     * 首窗 CreateRenderTarget 不再付 ~39ms 设备创建 (L 启动剖析 P1)。 */
    ui::Renderer::PrewarmSharedDeviceAsync();
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
    win->startMaximizedPending_ = (config->start_maximized != 0);
    int initX = (config->x != 0 || config->y != 0) ? config->x : CW_USEDEFAULT;
    int initY = (config->x != 0 || config->y != 0) ? config->y : CW_USEDEFAULT;
    /* Build 65+ (L14): 把 owner UiWindow handle 解析成 HWND 传给 Create.
     * 0 / 找不到 = nullptr (顶级窗口). */
    HWND ownerHwnd = nullptr;
    if (config->owner) {
        if (auto* ow = Ctx().GetWindow(config->owner)) ownerHwnd = ow->Handle();
    }
    if (!win->Create(
            config->title ? config->title : L"",
            config->width > 0 ? config->width : 800,
            config->height > 0 ? config->height : 600,
            config->system_frame == 0,   /* 0 = borderless (default) */
            config->resizable != 0,
            config->accept_files != 0,
            initX, initY,
            config->tool_window != 0,
            ownerHwnd)) {
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
    Ctx().GetWindow(id)->RegisterRenderWindow(id);
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
    if (w) w->ShowImmediate(true);
}

UI_API void ui_window_show_immediate_no_activate(UiWindow win) {
    auto* w = Win(win);
    if (w) w->ShowImmediate(false);
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

UI_API void ui_window_set_resizable(UiWindow win, int resizable) {
    auto* w = Win(win);
    if (w) w->SetResizable(resizable != 0);
}

UI_API void ui_window_invalidate(UiWindow win) {
    auto* w = Win(win);
    if (w) w->Invalidate();
}

UI_API void ui_window_begin_canvas_visual_transaction(UiWindow win) {
    auto* w = Win(win);
    if (w) w->BeginCanvasVisualTransaction();
}

UI_API void ui_window_end_canvas_visual_transaction(UiWindow win) {
    auto* w = Win(win);
    if (w) w->EndCanvasVisualTransaction();
}

UI_API void ui_window_focus_widget(UiWindow win, UiWidget widget) {
    auto* w = Win(win);
    if (w) w->FocusWidget(widget ? W(widget) : nullptr);
}

UI_API void ui_window_relayout(UiWindow win) {
    auto* w = Win(win);
    if (w) { w->LayoutRoot(); w->Invalidate(); }
}

UI_API void* ui_window_hwnd(UiWindow win) {
    auto* w = Win(win);
    return w ? (void*)w->Handle() : nullptr;
}

UI_API float ui_window_dpi_scale(UiWindow win) {
    auto* w = Win(win);
    return w ? w->DpiScale() : 1.0f;
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

UI_API void ui_window_on_close_request(UiWindow win, UiWindowCloseRequestCallback cb, void* userdata) {
    auto* w = Win(win);
    if (!w) return;
    if (!cb) { w->onCloseRequest = nullptr; return; }
    uint64_t id = win;
    w->onCloseRequest = [cb, userdata, id]() {
        return cb(id, userdata) != 0;
    };
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

/* BREAKING (build 75): 老的 imperative C API ui_menu_add_item / add_item_ex /
 * add_submenu(text, sub) 全删 — menu 现在只走声明式 .uix 路径 (PageState 内部
 * 用 AddItemContent / AddSubmenu(widget, sub)). 这一条体现"早期 lib 砍老兼容,
 * 别留 C 端老 caller 路径". 仍保留: ui_menu_create / destroy / add_separator /
 * set_enabled / set_bg_color / set_corner_radius / show / close — 这些是声明式
 * 路径也会用到的菜单生命周期 / 全局视觉控制 API. */

UI_API void ui_menu_add_separator(UiMenu menu) {
    auto m = Ctx().GetMenu(menu);
    if (m) m->AddSeparator();
}

UI_API void ui_menu_set_enabled(UiMenu menu, int id, int enabled) {
    auto m = Ctx().GetMenu(menu);
    if (m) m->SetEnabled(id, enabled != 0);
}

UI_API void ui_menu_set_bg_color(UiMenu menu, UiColor color) {
    auto m = Ctx().GetMenu(menu);
    if (m) m->SetBgColor({color.r, color.g, color.b, color.a});
}

UI_API void ui_menu_set_frosted_material(UiMenu menu, int enabled) {
    auto m = Ctx().GetMenu(menu);
    if (m) m->SetFrostedMaterial(enabled != 0);
}

UI_API void ui_menu_set_backdrop_blur(UiMenu menu, float radius) {
    auto m = Ctx().GetMenu(menu);
    if (m) m->SetBackdropBlur(radius);
}

UI_API void ui_menu_set_corner_radius(UiMenu menu, float radius) {
    auto m = Ctx().GetMenu(menu);
    if (m) m->SetCornerRadius(radius);
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
    if (w && text) w->ShowToast(text, duration_ms > 0 ? duration_ms : 2000, 0, 0, 0);
}

UI_API void ui_toast_ex(UiWindow win, const wchar_t* text, int duration_ms,
                        int position, int icon, int anim) {
    auto* w = Win(win);
    if (w && text) w->ShowToast(text, duration_ms > 0 ? duration_ms : 2000, position, icon, anim);
}

UI_API void ui_window_on_menu(UiWindow win, UiMenuCallback cb, void* userdata) {
    auto* w = Win(win);
    if (!w) return;
    if (!cb) { w->onMenuItemClick = nullptr; return; }
    uint64_t id = win;
    w->onMenuItemClick = [cb, userdata, id](const ui::MenuClickInfo* info) {
        cb(id, static_cast<UiMenuItem>(info), userdata);
    };
}

UI_API const char* ui_menu_item_id(UiMenuItem item) {
    auto* info = static_cast<const ui::MenuClickInfo*>(item);
    return (info && !info->id.empty()) ? info->id.c_str() : nullptr;
}

UI_API const char* ui_menu_item_attr(UiMenuItem item, const char* name) {
    auto* info = static_cast<const ui::MenuClickInfo*>(item);
    return info ? info->Attr(name) : nullptr;
}

UI_API void ui_window_on_right_click(UiWindow win, UiRightClickCallback cb, void* userdata) {
    auto* w = Win(win);
    if (!w) return;
    if (!cb) { w->onRightClick = nullptr; return; }
    uint64_t id = win;
    w->onRightClick = [cb, userdata, id](float x, float y) { cb(id, x, y, userdata); };
}

UI_API void ui_window_on_titlebar_drag(UiWindow win, UiTitleBarDragCallback cb, void* userdata) {
    auto* w = Win(win);
    if (!w) return;
    if (!cb) { w->onTitleBarDrag = nullptr; return; }
    uint64_t id = win;
    w->onTitleBarDrag = [cb, userdata, id]() { cb(id, userdata); };
}

UI_API void ui_window_set_titlebar_drag_intercept(UiWindow win, int on) {
    auto* w = Win(win);
    if (!w) return;
    w->titlebarDragIntercept_ = (on != 0);
    if (!on) w->tbDragTracking_ = false;
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

UI_API UiWidget ui_chips_input(const wchar_t* placeholder) {
    return Reg(std::make_shared<ui::ChipsInputWidget>(placeholder ? placeholder : L""));
}

UI_API void ui_chips_input_set_value(UiWidget w, const wchar_t* tpl) {
    auto* ci = As<ui::ChipsInputWidget>(w);
    if (ci) ci->SetValue(tpl ? tpl : L"");
}

UI_API const wchar_t* ui_chips_input_get_value(UiWidget w) {
    static thread_local std::wstring buf;
    auto* ci = As<ui::ChipsInputWidget>(w);
    buf = ci ? ci->Value() : L"";
    return buf.c_str();
}

UI_API void ui_chips_input_set_chip_labels(UiWidget w, const wchar_t* spec) {
    auto* ci = As<ui::ChipsInputWidget>(w);
    if (ci) ci->SetChipLabels(spec ? spec : L"");
}

UI_API void ui_chips_input_insert_chip(UiWidget w, const wchar_t* key) {
    auto* ci = As<ui::ChipsInputWidget>(w);
    if (ci && key) ci->InsertChip(key);
}

UI_API void ui_chips_input_on_changed(UiWidget w, UiChipsChangedCallback cb,
                                       void* userdata) {
    auto* ci = As<ui::ChipsInputWidget>(w);
    if (!ci) return;
    if (!cb) { ci->onTextChanged = nullptr; return; }
    uint64_t handle = w;
    ci->onTextChanged = [cb, userdata, handle](const std::wstring& v) {
        cb(handle, v.c_str(), userdata);
    };
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

// ================================================================
// MsgBox (build 158 — 取代 in-window ui_dialog_*)
// ================================================================

UI_API int ui_msgbox(UiWindow win,
                     const wchar_t* title, const wchar_t* message,
                     const wchar_t* const* buttons, int button_count,
                     int default_idx, int cancel_idx, int icon) {
    if (!buttons || button_count < 1) return -1;
    if (button_count > 4) button_count = 4;
    std::vector<std::wstring> btns;
    btns.reserve((size_t)button_count);
    for (int i = 0; i < button_count; ++i) {
        btns.emplace_back(buttons[i] ? buttons[i] : L"");
    }
    return ui::MsgBox::Show(win,
                            title ? title : L"", message ? message : L"",
                            btns, default_idx, cancel_idx, icon,
                            /*check_text=*/L"", /*check_initial=*/0,
                            /*btn_colors=*/nullptr)
        .button;
}

UI_API UiMsgBoxResult ui_msgbox_ex(UiWindow win, const UiMsgBoxParams* p) {
    UiMsgBoxResult res{};
    res.button = -1;
    /* struct_size 护栏的正确语义: 最小必需 = 到 icon 为止的老布局; 之后的
     * 字段按 size 分段取 — 旧编译的调用方传更小的 sizeof 仍然合法。 */
    constexpr uint32_t kMinSize =
        (uint32_t)(offsetof(UiMsgBoxParams, icon) + sizeof(int));
    if (!p || p->struct_size < kMinSize ||
        !p->buttons || p->button_count < 1) {
        return res;
    }
    auto field_avail = [p](size_t end_off) {
        return p->struct_size >= (uint32_t)end_off;
    };
    int n = p->button_count;
    if (n > 4) n = 4;
    std::vector<std::wstring> btns;
    btns.reserve((size_t)n);
    for (int i = 0; i < n; ++i) {
        btns.emplace_back(p->buttons[i] ? p->buttons[i] : L"");
    }
    const wchar_t* check_text =
        field_avail(offsetof(UiMsgBoxParams, check_initial) + sizeof(int))
            ? p->check_text : nullptr;
    const int check_initial = check_text ? p->check_initial : 0;
    const UiColor* colors =
        field_avail(offsetof(UiMsgBoxParams, button_colors) + sizeof(void*))
            ? p->button_colors : nullptr;
    /* build 172: 每按钮快捷键 (struct_size 护栏 — 旧调用方不含此字段仍合法)。 */
    std::vector<int> btn_keys;
    if (field_avail(offsetof(UiMsgBoxParams, button_keys) + sizeof(void*))
        && p->button_keys) {
        btn_keys.assign(p->button_keys, p->button_keys + n);
    }
    return ui::MsgBox::Show(
        win,
        p->title ? p->title : L"", p->message ? p->message : L"",
        btns, p->default_idx, p->cancel_idx, p->icon,
        check_text ? check_text : L"", check_initial, colors, btn_keys);
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
    iv->ClearImage();
}

/* 从 ImageView 的 CPU backing 读回像素到 CPU 内存。
 * 返回 1 成功（*out_pixels 由 malloc 分配，调用方用 ui_image_free_pixels 释放）。
 * tile 模式 / 无图 / 回读失败时返回 0。
 * 像素格式：BGRA (premultiplied alpha)，stride = w*4。*/
UI_API int ui_image_get_pixels(UiWidget w, UiWindow win,
                                void** out_pixels, int* out_w, int* out_h) {
    if (out_pixels) *out_pixels = nullptr;
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    (void)win;

    auto* iv = As<ui::ImageViewWidget>(w);
    if (!iv) return 0;
    return iv->CopyPixels(out_pixels, out_w, out_h) ? 1 : 0;
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
    if (!iv) {
        /* L7: 默默 return 让调用方拿不到反馈, 容易写成 dead code (e.g.
         * GuoheView 一直对着 ui_gh_img_view 调这个, 永不触发). 提示一下. */
        OutputDebugStringA("[core-ui] ui_image_on_mouse_move: widget is not ImageView. "
                           "For ui_gh_img_view or other widget types, use ui_widget_on_mouse_move.\n");
        return;
    }
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
    if (iv && wn) iv->SetTilePreview(pixels, width, height, stride, wn->GetRenderer());
}
UI_API void ui_image_clear_tiles(UiWidget w) {
    auto* iv = As<ui::ImageViewWidget>(w);
    if (iv) iv->ClearTiles();
}
UI_API void ui_image_evict_tile(UiWidget w, int tile_x, int tile_y) {
    auto* iv = As<ui::ImageViewWidget>(w);
    if (iv) iv->EvictTile(tile_x, tile_y);
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
// ImageViewPlus —— 万能图像查看（位图 / SVG / GIF / 分块 / 扩展）
// ================================================================

UI_API UiWidget ui_image_view_plus(void) {
    return Reg(std::make_shared<ui::ImageViewPlusWidget>());
}

UI_API int ui_image_view_plus_load(UiWidget w, UiWindow win, const wchar_t* path) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    auto* wn = Win(win);
    if (!iv || !wn || !path) return 0;
    return iv->LoadFromFile(path, wn->GetRenderer()) ? 1 : 0;
}

UI_API void ui_image_view_plus_set_pixels(UiWidget w, UiWindow win,
                                           const void* pixels, int width, int height, int stride) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    auto* wn = Win(win);
    if (iv && wn && pixels) iv->SetPixels(pixels, width, height, stride, wn->GetRenderer());
}

UI_API void ui_image_view_plus_clear(UiWidget w) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (iv) iv->Clear();
}

UI_API float ui_image_view_plus_get_zoom(UiWidget w) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    return iv ? iv->Zoom() : 1.0f;
}
UI_API void ui_image_view_plus_set_zoom(UiWidget w, float zoom) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (iv) iv->SetZoom(zoom);
}
UI_API void ui_image_view_plus_set_zoom_around(UiWidget w, float zoom,
                                               float anchor_x, float anchor_y) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (iv) iv->SetZoomAround(zoom, anchor_x, anchor_y);
}
UI_API void ui_image_view_plus_fit(UiWidget w) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (iv) iv->FitToView();
}
UI_API void ui_image_view_plus_reset(UiWidget w) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (iv) iv->ResetView();
}
UI_API void ui_image_view_plus_get_pan(UiWidget w, float* out_x, float* out_y) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (iv) { if (out_x) *out_x = iv->PanX(); if (out_y) *out_y = iv->PanY(); }
}
UI_API void ui_image_view_plus_set_pan(UiWidget w, float x, float y) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (iv) iv->SetPan(x, y);
}
UI_API void ui_image_view_plus_set_zoom_range(UiWidget w, float lo, float hi) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (iv) iv->SetZoomRange(lo, hi);
}

UI_API int ui_image_view_plus_width(UiWidget w) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    return iv ? iv->ImageWidth() : 0;
}
UI_API int ui_image_view_plus_height(UiWidget w) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    return iv ? iv->ImageHeight() : 0;
}
UI_API int ui_image_view_plus_raw_width(UiWidget w) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    return iv ? iv->RawImageWidth() : 0;
}
UI_API int ui_image_view_plus_raw_height(UiWidget w) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    return iv ? iv->RawImageHeight() : 0;
}

UI_API void ui_image_view_plus_set_rotation(UiWidget w, int angle) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (iv) iv->SetRotation(angle);
}
UI_API int ui_image_view_plus_get_rotation(UiWidget w) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    return iv ? iv->Rotation() : 0;
}

UI_API void ui_image_view_plus_set_checkerboard(UiWidget w, int on) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (iv) iv->SetCheckerboard(on != 0);
}
UI_API void ui_image_view_plus_set_antialias(UiWidget w, int on) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (iv) iv->SetAntialias(on != 0);
}
UI_API int ui_image_view_plus_get_antialias(UiWidget w) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    return iv ? (iv->Antialias() ? 1 : 0) : 0;
}

UI_API void ui_image_view_plus_set_free_pan(UiWidget w, int on) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (iv) iv->SetFreePan(on != 0);
}
UI_API int ui_image_view_plus_get_free_pan(UiWidget w) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    return iv ? (iv->FreePan() ? 1 : 0) : 0;
}

UI_API void ui_image_view_plus_set_loading(UiWidget w, int on) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (iv) iv->SetLoading(on != 0);
}
UI_API int ui_image_view_plus_get_loading(UiWidget w) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    return iv ? (iv->IsLoading() ? 1 : 0) : 0;
}

UI_API int ui_image_view_plus_is_animated(UiWidget w) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    return iv ? (iv->IsAnimated() ? 1 : 0) : 0;
}
UI_API int ui_image_view_plus_frame_count(UiWidget w) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    return iv ? iv->FrameCount() : 0;
}
UI_API int ui_image_view_plus_current_frame(UiWidget w) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    return iv ? iv->CurrentFrame() : 0;
}
UI_API void ui_image_view_plus_start_animation(UiWidget w) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (iv) iv->StartAnimation();
}
UI_API void ui_image_view_plus_stop_animation(UiWidget w) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (iv) iv->StopAnimation();
}

UI_API int ui_image_view_plus_is_vector(UiWidget w) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (!iv || !iv->Source()) return 0;
    return iv->Source()->Caps().vector ? 1 : 0;
}
UI_API const char* ui_image_view_plus_source_type(UiWidget w) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (!iv || !iv->Source()) return "";
    return iv->Source()->TypeName();
}

UI_API void ui_image_view_plus_set_crop_mode(UiWidget w, int on) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (iv) iv->SetCropMode(on != 0);
}
UI_API int ui_image_view_plus_is_crop_mode(UiWidget w) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    return iv ? (iv->IsCropMode() ? 1 : 0) : 0;
}
UI_API void ui_image_view_plus_set_crop_rect(UiWidget w, float x, float y, float cw, float ch) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (iv) iv->SetCropRect(x, y, cw, ch);
}
UI_API void ui_image_view_plus_get_crop_rect(UiWidget w, float* x, float* y, float* cw, float* ch) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (!iv) return;
    float xx, yy, ww, hh;
    iv->GetCropRect(xx, yy, ww, hh);
    if (x)  *x  = xx;
    if (y)  *y  = yy;
    if (cw) *cw = ww;
    if (ch) *ch = hh;
}
UI_API void ui_image_view_plus_set_crop_aspect(UiWidget w, float ratio) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (iv) iv->SetCropAspectRatio(ratio);
}
UI_API void ui_image_view_plus_reset_crop(UiWidget w) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (iv) iv->ResetCrop();
}

UI_API void ui_image_view_plus_on_crop_changed(UiWidget w, UiCropCallback cb, void* ud) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (!iv) return;
    UiWidget apiHandle = w;
    iv->onCropChanged = [cb, ud, apiHandle](float x, float y, float cw, float ch) {
        if (cb) cb(apiHandle, x, y, cw, ch, ud);
    };
}

UI_API void ui_image_view_plus_on_loaded(UiWidget w, UiLoadedCallback cb, void* ud) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (!iv) return;
    UiWidget apiHandle = w;
    iv->onLoaded = [cb, ud, apiHandle]() {
        if (cb) cb(apiHandle, ud);
    };
}

UI_API void ui_image_view_plus_on_load_failed(UiWidget w, UiLoadFailedCallback cb, void* ud) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (!iv) return;
    UiWidget apiHandle = w;
    iv->onLoadFailed = [cb, ud, apiHandle](const std::wstring& path) {
        if (cb) cb(apiHandle, path.c_str(), ud);
    };
}

UI_API void ui_image_view_plus_on_viewport_changed(UiWidget w, UiViewportCallback cb, void* ud) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (!iv) return;
    UiWidget apiHandle = w;
    iv->onViewportChanged = [cb, ud, apiHandle](float z, float px, float py) {
        if (cb) cb(apiHandle, z, px, py, ud);
    };
}

UI_API void ui_image_view_plus_begin_tiled(UiWidget w, UiWindow win,
                                             int fullW, int fullH, int tileSize) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    auto* wn = Win(win);
    if (iv && wn) iv->BeginTiled(fullW, fullH, tileSize, wn->GetRenderer());
}

UI_API void ui_image_view_plus_set_tile(UiWidget w, int tx, int ty,
                                         const void* pixels, int width, int height, int stride) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (!iv || !pixels) return;
    auto* src = iv->Source();
    if (!src) return;
    auto* tiled = dynamic_cast<ui::IImageSource::ITiledSource*>(src);
    if (tiled) tiled->SetTile(tx, ty, pixels, width, height, stride);
}

UI_API void ui_image_view_plus_clear_tiles(UiWidget w) {
    auto* iv = As<ui::ImageViewPlusWidget>(w);
    if (!iv) return;
    auto* src = iv->Source();
    if (!src) return;
    auto* tiled = dynamic_cast<ui::IImageSource::ITiledSource*>(src);
    if (tiled) tiled->ClearTiles();
}

// ================================================================
// GhImgView — 通用瓦块画布（数据形状契约：BGRA8 premul + 256 瓦块 + pyramid）
// ================================================================

UI_API UiWidget ui_gh_img_view(void) {
    return Reg(std::make_shared<ui::GhImgViewWidget>());
}

UI_API void ui_gh_img_view_begin(UiWidget w, UiWindow win, const UiGhImgViewInfo* info) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    auto* wn = Win(win);
    if (!gv || !wn || !info) return;
    ui::GhImgViewWidget::Info ii;
    ii.fullWidth   = info->full_width;
    ii.fullHeight  = info->full_height;
    ii.tileSize    = info->tile_size ? info->tile_size : 256u;
    ii.levels      = info->levels    ? info->levels    : 1u;
    ii.pixelFormat = info->pixel_format;
    ii.keepPreview = info->keep_preview != 0;   /* L168: 保留现有 preview 兜底层 */
    gv->Begin(ii, wn->GetRenderer());
}

UI_API void ui_gh_img_view_set_preview(UiWidget w, UiWindow win,
                                        const void* bgra, uint32_t pw, uint32_t ph,
                                        uint32_t stride) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    auto* wn = Win(win);
    if (!gv || !wn || !bgra) return;
    gv->SetPreview(bgra, pw, ph, stride, wn->GetRenderer());
}

UI_API void ui_gh_img_view_set_tile(UiWidget w, UiWindow win,
                                     uint32_t level, uint32_t tx, uint32_t ty,
                                     const void* bgra, uint32_t tw, uint32_t th,
                                     uint32_t stride) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    auto* wn = Win(win);
    if (!gv || !wn || !bgra) return;
    gv->SetTile(level, tx, ty, bgra, tw, th, stride, wn->GetRenderer());
}

UI_API void ui_gh_img_view_clear_level(UiWidget w, uint32_t level) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (gv) gv->ClearLevel(level);
}

UI_API void ui_gh_img_view_begin_tile_batch(UiWidget w) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (gv) gv->BeginTileBatch();
}

UI_API void ui_gh_img_view_end_tile_batch(UiWidget w) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (gv) gv->EndTileBatch();
}

UI_API void ui_gh_img_view_begin_view_update(UiWidget w) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (gv) gv->BeginViewUpdate();
}

UI_API void ui_gh_img_view_end_view_update(UiWidget w) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (gv) gv->EndViewUpdate();
}

UI_API void ui_gh_img_view_clear(UiWidget w) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (gv) gv->Clear();
}

UI_API int ui_gh_img_view_set_svg_file(UiWidget w, const wchar_t* path,
                                          uint32_t* out_w, uint32_t* out_h) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (!gv || !path) return -1;
    /* 找 widget 所属 window 拿 Renderer (SVG 解析需要 ID2D1DeviceContext5). */
    auto* wi = Ctx().FindWindowByWidget(gv);
    if (!wi) return -2;
    if (!gv->SetSvgFromFile(path, wi->GetRenderer())) return -3;
    if (out_w) *out_w = gv->GetInfo().fullWidth;
    if (out_h) *out_h = gv->GetInfo().fullHeight;
    return 0;
}

UI_API int ui_gh_img_view_is_svg_mode(UiWidget w) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    return (gv && gv->IsSvgMode()) ? 1 : 0;
}

UI_API int ui_gh_img_view_render_svg_to_bgra(UiWidget w,
                                                uint32_t target_w,
                                                uint32_t target_h,
                                                uint8_t* out_bgra,
                                                uint32_t* out_w,
                                                uint32_t* out_h) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (!gv) return -1;
    auto* wi = Ctx().FindWindowByWidget(gv);
    if (!wi) return -2;
    return gv->RenderSvgToBgra(target_w, target_h, out_bgra, out_w, out_h,
                                  wi->GetRenderer());
}

UI_API void ui_gh_img_view_set_auto_level(UiWidget w, int on) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (gv) gv->SetAutoLevel(on != 0);
}
UI_API int ui_gh_img_view_get_auto_level(UiWidget w) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    return (gv && gv->AutoLevel()) ? 1 : 0;
}
UI_API void ui_gh_img_view_set_active_level(UiWidget w, uint32_t level) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (gv) gv->SetActiveLevel(level);
}
UI_API uint32_t ui_gh_img_view_get_active_level(UiWidget w) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    return gv ? gv->ActiveLevel() : 0u;
}
UI_API void ui_gh_img_view_set_antialias(UiWidget w, int on) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (gv) gv->SetAntialias(on != 0);
}
UI_API int ui_gh_img_view_get_antialias(UiWidget w) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    return (gv && gv->Antialias()) ? 1 : 0;
}
UI_API void ui_gh_img_view_set_checkerboard(UiWidget w, int on) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (gv) gv->SetCheckerboard(on != 0);
}
UI_API int ui_gh_img_view_get_checkerboard(UiWidget w) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    return (gv && gv->Checkerboard()) ? 1 : 0;
}
UI_API void ui_gh_img_view_set_shadow(UiWidget w, int on) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (gv) gv->SetShadow(on != 0);
}
UI_API int ui_gh_img_view_get_shadow(UiWidget w) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    return (gv && gv->Shadow()) ? 1 : 0;
}
UI_API void ui_gh_img_view_set_fit_padding(UiWidget w, float dip) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (gv) gv->SetFitPadding(dip);
}
UI_API float ui_gh_img_view_get_fit_padding(UiWidget w) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    return gv ? gv->FitPadding() : 0.0f;
}
UI_API void ui_gh_img_view_set_wheel_zoom_enabled(UiWidget w, int on) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (gv) gv->SetWheelZoomEnabled(on != 0);
}
UI_API int ui_gh_img_view_get_wheel_zoom_enabled(UiWidget w) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    return (gv && gv->WheelZoomEnabled()) ? 1 : 0;
}
UI_API void ui_gh_img_view_set_pan_lock(UiWidget w, int lock_x, int lock_y) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (gv) gv->SetPanLock(lock_x != 0, lock_y != 0);
}
UI_API void ui_gh_img_view_get_pan_lock(UiWidget w, int* out_lock_x, int* out_lock_y) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (out_lock_x) *out_lock_x = (gv && gv->PanLockX()) ? 1 : 0;
    if (out_lock_y) *out_lock_y = (gv && gv->PanLockY()) ? 1 : 0;
}

UI_API float ui_gh_img_view_get_zoom(UiWidget w) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    return gv ? gv->Zoom() : 1.0f;
}
UI_API void ui_gh_img_view_set_dpi_scale(UiWidget w, float scale) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (gv) gv->SetDpiScale(scale);
}
UI_API void ui_gh_img_view_set_zoom(UiWidget w, float zoom) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (gv) gv->SetZoom(zoom);
}
UI_API void ui_gh_img_view_set_zoom_around(UiWidget w, float zoom,
                                            float anchor_x, float anchor_y) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (gv) gv->SetZoomAround(zoom, anchor_x, anchor_y);
}
UI_API void ui_gh_img_view_set_zoom_range(UiWidget w, float lo, float hi) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (gv) gv->SetZoomRange(lo, hi);
}
UI_API void ui_gh_img_view_get_pan(UiWidget w, float* out_x, float* out_y) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (!gv) return;
    if (out_x) *out_x = gv->PanX();
    if (out_y) *out_y = gv->PanY();
}
UI_API void ui_gh_img_view_set_pan(UiWidget w, float x, float y) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (gv) gv->SetPan(x, y);
}
UI_API void ui_gh_img_view_fit(UiWidget w) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (gv) gv->Fit();
}
UI_API void ui_gh_img_view_reset(UiWidget w) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (gv) gv->Reset();
}

UI_API void ui_gh_img_view_set_rotation(UiWidget w, int angle) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (gv) gv->SetRotation(angle);
}
UI_API int  ui_gh_img_view_get_rotation(UiWidget w) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    return gv ? gv->Rotation() : 0;
}

UI_API void ui_gh_img_view_on_viewport(UiWidget w,
                                        UiGhImgViewViewportCallback cb,
                                        void* userdata) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (!gv) return;
    UiWidget apiHandle = w;
    gv->onViewportChanged = [cb, userdata, apiHandle](const ui::GhImgViewWidget::Viewport& vp) {
        if (!cb) return;
        UiGhImgViewport out;
        out.active_level = vp.activeLevel;
        out.zoom         = vp.zoom;
        out.pan_x        = vp.panX;
        out.pan_y        = vp.panY;
        out.visible_tx0  = vp.visibleTx0;
        out.visible_ty0  = vp.visibleTy0;
        out.visible_tx1  = vp.visibleTx1;
        out.visible_ty1  = vp.visibleTy1;
        cb(apiHandle, &out, userdata);
    };
}

// L48: tile evict callback — NotifyViewport trim viewport 外 tile 时 fire,
// caller 同步自己端 pushed_tiles_ erase.
UI_API void ui_gh_img_view_on_tile_evicted(UiWidget w,
                                            UiGhImgViewTileEvictedCallback cb,
                                            void* userdata) {
    auto* gv = As<ui::GhImgViewWidget>(w);
    if (!gv) return;
    UiWidget apiHandle = w;
    gv->onTileEvicted = [cb, userdata, apiHandle](uint32_t level, uint32_t tx, uint32_t ty) {
        if (cb) cb(apiHandle, level, tx, ty, userdata);
    };
}

// ================================================================
// OcrImgView — 图片划词 (OCR 文本选择画布)
// ================================================================

UI_API UiWidget ui_ocr_img_view(void) {
    return Reg(std::make_shared<ui::OcrImgViewWidget>());
}

UI_API int ui_ocr_img_view_set_image(UiWidget w, UiWindow win,
                                      const void* bgra, uint32_t width,
                                      uint32_t height, uint32_t stride) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    auto* wn = Win(win);
    if (!ov || !wn || !bgra) return ui::OcrImgViewWidget::kSetImageBadArgs;
    return ov->SetImage(bgra, width, height, stride, wn->GetRenderer());
}

UI_API void ui_ocr_img_view_clear_image(UiWidget w) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    if (ov) ov->ClearImage();
}

UI_API void ui_ocr_img_view_set_text(UiWidget w, const UiOcrTextItem* items,
                                      uint32_t count, size_t item_stride,
                                      UiOcrGranularity granularity) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    if (!ov) return;
    if (!items || count == 0 || item_stride == 0) {
        ov->Model().Clear();
        return;
    }
    /* stride 兼容拷贝: 宿主结构体可能比当前 lib 版本旧 (小 stride, 尾部字段
     * 取默认) 或新 (大 stride, 多余字节忽略)。逐字段搬, 不整块 memcpy。 */
    std::vector<ui::OcrTextItem> v;
    v.reserve(count);
    const auto* base = reinterpret_cast<const uint8_t*>(items);
    const size_t known = std::min(item_stride, sizeof(UiOcrTextItem));
    for (uint32_t i = 0; i < count; ++i) {
        UiOcrTextItem src{};
        memcpy(&src, base + (size_t)i * item_stride, known);
        ui::OcrTextItem it;
        it.text  = src.text ? src.text : "";
        memcpy(it.quad, src.quad, sizeof(it.quad));
        it.block = src.block;
        it.line  = src.line;
        it.word  = src.word;
        it.flags = src.flags;
        v.push_back(std::move(it));
    }
    /* BLOCK 粒度: 入口拆成字, 让选择粒度统一到字级。CHAR 粒度直接用 ——
     * 但 word 字段若全为 0 (宿主没填分词), 双击会选中整行, 这是宿主的
     * 数据问题, lib 不猜。 */
    if (granularity == UI_OCR_GRANULARITY_BLOCK) {
        std::vector<ui::OcrTextItem> chars;
        ui::SplitItemsIntoChars(v.data(), v.size(), chars);
        ov->Model().SetItems(chars.data(), chars.size());
    } else {
        ov->Model().SetItems(v.data(), v.size());
    }
}

UI_API void ui_ocr_img_view_clear_text(UiWidget w) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    if (ov) ov->Model().Clear();
}

UI_API uint32_t ui_ocr_img_view_item_count(UiWidget w) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    return ov ? (uint32_t)ov->Model().ItemCount() : 0u;
}

UI_API const char* ui_ocr_img_view_selected_text(UiWidget w) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    if (!ov) return "";
    return ov->SelectedTextUtf8().c_str();
}

UI_API void ui_ocr_img_view_select_all(UiWidget w) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    if (ov) ov->Model().SelectAll();
}

UI_API void ui_ocr_img_view_clear_selection(UiWidget w) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    if (ov) ov->Model().ClearSelection();
}

UI_API int ui_ocr_img_view_has_selection(UiWidget w) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    return (ov && ov->Model().HasSelection()) ? 1 : 0;
}

UI_API int ui_ocr_img_view_copy_selection(UiWidget w) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    return (ov && ov->CopySelectionToClipboard()) ? 1 : 0;
}

UI_API void ui_ocr_img_view_select_range(UiWidget w, uint32_t first,
                                          uint32_t last) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    if (!ov) return;
    auto& model = ov->Model();
    const size_t count = model.ItemCount();
    if (count == 0) return;
    if (first > last) { const uint32_t t = first; first = last; last = t; }
    const uint32_t maxIndex = (uint32_t)(count - 1);
    if (first > maxIndex) first = maxIndex;
    if (last  > maxIndex) last  = maxIndex;
    /* 走跟真实拖选同一条状态机路径 (Begin→Update→End), 选区语义与手势
     * 完全一致, onSelectionChanged 也照常 fire。 */
    model.BeginSelect(first);
    model.UpdateSelect(last);
    model.EndSelect();
}

UI_API int ui_ocr_img_view_selection_range(UiWidget w, uint32_t* first,
                                            uint32_t* last) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    if (!ov) return 0;
    const auto& selection = ov->Model().Selection();
    if (selection.empty()) return 0;
    if (first) *first = selection.front().first;
    if (last)  *last  = selection.back().last;
    return 1;
}

UI_API int ui_ocr_img_view_item_meta(UiWidget w, uint32_t index,
                                      uint32_t* block, uint32_t* line,
                                      uint32_t* word) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    if (!ov) return 0;
    const auto& model = ov->Model();
    if (index >= model.ItemCount()) return 0;
    const ui::OcrTextItem& item = model.Item(index);
    if (block) *block = item.block;
    if (line)  *line  = item.line;
    if (word)  *word  = item.word;
    return 1;
}

UI_API void ui_ocr_img_view_on_selection_changed(UiWidget w,
                                                  UiOcrSelectionCallback cb,
                                                  void* userdata) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    if (!ov) return;
    UiWidget apiHandle = w;
    auto* widget = ov;
    ov->Model().onSelectionChanged = [cb, userdata, apiHandle, widget] {
        /* widget 内部依赖这个 callback 失效 selected_text 缓存 + 重绘,
         * 宿主注册后必须保持该行为再转发。 */
        widget->OnModelSelectionChanged();
        if (cb) cb(apiHandle, userdata);
    };
}

UI_API void ui_ocr_img_view_fit(UiWidget w) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    if (ov) ov->Fit();
}

UI_API void ui_ocr_img_view_reset(UiWidget w) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    if (ov) ov->Reset();
}

UI_API void ui_ocr_img_view_set_zoom(UiWidget w, float zoom) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    if (ov) ov->SetZoom(zoom);
}

UI_API float ui_ocr_img_view_get_zoom(UiWidget w) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    return ov ? ov->Zoom() : 1.0f;
}

UI_API void ui_ocr_img_view_set_pan(UiWidget w, float px, float py) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    if (ov) ov->SetPan(px, py);
}

UI_API void ui_ocr_img_view_get_pan(UiWidget w, float* px, float* py) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    if (px) *px = ov ? ov->PanX() : 0.0f;
    if (py) *py = ov ? ov->PanY() : 0.0f;
}

UI_API void ui_ocr_img_view_set_rotation(UiWidget w, int angle) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    if (ov) ov->SetRotation(angle);
}

UI_API int ui_ocr_img_view_get_rotation(UiWidget w) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    return ov ? ov->Rotation() : 0;
}

UI_API void ui_ocr_img_view_image_to_screen(UiWidget w, float ix, float iy,
                                             float* sx, float* sy) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    float x = 0, y = 0;
    if (ov) ov->ImageToScreen(ix, iy, x, y);
    if (sx) *sx = x;
    if (sy) *sy = y;
}

UI_API void ui_ocr_img_view_screen_to_image(UiWidget w, float sx, float sy,
                                             float* ix, float* iy) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    float x = 0, y = 0;
    if (ov) ov->ScreenToImage(sx, sy, x, y);
    if (ix) *ix = x;
    if (iy) *iy = y;
}

UI_API void ui_ocr_img_view_set_highlight_color(UiWidget w, float rr, float gg,
                                                 float bb, float aa) {
    auto* ov = As<ui::OcrImgViewWidget>(w);
    if (ov) ov->SetHighlightColor(D2D1_COLOR_F{ rr, gg, bb, aa });
}

// ================================================================
// ImageViewGDI — GDI-based image view (bypasses D2D texture limit)
// 用途：显示超过 D3D feature level 最大纹理 (16384 常见) 的大图
// 架构：CreateDIBSection + StretchBlt 子窗口，无 GPU 参与
// ================================================================

UI_API UiWidget ui_image_view_gdi(void) {
    return Reg(std::make_shared<ui::ImageViewGDIWidget>());
}

UI_API void ui_image_gdi_set_parent(UiWidget w, UiWindow win) {
    auto* iv = As<ui::ImageViewGDIWidget>(w);
    auto* wn = Win(win);
    if (iv && wn) iv->SetParentHwnd(wn->Handle());
}

UI_API void ui_image_gdi_set_pixels(UiWidget w, const void* pixels,
                                     int width, int height, int stride) {
    auto* iv = As<ui::ImageViewGDIWidget>(w);
    if (iv && pixels) iv->SetPixels(pixels, width, height, stride);
}

UI_API void ui_image_gdi_set_file(UiWidget w, const wchar_t* path) {
    auto* iv = As<ui::ImageViewGDIWidget>(w);
    if (iv && path) iv->SetFile(path);
}

UI_API void ui_image_gdi_clear(UiWidget w) {
    auto* iv = As<ui::ImageViewGDIWidget>(w);
    if (iv) iv->Clear();
}

UI_API float ui_image_gdi_get_zoom(UiWidget w) {
    auto* iv = As<ui::ImageViewGDIWidget>(w);
    return iv ? iv->Zoom() : 1.0f;
}

UI_API void ui_image_gdi_set_zoom(UiWidget w, float zoom) {
    auto* iv = As<ui::ImageViewGDIWidget>(w);
    if (iv) iv->SetZoom(zoom);
}

UI_API void ui_image_gdi_fit(UiWidget w) {
    auto* iv = As<ui::ImageViewGDIWidget>(w);
    if (iv) iv->FitToView();
}

UI_API int ui_image_gdi_width(UiWidget w) {
    auto* iv = As<ui::ImageViewGDIWidget>(w);
    return iv ? iv->ImageWidth() : 0;
}

UI_API int ui_image_gdi_height(UiWidget w) {
    auto* iv = As<ui::ImageViewGDIWidget>(w);
    return iv ? iv->ImageHeight() : 0;
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

UI_API void ui_icon_button_set_icon_color_role(UiWidget w, UiIconColorRole role) {
    auto* ib = As<ui::IconButtonWidget>(w);
    if (ib) ib->SetIconColorRole(static_cast<int>(role));
}

UI_API void ui_icon_button_set_icon_padding(UiWidget w, float padding) {
    auto* ib = As<ui::IconButtonWidget>(w);
    if (ib) ib->SetIconPadding(padding);
}

UI_API void ui_icon_button_set_hover_visual(UiWidget w, int enabled) {
    auto* ib = As<ui::IconButtonWidget>(w);
    if (ib) ib->SetHoverVisual(enabled != 0);
}
UI_API int  ui_icon_button_get_hover_visual(UiWidget w) {
    auto* ib = As<ui::IconButtonWidget>(w);
    return ib ? (ib->HoverVisual() ? 1 : 0) : 1;
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

UI_API void ui_titlebar_set_icon_pixels(UiWidget titlebar,
                                         const void* rgba, int width, int height) {
    auto* tb = As<ui::TitleBarWidget>(titlebar);
    if (tb) tb->SetIconFromPixels(static_cast<const uint8_t*>(rgba), width, height);
}

UI_API void ui_titlebar_set_bg_color(UiWidget titlebar, UiColor color) {
    auto* tb = As<ui::TitleBarWidget>(titlebar);
    if (tb) tb->SetCustomBgColor({color.r, color.g, color.b, color.a});
}

UI_API void ui_titlebar_set_title_weight(UiWidget titlebar, int weight) {
    auto* tb = As<ui::TitleBarWidget>(titlebar);
    if (tb) tb->SetTitleWeight(weight);
}

// ================================================================
// Widget tree operations
// ================================================================

// 运行时改 widget 几何 / 树结构后必须 mark layout dirty + invalidate window，
// 否则 widget rect 一直停在初始 [0,0,0,0]，肉眼不可见。下游用 C API 写
// "运行时插入 / 改尺寸" 的 100% 会撞这个坑。
static void MarkLayoutAndRepaint() {
    ui::RequestLayout();
    Ctx().InvalidateAllWindows();
}

UI_API void ui_widget_add_child(UiWidget parent, UiWidget child) {
    auto p = Ctx().handles.Lookup(parent);
    auto c = Ctx().handles.Lookup(child);
    if (p && c) {
        p->AddChild(c);
        MarkLayoutAndRepaint();
    }
}

UI_API void ui_widget_remove_child(UiWidget parent, UiWidget child) {
    auto* p = W(parent);
    auto* c = W(child);
    if (p && c) {
        p->RemoveChild(c);
        MarkLayoutAndRepaint();
    }
}

UI_API void ui_widget_destroy(UiWidget widget) {
    Ctx().handles.Remove(widget);
}

UI_API UiWidget ui_widget_find_by_id(UiWidget root, const char* id) {
    auto* r = W(root);
    if (!r || !id) return UI_INVALID;
    auto* found = r->FindById(id);
    if (!found) return UI_INVALID;
    // 若已在 handle table 里就复用；否则新插入一个句柄（markup / HTML 构建
    // 的 widget 默认不在 handle table 里，直接 FindHandle 会返回 0）。
    uint64_t h = Ctx().handles.FindHandle(found);
    if (!h) h = Ctx().handles.Insert(found->shared_from_this());
    // CustomWidget 在 OnDraw / 鼠标 / 键盘回调里会把 apiHandle 传回给用户，
    // 用户拿这个 handle 调 ui_custom_focus / ui_widget_invalidate 等 API。
    // ui_custom() 创建路径里 apiHandle 在工厂里就 stamp 了；HTML 工厂构造
    // 时没法知道 handle，所以这里在第一次 Insert 时回填一次。
    if (auto* cw = dynamic_cast<ui::CustomWidget*>(found); cw && cw->apiHandle == 0) {
        cw->apiHandle = h;
    }
    return h;
}

// ================================================================
// Widget common properties
// ================================================================

UI_API void ui_widget_set_id(UiWidget w, const char* id) {
    auto* p = W(w); if (p && id) p->id = id;
}

UI_API void ui_widget_set_width(UiWidget w, float width) {
    auto* p = W(w); if (p) { p->fixedW = width; MarkLayoutAndRepaint(); }
}

UI_API void ui_widget_set_height(UiWidget w, float height) {
    auto* p = W(w); if (p) { p->fixedH = height; MarkLayoutAndRepaint(); }
}

UI_API void ui_widget_set_size(UiWidget w, float width, float height) {
    auto* p = W(w);
    if (p) { p->fixedW = width; p->fixedH = height; MarkLayoutAndRepaint(); }
}

UI_API void ui_widget_set_layout_pinned(UiWidget w, int pinned) {
    if (auto* p = W(w)) p->layoutPinned = pinned != 0;
}

UI_API void ui_widget_set_expand(UiWidget w, int expand) {
    auto* p = W(w);
    if (p) { p->expanding = (expand != 0); MarkLayoutAndRepaint(); }
}

UI_API void ui_widget_set_padding(UiWidget w, float left, float top, float right, float bottom) {
    auto* p = W(w);
    if (p) {
        p->padL = left; p->padT = top; p->padR = right; p->padB = bottom;
        MarkLayoutAndRepaint();
    }
}

UI_API void ui_widget_set_padding_uniform(UiWidget w, float pad) {
    auto* p = W(w);
    if (p) {
        p->padL = p->padT = p->padR = p->padB = pad;
        MarkLayoutAndRepaint();
    }
}

UI_API void ui_widget_set_gap(UiWidget w, float gap) {
    auto* p = W(w); if (p) p->Gap(gap);
}

UI_API void ui_widget_set_visible(UiWidget w, int visible) {
    /* L95: 改可见性会折叠/展开 flex 布局, 必须跟 set_size/set_expand/add_child
     * 一样请求重布局+重绘 —— 否则隐藏时算出的 0×0 rect 卡住, set_visible(0→1)
     * 后控件不显示, 要等别的 relayout(如重开窗口)才生效. 之前漏了这行. */
    auto* p = W(w);
    if (p && p->visible != (visible != 0)) {
        p->visible = (visible != 0);
        MarkLayoutAndRepaint();
    }
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

UI_API void ui_widget_set_cursor(UiWidget w, int cursor) {
    auto* p = W(w);
    if (!p) return;
    /* clamp 到 enum 合法范围, 越界保留当前值 (不破坏现有 widget 状态). */
    if (cursor < 0 || cursor > (int)ui::CursorKind::None) return;
    p->cursor = static_cast<ui::CursorKind>(cursor);
}

UI_API int ui_widget_get_cursor(UiWidget w) {
    auto* p = W(w);
    return p ? static_cast<int>(p->cursor) : 0;
}

/* 通用 widget mouse_move hook — 任意 widget 类型 (div / gh_img_view /
 * image / svg / custom...) 都可挂. 利用 ui_window dispatch 已经在 hit
 * widget 上触发的 Widget::onMouseMoveHook 字段, 不依赖具体子类.
 * x/y 为 widget-local DIP (减去 widget rect 左上). cb=NULL 解绑. */
UI_API void ui_widget_on_mouse_move(UiWidget w,
                                     UiWidgetMouseMoveCallback cb,
                                     void* userdata) {
    auto* p = W(w);
    if (!p) return;
    if (!cb) { p->onMouseMoveHook = nullptr; return; }
    uint64_t handle = w;
    /* 捕 p 指针 hook 内部用 — Widget 生命周期跟 hook 一致 (hook 存在 widget
     * 上, widget 析构时一起销毁), p 不会悬空. */
    p->onMouseMoveHook = [cb, userdata, handle, p](const ui::MouseEvent& e) {
        cb(handle, e.x - p->rect.left, e.y - p->rect.top, userdata);
    };
}

/* 通用 widget mouse_leave hook — 跟 web mouseleave 语义对齐. 触发路径两条:
 * (a) cursor 在同窗口内从 widget A 转去 widget B, A (含其 ancestor 链中
 *     不在 B 链的部分) 触发 leave;
 * (b) cursor 整体离开窗口 (WM_MOUSELEAVE), 当前 hovered widget 链全员触发.
 * cb=NULL 解绑. 无 x/y 参数 — leave 时 cursor 在别处或已脱离, 给坐标无意义. */
UI_API void ui_widget_on_mouse_leave(UiWidget w,
                                      UiWidgetMouseLeaveCallback cb,
                                      void* userdata) {
    auto* p = W(w);
    if (!p) return;
    if (!cb) { p->onMouseLeaveHook = nullptr; return; }
    uint64_t handle = w;
    p->onMouseLeaveHook = [cb, userdata, handle]() {
        cb(handle, userdata);
    };
}

/* Drag & drop (build 259+) — 见 ui_core.h 注释. */
UI_API void ui_widget_set_draggable(UiWidget w, int draggable) {
    auto* p = W(w);
    if (p) p->draggable = draggable != 0;
}

UI_API void ui_widget_set_drag_data(UiWidget w, const wchar_t* data) {
    auto* p = W(w);
    if (!p) return;
    p->dragData = data ? data : L"";
    p->draggable = true;  // payload implies draggable (matches .uix attr)
}

UI_API void ui_widget_set_drop_target(UiWidget w, int accept) {
    auto* p = W(w);
    if (p) p->dropTarget = accept != 0;
}

UI_API void ui_widget_on_drop(UiWidget w, UiDropCallback cb, void* userdata) {
    auto* p = W(w);
    if (!p) return;
    if (!cb) { p->onDropHook = nullptr; return; }
    uint64_t handle = w;
    /* 捕 p 指针 — hook 存在 widget 上, 生命周期一致, 不悬空 (同 mouse_move). */
    p->onDropHook = [cb, userdata, handle, p](const std::wstring& data,
                                              const ui::MouseEvent& e) {
        cb(handle, data.c_str(),
           e.x - p->rect.left, e.y - p->rect.top, userdata);
    };
}

UI_API void ui_widget_on_drag_end(UiWidget w, UiDragEndCallback cb,
                                   void* userdata) {
    auto* p = W(w);
    if (!p) return;
    if (!cb) { p->onDragEndHook = nullptr; return; }
    uint64_t handle = w;
    p->onDragEndHook = [cb, userdata, handle](bool dropped) {
        cb(handle, dropped ? 1 : 0, userdata);
    };
}

/* Build 64+ (L13): 通用 widget focus / blur hook. 内部接 Widget::onFocusHook /
 * onBlurHook (这两个 hook lib 内已存在, JS 端通过 page_state.cpp 用着, 现在
 * 暴露 C API). 触发点: UiWindowImpl::SetFocus 切换 focusedWidget_ 时, 旧 widget
 * 触发 blur, 新 widget 触发 focus. cb=NULL 解绑. */
UI_API void ui_widget_on_focus(UiWidget w,
                                UiWidgetFocusCallback cb,
                                void* userdata) {
    auto* p = W(w);
    if (!p) return;
    if (!cb) { p->onFocusHook = nullptr; return; }
    uint64_t handle = w;
    p->onFocusHook = [cb, userdata, handle]() {
        cb(handle, userdata);
    };
}

UI_API void ui_widget_on_blur(UiWidget w,
                               UiWidgetFocusCallback cb,
                               void* userdata) {
    auto* p = W(w);
    if (!p) return;
    if (!cb) { p->onBlurHook = nullptr; return; }
    uint64_t handle = w;
    p->onBlurHook = [cb, userdata, handle]() {
        cb(handle, userdata);
    };
}

/* build 287: 通用 widget submit hook. 接 Widget::onSubmitHook —— TextInput /
 * TextArea 在 OnKeyDown 收到 VK_RETURN 时 fire。见 ui_core.h 里的说明: 这是
 * v-for 迭代体内做行内编辑时唯一能拿到 Enter 的路子。cb=NULL 解绑. */
UI_API void ui_widget_on_submit(UiWidget w,
                                 UiWidgetFocusCallback cb,
                                 void* userdata) {
    auto* p = W(w);
    if (!p) return;
    if (!cb) { p->onSubmitHook = nullptr; return; }
    uint64_t handle = w;
    p->onSubmitHook = [cb, userdata, handle]() {
        cb(handle, userdata);
    };
}

/* Build 66+ (L16): 通用 widget 滚轮回调. 接 Widget::onMouseWheelHook
 * (跟 .uix @wheel 同路径). UiWindowImpl::OnMouseWheel 开头会无条件 fire
 * 这个 hook, 跟 widget 子类的 OnMouseWheel dispatch loop 无关 — 所以
 * <custom> 等不在 dispatch list 里的 widget 也能收到. cb=NULL 解绑. */
UI_API void ui_widget_on_mouse_wheel(UiWidget w,
                                       UiWidgetWheelCallback cb,
                                       void* userdata) {
    auto* p = W(w);
    if (!p) return;
    if (!cb) { p->onMouseWheelHook = nullptr; return; }
    uint64_t handle = w;
    /* widget 内坐标: e.x / e.y 是 widget 坐标系 (跟 onMouseMoveHook 同). */
    p->onMouseWheelHook = [cb, userdata, handle, p](const ui::MouseEvent& e) {
        cb(handle, e.x - p->rect.left, e.y - p->rect.top, e.delta, userdata);
    };
}

/* build 287: widget 级 contextmenu hook. 见 ui_core.h 里的说明 —— 这是列表
 * 逐行右键唯一可靠的挂法, selector trigger 撞 v-if 时序会失效。
 * x/y 传窗口 DIP 坐标 (MouseEvent 里就是), 直接能喂 ui_menu_show. cb=NULL 解绑. */
UI_API void ui_widget_on_context_menu(UiWidget w,
                                       UiWidgetContextMenuCallback cb,
                                       void* userdata) {
    auto* p = W(w);
    if (!p) return;
    if (!cb) { p->onContextMenuHook = nullptr; return; }
    uint64_t handle = w;
    p->onContextMenuHook = [cb, userdata, handle](const ui::MouseEvent& e) {
        cb(handle, e.x, e.y, userdata);
    };
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

UI_API void ui_text_area_set_selection(UiWidget w, int start, int end) {
    auto* ta = As<ui::TextAreaWidget>(w);
    if (ta) ta->SetSelectionRange(start, end);
}

UI_API int ui_text_area_get_selection(UiWidget w, int* start, int* end) {
    auto* ta = As<ui::TextAreaWidget>(w);
    return (ta && ta->SelectionRange(start, end)) ? 1 : 0;
}

UI_API void ui_text_area_on_selection_changed(UiWidget w,
                                               UiTextAreaSelectionCallback cb,
                                               void* userdata) {
    auto* ta = As<ui::TextAreaWidget>(w);
    if (!ta) return;
    UiWidget handle = w;
    ta->onSelectionChanged = [cb, userdata, handle] {
        if (cb) cb(handle, userdata);
    };
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
    if (!cb) {
        p->onClick = nullptr;
        if (!p->id.empty()) Ctx().SetClickCallback(p->id, nullptr);
        return;
    }
    uint64_t handle = w;
    auto fn = [cb, userdata, handle]() { cb(handle, userdata); };
    p->onClick = fn;
    // Persist by HTML id so v-if / v-for remount can rebind to a fresh
    // widget instance with the same id. Widgets without an id fall through
    // to the legacy "lives only on this instance" behaviour.
    if (!p->id.empty()) Ctx().SetClickCallback(p->id, std::move(fn));
}

UI_API void ui_checkbox_on_changed(UiWidget w, UiValueCallback cb, void* userdata) {
    auto* p = W(w);
    if (!p) return;
    if (!cb) {
        p->onValueChanged = nullptr;
        if (!p->id.empty()) Ctx().SetValueCallback(p->id, nullptr);
        return;
    }
    uint64_t handle = w;
    auto fn = [cb, userdata, handle](bool val) { cb(handle, val ? 1 : 0, userdata); };
    p->onValueChanged = fn;
    if (!p->id.empty()) Ctx().SetValueCallback(p->id, std::move(fn));
}

UI_API void ui_slider_on_changed(UiWidget w, UiFloatCallback cb, void* userdata) {
    auto* p = W(w);
    if (!p) return;
    if (!cb) {
        p->onFloatChanged = nullptr;
        if (!p->id.empty()) Ctx().SetFloatCallback(p->id, nullptr);
        return;
    }
    uint64_t handle = w;
    auto fn = [cb, userdata, handle](float val) { cb(handle, val, userdata); };
    p->onFloatChanged = fn;
    if (!p->id.empty()) Ctx().SetFloatCallback(p->id, std::move(fn));
}

UI_API void ui_toggle_on_changed(UiWidget w, UiValueCallback cb, void* userdata) {
    auto* p = W(w);
    if (!p) return;
    if (!cb) {
        p->onValueChanged = nullptr;
        if (!p->id.empty()) Ctx().SetValueCallback(p->id, nullptr);
        return;
    }
    uint64_t handle = w;
    auto fn = [cb, userdata, handle](bool val) { cb(handle, val ? 1 : 0, userdata); };
    p->onValueChanged = fn;
    if (!p->id.empty()) Ctx().SetValueCallback(p->id, std::move(fn));
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
    // Push the new palette through every loaded page's --bg/--fg/--accent
    // table and re-cascade. Without this, .uix authors writing
    // `background: var(--bg)` would only see the value frozen at compile time.
    ui::page::RefreshAllPageThemes();
    Ctx().InvalidateAllWindows();
}

UI_API void ui_theme_set_accent(UiColor color) {
    D2D1_COLOR_F c = { color.r, color.g, color.b, color.a };
    theme::SetAccent(c);
    ui::page::RefreshAllPageThemes();
    Ctx().InvalidateAllWindows();
}

UI_API int ui_theme_set_accent_hex(const char* color) {
    /* NULL / "" / "none" → 取消覆盖, 等同 alpha=0 */
    if (!color || !*color || std::string(color) == "none") {
        D2D1_COLOR_F zero = {0, 0, 0, 0};
        theme::SetAccent(zero);
        ui::page::RefreshAllPageThemes();
        Ctx().InvalidateAllWindows();
        return 0;
    }
    ui::css::Color cc;
    if (!ui::css::ParseColor(std::string(color), cc)) return -1;
    D2D1_COLOR_F c = { cc.r, cc.g, cc.b, cc.a };
    theme::SetAccent(c);
    ui::page::RefreshAllPageThemes();
    Ctx().InvalidateAllWindows();
    return 0;
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

/* Build 64+ (L13): set_focused 同时把 widget 推进 owner window 的 focusedWidget_
 * 槽, 让键盘事件 (WM_KEYDOWN) 能路由到这里; 同时触发 onFocusHook / onBlurHook.
 * 旧调用方 (只把 paint bool 切来切去, 不需要键盘) 行为完全兼容 — 仍然写 paint
 * bool, 多做的 SetFocus / ClearFocus 对没注册 keyDown 回调的 widget 无副作用. */
UI_API void ui_custom_set_focused(UiWidget w, int focused) {
    auto* cw = As<ui::CustomWidget>(w);
    if (!cw) return;
    cw->focused = (focused != 0);
    auto* wi = Ctx().FindWindowByWidget(cw);
    if (!wi) return;
    if (focused) {
        wi->SetFocus(cw);
    } else if (wi->FocusedWidget() == cw) {
        wi->ClearFocus();
    }
    wi->Invalidate();
}

UI_API int ui_custom_get_focused(UiWidget w) {
    auto* cw = As<ui::CustomWidget>(w);
    return cw ? (int)cw->focused : 0;
}

/* Build 64+ (L13): 让 <custom> 进入 lib 的键盘焦点系统 (focusable=true 时, 鼠标
 * 点击会 SetFocus(this), Tab 也能走到). 默认 false 保留"纯展示型" custom widget
 * 不吃键盘的行为. 调用方接管键盘交互时 opt-in. */
UI_API void ui_custom_set_focusable(UiWidget w, int focusable) {
    auto* cw = As<ui::CustomWidget>(w);
    if (cw) cw->focusable = (focusable != 0);
}

// ================================================================
// Drawing API (used inside UiCustomDrawCallback)
// ================================================================

static ui::DrawApiContext* D(UiDrawCtx ctx) {
    auto* d = static_cast<ui::DrawApiContext*>(ctx);
    return (d && d->magic == ui::DrawApiContext::kMagic) ? d : nullptr;
}

static ui::Renderer* R(UiDrawCtx ctx) {
    auto* d = D(ctx);
    return d ? d->renderer : nullptr;
}

static ui::DisplayListRecorder* Rec(UiDrawCtx ctx) {
    auto* d = D(ctx);
    return d ? d->recorder : nullptr;
}

static uint64_t NextDrawBitmapGeneration() {
    static std::atomic<uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

UI_API void ui_draw_fill_rect(UiDrawCtx ctx, UiRect rect, UiColor color) {
    const auto d2dRect = ToD2DRect(rect);
    const auto d2dColor = ToD2D(color);
    if (auto* rec = Rec(ctx)) rec->FillRect(d2dRect, d2dColor);
    if (auto* r = R(ctx)) r->FillRect(d2dRect, d2dColor);
}

UI_API void ui_draw_rect(UiDrawCtx ctx, UiRect rect, UiColor color, float width) {
    const auto d2dRect = ToD2DRect(rect);
    const auto d2dColor = ToD2D(color);
    if (auto* rec = Rec(ctx)) rec->DrawRect(d2dRect, d2dColor, width);
    if (auto* r = R(ctx)) r->DrawRect(d2dRect, d2dColor, width);
}

UI_API void ui_draw_fill_rounded_rect(UiDrawCtx ctx, UiRect rect, float rx, float ry, UiColor color) {
    const auto d2dRect = ToD2DRect(rect);
    const auto d2dColor = ToD2D(color);
    if (auto* rec = Rec(ctx)) rec->FillRoundedRect(d2dRect, rx, ry, d2dColor);
    if (auto* r = R(ctx)) r->FillRoundedRect(d2dRect, rx, ry, d2dColor);
}

UI_API void ui_draw_rounded_rect(UiDrawCtx ctx, UiRect rect, float rx, float ry, UiColor color, float width) {
    const auto d2dRect = ToD2DRect(rect);
    const auto d2dColor = ToD2D(color);
    if (auto* rec = Rec(ctx)) rec->DrawRoundedRect(d2dRect, rx, ry, d2dColor, width);
    if (auto* r = R(ctx)) r->DrawRoundedRect(d2dRect, rx, ry, d2dColor, width);
}

UI_API void ui_draw_line(UiDrawCtx ctx, float x1, float y1, float x2, float y2, UiColor color, float width) {
    const auto d2dColor = ToD2D(color);
    if (auto* rec = Rec(ctx)) {
        rec->DrawLine({x1, y1}, {x2, y2}, d2dColor, width);
    }
    if (auto* r = R(ctx)) r->DrawLine(x1, y1, x2, y2, d2dColor, width);
}

UI_API void ui_draw_text(UiDrawCtx ctx, const wchar_t* text, UiRect rect, UiColor color, float fontSize) {
    const wchar_t* safeText = text ? text : L"";
    const auto d2dRect = ToD2DRect(rect);
    const auto d2dColor = ToD2D(color);
    if (auto* rec = Rec(ctx)) {
        ui::TextStyle style;
        style.color = d2dColor;
        style.font_size = fontSize;
        style.paragraph_alignment = static_cast<int>(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        rec->DrawText(safeText, d2dRect, style);
    }
    if (auto* r = R(ctx)) r->DrawText(safeText, d2dRect, d2dColor, fontSize);
}

UI_API void ui_draw_text_ex(UiDrawCtx ctx, const wchar_t* text, UiRect rect, UiColor color,
                             float fontSize, int align, int bold) {
    const wchar_t* safeText = text ? text : L"";
    const auto d2dRect = ToD2DRect(rect);
    const auto d2dColor = ToD2D(color);
    const auto weight = bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
    if (auto* rec = Rec(ctx)) {
        ui::TextStyle style;
        style.color = d2dColor;
        style.font_size = fontSize;
        style.alignment = align;
        style.paragraph_alignment = static_cast<int>(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        style.weight = static_cast<int>(weight);
        rec->DrawText(safeText, d2dRect, style);
    }
    if (auto* r = R(ctx)) {
        r->DrawText(safeText, d2dRect, d2dColor, fontSize,
                    (DWRITE_TEXT_ALIGNMENT)align, weight);
    }
}

UI_API float ui_draw_measure_text(UiDrawCtx ctx, const wchar_t* text, float fontSize) {
    if (!text) return 0;
    if (ui::g_activeMeasureContext) {
        return ui::g_activeMeasureContext->MeasureTextWidth(text, fontSize);
    }
    auto* r = R(ctx);
    if (!r) return 0;
    return r->MeasureTextWidth(text, fontSize);
}

UI_API void ui_draw_bitmap(UiDrawCtx ctx, const uint8_t* pixels,
                             int width, int height, int stride, UiRect dest) {
    auto* d = D(ctx);
    auto* r = d ? d->renderer : nullptr;
    auto* rec = d ? d->recorder : nullptr;
    if ((!r && !rec) || !pixels || width <= 0 || height <= 0) return;

    ui::ResourceKey resourceKey = ui::GlobalResourceStore().AddImage(
        ui::ResourceKind::Bitmap, NextDrawBitmapGeneration(), width, height, stride,
        ui::PixelFormat::BgraPremul, pixels, true);
    auto res = ui::GlobalResourceStore().Acquire(resourceKey);
    if (!res || !res->bytes) return;

    bool ownedByDisplayList = false;
    const auto d2dDest = ToD2DRect(dest);
    if (rec) {
        rec->DrawImage({resourceKey, res->width, res->height, res->stride, res->format},
                       d2dDest, ui::ImageSampling::Linear, 1.0f);
        rec->OwnResource(resourceKey);
        ownedByDisplayList = true;
    }

    if (r) {
        auto bitmap = r->CreateBitmapFromPixels(res->bytes->data(), res->width, res->height, res->stride);
        if (bitmap) {
            if (!ownedByDisplayList) d->transientResources.push_back(resourceKey);
            r->DrawBitmap(bitmap.Get(), d2dDest, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            return;
        }
    }

    if (!ownedByDisplayList) {
        ui::GlobalResourceStore().Remove(resourceKey);
    }
}

UI_API void ui_draw_push_clip(UiDrawCtx ctx, UiRect rect) {
    const auto d2dRect = ToD2DRect(rect);
    if (auto* rec = Rec(ctx)) rec->PushClip(d2dRect);
    if (auto* r = R(ctx)) r->PushClip(d2dRect);
}

UI_API void ui_draw_pop_clip(UiDrawCtx ctx) {
    if (auto* rec = Rec(ctx)) rec->PopClip();
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

// 通用: 一次拿控件基础属性 (id/type/text), 精简 json。free 用 ui_debug_free。
UI_API char* ui_widget_get_basic(UiWidget widget) {
    auto* w = W(widget);
    if (!w) return dupStr("null");
    return dupStr(ui::WidgetBasicJson(w));
}

// 派生自 basic: 直接取控件文本写进 buf (utf-16, 含结尾 null)。返回文本长度
// (不含 null); 若返回值 > cap-1 说明被截断, 调用方可据此扩容重取。无文本语义
// 的控件返回 0 + 空串。
UI_API int ui_widget_get_text(UiWidget widget, wchar_t* buf, int cap) {
    auto* w = W(widget);
    if (!w) { if (buf && cap > 0) buf[0] = L'\0'; return 0; }
    bool has = false;
    std::wstring t = ui::WidgetTextValue(w, &has);
    int need = (int)t.size();
    if (buf && cap > 0) {
        int n = (need < cap - 1) ? need : (cap - 1);
        for (int i = 0; i < n; ++i) buf[i] = t[(size_t)i];
        buf[n] = L'\0';
    }
    return need;
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

UI_API int ui_debug_screenshot_menu(UiWindow win, const wchar_t* outPath) {
    auto* w = Win(win);
    if (!w || !outPath) return -1;
    auto menu = w->ActiveMenu();
    if (!menu || !menu->IsVisible()) return -1;
    return menu->Screenshot(outPath);
}

UI_API int ui_debug_screenshot_widget(UiWindow win, UiWidget widget,
                                      const wchar_t* outPath) {
    auto* w = Win(win);
    auto* wid = W(widget);
    if (!w || !wid) return -1;
    const auto& r = wid->rect;
    if (r.right <= r.left || r.bottom <= r.top) return -2;  // not laid out yet
    return w->ScreenshotRegion(r, outPath);
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

UI_API int ui_debug_double_click_at(UiWindow win, float x, float y) {
    auto* wi = Win(win); if (!wi) return -1;
    wi->SimMouseDoubleClick(x, y);
    wi->Invalidate();
    return 0;
}

UI_API int ui_debug_double_click(UiWindow win, UiWidget w) {
    auto* wi = Win(win); if (!wi) return -1;
    auto* p = W(w); if (!p) return -1;
    float cx, cy;
    if (!WidgetCenter(p, cx, cy)) return -1;
    return ui_debug_double_click_at(win, cx, cy);
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

UI_API int ui_debug_key_mod(UiWindow win, int vk, int ctrl, int shift, int alt) {
    auto* wi = Win(win); if (!wi) return -1;
    /* Widget 的 OnKeyDown 普遍用 GetKeyState(VK_CONTROL) 之类读修饰键
     * (LabelWidget / TextInputWidget / OcrImgViewWidget 都是)。GetKeyState
     * 读的是【调用线程】的键盘状态表, SetKeyboardState 能改它 —— debug 命令
     * 本来就在 UI 线程执行, 所以注入期间临时把修饰键置下、结束还原, 模拟键
     * 就能走到和真实按键完全一样的分支。不这么做的话 Ctrl+C / Ctrl+A 之类
     * 组合键永远测不了 (vk 到了但 GetKeyState 说 Ctrl 没按下)。 */
    BYTE saved[256];
    const bool haveState = GetKeyboardState(saved) != FALSE;
    if (haveState) {
        BYTE st[256];
        memcpy(st, saved, sizeof(st));
        const BYTE down = 0x80, up = 0x00;
        st[VK_CONTROL] = st[VK_LCONTROL] = ctrl  ? down : up;
        st[VK_SHIFT]   = st[VK_LSHIFT]   = shift ? down : up;
        st[VK_MENU]    = st[VK_LMENU]    = alt   ? down : up;
        SetKeyboardState(st);
    }
    wi->SimKeyDown(vk);
    if (haveState) SetKeyboardState(saved);
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

UI_API void ui_asset_invalidate(const char* name) {
    if (!name || !*name) return;
    ui::ImageWidget::RetryFailedLoads(name);
}

UI_API int ui_debug_scroll_set(UiWidget w, float y) {
    auto* sv = As<ui::ScrollViewWidget>(w); if (!sv) return -1;
    /* build 285: SetScrollY 自己走纯偏移路径把内容归位了, 不必再补一次
     * DoLayout()。留着的话调试通道量到的是"滚动 + 全量重布局", 跟真实鼠标
     * 滚动不是同一条路 —— 性能对比会失真, 而且掩盖住本次优化的效果。 */
    sv->SetScrollY(y);
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

UI_API int ui_debug_menu_open_submenu_path(UiWindow win, const int* path, int depth) {
    auto* wi = Win(win); if (!wi) return -1;
    if (depth <= 0 || !path) return -2;
    auto m = wi->ActiveMenu(); if (!m) return -3;
    ui::ContextMenu* cur = m.get();
    for (int i = 0; i < depth; ++i) {
        cur = cur->OpenSubmenuAt(path[i]);
        if (!cur) return -4;
    }
    return 0;
}

UI_API int ui_debug_screenshot_submenu_path(UiWindow win, const int* path, int depth,
                                              const wchar_t* outPath) {
    auto* wi = Win(win); if (!wi || !outPath) return -1;
    if (depth <= 0 || !path) return -2;
    auto m = wi->ActiveMenu(); if (!m) return -3;
    ui::ContextMenu* cur = m.get();
    for (int i = 0; i < depth; ++i) {
        cur = cur->OpenSubmenuAt(path[i]);
        if (!cur) return -4;
    }
    return cur->Screenshot(outPath);
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

UI_API void ui_window_set_frameless(UiWindow win, int frameless) {
    auto* wi = Win(win); if (!wi) return;
    wi->SetFrameless(frameless != 0);
}

UI_API int ui_window_is_frameless(UiWindow win) {
    auto* wi = Win(win); if (!wi) return 0;
    return wi->IsFrameless() ? 1 : 0;
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

UI_API int ui_window_dpi(UiWindow win) {
    auto* wi = Win(win);
    return wi ? wi->Dpi() : 96;
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

UI_API void ui_window_set_aspect_lock(UiWindow win, int ratio_w, int ratio_h) {
    auto* wi = Win(win); if (!wi) return;
    wi->SetAspectLock(ratio_w, ratio_h);
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
