#pragma once
//
// toggle_visual.h — 开关 (ToggleWidget) 悬停 / 按下状态的几何与配色 (纯算术, 有单测)。
//
// 对标 WinUI 3 ToggleSwitch (ToggleSwitch_themeresources.xaml):
//   - 圆点: 常态 12x12, 悬停 14x14, 按住 17x14; 状态过渡 83ms, cubic-bezier(0,0,0,1)
//     (ControlFasterAnimationDuration + ControlFastOutSlowInKeySpline)。
//   - 按住时圆点贴住当前一侧, 朝要去的那一侧拉长 (关闭态往右, 开启态往左)。
//   - 轨道: 关闭态底色逐级加深 (ControlAltFillColor Secondary → Tertiary → Quarternary,
//     每级约 9/255 的黑, 深色主题为白); 开启态主题色透明度 100% / 90% / 80%
//     (AccentFillColor Default / Secondary / Tertiary)。
//   - 按住后拖出开关: 退回常态。
//
namespace ui::togglevisual {

constexpr float kStateDurationMs = 83.0f;
constexpr float kKnobHoverSize   = 14.0f;

enum class Interaction { Normal, Hover, Pressed };

struct KnobSize { float w = 0.0f, h = 0.0f; };
struct KnobRect { float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f, radius = 0.0f; };
struct Rgba { float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f; };

Interaction ResolveInteraction(bool hovered, bool pressed);
KnobSize    TargetKnobSize(Interaction s);
// 轨道颜色强度: 常态 0, 悬停 1, 按住 2 (动画在这个数上插值)。
float       InteractionLevel(Interaction s);

// cx / cy: 圆点中心 (cx 随开关位移动画走); 宽超过悬停尺寸的部分朝 extendRight 一侧长出去。
KnobRect ComputeKnobRect(float cx, float cy, float w, float h, bool extendRight);

Rgba OffTrackColor(const Rgba& base, float level, bool dark);
Rgba OnTrackColor(const Rgba& accent, float level);

// 圆点滑动这一程的时长比例: 按要走的距离 (0~1, 一整程为 1) 等比缩短, 不短于 83ms,
// 不长于整程。中途掉头 (连点打断) 时走一小段不用耗满整程, 连点更跟手。
float SlideDurationScale(float distance, float fullDurationMs);

}  // namespace ui::togglevisual
