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

#include "widget.h"
#include "controls.h"
#include <vector>
#include <functional>
#include <cstdint>

namespace ui {

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
    void Apply(float t);
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

    bool HasActive() const;

private:
    std::vector<Animation> anims_;
};

// Global animation manager
UI_API AnimationManager& Animations();

} // namespace ui
