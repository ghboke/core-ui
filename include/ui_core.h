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

typedef enum UiIconColorRole {
    UI_ICON_COLOR_BUTTON_TEXT = 0,
    UI_ICON_COLOR_TITLEBAR_TEXT = 1,
    UI_ICON_COLOR_CONTENT_TEXT = 2,
    UI_ICON_COLOR_ACCENT = 3,
    UI_ICON_COLOR_DANGER = 4,
    UI_ICON_COLOR_DIVIDER = 5
} UiIconColorRole;

typedef struct UiRect {
    float left, top, right, bottom;
} UiRect;

/* ------------------------------------------------------------------ */
/* Window configuration                                               */
/* ------------------------------------------------------------------ */
/* IMPORTANT — 单位语义 (build 92+ 文档化):
 *
 * 所有窗口几何字段 (width / height / x / y) + ui_window_set_size /
 * ui_window_set_min_size 的参数, 单位都是 **DIP** (device-independent pixel,
 * 1/96 inch), 跟 CSS / Win32 GDI / WPF 的 "logical pixel" 一致.
 *
 * 物理像素换算: phys = dip × monitor_dpi ÷ 96
 *   - 100% 缩放 (96 DPI):  width=400 → 400 物理 px
 *   - 125% 缩放 (120 DPI): width=400 → 500 物理 px
 *   - 150% 缩放 (144 DPI): width=400 → 600 物理 px
 *   - 200% 缩放 (192 DPI): width=400 → 800 物理 px
 *
 * 同款语义适用于 .uix `<window width="..." height="..." x="..." y="...">`
 * 属性. 想要 "在 1.5x 屏看上去 425 物理 px 宽" 的窗口, 应该填
 * 425 × 96/144 ≈ 283.
 *
 * 这跟 CSS 的 px 单位完全一致 (CSS 1px = 1 reference pixel = 1/96 inch),
 * 不是 raw screen pixel. */
typedef struct UiWindowConfig {
    const wchar_t* title;
    int width;          /* DIP — 见结构体顶部 "单位语义" 注释 */
    int height;         /* DIP */
    int system_frame;   /* 0 (default) = borderless custom chrome, 1 = system title bar */
    int resizable;      /* 1 = WS_THICKFRAME */
    int accept_files;   /* 1 = WS_EX_ACCEPTFILES */
    int x;              /* 屏幕物理像素 (Win32 惯例). x=0 且 y=0 → 屏幕居中.
                           build 94+ L23: 之前是 DIP, 跟 set_position / set_rect
                           的 screen-px 输入不自洽, 改齐. 持久化 DPI-stable 位置
                           用 ui_window_dpi() 自己 MulDiv. */
    int y;              /* 屏幕物理像素. 见上. */
    int tool_window;    /* 1 = 工具窗口（不在任务栏显示图标） */
    int skip_animation; /* 1 = 跳过开场动画（文件关联打开时用，加速首次显示） */
    const void* icon_pixels; /* RGBA 像素数据（32bpp），NULL = 默认图标 */
    int icon_width;          /* 图标宽度 */
    int icon_height;         /* 图标高度 */
    /* Build 65+ (L14): owner 窗口 handle. 0 = 顶级窗口 (默认). 非 0 时新窗口
     * 附属于该 UiWindow — Z-order 在 owner 之上, owner 最小化 / 关闭时跟随,
     * 不在 Alt+Tab / taskbar 单独显示一项 (lib 内部会撤掉 WS_EX_APPWINDOW
     * ex style). Win32 owner 关系是顶级窗口之间的生命周期绑定, 不是 child
     * window (child window 会限制在父窗矩形里 — 不是这里的语义). 适合设置
     * 对话框 / 偏好窗 / 子工具窗附属于主窗口. */
    UiWindow owner;
    /* Build 105+ (L25): 1 = 首帧即 maximized 态; 0 (default) = normal.
     * 持久化"最大化关 → 最大化开"用. caller 自己在 Create 后调
     * ShowWindow(SW_MAXIMIZE) 会跟 lib 内部 layered fade-in / SW_HIDE 时序
     * 撞车出 1 帧 normal → max 闪烁, 用这个 hint 让 lib 内部走现有的
     * preMaximized 路径 (Show / ShowImmediate 都识别) 一次到位.
     *
     * x/y/width/height 在 start_maximized=1 时仍要填 — 那是用户双击 titlebar
     * 还原后的位置/尺寸, lib 不会丢弃. */
    int start_maximized;
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
/* Trace sink                                                          */
/* ------------------------------------------------------------------ */
typedef enum UiTraceFieldType {
    UI_TRACE_FIELD_I64  = 1,
    UI_TRACE_FIELD_U64  = 2,
    UI_TRACE_FIELD_F64  = 3,
    UI_TRACE_FIELD_BOOL = 4,
    UI_TRACE_FIELD_STR  = 5
} UiTraceFieldType;

typedef struct UiTraceField {
    const char* key;
    int         type;       /* UiTraceFieldType */
    int64_t     i64;
    uint64_t    u64;
    double      f64;
    int         boolean;
    const char* str;        /* UTF-8, borrowed; valid only during callback */
} UiTraceField;

typedef struct UiTraceEvent {
    const char* source;     /* core_window / gh_img_view / ... */
    const char* category;   /* window / render / input / ... */
    const char* name;       /* event name */
    uint32_t    field_count;
    const UiTraceField* fields;
} UiTraceEvent;

typedef void (*UiTraceSink)(const UiTraceEvent* event, void* userdata);

/* Registers a process-wide trace sink. core-ui never owns output policy:
 * callers decide enablement, filtering, persistence, and analysis. */
UI_API void ui_trace_set_sink(UiTraceSink sink, void* userdata);

/* ------------------------------------------------------------------ */
/* Window management                                                  */
/* ------------------------------------------------------------------ */
UI_API UiWindow ui_window_create(const UiWindowConfig* config);
UI_API void     ui_window_destroy(UiWindow win);
UI_API void     ui_window_show(UiWindow win);
UI_API void     ui_window_show_immediate(UiWindow win);  /* 跳过开场动画 */
UI_API void     ui_window_show_immediate_no_activate(UiWindow win);  /* 显示但不抢键盘焦点 */
UI_API void     ui_window_prepare_rt(UiWindow win);     /* 预创建渲染目标（不显示窗口） */
UI_API void     ui_window_hide(UiWindow win);
UI_API void     ui_window_set_root(UiWindow win, UiWidget root);
UI_API void     ui_window_set_title(UiWindow win, const wchar_t* title);
UI_API void     ui_window_set_resizable(UiWindow win, int resizable);
UI_API void     ui_window_invalidate(UiWindow win);
/* Batch visual mutations on one window. While active, resize/paint invalidations
 * are coalesced and only the final state is painted when the outermost batch
 * ends. Calls may be nested.
 *
 * CanvasVisualTransaction is for image/canvas workflows that update widget
 * visual state and HWND geometry as one unit. */
UI_API void     ui_window_begin_canvas_visual_transaction(UiWindow win);
UI_API void     ui_window_end_canvas_visual_transaction(UiWindow win);
/* 强制重新布局（set_visible 等改变影响布局的属性后调用） */
UI_API void     ui_window_relayout(UiWindow win);
UI_API void*    ui_window_hwnd(UiWindow win);   /* returns HWND */

/* DPI 比例 = 当前显示器 DPI / 96. 100% 缩放 → 1.0, 125% → 1.25, 150% → 1.5.
 * 用于把"屏幕物理像素 vs DIP/logical 像素"换算. 例如 1:1 zoom 语义 (image
 * pixel = screen physical pixel) 在 PMv2 + SetDpi(144) 环境下需要
 * ui_gh_img_view_set_zoom(canvas, 1.0f / ui_window_dpi_scale(win)) 才正确. */
UI_API float    ui_window_dpi_scale(UiWindow win);

/* ------------------------------------------------------------------ */
/* Window callbacks                                                   */
/* ------------------------------------------------------------------ */
typedef void (*UiWindowCloseCallback)(UiWindow win, void* userdata);
/* WM_CLOSE/标题栏关闭按钮/Alt+F4 的关闭请求回调。
 * 返回 0 = 宿主已处理，取消默认 DestroyWindow；非 0 = 继续默认关闭。
 * 与 ui_window_on_close 不同：on_close 是关闭前通知，不能阻止关闭。 */
typedef int  (*UiWindowCloseRequestCallback)(UiWindow win, void* userdata);
typedef void (*UiWindowResizeCallback)(UiWindow win, int w, int h, void* userdata);
typedef void (*UiWindowDropCallback)(UiWindow win, const wchar_t* path, void* userdata);
typedef void (*UiWindowKeyCallback)(UiWindow win, int vk_code, void* userdata);

/* ------------------------------------------------------------------ */
/* Context Menu                                                       */
/* ------------------------------------------------------------------ */
typedef uint64_t UiMenu;

/* BREAKING (build 75 / L17 follow-up): menu 改为 widget-template 模型 ——
 * 老的 ui_menu_add_item / add_item_ex / add_submenu(text, sub) imperative
 * 构造路径全删. 菜单只能通过声明式 .uix `<menu>` `<menuitem>` 声明, lib
 * PageState 内部按 widget tree 实例化. 保留下面的 lifecycle / 视觉 API
 * 供 imperative show + 全局视觉 tweak 用. */
UI_API UiMenu   ui_menu_create(void);
UI_API void     ui_menu_destroy(UiMenu menu);
UI_API void     ui_menu_add_separator(UiMenu menu);
UI_API void     ui_menu_set_enabled(UiMenu menu, int id, int enabled);
UI_API void     ui_menu_set_bg_color(UiMenu menu, UiColor color);
/* Enable or disable the frosted-glass popup material. When disabled, menus use
 * an opaque theme card and do not capture the backdrop. */
UI_API void     ui_menu_set_frosted_material(UiMenu menu, int enabled);
/* Control the frosted material blur radius in DIP. The material must be enabled
 * separately with ui_menu_set_frosted_material. radius < 0 restores the default
 * auto blur, radius == 0 disables backdrop capture while keeping material tint,
 * radius > 0 uses that explicit blur radius. */
UI_API void     ui_menu_set_backdrop_blur(UiMenu menu, float radius);
/* Build 69+ (L19): 单菜单圆角半径覆盖. <0 = 用 lib 默认 (10.0). 影响 shadow
 * + card bg. hover item highlight (6px) 不动 — 它是 item 级视觉, 不属于
 * 容器圆角范畴. submenu 单独设, 不继承父菜单. */
UI_API void     ui_menu_set_corner_radius(UiMenu menu, float radius);
UI_API void     ui_menu_show(UiWindow win, UiMenu menu, float x, float y);
UI_API void     ui_menu_close(UiWindow win);

/* Toast notification (auto-dismiss)
 *
 * 简单入口 ui_toast: 顶部 + slide-in/out + 无图标, 覆盖 95% 场景.
 * 完整入口 ui_toast_ex: position (0=top 1=center 2=bottom) + icon
 *   (0=none 1=success 2=error 3=warning) + anim (UI_TOAST_ANIM_*).
 *
 * BREAKING (build 106): ui_toast_at 已删除; ui_toast_ex 签名增加 anim 参数.
 *   旧 caller 改 ui_toast_ex(win, text, dur, pos, icon, UI_TOAST_ANIM_SLIDE)
 *   保留原行为. ui_toast(3 参数) 不变.
 */
enum {
    UI_TOAST_ANIM_SLIDE = 0,  /* 默认: top/bottom 滑入, center 直现, 退出淡出 */
    UI_TOAST_ANIM_FADE  = 1,  /* 纯渐入渐出: alpha 0→1→1→0, y 全程钉在目标位置 */
};

UI_API void     ui_toast(UiWindow win, const wchar_t* text, int duration_ms);
UI_API void     ui_toast_ex(UiWindow win, const wchar_t* text, int duration_ms,
                            int position, int icon, int anim);

/* ------------------------------------------------------------------ */
/* MsgBox (独立窗口模态询问框, build 158)                              */
/* ------------------------------------------------------------------ */
/* BREAKING (build 158): in-window 覆盖层式 ui_dialog_* 移除, 由独立窗口
 * 的 ui_msgbox 取代 — 像系统 MessageBox 的使用手感: 同步阻塞、任意
 * 1..4 个按钮、键盘语义 (Enter=默认 / Esc=取消 / Tab 切焦点)、主题
 * 跟随、居中于宿主、按宿主 DPI。 */

enum {
    UI_MSGBOX_ICON_NONE     = 0,
    UI_MSGBOX_ICON_INFO     = 1,
    UI_MSGBOX_ICON_WARNING  = 2,
    UI_MSGBOX_ICON_ERROR    = 3,
    UI_MSGBOX_ICON_QUESTION = 4,
};

/* 模态询问框 — 阻塞到用户选择, 返回点击的按钮索引。
 *   buttons/button_count: 1..4 个按钮文案 (从左到右排列, 整体右对齐)
 *   default_idx: Enter 触发 + accent 主按钮高亮
 *   cancel_idx:  Esc / Alt+F4 / 系统关闭 返回此索引; -1 = 不可取消
 * 模态期间宿主窗口被禁用, 返回前恢复并交还前台。 */
UI_API int ui_msgbox(UiWindow win,
                     const wchar_t* title, const wchar_t* message,
                     const wchar_t* const* buttons, int button_count,
                     int default_idx, int cancel_idx, int icon);

/* 完整形态 (build 159) — params in / result out 成对结构, 两端加字段都
 * 不动签名 (params 走 struct_size 护栏, result 走 _reserved 余量)。
 * ui_msgbox 是本接口的便捷皮。 */
typedef struct UiMsgBoxParams {
    uint32_t struct_size;          /* = sizeof(UiMsgBoxParams) */
    const wchar_t* title;
    const wchar_t* message;
    const wchar_t* const* buttons; /* 1..4 个 */
    int button_count;
    int default_idx;               /* Enter 触发 + accent 主按钮 */
    int cancel_idx;                /* Esc/关闭; -1 = 不可取消 */
    int icon;                      /* UI_MSGBOX_ICON_* */
    /* 勾选框 (系统 TaskDialog verification checkbox 同位): NULL = 不显示。
     * 放按钮行上方、与消息文本左对齐。 */
    const wchar_t* check_text;
    int check_initial;
    /* 按钮自定义色 (build 160): NULL = 全默认。数组长度 = button_count,
     * alpha=0 的项 = 该按钮默认样式。有色按钮 = 实底 + 按亮度自动黑白字
     * + 自动 hover/press 反馈 (危险操作红等)。
     * struct_size 护栏: 旧编译的调用方 (不含本字段的 sizeof) 仍合法 —
     * 实现按 struct_size 判定字段可用性。 */
    const UiColor* button_colors;
    /* 每按钮快捷键绑定 (build 172): NULL = 不绑 (仍走 default_idx=Enter /
     * cancel_idx=Esc)。数组长度 = button_count, 元素为 Win32 VK 键码 (如
     * VK_RETURN/VK_ESCAPE/'Y'/'N'), 0 = 该按钮无绑定。按某键即触发绑该键的
     * 按钮 (按数组顺序取首个命中); button_keys 优先于 default_idx/cancel_idx 的
     * Enter/Esc 兜底。供"是=Y/否=N"类多按钮对话框 + IPC dialog_key 自动化。
     * struct_size 护栏同 button_colors。 */
    const int* button_keys;
} UiMsgBoxParams;

typedef struct UiMsgBoxResult {
    int button;        /* 被点按钮索引 (Esc/关闭 = cancel_idx) */
    int checked;       /* 勾选框终态 0/1 (无勾选框 = check_initial 原样) */
    int _reserved[2];  /* ABI 余量 — 加结果维度不破返回布局 */
} UiMsgBoxResult;

UI_API UiMsgBoxResult ui_msgbox_ex(UiWindow win, const UiMsgBoxParams* params);

/* 不透明句柄 — 指向被点击菜单项 (id + 全部静态属性). 仅在回调期间有效;
 * 出回调即失效, 宿主需要的值要当场拷走。 */
typedef const void* UiMenuItem;
/* BREAKING (build 130): item_id (int) → UiMenuItem 句柄. 菜单项 id 从"数字耦合
 * 枚举值"升级为字符串 key, 回调还能读该项任意属性 (data-* 等)。用
 * ui_menu_item_id / ui_menu_item_attr 取值。 */
typedef void (*UiMenuCallback)(UiWindow win, UiMenuItem item, void* userdata);
UI_API void ui_window_on_menu(UiWindow win, UiMenuCallback cb, void* userdata);
/* 读点击项的 "id" 属性 (key 如 "cmd_delete" 或数字串 "1000")。无 id → NULL。
 * 返回的指针仅在回调期间有效。UTF-8。 */
UI_API const char* ui_menu_item_id(UiMenuItem item);
/* 读点击项的任意静态属性 (如 "data-index" / "shortcut" / 自定义)。缺 → NULL。
 * 返回的指针仅在回调期间有效。UTF-8。 */
UI_API const char* ui_menu_item_attr(UiMenuItem item, const char* name);

typedef void (*UiRightClickCallback)(UiWindow win, float x, float y, void* userdata);
UI_API void ui_window_on_right_click(UiWindow win, UiRightClickCallback cb, void* userdata);

/* L219: 标题栏背景"拖动"(按下后超过系统拖动阈值)回调。仅当
 * ui_window_set_titlebar_drag_intercept(win, 1) 开启拦截时触发 —— 此时标题栏背景
 * 拖动不走系统移动(拖窗), 而 fire 此回调(纯点击不触发)。典型用途: 全屏态拖标题栏退出
 * 全屏。intercept=0(默认)时标题栏拖动走正常拖窗。 */
typedef void (*UiTitleBarDragCallback)(UiWindow win, void* userdata);
UI_API void ui_window_on_titlebar_drag(UiWindow win, UiTitleBarDragCallback cb, void* userdata);
UI_API void ui_window_set_titlebar_drag_intercept(UiWindow win, int on);

/* ------------------------------------------------------------------ */
/* Window callbacks                                                   */
/* ------------------------------------------------------------------ */
UI_API void ui_window_on_close(UiWindow win, UiWindowCloseCallback cb, void* userdata);
UI_API void ui_window_on_close_request(UiWindow win, UiWindowCloseRequestCallback cb, void* userdata);
UI_API void ui_window_on_resize(UiWindow win, UiWindowResizeCallback cb, void* userdata);
UI_API void ui_window_on_drop(UiWindow win, UiWindowDropCallback cb, void* userdata);
UI_API void ui_window_on_key(UiWindow win, UiWindowKeyCallback cb, void* userdata);

/* ------------------------------------------------------------------ */
/* Layout containers                                                  */
/* ------------------------------------------------------------------ */
UI_API UiWidget ui_vbox(void);
UI_API UiWidget ui_hbox(void);
UI_API UiWidget ui_stack(void);
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
/* chips-input (build 259+): 单行模板编辑器 — 原子 chip + 自由文本混排.
 * value 序列化为模板串, chip 记作 {key}. .uix 标签 <chips-input
 * placeholder=".." chip-labels="key=显示名;..">, 支持 :value / v-model /
 * @change. 是窗口拖放框架的原生投放目标 (载荷=chip key). */
UI_API UiWidget ui_chips_input(const wchar_t* placeholder);
UI_API void     ui_chips_input_set_value(UiWidget w, const wchar_t* tpl);
/* 返回内部缓冲, 下次调用前有效. */
UI_API const wchar_t* ui_chips_input_get_value(UiWidget w);
UI_API void     ui_chips_input_set_chip_labels(UiWidget w, const wchar_t* spec);
UI_API void     ui_chips_input_insert_chip(UiWidget w, const wchar_t* key);
typedef void (*UiChipsChangedCallback)(UiWidget w, const wchar_t* value,
                                        void* userdata);
UI_API void     ui_chips_input_on_changed(UiWidget w, UiChipsChangedCallback cb,
                                           void* userdata);
UI_API UiWidget ui_combobox(const wchar_t** items, int count);
UI_API UiWidget ui_radio_button(const wchar_t* text, const char* group);
UI_API UiWidget ui_toggle(const wchar_t* text);
UI_API UiWidget ui_progress_bar(float min_val, float max_val, float value);
UI_API UiWidget ui_tab_control(void);
UI_API UiWidget ui_scroll_view(void);
UI_API UiWidget ui_nav_item(const wchar_t* text, const char* svg);
UI_API UiWidget ui_split_view(void);
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
/* ImageViewPlus (万能图像查看：位图 / SVG 矢量 / GIF / 分块 / 未来扩展)
 *
 * 与 ui_image_view 的区别：
 *   - 对 SVG 使用 D2D1SvgDocument 原生矢量（Win10 1607+，自动降级到路径解析）
 *   - 动画统一 Tick 驱动，扩展新后端无需改 widget
 *   - 加载按扩展名自动分派，调用方统一接口
 */
/* ------------------------------------------------------------------ */
UI_API UiWidget ui_image_view_plus(void);
UI_API int      ui_image_view_plus_load(UiWidget w, UiWindow win, const wchar_t* path);
UI_API void     ui_image_view_plus_set_pixels(UiWidget w, UiWindow win,
                                               const void* pixels, int width, int height, int stride);
UI_API void     ui_image_view_plus_clear(UiWidget w);

UI_API float    ui_image_view_plus_get_zoom(UiWidget w);
UI_API void     ui_image_view_plus_set_zoom(UiWidget w, float zoom);
/* 以 widget 像素坐标 (anchor_x, anchor_y) 为锚点缩放：anchor 处的图像
 * 像素位置在 zoom 前后保持不变。鼠标 wheel 缩放就是这条路径；下游 reload
 * (切 pyramid level / 换图) 后想保持鼠标锚点也用这个。 */
UI_API void     ui_image_view_plus_set_zoom_around(UiWidget w, float zoom,
                                                    float anchor_x, float anchor_y);
UI_API void     ui_image_view_plus_fit(UiWidget w);
UI_API void     ui_image_view_plus_reset(UiWidget w);
UI_API void     ui_image_view_plus_get_pan(UiWidget w, float* out_x, float* out_y);
UI_API void     ui_image_view_plus_set_pan(UiWidget w, float x, float y);
UI_API void     ui_image_view_plus_set_zoom_range(UiWidget w, float min_zoom, float max_zoom);

UI_API int      ui_image_view_plus_width(UiWidget w);        /* 考虑旋转 */
UI_API int      ui_image_view_plus_height(UiWidget w);
UI_API int      ui_image_view_plus_raw_width(UiWidget w);    /* 不含旋转 */
UI_API int      ui_image_view_plus_raw_height(UiWidget w);

UI_API void     ui_image_view_plus_set_rotation(UiWidget w, int angle);
UI_API int      ui_image_view_plus_get_rotation(UiWidget w);

UI_API void     ui_image_view_plus_set_checkerboard(UiWidget w, int on);
UI_API void     ui_image_view_plus_set_antialias(UiWidget w, int on);
UI_API int      ui_image_view_plus_get_antialias(UiWidget w);
/* 自由拖拽：on=1 任意尺寸都能拖到画布外；on=0（默认）小图强制居中，大图限边界内 */
UI_API void     ui_image_view_plus_set_free_pan(UiWidget w, int on);
UI_API int      ui_image_view_plus_get_free_pan(UiWidget w);

UI_API void     ui_image_view_plus_set_loading(UiWidget w, int loading);
UI_API int      ui_image_view_plus_get_loading(UiWidget w);

/* 动画 */
UI_API int      ui_image_view_plus_is_animated(UiWidget w);
UI_API int      ui_image_view_plus_frame_count(UiWidget w);
UI_API int      ui_image_view_plus_current_frame(UiWidget w);
UI_API void     ui_image_view_plus_start_animation(UiWidget w);
UI_API void     ui_image_view_plus_stop_animation(UiWidget w);

/* 源元信息 */
UI_API int      ui_image_view_plus_is_vector(UiWidget w);    /* SVG / 其它矢量后端 */
UI_API const char* ui_image_view_plus_source_type(UiWidget w);

/* Crop 模式 */
UI_API void     ui_image_view_plus_set_crop_mode(UiWidget w, int on);
UI_API int      ui_image_view_plus_is_crop_mode(UiWidget w);
UI_API void     ui_image_view_plus_set_crop_rect(UiWidget w, float x, float y, float width, float height);
UI_API void     ui_image_view_plus_get_crop_rect(UiWidget w, float* x, float* y, float* width, float* height);
UI_API void     ui_image_view_plus_set_crop_aspect(UiWidget w, float ratio);   /* 0=free */
UI_API void     ui_image_view_plus_reset_crop(UiWidget w);

typedef void (*UiCropCallback)(UiWidget widget, float x, float y, float w, float h, void* userdata);
UI_API void     ui_image_view_plus_on_crop_changed(UiWidget w, UiCropCallback cb, void* userdata);

typedef void (*UiLoadedCallback)(UiWidget widget, void* userdata);
typedef void (*UiLoadFailedCallback)(UiWidget widget, const wchar_t* path, void* userdata);
UI_API void     ui_image_view_plus_on_loaded(UiWidget w, UiLoadedCallback cb, void* userdata);
UI_API void     ui_image_view_plus_on_load_failed(UiWidget w, UiLoadFailedCallback cb, void* userdata);
UI_API void     ui_image_view_plus_on_viewport_changed(UiWidget w, UiViewportCallback cb, void* userdata);

/* 分块大图模式 */
UI_API void     ui_image_view_plus_begin_tiled(UiWidget w, UiWindow win,
                                                 int full_width, int full_height, int tile_size);
UI_API void     ui_image_view_plus_set_tile(UiWidget w, int tile_x, int tile_y,
                                             const void* pixels, int width, int height, int stride);
UI_API void     ui_image_view_plus_clear_tiles(UiWidget w);

/* ------------------------------------------------------------------ */
/* GhImgView (通用瓦块画布；按 ghde 数据形状设计但不依赖解码器)        */
/*                                                                    */
/* 数据契约（见 CLAUDE.md "ui_gh_img_view widget — 数据形状契约"）：   */
/*   - 像素布局 = BGRA8 premultiplied                                 */
/*   - 瓦块尺寸 = 256 × 256（边缘可不足）                              */
/*   - 多级 pyramid，顶级 = 0，每级 1/2 降采样                         */
/*   - 旋转 = 像素已烘焙（widget 不二次旋转）                          */
/*                                                                    */
/* 调用方负责喂数据；widget 只管显示与交互。不做文件 IO / 解码 / 动画。*/
/* ------------------------------------------------------------------ */
typedef struct UiGhImgViewInfo {
    uint32_t full_width;
    uint32_t full_height;
    uint32_t tile_size;       /* 256 */
    uint32_t levels;          /* 1 = 单级；N = pyramid N 级 */
    uint32_t pixel_format;    /* 0 = BGRA8 premul（v1 仅此） */
    uint32_t keep_preview;    /* 原 reserved[0]: 1 = 切金字塔(begin)时保留现有 preview
                                 兜底层不清空 — 宿主已先 set_preview 一张清晰低清图,
                                 换金字塔结构时让它继续兜底, 消除"清晰预览→低清垫底→
                                 清晰"的闪烁 (L168)。默认 0 = 旧行为 (begin 清 preview)。*/
    uint32_t reserved[2];     /* 必须置零，留给 v2 字段（HDR fp16 等） */
} UiGhImgViewInfo;

typedef struct UiGhImgViewport {
    uint32_t active_level;
    float    zoom;
    float    pan_x, pan_y;
    /* 当前 level 网格下，可见瓦块半开区间 [tx0,tx1) × [ty0,ty1) */
    uint32_t visible_tx0, visible_ty0;
    uint32_t visible_tx1, visible_ty1;
} UiGhImgViewport;

typedef void (*UiGhImgViewViewportCallback)(UiWidget w, const UiGhImgViewport* vp, void* userdata);

UI_API UiWidget ui_gh_img_view(void);
UI_API void     ui_gh_img_view_begin(UiWidget w, UiWindow win, const UiGhImgViewInfo* info);
UI_API void     ui_gh_img_view_set_preview(UiWidget w, UiWindow win,
                                            const void* bgra, uint32_t pw, uint32_t ph,
                                            uint32_t stride);
UI_API void     ui_gh_img_view_set_tile(UiWidget w, UiWindow win,
                                         uint32_t level, uint32_t tx, uint32_t ty,
                                         const void* bgra, uint32_t tw, uint32_t th,
                                         uint32_t stride);
UI_API void     ui_gh_img_view_clear_level(UiWidget w, uint32_t level);
UI_API void     ui_gh_img_view_clear(UiWidget w);

/* L115: tile 批量提交. begin 后多次 ui_gh_img_view_set_tile 不逐个触发重绘,
 * end 时一次重绘. 宿主切金字塔级时把整级可见 tile 攒齐、一次 batch 提交, 配合
 * lib 内部"切级保留旧级"(OnDraw 多级 fallback), 消除放大切级的逐 tile 波浪刷新.
 * begin/end 必须成对; end 触发一次 invalidate. */
UI_API void     ui_gh_img_view_begin_tile_batch(UiWidget w);
UI_API void     ui_gh_img_view_end_tile_batch(UiWidget w);
UI_API void     ui_gh_img_view_begin_view_update(UiWidget w);
UI_API void     ui_gh_img_view_end_view_update(UiWidget w);

/* Build 70+ (L20): SVG 矢量源. 喂一个 .svg 文件, widget 进入 SVG 模式 —
 * 跳过瓦块逻辑, OnDraw 直接 DrawSvgDocument (ID2D1DeviceContext5 原生光栅化).
 * info_.fullWidth/Height 由 SVG natural size (优先 viewBox, fallback width/
 * height) 喂入, Fit/Reset/zoom 等几何全部复用瓦块路径代码.
 *
 * 之前 tile / preview 状态被清空 (转换 source type).
 *
 * 返 0 OK / 非 0 失败 (文件不存在 / SVG 解析失败 / Win10 1607 前
 * CreateSvgDocument 不可用). 失败时 widget 状态不变.
 *
 * out_w / out_h 出 SVG natural size, 可为 NULL (调用方不需要时跳过).
 * 拿到尺寸后通常配 ui_gh_img_view_get_zoom 算实际显示像素 (zoom × natural). */
UI_API int      ui_gh_img_view_set_svg_file(UiWidget w, const wchar_t* path,
                                              uint32_t* out_w, uint32_t* out_h);
UI_API int      ui_gh_img_view_is_svg_mode(UiWidget w);

/* Build 71+ (L21): 把 SetSvgFile 加载的 SVG 光栅化到 caller 缓冲, 用于鸟瞰图缩
 * 略图等场景. fit 保 aspect, 实际像素尺寸经 out_w/out_h 回填, BGRA8 premul 数据
 * 写 out_bgra (大小 = out_w*out_h*4 字节, packed). out_bgra 至少分配
 * target_w*target_h*4 字节.
 *
 * 返 0 OK / 非 0 失败 (-1 = 未加载 SVG, -2 = 参数错, -3 = D2D 不支持, 其他
 * 内部 D2D 错误). 失败时 out_bgra 内容未定义. */
UI_API int      ui_gh_img_view_render_svg_to_bgra(UiWidget w,
                                                    uint32_t target_w,
                                                    uint32_t target_h,
                                                    uint8_t* out_bgra,
                                                    uint32_t* out_w,
                                                    uint32_t* out_h);

UI_API void     ui_gh_img_view_set_auto_level(UiWidget w, int on);
UI_API int      ui_gh_img_view_get_auto_level(UiWidget w);
UI_API void     ui_gh_img_view_set_active_level(UiWidget w, uint32_t level);
UI_API uint32_t ui_gh_img_view_get_active_level(UiWidget w);

/* 采样模式 (抗锯齿). on=1 (默认) 放大时平滑 (HQ_CUBIC/LINEAR); on=0 放大时
 * NEAREST_NEIGHBOR, 像素边界清晰 (像素画 / 逐像素查看). 下采始终平滑. */
UI_API void     ui_gh_img_view_set_antialias(UiWidget w, int on);
UI_API int      ui_gh_img_view_get_antialias(UiWidget w);
/* 棋盘垫层. on=1 在图片矩形 (旋转后可见 AABB) 底下画主题色棋盘格, 透明像素
 * 透出棋盘; on=0 (默认) 不画。不透明图开着无视觉差异 (被完全盖住), 宿主可只按
 * "画布背景是否纯色" 决定开关, 无需探测图片 alpha. letterbox 区永不画. */
UI_API void     ui_gh_img_view_set_checkerboard(UiWidget w, int on);
UI_API int      ui_gh_img_view_get_checkerboard(UiWidget w);
/* 图片阴影 (build 283+): 在图片矩形**外圈**画投影 + 一道 1dip 描边, 让图片边界
 * 从画布背景里浮出来。默认关。参数按当前主题自动取 (浅色以投影为主, 深色以描边
 * 为主 —— 黑投影在深色画布上几乎不可见), 尺寸按 dip、不随缩放变。
 * 只画外圈: 整块模糊铺在图片底下的话, 透明 PNG 在关棋盘时会透出暗块。 */
UI_API void     ui_gh_img_view_set_shadow(UiWidget w, int on);
UI_API int      ui_gh_img_view_get_shadow(UiWidget w);
/* 适应窗口 (ui_gh_img_view_fit) 时四周留出的内边距 (dip)。默认 0 = 长边贴满视口。
 * **开了图片阴影就必须留一点**, 否则图片贴着视口边, 阴影整条画在视口外看不见。
 * 画布很小时按短边 25% 自动夹紧, 不会把可用空间吃光。 */
UI_API void     ui_gh_img_view_set_fit_padding(UiWidget w, float dip);
UI_API float    ui_gh_img_view_get_fit_padding(UiWidget w);
/* 内部滚轮缩放开关. on=1 (默认) 滚轮缩放; on=0 widget 不内部缩放, 宿主用
 * ui_widget_on_mouse_wheel hook 自行接管滚轮 (如缩放 / 切图二选一). 命令式
 * 缩放 API (set_zoom / set_zoom_around 等) 不受此开关影响. */
UI_API void     ui_gh_img_view_set_wheel_zoom_enabled(UiWidget w, int on);
UI_API int      ui_gh_img_view_get_wheel_zoom_enabled(UiWidget w);
/* 内部鼠标拖动平移按轴锁. lock_x/lock_y=1 时该轴拖动不平移 (默认两轴 0=自由)。
 * 用于"锁定/只读"视图: 如长图锁水平 set_pan_lock(w,1,0) → 左右固定居中、上下仍
 * 可拖动阅读。命令式 set_pan 不受影响; 拖动期间 ui_widget_on_mouse_move hook 仍
 * fire (宿主"拖出图片"等手势照常)。 */
UI_API void     ui_gh_img_view_set_pan_lock(UiWidget w, int lock_x, int lock_y);
UI_API void     ui_gh_img_view_get_pan_lock(UiWidget w, int* out_lock_x, int* out_lock_y);

UI_API float    ui_gh_img_view_get_zoom(UiWidget w);
/* 渲染线程模式下 widget 拿不到 RT DPI, host 需喂入窗口 DPI scale
   (ui_window_dpi_scale) — 影响 mip 级选择与整数倍缩放的像素对齐. */
UI_API void     ui_gh_img_view_set_dpi_scale(UiWidget w, float scale);
UI_API void     ui_gh_img_view_set_zoom(UiWidget w, float zoom);
UI_API void     ui_gh_img_view_set_zoom_around(UiWidget w, float zoom,
                                                float anchor_x, float anchor_y);
UI_API void     ui_gh_img_view_set_zoom_range(UiWidget w, float lo, float hi);
UI_API void     ui_gh_img_view_get_pan(UiWidget w, float* out_x, float* out_y);
UI_API void     ui_gh_img_view_set_pan(UiWidget w, float x, float y);
UI_API void     ui_gh_img_view_fit(UiWidget w);
UI_API void     ui_gh_img_view_reset(UiWidget w);

/* Rotation (90° 倍数, CW). pan/zoom 不变. angle: 任意 int → 规整到
 * 0/90/180/270 (mod 360 + round to nearest 90). pan 存屏幕空间, 鼠标
 * 拖动方向永远匹配视觉方向, 跟旋转角度解耦. Begin / Clear 把 rotation
 * 重置为 0. */
UI_API void     ui_gh_img_view_set_rotation(UiWidget w, int angle);
UI_API int      ui_gh_img_view_get_rotation(UiWidget w);

UI_API void     ui_gh_img_view_on_viewport(UiWidget w,
                                            UiGhImgViewViewportCallback cb,
                                            void* userdata);

/* Tile evict callback — NotifyViewport 内 trim viewport 外 tile 时, lib 对
 * 每个被清的 tile fire 一次, caller 同步自己端"已 push"状态 (典型
 * pushed_tiles_ 之类 unordered_set::erase), 让下次 viewport callback 能
 * 重新 enqueue 该 tile 解码 (如果它又回到 viewport 内).
 *
 * 设计语义 (L48): tile cache 严格跟 viewport 绑定, 内存稳态 = viewport 内
 * tile 数 × 256KB. zoom out / pan 跨 level 重解码 viewport tile, 单 tile
 * 0.38ms × 4 worker 并发 ~10ms 不可感知.
 *
 * 不注册 (cb=NULL) → trim 静默. caller 不维护"已 push"set 也不需要注册.
 * cb 在 NotifyViewport 调用栈里同步 fire (UI 线程), 不要做阻塞操作. */
typedef void (*UiGhImgViewTileEvictedCallback)(
    UiWidget w, uint32_t level, uint32_t tx, uint32_t ty, void* userdata);
UI_API void     ui_gh_img_view_on_tile_evicted(UiWidget w,
                                                 UiGhImgViewTileEvictedCallback cb,
                                                 void* userdata);

/* ------------------------------------------------------------------ */
/* OcrImgView — 图片划词 (OCR 文本选择画布)                            */
/*                                                                    */
/* 宿主加载图 → 调任意 OCR 引擎 → 把 [文本 + quad 坐标] 喂进来 →      */
/* 用户像 PDF 阅读器一样在图上拖动划词、Ctrl+C 复制。                  */
/*                                                                    */
/* 数据契约:                                                          */
/*   - 图像: 单张 BGRA8 premultiplied 整图 (v1; 瓦块大图留扩展)        */
/*   - quad: 顶级图像像素坐标, 4 顶点 TL,TR,BR,BL; 轴对齐 bbox 是      */
/*     其特例, 支持任意凸四边形 (倾斜/透视文本)                        */
/*   - items 数组顺序 == 阅读序 (宿主保证; 竖排/RTL 同理)              */
/*   - 层级: block(段) > line(行) > item(词/行, OCR 给什么粒度就是什么)*/
/*                                                                    */
/* 复制拼接: 同行直连; 带 SPACE_AFTER 的词后加空格 (拉丁文); 跨行 \n;  */
/* 跨 block \n\n; 选区末词不留尾分隔。                                 */
/*                                                                    */
/* 交互 (对齐 PDF 阅读器惯例):                                        */
/*   单击(文字上或空白) = 清选区    文本上拖动 = 逐【字】划选          */
/*   空白拖动 = 平移画布            双击 = 选中该词                    */
/*   三击 = 选整行                  滚轮 = 以光标为锚缩放              */
/*   旋转 90° 倍数 (set_rotation), 选区与高亮自动跟随                  */
/*   Ctrl+C 复制 / Ctrl+A 全选 / Esc 清选区                            */
/* 选择粒度恒为【字】: CHAR 粒度下宿主直喂字坐标, BLOCK 粒度下 lib 在  */
/* 入口把块拆成字。双击靠 word 分组还原出"词"。                        */
/* 所有 API 仅 UI 线程。                                              */
/* ------------------------------------------------------------------ */

/* item.flags 位定义 */
#define UI_OCR_ITEM_SPACE_AFTER  (1u << 0)   /* 复制时该词后补一个空格 */

typedef struct UiOcrTextItem {
    const char* text;      /* UTF-8, lib 深拷贝, 调用后宿主可释放 */
    float       quad[8];   /* 图像像素坐标: TLx,TLy, TRx,TRy, BRx,BRy, BLx,BLy */
    uint32_t    block;     /* 段落分组 */
    uint32_t    line;      /* 行分组 */
    uint32_t    word;      /* 词分组: 同 block+line+word 的相邻 item = 一个词。
                              CHAR 粒度下双击选词靠它 (一个词的各字填同值);
                              BLOCK 粒度下忽略 (每个 item 自成一词)。
                              注意: CHAR 粒度下若整行都填 0 (常见于零初始化
                              后忘了赋值), 双击会选中【整行】—— 因为整行确实
                              被当成了一个词。lib 不猜词边界, 这是宿主的责任。*/
    uint32_t    flags;     /* UI_OCR_ITEM_* 位 */
    uint32_t    reserved;  /* 置零 (对齐 + 预留) */
} UiOcrTextItem;

/* 喂入粒度。选择粒度始终是【字】—— 差别只在字坐标从哪来。 */
typedef enum UiOcrGranularity {
    /* 每个 item 是一个字 (主路径)。多数 OCR 引擎给得出单字坐标, 直接喂
     * 最准。用 word 字段把字分成词, 双击才能选中整词。 */
    UI_OCR_GRANULARITY_CHAR  = 0,
    /* 每个 item 是一个词/行/块 (兼容路径)。lib 在入口把块按 UTF-8 码点拆
     * 成字, quad 沿字向按字宽权重插值 (CJK/全角 1.0, 空白 0.35, 其余 0.55)。
     * 中文全角等宽 → 拆分几乎精确, 而整行返回正是中文 OCR 常态; 拉丁文是
     * 近似, 但拉丁引擎普遍已按词返回, 块很短, 误差影响小。
     * 块内以 ASCII 空白分词, 中文无法分词故整块归一个词 (双击选整块)。 */
    UI_OCR_GRANULARITY_BLOCK = 1,
} UiOcrGranularity;

/* 选区变化时同步 fire (UI 线程)。拿文本用 ui_ocr_img_view_selected_text。 */
typedef void (*UiOcrSelectionCallback)(UiWidget w, void* userdata);

UI_API UiWidget ui_ocr_img_view(void);

/* 喂单张 BGRA8 premul 整图 (stride==0 按 w*4)。喂图后自动 Fit 并清空
 * 旧文本/选区 (换图语义) — 先 set_image 再 set_text。
 *
 * 返回 0 成功, 非 0 失败:
 *   -1 参数非法 (空指针 / 宽高为 0)
 *   -2 尺寸超出 GPU 单张纹理上限 (典型 16384) —— 宿主须先降采样
 *   -3 资源创建失败 (内存不足等)
 * 务必检查返回值: 尺寸超限在旧版本里表现为"喂图成功但画面空白", 因为
 * 资源存储只存 CPU 字节、D2D 位图要到绘制时才建。现在改为喂入即报错。*/
UI_API int      ui_ocr_img_view_set_image(UiWidget w, UiWindow win,
                                           const void* bgra, uint32_t width,
                                           uint32_t height, uint32_t stride);
UI_API void     ui_ocr_img_view_clear_image(UiWidget w);

/* 喂/替换 OCR 结果。item_stride 传 sizeof(UiOcrTextItem) — 结构体尾部
 * 未来加字段时, 旧编译的宿主传旧 stride 依然正确 (前向兼容惯用法)。
 * granularity 见 UiOcrGranularity: 有单字坐标传 CHAR, 只有行/块框传 BLOCK。*/
UI_API void     ui_ocr_img_view_set_text(UiWidget w, const UiOcrTextItem* items,
                                          uint32_t count, size_t item_stride,
                                          UiOcrGranularity granularity);
UI_API void     ui_ocr_img_view_clear_text(UiWidget w);
/* 当前生效的文本项数 = 【字】数。BLOCK 粒度下这是拆分后的结果, 通常远大于
 * 宿主传入的 count; CHAR 粒度下等于传入的 count。 */
UI_API uint32_t ui_ocr_img_view_item_count(UiWidget w);

/* 当前选中文本 UTF-8。内部缓冲, 下次选区变化前有效; 无选区返 "" (非 NULL)。*/
UI_API const char* ui_ocr_img_view_selected_text(UiWidget w);
UI_API void     ui_ocr_img_view_select_all(UiWidget w);
UI_API void     ui_ocr_img_view_clear_selection(UiWidget w);
UI_API int      ui_ocr_img_view_has_selection(UiWidget w);
/* 选中文本写系统剪贴板 (CF_UNICODETEXT), 等同用户按 Ctrl+C。返 1 成功。 */
UI_API int      ui_ocr_img_view_copy_selection(UiWidget w);
UI_API void     ui_ocr_img_view_on_selection_changed(UiWidget w,
                                                      UiOcrSelectionCallback cb,
                                                      void* userdata);

/* ---- 宿主侧列表 ↔ 画布 双向联动 (build 277+) ----------------------
 * 典型场景: 窗口左边是本 widget, 右边是宿主自己排的"识别结果行列表"。
 * 点列表行 → select_range 让图上高亮对应文字; 图上划词 → selection_range
 * + item_meta 反查行号 → 滚动/高亮右侧列表。
 *
 * 下标空间 = 【当前生效的字级 item】(item_count 的那个空间)。BLOCK 粒度
 * 喂入时 lib 已把块拆成字, 宿主传入的块下标 ≠ 这里的下标 —— 用 item_meta
 * 的 block/line/word 建映射, 别假设一一对应。 */

/* 按阅读序闭区间设选区 [first,last]。first>last 自动交换, 越界 clamp 到
 * 末项, 无数据时 no-op。走的是跟用户拖选同一条状态机, 会 fire
 * selection_changed 回调。 */
UI_API void     ui_ocr_img_view_select_range(UiWidget w, uint32_t first,
                                              uint32_t last);
/* 当前选区的 item 下标闭区间。有选区返 1 并写出 first/last; 无选区返 0
 * (不写出参)。指针可为 NULL。 */
UI_API int      ui_ocr_img_view_selection_range(UiWidget w, uint32_t* first,
                                                 uint32_t* last);
/* 取第 index 个 item 的分组元数据 (BLOCK 粒度拆字后仍保留原 block/line/
 * word)。index 越界返 0, 成功返 1。指针可为 NULL。 */
UI_API int      ui_ocr_img_view_item_meta(UiWidget w, uint32_t index,
                                           uint32_t* block, uint32_t* line,
                                           uint32_t* word);

/* 视口 (语义对齐 ui_gh_img_view 同名 API) */
UI_API void     ui_ocr_img_view_fit(UiWidget w);
UI_API void     ui_ocr_img_view_reset(UiWidget w);
UI_API void     ui_ocr_img_view_set_zoom(UiWidget w, float zoom);
UI_API float    ui_ocr_img_view_get_zoom(UiWidget w);
UI_API void     ui_ocr_img_view_set_pan(UiWidget w, float px, float py);
UI_API void     ui_ocr_img_view_get_pan(UiWidget w, float* px, float* py);
/* 旋转 (90° 倍数, 顺时针度数)。任意 int 会被规整到 0/90/180/270。
 * zoom/pan 在旋转后保留; pan 存屏幕空间, 所以鼠标拖动方向始终匹配视觉方向。
 * 选区与高亮自动跟随旋转 —— 文本 quad 是图像坐标, 换算时已含旋转。
 * 典型用法: 手机横拍的照片 OCR 后转正阅读。 */
UI_API void     ui_ocr_img_view_set_rotation(UiWidget w, int angle);
UI_API int      ui_ocr_img_view_get_rotation(UiWidget w);
/* 图像像素坐标 → 窗口 DIP 坐标 (随当前 zoom/pan)。宿主在选区旁定位
 * 弹窗/工具条用。反向换算用 screen_to_image。 */
UI_API void     ui_ocr_img_view_image_to_screen(UiWidget w, float ix, float iy,
                                                 float* sx, float* sy);
UI_API void     ui_ocr_img_view_screen_to_image(UiWidget w, float sx, float sy,
                                                 float* ix, float* iy);
/* 高亮填充色 RGBA (0~1)。不调用则用主题 accent + 0.35 alpha。 */
UI_API void     ui_ocr_img_view_set_highlight_color(UiWidget w, float rr, float gg,
                                                     float bb, float aa);

/* ------------------------------------------------------------------ */
/* IconButton (SVG icon button)                                      */
/* ------------------------------------------------------------------ */
UI_API UiWidget ui_icon_button(const char* svg, int ghost);
UI_API void     ui_icon_button_set_svg(UiWidget w, const char* svg);
UI_API void     ui_icon_button_set_ghost(UiWidget w, int ghost);
UI_API void     ui_icon_button_set_icon_color(UiWidget w, UiColor color);
UI_API void     ui_icon_button_set_icon_color_role(UiWidget w, UiIconColorRole role);
UI_API void     ui_icon_button_set_icon_padding(UiWidget w, float padding);
/* ghost 模式 hover/press bg 视觉开关. 1 (默认) = 标准按钮反馈,
 * 0 = 永远只画 icon (titlebar 装饰按钮 / 状态指示器场景). 仅对 ghost
 * 模式有意义; normal 模式 hover bg 是核心 feature, 不受这个开关影响. */
UI_API void     ui_icon_button_set_hover_visual(UiWidget w, int enabled);
UI_API int      ui_icon_button_get_hover_visual(UiWidget w);

/* ------------------------------------------------------------------ */
/* TitleBar (borderless window title bar)                             */
/* ------------------------------------------------------------------ */
UI_API UiWidget ui_titlebar(const wchar_t* title);
UI_API void     ui_titlebar_set_title(UiWidget titlebar, const wchar_t* title);
UI_API void     ui_titlebar_show_buttons(UiWidget titlebar, int showMin, int showMax, int showClose);
/* show=0 完全不画图标，标题文字滑到最左；show=1 走"用户设的→EXE 嵌入资源
   ID=1→不画"三段查找。默认 show=1。 */
UI_API void     ui_titlebar_show_icon(UiWidget titlebar, int show);
/* 显式设标题栏图标，覆盖 EXE 嵌入图标的自动加载。传 NULL 清掉，回到自动
   加载行为。

   pixels 字节序是 **BGRA（预乘 alpha）**，即 0xAARRGGBB 的内存序，跟 Windows
   32bpp top-down DIB / DrawIconEx 的输出一致 —— 从 HICON 拿像素可以直接传，
   不要手动 swap R/B（build 291 之前这里的文档写成 "RGBA8888"，下游照字面
   交换通道，橙色图标画成了蓝色）。

   一般不需要调用：build 291 起 EXE 嵌入图标（资源 ID=1）自动加载走 128px
   高分辨率 + 高质量缩小，任何 DPI 缩放都清晰。 */
UI_API void     ui_titlebar_set_icon_pixels(UiWidget titlebar,
                                             const void* rgba, int width, int height);
UI_API void     ui_titlebar_set_bg_color(UiWidget titlebar, UiColor color);
UI_API void     ui_titlebar_add_widget(UiWidget titlebar, UiWidget custom_widget);
/* Title font weight. Default 400 (NORMAL). Typical values:
   400 NORMAL / 500 MEDIUM / 600 SEMI_BOLD / 700 BOLD. */
UI_API void     ui_titlebar_set_title_weight(UiWidget titlebar, int weight);

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
/* build 161: 布局豁免 — pinned=1 且 widget 已有有效 rect 时, relayout 不
 * 改写其位置 (子树仍布局)。给可拖动浮动面板用 (拖后不被反应式重建/菜单
 * 等触发的 relayout 打回 CSS 默认位)。仅对 position:absolute 组件生效;
 * 显式 ui_widget_set_rect 仍有效。 */
UI_API void ui_widget_set_layout_pinned(UiWidget w, int pinned);
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

/* 通用基础属性查询。
 * ui_widget_get_basic: 返回精简 json {"id","type","text"} (比 ui_debug_dump_widget
 *   轻 — 无 rect/metrics/children), 给业务一次拿控件名称/类型/值。free 用 ui_debug_free。
 * ui_widget_get_text: 派生自 basic, 直接取控件文本写进 buf (utf-16, 含结尾 null),
 *   返回文本长度 (不含 null; 若 > cap-1 表示被截断, 可据此扩容重取)。
 * 覆盖所有文本控件: Label/Button/Input/Combo/TextArea/Toggle/Radio/Check/Nav/
 * Overlay/TitleBar; 无文本语义的控件 get_text 返回 0、basic 的 "text" 为 null。 */
UI_API char* ui_widget_get_basic(UiWidget w);
UI_API int   ui_widget_get_text(UiWidget w, wchar_t* buf, int cap);

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

/* 宿主驱动的选区 (build 278) —— 外部数据源 ↔ 文本双向联动。典型场景:
 * OCR 结果窗里图上划词 → 宿主把右侧文本框对应区间选中并滚到可见; 反过来
 * 用户在文本框里拖选 → on_selection_changed 回调里读区间反查图上位置。
 * start/end 是 UTF-16 码元下标; 顺序自动交换, 越界 clamp, 相等=清选区。
 * set_selection 不会触发 on_selection_changed (避免联动回环)。 */
UI_API void           ui_text_area_set_selection(UiWidget w, int start, int end);
UI_API int            ui_text_area_get_selection(UiWidget w, int* start, int* end);
typedef void (*UiTextAreaSelectionCallback)(UiWidget w, void* userdata);
UI_API void           ui_text_area_on_selection_changed(UiWidget w,
                                                         UiTextAreaSelectionCallback cb,
                                                         void* userdata);

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
/* Stack                                                              */
/* ------------------------------------------------------------------ */
UI_API int  ui_stack_get_active_index(UiWidget stack);
UI_API void ui_stack_set_active_index(UiWidget stack, int index);

/* ------------------------------------------------------------------ */
/* NavItem                                                            */
/* ------------------------------------------------------------------ */
UI_API int  ui_nav_get_selected(UiWidget nav_item);
UI_API void ui_nav_set_selected(UiWidget nav_item, int selected);

/* ------------------------------------------------------------------ */
/* SplitView                                                          */
/* ------------------------------------------------------------------ */
typedef enum UiSplitViewMode {
    UI_SPLIT_VIEW_OVERLAY = 0,
    UI_SPLIT_VIEW_INLINE = 1,
    UI_SPLIT_VIEW_COMPACT_OVERLAY = 2,
    UI_SPLIT_VIEW_COMPACT_INLINE = 3
} UiSplitViewMode;

UI_API void ui_split_set_pane(UiWidget split_view, UiWidget pane);
UI_API void ui_split_set_content(UiWidget split_view, UiWidget content);
UI_API int  ui_split_get_open(UiWidget split_view);
UI_API void ui_split_set_open(UiWidget split_view, int open);
UI_API void ui_split_set_open_immediate(UiWidget split_view, int open);
UI_API void ui_split_set_mode(UiWidget split_view, UiSplitViewMode mode);
UI_API void ui_split_set_open_pane_length(UiWidget split_view, float length);
UI_API void ui_split_set_compact_pane_length(UiWidget split_view, float length);

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

/* 光标类型 (跟 widget_factory 解析的 CSS cursor 关键字对齐, 程序化 widget
 * 可通过 ui_widget_set_cursor 直接设置). 数值跟 lib 内部 CursorKind enum
 * 一一对应, 不要重排. */
typedef enum {
    UI_CURSOR_DEFAULT     = 0,     /* IDC_ARROW (inherit) */
    UI_CURSOR_POINTER     = 1,     /* IDC_HAND (hand) */
    UI_CURSOR_TEXT        = 2,     /* IDC_IBEAM */
    UI_CURSOR_CROSSHAIR   = 3,     /* IDC_CROSS */
    UI_CURSOR_WAIT        = 4,     /* IDC_WAIT */
    UI_CURSOR_MOVE        = 5,     /* IDC_SIZEALL */
    UI_CURSOR_NOT_ALLOWED = 6,     /* IDC_NO */
    UI_CURSOR_EW_RESIZE   = 7,     /* IDC_SIZEWE */
    UI_CURSOR_NS_RESIZE   = 8,     /* IDC_SIZENS */
    UI_CURSOR_NESW_RESIZE = 9,     /* IDC_SIZENESW */
    UI_CURSOR_NWSE_RESIZE = 10,    /* IDC_SIZENWSE */
    UI_CURSOR_HELP        = 11,    /* IDC_HELP */
    UI_CURSOR_NONE        = 12     /* 隐藏光标 */
} UiCursor;
UI_API void ui_widget_set_cursor(UiWidget w, int cursor);
UI_API int  ui_widget_get_cursor(UiWidget w);

/* 通用 widget mouse_move 回调 (任意 widget 类型都可挂, 不限于 ImageView).
 * x/y 为 widget-local DIP (相对 widget rect 左上). 调用 cb=NULL 解绑.
 * ui_image_on_mouse_move 只对 ImageViewWidget 有效; ui_gh_img_view /
 * 自绘 widget / div / svg 等需要 mouse 跟踪请用此 API. */
typedef void (*UiWidgetMouseMoveCallback)(UiWidget w, float x, float y, void* userdata);
UI_API void ui_widget_on_mouse_move(UiWidget w,
                                     UiWidgetMouseMoveCallback cb,
                                     void* userdata);

/* widget mouse_leave 回调 (跟 web mouseleave 语义对齐, 任意 widget 类型可挂).
 * 两条触发路径:
 *   1) cursor 同窗口内移到别的 widget → 旧 hovered ancestor 链里不在新链的
 *      widget 触发 leave (逐层);
 *   2) cursor 出窗口 (WM_MOUSELEAVE) → 当前 hovered ancestor 链全员触发.
 * 无 x/y 参数 (leave 时 cursor 已脱离, 坐标无意义). cb=NULL 解绑. */
typedef void (*UiWidgetMouseLeaveCallback)(UiWidget w, void* userdata);
UI_API void ui_widget_on_mouse_leave(UiWidget w,
                                      UiWidgetMouseLeaveCallback cb,
                                      void* userdata);

/* Build 64+ (L13): widget 焦点切换回调. 当 lib 内部 SetFocus 把 widget 切入 /
 * 切出 focusedWidget_ 槽时触发. 任意 widget 类型可挂. 鼠标点击触发的失焦也
 * 走这里 (lib mouse_down 分发完会按 hit-test 结果 SetFocus). cb=NULL 解绑. */
typedef void (*UiWidgetFocusCallback)(UiWidget w, void* userdata);
UI_API void ui_widget_on_focus(UiWidget w,
                                UiWidgetFocusCallback cb,
                                void* userdata);
UI_API void ui_widget_on_blur (UiWidget w,
                                UiWidgetFocusCallback cb,
                                void* userdata);

/* build 287: 提交回调 (TextInput / TextArea 里按 Enter)。接
 * Widget::onSubmitHook —— 跟 .uix 的 @submit 同一条路径。
 *
 * 存在理由: v-for 迭代体内的事件走 WireLoopScopeEvent, 那里只认 click /
 * change / input / dblclick, @submit 接不上; 窗口级 ui_window_on_key 也够不着,
 * 因为 TextInputWidget 的 OnKeyDown 会先把 VK_RETURN 消费掉并返回 true。
 * 于是"在 v-for 里放输入框做行内编辑"这类界面拿不到 Enter。宿主用
 * ui_widget_find_by_id 拿到输入框后直接挂这个回调即可。cb=NULL 解绑。
 * 配合已有的 ui_widget_on_blur 就能做完整的行内编辑 (Enter 提交 / 失焦提交)。 */
UI_API void ui_widget_on_submit(UiWidget w,
                                 UiWidgetFocusCallback cb,
                                 void* userdata);

/* build 174: 编程式把键盘焦点设到 widget w (并亮焦点环, 视同键盘导航)。w=0 清焦点。
 * 供模态对话框初始焦点落在按钮上 + 方向键移焦点 (msgbox)。 */
UI_API void ui_window_focus_widget(UiWindow win, UiWidget w);

/* Build 66+ (L16): widget 滚轮回调. 任意 widget 类型可挂 — 之前
 * ui_custom_on_mouse_wheel 只对 CustomWidget 生效, 而且 lib 的 OnMouseWheel
 * dispatch loop 写死只识别 TextArea/ImageView/ImageViewPlus/GhImgView/
 * ScrollView 五种, CustomWidget 不在内, mouse_wheel 实际收不到.
 *
 * 这个 API 接 Widget::onMouseWheelHook (跟 .uix @wheel 同一条路径, 在
 * OnMouseWheel 分发开头无条件 fire, 不挑 widget 类型). x/y 是 widget 内
 * 屏幕坐标, delta 正值上滚 / 负值下滚 (跟 WM_MOUSEWHEEL WHEEL_DELTA 同号).
 * cb=NULL 解绑. */
typedef void (*UiWidgetWheelCallback)(UiWidget w, float x, float y,
                                       float delta, void* userdata);
UI_API void ui_widget_on_mouse_wheel(UiWidget w,
                                       UiWidgetWheelCallback cb,
                                       void* userdata);

UI_API void ui_widget_on_click(UiWidget w, UiClickCallback cb, void* userdata);

/* build 287: widget 级右键 (contextmenu) 回调。接 Widget::onContextMenuHook,
 * 跟 .uix 的 @contextmenu 同一条路径 —— 右键抬起时从命中 widget 沿父链向上
 * 冒泡, 第一个挂了 hook 的消费掉。x/y 是窗口 DIP 坐标, 可直接喂 ui_menu_show。
 *
 * 存在理由: `<menu trigger="#foo" event="rclick">` 的 trigger 是在页面初次
 * wire 时按 selector 解析一次的。#foo 若在 v-if 里且当时没挂载, 解析失败后
 * 不会补挂, 那个右键菜单从此永远打不开。列表类界面 (空态用 v-if 换一块提示)
 * 正好撞上。逐行挂这个回调既绕开了 selector 的时序坑, 也顺带解决了"点的是
 * 哪一行"—— 不用再靠 mouse_move 猜。cb=NULL 解绑。 */
typedef void (*UiWidgetContextMenuCallback)(UiWidget w, float x, float y,
                                             void* userdata);
UI_API void ui_widget_on_context_menu(UiWidget w,
                                       UiWidgetContextMenuCallback cb,
                                       void* userdata);

/* ------------------------------------------------------------------ */
/* Drag & drop (build 259+) — HTML5 语义的控件间拖放                    */
/* ------------------------------------------------------------------ */
/* 源侧: set_draggable 后按住超过阈值(4 DIP)即开始拖动, 窗口画拖影;
 * drag_data 是随拖动携带的字符串载荷 (UTF-8), 设置载荷隐含 draggable.
 * 目标侧: set_drop_target 或挂 on_drop 即接受投放; 拖动悬停时 widget 进入
 * CSS `:drag-over` 伪类状态. 对应 .uix 属性 draggable / drag-data /
 * drop-target 和事件 @dragstart/@dragend/@dragenter/@dragover/@dragleave/
 * @drop ($event.data 为载荷; @dragstart 里可改写 $event.data). */
UI_API void ui_widget_set_draggable(UiWidget w, int draggable);
UI_API void ui_widget_set_drag_data(UiWidget w, const wchar_t* data);
UI_API void ui_widget_set_drop_target(UiWidget w, int accept);
/* x/y 为 widget-local DIP. cb=NULL 解绑 (同时清除 on_drop 隐含的可投放态). */
typedef void (*UiDropCallback)(UiWidget w, const wchar_t* data,
                                float x, float y, void* userdata);
UI_API void ui_widget_on_drop(UiWidget w, UiDropCallback cb, void* userdata);
/* dropped: 1=落在有效目标并触发了 drop, 0=取消/落空. cb=NULL 解绑. */
typedef void (*UiDragEndCallback)(UiWidget w, int dropped, void* userdata);
UI_API void ui_widget_on_drag_end(UiWidget w, UiDragEndCallback cb, void* userdata);
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
/* 设置全局品牌色 (覆盖默认 accent).
 * 调用后所有用 accent 的 widget (primary button / nav 选中态 / slider /
 * progress / focus 边等) 立即跟随. hover/press/selected 自动派生.
 * accentText (按钮上的文字色) 按背景亮度自动选黑白.
 * 跨 ui_theme_set_mode 切换深/浅色仍保留 (再调一次深浅切换后还是品牌色).
 * 传 alpha=0 取消覆盖, 回到当前 mode 默认 accent.
 * 调完会自动 InvalidateAllWindows 触发重绘. */
UI_API void    ui_theme_set_accent(UiColor color);
/* 字符串版本: 接受跟 CSS 同款写法 — "#RGB" / "#RRGGBB" / "#RRGGBBAA" /
 * "rgb(r,g,b)" / "rgba(r,g,b,a)" / 命名色 ("red", "transparent" 等).
 * 解析失败返回非 0 (调用方可以 fallback). 传 NULL / "" / "none" 视为
 * "取消覆盖" (等同 ui_theme_set_accent({0,0,0,0})).                    */
UI_API int     ui_theme_set_accent_hex(const char* color);
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
/* Build 64+ (L13): 同时把 widget 推进 owner window 的 focusedWidget_ 槽 — 让
 * WM_KEYDOWN 路由到这里的 keyDown 回调, 触发 widget 的 onFocus/onBlur hook.
 * 调用前请先 ui_custom_set_focusable(w, 1) — 没标 focusable 的 custom widget
 * 鼠标点击不会自动获得焦点 (跟 ButtonWidget 等原生 widget 行为对齐). */
UI_API void ui_custom_set_focused(UiWidget w, int focused);
UI_API int  ui_custom_get_focused(UiWidget w);
/* Build 64+ (L13): opt-in 让 <custom> 进入 lib 的键盘焦点系统. 默认 false (纯
 * 展示型 custom widget 不吃 Tab / 鼠标点击不抢焦点). 接管键盘交互前置位 1. */
UI_API void ui_custom_set_focusable(UiWidget w, int focusable);

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
/* 截图当前打开的 popup menu (右键菜单 / 下拉菜单 / submenu 链最深的那个).
 * 没菜单打开返回 -1. 用来验证 menu icon / 颜色 / 子菜单实际渲染. */
UI_API int      ui_debug_screenshot_menu(UiWindow win, const wchar_t* outPath);
/* 截单个 widget。widget 必须已布局（rect 非空），输出 PNG。 */
UI_API int      ui_debug_screenshot_widget(UiWindow win, UiWidget w, const wchar_t* outPath);

/* ------------------------------------------------------------------ */
/* Debug server — named-pipe IPC（库级，所有应用均可一行启用）          */
/* ------------------------------------------------------------------ */
/* 启动后 \\.\pipe\<pipe_name> 接收文本命令，返回 JSON 响应。命令集合见
 * docs/debug-simulation.md。pipe_name=NULL 用默认 "ui_core_debug".
 *
 * Build 63+ (L12): 同进程可启动多个 server, 每个绑独立 UiWindow + 独立
 * pipe_name (多窗口应用每个窗口可有自己的调试 pipe). 同 pipe_name 重复
 * start 返 -2; 不同 pipe_name 都返 0. */
UI_API int   ui_debug_server_start(UiWindow win, const char* pipe_name);
/* Stop 所有 server 并清掉 userHandler (跟旧 single-server 语义一致).
 * 注意: 不要在 UI 线程内同步调 — worker invoke_sync 回 UI 线程跑 widget
 * 命令时, 主线程 join 会 deadlock. */
UI_API void  ui_debug_server_stop(void);
/* Stop 指定 pipe_name 的一个 server, 其它继续运行. NULL / 找不到 no-op.
 * 多窗口场景下析构子窗口时用 (主窗口的 pipe 不受影响). userHandler 不动. */
UI_API void  ui_debug_server_stop_named(const char* pipe_name);
/* 自定义命令处理器，可覆盖 builtin 或新增私有命令。                     */
/* 返回值：>0 = 写入 out_buf 的字节数（作为 JSON 响应），0 = 不处理（回退 */
/* 到 builtin），<0 = 错误（也回退）。out_buf 容量由 out_cap 给出。     */
typedef int (*UiDebugCommandHandler)(const char* cmd, const char* args,
                                     char* out_buf, int out_cap, void* userdata);
UI_API void  ui_debug_server_set_handler(UiDebugCommandHandler cb, void* userdata);

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
/* 按窗口坐标双击。走真实 WM_LBUTTONDBLCLK 路径 (@dblclick hook +
 * Widget::OnMouseDoubleClick), 位置敏感的双击 (如图片划词选整行) 必须用
 * 这个而不是 ui_debug_double_click (后者只能点 widget 中心)。 */
UI_API int   ui_debug_double_click_at(UiWindow win, float x, float y);
/* 带修饰键的按键注入。widget 的 OnKeyDown 多用 GetKeyState 判 Ctrl/Shift/
 * Alt, 普通 ui_debug_key 只送 vk、修饰键读出来永远是"没按下" —— 组合键
 * (Ctrl+C / Ctrl+A ...) 必须用这个。注入期间临时改线程键盘状态, 调用后还原。*/
UI_API int   ui_debug_key_mod(UiWindow win, int vk, int ctrl, int shift, int alt);
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
/* build 85 (调试用): 沿 path 逐级打开 submenu (不是 click leaf), 让 caller
   可以拍照截图深层 submenu. path 每个 index 都必须指向有 submenu 的 item.
   返 0 成功 (最深 submenu 已 ShowPopup), 负数失败. */
UI_API int   ui_debug_menu_open_submenu_path(UiWindow win, const int* path, int depth);
/* combo: 沿 path 打开 submenu + 截图最深 submenu 的 popup. */
UI_API int   ui_debug_screenshot_submenu_path(UiWindow win, const int* path, int depth,
                                                const wchar_t* outPath);

/* 开关 context menu 的"前台变化即自动关闭"行为。
   自动化脚本（如 PowerShell 发 pipe 命令）持有前台窗口时，需要调用
   ui_debug_set_menu_autoclose(0) 关掉自动关闭；否则菜单打开后 50ms 内就被关掉。
   参数 enabled=0 表示关闭自动关闭，=1 恢复正常。 */
UI_API void  ui_debug_set_menu_autoclose(int enabled);

/* 在 UI 线程上同步执行 fn(ud)。跨线程调用时内部用 SendMessage 到窗口，
   已在 UI 线程时直接调用。返回前 fn 必已执行完成。主要给自动化脚本 / 调试 pipe
   用，保证 widget 访问不跨线程产生数据竞争。 */
UI_API void  ui_window_invoke_sync(UiWindow win, void (*fn)(void* ud), void* ud);

/* ------------------------------------------------------------------ */
/* Frameless canvas mode —— 无边框画布 (since 1.2.0)                  */
/* ------------------------------------------------------------------ */
/* 覆盖窗口最小尺寸（DIP）。传 0 恢复默认（theme::kMinWidth/kMinHeight，480×360）。
   用于无边框画布等场景：窗口随 widget 尺寸动态缩放，需要小于主题默认最小尺寸。 */
UI_API void  ui_window_set_min_size(UiWindow win, int w_dip, int h_dip);

/* 运行时切换窗口边框：
 *   frameless != 0 → 无系统边框 / 标题栏（需自己放 TitleBar）
 *   frameless == 0 → 系统原生标题栏 + 最小化 / 最大化 / 关闭按钮
 * 内部用 SetWindowLongPtr + SWP_FRAMECHANGED 热更新窗口样式。 */
UI_API void  ui_window_set_frameless(UiWindow win, int frameless);
UI_API int   ui_window_is_frameless(UiWindow win);

/* 窗口背景擦除模式：
     0 = 主题背景色填充（默认，普通窗口）
     1 = 透明 / 不擦背景（画布模式：widget 自己画满客户区，
         避免 SetWindowPos 扩大窗口时的背景色闪烁）
   mode=1 要求 widget 覆盖整个客户区，否则会看到前一帧残留像素。 */
UI_API void  ui_window_set_background_mode(UiWindow win, int mode);

/* 标记 widget 为"点击即拖动窗口"：命中该 widget 时 WM_NCHITTEST 返回 HTCAPTION，
   交给系统做窗口拖动。HitTest 返回的是最深层子节点，所以 Panel 里面的 Button
   等控件不会被这个标记波及（除非它们自己也标了 dragWindow）。
   典型用法：无边框画布模式给根 Panel 打上这个标，让整个画布都能拖窗口。 */
UI_API void  ui_widget_set_drag_window(UiWidget w, int enable);

/* ---- 窗口几何 ---- */
/* x / y 是屏幕物理像素 (Win32 惯例); w_dip / h_dip 是 DIP (按当前 DPI 换算).
   getter / setter / UiWindowConfig.x/y 全部统一 — get→set round-trip
   把窗口几何复制到 sub-window 不再错位 (build 94+ L23 修, 之前 getter
   返 DIP 跟 setter 不自洽).

   持久化 DPI-stable 位置 (DPI 变了重启窗口不漂移): caller 用 ui_window_dpi
   自己换算:
       int dpi  = ui_window_dpi(win);
       int xDip = MulDiv(xScreen, 96, dpi);   // 存盘
       int xPx  = MulDiv(xDip,    dpi, 96);   // 读盘 restore

   set_rect / set_size 每次调用都会触发一次同步重绘 (InvalidateRect +
   UpdateWindow), 配合 ui_window_set_background_mode(win, 1) 把扩大窗口
   时背景闪减到最小. */
UI_API void  ui_window_set_rect(UiWindow win, int x_screen, int y_screen,
                                 int w_dip, int h_dip);
UI_API void  ui_window_set_size(UiWindow win, int w_dip, int h_dip);
UI_API void  ui_window_set_position(UiWindow win, int x_screen, int y_screen);
UI_API void  ui_window_get_rect_screen(UiWindow win,
                                        int* out_x, int* out_y,
                                        int* out_w_dip, int* out_h_dip);

/* 返当前窗口 DPI (96 / 120 / 144 / 192...). 用 Win32 GetDpiForWindow,
 * 跟随窗口所在 monitor 的 DPI. 配合 MulDiv 做 screen px ↔ DIP 转换:
 *   int dip      = MulDiv(screen_px, 96, ui_window_dpi(win));
 *   int screen   = MulDiv(dip,       ui_window_dpi(win), 96);
 * 典型场景: 应用想跨 DPI / 跨显示器持久化窗口位置, 存 DIP 重启 restore
 * 时按当前 DPI 转回 screen px, 这样把窗口从 100% DPI 主屏拖到 150% 副屏
 * 再重启, 位置不会偏移. */
UI_API int   ui_window_dpi(UiWindow win);

/* 滚轮缩放"光标不动"原语：resize 到 (w_dip, h_dip)，
   并把新客户区里的 (client_x_dip, client_y_dip) 对齐到屏幕 (screen_x, screen_y)。
   典型调用：滚轮回调中已知鼠标在屏幕 (sx, sy)、在旧客户区(cx, cy)，
   缩放比变为 z'/z 后，新客户区中同一图像点位于 (cx * z'/z, cy * z'/z)，
   传这个点 + 屏幕鼠标坐标，窗口会自动移动使该点保持在鼠标下。
   注意：仅无边框窗口（system_frame=0）结果准确，有系统边框时 client 区
   与窗口左上角之间还隔着边框 + 标题栏，会偏移。 */
UI_API void  ui_window_resize_with_anchor(UiWindow win,
                                           int w_dip, int h_dip,
                                           float client_x_dip, float client_y_dip,
                                           int screen_x, int screen_y);

/* 一键进入 / 退出"无边框画布"模式。开启时等同于：
     - ui_window_set_min_size(win, 32, 32)
     - ui_window_set_background_mode(win, 1)
     - 根 widget.dragWindow = true
     - 如果根树里有 TitleBar，visible=false
   关闭时反之。要求窗口在 ui_window_create 时就用 system_frame=0 创建无边框。 */
UI_API void  ui_window_enable_canvas_mode(UiWindow win, int enable);

/* L57 (build 124+): 锁定窗口 aspect 比例 — 用户拖窗任意一条边 / 角 resize 时,
 * lib 在 WM_SIZING 收到 user 拖出的 RECT 后, 按 ratio 算合法 size 写回, Win32
 * 把这个 RECT 当 user 实际拖的 size 处理. 拖横向 → 按 w 算 h; 拖纵向 → 按 h
 * 算 w; 拖角 → 按拖动幅度大的那边算另一边.
 *
 * 看图器 borderless 模式典型用法: enter 时调 set_aspect_lock(win, img_w, img_h),
 * 切图时更新, exit 时 set_aspect_lock(win, 0, 0) 解锁.
 *
 * ratio_w 或 ratio_h 任一 <= 0 = disable (默认). */
UI_API void  ui_window_set_aspect_lock(UiWindow win, int ratio_w, int ratio_h);

/* ------------------------------------------------------------------ */
/* Font / Text rendering (since 1.3.0)                                 */
/* ------------------------------------------------------------------ */
/* 文字渲染档位。见 docs/c-api.md 对比图。 */
typedef enum UiTextRenderMode {
    UI_TEXT_RENDER_SMOOTH     = 0, /* GRAYSCALE + NATURAL_SYM（默认 / WinUI 风） */
    UI_TEXT_RENDER_CLEARTYPE  = 1, /* CLEARTYPE + NATURAL（Office/Chrome） */
    UI_TEXT_RENDER_SHARP      = 2, /* CLEARTYPE + GDI_CLASSIC（记事本最锐） */
    UI_TEXT_RENDER_GRAY_SHARP = 3, /* GRAYSCALE + GDI_CLASSIC（锐且无彩边） */
    UI_TEXT_RENDER_ALIASED    = 4, /* 无抗锯齿 / 像素字体 */
} UiTextRenderMode;

/* ---- 全局默认（进程级，新建窗口的初值；旧窗口已创建也会跟随，只要它自己没 override）---- */
/* 设置全局默认字体族。NULL = 恢复 "Segoe UI"。
   常用值: L"Microsoft YaHei UI"（中文应用推荐）/ L"Segoe UI" / L"SimSun"。 */
UI_API void             ui_theme_set_default_font(const wchar_t* family);
UI_API const wchar_t*   ui_theme_get_default_font(void);

/* 设置"中英分离"字体：ASCII/拉丁字符用 latin，CJK 字符用 cjk。
   任一参数为 NULL 表示不覆盖该范围（由 default_font 或系统 fallback 处理）。
   两者皆 NULL = 关闭中英分离。
   典型: ui_theme_set_cjk_font(L"Segoe UI", L"Microsoft YaHei UI"). */
UI_API void             ui_theme_set_cjk_font(const wchar_t* latin, const wchar_t* cjk);
UI_API const wchar_t*   ui_theme_get_cjk_latin_font(void);
UI_API const wchar_t*   ui_theme_get_cjk_cjk_font(void);

/* 设置全局默认文字渲染模式。 */
UI_API void             ui_theme_set_text_render_mode(UiTextRenderMode mode);
UI_API UiTextRenderMode ui_theme_get_text_render_mode(void);

/* ---- 窗口级覆盖（优先级高于全局） ---- */
/* NULL / UI_TEXT_RENDER_SMOOTH 不能表达"清除 override"，用 ui_window_clear_font_override 重置。 */
UI_API void             ui_window_set_default_font(UiWindow win, const wchar_t* family);
UI_API void             ui_window_set_cjk_font(UiWindow win, const wchar_t* latin, const wchar_t* cjk);
UI_API void             ui_window_set_text_render_mode(UiWindow win, UiTextRenderMode mode);
UI_API void             ui_window_clear_font_override(UiWindow win);   /* 清除所有 font/mode 覆盖 */

/* ---- Dialog / Toast ---- */

/* ---- HWND 通道：通过 Win32 消息循环派发（异步，需 pump） ---- */
UI_API int   ui_debug_post_click(UiWindow win, float x, float y);
UI_API int   ui_debug_post_right_click(UiWindow win, float x, float y);
UI_API int   ui_debug_post_mouse_move(UiWindow win, float x, float y);
UI_API int   ui_debug_post_key(UiWindow win, int vk);
UI_API int   ui_debug_post_char(UiWindow win, unsigned int ch);

/* 处理所有已排队的窗口消息（使 Post* 生效）。返回已处理的消息数。 */
UI_API int   ui_debug_pump(void);

/* ------------------------------------------------------------------ */
/* Asset resolver — 资源按"名字"加载（HTML <img src> / <link href>）  */
/* ------------------------------------------------------------------ */
/* HTML 里 <img src="logo.png"> / <link href="theme.css"> 这类按名字
   引用的资源，库不假设任何路径或文件系统结构，统一通过这里解析。
   注册的来源按注册顺序串成 chain，先注册先匹配。

   两种典型工作流：
   - dev：注册一个目录，边改边看 → ui_asset_register_dir("assets/")
   - ship：CMake ui_core_embed_binary() 烤进 exe，启动时注册 →
           ui_asset_register_blob("logo.png", k_logo_bytes, k_logo_size)
   两种工作流不需要改一行 HTML。 */

/* 自定义解析回调：name → bytes。返回非 0 表示找到。bytes 必须保活到
   下一次 ui_asset_reset() 或进程结束（库不复制内容）。 */
typedef int (*UiAssetResolver)(const char* name,
                                const void** out_bytes,
                                size_t*       out_size,
                                void*         userdata);

UI_API void ui_asset_register_dir(const char* dir_utf8);
UI_API void ui_asset_register_blob(const char* name, const void* bytes, size_t size);
UI_API void ui_asset_register_resolver(UiAssetResolver fn, void* userdata);
UI_API void ui_asset_reset(void);

/* 让所有 src == name 且**上次解析失败**的 <img> 重新尝试 (自 1.7.0 build 286)。
 *
 * 为什么需要: ImageWidget 是惰性的 —— 第一次真正要画它时才调 resolver 解析,
 * 失败后置一个闩锁不再每帧重试 (否则每帧一次 IO)。这对同步 resolver 没问题,
 * 但**异步** resolver (宿主先返回失败, 数据从后台线程读上来之后才可用) 会被
 * 这个闩锁永久钉死, 除非 src 变化。数据就绪后调本函数打开闩锁, 下一帧自然重试。
 *
 * 配合"惰性解析 + 视口剔除"(build 285) 可以做出真正的按需加载: 落在视口外的
 * 行不绘制 → 不解析 → resolver 不会被问到; 滚到哪里才请求哪里的资源。宿主在
 * resolver 里把未命中的名字入队交后台读, 读完对该名字调一次本函数即可。
 *
 * 只在 UI 线程调。name 没有任何匹配实例时是 no-op。 */
UI_API void ui_asset_invalidate(const char* name);

/* ------------------------------------------------------------------ */
/* Page API — HTML + Vue-like declarative pages                       */
/* ------------------------------------------------------------------ */
typedef uint64_t UiPage;

/* Load a .uix page (SFC: <window> + <template> + <script> + <style>) from
 * file or string. Returns 0 on failure. */
UI_API UiPage   ui_page_load_file(const wchar_t* path);
UI_API UiPage   ui_page_load_string(const char* html_source);
UI_API void     ui_page_destroy(UiPage p);

/* Reload a previously-loaded page from disk (preserves reactive variable values by name). */
UI_API int      ui_page_reload(UiPage p);

/* Root widget handle (usable with ui_window_set_content etc.). Returns 0 on failure. */
UI_API UiWidget ui_page_root(UiPage p);

/* Open a window sized and titled per the page's <window ... /> tag.
 * Any fields not specified in the tag fall back to caller's optional override,
 * then to sensible defaults. Also switches theme if <window theme="..."> was set.
 * The page's root widget is automatically installed as the window content.
 * Returns the new UiWindow handle, or 0 on failure. */
UI_API UiWindow ui_page_open_window(UiPage p, const UiWindowConfig* override_defaults);

/* Same as ui_page_open_window but returns window 处于 hidden 状态, D2D RT 已
 * 预创建. Caller 自己同步预热 (decode + 喂 widget bitmap / 调整 reactive state)
 * 后调 ui_window_show_immediate(win) 一次性出图. 用于 "argv 启动 → 双击文件
 * 关联 → 用户期望瞬间看到图" 场景, 避免 ui_page_open_window 内嵌 ShowWindow
 * 后用户看到"空窗 → 0.3-1s 后图替换"两段式视觉. build 99+ L27. */
UI_API UiWindow ui_page_prepare_window(UiPage p, const UiWindowConfig* override_defaults);

/* Set reactive variables. The page's template re-evaluates any dependent bindings. */
UI_API void     ui_page_set_bool(UiPage p, const char* name, int value);
UI_API void     ui_page_set_int(UiPage p, const char* name, int value);
UI_API void     ui_page_set_float(UiPage p, const char* name, double value);
UI_API void     ui_page_set_text(UiPage p, const char* name, const wchar_t* value);

/* Set a list of strings (for v-for). items is an array of `count` wide strings. */
UI_API void     ui_page_set_text_list(UiPage p, const char* name, const wchar_t** items, int count);

/* JSON I/O — push 任意嵌套对象 / 数组到 reactive state, 或反向读出. 比并行
 * 字符串数组好用得多, 适合 v-for 一条记录多字段的场景:
 *
 *   const char* json = "[{\"id\":1,\"name\":\"a\",\"on\":true}, ...]";
 *   ui_page_set_json(p, "items", json);   // 0=ok, -1=parse error
 *
 *   <div v-for="x in items" :key="x.id">{{ x.name }} {{ x.on }}</div>
 *
 * ui_page_get_json 返回 malloc 出的 UTF-8 JSON, 调用方用 ui_page_free 释放.
 * 不存在或 kind=Function 返回 NULL.                                           */
UI_API int      ui_page_set_json(UiPage p, const char* name, const char* utf8_json);
UI_API char*    ui_page_get_json(UiPage p, const char* name);
UI_API void     ui_page_free    (void* ptr);

/* (Removed in 1.5.0) ui_page_set_handler / ui_page_set_handler_ex —
 * Vue 3 SFC `methods: { onDel(id, on) { ... } }` replaces these. C-side
 * native methods can register on the JS state via the QuickJS API
 * directly when needed. Existing call sites should migrate to either
 * Vue 3 methods{} or expose a free-standing C function via the
 * embedding application. */

/* ---- Internationalization ------------------------------------------ */
/* Install a translation table for `locale`. `keys_values` is a flat array
 * of alternating UTF-8 key and value strings, with `count` being the number
 * of PAIRS (so the array length is count*2).
 * Example: const char* kv[] = { "save", "Save", "cancel", "Cancel" };
 *          ui_page_load_translations(p, "en", kv, 2);                    */
UI_API void     ui_page_load_translations(UiPage p, const char* locale,
                                          const char** keys_values, int count);

/* Load translations from `.lang` file format (UTF-8, one `key=value` per
 * line, # and ; line comments, BOM tolerated).
 *   ui_page_load_language_string — feed content as string (good for embed)
 *   ui_page_load_language_file   — read from disk (returns 0 on open failure)
 *
 * Replace-not-merge: calling either of these (or ui_page_load_translations)
 * twice for the same locale **replaces** the prior table for that locale —
 * keys not in the new content drop. Build the full table per call.
 *
 * Templates: write `<label>@app.title</label>` (compile-time desugar to
 * `$t('app.title')`) or explicit `{{ $t('app.title') }}`. Both auto-update
 * on ui_page_set_locale(). Missing keys fall back to the key itself. */
UI_API void     ui_page_load_language_string(UiPage p, const char* locale,
                                              const char* utf8_content);
UI_API int      ui_page_load_language_file  (UiPage p, const char* locale,
                                              const wchar_t* path);

/* Switch the active locale. Any binding that calls $t() in the template
 * re-evaluates, so labels refresh in place.                              */
UI_API void     ui_page_set_locale(UiPage p, const char* locale);

/* Retrieve a UiMenu handle for a `<menu id="name">` declared in the HTML.
 * Returned menu is owned by the page; do NOT call ui_menu_destroy on it.
 * Use with ui_menu_show + ui_window_on_menu for click-routing.
 *   <menu id="ctx">
 *     <menuitem id="1">Save</menuitem>
 *     <separator/>
 *     <menuitem id="2" shortcut="Ctrl+Q">Quit</menuitem>
 *   </menu>                                                              */
UI_API UiMenu   ui_page_menu(UiPage p, const char* name);

/* Retrieve last error (compile + runtime). Returned pointer is valid until next call. */
UI_API const char* ui_page_last_error(UiPage p);

/* ---- Lifecycle hooks (Vue parity for v-if / v-for) -------------------- */
/* Fires every time a widget with the given HTML id is mounted: initial
 * render, v-if becoming truthy, v-for iteration build. The widget handle
 * passed to the callback is FRESH (a previous handle for the same id will
 * be invalid after unmount). Use this instead of registering callbacks via
 * a one-shot ui_widget_find_by_id when the target lives inside v-if/v-for.
 *
 *   void on_mount(UiPage p, UiWidget w, void* ud) {
 *       ui_widget_on_click(w, on_click_cb, ud);
 *   }
 *   ui_page_on_widget_mount(page, "btn_x", on_mount, NULL);
 *
 * Pass cb=NULL to clear. Multiple registrations on the same id replace the
 * previous one (only one mount + one unmount handler per id).            */
typedef void (*UiWidgetLifecycleCallback)(UiPage page, UiWidget w, void* userdata);
UI_API void ui_page_on_widget_mount(UiPage p, const char* widget_id,
                                     UiWidgetLifecycleCallback cb, void* userdata);
UI_API void ui_page_on_widget_unmount(UiPage p, const char* widget_id,
                                       UiWidgetLifecycleCallback cb, void* userdata);

/* Historical process-wide feature flag. The page system now routes .uix files
 * through the QuickJS runtime (Vue 3 SFC `export default { ... }` syntax +
 * Proxy reactive state) by default; this function is kept for compatibility
 * with older embedders that toggled the rollout explicitly.
 *
 * Also resolved from env var UI_PAGE_QUICKJS=1 if this function is not called
 * before the first ui_page_load_*.
 *
 * Supported .uix features include text and attribute bindings, methods,
 * computed values, @event handlers, v-if, v-show, v-for, v-model, i18n, and
 * the page helper functions exposed on `this` such as $rect/$windowSize/
 * $nextTick.                                                             */
UI_API void ui_page_set_quickjs_enabled(int v);

/* ------------------------------------------------------------------ */
/* Layout API — `.ui` 标记文件加载器（XAML/QML 风格的旧声明式系统）   */
/* HTML 那套 (UiPage) 是 Vue 风格响应式；这套是更简单的"一次性构建    */
/* widget 树 + 显式数据绑定 + 命名 handler"。ui-demo.exe 用的是这个。 */
/* ------------------------------------------------------------------ */
typedef uint64_t UiLayout;

/* 创建空 layout / 销毁 */
UI_API UiLayout       ui_layout_create(void);
UI_API void           ui_layout_destroy(UiLayout layout);

/* 加载 .ui 内容（文件或字符串）。返回 1=成功 / 0=失败，失败原因看 last_error。 */
UI_API int            ui_layout_load_file  (UiLayout layout, const wchar_t* path);
UI_API int            ui_layout_load_string(UiLayout layout, const char* utf8_source);
UI_API const char*    ui_layout_last_error (UiLayout layout);

/* 加载后取根 widget / 按 id 查找。返回 0/UI_INVALID 表示没有。 */
UI_API UiWidget       ui_layout_root        (UiLayout layout);
UI_API UiWidget       ui_layout_find_by_id  (UiLayout layout, const char* id);

/* 一步式：按 .ui 里的 <ui width="..." height="..." title="..."> 提示创建窗口，
   挂根 widget，调 show。等同于 demo/app.cpp 那一坨手写 boilerplate。
   override_defaults 字段非 0/NULL 的会覆盖 .ui 提示。 */
UI_API UiWindow       ui_layout_open_window(UiLayout layout,
                                             const UiWindowConfig* override_defaults);

/* 事件 handler — .ui 里 onClick="onSomething" 等字面量名称在此绑实际函数。
   按签名分 5 组。user 是任意用户上下文指针，会原样回传给 fn。 */
UI_API void           ui_layout_set_handler_void  (UiLayout, const char* name,
                                                    void (*fn)(void* user), void* user);
UI_API void           ui_layout_set_handler_bool  (UiLayout, const char* name,
                                                    void (*fn)(int v, void* user), void* user);
UI_API void           ui_layout_set_handler_int   (UiLayout, const char* name,
                                                    void (*fn)(int v, void* user), void* user);
UI_API void           ui_layout_set_handler_float (UiLayout, const char* name,
                                                    void (*fn)(double v, void* user), void* user);
UI_API void           ui_layout_set_handler_text  (UiLayout, const char* name,
                                                    void (*fn)(const wchar_t* v, void* user), void* user);

/* 数据绑定 push（按 .ui 里 :prop="bindingName" 对应的名称设值）。 */
UI_API void           ui_layout_set_bool (UiLayout, const char* name, int    value);
UI_API void           ui_layout_set_int  (UiLayout, const char* name, int    value);
UI_API void           ui_layout_set_float(UiLayout, const char* name, double value);
UI_API void           ui_layout_set_text (UiLayout, const char* name, const wchar_t* value);

/* i18n. lang 文件 / 字符串是 key=value 格式（UTF-8，每行一条）。
   Apply 把 .ui 里所有 @key 替换为当前语言的字符串。 */
UI_API int            ui_layout_load_language_string(UiLayout, const char* utf8_content);
UI_API int            ui_layout_load_language_file  (UiLayout, const wchar_t* path);
UI_API void           ui_layout_apply_language      (UiLayout);

#ifdef __cplusplus
}
#endif

#endif /* UI_CORE_H */
