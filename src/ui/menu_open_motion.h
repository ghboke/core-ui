#pragma once
//
// menu_open_motion.h — 弹出菜单打开动画的数值核心 (纯算术, 不碰 DComp / Win32, 有单测)。
//
// 参照 WinUI 3 MenuFlyout 的 MenuPopupThemeTransition (时长 / 曲线 / 淡入 / 方向照抄),
// 展开方式按用户定的"画卷摊开" (2026-10-19):
//   - 时长 250ms, 曲线 cubic-bezier(0, 0, 0, 1);
//   - 内容不动, 只有裁剪框的下边缘从卡片顶边 (ClosedRatio 0) 推到底, 推到哪露到哪;
//     WinUI 原版是从半高起步、内容跟着滑, 用户看过觉得像"整个出现", 不要;
//   - 透明度 0 → 1 线性, 83ms;
//   - 菜单落在锚点上方时反过来, 上边缘从卡片底边往上推。
//
// DComp 动画曲线只能是分段三次多项式 (AddCubic), 所以贝塞尔曲线在这里拟合成若干段
// Hermite 三次, 交给合成器按垂直同步插值。
//
#include <vector>

namespace ui::menumotion {

constexpr double kOpenDurationMs = 250.0;
constexpr double kFadeDurationMs = 83.0;
constexpr double kClosedRatio    = 0.0;   // 起步时已露出的卡片比例
constexpr int    kCurveSegments  = 12;

// cubic-bezier(x1, y1, x2, y2) 在时间进度 x (0..1) 处的值进度。
double BezierProgress(double x1, double y1, double x2, double y2, double x);

// 一段 DComp 三次: value = c0 + c1*t + c2*t^2 + c3*t^3, t 是相对 beginSec 的秒数。
struct CubicSegment {
    double beginSec = 0.0;
    double c0 = 0.0, c1 = 0.0, c2 = 0.0, c3 = 0.0;
};

// 把 from → to 按 cubic-bezier(0,0,0,1) 走 durationSec 秒, 拟合成 count 段三次。
// durationSec <= 0 或 count < 1 返回空 (调用方直接落终值)。
std::vector<CubicSegment> EaseOutSegments(double durationSec, double from, double to,
                                          int count = kCurveSegments);

// 按段求 tSec 时刻的值 (单测用; 合成器那边是 DComp 自己算)。tSec >= durationSec 给 to。
double EvalSegments(const std::vector<CubicSegment>& segs, double durationSec, double to,
                    double tSec);

// 一次打开动画的几何 (都是窗口内像素)。窗口 = 卡片 + 四周 marginPx 阴影边。
// 裁剪框左右不动 (整个窗口宽), 只动一条边; 内容不位移。
struct OpenGeometry {
    double clipTopFrom = 0.0, clipTopTo = 0.0;
    double clipBottomFrom = 0.0, clipBottomTo = 0.0;
};

// windowH: 窗口高 (含阴影边); cardH: 卡片高; upward: 菜单在锚点上方, 从下往上展开。
OpenGeometry ComputeOpenGeometry(double windowH, double marginPx, double cardH, bool upward,
                                 double closedRatio = kClosedRatio);

}  // namespace ui::menumotion
