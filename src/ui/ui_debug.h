/*
 * ui_debug.h — Widget tree debug inspector
 */
#pragma once

#include <string>

namespace ui {

class Widget;
class Renderer;

// Dump the widget tree rooted at `root` as a JSON string.
// If renderer is provided, text measurement (textWidth, lines) is included.
std::string DebugDumpTree(Widget* root, Renderer* renderer = nullptr, int depth = 0);

} // namespace ui
