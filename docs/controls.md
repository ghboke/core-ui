# 控件参考

所有控件样式对齐 WinUI 3 源码级规格（microsoft-ui-xaml themeresources）。

## Label

```xml
<Label text="标题文字" fontSize="20" bold="true"
       textColor="theme.accent" align="center"
       wrap="true" maxLines="3" />
```

| 属性 | 说明 | 默认 |
|------|------|------|
| `text` | 文本内容，支持 `{binding}` | — |
| `fontSize` | 字号 (px) | `14` |
| `bold` | 粗体 | `false` |
| `textColor` | 颜色 | 主题文字色 |
| `align` | `left` `right` `center` | `left` |
| `wrap` | 自动换行 | `false` |
| `maxLines` | 最大行数（需 wrap），0=不限 | `0` |

C API：
```c
ui_label_set_text(w, L"文本");
ui_label_set_font_size(w, 16);
ui_label_set_bold(w, 1);
ui_label_set_text_color(w, color);
ui_label_set_align(w, 0);  // 0=左, 1=右, 2=中
ui_label_set_wrap(w, 1);
ui_label_set_max_lines(w, 3);
```

## Button

32px 高度，4px 圆角，elevation 底部边框，按下文字变灰。

```xml
<!-- 标准按钮（灰色背景，默认） -->
<Button text="Cancel" width="100" onClick="onCancel" />

<!-- 主色按钮（accent 蓝色背景 + 白色文字） -->
<Button text="Save" type="primary" width="100" onClick="onSave" />

<!-- 自定义颜色 -->
<Button text="Delete" bgColor="0.8,0.2,0.2,1" textColor="1,1,1,1" />
<Button text="Green" bgColor="0.2,0.6,0.3,1" textColor="1,1,1,1" />

<!-- 主题色引用 -->
<Button text="Accent" bgColor="theme.accent" textColor="1,1,1,1" />
```

| 属性 | 说明 |
|------|------|
| `text` | 按钮文字 |
| `type` | `default`(标准) 或 `primary`(主色 accent 填充) |
| `bgColor` | 自定义背景色（hover 自动变暗 10%，press 变暗 20%） |
| `textColor` | 自定义文字颜色 |
| `onClick` | 点击事件名 |
| `fontSize` | 字号 |

C API：
```c
UiWidget btn = ui_button(L"Save");
ui_button_set_type(btn, 1);  // 0=default, 1=primary
ui_button_set_bg_color(btn, (UiColor){0.8f, 0.2f, 0.2f, 1});
ui_button_set_text_color(btn, (UiColor){1, 1, 1, 1});
ui_widget_on_click(btn, callback, userdata);
```

## CheckBox

20x20 方框，4px 圆角，选中态 accent 填充 + 勾号字形，200ms 动画。

```xml
<CheckBox text="Enable feature" checked="true" onChanged="onToggle" />
```

| 属性 | 说明 |
|------|------|
| `text` | 标签文字 |
| `checked` | 初始选中，支持 `{binding}` |
| `onChanged` | 变化事件，handler 参数 `(bool)` |

C API：
```c
ui_checkbox_set_checked(w, 1);
int checked = ui_checkbox_get_checked(w);
ui_checkbox_on_changed(w, callback, userdata);
```

## RadioButton

20x20 外圆，选中态 accent 填充 + 白色中心点（rest=12 hover=14 press=10 缩放），同 group 互斥。

```xml
<RadioButton text="Option A" group="myGroup" selected="true" />
<RadioButton text="Option B" group="myGroup" />
<RadioButton text="Option C" group="myGroup" />
```

| 属性 | 说明 |
|------|------|
| `text` | 标签文字 |
| `group` | 分组名（同组互斥） |
| `selected` | 初始选中 |

C API：
```c
ui_radio_set_selected(w, 1);
int sel = ui_radio_get_selected(w);
```

## Toggle（开关）

40x20 轨道，12x12 滑块，off 态灰边框，on 态 accent 填充。

```xml
<Toggle text="Wi-Fi" on="true" onChanged="onWifiToggle" />
```

| 属性 | 说明 |
|------|------|
| `text` | 标签文字 |
| `on` | 初始状态，支持 `{binding}` |
| `onChanged` | 变化事件，handler 参数 `(bool)` |

C API：
```c
ui_toggle_set_on(w, 1);
int on = ui_toggle_get_on(w);
ui_toggle_on_changed(w, callback, userdata);
```

## Slider

4px 轨道，18px thumb（白色外圈 + accent 内点），hover/press 缩放动画（指数缓动），仅鼠标靠近 thumb 时触发 hover。

```xml
<Slider min="0" max="100" value="50" onChanged="onVolume" expand="true" />
```

| 属性 | 说明 |
|------|------|
| `min` / `max` | 值范围 |
| `value` | 当前值，支持 `{binding}` |
| `onChanged` | 变化事件，handler 参数 `(float)` |

C API：
```c
ui_slider_set_value(w, 75.0f);
float val = ui_slider_get_value(w);
ui_slider_on_changed(w, callback, userdata);
```

## ProgressBar

3px 指示条 + 1px 轨道，1.5px 圆角。

```xml
<ProgressBar min="0" max="100" value="73" expand="true" />
<ProgressBar indeterminate="true" expand="true" />
```

| 属性 | 说明 |
|------|------|
| `min` / `max` | 值范围 |
| `value` | 当前值（有值变化动画） |
| `indeterminate` | 不确定模式（滑动动画 888ms） |

C API：
```c
ui_progress_set_value(w, 50.0f);
float val = ui_progress_get_value(w);
ui_progress_set_indeterminate(w, 1);
```

## TextInput（单行输入）

32px 高度，padding 11,5,11,6，4px 圆角，focus 态底部 2px accent 线。Placeholder 聚焦后变淡但不消失，输入后才隐藏。

```xml
<TextInput placeholder="Enter your name..." expand="true" maxLength="50" />
```

功能：光标闪烁、文本选择、Ctrl+C/V/X/A、Shift+方向键选择。

C API：
```c
ui_text_input_set_text(w, L"text");
const wchar_t* t = ui_text_input_get_text(w);  // 内部指针，不要 free
```

## TextArea（多行输入）

同 TextInput 功能 + 回车换行、鼠标滚轮滚动、上下方向键跨行。

```xml
<TextArea placeholder="Type notes..." height="120" expand="true" />
```

C API：
```c
ui_text_area_set_text(w, L"line1\nline2");
const wchar_t* t = ui_text_area_get_text(w);
```

## ComboBox

32px 高度，8px 下拉圆角，elevation 底部边框，选中项左侧 accent 竖条。

```xml
<ComboBox items="Dark,Light,System" selected="0"
          onChanged="onThemeSelect" width="180" />
```

| 属性 | 说明 |
|------|------|
| `items` | 选项列表，逗号分隔 |
| `selected` | 初始选中索引 |
| `onChanged` | 变化事件，handler 参数 `(int)` |

C API：
```c
const wchar_t* items[] = {L"A", L"B", L"C"};
UiWidget combo = ui_combobox(items, 3);
ui_combobox_set_selected(combo, 1);
int sel = ui_combobox_get_selected(combo);
ui_combobox_on_changed(combo, callback, userdata);
```

## NavItem（导航项）

SplitView 侧边栏专用。40px 高度，左侧 accent 竖条指示器，图标区 48px（compact 时居中），文字自动裁剪。

```xml
<NavItem text="Home"
         svg="&lt;svg viewBox='0 0 24 24'&gt;&lt;path d='...'/&gt;&lt;/svg&gt;"
         selected="true" onClick="onNavHome" />
```

| 属性 | 说明 |
|------|------|
| `text` | 标签文字 |
| `svg` | SVG 图标（XML 转义） |
| `glyph` | 图标字体字符 |
| `selected` | 选中状态 |
| `onClick` | 点击事件 |

## Separator

```xml
<Separator />                    <!-- 水平 -->
<Separator vertical="true" />    <!-- 垂直 -->
```

## TitleBar

无边框窗口标题栏，内置最小化/最大化/关闭按钮。

```xml
<TitleBar title="My App" />

<!-- 内嵌自定义控件 -->
<TitleBar title="My App">
  <Button text="★" width="36" height="28" />
</TitleBar>
```

C API：
```c
ui_titlebar_set_title(tb, L"New Title");
ui_titlebar_show_buttons(tb, showMin, showMax, showClose);
ui_titlebar_show_icon(tb, show);
ui_titlebar_add_widget(tb, custom_widget);
```

## IconButton

SVG 图标按钮。Normal 模式始终显示背景，Ghost 模式默认透明 hover 显示。

```xml
<IconButton svg="<svg>...</svg>" ghost="true" width="36" height="36" />
```

C API：
```c
UiWidget btn = ui_icon_button(svg_str, 1);  // 1=ghost
ui_icon_button_set_svg(btn, new_svg);
ui_icon_button_set_ghost(btn, 0);
ui_icon_button_set_icon_color(btn, color);
ui_icon_button_set_icon_padding(btn, 8);
```

## TabControl

```xml
<TabControl id="tabs">
  <Tab title="Page 1">
    <VBox padding="16">...</VBox>
  </Tab>
  <Tab title="Page 2">
    <VBox padding="16">...</VBox>
  </Tab>
</TabControl>
```

C API：
```c
UiWidget tabs = ui_tab_control();
ui_tab_add(tabs, L"Page 1", content1);
ui_tab_set_active(tabs, 0);
int active = ui_tab_get_active(tabs);
```

## ImageView

可缩放、平移、旋转的图片画布。支持 PNG/JPG/GIF，大图瓦片渲染。

```xml
<ImageView expand="true" />
```

C API：
```c
ui_image_load_file(img, win, L"photo.png");
ui_image_set_pixels(img, win, pixels, w, h, stride);
ui_image_fit(img);
ui_image_reset(img);
ui_image_set_zoom(img, 2.0f);
ui_image_set_rotation(img, 90);  // 0/90/180/270
ui_image_set_checkerboard(img, 1);
ui_image_set_loading(img, 1);
int w = ui_image_width(img);
int h = ui_image_height(img);

// 瓦片渲染（超大图）
ui_image_set_tiled(img, win, fullW, fullH, tileSize);
ui_image_set_tile(img, win, tx, ty, pixels, w, h, stride);

// 视口变化回调
ui_image_on_viewport_changed(img, callback, userdata);
```

交互：滚轮缩放（朝光标位置）、拖拽平移、高倍放大自动最近邻插值。

### 裁剪模式

ImageView 内置裁剪覆盖层，支持自由裁剪和锁定比例。

```cpp
// 开启裁剪模式
auto* iv = g_layout.FindAs<ui::ImageViewWidget>("myImage");
iv->SetCropMode(true);

// 锁定比例 (0=自由, 1.0=1:1, 16.0/9.0=16:9)
iv->SetCropAspectRatio(16.0f / 9.0f);

// 获取裁剪区域（图片像素坐标）
float x, y, w, h;
iv->GetCropRect(x, y, w, h);

// 手动设置裁剪区域
iv->SetCropRect(100, 50, 800, 600);

// 重置为全图
iv->ResetCrop();

// 裁剪区域变化回调
iv->onCropChanged = [](float x, float y, float w, float h) {
    // 实时获取裁剪尺寸
};
```

裁剪覆盖层特性：
- 半透明黑色遮罩 + 白色裁剪框 + 三分线参考格
- 8 个拖拽手柄（四角 + 四边中点）
- 框内拖拽整体移动
- 支持锁定宽高比
- 裁剪区域自动限制在图片范围内

## Dialog（模态对话框）

```c
UiWidget dlg = ui_dialog();
ui_dialog_set_ok_text(dlg, L"确定");
ui_dialog_set_cancel_text(dlg, L"取消");
ui_dialog_set_show_cancel(dlg, 1);

void on_result(UiWidget dlg, int confirmed, void* ud) { }
ui_dialog_show(dlg, win, L"确认", L"确定删除？", on_result, data);
```

## Toast（通知）

```c
ui_toast(win, L"已保存", 2000);                    // 底部
ui_toast_at(win, L"提示", 3000, 0);                // 0=顶, 1=中, 2=底
ui_toast_ex(win, L"成功", 2000, 2, 1);             // 图标: 1=✓ 2=✕ 3=⚠
```

滑入/滑出动画，自动消失。

## ContextMenu（右键菜单）

```c
UiMenu menu = ui_menu_create();
ui_menu_add_item(menu, 1, L"Item");
ui_menu_add_item_ex(menu, 2, L"Cut", L"Ctrl+X", svg_icon);
ui_menu_add_separator(menu);

UiMenu sub = ui_menu_create();
ui_menu_add_item(sub, 10, L"Sub Item");
ui_menu_add_submenu(menu, L"More", sub);

ui_menu_set_enabled(menu, 99, 0);
ui_menu_show(win, menu, x, y);
ui_menu_close(win);
ui_menu_destroy(menu);
```

支持 SVG 图标、快捷键提示、子菜单、禁用项、自动定位。

## CustomWidget（自定义绘制）

```c
UiWidget cw = ui_custom();

void my_draw(UiWidget w, UiDrawCtx ctx, UiRect rect, void* ud) {
    ui_draw_fill_rounded_rect(ctx, rect, 6, 6, (UiColor){0.2f, 0.3f, 0.8f, 1});
    ui_draw_text(ctx, L"Hello", rect, (UiColor){1,1,1,1}, 14);
}
ui_custom_on_draw(cw, my_draw, NULL);

// 鼠标/键盘事件
ui_custom_on_mouse_down(cw, cb, ud);
ui_custom_on_mouse_move(cw, cb, ud);
ui_custom_on_mouse_up(cw, cb, ud);
ui_custom_on_mouse_wheel(cw, cb, ud);
ui_custom_on_key_down(cw, cb, ud);
ui_custom_on_char(cw, cb, ud);
ui_custom_on_layout(cw, cb, ud);
```

### 绘制 API（仅在 draw 回调内）

```c
ui_draw_fill_rect(ctx, rect, color);
ui_draw_rect(ctx, rect, color, width);
ui_draw_fill_rounded_rect(ctx, rect, rx, ry, color);
ui_draw_rounded_rect(ctx, rect, rx, ry, color, width);
ui_draw_line(ctx, x1, y1, x2, y2, color, width);
ui_draw_text(ctx, text, rect, color, fontSize);
ui_draw_text_ex(ctx, text, rect, color, fontSize, align, bold);
ui_draw_measure_text(ctx, text, fontSize);
ui_draw_bitmap(ctx, pixels, w, h, stride, destRect);
ui_draw_push_clip(ctx, rect);
ui_draw_pop_clip(ctx);
```

## 调试工具

### 控件树导出

导出整个窗口的控件树为 JSON，包含每个控件的类型、ID、位置、尺寸、状态等信息。

```c
char* json = ui_debug_dump_tree(win);
printf("%s\n", json);
ui_debug_free(json);

// 单个控件
char* info = ui_debug_dump_widget(widget);
printf("%s\n", info);
ui_debug_free(info);
```

### 控件高亮

在指定 ID 的控件周围绘制红色边框 + 黄色内框，方便快速定位控件在界面上的位置。

```c
// 高亮 ID 为 "my_button" 的控件
ui_debug_highlight(win, "my_button");

// 清除高亮
ui_debug_highlight(win, NULL);
```

### 截图

将窗口当前画面保存为 PNG 文件。

```c
int result = ui_debug_screenshot(win, L"screenshot.png");
// result == 0 表示成功
```
