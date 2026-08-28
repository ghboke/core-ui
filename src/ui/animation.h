#pragma once

#ifndef UI_API
  #if defined(UI_CORE_STATIC)
    #define UI_API
  #elif defined(UI_CORE_BUILDING)
    #define UI_API __declspec(dllexport)
  #else
    #define UI_API __declspec(dllimport)
  #endif
#endif

#include "easing.h"
#include "widget.h"
#include <vector>
#include <functional>
#include <cstdint>

namespace ui {

struct UI_API AnimationSpec {
    float durationMs = 200.0f;
    EasingFunction easing = EasingFunction::EaseOutCubic;
};

class UI_API AnimatedFloat {
public:
    AnimatedFloat(float value = 0.0f,
                  float durationMs = 200.0f,
                  EasingFunction easing = EasingFunction::EaseOutCubic)
        : current_(value),
          target_(value),
          start_(value),
          spec_{durationMs, easing} {}

    float Current() const { return current_; }
    float Target() const { return target_; }
    bool IsActive() const { return active_; }
    bool Active() const { return IsActive(); }

    void SetImmediate(float value);
    void Retarget(float value);
    void Retarget(float value, const AnimationSpec& spec);
    void SetTarget(float value) { Retarget(value); }
    void SetIdleTarget(float value);
    float Sample(uint64_t now);
    float SampleFrame(uint64_t now) { return Sample(now); }

    void SetDuration(float durationMs) { spec_.durationMs = durationMs; }
    float Duration() const { return spec_.durationMs; }
    void SetEasing(EasingFunction easing) { spec_.easing = easing; }
    EasingFunction Easing() const { return spec_.easing; }

private:
    float current_ = 0.0f;
    float target_ = 0.0f;
    float start_ = 0.0f;
    AnimationSpec spec_{};
    uint64_t startTick_ = 0;
    bool active_ = false;
};

// Animatable property types
enum class AnimProperty {
    Opacity,        // Widget::opacity (0.0 ~ 1.0)
    PosX,           // Widget::rect.left (shifts entire widget)
    PosY,           // Widget::rect.top
    Width,          // Widget::fixedW
    Height,         // Widget::fixedH
    BgColorR,       // Widget::bgColor.r
    BgColorG,       // Widget::bgColor.g
    BgColorB,       // Widget::bgColor.b
    BgColorA,       // Widget::bgColor.a
};

class UI_API Animation {
public:
    Widget*         target = nullptr;
    AnimProperty    property = AnimProperty::Opacity;
    float           from = 0;
    float           to = 1;
    float           durationMs = 300;
    EasingFunction  easing = EasingFunction::EaseOutCubic;
    std::function<void()> onComplete;

    // State
    bool   active = false;
    bool   finished = false;
    float  progress = 0;        // 0..1
    uint64_t startTick = 0;

    void Start();
    void Tick(uint64_t now);
    float ValueAt(uint64_t now) const;
    void Apply(float t);
    void ApplyValue(float value);
};

class UI_API AnimationManager {
public:
    // Add and start an animation
    void Animate(Widget* target, AnimProperty prop, float from, float to,
                 float durationMs = 300, EasingFunction easing = EasingFunction::EaseOutCubic,
                 std::function<void()> onComplete = nullptr);

    // Convenience: fade in/out
    void FadeIn(Widget* target, float durationMs = 200);
    void FadeOut(Widget* target, float durationMs = 200);

    // Tick all active animations. Returns true if any are still running.
    bool Tick();

    // Remove all animations for a target
    void Cancel(Widget* target);

    // Remove only the animation driving `prop` on `target`.
    //
    // 宿主在同一帧里先让属性变到 A (起了 transition 动画) 又改主意直接落值 B 时
    // 必须调这个: 不掐掉那条在飞的动画, 它下一帧就把值覆盖回 A, 表现成"最后一次
    // 写丢了"(下游 GuoheView bug-070)。整体 Cancel(target) 不能用 —— 会把同一
    // widget 上其他属性的动画一起干掉。
    void Cancel(Widget* target, AnimProperty prop);

    bool HasActive() const;
    size_t Count() const { return anims_.size(); }

private:
    std::vector<Animation> anims_;
};

} // namespace ui
