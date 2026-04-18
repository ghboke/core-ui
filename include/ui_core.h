/*
 * ui_core.h — Public C API for the Core UI framework
 *
 * Pure C header. No C++ types exposed.
 * Link against core-ui.dll (import library: core-ui.lib).
 */
#ifndef UI_CORE_H
#define UI_CORE_H

#include <stdint.h>
#include <wchar.h>

/* ------------------------------------------------------------------ */
/* Export / import                                                     */
/* ------------------------------------------------------------------ */
#if defined(UI_CORE_STATIC)
  #define UI_API
#elif defined(UI_CORE_BUILDING)
  #define UI_API __declspec(dllexport)
#else
  #define UI_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Version                                                            */
/* ------------------------------------------------------------------ */
/* 编译期版本宏（由 CMake 注入，若未注入则使用默认值）。                    */
#ifndef UI_CORE_VERSION_MAJOR
#define UI_CORE_VERSION_MAJOR 1
#endif
#ifndef UI_CORE_VERSION_MINOR
#define UI_CORE_VERSION_MINOR 0
#endif
#ifndef UI_CORE_VERSION_PATCH
#define UI_CORE_VERSION_PATCH 0
#endif
#ifndef UI_CORE_VERSION_BUILD
#define UI_CORE_VERSION_BUILD 1
#endif

#define UI_CORE_VERSION_STRINGIFY_(x) #x
#define UI_CORE_VERSION_STRINGIFY(x) UI_CORE_VERSION_STRINGIFY_(x)
#define UI_CORE_VERSION_STRING \
    UI_CORE_VERSION_STRINGIFY(UI_CORE_VERSION_MAJOR) "." \
    UI_CORE_VERSION_STRINGIFY(UI_CORE_VERSION_MINOR) "." \
    UI_CORE_VERSION_STRINGIFY(UI_CORE_VERSION_PATCH) "." \
    UI_CORE_VERSION_STRINGIFY(UI_CORE_VERSION_BUILD)

/* 运行时 API：无需 ui_init，任意时刻可调。                                  */
UI_API void        ui_core_version(int* major, int* minor, int* patch); /* 任一指针可为 NULL */
UI_API int         ui_core_version_build(void);                         /* 构建编号 */
UI_API const char* ui_core_version_string(void);                        /* "1.0.0.1" */

/* ------------------------------------------------------------------ */
/* Opaque handles                                                     */
/* ------------------------------------------------------------------ */
typedef uint64_t UiWidget;
typedef uint64_t UiWindow;

#define UI_INVALID 0

/* ------------------------------------------------------------------ */
/* Basic types                                                        */
/* ------------------------------------------------------------------ */
typedef struct UiColor {
    float r, g, b, a;
} UiColor;

typedef struct UiRect {
    float left, top, right, bottom;
} UiRect;

/* ------------------------------------------------------------------ */
/* Window configuration                                               */
/* ------------------------------------------------------------------ */
typedef struct UiWindowConfig {
    const wchar_t* title;
    int width;
    int height;
    int system_frame;   /* 0 (default) = borderless custom chrome, 1 = system title bar */
    int resizable;      /* 1 = WS_THICKFRAME */
    int accept_files;   /* 1 = WS_EX_ACCEPTFILES */
    int x;              /* 窗口初始 x 坐标，0 = 屏幕居中 */
    int y;              /* 窗口初始 y 坐标，0 = 屏幕居中 */
    int tool_window;    /* 1 = 工具窗口（不在任务栏显示图标） */
    int skip_animation; /* 1 = 跳过开场动画（文件关联打开时用，加速首次显示） */
    const void* icon_pixels; /* RGBA 像素数据（32bpp），NULL = 默认图标 */
    int icon_width;          /* 图标宽度 */
    int icon_height;         /* 图标高度 */
} UiWindowConfig;

/* ------------------------------------------------------------------ */
/* Theme                                                              */
/* ------------------------------------------------------------------ */
typedef enum UiThemeMode {
    UI_THEME_DARK  = 0,
    UI_THEME_LIGHT = 1
} UiThemeMode;

/* ------------------------------------------------------------------ */
/* Initialization / shutdown / message loop                           */
/* ------------------------------------------------------------------ */
UI_API int  ui_init(void);                        /* returns 0, default dark theme */
UI_API int  ui_init_with_theme(UiThemeMode mode); /* init with specified theme */
UI_API void ui_shutdown(void);
UI_API int  ui_run(void);              /* message loop, returns exit code */
UI_API void ui_quit(int exit_code);    /* posts WM_QUIT */

/* ------------------------------------------------------------------ */
/* Window management                                                  */
/* ------------------------------------------------------------------ */
UI_API UiWindow ui_window_create(const UiWindowConfig* config);
UI_API void     ui_window_destroy(UiWindow win);
UI_API void     ui_window_show(UiWindow win);
UI_API void     ui_window_show_immediate(UiWindow win);  /* 跳过开场动画 */
UI_API void     ui_window_prepare_rt(UiWindow win);     /* 预创建渲染目标（不显示窗口） */
UI_API void     ui_window_hide(UiWindow win);
UI_API void     ui_window_set_root(UiWindow win, UiWidget root);
UI_API void     ui_window_set_title(UiWindow win, const wchar_t* title);
UI_API void     ui_window_invalidate(UiWindow win);
/* 强制重新布局（set_visible 等改变影响布局的属性后调用） */
UI_API void     ui_window_relayout(UiWindow win);
UI_API void*    ui_window_hwnd(UiWindow win);   /* returns HWND */

/* ------------------------------------------------------------------ */
/* Window callbacks                                                   */
/* ------------------------------------------------------------------ */
typedef void (*UiWindowCloseCallback)(UiWindow win, void* userdata);
typedef void (*UiWindowResizeCallback)(UiWindow win, int w, int h, void* userdata);
typedef void (*UiWindowDropCallback)(UiWindow win, const wchar_t* path, void* userdata);
typedef void (*UiWindowKeyCallback)(UiWindow win, int vk_code, void* userdata);

/* ------------------------------------------------------------------ */
/* Context Menu                                                       */
/* ------------------------------------------------------------------ */
typedef uint64_t UiMenu;

UI_API UiMenu   ui_menu_create(void);
UI_API void     ui_menu_destroy(UiMenu menu);
UI_API void     ui_menu_add_item(UiMenu menu, int id, const wchar_t* text);
UI_API void     ui_menu_add_item_ex(UiMenu menu, int id, const wchar_t* text,
                                     const wchar_t* shortcut, const char* svg);
UI_API void     ui_menu_add_separator(UiMenu menu);
UI_API void     ui_menu_add_submenu(UiMenu menu, const wchar_t* text, UiMenu submenu);
UI_API void     ui_menu_set_enabled(UiMenu menu, int id, int enabled);
UI_API void     ui_menu_set_bg_color(UiMenu menu, UiColor color);
UI_API void     ui_menu_show(UiWindow win, UiMenu menu, float x, float y);
UI_API void     ui_menu_close(UiWindow win);

/* Toast notification (bottom-center, auto-fade) */
UI_API void     ui_toast(UiWindow win, const wchar_t* text, int duration_ms);
UI_API void     ui_toast_at(UiWindow win, const wchar_t* text, int duration_ms, int position); /* 0=top 1=center 2=bottom */
UI_API void     ui_toast_ex(UiWindow win, const wchar_t* text, int duration_ms, int position, int icon); /* icon: 0=none 1=success 2=error 3=warning */

/* ------------------------------------------------------------------ */
/* Dialog (modal confirm / alert)                                     */
/* ------------------------------------------------------------------ */
typedef void (*UiDialogCallback)(UiWidget dialog, int confirmed, void* userdata);

UI_API void ui_dialog_show(UiWidget dialog, UiWindow win,
                           const wchar_t* title, const wchar_t* message,
                           UiDialogCallback cb, void* userdata);
UI_API void ui_dialog_hide(UiWidget dialog, UiWindow win);
UI_API void ui_dialog_set_ok_text(UiWidget dialog, const wchar_t* text);
UI_API void ui_dialog_set_cancel_text(UiWidget dialog, const wchar_t* text);
UI_API void ui_dialog_set_show_cancel(UiWidget dialog, int show);

typedef void (*UiMenuCallback)(UiWindow win, int item_id, void* userdata);
UI_API void ui_window_on_menu(UiWindow win, UiMenuCallback cb, void* userdata);

typedef void (*UiRightClickCallback)(UiWindow win, float x, float y, void* userdata);
UI_API void ui_window_on_right_click(UiWindow win, UiRightClickCallback cb, void* userdata);

/* ------------------------------------------------------------------ */
/* Window callbacks                                                   */
/* ------------------------------------------------------------------ */
UI_API void ui_window_on_close(UiWindow win, UiWindowCloseCallback cb, void* userdata);
UI_API void ui_window_on_resize(UiWindow win, UiWindowResizeCallback cb, void* userdata);
UI_API void ui_window_on_drop(UiWindow win, UiWindowDropCallback cb, void* userdata);
UI_API void ui_window_on_key(UiWindow win, UiWindowKeyCallback cb, void* userdata);

/* ------------------------------------------------------------------ */
/* Layout containers                                                  */
/* ------------------------------------------------------------------ */
UI_API UiWidget ui_vbox(void);
UI_API UiWidget ui_hbox(void);
UI_API UiWidget ui_spacer(float size);     /* 0 = expanding */
UI_API UiWidget ui_panel(UiColor bg);
UI_API UiWidget ui_panel_themed(int theme_color_id);  /* 0=sidebar_bg, 1=toolbar_bg, 2=content_bg */

/* ------------------------------------------------------------------ */
/* Controls                                                           */
/* ------------------------------------------------------------------ */
UI_API UiWidget ui_label(const wchar_t* text);
UI_API UiWidget ui_button(const wchar_t* text);
UI_API UiWidget ui_checkbox(const wchar_t* text);
UI_API UiWidget ui_slider(float min_val, float max_val, float value);
UI_API UiWidget ui_separator(void);
UI_API UiWidget ui_vseparator(void);
UI_API UiWidget ui_text_input(const wchar_t* placeholder);
UI_API UiWidget ui_text_area(const wchar_t* placeholder);
UI_API UiWidget ui_combobox(const wchar_t** items, int count);
UI_API UiWidget ui_radio_button(const wchar_t* text, const char* group);
UI_API UiWidget ui_toggle(const wchar_t* text);
UI_API UiWidget ui_progress_bar(float min_val, float max_val, float value);
UI_API UiWidget ui_tab_control(void);
UI_API UiWidget ui_scroll_view(void);
UI_API UiWidget ui_dialog(void);

/* ------------------------------------------------------------------ */
/* ImageView (zoomable, pannable image canvas)                       */
/* ------------------------------------------------------------------ */
UI_API UiWidget ui_image_view(void);
UI_API void     ui_image_load_file(UiWidget w, UiWindow win, const wchar_t* path);
UI_API void     ui_image_set_pixels(UiWidget w, UiWindow win,
                                     const void* pixels, int width, int height, int stride);
UI_API void     ui_image_clear(UiWidget w);
UI_API float    ui_image_get_zoom(UiWidget w);
UI_API void     ui_image_set_zoom(UiWidget w, float zoom);
UI_API void     ui_image_fit(UiWidget w);
UI_API void     ui_image_reset(UiWidget w);
UI_API void     ui_image_get_pan(UiWidget w, float* out_x, float* out_y);
UI_API void     ui_image_set_pan(UiWidget w, float x, float y);
UI_API int      ui_image_width(UiWidget w);
UI_API int      ui_image_height(UiWidget w);
/* 读回当前 bitmap 像素到 CPU（BGRA premultiplied, stride=w*4）。
 * 成功返回 1，*out_pixels 需用 ui_image_free_pixels 释放；失败返回 0（tile 模式/无图/回读失败）。*/
UI_API int      ui_image_get_pixels(UiWidget w, UiWindow win, void** out_pixels, int* out_w, int* out_h);
UI_API void     ui_image_free_pixels(void* pixels);
UI_API void     ui_image_set_checkerboard(UiWidget w, int on);
UI_API void     ui_image_set_antialias(UiWidget w, int on);
UI_API int      ui_image_get_antialias(UiWidget w);
UI_API void     ui_image_set_zoom_range(UiWidget w, float min_zoom, float max_zoom);

typedef void (*UiViewportCallback)(UiWidget widget, float zoom, float panX, float panY, void* userdata);
UI_API void     ui_image_on_viewport_changed(UiWidget w, UiViewportCallback cb, void* userdata);

/* ImageView mouse down/move 钩子：返回非 0 吞掉事件。
 * mouse_move 钩子吞事件时 core-ui 会顺带结束 pan 状态，
 * 用于 pan 中途切换到"拖出"等流程。 */
typedef int (*UiImageMouseDownCallback)(UiWidget widget, float x, float y, int btn, void* userdata);
typedef int (*UiImageMouseMoveCallback)(UiWidget widget, float x, float y, void* userdata);
UI_API void     ui_image_on_mouse_down(UiWidget w, UiImageMouseDownCallback cb, void* userdata);
UI_API void     ui_image_on_mouse_move(UiWidget w, UiImageMouseMoveCallback cb, void* userdata);

/* Rotation (0, 90, 180, 270 degrees) */
UI_API void     ui_image_set_rotation(UiWidget w, int angle);
UI_API int      ui_image_get_rotation(UiWidget w);

/* Loading spinner */
UI_API void     ui_image_set_loading(UiWidget w, int loading);
UI_API int      ui_image_get_loading(UiWidget w);

/* Tiled rendering (for very large images) */
UI_API void     ui_image_set_tiled(UiWidget w, UiWindow win, int full_width, int full_height, int tile_size);
UI_API void     ui_image_set_tile(UiWidget w, UiWindow win, int tile_x, int tile_y,
                                   const void* pixels, int width, int height, int stride);
UI_API void     ui_image_set_tile_preview(UiWidget w, UiWindow win,
                                           const void* pixels, int width, int height, int stride);
UI_API void     ui_image_clear_tiles(UiWidget w);
UI_API int      ui_image_is_tiled(UiWidget w);

/* Raw image dimensions (before rotation) */
UI_API int      ui_image_raw_width(UiWidget w);
UI_API int      ui_image_raw_height(UiWidget w);

/* ------------------------------------------------------------------ */
/* IconButton (SVG icon button)                                      */
/* ------------------------------------------------------------------ */
UI_API UiWidget ui_icon_button(const char* svg, int ghost);
UI_API void     ui_icon_button_set_svg(UiWidget w, const char* svg);
UI_API void     ui_icon_button_set_ghost(UiWidget w, int ghost);
UI_API void     ui_icon_button_set_icon_color(UiWidget w, UiColor color);
UI_API void     ui_icon_button_set_icon_padding(UiWidget w, float padding);

/* ------------------------------------------------------------------ */
/* TitleBar (borderless window title bar)                             */
/* ------------------------------------------------------------------ */
UI_API UiWidget ui_titlebar(const wchar_t* title);
UI_API void     ui_titlebar_set_title(UiWidget titlebar, const wchar_t* title);
UI_API void     ui_titlebar_show_buttons(UiWidget titlebar, int showMin, int showMax, int showClose);
UI_API void     ui_titlebar_show_icon(UiWidget titlebar, int show);
UI_API void     ui_titlebar_set_bg_color(UiWidget titlebar, UiColor color);
UI_API void     ui_titlebar_add_widget(UiWidget titlebar, UiWidget custom_widget);

/* ------------------------------------------------------------------ */
/* Widget tree operations                                             */
/* ------------------------------------------------------------------ */
UI_API void     ui_widget_add_child(UiWidget parent, UiWidget child);
UI_API void     ui_widget_remove_child(UiWidget parent, UiWidget child);
UI_API void     ui_widget_destroy(UiWidget widget);
UI_API UiWidget ui_widget_find_by_id(UiWidget root, const char* id);

/* ------------------------------------------------------------------ */
/* Widget common properties                                           */
/* ------------------------------------------------------------------ */
UI_API void ui_widget_set_id(UiWidget w, const char* id);
UI_API void ui_widget_set_width(UiWidget w, float width);
UI_API void ui_widget_set_height(UiWidget w, float height);
UI_API void ui_widget_set_size(UiWidget w, float width, float height);
UI_API void ui_widget_set_expand(UiWidget w, int expand);
UI_API void ui_widget_set_padding(UiWidget w, float left, float top, float right, float bottom);
UI_API void ui_widget_set_padding_uniform(UiWidget w, float p);
UI_API void ui_widget_set_gap(UiWidget w, float gap);
UI_API void ui_widget_set_visible(UiWidget w, int visible);
UI_API void ui_widget_set_opacity(UiWidget w, float opacity);  /* 0.0 = 全透明 + 不响应 hit */
UI_API float ui_widget_get_opacity(UiWidget w);
UI_API void ui_widget_set_enabled(UiWidget w, int enabled);
UI_API void ui_widget_set_bg_color(UiWidget w, UiColor color);

UI_API int    ui_widget_get_visible(UiWidget w);
UI_API int    ui_widget_get_enabled(UiWidget w);
UI_API UiRect ui_widget_get_rect(UiWidget w);
UI_API void   ui_widget_set_rect(UiWidget w, UiRect rect);

/* ------------------------------------------------------------------ */
/* Label                                                              */
/* ------------------------------------------------------------------ */
UI_API void ui_label_set_text(UiWidget w, const wchar_t* text);
UI_API void ui_label_set_font_size(UiWidget w, float size);
UI_API void ui_label_set_bold(UiWidget w, int bold);
UI_API void ui_label_set_wrap(UiWidget w, int wrap);  /* 1 = 自动换行 */
UI_API void ui_label_set_max_lines(UiWidget w, int maxLines);  /* wrap 模式最大行数，0=不限 */
UI_API void ui_label_set_text_color(UiWidget w, UiColor color);
UI_API void ui_label_set_align(UiWidget w, int align);  /* 0=left, 1=right, 2=center */

/* ------------------------------------------------------------------ */
/* Button                                                             */
/* ------------------------------------------------------------------ */
UI_API void ui_button_set_font_size(UiWidget w, float size);
UI_API void ui_button_set_type(UiWidget w, int type);  /* 0=default, 1=primary(accent) */
UI_API void ui_button_set_text_color(UiWidget w, UiColor color);
UI_API void ui_button_set_bg_color(UiWidget w, UiColor color);  /* 自定义背景色，hover/press 自动加深 */

/* ------------------------------------------------------------------ */
/* CheckBox                                                           */
/* ------------------------------------------------------------------ */
UI_API int  ui_checkbox_get_checked(UiWidget w);
UI_API void ui_checkbox_set_checked(UiWidget w, int checked);

/* ------------------------------------------------------------------ */
/* Slider                                                             */
/* ------------------------------------------------------------------ */
UI_API float ui_slider_get_value(UiWidget w);
UI_API void  ui_slider_set_value(UiWidget w, float value);

/* ------------------------------------------------------------------ */
/* TextInput                                                          */
/* ------------------------------------------------------------------ */
/* Returned pointer is valid until the next call to this function on the same thread. */
UI_API const wchar_t* ui_text_input_get_text(UiWidget w);
UI_API void           ui_text_input_set_text(UiWidget w, const wchar_t* text);
UI_API void           ui_text_input_set_read_only(UiWidget w, int read_only);

/* ------------------------------------------------------------------ */
/* TextArea (multi-line text input)                                   */
/* ------------------------------------------------------------------ */
/* Returned pointer is valid until the next call to this function on the same thread. */
UI_API const wchar_t* ui_text_area_get_text(UiWidget w);
UI_API void           ui_text_area_set_text(UiWidget w, const wchar_t* text);
UI_API void           ui_text_area_set_read_only(UiWidget w, int read_only);

/* ------------------------------------------------------------------ */
/* ComboBox                                                           */
/* ------------------------------------------------------------------ */
UI_API int  ui_combobox_get_selected(UiWidget w);
UI_API void ui_combobox_set_selected(UiWidget w, int index);

/* ------------------------------------------------------------------ */
/* RadioButton                                                        */
/* ------------------------------------------------------------------ */
UI_API int ui_radio_get_selected(UiWidget w);
UI_API void ui_radio_set_selected(UiWidget w, int selected);

/* ------------------------------------------------------------------ */
/* Toggle                                                             */
/* ------------------------------------------------------------------ */
UI_API int  ui_toggle_get_on(UiWidget w);
UI_API void ui_toggle_set_on(UiWidget w, int on);
UI_API void ui_toggle_set_on_immediate(UiWidget w, int on);

/* ------------------------------------------------------------------ */
/* ProgressBar                                                        */
/* ------------------------------------------------------------------ */
UI_API float ui_progress_get_value(UiWidget w);
UI_API void  ui_progress_set_value(UiWidget w, float value);

/* ------------------------------------------------------------------ */
/* TabControl                                                         */
/* ------------------------------------------------------------------ */
UI_API void ui_tab_add(UiWidget tab_control, const wchar_t* title, UiWidget content);
UI_API int  ui_tab_get_active(UiWidget tab_control);
UI_API void ui_tab_set_active(UiWidget tab_control, int index);

/* ------------------------------------------------------------------ */
/* ScrollView                                                         */
/* ------------------------------------------------------------------ */
UI_API void ui_scroll_set_content(UiWidget scroll_view, UiWidget content);

/* ------------------------------------------------------------------ */
/* Widget callbacks                                                   */
/* ------------------------------------------------------------------ */
typedef void (*UiClickCallback)(UiWidget widget, void* userdata);
typedef void (*UiValueCallback)(UiWidget widget, int value, void* userdata);
typedef void (*UiFloatCallback)(UiWidget widget, float value, void* userdata);
typedef void (*UiSelectionCallback)(UiWidget widget, int index, void* userdata);

UI_API void ui_widget_set_tooltip(UiWidget w, const wchar_t* text);
UI_API void ui_widget_on_click(UiWidget w, UiClickCallback cb, void* userdata);
UI_API void ui_checkbox_on_changed(UiWidget w, UiValueCallback cb, void* userdata);
UI_API void ui_slider_on_changed(UiWidget w, UiFloatCallback cb, void* userdata);
UI_API void ui_toggle_on_changed(UiWidget w, UiValueCallback cb, void* userdata);
UI_API void ui_combobox_on_changed(UiWidget w, UiSelectionCallback cb, void* userdata);

UI_API void        ui_theme_set_mode(UiThemeMode mode);
UI_API UiThemeMode ui_theme_get_mode(void);

/* Get current theme colors (read-only, changes when theme switches) */
UI_API UiColor ui_theme_bg(void);             /* window background */
UI_API UiColor ui_theme_content_bg(void);     /* content area background */
UI_API UiColor ui_theme_sidebar_bg(void);     /* sidebar / panel bg */
UI_API UiColor ui_theme_toolbar_bg(void);     /* toolbar / panel bg */
UI_API UiColor ui_theme_accent(void);         /* accent color */
UI_API UiColor ui_theme_text(void);           /* button/normal text */
UI_API UiColor ui_theme_divider(void);        /* divider/border */

/* ------------------------------------------------------------------ */
/* CustomWidget (user-defined widget via C callbacks)                  */
/* ------------------------------------------------------------------ */
typedef void* UiDrawCtx;

typedef void (*UiCustomDrawCallback)(UiWidget w, UiDrawCtx ctx, UiRect rect, void* ud);
typedef int  (*UiCustomMouseCallback)(UiWidget w, float x, float y, int btn, void* ud);
typedef int  (*UiCustomWheelCallback)(UiWidget w, float x, float y, float delta, void* ud);
typedef int  (*UiCustomKeyCallback)(UiWidget w, int vk, void* ud);
typedef int  (*UiCustomCharCallback)(UiWidget w, int ch, void* ud);
typedef void (*UiCustomLayoutCallback)(UiWidget w, UiRect rect, void* ud);

UI_API UiWidget ui_custom(void);
UI_API void ui_custom_on_draw(UiWidget w, UiCustomDrawCallback cb, void* ud);
UI_API void ui_custom_on_mouse_down(UiWidget w, UiCustomMouseCallback cb, void* ud);
UI_API void ui_custom_on_mouse_move(UiWidget w, UiCustomMouseCallback cb, void* ud);
UI_API void ui_custom_on_mouse_up(UiWidget w, UiCustomMouseCallback cb, void* ud);
UI_API void ui_custom_on_mouse_wheel(UiWidget w, UiCustomWheelCallback cb, void* ud);
UI_API void ui_custom_on_key_down(UiWidget w, UiCustomKeyCallback cb, void* ud);
UI_API void ui_custom_on_char(UiWidget w, UiCustomCharCallback cb, void* ud);
UI_API void ui_custom_on_layout(UiWidget w, UiCustomLayoutCallback cb, void* ud);
UI_API void ui_custom_set_focused(UiWidget w, int focused);
UI_API int  ui_custom_get_focused(UiWidget w);

/* ------------------------------------------------------------------ */
/* Drawing API (use inside UiCustomDrawCallback)                      */
/* ------------------------------------------------------------------ */
UI_API void  ui_draw_fill_rect(UiDrawCtx ctx, UiRect rect, UiColor color);
UI_API void  ui_draw_rect(UiDrawCtx ctx, UiRect rect, UiColor color, float width);
UI_API void  ui_draw_fill_rounded_rect(UiDrawCtx ctx, UiRect rect, float rx, float ry, UiColor color);
UI_API void  ui_draw_rounded_rect(UiDrawCtx ctx, UiRect rect, float rx, float ry, UiColor color, float width);
UI_API void  ui_draw_line(UiDrawCtx ctx, float x1, float y1, float x2, float y2, UiColor color, float width);
UI_API void  ui_draw_text(UiDrawCtx ctx, const wchar_t* text, UiRect rect, UiColor color, float fontSize);
UI_API void  ui_draw_text_ex(UiDrawCtx ctx, const wchar_t* text, UiRect rect, UiColor color,
                              float fontSize, int align, int bold);
UI_API float ui_draw_measure_text(UiDrawCtx ctx, const wchar_t* text, float fontSize);
UI_API void  ui_draw_bitmap(UiDrawCtx ctx, const uint8_t* pixels,
                             int width, int height, int stride, UiRect dest);
UI_API void  ui_draw_push_clip(UiDrawCtx ctx, UiRect rect);
UI_API void  ui_draw_pop_clip(UiDrawCtx ctx);

/* ------------------------------------------------------------------ */
/* Debug / Inspector                                                  */
/* ------------------------------------------------------------------ */
/* Returns a JSON string describing the widget tree of the window.    */
/* Caller must free the returned pointer with ui_debug_free().        */
UI_API char*    ui_debug_dump_tree(UiWindow win);
UI_API char*    ui_debug_dump_widget(UiWidget w);
UI_API void     ui_debug_free(char* ptr);
UI_API void     ui_debug_highlight(UiWindow win, const char* widget_id);  /* 红框高亮指定控件，NULL 清除 */
UI_API int      ui_debug_screenshot(UiWindow win, const wchar_t* outPath);  /* 截图保存为 PNG，成功返回 0 */

/* ------------------------------------------------------------------ */
/* Debug / Simulation — 自动化测试用的事件注入 API                     */
/* ------------------------------------------------------------------ */
/*  两类通道：                                                        */
/*    1) ui_debug_*   —— 直接走 widget 事件路径（同步、触发回调）      */
/*    2) ui_debug_post_* —— 通过 Win32 PostMessage 走真实消息循环      */
/*       （需调用 ui_debug_pump 或等待 ui_run 处理）                   */
/*  所有函数返回 0 成功，非 0 失败；坐标参数为 DIP（逻辑像素）。        */

/* ---- Widget 查询 ---- */
UI_API int   ui_debug_widget_center(UiWidget w, float* outX, float* outY);
UI_API int   ui_debug_widget_is_visible(UiWidget w);

/* ---- 鼠标：走内部事件通路（触发命中测试、焦点、回调…） ---- */
UI_API int   ui_debug_click(UiWindow win, UiWidget w);
UI_API int   ui_debug_click_at(UiWindow win, float x, float y);
UI_API int   ui_debug_double_click(UiWindow win, UiWidget w);
UI_API int   ui_debug_right_click(UiWindow win, UiWidget w);
UI_API int   ui_debug_right_click_at(UiWindow win, float x, float y);
UI_API int   ui_debug_hover(UiWindow win, UiWidget w);
UI_API int   ui_debug_mouse_move(UiWindow win, float x, float y);
UI_API int   ui_debug_mouse_down(UiWindow win, float x, float y);
UI_API int   ui_debug_mouse_up(UiWindow win, float x, float y);
UI_API int   ui_debug_drag(UiWindow win, UiWidget w, float dx, float dy);
UI_API int   ui_debug_drag_to(UiWindow win, float x1, float y1, float x2, float y2);
UI_API int   ui_debug_wheel(UiWindow win, UiWidget w, float delta);
UI_API int   ui_debug_wheel_at(UiWindow win, float x, float y, float delta);

/* ---- 焦点 / 键盘 ---- */
UI_API int   ui_debug_focus(UiWindow win, UiWidget w);
UI_API int   ui_debug_blur(UiWindow win);
UI_API int   ui_debug_key(UiWindow win, int vk);          /* 发给焦点控件 */
UI_API int   ui_debug_type_char(UiWindow win, unsigned int ch);
UI_API int   ui_debug_type_text(UiWindow win, const wchar_t* text);

/* ---- 控件高层操作（直接改状态 + 触发 on_changed 回调） ---- */
UI_API int   ui_debug_checkbox_toggle(UiWindow win, UiWidget w);
UI_API int   ui_debug_checkbox_set(UiWindow win, UiWidget w, int checked);
UI_API int   ui_debug_toggle_set(UiWindow win, UiWidget w, int on);
UI_API int   ui_debug_radio_select(UiWindow win, UiWidget w);
UI_API int   ui_debug_combo_select(UiWindow win, UiWidget w, int index);
UI_API int   ui_debug_combo_open(UiWidget w);
UI_API int   ui_debug_combo_close(UiWidget w);
UI_API int   ui_debug_slider_set(UiWindow win, UiWidget w, float value);
UI_API int   ui_debug_number_set(UiWindow win, UiWidget w, float value);
UI_API int   ui_debug_tab_set(UiWidget w, int index);
UI_API int   ui_debug_expander_set(UiWidget w, int expanded);
UI_API int   ui_debug_splitview_set(UiWidget w, int open);
UI_API int   ui_debug_flyout_show(UiWidget flyout, UiWidget anchor);
UI_API int   ui_debug_flyout_hide(UiWidget flyout);
UI_API int   ui_debug_text_set(UiWidget w, const wchar_t* text);
UI_API int   ui_debug_scroll_set(UiWidget scrollview, float y);

/* ---- Context menu（对当前已打开的 active menu 操作） ---- */
UI_API int   ui_debug_menu_is_open(UiWindow win);
UI_API int   ui_debug_menu_item_count(UiWindow win);
UI_API int   ui_debug_menu_click_index(UiWindow win, int index);
UI_API int   ui_debug_menu_click_id(UiWindow win, int item_id);
UI_API int   ui_debug_menu_close(UiWindow win);

/* 子菜单：path 是整数索引数组，如 {2,1} 即"顶层第 2 项的 submenu 中第 1 项"。
   对只操作顶层的情形，等同 ui_debug_menu_click_index（depth=1）。 */
UI_API int   ui_debug_menu_item_count_at(UiWindow win, const int* path, int depth);
UI_API int   ui_debug_menu_item_id_at(UiWindow win, const int* path, int depth);
UI_API int   ui_debug_menu_has_submenu_at(UiWindow win, const int* path, int depth);
UI_API int   ui_debug_menu_click_path(UiWindow win, const int* path, int depth);

/* 开关 context menu 的"前台变化即自动关闭"行为。
   自动化脚本（如 PowerShell 发 pipe 命令）持有前台窗口时，需要调用
   ui_debug_set_menu_autoclose(0) 关掉自动关闭；否则菜单打开后 50ms 内就被关掉。
   参数 enabled=0 表示关闭自动关闭，=1 恢复正常。 */
UI_API void  ui_debug_set_menu_autoclose(int enabled);

/* 在 UI 线程上同步执行 fn(ud)。跨线程调用时内部用 SendMessage 到窗口，
   已在 UI 线程时直接调用。返回前 fn 必已执行完成。主要给自动化脚本 / 调试 pipe
   用，保证 widget 访问不跨线程产生数据竞争。 */
UI_API void  ui_window_invoke_sync(UiWindow win, void (*fn)(void* ud), void* ud);

/* ---- Dialog / Toast ---- */
UI_API int   ui_debug_dialog_confirm(UiWindow win);
UI_API int   ui_debug_dialog_cancel(UiWindow win);

/* ---- HWND 通道：通过 Win32 消息循环派发（异步，需 pump） ---- */
UI_API int   ui_debug_post_click(UiWindow win, float x, float y);
UI_API int   ui_debug_post_right_click(UiWindow win, float x, float y);
UI_API int   ui_debug_post_mouse_move(UiWindow win, float x, float y);
UI_API int   ui_debug_post_key(UiWindow win, int vk);
UI_API int   ui_debug_post_char(UiWindow win, unsigned int ch);

/* 处理所有已排队的窗口消息（使 Post* 生效）。返回已处理的消息数。 */
UI_API int   ui_debug_pump(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_CORE_H */
