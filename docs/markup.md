# .ui 标记文件

声明式界面描述文件，类似 XAML / QML。

## 文件结构

```xml
<ui version="1" width="900" height="600" resizable="true"
    title="App Title" tabNavigation="true">
  <style>
    /* CSS-like 样式 */
  </style>

  <VBox gap="0" expand="true">
    <!-- 界面内容 -->
  </VBox>
</ui>
```

`<ui>` 属性：

| 属性 | 说明 |
|------|------|
| `version` | 格式版本 |
| `width` / `height` | 窗口尺寸 |
| `resizable` | 可调大小 |
| `title` | 窗口标题 |
| `tabNavigation` | Tab 键切换焦点 |

## 通用属性

所有标签都支持：

| 属性 | 说明 | 示例 |
|------|------|------|
| `id` | 控件 ID | `id="myBtn"` |
| `width` / `height` | 固定尺寸 | `width="120"` |
| `minWidth` / `minHeight` | 最小尺寸 | `minWidth="200"` |
| `maxWidth` / `maxHeight` | 最大尺寸 | `maxWidth="400"` |
| `expand` | 弹性填充 | `expand="true"` |
| `flex` | 弹性权重 | `flex="2"` |
| `padding` | 内边距 | `"16"` 或 `"8,4,8,4"` (左上右下) |
| `margin` | 外边距 | `"4"` |
| `gap` | 子控件间距 | `gap="8"` |
| `bgColor` | 背景色 | `"theme.sidebarBg"` 或 `"0.2,0.2,0.2,1"` |
| `visible` | 显示/隐藏 | `visible="false"` |
| `enabled` | 启用/禁用 | `enabled="false"` |
| `tooltip` | 悬停提示 | `tooltip="帮助"` |
| `class` | 样式类 | `class="title"` |
| `onClick` | 点击事件 | `onClick="handler"` |
| `colspan` / `rowspan` | Grid 跨列/行 | `colspan="2"` |

## 样式系统

```xml
<style>
  /* 类选择器 */
  .title   { fontSize: 20; bold: true; }
  .dim     { fontSize: 12; textColor: theme.statusBarText; }

  /* 标签选择器 */
  Button   { bgColor: theme.accent; }

  /* 伪状态 */
  Button:hover   { bgColor: theme.accentHover; }
  Button:pressed { bgColor: theme.accent; }
  Button:disabled { bgColor: theme.disabledBg; }

  /* 主题色引用 */
  .sidebar { bgColor: theme.sidebarBg; }
</style>
```

### 可用主题色

| 名称 | 说明 |
|------|------|
| `theme.accent` / `theme.accentHover` | 强调色 |
| `theme.windowBg` / `theme.contentBg` | 窗口/内容区背景 |
| `theme.sidebarBg` / `theme.toolbarBg` | 侧边栏/工具栏背景 |
| `theme.titleBarBg` / `theme.titleBarText` | 标题栏 |
| `theme.btnText` / `theme.btnHover` / `theme.btnPress` | 按钮 |
| `theme.divider` | 分隔线 |
| `theme.inputBg` / `theme.inputBorder` | 输入框 |
| `theme.statusBarBg` / `theme.statusBarText` | 状态栏 |
| `theme.contentText` | 内容文字 |
| `theme.foreground1` ~ `theme.foreground4` | 前景层级 |
| `theme.background1` ~ `theme.background5` | 背景层级 |
| `theme.cardBg` / `theme.cardBorder` | 卡片 |
| `theme.disabledBg` / `theme.disabledText` | 禁用态 |

## 数据绑定

属性值用 `{变量名}` 绑定，C++ 推送值时自动更新 UI：

```xml
<Label text="{statusText}" />
<Slider value="{volume}" onChanged="onVolumeChange" />
<CheckBox text="Enable" checked="{isEnabled}" onChanged="onChanged" />
<Toggle text="Dark" on="{darkMode}" onChanged="onDarkMode" />
```

```cpp
g_layout.SetText("statusText", L"Ready");
g_layout.SetFloat("volume", 75.0f);
g_layout.SetBool("isEnabled", true);
```

## 事件处理

`.ui` 文件声明事件名，C++ 注册 handler（**必须在 LoadFile() 之前**）：

```xml
<Button text="Save" onClick="onSave" />
<Toggle text="Dark" onChanged="onDarkMode" />
<Slider min="0" max="100" onChanged="onVolume" />
<ComboBox items="A,B,C" onChanged="onSelect" />
```

```cpp
g_layout.SetHandler("onSave",     std::function<void()>([]() { /* ... */ }));
g_layout.SetHandler("onDarkMode", std::function<void(bool)>([](bool on) { /* ... */ }));
g_layout.SetHandler("onVolume",   std::function<void(float)>([](float v) { /* ... */ }));
g_layout.SetHandler("onSelect",   std::function<void(int)>([](int i) { /* ... */ }));
```

## 控件标签

### 容器

| 标签 | 说明 | 特有属性 |
|------|------|----------|
| `VBox` | 垂直布局 | `gap` |
| `HBox` | 水平布局 | `gap` |
| `Grid` | 网格布局 | `cols`, `colGap`, `rowGap` |
| `Stack` | 堆叠（显示一个子项） | `active` |
| `ScrollView` | 滚动容器 | |
| `SplitView` | 侧边导航 | `mode`, `openPaneLength`, `compactPaneLength`, `open` |
| `Panel` | 自由定位容器 | |
| `Spacer` | 占位 | `size` |
| `Separator` | 分隔线 | `vertical` |

### 控件

| 标签 | 说明 | 特有属性 |
|------|------|----------|
| `Label` | 文本 | `text`, `bold`, `fontSize`, `wrap`, `textColor` |
| `Button` | 按钮 | `text`, `type` (`"primary"`), `textColor`, `bgColor` |
| `IconButton` | 图标按钮 | `svg`, `ghost` |
| `CheckBox` | 复选框 | `text`, `checked` |
| `RadioButton` | 单选框 | `text`, `group`, `selected` |
| `Toggle` | 开关 | `text`, `on`, `onChanged` |
| `Slider` | 滑块 | `min`, `max`, `value`, `onChanged` |
| `ProgressBar` | 进度条 | `min`, `max`, `value`, `indeterminate` |
| `TextInput` | 单行输入 | `placeholder` |
| `TextArea` | 多行输入 | `placeholder` |
| `ComboBox` | 下拉选择 | `items` (逗号分隔), `onChanged` |
| `ImageView` | 图片 | |
| `TitleBar` | 标题栏 | `title` |
| `NavItem` | 导航项 | `text`, `svg`, `selected` |

### Expander

折叠/展开面板：

```xml
<Expander header="Advanced Options" expanded="true">
  <CheckBox text="Feature A" checked="true" />
  <Toggle text="Experimental" />
</Expander>
```

### NumberBox

数字输入框，带增减按钮：

```xml
<NumberBox min="0" max="100" value="42" step="1" width="120" />
<NumberBox min="0" max="1" value="0.5" step="0.1" decimals="2" width="120" />
```

### Flyout

弹出浮层，附着在锚点控件上。超出视口时自动翻转方向。

```xml
<!-- 触发按钮 -->
<Button id="flyoutBtn" text="Show Flyout" onClick="onShowFlyout" />

<!-- Flyout 放在根层级，避免被 ScrollView 裁切 -->
<Flyout id="demoFlyout" placement="bottom">
  <VBox gap="8" padding="4">
    <Label text="Title" bold="true" />
    <Button text="Dismiss" onClick="onDismiss" />
  </VBox>
</Flyout>
```

| 属性 | 说明 |
|------|------|
| `placement` | `top` / `bottom` / `left` / `right` / `auto` |

```cpp
// 显示
auto* flyout = g_layout.FindAs<ui::FlyoutWidget>("demoFlyout");
auto* anchor = g_layout.FindById("flyoutBtn");
flyout->Show(anchor);

// 隐藏
flyout->Hide();
```

## 组件引入

```xml
<Include src="components/header.ui" title="My Title" />
```

被引入文件中用 `{props.title}` 引用传入的属性。递归深度上限 8 层。

## 列表渲染

```xml
<Repeater model="{userList}">
  <HBox gap="8" height="32">
    <Label text="{item.name}" expand="true" />
    <Label text="{item.email}" />
  </HBox>
</Repeater>
```

## 查找和操作控件

```cpp
auto* stack = g_layout.FindAs<ui::StackWidget>("pages");
stack->SetActiveIndex(2);

auto* sv = g_layout.FindAs<ui::SplitViewWidget>("nav");
sv->TogglePane();

g_layout.SetText("label", L"Done");
g_layout.SetFloat("progress", 100.0f);
g_layout.SetBool("checked", true);
```

## 热重载

```cpp
g_layout.Reload();  // 重新加载 .ui 文件，自动重连事件
```
