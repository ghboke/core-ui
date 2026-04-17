export const AI_SKILL_CONTENT = `# UI Core — AI Coding Skill

> Windows 桌面 UI 框架。Direct2D 硬件加速，Fluent 2 设计系统，纯 C API + 声明式 .ui 标记。
> 本文件供 AI 编程助手（Claude / GPT / Copilot）使用，包含完整的 API 参考和使用模式。

---

## 1. 架构概览

- **渲染**: Direct2D + Direct3D 11，GPU 合成，60fps
- **API**: 纯 C 导出（ui_core.h），所有对象为 \`uint64_t\` 不透明句柄
- **UI 描述**: 两种方式 — C API 手动构建 或 .ui XML 标记文件
- **主题**: 深色/浅色自动切换，Fluent 2 语义 Token
- **DPI**: Per-Monitor DPI V2，多显示器自动缩放
- **分发**: 单 DLL (ui-core.dll) 或静态链接

## 2. 项目结构

\`\`\`
my_app/
  main.cpp          — 入口
  app.ui            — 界面布局（声明式）
  lang/zh-CN.lang   — 语言包（可选）
  ui_core.h         — 头文件
  ui-core.dll       — 运行时库
  ui-core.lib       — 导入库
\`\`\`

CMake 集成:
\`\`\`cmake
add_executable(my_app WIN32 main.cpp)
target_include_directories(my_app PRIVATE third_party/ui-core/include)
target_link_libraries(my_app PRIVATE \${CMAKE_SOURCE_DIR}/third_party/ui-core/ui-core.lib)
add_custom_command(TARGET my_app POST_BUILD
    COMMAND \${CMAKE_COMMAND} -E copy_if_different
        \${CMAKE_SOURCE_DIR}/third_party/ui-core/ui-core.dll $<TARGET_FILE_DIR:my_app>)
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
\`\`\`

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

## 13. 调试 API

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

## 14. AI 验证流程

推荐工作流: 生成代码 → 编译运行 → 截图 → 检查 → 修正

\`\`\`
Step 1: 编译运行
  cmake --build build && ./build/Release/app.exe

Step 2: 截图验证
  ui_debug_screenshot(win, L"verify.png");
  → 视觉检查布局是否正确

Step 3: 结构验证
  char* tree = ui_debug_dump_tree(win);
  → 解析 JSON，检查:
    - 控件层级是否正确
    - 所有 ID 是否存在
    - rect 尺寸是否合理（非零、未重叠）
  ui_debug_free(tree);

Step 4: 定位问题
  ui_debug_highlight(win, "suspect_id");
  ui_debug_screenshot(win, L"highlight.png");

Step 5: 交互验证
  const wchar_t* text = ui_text_input_get_text(input);
  int checked = ui_checkbox_get_checked(cb);
\`\`\`

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

    UiWidget title = ui_label(L"Hello, UI Core!");
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
      <Label text="Hello, UI Core!" fontSize="24" bold="true" />
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
