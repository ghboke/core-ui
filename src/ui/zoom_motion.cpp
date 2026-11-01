#include "zoom_motion.h"

#include <algorithm>
#include <cmath>

namespace ui::zoommotion {

namespace {

constexpr double kMinDisplacement = 1e-9;   // log 位移小于它视为不动
constexpr double kDoneEps         = 1e-9;   // 段剩余时间小于它视为走完

double SafeLog(float z) {
    return std::log(z > 1e-12f ? static_cast<double>(z) : 1e-12);
}

int Sign(double v) { return (v > 0.0) - (v < 0.0); }

void Finish(State& s) {
    s.count   = 0;
    s.active  = false;
    s.logZoom = SafeLog(s.target);
}

void AddSegment(State& s, double displacement, double durationMs) {
    if (s.count < kMaxSegments) {
        s.segs[s.count++] = Segment{durationMs, displacement / durationMs};
        return;
    }
    /* 满了: 并入最后一段, 位移守恒, 剩余时间取较长者。 */
    Segment& last = s.segs[kMaxSegments - 1];
    const double remainingDisp = last.rate * last.remainingMs;
    last.remainingMs = std::max(last.remainingMs, durationMs);
    last.rate = (remainingDisp + displacement) / last.remainingMs;
}

}  // namespace

void Push(State& s, float current, float target, float durationMs) {
    const double ms = durationMs > 1.0f ? static_cast<double>(durationMs) : 1.0;
    if (!s.active) {
        s.logZoom = SafeLog(current);
        s.target  = current;
        s.count   = 0;
    }

    const double logNew = SafeLog(target);
    const int pendingDir = Sign(Rate(s));
    const int newDir     = Sign(logNew - s.logZoom);
    double base = SafeLog(s.target);
    if (s.count > 0 && pendingDir != 0 && newDir != 0 && newDir != pendingDir) {
        /* 反向: 丢掉积压, 从当前位置出发 (Qt 原版同样在反向时清零计划量)。 */
        s.count = 0;
        base = s.logZoom;
    }

    s.target = target;
    const double displacement = logNew - base;
    if (std::fabs(displacement) < kMinDisplacement) {
        if (s.count == 0) Finish(s);
        return;
    }
    AddSegment(s, displacement, ms);
    s.active = true;
}

bool Step(State& s, double dtMs) {
    if (!s.active) return false;
    if (!(dtMs > 0.0)) return true;

    int kept = 0;
    for (int i = 0; i < s.count; ++i) {
        Segment seg = s.segs[i];
        const double take = std::min(dtMs, seg.remainingMs);
        s.logZoom += seg.rate * take;
        seg.remainingMs -= take;
        if (seg.remainingMs > kDoneEps) s.segs[kept++] = seg;
    }
    s.count = kept;
    if (s.count == 0) {
        /* 逐段累加有舍入误差, 结束时落到调用方给的原值。 */
        Finish(s);
        return false;
    }
    return true;
}

float Current(const State& s) {
    return s.active ? static_cast<float>(std::exp(s.logZoom)) : s.target;
}

float Target(const State& s) { return s.target; }

double Rate(const State& s) {
    double r = 0.0;
    for (int i = 0; i < s.count; ++i) r += s.segs[i].rate;
    return r;
}

}  // namespace ui::zoommotion
