#include "easing.h"
#include "menu_open_motion.h"

#include <algorithm>
#include <cmath>

namespace ui {

float ApplyEasing(EasingFunction func, float t) {
    t = std::clamp(t, 0.0f, 1.0f);

    switch (func) {
        case EasingFunction::Linear:
            return t;
        case EasingFunction::EaseInQuad:
            return t * t;
        case EasingFunction::EaseOutQuad:
            return 1 - (1 - t) * (1 - t);
        case EasingFunction::EaseInOutQuad:
            return t < 0.5f ? 2 * t * t : 1 - std::pow(-2 * t + 2, 2) / 2;
        case EasingFunction::EaseInCubic:
            return t * t * t;
        case EasingFunction::EaseOutCubic:
            return 1 - std::pow(1 - t, 3);
        case EasingFunction::EaseInOutCubic:
            return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
        case EasingFunction::EaseInElastic:
            return t == 0 ? 0 : t == 1 ? 1 :
                -std::pow(2, 10 * t - 10) * std::sin((t * 10 - 10.75) * (2 * 3.14159 / 3));
        case EasingFunction::EaseOutElastic:
            return t == 0 ? 0 : t == 1 ? 1 :
                std::pow(2, -10 * t) * std::sin((t * 10 - 0.75) * (2 * 3.14159 / 3)) + 1;
        case EasingFunction::EaseInBounce: {
            float x = 1 - t;
            if (x < 1/2.75f) return 1 - (7.5625f * x * x);
            if (x < 2/2.75f) {
                x -= 1.5f/2.75f;
                return 1 - (7.5625f * x * x + 0.75f);
            }
            if (x < 2.5f/2.75f) {
                x -= 2.25f/2.75f;
                return 1 - (7.5625f * x * x + 0.9375f);
            }
            x -= 2.625f/2.75f;
            return 1 - (7.5625f * x * x + 0.984375f);
        }
        case EasingFunction::EaseOutBounce: {
            if (t < 1/2.75f) return 7.5625f * t * t;
            if (t < 2/2.75f) {
                t -= 1.5f/2.75f;
                return 7.5625f * t * t + 0.75f;
            }
            if (t < 2.5f/2.75f) {
                t -= 2.25f/2.75f;
                return 7.5625f * t * t + 0.9375f;
            }
            t -= 2.625f/2.75f;
            return 7.5625f * t * t + 0.984375f;
        }
        case EasingFunction::FluentFastOutSlowIn:
            return static_cast<float>(menumotion::BezierProgress(0.0, 0.0, 0.0, 1.0, t));
        default:
            return t;
    }
}

}  // namespace ui
