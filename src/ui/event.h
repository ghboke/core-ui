#pragma once

namespace ui {

struct MouseEvent {
    float x = 0;      // DIP coordinates (absolute, relative to window)
    float y = 0;
    float delta = 0;   // wheel delta (vertical, +up like Win32)
    bool  leftBtn = false;
    // DOM MouseEvent-style fields, filled at the ui_window dispatch sites
    // (StampDomMouseState). Widgets that construct MouseEvent themselves may
    // leave them at defaults — only the @mouse* UIX/JS path reads them.
    float deltaX = 0;      // horizontal wheel delta
    int   button  = -1;    // DOM: 0 left, 1 middle, 2 right, -1 none
    int   buttons = 0;     // DOM bitmask: 1 left, 2 right, 4 middle
    bool  ctrl  = false;
    bool  shift = false;
    bool  alt   = false;
};

} // namespace ui
