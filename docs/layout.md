# 布局容器

## VBox / HBox

垂直/水平排列子控件，类似 CSS Flexbox。

```xml
<VBox gap="8" padding="16" expand="true" align="center" justify="spaceBetween">
  <Label text="Top" />
  <Spacer expand="true" />
  <Button text="Bottom" />
</VBox>

<HBox gap="12" height="36" align="center">
  <Label text="Name:" width="80" />
  <TextInput placeholder="..." expand="true" />
</HBox>
```

| 属性 | 说明 | 默认 |
|------|------|------|
| `gap` | 子控件间距 (px) | `4` |
| `padding` | 内边距。`"16"` 或 `"8,4,8,4"` (左上右下) | `0` |
| `align` | 交叉轴对齐：`start` `center` `end` `stretch` | `stretch` |
| `justify` | 主轴分布：`start` `center` `end` `spaceBetween` `spaceAround` | `start` |
| `expand` | 填满父容器剩余空间 | `false` |
| `flex` | 弹性权重（多个 expand 控件按 flex 比例分配） | `1` |

## Grid

网格布局，子控件按列数自动排列。

```xml
<Grid cols="2" colGap="12" rowGap="8">
  <Label text="Name" />
  <TextInput placeholder="..." />
  <Label text="Email" />
  <TextInput placeholder="..." />
</Grid>
```

| 属性 | 说明 | 默认 |
|------|------|------|
| `cols` | 列数 | `2` |
| `colGap` / `rowGap` | 列/行间距 | `0` |
| `gap` | 同时设置列行间距 | — |

子控件支持 `colspan="2"` `rowspan="2"` 跨行列。

## Stack

一次只显示一个子元素，用于页面切换。

```xml
<Stack id="pages" active="0" expand="true">
  <VBox><!-- 页面 0 --></VBox>
  <VBox><!-- 页面 1 --></VBox>
</Stack>
```

C++ 切换页面：
```cpp
auto* stack = g_layout.FindAs<ui::StackWidget>("pages");
stack->SetActiveIndex(1);
stack->DoLayout();
```

## ScrollView

可滚动容器，包裹一个子元素。滚动条宽度 4px（WinUI 3 thin 风格）。

```xml
<ScrollView expand="true">
  <VBox padding="16" gap="8">
    <!-- 内容超出时可滚动 -->
  </VBox>
</ScrollView>
```

支持鼠标滚轮和拖拽滚动条。

## Splitter

可拖拽调整比例的分割面板。

```xml
<Splitter ratio="0.3" expand="true">
  <VBox><!-- 左/上面板 --></VBox>
  <VBox><!-- 右/下面板 --></VBox>
</Splitter>
```

| 属性 | 说明 | 默认 |
|------|------|------|
| `ratio` | 分割比例 (0.05~0.95) | `0.3` |
| `vertical` | `true`=上下分割，`false`=左右分割 | `false` |

## SplitView（侧边栏导航）

WinUI 3 NavigationView 底层组件。第一个子元素为 Pane（侧边栏），第二个为 Content（内容区）。

```xml
<SplitView id="nav" mode="compactInline"
           openPaneLength="260" compactPaneLength="48"
           open="true" expand="true">
  <!-- Pane -->
  <VBox class="sidebar" padding="4" gap="2">
    <NavItem text="" svg="..." onClick="onToggle" />
    <NavItem text="Home" svg="..." onClick="onHome" selected="true" />
    <NavItem text="Settings" svg="..." onClick="onSettings" />
  </VBox>
  <!-- Content -->
  <Stack id="pages" active="0" expand="true">
    <ScrollView>...</ScrollView>
    <ScrollView>...</ScrollView>
  </Stack>
</SplitView>
```

| 属性 | 说明 | 默认 |
|------|------|------|
| `mode` | 显示模式（见下表） | `compactInline` |
| `openPaneLength` | 展开宽度 | `320` |
| `compactPaneLength` | 收起宽度（compact 模式的图标条） | `48` |
| `open` | 初始是否展开 | `false` |

### 显示模式

| mode | 关闭态 | 打开态 |
|------|--------|--------|
| `overlay` | 完全隐藏 | 滑出覆盖内容 |
| `inline` | 始终并排 | 展开推挤内容 |
| `compactOverlay` | 48px 图标条 | 展开覆盖内容 |
| `compactInline` | 48px 图标条 | 展开推挤内容 |

C++ 操作：
```cpp
auto* sv = g_layout.FindAs<ui::SplitViewWidget>("nav");
sv->TogglePane();          // 切换展开/收起
sv->SetPaneOpen(true);     // 展开（有动画）
sv->SetPaneOpenImmediate(false);  // 收起（无动画）
```

动画：展开 200ms 减速，收起 100ms 加速。Overlay 模式点击遮罩自动关闭。

## Spacer

```xml
<Spacer size="12" />           <!-- 固定 12px 间距 -->
<Spacer expand="true" />       <!-- 弹性填充剩余空间 -->
```

## Panel

带背景色的通用容器。

```xml
<Panel bgColor="theme.sidebarBg" padding="16" gap="8">
  <!-- 子控件 -->
</Panel>
```

C API 创建主题面板：
```c
ui_panel_themed(0);  // 0=sidebar, 1=toolbar, 2=content
```
