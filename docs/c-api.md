# C API 速查

纯 C 接口，适合简单场景或非 C++ 宿主。所有函数通过 `ui_core.h` 引用。

## 生命周期

| 函数 | 说明 |
|------|------|
| `ui_init()` | 初始化（默认浅色） |
| `ui_init_with_theme(mode)` | 指定主题初始化（`UI_THEME_DARK` / `UI_THEME_LIGHT`） |
| `ui_shutdown()` | 释放资源 |
| `ui_run()` | 消息循环，所有窗口关闭后返回 |
| `ui_quit(code)` | 强制退出 |

## 窗口

```c
UiWindowConfig cfg = {0};
cfg.title        = L"标题";
cfg.width        = 800;
cfg.height       = 600;
cfg.system_frame = 0;      // 0=无边框, 1=系统边框
cfg.resizable    = 1;
cfg.accept_files = 1;      // 接受拖放
cfg.x = 0; cfg.y = 0;      // 0=居中
cfg.tool_window  = 0;      // 1=工具窗口
cfg.skip_animation = 0;    // 1=跳过开场动画
cfg.icon_pixels = rgba;    // RGBA 像素数据（32bpp），NULL=默认图标
cfg.icon_width  = 32;
cfg.icon_height = 32;

cfg.owner       = parent;  // 1.6.0: 子窗口附属主窗 (任务栏不独立 / 跟随 minimize)
cfg.start_maximized = 1;   // 1.6.0: 首帧直接最大化 (无 normal→max 1 帧闪)

UiWindow win = ui_window_create(&cfg);
ui_window_set_root(win, root);
ui_window_show(win);
```

### 首帧零闪烁路径（自 1.6.0 build 99）

`ui_page_prepare_window` 在 `ui_page_open_window` **之前**预创建隐藏窗口 + Direct2D
RT，等内容准备好再一次性显示：

```c
UiPage page = ui_page_load_file(L"app.uix");

/* 1) 准备隐藏窗口 (HWND 创建 + RT 预热, 不显示) */
ui_page_prepare_window(page, NULL);

/* 2) 后台加载耗时资源（解码大图、加载语言包等） */
load_heavy_data();

/* 3) 一次性显示 (走 ShowImmediate / Show, 首帧已是终态画面) */
UiWindow win = ui_page_open_window(page, NULL);
```

配合 `cfg.start_maximized = 1`（自 1.6.0 build 105）实现持久化"最大化关 → 最大化
开"零闪：之前要靠 caller 在 Show 后调 `ShowWindow(SW_MAXIMIZE)`，会跟 lib
`SW_HIDE` + layered fade-in 时序撞出 1 帧 normal flash。

| 函数 | 说明 |
|------|------|
| `ui_window_create(cfg)` | 创建 |
| `ui_window_destroy(win)` | 销毁 |
| `ui_window_show(win)` | 显示（含动画） |
| `ui_window_show_immediate(win)` | 显示（无动画） |
| `ui_window_hide(win)` | 隐藏 |
| `ui_window_set_root(win, w)` | 设置根控件 |
| `ui_window_set_title(win, text)` | 改标题 |
| `ui_window_invalidate(win)` | 重绘 |
| `ui_window_hwnd(win)` | 获取原生 HWND |

### 窗口回调

```c
ui_window_on_close(win, cb, ud);       // void cb(UiWindow, void*)
ui_window_on_resize(win, cb, ud);      // void cb(UiWindow, int w, int h, void*)
ui_window_on_drop(win, cb, ud);        // void cb(UiWindow, const wchar_t* path, void*)
ui_window_on_key(win, cb, ud);         // void cb(UiWindow, int vk, void*)
ui_window_on_right_click(win, cb, ud); // void cb(UiWindow, float x, float y, void*)
ui_window_on_menu(win, cb, ud);        // void cb(UiWindow, int item_id, void*)
```

## 创建控件

| 函数 | 说明 |
|------|------|
| `ui_vbox()` / `ui_hbox()` | 垂直/水平布局 |
| `ui_spacer(size)` | 间距（0=弹性） |
| `ui_panel(color)` / `ui_panel_themed(id)` | 面板容器 |
| `ui_label(text)` | 文本 |
| `ui_button(text)` | 按钮 |
| `ui_checkbox(text)` | 复选框 |
| `ui_toggle(text)` | 开关 |
| `ui_radio_button(text, group)` | 单选 |
| `ui_slider(min, max, value)` | 滑块 |
| `ui_progress_bar(min, max, value)` | 进度条 |
| `ui_text_input(placeholder)` | 单行输入 |
| `ui_text_area(placeholder)` | 多行输入 |
| `ui_combobox(items, count)` | 下拉框 |
| `ui_tab_control()` | 选项卡 |
| `ui_scroll_view()` | 滚动容器 |
| `ui_icon_button(svg, ghost)` | SVG 图标按钮 |
| `ui_titlebar(title)` | 标题栏 |
| `ui_image_view()` | 图片画布 |
| `ui_separator()` / `ui_vseparator()` | 分隔线 |
| `ui_dialog()` | 模态对话框 |
| `ui_custom()` | 自定义绘制 |
| `ui_menu_create()` | 右键菜单 |

## 通用属性

```c
ui_widget_set_id(w, "my_id");
ui_widget_set_width(w, 120);
ui_widget_set_height(w, 36);
ui_widget_set_size(w, 120, 36);
ui_widget_set_expand(w, 1);
ui_widget_set_padding(w, left, top, right, bottom);
ui_widget_set_padding_uniform(w, 16);
ui_widget_set_gap(w, 8);
ui_widget_set_visible(w, 1);
ui_widget_set_enabled(w, 1);
ui_widget_set_bg_color(w, color);
ui_widget_set_tooltip(w, L"提示");
ui_widget_set_rect(w, rect);

UiRect r = ui_widget_get_rect(w);
int vis = ui_widget_get_visible(w);
int en = ui_widget_get_enabled(w);
```

## 树操作

```c
ui_widget_add_child(parent, child);
ui_widget_remove_child(parent, child);
ui_widget_destroy(w);
UiWidget found = ui_widget_find_by_id(root, "id");
```

## 控件操作

```c
// Label
ui_label_set_text(w, L"文本");
ui_label_set_font_size(w, 16);
ui_label_set_bold(w, 1);
ui_label_set_text_color(w, color);
ui_label_set_align(w, 0);      // 0=左, 1=右, 2=中
ui_label_set_wrap(w, 1);
ui_label_set_max_lines(w, 3);

// Button
ui_button_set_font_size(w, 14);
ui_button_set_type(w, 1);      // 0=default, 1=primary(accent)
ui_button_set_bg_color(w, color);    // 自定义背景，hover/press 自动加深
ui_button_set_text_color(w, color);  // 自定义文字色

// CheckBox
ui_checkbox_get_checked(w);     // → int
ui_checkbox_set_checked(w, 1);

// Toggle
ui_toggle_get_on(w);            // → int
ui_toggle_set_on(w, 1);

// RadioButton
ui_radio_get_selected(w);       // → int
ui_radio_set_selected(w, 1);

// Slider
ui_slider_get_value(w);         // → float
ui_slider_set_value(w, 75.0f);

// ProgressBar
ui_progress_get_value(w);       // → float
ui_progress_set_value(w, 50.0f);
ui_progress_set_indeterminate(w, 1);

// TextInput
ui_text_input_get_text(w);      // → const wchar_t*（内部指针）
ui_text_input_set_text(w, L"text");

// TextArea
ui_text_area_get_text(w);
ui_text_area_set_text(w, L"line1\nline2");

// ComboBox
ui_combobox_get_selected(w);    // → int
ui_combobox_set_selected(w, 1);

// TabControl
ui_tab_add(tabs, L"Title", content);
ui_tab_get_active(tabs);
ui_tab_set_active(tabs, 0);

// ScrollView
ui_scroll_set_content(sv, content);

// TitleBar
ui_titlebar_set_title(tb, L"Title");
ui_titlebar_show_buttons(tb, min, max, close);
ui_titlebar_show_icon(tb, show);
ui_titlebar_add_widget(tb, widget);

// IconButton
ui_icon_button_set_svg(w, svg);
ui_icon_button_set_ghost(w, 1);
ui_icon_button_set_icon_color(w, color);
ui_icon_button_set_icon_padding(w, 8);

// Dialog（窗口级 modal overlay，不进 widget 树；详见 controls.md）
ui_dialog_set_ok_text(dlg, L"确定");
ui_dialog_set_cancel_text(dlg, L"取消");
ui_dialog_set_show_cancel(dlg, 1);
ui_dialog_set_theme_mode(dlg, 0);   // 0=auto / 1=light / 2=dark
ui_dialog_show(dlg, win, L"标题", L"消息", on_result, userdata);
ui_dialog_hide(dlg, win);
```

## 回调

```c
// 点击
void on_click(UiWidget w, void* ud) { }
ui_widget_on_click(btn, on_click, data);

// CheckBox / Toggle 值变化
void on_value(UiWidget w, int value, void* ud) { }
ui_checkbox_on_changed(cb, on_value, data);
ui_toggle_on_changed(tg, on_value, data);

// Slider 值变化
void on_float(UiWidget w, float value, void* ud) { }
ui_slider_on_changed(sl, on_float, data);

// ComboBox 选择变化
void on_select(UiWidget w, int index, void* ud) { }
ui_combobox_on_changed(combo, on_select, data);
```

### Widget 级事件回调（自 1.6.0 builds 57-66）

任意 widget（含自绘 `<custom>`）都能挂的通用事件回调。之前只有 button / input
这种预制控件有 onclick，其它 widget 拿不到鼠标 / 焦点事件——1.6.0 全面开放：

```c
/* 鼠标位置 / 滚轮 / 离开 (widget-local 坐标) */
ui_widget_on_mouse_move (w, on_move,  ud);   // void cb(UiWidget, float x, float y, void*)
ui_widget_on_mouse_leave(w, on_leave, ud);   // void cb(UiWidget, void*)
ui_widget_on_mouse_wheel(w, on_wheel, ud);   // void cb(UiWidget, float delta, void*)

/* 焦点 (配合 ui_custom_set_focusable 让自绘 widget 进键盘焦点系统) */
ui_custom_set_focusable(custom, 1);
ui_widget_on_focus(w, on_focus, ud);         // void cb(UiWidget, void*)
ui_widget_on_blur (w, on_blur,  ud);

/* 程序化光标 */
ui_widget_set_cursor(w, "hand");             // hand / wait / size-ns / size-we / IDC_*
```

`ui_icon_button` 也加了 ghost hover 视觉开关（自 build 57）：

```c
UiWidget btn = ui_icon_button(svg_str, /*ghost*/ 1);
ui_icon_button_set_hover_visual(btn, 0);     // 关闭 hover 高亮
```

## 主题

```c
ui_theme_set_mode(UI_THEME_DARK);
ui_theme_set_mode(UI_THEME_LIGHT);
UiThemeMode mode = ui_theme_get_mode();

UiColor bg      = ui_theme_bg();
UiColor content = ui_theme_content_bg();
UiColor sidebar = ui_theme_sidebar_bg();
UiColor toolbar = ui_theme_toolbar_bg();
UiColor accent  = ui_theme_accent();
UiColor text    = ui_theme_text();
UiColor divider = ui_theme_divider();
```

### 自定义品牌色（自 build 28）

```c
/* hex / rgb() / 命名色字符串 (推荐, 自 build 29) */
ui_theme_set_accent_hex("#2563EB");           /* 6 位 hex */
ui_theme_set_accent_hex("#f80");              /* 短格式 → #FF8800 */
ui_theme_set_accent_hex("rgb(37,99,235)");
ui_theme_set_accent_hex("red");               /* 命名色 (22 个常用) */
ui_theme_set_accent_hex(NULL);                /* 取消覆盖 */
/* 返回 0=成功, -1=解析失败 (state 不动) */

/* 直接传 UiColor (低层接口) */
UiColor green = {0.13f, 0.52f, 0.32f, 1.0f};   /* #218554 */
ui_theme_set_accent(green);

/* 跨 set_mode(DARK/LIGHT) 切换深浅色后, 品牌色仍保留. */
ui_theme_set_mode(UI_THEME_DARK);              /* accent 还是绿色 */

/* 取消覆盖, 回当前 mode 默认 accent (Win11 蓝). */
ui_theme_set_accent((UiColor){0, 0, 0, 0});    /* alpha=0 */
```

primary button / slider / progress / focus 下划线 / nav 选中 等所有用
accent 的 widget 自动跟随。hover/press/selected/text 自动派生。

派生规则：
- `accentHover` = base 各通道 +0.08（轻提亮）
- `accentPress` = base 各通道 -0.12（压暗）
- `accentText` = base luminance > 0.6 → 黑字，否则白字（WCAG 简化）
- `accentSelected` = base

## 调试与事件模拟

### Inspector

```c
// 导出控件树 JSON
char* json = ui_debug_dump_tree(win);    // 完整控件树
char* info = ui_debug_dump_widget(w);    // 单个控件
ui_debug_free(json);                     // 必须释放

// 控件高亮（红框 + 黄色内框，定位控件位置）
ui_debug_highlight(win, "my_widget_id"); // 高亮指定 ID 控件
ui_debug_highlight(win, NULL);           // 清除高亮

// 截图（保存当前窗口画面为 PNG）
ui_debug_screenshot(win, L"screenshot.png");                  // 成功返回 0
ui_debug_screenshot_widget(win, w, L"widget.png");            // 单个 widget 区域
ui_debug_screenshot_menu(win, L"menu.png");                   // 弹出的菜单 popup（自 build 27）
                                                              // 没菜单打开返回 -1
```

### 事件模拟（自 1.1.0）

完整的事件注入 API，用于自动化测试 / AI 代理 / 脚本回归。**详见
[`docs/debug-simulation.md`](./debug-simulation.md)**。

```c
// 鼠标 / 键盘
ui_debug_click(win, btn);                         // 完整 MouseDown+Up，触发 onClick
ui_debug_right_click_at(win, 300, 200);           // 右键弹菜单
ui_debug_double_click_at(win, 150, 178);          // 按坐标双击，走真实 WM_LBUTTONDBLCLK
                                                  // （连点两次 click 进不了双击分支）
ui_debug_focus(win, inputBox);
ui_debug_type_text(win, L"hello");                // 逐字符输入
ui_debug_key_mod(win, 'C', 1, 0, 0);              // Ctrl+C；组合键必须用这个，
                                                  // ui_debug_key 送不出修饰键状态

// 控件高层操作
ui_debug_checkbox_set(win, cb, 1);                // 勾选 + 触发 onValueChanged
ui_debug_combo_select(win, combo, 2);             // 选中第 3 项
ui_debug_slider_set(win, slider, 0.75f);

// Context menu（含子菜单路径）
int path[] = {2, 0};                              // "Paste Special" -> "Paste as Plain"
ui_debug_menu_click_path(win, path, 2);

// HWND 通道（走 Win32 消息循环）
ui_debug_post_click(win, 100, 100);
ui_debug_pump();                                  // 处理消息队列

// 线程安全：工作线程里访问 widget 前先 marshal 到 UI 线程
ui_window_invoke_sync(win, my_fn, userdata);
```

共 60+ 个 `ui_debug_*` 函数。demo 还内置了 `\\.\pipe\ui_core_debug` 命名管道，
用 PowerShell / Python 即可驱动；内部 smoke 脚本不随公开仓发布。

### 每窗口独立调试管道（自 1.6.0 build 63）

`ui_debug_server_start` 现在支持每窗口独立 pipe，多 server 并存——主窗 + 设置窗
可以同时跑各自的调试 server，互不抢占：

```c
ui_debug_server_start(main_win,     L"my_app_main_debug");
ui_debug_server_start(settings_win, L"my_app_settings_debug");
/* 传 NULL 用默认 \\.\pipe\ui_core_debug (多应用共用, 跨进程会撞, 不推荐) */
```

### Toast 动画风格（自 1.6.0 build 106）

```c
enum {
    UI_TOAST_ANIM_SLIDE = 0,   /* 默认: top/bottom 滑入, center 直现, 退出淡出 */
    UI_TOAST_ANIM_FADE  = 1,   /* 纯渐入渐出: alpha 0→1→1→0, y 全程钉在目标位置 */
};

ui_toast    (win, L"Saved", 2000);                                  /* 3 参不变 */
ui_toast_ex (win, L"Done",  2000, /*pos*/1, /*icon*/0,
             UI_TOAST_ANIM_FADE);                                   /* 适合 center toast */
```

**BREAKING (自 build 106)**：旧 `ui_toast_at` 删除，旧 `ui_toast_ex` 加 `anim` 参数。
caller 旧调用末位补 `UI_TOAST_ANIM_SLIDE` 即可。

## .uix 页面 i18n（自 1.4.0 build 20）

`.uix` 页面的 i18n 后端在 PageState 里就是 `$t(key)` 函数 + `$locale` 响应式
变量。模板里两种写法等价：

```vue
<label>@app.title</label>                       <!-- 语法糖（推荐） -->
<label>{{ $t('app.title') }}</label>            <!-- 等价显式 -->
:title="$t('window.title')"                     <!-- 属性绑定 -->
```

加载语言包：

```c
/* 1) .lang 文件格式：UTF-8、key=value、# / ; 注释、BOM/CRLF 容忍 */
ui_page_load_language_file (page, "zh", L"app/lang/zh.lang");
ui_page_load_language_file (page, "en", L"app/lang/en.lang");

/* 2) 字符串内容（配合 ui_core_embed_text 单文件分发） */
ui_page_load_language_string(page, "zh", k_zh_lang);

/* 3) 直接传 key/value 数组 */
const char* kv[] = { "btn.save", "保存", "btn.cancel", "取消" };
ui_page_load_translations(page, "zh", kv, 2);

/* 切换 → 所有 $t() 调用自动 re-eval（响应式） */
ui_page_set_locale(page, "en");
```

**Replace-not-merge**：同 locale 二次调用任何 load* 都会**替换**前一次表，
没在新表里的 key 会丢。要做"基础包 + 增量补丁"自己合并 key 后再调用。

**Missing key fallback**：`$t("nope")` 返回 `"nope"`（key 本身），不是 null。

## 资源解析器（自 1.4.0 build 19）

`.uix` 模板里 `<img src="logo.png">` / `<link rel="stylesheet" href="theme.css">`
按"名字"引用资源。库不假设任何路径，统一通过这里解析。注册顺序就是匹配
优先级，先注册先匹配。

```c
typedef int (*UiAssetResolver)(const char* name,
                                const void** out_bytes,
                                size_t*       out_size,
                                void*         userdata);

UI_API void ui_asset_register_dir     (const char* dir_utf8);
UI_API void ui_asset_register_blob    (const char* name,
                                        const void* bytes, size_t size);
UI_API void ui_asset_register_resolver(UiAssetResolver fn, void* userdata);
UI_API void ui_asset_reset            (void);
```

### dev 工作流（边改边看）

```c
ui_init();
ui_asset_register_dir("E:/myapp/assets");
/* 模板里 <img src="logo.png"> → 读 E:/myapp/assets/logo.png
   模板里 <link href="theme.css"> → 读 E:/myapp/assets/theme.css */
UiPage p = ui_page_load_file(L"app.uix");
ui_run();
```

### ship 工作流（CMake 烤进 exe，单文件分发）

CMakeLists.txt：

```cmake
include(${UI_CORE_DIR}/cmake/UiCoreHelpers.cmake)

# 文本 → char 数组（带 0x00 终结符），适合 .css / .uix / .lang
ui_core_embed_text  (my_app FILE assets/theme.css OUT theme_css.embed.h VAR k_theme_css)

# 二进制 → unsigned char 数组（无终结符），适合 .png / .jpg / .ico
ui_core_embed_binary(my_app FILE assets/logo.png  OUT logo_png.embed.h  VAR k_logo_png)
```

main.cpp：

```c
#include "logo_png.embed.h"
#include "theme_css.embed.h"

int WinMain(...) {
    ui_init();
    ui_asset_register_blob("logo.png",  k_logo_png,  k_logo_png_size);
    ui_asset_register_blob("theme.css", k_theme_css, k_theme_css_size);
    UiPage p = ui_page_load_file(L"app.uix");   // .uix 不变
    ui_run();
}
```

dev 和 ship **用同一份 `.uix`**。dev 注册 dir、ship 注册 blob，模板里
`src` / `href` 都是同一个名字。

### 自定义解析（远程下载、ZIP 包等）

```c
static int my_resolver(const char* name,
                        const void** bytes, size_t* size, void* ud) {
    /* 找到就填 *bytes / *size 返回 1，找不到返回 0 */
    /* bytes 必须保活到 ui_asset_reset() 或进程结束 */
    return 0;
}

ui_asset_register_resolver(my_resolver, my_userdata);
```

### Cascade 顺序

`<link rel="stylesheet">` 在 cascade 里**早于**内联 `<style>`，所以同一选择器
两边都设了的话，inline `<style>` 胜出（跟浏览器一致）。

## 无边框画布模式（自 1.2.0）

用于"整个窗口就是一张画布 + 滚轮缩放 + 按住拖动窗口"类场景：

```c
/* 1. 创建无边框窗口 */
UiWindowConfig cfg = {0};
cfg.system_frame = 0;
cfg.resizable    = 1;
cfg.width = 512; cfg.height = 512;
cfg.title = L"Canvas";
UiWindow win = ui_window_create(&cfg);

/* 2. 根 widget 用 ImageView（或你自己的 CustomWidget） */
UiWidget canvas = ui_image_view();
ui_image_load_file(canvas, win, L"photo.png");
ui_window_set_root(win, canvas);

/* 3. 一键进入画布模式（=设 min_size=32、bg_mode=1、根.dragWindow=true、
      隐藏 TitleBar 如有） */
ui_window_enable_canvas_mode(win, 1);
ui_window_show(win);

/* 4. 滚轮回调里"光标不动"缩放：
      用 ui_window_resize_with_anchor 同时改窗口尺寸 + 移动位置，
      使客户区里的 (new_cx, new_cy) 恰好落在屏幕 (screen_x, screen_y)。 */
void on_wheel(UiWindow win, float wheel_x, float wheel_y, float delta) {
    float zoom = ui_image_get_zoom(canvas);
    float ratio = delta > 0 ? 1.1f : 1.0f / 1.1f;
    float new_zoom = zoom * ratio;
    ui_image_set_zoom(canvas, new_zoom);

    /* 获取新窗口尺寸 = 图片尺寸 × 新 zoom */
    int img_w = ui_image_width(canvas);
    int img_h = ui_image_height(canvas);
    int new_w = (int)(img_w * new_zoom);
    int new_h = (int)(img_h * new_zoom);

    /* 鼠标在客户区 (wheel_x, wheel_y)；在屏幕 (sx, sy)；
       缩放后同一图像点位于 (wheel_x * ratio, wheel_y * ratio)。
       让该点钉在屏幕 (sx, sy) 不动： */
    int sx, sy; /* 自行通过 GetCursorPos 或回调拿到屏幕坐标 */
    ui_window_resize_with_anchor(win, new_w, new_h,
                                  wheel_x * ratio, wheel_y * ratio,
                                  sx, sy);
}
```

关键 API：

| 函数 | 用途 |
|------|------|
| `ui_window_enable_canvas_mode(win, 1)` | 一键进入画布模式 |
| `ui_window_set_min_size(win, w, h)` | 覆盖主题默认最小尺寸（480×360） |
| `ui_window_set_background_mode(win, 1)` | 透明 Clear，避免扩窗口时背景闪 |
| `ui_widget_set_drag_window(w, 1)` | 命中即拖窗 |
| `ui_window_set_rect(win, x, y, w, h)` | 原子 SetWindowPos + 同步重绘（x/y = screen px, w/h = DIP） |
| `ui_window_set_position(win, x, y)` | 只移动，不改尺寸（x/y = screen px） |
| `ui_window_resize_with_anchor(...)` | 滚轮缩放时光标锚点不动 |
| `ui_window_get_rect_screen(win, ...)` | 读窗口几何（x/y = screen px, w/h = DIP）。build 94+ L23 跟 setter 统一 |
| `ui_window_dpi(win)` | 当前窗口 DPI (96/120/144/...)。配合 `MulDiv` 做 screen px ↔ DIP 转换 |
| `ui_window_dpi_scale(win)` | 1.6.0 build 101 加。返 `dpi / 96.0f`，DIP ↔ screen px 浮点转换更直观 |

**BREAKING (1.6.0 build 94)**：窗口几何 API x/y 全统一成 screen px（之前 setter
是 screen px, getter 返 DIP 不对称）。旧版本对一对 DIP/screen 没意识到的 caller
升级 1.6.0 后位置会偏，按上面 dpi_scale 帮助函数转换一次即可。
