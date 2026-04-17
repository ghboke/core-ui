# 设计系统

基于微软官方 `@fluentui/tokens`，控件样式对齐 WinUI 3 `microsoft-ui-xaml` 源码。

## 色彩

### Brand 色阶

```
shade50(#061724) → shade40 → shade30 → shade20 → shade10
→ primary(#0f6cbd)
→ tint10 → tint20 → tint30 → tint40 → tint50 → tint60(#ebf3fc)
```

### 灰阶

33 级中性色 `grey::g2`(#050505) → `grey::g98`(#fafafa)

### 语义 Token

深浅色自动切换：

| Token 类别 | 包含 |
|-----------|------|
| 前景 | foreground1(主文字) ~ foreground4(四级), foregroundOnBrand |
| 背景 | background1(主背景) ~ background5 |
| 控件 | btnNormal/Hover/Press, inputBg/Border/BorderFocus |
| 卡片 | cardBg, cardBorder |
| 强调 | accent, accentHover, accentPress, accentText, accentSelected |
| 侧边 | sidebarBg, sidebarItemHover, sidebarText |
| 禁用 | disabledBg, disabledText |
| 阴影 | shadowAmbient, shadowKey |

### 状态色

| 状态 | 色值 |
|------|------|
| danger | #d13438 |
| success | #107c10 |
| warning | #fde300 |
| info | #0078d4 |

## 字体

Segoe UI，10 级 type ramp：

| 样式 | 大小 | 常量名 | 用途 |
|------|------|--------|------|
| Caption2 | 10px | `kFontSizeCaption2` | 极小标注 |
| Caption | 12px | `kFontSizeCaption` | 辅助文字 |
| Body | 14px | `kFontSizeBody` | 正文（默认） |
| Body2 | 16px | `kFontSizeBody2` | 较大正文 |
| Subtitle | 20px | `kFontSizeSubtitle` | 副标题 |
| Title3 | 24px | `kFontSizeTitle3` | 三级标题 |
| Title2 | 28px | `kFontSizeTitle2` | 二级标题 |
| Title1 | 32px | `kFontSizeTitle1` | 一级标题 |
| Large | 40px | `kFontSizeLarge` | 大标题 |
| Display | 68px | `kFontSizeDisplay` | 展示 |

## 间距

```
namespace spacing:
  none(0)  xxs(2)  xs(4)  sNudge(6)  s(8)
  mNudge(10)  m(12)  l(16)  xl(20)  xxl(24)  xxxl(32)
```

## 圆角

```
namespace radius:
  none(0)  small(2)  medium(4)  large(6)
  xLarge(8)  xxLarge(12)  circular(9999)
```

| 用途 | 值 |
|------|------|
| 控件（Button/TextBox/CheckBox） | 4px (medium) |
| 弹出层（Dialog/Dropdown/Menu） | 8px (xLarge) |
| 卡片 | 8px |
| 圆形元素 | 9999px |

## 描边

```
namespace stroke:
  thin(1)  thick(2)  thicker(3)  thickest(4)
```

## 阴影

6 级，每级含 ambient（环境光）+ key（方向光）双层：

```
namespace shadow:
  s2  { ambientBlur:2,  keyOffsetY:1,  keyBlur:2  }
  s4  { ambientBlur:2,  keyOffsetY:2,  keyBlur:4  }
  s8  { ambientBlur:2,  keyOffsetY:4,  keyBlur:8  }
  s16 { ambientBlur:2,  keyOffsetY:8,  keyBlur:16 }
  s28 { ambientBlur:8,  keyOffsetY:14, keyBlur:28 }
  s64 { ambientBlur:8,  keyOffsetY:32, keyBlur:64 }
```

阴影颜色从 `Current().shadowAmbient` / `Current().shadowKey` 读取（深浅色不同透明度）。

## 动画

### 时长

```
namespace duration:
  ultraFast(50ms)  faster(100ms)  fast(150ms)
  normal(200ms)    gentle(250ms)  slow(300ms)
  slower(400ms)    ultraSlow(500ms)
```

### 缓动曲线

9 条 cubic-bezier：

| 名称 | 控制点 | 用途 |
|------|--------|------|
| decelerateMax | (0.1, 0.9, 0.2, 1.0) | 快入慢出（最强） |
| decelerateMid | (0.0, 0.0, 0.0, 1.0) | 快入慢出（中） |
| decelerateMin | (0.33, 0.0, 0.1, 1.0) | 快入慢出（最弱） |
| accelerateMax | (0.9, 0.1, 1.0, 0.2) | 慢入快出（最强） |
| accelerateMid | (1.0, 0.0, 1.0, 1.0) | 慢入快出（中） |
| accelerateMin | (0.8, 0.0, 0.78, 1.0) | 慢入快出（最弱） |
| easyEaseMax | (0.8, 0.0, 0.2, 1.0) | 对称缓动（最强） |
| easyEase | (0.33, 0.0, 0.67, 1.0) | 对称缓动 |
| linear | (0.0, 0.0, 1.0, 1.0) | 匀速 |

### 控件内置动画

| 控件 | 动画 |
|------|------|
| CheckBox | 200ms 勾选/取消动画 |
| RadioButton | 200ms 内点缩放 |
| Toggle | 200ms 滑块移动 + 轨道变色 |
| Slider | 指数缓动 thumb 缩放（hover/press） |
| ProgressBar | 300ms 值变化 + 888ms 不确定模式 |
| SplitView | 200ms 展开 / 100ms 收起 |
| Dialog | 窗口开/关动画 |
| Toast | 滑入/滑出动画 |

## C++ 动画 API

```cpp
AnimationManager& anims = ui::Animations();

anims.Animate(widget, AnimProperty::Opacity, 0.0f, 1.0f,
              300.0f, EasingFunction::EaseOutCubic, []() { /* done */ });

anims.FadeIn(widget, 200.0f);
anims.FadeOut(widget, 200.0f);
anims.Cancel(widget);
```

可动画属性：`Opacity` `PosX` `PosY` `Width` `Height` `BgColorR/G/B/A`

缓动函数：`Linear` `EaseInQuad` `EaseOutQuad` `EaseInOutQuad` `EaseInCubic` `EaseOutCubic` `EaseInOutCubic` `EaseInElastic` `EaseOutElastic` `EaseInBounce` `EaseOutBounce` `EaseOutBack`
