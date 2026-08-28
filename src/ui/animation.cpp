#include "animation.h"
#include "widget.h"
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace ui {

// ---- AnimatedFloat ----

void AnimatedFloat::SetImmediate(float value) {
    current_ = value;
    target_ = value;
    start_ = value;
    startTick_ = 0;
    active_ = false;
}

void AnimatedFloat::Retarget(float value) {
    Retarget(value, spec_);
}

void AnimatedFloat::Retarget(float value, const AnimationSpec& spec) {
    spec_ = spec;
    if (target_ == value && active_) return;
    if (target_ == value && !active_) {
        current_ = value;
        start_ = value;
        startTick_ = 0;
        return;
    }
    target_ = value;
    start_ = current_;
    startTick_ = 0;
    active_ = true;
}

void AnimatedFloat::SetIdleTarget(float value) {
    if (!active_) SetImmediate(value);
}

float AnimatedFloat::Sample(uint64_t now) {
    if (!active_) {
        current_ = target_;
        start_ = target_;
        startTick_ = 0;
        return current_;
    }

    if (startTick_ == 0) {
        startTick_ = now;
        start_ = current_;
    }

    float u = spec_.durationMs > 0.0f
        ? std::clamp((float)(now - startTick_) / spec_.durationMs, 0.0f, 1.0f)
        : 1.0f;
    float eased = ApplyEasing(spec_.easing, u);
    current_ = start_ + (target_ - start_) * eased;

    if (u >= 1.0f || std::abs(target_ - current_) < 0.001f) {
        current_ = target_;
        start_ = target_;
        startTick_ = 0;
        active_ = false;
    }

    return current_;
}

// ---- Animation ----

void Animation::Start() {
    active = true;
    finished = false;
    progress = 0;
    startTick = 0;
}

void Animation::Tick(uint64_t now) {
    if (!active || finished) return;

    if (startTick == 0) {
        startTick = now;
    }

    float elapsed = now >= startTick ? (float)(now - startTick) : 0.0f;
    progress = durationMs > 0.0f
        ? std::clamp(elapsed / durationMs, 0.0f, 1.0f)
        : 1.0f;

    float t = ApplyEasing(easing, progress);
    Apply(t);

    if (progress >= 1.0f) {
        ApplyValue(to);
        finished = true;
        active = false;
        if (onComplete) onComplete();
    }
}

float Animation::ValueAt(uint64_t now) const {
    if (!active || finished) return to;

    uint64_t effectiveStart = startTick != 0 ? startTick : now;
    float elapsed = now >= effectiveStart ? (float)(now - effectiveStart) : 0.0f;
    float u = durationMs > 0.0f
        ? std::clamp(elapsed / durationMs, 0.0f, 1.0f)
        : 1.0f;
    float t = ApplyEasing(easing, u);
    return from + (to - from) * t;
}

void Animation::Apply(float t) {
    if (!target) return;
    float val = from + (to - from) * t;
    ApplyValue(val);
}

void Animation::ApplyValue(float val) {
    if (!target) return;

    switch (property) {
        case AnimProperty::Opacity:  target->opacity = val; break;
        case AnimProperty::PosX:     target->rect.left = val; break;
        case AnimProperty::PosY:     target->rect.top = val; break;
        case AnimProperty::Width:
            target->fixedW = val;
            ui::LayoutDirtyFlag() = true;
            break;
        case AnimProperty::Height:
            target->fixedH = val;
            ui::LayoutDirtyFlag() = true;
            break;
        case AnimProperty::BgColorR: target->bgColor.r = val; break;
        case AnimProperty::BgColorG: target->bgColor.g = val; break;
        case AnimProperty::BgColorB: target->bgColor.b = val; break;
        case AnimProperty::BgColorA: target->bgColor.a = val; break;
    }
}

// ---- AnimationManager ----

void AnimationManager::Animate(Widget* target, AnimProperty prop, float from, float to,
                               float durationMs, EasingFunction easing,
                               std::function<void()> onComplete) {
    if (!target) return;

    uint64_t now = GetTickCount64();
    float startValue = from;
    for (auto& a : anims_) {
        if (a.target == target && a.property == prop) {
            startValue = a.ValueAt(now);
            a.ApplyValue(startValue);
            break;
        }
    }

    anims_.erase(std::remove_if(anims_.begin(), anims_.end(), [&](const Animation& a) {
        return a.target == target && a.property == prop;
    }), anims_.end());

    if (durationMs <= 0.0f || std::abs(startValue - to) <= 0.001f) {
        Animation immediate;
        immediate.target = target;
        immediate.property = prop;
        immediate.from = startValue;
        immediate.to = to;
        immediate.ApplyValue(to);
        if (onComplete) onComplete();
        return;
    }

    Animation anim;
    anim.target = target;
    anim.property = prop;
    anim.from = startValue;
    anim.to = to;
    anim.durationMs = durationMs;
    anim.easing = easing;
    anim.onComplete = std::move(onComplete);
    anim.Start();
    anims_.push_back(std::move(anim));
}

void AnimationManager::FadeIn(Widget* target, float durationMs) {
    if (!target) return;
    target->visible = true;
    Animate(target, AnimProperty::Opacity, target->opacity, 1.0f, durationMs);
}

void AnimationManager::FadeOut(Widget* target, float durationMs) {
    if (!target) return;
    Animate(target, AnimProperty::Opacity, target->opacity, 0.0f, durationMs, EasingFunction::EaseOutCubic,
            [target]() { target->visible = false; target->opacity = 1.0f; });
}

bool AnimationManager::Tick() {
    if (anims_.empty()) return false;

    uint64_t now = GetTickCount64();
    for (auto& a : anims_) {
        if (a.active) a.Tick(now);
    }

    // Remove finished
    anims_.erase(std::remove_if(anims_.begin(), anims_.end(), [](const Animation& a) {
        return a.finished;
    }), anims_.end());

    return !anims_.empty();
}

void AnimationManager::Cancel(Widget* target) {
    anims_.erase(std::remove_if(anims_.begin(), anims_.end(), [target](const Animation& a) {
        return a.target == target;
    }), anims_.end());
}

void AnimationManager::Cancel(Widget* target, AnimProperty prop) {
    if (!target) return;
    anims_.erase(std::remove_if(anims_.begin(), anims_.end(), [&](const Animation& a) {
        return a.target == target && a.property == prop;
    }), anims_.end());
}

bool AnimationManager::HasActive() const {
    return !anims_.empty();
}

} // namespace ui
