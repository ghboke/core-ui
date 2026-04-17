#pragma once

namespace ui {

struct MouseEvent {
    float x = 0;      // DIP coordinates (absolute, relative to window)
    float y = 0;
    float delta = 0;   // wheel delta
    bool  leftBtn = false;
};

} // namespace ui
