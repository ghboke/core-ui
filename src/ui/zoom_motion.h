#pragma once
//
// zoom_motion.h — 画布平滑缩放的数值核心 (纯算术, 不碰 D2D / Win32, 有单测)。
//
// 模型: Qt "Smooth Zoom In QGraphicsView" 的做法, 改成按时间推进:
//   - 每格缩放 = 一段固定时长的匀速运动 (log(zoom) 空间里速度恒定, 视觉匀速),
//     到点硬停, 没有缓入缓出。
//   - 连续滚动时多段同时生效, 速度相加 —— 越滚越快; 每段位移固定, 所以终点
//     精确等于逐格相乘的结果。
//   - 新的一格与正在进行的方向相反: 清掉积压, 从当前位置立刻反向。
//   - 按经过的毫秒推进 (Qt 原版按帧乘系数, 掉帧会变速), 与帧率无关。
//
// 用法: Push(每格一次) → 每帧 Step(dt) 直到返回 false; Current() 取当前值。
//
namespace ui::zoommotion {

constexpr int kMaxSegments = 32;   // 超出时并入最后一段 (位移守恒)

struct Segment {
    double remainingMs = 0.0;
    double rate        = 0.0;   // d(logZoom)/ms
};

struct State {
    double  logZoom = 0.0;      // 当前 log(zoom)
    float   target  = 1.0f;     // 终点 (调用方给的原值, 结束时逐位落到它)
    Segment segs[kMaxSegments];
    int     count  = 0;
    bool    active = false;
};

// 追加一格: 从当前终点 (空闲时为 current) 匀速走到 target, 用时 durationMs。
// target 与正在进行的方向相反时清掉积压, 从当前位置出发。
// durationMs <= 0 视为 1ms; 位移可忽略时不起动画。
void Push(State& s, float current, float target, float durationMs);

// 推进 dtMs 毫秒 (<= 0 不推进)。全部段走完时落到 target、置 active=false 并返回 false。
bool Step(State& s, double dtMs);

float  Current(const State& s);
float  Target(const State& s);
double Rate(const State& s);    // 当前合速度 (各段 rate 之和), 单测用

}  // namespace ui::zoommotion
