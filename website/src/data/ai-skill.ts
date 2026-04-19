export const AI_SKILL_CONTENT = `# Core UI — AI Coding Skill

> Windows 桌面 UI 框架。Direct2D 硬件加速，Fluent 2 设计系统，纯 C API + 声明式 .ui 标记。
> 本文件供 AI 编程助手（Claude / GPT / Copilot）使用，包含完整的 API 参考和使用模式。

---

## 1. 架构概览

- **渲染**: Direct2D + Direct3D 11，GPU 合成，60fps
- **API**: 纯 C 导出（ui_core.h），所有对象为 \`uint64_t\` 不透明句柄
- **UI 描述**: 两种方式 — C API 手动构建 或 .ui XML 标记文件
- **主题**: 深色/浅色自动切换，Fluent 2 语义 Token
- **DPI**: Per-Monitor DPI V2，多显示器自动缩放
- **分发**: 单 DLL (core-ui.dll) 或静态链接
- **自动化 / 调试**: 约 60 个 \`ui_debug_*\` 事件注入 API（since 1.1.0），AI 可自行完成
  "生成 → 运行 → 点击 → 截图 → 验证 → 修改"闭环（详见 §14）

## 2. 项目结构

\`\`\`
my_app/
  main.cpp          — 入口
  app.ui            — 界面布局（声明式）
  lang/zh-CN.lang   — 语言包（可选）
  ui_core.h         — 头文件
  core-ui.dll       — 运行时库
  core-ui.lib       — 导入库
\`\`\`

CMake 集成:
\`\`\`cmake
add_executable(my_app WIN32 main.cpp)
target_include_directories(my_app PRIVATE third_party/core-ui/include)
target_link_libraries(my_app PRIVATE \${CMAKE_SOURCE_DIR}/third_party/core-ui/core-ui.lib)
add_custom_command(TARGET my_app POST_BUILD
    COMMAND \${CMAKE_COMMAND} -E copy_if_different
        \${CMAKE_SOURCE_DIR}/third_party/core-ui/core-ui.dll $<TARGET_FILE_DIR:my_app>)
\`\`\`

## 3. 生命周期

\`\`\`c
ui_init();                          // 初始化（默认深色主题）
ui_init_with_theme(UI_THEME_LIGHT); // 或指定主题

UiWindowConfig cfg = {0};
cfg.title = L"My App";
cfg.width = 800; cfg.height = 600;
cfg.resizable = 1;
UiWindow win = ui_window_create(&cfg);

UiWidget root = ui_vbox();
// ... 构建控件树 ...
ui_window_set_root(win, root);
ui_window_show(win);

int ret = ui_run();   // 阻塞消息循环，所有窗口关闭后返回
ui_shutdown();
\`\`\`

## 4. 句柄系统

所有对象都是 \`uint64_t\`:
\`\`\`c
typedef uint64_t UiWidget;
typedef uint64_t UiWindow;
typedef uint64_t UiMenu;
#define UI_INVALID 0   // 无效句柄，创建失败时返回
\`\`\`

基本类型:
\`\`\`c
typedef struct UiColor { float r, g, b, a; } UiColor;
typedef struct UiRect  { float left, top, right, bottom; } UiRect;
\`\`\`

## 5. 窗口 API

### 创建
\`\`\`c
UiWindowConfig cfg = {0};
cfg.title        = L"标题";     // wchar_t*
cfg.width        = 800;
cfg.height       = 600;
cfg.system_frame = 0;           // 0=无边框自定义标题栏, 1=系统边框
cfg.resizable    = 1;
cfg.accept_files = 1;           // 接受文件拖放
cfg.x = 0; cfg.y = 0;          // 0=屏幕居中
cfg.tool_window  = 0;           // 1=工具窗口（不在任务栏显示）
cfg.skip_animation = 0;         // 1=跳过开场动画
cfg.icon_pixels = rgba_data;    // RGBA 32bpp 像素，NULL=默认
cfg.icon_width  = 32;
cfg.icon_height = 32;
UiWindow win = ui_window_create(&cfg);
\`\`\`

### 窗口操作
| 函数 | 说明 |
|------|------|
| \`ui_window_show(win)\` | 显示（含动画） |
| \`ui_window_show_immediate(win)\` | 显示（无动画） |
| \`ui_window_hide(win)\` | 隐藏 |
| \`ui_window_destroy(win)\` | 销毁 |
| \`ui_window_set_root(win, widget)\` | 设置根控件 |
| \`ui_window_set_title(win, L"...")\` | 改标题 |
| \`ui_window_invalidate(win)\` | 请求重绘 |
| \`ui_window_hwnd(win)\` | 获取原生 HWND |

### 窗口回调
\`\`\`c
ui_window_on_close(win, cb, ud);        // void cb(UiWindow, void*)
ui_window_on_resize(win, cb, ud);       // void cb(UiWindow, int w, int h, void*)
ui_window_on_drop(win, cb, ud);         // void cb(UiWindow, const wchar_t* path, void*)
ui_window_on_key(win, cb, ud);          // void cb(UiWindow, int vk_code, void*)
ui_window_on_right_click(win, cb, ud);  // void cb(UiWindow, float x, float y, void*)
ui_window_on_menu(win, cb, ud);         // void cb(UiWindow, int item_id, void*)
\`\`\`

## 6. 控件创建

### 有 C API factory 的控件（可纯 C 代码创建）

| 函数 | 说明 |
|------|------|
| \`ui_vbox()\` / \`ui_hbox()\` | 垂直/水平布局容器 |
| \`ui_spacer(size)\` | 间距（0=弹性填充） |
| \`ui_panel(color)\` | 带背景色的面板 |
| \`ui_panel_themed(id)\` | 主题面板（0=sidebar 1=toolbar 2=content） |
| \`ui_label(L"text")\` | 文本标签 |
| \`ui_button(L"text")\` | 按钮 |
| \`ui_checkbox(L"text")\` | 复选框 |
| \`ui_toggle(L"text")\` | 开关 |
| \`ui_radio_button(L"text", "group")\` | 单选按钮 |
| \`ui_slider(min, max, value)\` | 滑块 |
| \`ui_progress_bar(min, max, value)\` | 进度条 |
| \`ui_text_input(L"placeholder")\` | 单行输入 |
| \`ui_text_area(L"placeholder")\` | 多行输入 |
| \`ui_combobox(items, count)\` | 下拉选择 |
| \`ui_tab_control()\` | 选项卡 |
| \`ui_scroll_view()\` | 滚动容器 |
| \`ui_separator()\` / \`ui_vseparator()\` | 水平/垂直分隔线 |
| \`ui_icon_button(svg, ghost)\` | SVG 图标按钮 |
| \`ui_titlebar(L"title")\` | 自定义标题栏 |
| \`ui_image_view()\` | 图片查看器 |
| \`ui_dialog()\` | 模态对话框 |
| \`ui_custom()\` | 自定义绘制控件 |
| \`ui_menu_create()\` | 右键菜单 |

### 仅通过 .ui 标记创建（**没有** C API factory）

以下控件**只能**通过 \`.ui\` 文件声明创建，用 \`ui_widget_find_by_id(root, "id")\` 或
\`g_layout.FindAs<ui::XxxWidget>("id")\` 获取句柄；状态变更用 \`ui_debug_*\`（自动化场景）
或 C++ 方法（应用代码场景）：

| 控件 | markup 标签 | 状态 API |
|------|------------|---------|
| Grid | \`<Grid cols colGap rowGap>...</Grid>\` | — |
| Stack | \`<Stack id active>...</Stack>\` | C++ \`SetActiveIndex\` |
| NumberBox | \`<NumberBox min max value step decimals />\` | \`ui_debug_number_set(win, w, v)\` / C++ \`SetValue\` |
| Expander | \`<Expander header expanded>...</Expander>\` | \`ui_debug_expander_set(w, 1)\` / \`SetExpanded\` |
| Flyout | \`<Flyout id placement>...</Flyout>\` | \`ui_debug_flyout_show(fw, anchor)\` / \`Show/Hide\` |
| SplitView | \`<SplitView mode open>Pane Content</SplitView>\` | \`ui_debug_splitview_set(w, 1)\` / \`SetPaneOpen\` |
| NavItem | \`<NavItem text svg selected onClick />\`（SplitView Pane 里用） | \`SetSelected\` |
| Splitter | \`<Splitter ratio vertical>L R</Splitter>\` | C++ \`SetRatio\` |
| MenuBar | \`<MenuBar><Menu text>...</Menu></MenuBar>\` | 事件通过 \`onMenuItemClick\` |
| ToolTip | 不是独立控件，用任意控件的 \`tooltip="..."\` 属性 | — |

markup-only 的原因：这些控件的配置参数太多（NumberBox 有 min/max/step/decimals，SplitView
有 4 种模式），用 C API 写一堆 setter 反而比 XML 繁琐；用 markup 声明 + 事件回调才是
推荐模式。

## 7. 控件树与通用属性

### 树操作
\`\`\`c
ui_widget_add_child(parent, child);
ui_widget_remove_child(parent, child);
ui_widget_destroy(widget);
UiWidget w = ui_widget_find_by_id(root, "my_id");
\`\`\`

### 通用属性
\`\`\`c
ui_widget_set_id(w, "my_id");
ui_widget_set_width(w, 120);
ui_widget_set_height(w, 36);
ui_widget_set_size(w, 120, 36);
ui_widget_set_expand(w, 1);              // 弹性填充
ui_widget_set_padding(w, l, t, r, b);    // 四边内边距
ui_widget_set_padding_uniform(w, 16);    // 统一内边距
ui_widget_set_gap(w, 8);                 // 子控件间距
ui_widget_set_visible(w, 1);
ui_widget_set_enabled(w, 1);
ui_widget_set_bg_color(w, color);
ui_widget_set_tooltip(w, L"提示");

UiRect r = ui_widget_get_rect(w);
int vis  = ui_widget_get_visible(w);
int en   = ui_widget_get_enabled(w);
\`\`\`

## 8. 控件操作速查

### Label
\`\`\`c
ui_label_set_text(w, L"文本");
ui_label_set_font_size(w, 16);
ui_label_set_bold(w, 1);
ui_label_set_text_color(w, color);
ui_label_set_align(w, 0);       // 0=左 1=右 2=中
ui_label_set_wrap(w, 1);
ui_label_set_max_lines(w, 3);   // 0=不限
\`\`\`

### Button
\`\`\`c
ui_button_set_type(w, 1);       // 0=default 1=primary(accent)
ui_button_set_bg_color(w, c);   // 自定义色，hover/press 自动加深
ui_button_set_text_color(w, c);
ui_button_set_font_size(w, 14);
\`\`\`

### CheckBox / Toggle / RadioButton
\`\`\`c
ui_checkbox_get_checked(w);  ui_checkbox_set_checked(w, 1);
ui_toggle_get_on(w);         ui_toggle_set_on(w, 1);
ui_radio_get_selected(w);    ui_radio_set_selected(w, 1);
\`\`\`

### Slider / ProgressBar
\`\`\`c
ui_slider_get_value(w);      ui_slider_set_value(w, 75.0f);
ui_progress_get_value(w);    ui_progress_set_value(w, 50.0f);
\`\`\`

### TextInput / TextArea
\`\`\`c
const wchar_t* t = ui_text_input_get_text(w);  // 内部指针，不要 free
ui_text_input_set_text(w, L"text");
ui_text_input_set_read_only(w, 1);
// TextArea 同理: ui_text_area_get_text / set_text / set_read_only
\`\`\`

### ComboBox
\`\`\`c
const wchar_t* items[] = {L"A", L"B", L"C"};
UiWidget combo = ui_combobox(items, 3);
ui_combobox_set_selected(combo, 1);
int sel = ui_combobox_get_selected(combo);
\`\`\`

### TabControl
\`\`\`c
ui_tab_add(tabs, L"Page 1", content_widget);
ui_tab_set_active(tabs, 0);
int active = ui_tab_get_active(tabs);
\`\`\`

### TitleBar
\`\`\`c
ui_titlebar_set_title(tb, L"Title");
ui_titlebar_show_buttons(tb, showMin, showMax, showClose);
ui_titlebar_show_icon(tb, 1);
ui_titlebar_add_widget(tb, custom_widget);
\`\`\`

### IconButton
\`\`\`c
UiWidget btn = ui_icon_button(svg_str, 1);  // 1=ghost
ui_icon_button_set_svg(btn, new_svg);
ui_icon_button_set_ghost(btn, 0);
ui_icon_button_set_icon_color(btn, color);
ui_icon_button_set_icon_padding(btn, 8);
\`\`\`

### ImageView
\`\`\`c
ui_image_load_file(img, win, L"photo.png");
ui_image_set_pixels(img, win, rgba, w, h, stride);
ui_image_clear(img);
ui_image_fit(img);              // 适应视图
ui_image_reset(img);            // 100% 缩放
ui_image_set_zoom(img, 2.0f);
ui_image_get_zoom(img);
ui_image_set_pan(img, x, y);
ui_image_set_rotation(img, 90); // 0/90/180/270
ui_image_set_checkerboard(img, 1);
ui_image_set_loading(img, 1);   // 显示加载旋转
ui_image_set_zoom_range(img, 0.1f, 10.0f);
ui_image_on_viewport_changed(img, cb, ud);

// GDI+ 流式模式（零内存占用）
ui_image_set_source_file(img, L"huge.jpg");

// 瓦片渲染（超大图）
ui_image_set_tiled(img, win, fullW, fullH, tileSize);
ui_image_set_tile(img, win, tx, ty, pixels, w, h, stride);
\`\`\`

### Dialog
\`\`\`c
UiWidget dlg = ui_dialog();
ui_dialog_set_ok_text(dlg, L"确定");
ui_dialog_set_cancel_text(dlg, L"取消");
ui_dialog_set_show_cancel(dlg, 1);
ui_dialog_show(dlg, win, L"标题", L"内容", callback, userdata);
// callback: void cb(UiWidget dlg, int confirmed, void* ud)
\`\`\`

### Toast
\`\`\`c
ui_toast(win, L"已保存", 2000);                  // 底部
ui_toast_at(win, L"提示", 3000, 0);              // 位置: 0=顶 1=中 2=底
ui_toast_ex(win, L"成功", 2000, 2, 1);           // 图标: 0=无 1=✓ 2=✕ 3=⚠
\`\`\`

### ContextMenu
\`\`\`c
UiMenu menu = ui_menu_create();
ui_menu_add_item(menu, 1, L"Cut");
ui_menu_add_item_ex(menu, 2, L"Copy", L"Ctrl+C", svg_icon);
ui_menu_add_separator(menu);
UiMenu sub = ui_menu_create();
ui_menu_add_item(sub, 10, L"Sub Item");
ui_menu_add_submenu(menu, L"More", sub);
ui_menu_set_enabled(menu, 1, 0);    // 禁用
ui_menu_show(win, menu, x, y);
ui_menu_close(win);
ui_menu_destroy(menu);
\`\`\`

### ScrollView
\`\`\`c
UiWidget sv = ui_scroll_view();
ui_scroll_set_content(sv, content_widget);
\`\`\`

## 9. 事件回调

所有回调签名统一: \`(控件句柄, [值], userdata)\`

\`\`\`c
// 点击
void on_click(UiWidget w, void* ud) { }
ui_widget_on_click(btn, on_click, userdata);

// CheckBox / Toggle
void on_value(UiWidget w, int value, void* ud) { }
ui_checkbox_on_changed(cb, on_value, ud);
ui_toggle_on_changed(tg, on_value, ud);

// Slider
void on_float(UiWidget w, float value, void* ud) { }
ui_slider_on_changed(sl, on_float, ud);

// ComboBox
void on_select(UiWidget w, int index, void* ud) { }
ui_combobox_on_changed(combo, on_select, ud);
\`\`\`

**传递句柄到回调**: 通过 \`uintptr_t\` 转换
\`\`\`c
ui_widget_on_click(btn, on_click, (void*)(uintptr_t)win);
// 回调内还原:
UiWindow win = (UiWindow)(uintptr_t)ud;
\`\`\`

## 10. 主题

\`\`\`c
ui_theme_set_mode(UI_THEME_DARK);
ui_theme_set_mode(UI_THEME_LIGHT);
UiThemeMode mode = ui_theme_get_mode();

// 查询当前主题色（用于 CustomWidget）
UiColor bg      = ui_theme_bg();
UiColor content = ui_theme_content_bg();
UiColor sidebar = ui_theme_sidebar_bg();
UiColor toolbar = ui_theme_toolbar_bg();
UiColor accent  = ui_theme_accent();
UiColor text    = ui_theme_text();
UiColor divider = ui_theme_divider();
\`\`\`

## 11. CustomWidget（自定义绘制）

\`\`\`c
UiWidget cw = ui_custom();
ui_custom_on_draw(cw, draw_cb, ud);
ui_custom_on_mouse_down(cw, mouse_cb, ud);
ui_custom_on_mouse_move(cw, mouse_cb, ud);
ui_custom_on_mouse_up(cw, mouse_cb, ud);
ui_custom_on_mouse_wheel(cw, wheel_cb, ud);
ui_custom_on_key_down(cw, key_cb, ud);
ui_custom_on_char(cw, char_cb, ud);
ui_custom_on_layout(cw, layout_cb, ud);
ui_custom_set_focused(cw, 1);

// 绘制 API（仅在 draw 回调内使用）
ui_draw_fill_rect(ctx, rect, color);
ui_draw_rect(ctx, rect, color, strokeWidth);
ui_draw_fill_rounded_rect(ctx, rect, rx, ry, color);
ui_draw_rounded_rect(ctx, rect, rx, ry, color, strokeWidth);
ui_draw_line(ctx, x1, y1, x2, y2, color, width);
ui_draw_text(ctx, L"text", rect, color, fontSize);
ui_draw_text_ex(ctx, L"text", rect, color, fontSize, align, bold);
float w = ui_draw_measure_text(ctx, L"text", fontSize);
ui_draw_bitmap(ctx, pixels, w, h, stride, destRect);
ui_draw_push_clip(ctx, rect);
ui_draw_pop_clip(ctx);
\`\`\`

## 12. .ui 标记文件

### 文件结构
\`\`\`xml
<ui version="1" width="800" height="600" resizable="true" title="App">
  <style>
    .title { fontSize: 20; bold: true; }
    Button:hover { bgColor: theme.accentHover; }
  </style>
  <VBox gap="0" expand="true">
    <TitleBar title="App" />
    <VBox padding="24" gap="12" expand="true">
      <!-- 控件 -->
    </VBox>
  </VBox>
</ui>
\`\`\`

### 通用属性
| 属性 | 示例 |
|------|------|
| \`id\` | \`id="myBtn"\` |
| \`width\` / \`height\` | \`width="120"\` |
| \`minWidth\` / \`maxWidth\` | \`minWidth="200"\` |
| \`expand\` | \`expand="true"\` |
| \`flex\` | \`flex="2"\` |
| \`padding\` | \`"16"\` 或 \`"8,4,8,4"\`（左上右下） |
| \`margin\` | \`"4"\` |
| \`gap\` | \`gap="8"\` |
| \`bgColor\` | \`"theme.sidebarBg"\` 或 \`"0.2,0.2,0.2,1"\` (RGBA 0-1) |
| \`visible\` / \`enabled\` | \`visible="false"\` |
| \`tooltip\` | \`tooltip="帮助"\` |
| \`class\` | \`class="title"\` |
| \`onClick\` | \`onClick="handler"\` |

### 布局容器

**VBox / HBox**
\`\`\`xml
<VBox gap="12" padding="16" align="center" justify="spaceBetween" expand="true">
  <Label text="Top" />
  <Spacer expand="true" />
  <Button text="Bottom" />
</VBox>
\`\`\`
align: start | center | end | stretch (默认 stretch)
justify: start | center | end | spaceBetween | spaceAround

**Grid**
\`\`\`xml
<Grid cols="2" colGap="12" rowGap="8">
  <Label text="Name" />  <TextInput placeholder="..." />
  <Label text="Email" /> <TextInput placeholder="..." />
</Grid>
\`\`\`

**Stack** — 同时只显示一个子控件
\`\`\`xml
<Stack id="pages" active="0" expand="true">
  <VBox><!-- page 0 --></VBox>
  <VBox><!-- page 1 --></VBox>
</Stack>
\`\`\`

**SplitView** — 侧边栏导航
\`\`\`xml
<SplitView mode="compactInline" openPaneLength="260" compactPaneLength="48" open="true">
  <VBox padding="4" gap="2">  <!-- Pane -->
    <NavItem text="Home" svg="..." selected="true" onClick="onHome" />
    <NavItem text="Settings" svg="..." onClick="onSettings" />
  </VBox>
  <Stack id="pages" active="0" expand="true">  <!-- Content -->
    <VBox>...</VBox>
  </Stack>
</SplitView>
\`\`\`
mode: overlay | inline | compactOverlay | compactInline

**Splitter** — 可拖拽分割
\`\`\`xml
<Splitter ratio="0.3" expand="true">
  <VBox><!-- 左 --></VBox>
  <VBox><!-- 右 --></VBox>
</Splitter>
\`\`\`

**ScrollView**
\`\`\`xml
<ScrollView expand="true">
  <VBox padding="16" gap="8">...</VBox>
</ScrollView>
\`\`\`

### 控件标签
\`\`\`xml
<Label text="标题" fontSize="20" bold="true" wrap="true" />
<Button text="Save" type="primary" onClick="onSave" />
<IconButton svg="<svg>...</svg>" ghost="true" />
<CheckBox text="选项" checked="true" onChanged="onCheck" />
<RadioButton text="A" group="grp" selected="true" />
<Toggle text="开关" on="true" onChanged="onToggle" />
<Slider min="0" max="100" value="50" onChanged="onSlider" />
<ProgressBar min="0" max="100" value="73" />
<ProgressBar indeterminate="true" />
<TextInput placeholder="输入..." />
<TextArea placeholder="多行..." height="120" />
<ComboBox items="选项A,选项B,选项C" onChanged="onSelect" />
<NumberBox min="0" max="100" value="42" step="1" width="120" />
<Separator />
<Separator vertical="true" />
<TitleBar title="My App" />
<ImageView expand="true" />
<TabControl id="tabs">
  <Tab title="Page 1"><VBox>...</VBox></Tab>
</TabControl>
<Expander header="Advanced" expanded="true">
  <VBox>...</VBox>
</Expander>
<Flyout id="flyout" placement="bottom">
  <VBox padding="8"><Label text="Content" /></VBox>
</Flyout>
<MenuBar>
  <Menu text="File">
    <MenuItem id="101" text="New" shortcut="Ctrl+N" onClick="onNew" />
    <MenuItem id="102" text="Open" shortcut="Ctrl+O" />
    <Separator />
    <MenuItem id="103" text="Exit" />
  </Menu>
  <Menu text="Edit">
    <MenuItem id="201" text="Undo" shortcut="Ctrl+Z" />
  </Menu>
</MenuBar>
\`\`\`
MenuBar 的 item 点击统一走 \`ui_window_on_menu(win, cb, ud)\` 注册的回调，回调
参数是 MenuItem 的 \`id\`。若 MenuItem 自带 \`onClick\`，则两者都会触发。

### 数据绑定
\`\`\`xml
<Label text="{statusText}" />
<Slider value="{volume}" />
<CheckBox checked="{enabled}" />
\`\`\`
C++ 推送:
\`\`\`cpp
g_layout.SetText("statusText", L"Ready");
g_layout.SetFloat("volume", 75.0f);
g_layout.SetBool("enabled", true);
\`\`\`

### 事件处理
\`\`\`xml
<Button onClick="onSave" />
<Toggle onChanged="onDarkMode" />
<Slider onChanged="onVolume" />
<ComboBox onChanged="onSelect" />
\`\`\`
C++ 注册（**必须在 LoadFile 之前**）:
\`\`\`cpp
g_layout.SetHandler("onSave",     std::function<void()>([]() { }));
g_layout.SetHandler("onDarkMode", std::function<void(bool)>([](bool on) { }));
g_layout.SetHandler("onVolume",   std::function<void(float)>([](float v) { }));
g_layout.SetHandler("onSelect",   std::function<void(int)>([](int i) { }));
\`\`\`

### 样式
\`\`\`xml
<style>
  .title   { fontSize: 20; bold: true; textColor: theme.accent; }
  Button   { bgColor: theme.accent; }
  Button:hover { bgColor: theme.accentHover; }
  Button:pressed { bgColor: theme.accent; }
  Button:disabled { bgColor: theme.disabledBg; }
</style>
\`\`\`

可用主题色: theme.accent / accentHover / windowBg / contentBg / sidebarBg / toolbarBg /
titleBarBg / titleBarText / btnText / btnHover / btnPress / divider / inputBg / inputBorder /
statusBarBg / statusBarText / contentText / foreground1~4 / background1~5 /
cardBg / cardBorder / disabledBg / disabledText

### 组件引入
\`\`\`xml
<Include src="components/header.ui" title="My Title" />
<!-- 被引入文件用 {props.title} 引用。递归上限 8 层 -->
\`\`\`

### 列表渲染
\`\`\`xml
<Repeater model="{userList}">
  <HBox gap="8">
    <Label text="{item.name}" expand="true" />
    <Label text="{item.email}" />
  </HBox>
</Repeater>
\`\`\`

### 多语言 (i18n)
\`\`\`xml
<Label text="@welcome_msg" />
<Button text="@btn_save" />
\`\`\`
语言包文件 (UTF-8):
\`\`\`ini
# lang/zh-CN.lang
welcome_msg=欢迎
btn_save=保存
\`\`\`
C++:
\`\`\`cpp
g_layout.LoadLanguage(L"lang/zh-CN.lang");
g_layout.ApplyLanguage();
\`\`\`

### C++ 控件操作
\`\`\`cpp
auto* stack = g_layout.FindAs<ui::StackWidget>("pages");
stack->SetActiveIndex(1);

auto* sv = g_layout.FindAs<ui::SplitViewWidget>("nav");
sv->TogglePane();

g_layout.SetText("label", L"Done");
g_layout.Reload();  // 热重载 .ui 文件
\`\`\`

## 13. 调试 Inspector API

\`\`\`c
// 导出控件树为 JSON（包含类型、ID、位置、尺寸）
char* json = ui_debug_dump_tree(win);
printf("%s\\n", json);
ui_debug_free(json);  // ⚠ 必须释放

// 导出单个控件
char* info = ui_debug_dump_widget(widget);
ui_debug_free(info);

// 红框高亮指定控件
ui_debug_highlight(win, "widget_id");
ui_debug_highlight(win, NULL);  // 清除

// 截图保存为 PNG
int ok = ui_debug_screenshot(win, L"screenshot.png");  // 0=成功
\`\`\`

## 14. 事件注入 / 自动化 API（since 1.1.0）

**关键能力**：无需真实鼠标键盘即可驱动任意控件，让 AI 自己完成"生成 → 运行 →
点击 → 截图 → 验证 → 修改"闭环。约 60 个 \`ui_debug_*\` 函数，返回 0 成功，
坐标全部用 DIP（逻辑像素）。

### 鼠标
\`\`\`c
ui_debug_click(win, w);                     // 完整 MouseDown+Up，触发 onClick
ui_debug_click_at(win, x, y);               // 按坐标
ui_debug_double_click(win, w);
ui_debug_right_click(win, w);               // 或 _at(win,x,y)：等同 WM_RBUTTONUP
ui_debug_hover(win, w);                     // 移动到控件中心
ui_debug_mouse_move(win, x, y);
ui_debug_drag(win, w, dx, dy);              // 相对拖；drag_to(x1,y1,x2,y2) 绝对
ui_debug_wheel(win, w, delta);              // 滚轮，会自动向上找 ScrollView
\`\`\`

### 键盘 / 焦点
\`\`\`c
ui_debug_focus(win, w);                     // 设焦点并显示焦点环
ui_debug_blur(win);
ui_debug_key(win, VK_RETURN);               // 走 WndProc 同一套分发（Tab/Enter/方向键等）
ui_debug_type_char(win, L'A');
ui_debug_type_text(win, L"hello world");    // 逐字符 WM_CHAR 等效
\`\`\`

### 控件高层（直接改状态 + 触发回调）
\`\`\`c
ui_debug_checkbox_set(win, cb, 1);          // 或 _toggle
ui_debug_toggle_set(win, tg, 1);            // Switch
ui_debug_radio_select(win, rb);             // 自动取消同组
ui_debug_combo_select(win, combo, 2);       // 触发 onSelectionChanged
ui_debug_slider_set(win, sl, 0.75f);        // 触发 onFloatChanged
ui_debug_number_set(win, nb, 42.0f);
ui_debug_tab_set(tab, 1);
ui_debug_expander_set(ex, 1);               // 展开
ui_debug_splitview_set(sv, 1);              // 开侧栏
ui_debug_flyout_show(fw, anchor);           // 或 _hide
ui_debug_text_set(w, L"new text");          // TextInput / TextArea / Label
ui_debug_scroll_set(sv, 240.0f);
\`\`\`

### Context Menu（含任意深度子菜单）
\`\`\`c
// 先弹菜单（通常由 onRightClick 回调里 ui_menu_show 创建）
ui_debug_right_click_at(win, 300, 200);

// 自动化脚本持前台时必须关掉前台轮询，否则 50ms 内菜单被自动关：
ui_debug_set_menu_autoclose(0);  // 0=禁用自动关闭，1=恢复

// 顶层点击
ui_debug_menu_click_index(win, 0);         // 按索引
ui_debug_menu_click_id(win, 1001);         // 按 id

// 子菜单路径点击：path=[2,0] 表示顶层第 2 项的 submenu 里第 0 项
int path[] = {2, 0};
ui_debug_menu_click_path(win, path, 2);

// 查询
int n = ui_debug_menu_item_count_at(win, path, 1);     // 顶层第 2 项 submenu 的项数
int id = ui_debug_menu_item_id_at(win, path, 2);        // 叶子的 item id
int has = ui_debug_menu_has_submenu_at(win, path, 1);
ui_debug_menu_close(win);
\`\`\`

### Dialog
\`\`\`c
ui_debug_dialog_confirm(win);               // 等同按 Enter
ui_debug_dialog_cancel(win);                // 等同按 Esc
\`\`\`

### HWND 通道（走真实 Win32 消息循环；异步）
\`\`\`c
ui_debug_post_click(win, x, y);             // PostMessage WM_LBUTTONDOWN/UP
ui_debug_post_right_click(win, x, y);
ui_debug_post_key(win, VK_TAB);
ui_debug_post_char(win, L'A');
ui_debug_pump();                            // 处理消息队列，让 post_* 生效
\`\`\`

### 线程安全
\`\`\`c
// ui_debug_* 和 widget mutation 必须在 UI 线程上执行。
// 工作线程里访问前先 marshal：
ui_window_invoke_sync(win, my_fn, userdata);
\`\`\`

### 查询帮手
\`\`\`c
float cx, cy;
ui_debug_widget_center(w, &cx, &cy);        // 取 widget 中心 DIP 坐标
int visible = ui_debug_widget_is_visible(w);
\`\`\`

## 14b. AI 验证闭环（推荐工作流）

\`\`\`
Step 1: 编译运行
  cmake --build build && start ./build/app.exe

Step 2: 定位目标控件
  UiWidget btn = ui_widget_find_by_id(root, "submit");
  // 或者从 ui_debug_dump_tree 返回的 JSON 里解析 id + rect

Step 3: 模拟交互
  ui_debug_focus(win, inputEmail);
  ui_debug_type_text(win, L"user@example.com");
  ui_debug_click(win, btn);                 // 触发 onClick

Step 4: 截图 + 结构双重验证
  ui_debug_screenshot(win, L"step3.png");
  char* tree = ui_debug_dump_tree(win);
  // 解析 JSON 检查：
  //  - 预期 Label/Toast 是否出现
  //  - Flyout/Dialog.open 是否 true
  //  - ComboBox.selectedIndex 是否正确
  ui_debug_free(tree);

Step 5: 右键 + 子菜单验证
  ui_debug_right_click_at(win, 400, 300);
  ui_debug_screenshot(win, L"menu.png");    // 确认菜单弹出
  int path[] = {2, 0};
  ui_debug_menu_click_path(win, path, 2);
  // onMenuItemClick 被触发，对应 side effect 应出现

Step 6: 定位问题（如果失败）
  ui_debug_highlight(win, "suspect_id");
  ui_debug_screenshot(win, L"highlight.png");
\`\`\`

### 外部脚本驱动（可选）

demo 自带 Named Pipe \`\\\\.\\pipe\\ui_core_debug\`，PowerShell / Python 一行可驱动：
\`\`\`powershell
# PowerShell 示例
\$p = New-Object System.IO.Pipes.NamedPipeClientStream '.', 'ui_core_debug', 'InOut'
\$p.Connect(2000)
\$b = [System.Text.Encoding]::UTF8.GetBytes('click submitBtn')
\$p.Write(\$b, 0, \$b.Length); \$p.Flush()
# 读响应 ...
\`\`\`
命令清单见 \`docs/debug-simulation.md\` 或发 \`help\` 给 pipe。

## 14c. 画布 / 无边框窗口（since 1.2.0）

适合图片查看器、桌面挂件、色卡面板——整个窗口就是一张画布，滚轮缩放，按住空白拖动。

\`\`\`c
UiWindowConfig cfg = {0};
cfg.system_frame = 0;        // 无系统边框
cfg.resizable    = 1;
cfg.width        = 800;
cfg.height       = 600;
UiWindow win = ui_window_create(&cfg);

UiWidget root = ui_vbox();
ui_widget_set_drag_window(root, 1);   // 点空白处拖动窗口
ui_window_set_root(win, root);

ui_window_enable_canvas_mode(win, 1); // 一键打开：min_size 32x32 + bg_mode=1 + root dragWindow + 隐藏 TitleBar
ui_window_show(win);
\`\`\`

| 函数 | 说明 |
|------|------|
| \`ui_window_enable_canvas_mode(win, 1)\` | 一键切画布模式；=0 恢复 |
| \`ui_window_set_min_size(win, w, h)\` | 覆盖主题 480x360 下限 |
| \`ui_window_set_background_mode(win, 1)\` | 透明 Clear 代替主题色填充（避免 SetWindowPos 时闪色） |
| \`ui_widget_set_drag_window(w, 1)\` | 命中该 widget 的空白处时，让系统拖动窗口 |
| \`ui_window_set_rect(win, x, y, w, h)\` | 原子 SetWindowPos + 重绘 |
| \`ui_window_set_size(win, w, h)\` | 保持位置改尺寸 |
| \`ui_window_set_position(win, x, y)\` | 保持尺寸改位置 |
| \`ui_window_get_rect_screen(win, &x, &y, &w, &h)\` | 读当前屏幕矩形（DIP） |
| \`ui_window_resize_with_anchor(win, w, h, acx, acy, sx, sy)\` | resize 到 (w,h) 的同时把客户区 (acx,acy) 对齐到屏幕 (sx,sy)——滚轮缩放 "光标不动" 原语 |

> 提示：\`ui_widget_set_drag_window\` 走 \`WM_NCHITTEST → HTCAPTION\`，HitTest 深度优先所以内部交互控件天然不受影响，只有命中 Panel 本身的空白处才触发拖窗。

## 14d. 字体 / 文字渲染（since 1.3.0）

三层粒度：全局默认 / 单窗口覆盖 / 渲染模式 preset。不需要手写 DirectWrite 样板。

\`\`\`c
// 全局默认（一次设定，进程内所有窗口生效）
ui_theme_set_default_font(L"Microsoft YaHei UI");

// 中英分离：拉丁走 Consolas，汉字走 YaHei
ui_theme_set_cjk_font(L"Consolas", L"Microsoft YaHei UI");

// 全局渲染模式：WinUI 风格（默认），记事本最锐，灰度，ClearType，无抗锯齿
ui_theme_set_text_render_mode(UI_TEXT_RENDER_SMOOTH);

// 单窗口覆盖（当前窗口优先，不影响全局）
ui_window_set_default_font(win, L"SimSun");
ui_window_set_text_render_mode(win, UI_TEXT_RENDER_CLEARTYPE);

// 一次清除该窗口所有覆盖，回到全局默认
ui_window_clear_font_override(win);
\`\`\`

### \`UiTextRenderMode\` 枚举

| 值 | 风格 | 对应 DWrite |
|----|------|-------------|
| \`UI_TEXT_RENDER_SMOOTH\` (默认) | WinUI 风格 | GRAYSCALE + NATURAL_SYMMETRIC |
| \`UI_TEXT_RENDER_CLEARTYPE\` | Office / Chrome 风格 | CLEARTYPE + NATURAL |
| \`UI_TEXT_RENDER_SHARP\` | 记事本 最锐 | CLEARTYPE + GDI_CLASSIC |
| \`UI_TEXT_RENDER_GRAY_SHARP\` | 灰度 + 像素对齐 | GRAYSCALE + GDI_CLASSIC |
| \`UI_TEXT_RENDER_ALIASED\` | 像素块风 | ALIASED + ALIASED |

### 中英分离实现

\`ui_theme_set_cjk_font(latin, cjk)\` 用 \`IDWriteFontFallbackBuilder\` 把 11 个 CJK Unicode 区段（0x2E80-0xFFEF：部首、标点、平假名、片假名、汉字基本/扩展 A/兼容/兼容形式/半角全角等）映射到 \`cjk\`，其余区段用 \`latin\`，最后追加系统默认 fallback。

### API 速查

| 函数 | 层级 | 说明 |
|------|------|------|
| \`ui_theme_set_default_font(family)\` | 全局 | 进程默认字体 |
| \`ui_theme_set_cjk_font(latin, cjk)\` | 全局 | 中英分离 |
| \`ui_theme_set_text_render_mode(mode)\` | 全局 | 渲染模式 |
| \`ui_theme_get_default_font()\` | 全局 | 读当前默认 |
| \`ui_theme_get_cjk_latin_font()\` | 全局 | 读 latin 半 |
| \`ui_theme_get_cjk_cjk_font()\` | 全局 | 读 cjk 半 |
| \`ui_theme_get_text_render_mode()\` | 全局 | 读当前模式 |
| \`ui_window_set_default_font(win, family)\` | 窗口 | 覆盖默认 |
| \`ui_window_set_cjk_font(win, latin, cjk)\` | 窗口 | 覆盖中英分离 |
| \`ui_window_set_text_render_mode(win, mode)\` | 窗口 | 覆盖渲染模式 |
| \`ui_window_clear_font_override(win)\` | 窗口 | 一次清除所有覆盖 |

## 15. 设计系统速查

**字体**: Segoe UI, 10 级 — Caption2(10) Caption(12) Body(14) Body2(16) Subtitle(20) Title3(24) Title2(28) Title1(32) Large(40) Display(68)

**间距**: none(0) xxs(2) xs(4) sNudge(6) s(8) mNudge(10) m(12) l(16) xl(20) xxl(24) xxxl(32)

**圆角**: small(2) medium(4) large(6) xLarge(8) xxLarge(12) circular(9999)
- 控件用 4px，弹出层/卡片用 8px

**阴影**: 6 级 — s2 s4 s8 s16 s28 s64（ambient + key 双层）

**动画时长**: ultraFast(50ms) faster(100ms) fast(150ms) normal(200ms) gentle(250ms) slow(300ms) slower(400ms) ultraSlow(500ms)

**状态色**: danger(#d13438) success(#107c10) warning(#fde300) info(#0078d4)

**Brand 色**: primary(#0f6cbd), tint10~60, shade10~50

## 16. 常见陷阱

1. 字符串必须用 \`L"..."\` 宽字符，不是 \`"..."\`
2. \`ui_debug_dump_tree\` / \`dump_widget\` 返回的指针必须 \`ui_debug_free()\`
3. \`ui_run()\` 是阻塞消息循环，之后的代码不会执行
4. 句柄 0 (\`UI_INVALID\`) 表示创建失败，使用前检查
5. 回调中传句柄需要 \`(void*)(uintptr_t)\` 双重转换
6. \`ui_widget_set_padding()\` 是四参数（左上右下），单参数用 \`_uniform()\`
7. \`ui_text_input_get_text()\` 返回内部指针，不要 free，下次调用后失效
8. .ui 文件的事件 handler 必须在 \`LoadFile()\` 之前注册
9. \`ApplyLanguage()\` 必须在 \`LoadFile()\` 之后调用
10. 颜色值范围是 0.0~1.0 的 float，不是 0~255

## 17. 完整示例

### C API 方式
\`\`\`c
#include <ui_core.h>

static UiWidget g_status;

void on_click(UiWidget w, void* ud) {
    UiWindow win = (UiWindow)(uintptr_t)ud;
    ui_label_set_text(g_status, L"Button clicked!");
    ui_toast(win, L"Hello!", 2000);
}

int WINAPI wWinMain(HINSTANCE h, HINSTANCE, LPWSTR, int) {
    ui_init();
    UiWindowConfig cfg = {0};
    cfg.title = L"Demo"; cfg.width = 640; cfg.height = 480; cfg.resizable = 1;
    UiWindow win = ui_window_create(&cfg);

    UiWidget root = ui_vbox();
    ui_widget_set_padding_uniform(root, 24);
    ui_widget_set_gap(root, 12);

    UiWidget title = ui_label(L"Hello, Core UI!");
    ui_label_set_font_size(title, 24);
    ui_label_set_bold(title, 1);
    ui_widget_add_child(root, title);

    UiWidget btn = ui_button(L"Click Me");
    ui_button_set_type(btn, 1);
    ui_widget_set_width(btn, 120);
    ui_widget_on_click(btn, on_click, (void*)(uintptr_t)win);
    ui_widget_add_child(root, btn);

    g_status = ui_label(L"Ready");
    ui_widget_add_child(root, g_status);

    ui_window_set_root(win, root);
    ui_window_show(win);
    return ui_run();
}
\`\`\`

### .ui 标记方式
\`\`\`xml
<ui version="1" width="640" height="480" resizable="true" title="Demo">
  <VBox gap="0" expand="true">
    <TitleBar title="Demo" />
    <VBox padding="24" gap="12" expand="true">
      <Label text="Hello, Core UI!" fontSize="24" bold="true" />
      <Button id="btn" text="Click Me" type="primary" width="120" onClick="onBtnClick" />
      <Label id="status" text="Ready" />
    </VBox>
  </VBox>
</ui>
\`\`\`
\`\`\`cpp
g_layout.SetHandler("onBtnClick", std::function<void()>([]() {
    g_layout.SetText("status", L"Button clicked!");
    ui_toast(g_win, L"Hello!", 2000);
    ui_window_invalidate(g_win);
}));
g_layout.LoadFile(L"app.ui");
\`\`\`
`;
