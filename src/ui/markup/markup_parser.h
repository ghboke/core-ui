#pragma once
#include <string>
#include <vector>
#include <map>

namespace ui {

// Parsed style rule: .class / #id / TagName → properties
struct StyleRule {
    std::string selector;                       // ".sidebar", "#btn", "Label"
    std::map<std::string, std::string> props;   // key → value
};

// Parsed node in the UI tree
struct UiNode {
    std::string tag;                            // "VBox", "Label", etc.
    std::map<std::string, std::string> attrs;   // raw attribute key→value
    std::vector<UiNode> children;
    int line = 0;                               // source line for error messages
};

// Window hints (parsed from <ui> attributes)
struct WindowHints {
    int width = 0;          // 0 = use caller's default
    int height = 0;
    int resizable = -1;     // -1 = unset, 0 = false, 1 = true
    int tabNavigation = -1; // -1 = unset (default true), 0 = disabled, 1 = enabled
    std::string title;      // empty = unset
};

// Responsive media query
struct MediaQuery {
    float minWidth = 0;
    float maxWidth = 1e9f;
    float minHeight = 0;
    float maxHeight = 1e9f;
    std::vector<StyleRule> rules;
};

// Top-level parse result
struct UiDocument {
    int version = 1;
    WindowHints window;
    std::vector<StyleRule> styles;
    std::vector<MediaQuery> mediaQueries;
    UiNode root;                                // the single root element inside <ui>
};

// Parse a .ui file content string into a UiDocument.
// Returns true on success, false on error (errorOut filled).
bool ParseUiMarkup(const std::string& source, UiDocument& doc, std::string& errorOut);

} // namespace ui
