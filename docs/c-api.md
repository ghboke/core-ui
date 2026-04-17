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

UiWindow win = ui_window_create(&cfg);
ui_window_set_root(win, root);
ui_window_show(win);
```

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

## 调试

```c
// 导出控件树 JSON
char* json = ui_debug_dump_tree(win);    // 完整控件树
char* info = ui_debug_dump_widget(w);    // 单个控件
ui_debug_free(json);                     // 必须释放

// 控件高亮（红框 + 黄色内框，定位控件位置）
ui_debug_highlight(win, "my_widget_id"); // 高亮指定 ID 控件
ui_debug_highlight(win, NULL);           // 清除高亮

// 截图（保存当前窗口画面为 PNG）
ui_debug_screenshot(win, L"screenshot.png");  // 成功返回 0
```
