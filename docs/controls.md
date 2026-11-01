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

### 垂直居中（build 17+）

Label 文字始终垂直居中 (`DWRITE_PARAGRAPH_ALIGNMENT_CENTER`)，无论 wrap
开关。这样在 HBox 里跟 icon / input / button 等几何居中的兄弟节点放一起，
肉眼看到的 `align-items: center` 就是真的中心对齐。

之前 `wrap=true` 走 NEAR 顶对齐（DirectWrite 的 line-height 比纯字形高，
slack 全堆底部 → 文字钉在 rect 顶 → 跟 icon 错位）。多行段落场景：
LabelWidget 的 SizeHint 紧贴内容块（`textH + 6`），CENTER ≈ NEAR，不会
让段落塌进 rect 中间。

`.uix` 模板的 widget factory 把 `<label>/<span>/<a>/<small>/<strong>/<em>/<p>` 全
`SetWrap(true)`，所以这个修复对所有模板文本节点生效。

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

密码模式：`<TextInput password="true" />`（.uix 页面写 `<input type="password">`），每个字符显示成 `●`，Ctrl+C / Ctrl+X 不把内容
放进剪贴板（可以粘贴），`ui_text_input_get_text` 仍返回明文。

C API：
```c
ui_text_input_set_text(w, L"text");
ui_text_input_select_all(w);          // 全选 (input.select()), 之后输入整体替换
ui_text_input_set_password(w, 1);     // 密码模式
const wchar_t* t = ui_text_input_get_text(w);  // 内部指针，不要 free
```

## TextArea（多行输入）

同 TextInput 功能 + 回车换行、鼠标滚轮滚动、上下方向键跨行。

```xml
<TextArea placeholder="Type notes..." height="120" expand="true" />
```

`.uix` 模板里：

```vue
<textarea v-model="multi" placeholder="..." wrap="soft"/>
```

`wrap` 默认 `soft`（自动换行，匹配 DOM `<textarea>` 默认）；显式 `wrap="off"` /
`wrap="hard"` / `wrap="false"` / `wrap="0"` 关掉换行（超出宽度截断 + 单行省略号）。

### 实现说明（build 15+）

整个 TextArea 共享同一个 `IDWriteTextLayout` —— hit-test、caret 定位、
selection 渲染、文字 draw 全走这一个 layout，避免之前 per-substring
`MeasureTextWidth` 累加跟整行 shaping/kerning 不一致导致的"光标比鼠标点击位置后一些"
问题。

- **选区高亮**：`HitTestTextRange` 拿每行的精确字形矩形（wrap 后的视觉行，
  不是逻辑 `\n` 行），多行选区也精确
- **选中文字色**：focused 时默认白字（accent 蓝底），可被
  `selection-color` / `selection-inactive-color` CSS 属性覆盖（自 build 16）

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

// 与 min/max/close 同款的控制按钮, 贴在最小化左侧, 可加多个 (先加的离最小化最近)
UiWidget menuBtn = ui_titlebar_add_caption_button(tb, L"\xE700");  // MDL2 汉堡
ui_widget_on_click(menuBtn, on_menu_click, ud);
ui_titlebar_caption_button_set_glyph(menuBtn, L"\xE712");          // 换字形
ui_titlebar_caption_button_set_svg(menuBtn,                        // 或改用 SVG (16×16, currentColor)
    "<svg viewBox=\"0 0 16 16\"><path stroke=\"currentColor\" d=\"M3 4.5h10M3 8h10M3 11.5h10\"/></svg>");
ui_titlebar_caption_button_set_badge(menuBtn, 1);                  // 右上角红点
ui_window_invalidate(win);
```

布局 (右→左): close · max · min · 控制按钮 (紧贴, 无间隙) · 4px · `add_widget` 控件。

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

## SVG（.uix 页面子系统）

`<svg>` 元素映射到 `SvgWidget`，子图形（`<path>` `<circle>` `<rect>` `<ellipse>`
`<line>` `<polygon>` `<polyline>`）fold 进 widget 的 `SvgShape` 表，不作为独立
widget。viewport 由 `<svg width height viewBox>` 决定，widget rect 由 CSS
`width`/`height` 控制（缺省=viewport size）。

```html
<svg class="my-icon" width="24" height="24" viewBox="0 0 24 24">
  <path d="M12 17.27L18.18 21..."/>
</svg>
```

### CSS 命中（自 build 12 起，匹配浏览器语义）

```css
.my-icon path           { fill: var(--accent); }
.my-icon path           { fill: currentColor; }    /* 沿父链取最近 color */
.parent.active .icon path { fill: red; }           /* descendant + class 切 */
.btn:hover .icon path   { fill: blue; }            /* hover 时 path 实时重算 */
```

支持的 CSS 属性：`fill`, `stroke`, `fill-opacity`, `stroke-opacity`,
`stroke-width`, `stroke-dasharray`, `stroke-linecap`, `stroke-linejoin`, `opacity`.

优先级：inline `style="fill:..."` > stylesheet rule > presentation attr `fill="..."` > 默认黑。

`currentColor` 在父链上找最近的 `color: ...` 设置（库无完整 CSS inherit 系统，
只对 `currentColor` 关键字这一个 case 走父链查找）。

### 不支持

`<g>` 分组、`<filter>`、`<mask>`、`<clipPath>`、SMIL `<animate>`，path 上的
`v-if` / `v-for` / `:class`（path 不是 widget）。

## Image（.uix 页面子系统，自 build 19）

`.uix` 模板里 `<img src="...">` 走的轻量级位图 widget。**src 是资源解析器的 key
而不是文件路径** —— 上层应用通过 `ui_asset_register_dir/blob/resolver`
决定它怎么解析（详见 [c-api.md 资源解析器](c-api.md#资源解析器自-140-build-19)）。

```html
<img src="logo.png" width="64" height="64" object-fit="contain"/>
```

`object-fit` 跟 CSS 同名属性对齐：

| 值 | 行为 |
|---|---|
| `fill` | 拉伸填满 rect（不保持纵横比）|
| `contain`（默认）| 保持纵横比，整张图都可见，rect 多余部分留白 |
| `cover` | 保持纵横比，整个 rect 都被填满，超出部分裁剪 |
| `none` | 1:1 不缩放，左上角对齐，超出 rect 裁剪 |

支持的格式跟 WIC 解码器一致（PNG / JPG / BMP / GIF 静态帧 / ICO 多尺寸自动选最大）。
内存解码走 `IWICStream::InitializeFromMemory`，跟 ImageView 共用 strip-decode
路径（峰值内存 ≈ 一张位图 + 一条带缓冲）。

跟 ImageView 的区别：

|  | `<img>` (ImageWidget) | `<ImageView>` (ImageViewWidget) |
|---|---|---|
| 用途 | 静态图标 / 装饰图片 | 图片查看器（缩放 / 平移 / 旋转）|
| 缩放 / 拖动 | 无 | 有 |
| GIF 动画 | 静态首帧 | 完整时间轴 |
| SVG 矢量 | 无 | 有（image_view_plus）|
| 资源来源 | ui::asset 名字解析 | 文件路径或 `ui_image_set_pixels` |

复杂 SVG 用 `<ImageView>` 加载 SVG 文件；简单 path 直接 `<svg>` 内联。

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

### 锚点缩放（`set_zoom_around`，build 49+）

`ImageViewPlus`（`ui_image_view_plus_*` 系列）支持用指定的 widget 像素
坐标作为锚点缩放 —— 锚点处的图像像素位置在 zoom 前后保持不变。

```c
/* C API：以 widget 像素坐标 (anchor_x, anchor_y) 为锚点缩放
   到 new_zoom。anchor 处的图像像素在 zoom 前后保持不动。 */
ui_image_view_plus_set_zoom_around(canvas, new_zoom, anchor_x, anchor_y);
```

典型用途：**reload 后保持鼠标锚点**。

```c
/* 1. 用户在画布右上角 wheel 放大 → 触发 viewport_changed callback。
   2. 应用 reload (切高 pyramid level / 换图)，记录鼠标当前位置 (mx, my)
      和目标 zoom。
   3. reload 完后调 set_zoom_around 让鼠标下的图像位置不动： */
void on_viewport_changed(UiWidget canvas, float zoom, float panX, float panY,
                         void* user) {
    if (zoom > kHighResThreshold) {
        load_higher_pyramid_level(canvas);   /* 应用自己的 reload 逻辑 */

        /* reload 后图重置到 fit；用户希望鼠标位置仍是同一图像点：*/
        float mx, my;
        get_mouse_relative_to_canvas(&mx, &my);
        ui_image_view_plus_set_zoom_around(canvas, zoom, mx, my);
    }
}
```

该 API 内部用的算法跟原生 wheel 缩放完全一致。手动 `set_zoom + set_pan`
组合在锚点不在 canvas center 时会有视觉跳跃，用 `set_zoom_around` 一行
解决。

## Dialog（模态对话框）

**窗口级 overlay，不进 widget 树**。`ui_dialog_show` 把 dialog 注册为窗口的
`activeDialog_`，由 `UiWindowImpl::OnPaint` 在所有 widget / overlay 之上绘制，
mouse / wheel 事件在窗口入口被拦截直接派给 dialog（真 modal，下层按钮无法点）。
`.uix` 与 `.ui` 通用——无 markup tag，纯 C 命令调用即可。

```c
UiWidget dlg = ui_dialog();
ui_dialog_set_ok_text(dlg, L"确定");
ui_dialog_set_cancel_text(dlg, L"取消");
ui_dialog_set_show_cancel(dlg, 1);

void on_result(UiWidget dlg, int confirmed, void* ud) { }
ui_dialog_show(dlg, win, L"确认", L"确定删除？", on_result, data);
```

**主题模式**：默认跟随全局 `theme::Current()`（`ui_theme_set_mode` 设的那个），
可单独覆盖：

```c
ui_dialog_set_theme_mode(dlg, 0);   // 0 = 跟随全局（默认）
ui_dialog_set_theme_mode(dlg, 1);   // 1 = 强制 light
ui_dialog_set_theme_mode(dlg, 2);   // 2 = 强制 dark
```

不要 `ui_widget_add_child` 把 dialog 加到任何容器里——加了会被 flex 当普通子项
分空间 + 同时被 overlay 路径再画一次（双重绘制）。一个 dialog 实例可以反复 `show`
+ `show`（模板 / 文案 / 主题模式只需配一次）。

## Toast（通知）

```c
ui_toast(win, L"已保存", 2000);                                    // 顶部 + slide + 无图标
ui_toast_ex(win, L"提示", 3000, 0, 0, UI_TOAST_ANIM_SLIDE);        // 0=顶 1=中 2=底
ui_toast_ex(win, L"成功", 2000, 2, 1, UI_TOAST_ANIM_SLIDE);        // 图标: 1=✓ 2=✕ 3=⚠
ui_toast_ex(win, L"已复制", 1500, 1, 0, UI_TOAST_ANIM_FADE);       // 中央 + 纯渐入渐出
```

动画风格 `UI_TOAST_ANIM_SLIDE`(默认): top/bottom 滑入, center 直现, 退出淡出.
`UI_TOAST_ANIM_FADE`: 不论位置全程渐入渐出, y 不动 — 适合不希望干扰用户视线
的轻量提示 (例如复制成功、设置已应用), 或中央位置希望有进入动画的场景.

## ContextMenu（右键菜单）

### C API

```c
UiMenu menu = ui_menu_create();
ui_menu_add_item(menu, 1, L"Item");
ui_menu_add_item_ex(menu, 2, L"Cut", L"Ctrl+X", svg_icon);   // SVG icon (跟文字色)
ui_menu_add_item_bitmap(menu, 3, L"Logo", L"", bitmap);      // PNG/JPG (不变色, 自 build 26)
ui_menu_set_last_item_color(menu, color);                    // per-item 颜色 (自 build 26)
ui_menu_add_separator(menu);

UiMenu sub = ui_menu_create();
ui_menu_add_item(sub, 10, L"Sub Item");
ui_menu_add_submenu(menu, L"More", sub);

ui_menu_set_enabled(menu, 99, 0);
ui_menu_set_bg_color(menu, (UiColor){1.0f, 1.0f, 1.0f, 0.52f});  // 半透明 tint
ui_menu_set_backdrop_blur(menu, 18.0f);                       // 0 关闭, <0 默认
ui_menu_show(win, menu, x, y);
ui_menu_close(win);
ui_menu_destroy(menu);
```

### `.uix` 模板（自 build 22-27）

```vue
<menu trigger="#fileBtn" event="click">
  <menuitem id="1" onclick="onSave" shortcut="Ctrl+S">
    <svg viewBox="0 0 24 24"><path fill="currentColor" d="..."/></svg>
    Save
  </menuitem>
  <separator/>
  <menu text="Recent">                                   <!-- submenu -->
    <menuitem id="10" onclick="onOpen">file1.txt</menuitem>
  </menu>
  <separator/>
  <menuitem id="2" onclick="onQuit" style="color: #d63a26">Quit</menuitem>
</menu>
```

声明式 trigger 自动挂载，`onclick` 自动派发到 `<script>` methods，C 端零代码。
完整参考见 [uix-guide.md §17](uix-guide.md#17-menu-完整参考自-build-22-27)。

### 反应式菜单（自 1.6.0 build 73-89）

1.6.0 把 `<menu>` 重构成全反应式 widget，对齐 Vue 3：menuitem 内容、enabled
态、是否显示都跟 `data()` 状态绑定，rclick 弹出时直接读最新值，不需要手动
"先关菜单 → 改 menu → 再弹"。

```vue
<script>
export default {
  data() {
    return {
      commands: [
        { id: 'copy',  label: 'Copy',  enabled: true  },
        { id: 'paste', label: 'Paste', enabled: false },
      ],
      isAdmin: true
    };
  },
  methods: {
    run(cmd) { /* ... */ },
    del()    { /* ... */ }
  }
}
</script>

<template>
  <div id="tree-item">…</div>

  <menu trigger="#tree-item" event="rclick">
    <menuitem v-for="cmd in commands" :key="cmd.id"
              :disabled="!cmd.enabled" @click="run(cmd)">
      <label>{{ cmd.label }}</label>
    </menuitem>
    <separator/>
    <menuitem v-if="isAdmin" @click="del">
      <label style="color: #d63a26">删除</label>
    </menuitem>
  </menu>
</template>
```

**关键改动 (从 1.5.0 升级时需注意)：**

- **`menuitem` 是 widget 模板** (BREAKING, build 75)：必须在内部包一个子 widget
  (一般 `<label>`)，不能直接 `<menuitem>X</menuitem>`
- **`WireMenus` 静态简化路径已删** (BREAKING, build 73)：menu 必须走 `<menu>`
  声明式 + Vue 3 binding，不能再走 C 端 `WireMenus(...)`
- **rclick dispatch 改 deepest-match** (build 107-108)：嵌套 `<menu trigger>` 时
  子 widget 的 menu 不再被祖先抢走。下面的写法现在能正常工作——之前 minimap
  右键会被 canvas_body 抢走：

  ```vue
  <div id="canvas_body">
    <div id="minimap"></div>
  </div>

  <menu trigger="#canvas_body" event="rclick">…</menu>   <!-- 父 -->
  <menu trigger="#minimap"     event="rclick">…</menu>   <!-- 子, 现在能弹 -->
  ```

- **submenu autoclose 用"可见矩形"判定** (build 87)：之前用 hwnd 整框，鼠标
  滑到不可见区域 menu 会"卡住"不关
- **submenu 箭头改 outline + 跟主菜单同宽** (build 78-86)

### 特性

- SVG 图标（`<svg fill="currentColor">` 跟文字色一起变）
- PNG/JPG 图标（走 `ui_asset_register_*` 资源解析器）
- 子菜单（嵌套 `<menu text="...">`）
- per-item 颜色覆盖（`style="color: ..."`）
- 快捷键提示（`shortcut="Ctrl+S"`，仅显示，实际绑定走 `ui_window_on_key`）
- 菜单树行样式覆盖（`row-class="..."` 会追加到自动生成的 `.menuitem-row`）
- 可选关闭父子菜单同宽（`share-width="false"`）
- 禁用项 / 自动定位 / 鼠标 hover submenu 展开

### 自动化

`ui_debug_screenshot_menu(win, L"menu.png")` / `screenshot_menu <path>` —
直接读 popup RT 输出 PNG，验证 icon / 颜色 / submenu 实际渲染（自 build 27）。

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
