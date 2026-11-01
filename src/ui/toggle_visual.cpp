#include "toggle_visual.h"

#include <algorithm>

namespace ui::togglevisual {

namespace {

constexpr float kOffStepPerLevel = 9.0f / 255.0f;   // ControlAltFill 相邻两级的差
constexpr float kOnAlphaPerLevel = 0.1f;            // AccentFill 100% → 90% → 80%

}  // namespace

Interaction ResolveInteraction(bool hovered, bool pressed) {
    if (!hovered) return Interaction::Normal;
    return pressed ? Interaction::Pressed : Interaction::Hover;
}

KnobSize TargetKnobSize(Interaction s) {
    switch (s) {
        case Interaction::Hover:   return {kKnobHoverSize, kKnobHoverSize};
        case Interaction::Pressed: return {17.0f, kKnobHoverSize};
        default:                   return {12.0f, 12.0f};
    }
}

float InteractionLevel(Interaction s) {
    switch (s) {
        case Interaction::Hover:   return 1.0f;
        case Interaction::Pressed: return 2.0f;
        default:                   return 0.0f;
    }
}

KnobRect ComputeKnobRect(float cx, float cy, float w, float h, bool extendRight) {
    const float base = std::min(w, kKnobHoverSize);
    const float extra = std::max(0.0f, w - kKnobHoverSize);
    KnobRect r;
    r.left = cx - base / 2.0f;
    r.right = cx + base / 2.0f;
    if (extendRight) r.right += extra; else r.left -= extra;
    r.top = cy - h / 2.0f;
    r.bottom = cy + h / 2.0f;
    r.radius = std::min(r.right - r.left, h) / 2.0f;
    return r;
}

Rgba OffTrackColor(const Rgba& base, float level, bool dark) {
    const float a = std::clamp(level, 0.0f, 2.0f) * kOffStepPerLevel;
    const float k = dark ? 1.0f : 0.0f;
    return {base.r + (k - base.r) * a, base.g + (k - base.g) * a,
            base.b + (k - base.b) * a, base.a};
}

float SlideDurationScale(float distance, float fullDurationMs) {
    if (fullDurationMs <= 0.0f) return 1.0f;
    const float minMs = std::min(kStateDurationMs, fullDurationMs);
    const float ms = std::clamp(fullDurationMs * distance, minMs, fullDurationMs);
    return ms / fullDurationMs;
}

Rgba OnTrackColor(const Rgba& accent, float level) {
    const float alpha = 1.0f - std::clamp(level, 0.0f, 2.0f) * kOnAlphaPerLevel;
    return {accent.r, accent.g, accent.b, accent.a * alpha};
}

}  // namespace ui::togglevisual
