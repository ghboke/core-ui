#include "animation.h"
#include <windows.h>
#include <algorithm>

namespace ui {

// ---- Animation ----

void Animation::Start() {
    active = true;
    finished = false;
    progress = 0;
    startTick = GetTickCount64();
}

void Animation::Tick(uint64_t now) {
    if (!active || finished) return;

    float elapsed = (float)(now - startTick);
    progress = std::clamp(elapsed / durationMs, 0.0f, 1.0f);

    float t = ToggleWidget::ApplyEasing(easing, progress);
    Apply(t);

    if (progress >= 1.0f) {
        finished = true;
        active = false;
        if (onComplete) onComplete();
    }
}

void Animation::Apply(float t) {
    if (!target) return;
    float val = from + (to - from) * t;

    switch (property) {
        case AnimProperty::Opacity:  target->opacity = val; break;
        case AnimProperty::PosX:     target->rect.left = val; break;
        case AnimProperty::PosY:     target->rect.top = val; break;
        case AnimProperty::Width:    target->fixedW = val; break;
        case AnimProperty::Height:   target->fixedH = val; break;
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
    // Remove existing animation on same target+property
    anims_.erase(std::remove_if(anims_.begin(), anims_.end(), [&](const Animation& a) {
        return a.target == target && a.property == prop;
    }), anims_.end());

    Animation anim;
    anim.target = target;
    anim.property = prop;
    anim.from = from;
    anim.to = to;
    anim.durationMs = durationMs;
    anim.easing = easing;
    anim.onComplete = std::move(onComplete);
    anim.Start();
    anims_.push_back(std::move(anim));
}

void AnimationManager::FadeIn(Widget* target, float durationMs) {
    target->visible = true;
    Animate(target, AnimProperty::Opacity, 0.0f, 1.0f, durationMs);
}

void AnimationManager::FadeOut(Widget* target, float durationMs) {
    Animate(target, AnimProperty::Opacity, 1.0f, 0.0f, durationMs, EasingFunction::EaseOutCubic,
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

bool AnimationManager::HasActive() const {
    return !anims_.empty();
}

// Global instance
AnimationManager& Animations() {
    static AnimationManager mgr;
    return mgr;
}

} // namespace ui
