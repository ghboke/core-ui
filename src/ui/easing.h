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

namespace ui {

enum class EasingFunction {
    Linear,
    EaseInQuad,
    EaseOutQuad,
    EaseInOutQuad,
    EaseInCubic,
    EaseOutCubic,
    EaseInOutCubic,
    EaseInElastic,
    EaseOutElastic,
    EaseInBounce,
    EaseOutBounce,
    EaseOutBack,
    /* WinUI 3 ControlFastOutSlowInKeySpline = cubic-bezier(0, 0, 0, 1): 控件状态过渡
     * (悬停 / 按下, 83ms) 用的那条。新值只能往后加, CSS transition 按整数存。 */
    FluentFastOutSlowIn
};

UI_API float ApplyEasing(EasingFunction func, float t);

}  // namespace ui
