#pragma once
//
// anim_clock.h — 动画统一时钟。
//
// 所有逐帧动画 (AnimatedFloat、CSS transition、转圈 / 不确定进度条) 都从这里取时间。
// 基于 QueryPerformanceCounter, 1ms 步长; GetTickCount64 只有 15.6ms 粒度, 用它采样
// 动画每帧走的距离会忽大忽小。单位仍是毫秒, 可直接替换 GetTickCount64。
//
// 约定: 返回值永远不为 0 (多处用 0 表示"动画还没开始")。
// 不要拿它和 GetTickCount64 的时间戳相减 —— 两者起点不同。光标闪烁、撤销合并等
// 非动画计时继续用 GetTickCount64。
//
#include <cstdint>

namespace ui {

uint64_t AnimNowMs();

// 转圈角度 (度), 只看时间: 750ms 一圈 (= 原来 60fps 下每帧 +8°)。
// 以前每次绘制加 8°, 重绘一多 (鼠标晃动 / 多路定时器) 就转得飞快。
inline float SpinnerAngleDeg(uint64_t nowMs) {
    constexpr uint64_t kPeriodMs = 750;
    return (float)(nowMs % kPeriodMs) * 360.0f / (float)kPeriodMs;
}

}  // namespace ui
