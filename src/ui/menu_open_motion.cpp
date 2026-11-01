#include "menu_open_motion.h"

#include <algorithm>
#include <cmath>

namespace ui::menumotion {

namespace {

constexpr double kMinSegmentSec = 1e-9;

double BezierCoord(double p1, double p2, double s) {
    const double u = 1.0 - s;
    return 3.0 * u * u * s * p1 + 3.0 * u * s * s * p2 + s * s * s;
}

double BezierDeriv(double p1, double p2, double s) {
    const double u = 1.0 - s;
    return 3.0 * u * u * p1 + 6.0 * u * s * (p2 - p1) + 3.0 * s * s * (1.0 - p2);
}

// x(s) 在 s∈[0,1] 上单调 (控制点 x 都在 [0,1]), 二分求 s。
double SolveParam(double x1, double x2, double x) {
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 60; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (BezierCoord(x1, x2, mid) < x) lo = mid; else hi = mid;
    }
    return 0.5 * (lo + hi);
}

}  // namespace

double BezierProgress(double x1, double y1, double x2, double y2, double x) {
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;
    return BezierCoord(y1, y2, SolveParam(x1, x2, x));
}

std::vector<CubicSegment> EaseOutSegments(double durationSec, double from, double to,
                                          int count) {
    std::vector<CubicSegment> segs;
    if (durationSec <= 0.0 || count < 1) return segs;

    // 曲线 cubic-bezier(0, 0, 0, 1)。按贝塞尔参数 s 等分取节点: 起步段曲率大,
    // 这样取出来的时间节点自然在开头更密。
    constexpr double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 1.0;
    std::vector<double> xs(count + 1), ys(count + 1), slopes(count + 1);
    for (int i = 0; i <= count; ++i) {
        const double s = static_cast<double>(i) / count;
        xs[i] = BezierCoord(x1, x2, s);
        ys[i] = BezierCoord(y1, y2, s);
    }
    for (int i = 0; i <= count; ++i) {
        const double s = static_cast<double>(i) / count;
        const double dx = BezierDeriv(x1, x2, s);
        if (dx > 1e-6) {
            slopes[i] = BezierDeriv(y1, y2, s) / dx;
        } else {
            // 起点切线竖直 (dy/dx → ∞), 用相邻弦的斜率代替。
            const int j = (i < count) ? i + 1 : i - 1;
            slopes[i] = (ys[j] - ys[i]) / (xs[j] - xs[i]);
        }
    }

    const double span = to - from;
    segs.reserve(count);
    for (int i = 0; i < count; ++i) {
        const double h = (xs[i + 1] - xs[i]) * durationSec;
        if (h < kMinSegmentSec) continue;
        const double v0 = from + span * ys[i];
        const double v1 = from + span * ys[i + 1];
        const double m0 = span * slopes[i] / durationSec;
        const double m1 = span * slopes[i + 1] / durationSec;
        const double secant = (v1 - v0) / h;
        CubicSegment seg;
        seg.beginSec = xs[i] * durationSec;
        seg.c0 = v0;
        seg.c1 = m0;
        seg.c2 = (3.0 * secant - 2.0 * m0 - m1) / h;
        seg.c3 = (m0 + m1 - 2.0 * secant) / (h * h);
        segs.push_back(seg);
    }
    return segs;
}

double EvalSegments(const std::vector<CubicSegment>& segs, double durationSec, double to,
                    double tSec) {
    if (segs.empty() || tSec >= durationSec) return to;
    if (tSec <= segs.front().beginSec) return segs.front().c0;
    const CubicSegment* cur = &segs.front();
    for (const CubicSegment& s : segs) {
        if (s.beginSec > tSec) break;
        cur = &s;
    }
    const double t = tSec - cur->beginSec;
    return cur->c0 + t * (cur->c1 + t * (cur->c2 + t * cur->c3));
}

OpenGeometry ComputeOpenGeometry(double windowH, double marginPx, double cardH, bool upward,
                                 double closedRatio) {
    closedRatio = std::clamp(closedRatio, 0.0, 1.0);
    OpenGeometry g;
    g.clipTopFrom = g.clipTopTo = 0.0;
    g.clipBottomFrom = g.clipBottomTo = windowH;
    if (upward) {
        g.clipTopFrom = windowH - marginPx - closedRatio * cardH;
    } else {
        g.clipBottomFrom = marginPx + closedRatio * cardH;
    }
    return g;
}

}  // namespace ui::menumotion
