#include "markup_builder.h"
#include "../controls.h"
#include "../theme.h"
#include <sstream>
#include <fstream>
#include <algorithm>
#include <set>
#include <cstdlib>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace ui {

// ---- BindingContext ----

void BindingContext::Register(const std::string& name, SetterFn fn) {
    bindings_.emplace(name, std::move(fn));
}

void BindingContext::Set(const std::string& name, const BindingValue& val) {
    auto range = bindings_.equal_range(name);
    for (auto it = range.first; it != range.second; ++it) {
        it->second(val);
    }
}

// ---- HandlerMap ----

void HandlerMap::SetClick(const std::string& name, std::function<void()> fn)       { clicks_[name] = std::move(fn); }
void HandlerMap::SetValue(const std::string& name, std::function<void(bool)> fn)    { values_[name] = std::move(fn); }
void HandlerMap::SetFloat(const std::string& name, std::function<void(float)> fn)   { floats_[name] = std::move(fn); }
void HandlerMap::SetSelection(const std::string& name, std::function<void(int)> fn) { selections_[name] = std::move(fn); }
void HandlerMap::SetString(const std::string& name, std::function<void(const std::wstring&)> fn) { strings_[name] = std::move(fn); }

std::function<void()>*     HandlerMap::FindClick(const std::string& n)     { auto it = clicks_.find(n);     return it != clicks_.end() ? &it->second : nullptr; }
std::function<void(bool)>* HandlerMap::FindValue(const std::string& n)     { auto it = values_.find(n);     return it != values_.end() ? &it->second : nullptr; }
std::function<void(float)>*HandlerMap::FindFloat(const std::string& n)     { auto it = floats_.find(n);     return it != floats_.end() ? &it->second : nullptr; }
std::function<void(int)>*  HandlerMap::FindSelection(const std::string& n) { auto it = selections_.find(n); return it != selections_.end() ? &it->second : nullptr; }
std::function<void(const std::wstring&)>* HandlerMap::FindString(const std::string& n) { auto it = strings_.find(n); return it != strings_.end() ? &it->second : nullptr; }

// ---- Helpers ----

namespace {

std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring ws(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &ws[0], len);
    return ws;
}

float ParseFloat(const std::string& s, float def = 0) {
    try { return std::stof(s); } catch (...) { return def; }
}

int ParseInt(const std::string& s, int def = 0) {
    try { return std::stoi(s); } catch (...) { return def; }
}

bool ParseBool(const std::string& s) {
    return s == "true" || s == "1" || s == "yes";
}

// Check if a string is a binding expression: {varName}
bool IsBinding(const std::string& s, std::string& varName) {
    if (s.size() >= 3 && s.front() == '{' && s.back() == '}') {
        varName = s.substr(1, s.size() - 2);
        return true;
    }
    return false;
}

// Parse "r,g,b,a" or "r,g,b" color
bool ParseColor(const std::string& s, D2D1_COLOR_F& c) {
    std::istringstream ss(s);
    std::string token;
    float vals[4] = {0, 0, 0, 1};
    int i = 0;
    while (std::getline(ss, token, ',') && i < 4) {
        // Trim
        while (!token.empty() && token.front() == ' ') token.erase(token.begin());
        while (!token.empty() && token.back() == ' ')  token.pop_back();
        try { vals[i] = std::stof(token); } catch (...) { return false; }
        i++;
    }
    if (i < 3) return false;
    c = {vals[0], vals[1], vals[2], vals[3]};
    return true;
}

// Resolve theme.xxx color references
using ThemeColorFn = const D2D1_COLOR_F& (*)();

struct ThemeEntry { const char* name; ThemeColorFn fn; };
static const ThemeEntry kThemeColors[] = {
    {"theme.titleBarBg",      theme::kTitleBarBg},
    {"theme.titleBarText",    theme::kTitleBarText},
    {"theme.windowBg",        theme::kWindowBg},
    {"theme.windowBorder",    theme::kWindowBorder},
    {"theme.toolbarBg",       theme::kToolbarBg},
    {"theme.statusBarBg",     theme::kStatusBarBg},
    {"theme.statusBarText",   theme::kStatusBarText},
    {"theme.contentBg",       theme::kContentBg},
    {"theme.contentText",     theme::kContentText},
    {"theme.btnNormal",       theme::kBtnNormal},
    {"theme.btnHover",        theme::kBtnHover},
    {"theme.btnPress",        theme::kBtnPress},
    {"theme.btnText",         theme::kBtnText},
    {"theme.accent",          theme::kAccent},
    {"theme.accentHover",     theme::kAccentHover},
    {"theme.sidebarBg",       theme::kSidebarBg},
    {"theme.sidebarItemHover",theme::kSidebarItemHover},
    {"theme.sidebarText",     theme::kSidebarText},
    {"theme.divider",         theme::kDivider},
    {"theme.inputBg",         theme::kInputBg},
    {"theme.inputBorder",     theme::kInputBorder},
};

ThemeColorFn FindThemeColor(const std::string& name) {
    for (auto& e : kThemeColors) {
        if (name == e.name) return e.fn;
    }
    return nullptr;
}

// Apply a color attribute (handles theme.xxx and r,g,b,a)
bool ResolveColor(const std::string& val, D2D1_COLOR_F& color) {
    auto fn = FindThemeColor(val);
    if (fn) { color = fn(); return true; }
    return ParseColor(val, color);
}

// Split "A,B,C" into vector
std::vector<std::wstring> SplitItems(const std::string& s) {
    std::vector<std::wstring> items;
    std::istringstream ss(s);
    std::string token;
    while (std::getline(ss, token, ',')) {
        while (!token.empty() && token.front() == ' ') token.erase(token.begin());
        while (!token.empty() && token.back() == ' ')  token.pop_back();
        items.push_back(ToWide(token));
    }
    return items;
}

// ---- Attribute validation helpers ----

static const char* kKnownAttrs[] = {
    "id", "width", "height", "minWidth", "minHeight", "maxWidth", "maxHeight",
    "expand", "flex", "padding", "margin", "visible", "if", "enabled", "hitTransparent", "hit-transparent",
    "dragWindow", "drag-window",
    "focusable", "tabIndex", "tabStop", "colspan", "rowspan", "bgColor",
    "tooltip", "onClick", "onChanged", "onTextChanged", "class", "opacity",
    // Controls
    "text", "fontSize", "bold", "textColor", "textAlign", "align", "wrap", "maxLines",
    "type", "icon", "checked", "on", "selected", "group", "min", "max", "value",
    "step", "decimals", "items", "placeholder", "maxLength", "indeterminate",
    "svg", "glyph", "ghost",
    // Layout
    "gap", "colGap", "rowGap", "cols", "vertical", "ratio", "active",
    // SplitView / NavItem
    "mode", "openPaneLength", "compactPaneLength", "open",
    // TitleBar / Expander
    "title", "header", "expanded", "showMin", "showMax", "showClose", "showIcon",
    // Flyout
    "placement",
    // Positioning
    "position", "left", "top", "right", "bottom",
    // Transition / i18n
    "transition", "shortcut",
    nullptr
};

static int EditDistance(const std::string& a, const std::string& b) {
    int m = (int)a.size(), n = (int)b.size();
    if (m == 0) return n;
    if (n == 0) return m;
    std::vector<int> dp((m+1)*(n+1));
    auto at = [&](int i, int j) -> int& { return dp[i*(n+1)+j]; };
    for (int i = 0; i <= m; i++) at(i,0) = i;
    for (int j = 0; j <= n; j++) at(0,j) = j;
    for (int i = 1; i <= m; i++)
        for (int j = 1; j <= n; j++)
            at(i,j) = std::min({at(i-1,j)+1, at(i,j-1)+1, at(i-1,j-1)+(a[i-1]!=b[j-1]?1:0)});
    return at(m,n);
}

static std::string SuggestAttribute(const std::string& unknown) {
    std::string best;
    int bestDist = 3;  // max edit distance for suggestion
    for (int i = 0; kKnownAttrs[i]; i++) {
        int d = EditDistance(unknown, kKnownAttrs[i]);
        if (d < bestDist) { bestDist = d; best = kKnownAttrs[i]; }
    }
    return best;
}

static void ValidateAttributes(const UiNode& node, const std::map<std::string, std::string>& attrs,
                                std::string& warnings) {
    static std::set<std::string> known;
    if (known.empty()) {
        for (int i = 0; kKnownAttrs[i]; i++) known.insert(kKnownAttrs[i]);
    }
    for (auto& [key, val] : attrs) {
        if (known.count(key)) continue;
        if (key == "class" || key == "style" || key == "src") continue;  // special attrs
        std::string suggestion = SuggestAttribute(key);
        std::string msg = "line " + std::to_string(node.line) + ": warning: unknown attribute '" + key + "'";
        if (!suggestion.empty()) msg += ", did you mean '" + suggestion + "'?";
        if (!warnings.empty()) warnings += "\n";
        warnings += msg;
    }
}

// Check if a single selector part matches a node
static bool MatchesPart(const std::string& part, const UiNode& node) {
    if (part.empty()) return false;
    if (part[0] == '.') {
        auto it = node.attrs.find("class");
        return it != node.attrs.end() && ("." + it->second) == part;
    }
    if (part[0] == '#') {
        auto it = node.attrs.find("id");
        return it != node.attrs.end() && ("#" + it->second) == part;
    }
    return part == node.tag;
}

// Merge style rules into node's attrs
// ancestors: parent chain from root to immediate parent (for descendant selectors)
std::map<std::string, std::string> MergeStyles(
    const UiNode& node,
    const std::vector<StyleRule>& styles,
    const std::vector<const UiNode*>& ancestors = {})
{
    auto attrs = node.attrs;

    for (auto& rule : styles) {
        // Skip pseudo-state selectors (handled separately)
        if (rule.selector.find(':') != std::string::npos) continue;

        // Split comma-separated selectors: ".a, .b" → [".a", ".b"]
        std::vector<std::string> selectors;
        {
            std::istringstream ss(rule.selector);
            std::string seg;
            while (std::getline(ss, seg, ',')) {
                while (!seg.empty() && seg.front() == ' ') seg.erase(seg.begin());
                while (!seg.empty() && seg.back() == ' ') seg.pop_back();
                if (!seg.empty()) selectors.push_back(seg);
            }
        }

        for (auto& sel : selectors) {
            bool match = false;

            // Check for descendant selector (contains space): ".sidebar Label"
            auto spacePos = sel.find(' ');
            if (spacePos != std::string::npos) {
                std::string ancestorPart = sel.substr(0, spacePos);
                std::string selfPart = sel.substr(spacePos + 1);
                while (!selfPart.empty() && selfPart.front() == ' ') selfPart.erase(selfPart.begin());

                if (MatchesPart(selfPart, node)) {
                    // Check if any ancestor matches
                    for (auto* anc : ancestors) {
                        if (MatchesPart(ancestorPart, *anc)) { match = true; break; }
                    }
                }
            } else {
                // Simple selector
                match = MatchesPart(sel, node);
            }

            if (match) {
                // Determine priority: tag < class < id < descendant
                bool isId = sel.find('#') != std::string::npos;
                bool isClass = sel.find('.') != std::string::npos;
                bool isDescendant = sel.find(' ') != std::string::npos;

                for (auto& [k, v] : rule.props) {
                    if (isId || isDescendant || isClass)
                        attrs.insert_or_assign(k, v);  // higher priority overrides
                    else if (!attrs.count(k))
                        attrs[k] = v;  // tag: lowest priority
                }
            }
        }
    }

    // Inherit text properties from parent node (if not explicitly set)
    if (!ancestors.empty()) {
        auto* parent = ancestors.back();
        static const char* inheritableProps[] = {"fontSize", "textColor", "bold", nullptr};
        for (int i = 0; inheritableProps[i]; i++) {
            const char* prop = inheritableProps[i];
            if (!attrs.count(prop)) {
                auto it = parent->attrs.find(prop);
                if (it != parent->attrs.end()) attrs[prop] = it->second;
            }
        }
    }

    return attrs;
}

// Parse LayoutAlign from string
LayoutAlign ParseLayoutAlign(const std::string& s) {
    if (s == "start")   return LayoutAlign::Start;
    if (s == "center")  return LayoutAlign::Center;
    if (s == "end")     return LayoutAlign::End;
    if (s == "stretch") return LayoutAlign::Stretch;
    return LayoutAlign::Stretch;
}

// Parse LayoutJustify from string
LayoutJustify ParseLayoutJustify(const std::string& s) {
    if (s == "start")        return LayoutJustify::Start;
    if (s == "center")       return LayoutJustify::Center;
    if (s == "end")          return LayoutJustify::End;
    if (s == "spaceBetween") return LayoutJustify::SpaceBetween;
    if (s == "space-between")return LayoutJustify::SpaceBetween;
    if (s == "spaceAround")  return LayoutJustify::SpaceAround;
    if (s == "space-around") return LayoutJustify::SpaceAround;
    return LayoutJustify::Start;
}

// Parse padding/margin: "8" or "8,4" or "8,4,8,4"
void ParseSpacing(const std::string& s, float& l, float& t, float& r, float& b) {
    std::vector<float> vals;
    std::istringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        while (!tok.empty() && tok.front() == ' ') tok.erase(tok.begin());
        while (!tok.empty() && tok.back() == ' ')  tok.pop_back();
        try { vals.push_back(std::stof(tok)); } catch (...) { vals.push_back(0); }
    }
    if (vals.size() == 1) { l = t = r = b = vals[0]; }
    else if (vals.size() == 2) { l = r = vals[0]; t = b = vals[1]; }
    else if (vals.size() >= 4) { l = vals[0]; t = vals[1]; r = vals[2]; b = vals[3]; }
}

// ---- Create widget from tag name ----
WidgetPtr CreateWidget(const std::string& tag, const std::map<std::string, std::string>& attrs) {
    auto getAttr = [&](const char* key, const std::string& def = "") -> std::string {
        auto it = attrs.find(key);
        return it != attrs.end() ? it->second : def;
    };

    if (tag == "VBox")       return std::make_shared<VBoxWidget>();
    if (tag == "HBox")       return std::make_shared<HBoxWidget>();
    if (tag == "Grid")       return std::make_shared<GridWidget>();
    if (tag == "Stack")      return std::make_shared<StackWidget>();
    if (tag == "Spacer")     return std::make_shared<SpacerWidget>(ParseFloat(getAttr("size")));
    if (tag == "Label")      return std::make_shared<LabelWidget>(ToWide(getAttr("text")));
    if (tag == "Button") {
        auto btn = std::make_shared<ButtonWidget>(ToWide(getAttr("text")));
        auto typeStr = getAttr("type");
        if (typeStr == "primary") btn->SetType(ButtonType::Primary);
        return btn;
    }
    if (tag == "CheckBox")   return std::make_shared<CheckBoxWidget>(ToWide(getAttr("text")));
    if (tag == "Separator") {
        bool vert = ParseBool(getAttr("vertical"));
        return std::make_shared<SeparatorWidget>(vert);
    }
    if (tag == "Slider") {
        float mn = ParseFloat(getAttr("min"), 0);
        float mx = ParseFloat(getAttr("max"), 100);
        float val = ParseFloat(getAttr("value"), mn);
        return std::make_shared<SliderWidget>(mn, mx, val);
    }
    if (tag == "TextInput")  return std::make_shared<TextInputWidget>(ToWide(getAttr("placeholder")));
    if (tag == "TextArea")   return std::make_shared<TextAreaWidget>(ToWide(getAttr("placeholder")));
    if (tag == "ComboBox") {
        auto items = SplitItems(getAttr("items"));
        return std::make_shared<ComboBoxWidget>(std::move(items));
    }
    if (tag == "RadioButton") {
        return std::make_shared<RadioButtonWidget>(ToWide(getAttr("text")), getAttr("group"));
    }
    if (tag == "Toggle")       return std::make_shared<ToggleWidget>(ToWide(getAttr("text")));
    if (tag == "ProgressBar") {
        float mn = ParseFloat(getAttr("min"), 0);
        float mx = ParseFloat(getAttr("max"), 100);
        float val = ParseFloat(getAttr("value"), 0);
        return std::make_shared<ProgressBarWidget>(mn, mx, val);
    }
    if (tag == "ScrollView")   return std::make_shared<ScrollViewWidget>();
    if (tag == "TabControl")   return std::make_shared<TabControlWidget>();
    if (tag == "ImageView")    return std::make_shared<ImageViewWidget>();
    if (tag == "TitleBar")     return std::make_shared<TitleBarWidget>(ToWide(getAttr("title")));
    if (tag == "IconButton") {
        std::string svg = getAttr("svg");
        bool ghost = ParseBool(getAttr("ghost"));
        return std::make_shared<IconButtonWidget>(svg, ghost);
    }
    if (tag == "Custom")       return std::make_shared<CustomWidget>();
    if (tag == "MenuBar")      return std::make_shared<MenuBarWidget>();
    if (tag == "Panel") {
        auto themed = getAttr("themedBg");
        if (!themed.empty()) {
            auto panel = std::make_shared<PanelWidget>();
            int id = ParseInt(themed, -1);
            if (id == 0)      panel->bgColorFn = []() { return theme::kSidebarBg(); };
            else if (id == 1) panel->bgColorFn = []() { return theme::kToolbarBg(); };
            else if (id == 2) panel->bgColorFn = []() { return theme::kContentBg(); };
            return panel;
        }
        return std::make_shared<PanelWidget>();
    }
    if (tag == "Splitter") {
        bool vert = ParseBool(getAttr("vertical"));
        auto sp = std::make_shared<SplitterWidget>(vert);
        float ratio = ParseFloat(getAttr("ratio"), 0.3f);
        sp->SetRatio(ratio);
        return sp;
    }
    if (tag == "NumberBox") {
        float mn = ParseFloat(getAttr("min"), 0);
        float mx = ParseFloat(getAttr("max"), 100);
        float val = ParseFloat(getAttr("value"), 0);
        float step = ParseFloat(getAttr("step"), 1);
        auto nb = std::make_shared<NumberBoxWidget>(mn, mx, val, step);
        auto dec = getAttr("decimals");
        if (!dec.empty()) nb->SetDecimals(ParseInt(dec));
        return nb;
    }
    if (tag == "Expander") {
        auto ex = std::make_shared<ExpanderWidget>(ToWide(getAttr("header")));
        if (ParseBool(getAttr("expanded"))) ex->SetExpandedImmediate(true);
        else if (getAttr("expanded") == "false") ex->SetExpandedImmediate(false);
        return ex;
    }
    if (tag == "NavItem") {
        auto ni = std::make_shared<NavItemWidget>(ToWide(getAttr("text")), getAttr("svg"));
        auto glyph = getAttr("glyph");
        if (!glyph.empty()) ni->SetGlyph(ToWide(glyph));
        if (ParseBool(getAttr("selected"))) ni->SetSelected(true);
        return ni;
    }
    if (tag == "Flyout") {
        auto fw = std::make_shared<FlyoutWidget>();
        auto p = getAttr("placement");
        if (p == "top") fw->SetPlacement(FlyoutPlacement::Top);
        else if (p == "bottom") fw->SetPlacement(FlyoutPlacement::Bottom);
        else if (p == "left") fw->SetPlacement(FlyoutPlacement::Left);
        else if (p == "right") fw->SetPlacement(FlyoutPlacement::Right);
        return fw;
    }
    if (tag == "SplitView") {
        auto sv = std::make_shared<SplitViewWidget>();
        auto modeStr = getAttr("mode");
        if (modeStr == "overlay")         sv->SetDisplayMode(SplitViewMode::Overlay);
        else if (modeStr == "inline")     sv->SetDisplayMode(SplitViewMode::Inline);
        else if (modeStr == "compactOverlay") sv->SetDisplayMode(SplitViewMode::CompactOverlay);
        else                              sv->SetDisplayMode(SplitViewMode::CompactInline);
        auto openLen = getAttr("openPaneLength");
        if (!openLen.empty()) sv->SetOpenPaneLength(ParseFloat(openLen, 320));
        auto compactLen = getAttr("compactPaneLength");
        if (!compactLen.empty()) sv->SetCompactPaneLength(ParseFloat(compactLen, 48));
        if (ParseBool(getAttr("open"))) sv->SetPaneOpenImmediate(true);
        return sv;
    }

    // Unknown tag → generic Panel container
    return std::make_shared<PanelWidget>();
}

// ---- Apply pseudo-state styles (:hover, :pressed, :disabled) ----
void ApplyStateStyles(Widget* w, const UiNode& node, const std::vector<StyleRule>& styles) {
    // Match rules like "Button:hover", ".class:pressed", "#id:hover"
    auto matchSelector = [&](const std::string& sel, const std::string& pseudo) -> bool {
        // sel must end with ":pseudo"
        if (sel.size() < pseudo.size() + 2) return false;
        if (sel.substr(sel.size() - pseudo.size() - 1) != ":" + pseudo) return false;
        std::string base = sel.substr(0, sel.size() - pseudo.size() - 1);

        // Match base against tag, .class, or #id
        if (base == node.tag) return true;
        auto classIt = node.attrs.find("class");
        if (classIt != node.attrs.end() && base == "." + classIt->second) return true;
        auto idIt = node.attrs.find("id");
        if (idIt != node.attrs.end() && base == "#" + idIt->second) return true;
        return false;
    };

    auto resolveBgFn = [](const std::string& val) -> std::function<D2D1_COLOR_F()> {
        auto fn = FindThemeColor(val);
        if (fn) return [fn]() { return fn(); };
        D2D1_COLOR_F c;
        if (ParseColor(val, c)) return [c]() { return c; };
        return nullptr;
    };

    for (auto& rule : styles) {
        for (auto& [prop, val] : rule.props) {
            if (prop != "bgColor") continue;
            if (matchSelector(rule.selector, "hover"))    w->stateColors.hoverBg = resolveBgFn(val);
            if (matchSelector(rule.selector, "pressed"))  w->stateColors.pressedBg = resolveBgFn(val);
            if (matchSelector(rule.selector, "disabled")) w->stateColors.disabledBg = resolveBgFn(val);
        }
    }
}

// ---- Apply common attributes to any widget ----
void ApplyCommonAttrs(Widget* w, const std::map<std::string, std::string>& attrs,
                      BindingContext& bindings, HandlerMap& handlers) {
    for (auto& [key, val] : attrs) {
        std::string bindVar;

        if (key == "id") {
            w->id = val;
        }
        else if (key == "width") {
            if (!val.empty() && val.back() == '%')
                w->percentW = ParseFloat(val.substr(0, val.size()-1));
            else
                w->fixedW = ParseFloat(val);
        }
        else if (key == "height") {
            if (!val.empty() && val.back() == '%')
                w->percentH = ParseFloat(val.substr(0, val.size()-1));
            else
                w->fixedH = ParseFloat(val);
        }
        else if (key == "minWidth") {
            w->minW = ParseFloat(val);
        }
        else if (key == "minHeight") {
            w->minH = ParseFloat(val);
        }
        else if (key == "maxWidth") {
            w->maxW = ParseFloat(val);
        }
        else if (key == "maxHeight") {
            w->maxH = ParseFloat(val);
        }
        else if (key == "expand") {
            if (ParseBool(val)) {
                w->expanding = true;
            }
        }
        else if (key == "flex") {
            w->flex = ParseFloat(val, 1.0f);
            w->expanding = true;
        }
        else if (key == "padding") {
            ParseSpacing(val, w->padL, w->padT, w->padR, w->padB);
        }
        else if (key == "margin") {
            ParseSpacing(val, w->marginL, w->marginT, w->marginR, w->marginB);
        }
        else if (key == "visible" || key == "if") {
            if (IsBinding(val, bindVar)) {
                bindings.Register(bindVar, [w](const BindingValue& v) {
                    if (auto* b = std::get_if<bool>(&v)) w->visible = *b;
                });
            } else {
                w->visible = ParseBool(val);
            }
        }
        else if (key == "enabled") {
            w->enabled = ParseBool(val);
        }
        else if (key == "hitTransparent" || key == "hit-transparent") {
            w->hitTransparent = ParseBool(val);
        }
        else if (key == "dragWindow" || key == "drag-window") {
            w->dragWindow = ParseBool(val);
        }
        else if (key == "readOnly") {
            if (auto* ti = dynamic_cast<TextInputWidget*>(w)) ti->readOnly = ParseBool(val);
            else if (auto* ta = dynamic_cast<TextAreaWidget*>(w)) ta->readOnly = ParseBool(val);
            else if (auto* nb = dynamic_cast<NumberBoxWidget*>(w)) nb->readOnly = ParseBool(val);
        }
        else if (key == "focusable") {
            w->focusable = ParseBool(val);
        }
        else if (key == "tabIndex") {
            w->tabIndex = ParseInt(val);
        }
        else if (key == "tabStop") {
            w->tabStop = ParseBool(val);
        }
        else if (key == "colspan") {
            w->gridColSpan = ParseInt(val, 1);
        }
        else if (key == "rowspan") {
            w->gridRowSpan = ParseInt(val, 1);
        }
        else if (key == "bgColor") {
            auto themeFn = FindThemeColor(val);
            if (themeFn) {
                // Dynamic: re-read theme color every frame
                w->bgColorFn = [themeFn]() { return themeFn(); };
            } else {
                D2D1_COLOR_F c;
                if (ParseColor(val, c)) w->bgColor = c;
            }
        }
        else if (key == "tooltip") {
            if (!val.empty() && val[0] == '@') {
                /* @key 形式：记录 i18n key，等 ApplyLanguage 解析 */
                w->tooltipI18nKey = val.substr(1);
            } else {
                w->tooltip = ToWide(val);
            }
        }
        // Declarative transition
        else if (key == "transition") {
            // Format: "property durationMs easing, property durationMs easing"
            // e.g.: "opacity 200ms ease-out"
            std::istringstream tss(val);
            std::string segment;
            while (std::getline(tss, segment, ',')) {
                std::istringstream ss(segment);
                std::string prop, dur, ease;
                ss >> prop >> dur >> ease;

                int propId = -1;
                if (prop == "opacity") propId = 0;       // AnimProperty::Opacity
                else if (prop == "posX") propId = 1;
                else if (prop == "posY") propId = 2;
                else if (prop == "width") propId = 3;
                else if (prop == "height") propId = 4;
                else if (prop == "bgColorR") propId = 5;
                else if (prop == "bgColorG") propId = 6;
                else if (prop == "bgColorB") propId = 7;
                else if (prop == "bgColorA") propId = 8;

                float durationMs = 200.0f;
                if (!dur.empty()) {
                    if (dur.back() == 's' && dur.size() > 2 && dur[dur.size()-2] == 'm')
                        durationMs = ParseFloat(dur.substr(0, dur.size()-2));
                    else
                        durationMs = ParseFloat(dur);
                }

                int easingId = 5;  // EaseOutCubic default
                if (ease == "linear") easingId = 0;
                else if (ease == "ease-in") easingId = 4;    // EaseInCubic
                else if (ease == "ease-out") easingId = 5;   // EaseOutCubic
                else if (ease == "ease-in-out") easingId = 6; // EaseInOutCubic

                if (propId >= 0) {
                    w->transitions.push_back({propId, durationMs, easingId});
                }
            }
        }
        // Absolute positioning
        else if (key == "position") {
            if (val == "absolute") w->positionAbsolute = true;
        }
        else if (key == "left")   { w->posLeft = ParseFloat(val); }
        else if (key == "top")    { w->posTop = ParseFloat(val); }
        else if (key == "right")  { w->posRight = ParseFloat(val); }
        else if (key == "bottom") { w->posBottom = ParseFloat(val); }
        // Event handlers
        else if (key == "onClick") {
            auto* fn = handlers.FindClick(val);
            if (fn) w->onClick = *fn;
            else {
                // Deferred: store handler name, will be resolved later
                std::string handlerName = val;
                w->onClick = [&handlers, handlerName]() {
                    auto* fn = handlers.FindClick(handlerName);
                    if (fn) (*fn)();
                };
            }
        }
        else if (key == "onChanged") {
            // Could be bool (checkbox/toggle), float (slider), or int (combobox)
            // Wire all applicable types
            std::string handlerName = val;

            if (auto* cb = dynamic_cast<CheckBoxWidget*>(w)) {
                w->onValueChanged = [&handlers, handlerName](bool v) {
                    auto* fn = handlers.FindValue(handlerName);
                    if (fn) (*fn)(v);
                };
            }
            else if (auto* tg = dynamic_cast<ToggleWidget*>(w)) {
                w->onValueChanged = [&handlers, handlerName](bool v) {
                    auto* fn = handlers.FindValue(handlerName);
                    if (fn) (*fn)(v);
                };
            }
            else if (auto* sl = dynamic_cast<SliderWidget*>(w)) {
                w->onFloatChanged = [&handlers, handlerName](float v) {
                    auto* fn = handlers.FindFloat(handlerName);
                    if (fn) (*fn)(v);
                };
            }
            else if (auto* nb = dynamic_cast<NumberBoxWidget*>(w)) {
                w->onFloatChanged = [&handlers, handlerName](float v) {
                    auto* fn = handlers.FindFloat(handlerName);
                    if (fn) (*fn)(v);
                };
            }
            else if (auto* combo = dynamic_cast<ComboBoxWidget*>(w)) {
                combo->onSelectionChanged = [&handlers, handlerName](int v) {
                    auto* fn = handlers.FindSelection(handlerName);
                    if (fn) (*fn)(v);
                };
            }
            else if (dynamic_cast<TextInputWidget*>(w) || dynamic_cast<TextAreaWidget*>(w)) {
                w->onTextChanged = [&handlers, handlerName](const std::wstring& v) {
                    auto* fn = handlers.FindString(handlerName);
                    if (fn) (*fn)(v);
                };
            }
        }
        else if (key == "onTextChanged") {
            std::string handlerName = val;
            w->onTextChanged = [&handlers, handlerName](const std::wstring& v) {
                auto* fn = handlers.FindString(handlerName);
                if (fn) (*fn)(v);
            };
        }
    }
}

// ---- Apply widget-specific attributes ----
void ApplySpecificAttrs(Widget* w, const std::string& tag,
                        const std::map<std::string, std::string>& attrs,
                        BindingContext& bindings) {
    auto getAttr = [&](const char* key) -> const std::string* {
        auto it = attrs.find(key);
        return it != attrs.end() ? &it->second : nullptr;
    };

    // Grid specific
    if (tag == "Grid") {
        auto* grid = dynamic_cast<GridWidget*>(w);
        if (grid) {
            if (auto* v = getAttr("cols"))   grid->cols_ = ParseInt(*v, 2);
            if (auto* v = getAttr("gap"))    { grid->rowGap_ = ParseFloat(*v); grid->colGap_ = ParseFloat(*v); }
            if (auto* v = getAttr("rowGap")) grid->rowGap_ = ParseFloat(*v);
            if (auto* v = getAttr("colGap")) grid->colGap_ = ParseFloat(*v);
        }
    }

    // Stack specific
    if (tag == "Stack") {
        auto* stack = dynamic_cast<StackWidget*>(w);
        if (stack) {
            if (auto* v = getAttr("active")) stack->SetActiveIndex(ParseInt(*v));
        }
    }

    // VBox / HBox specific
    if (tag == "VBox" || tag == "HBox") {
        if (auto* v = getAttr("gap")) {
            if (auto* vbox = dynamic_cast<VBoxWidget*>(w)) vbox->gap_ = ParseFloat(*v);
            if (auto* hbox = dynamic_cast<HBoxWidget*>(w)) hbox->gap_ = ParseFloat(*v);
        }
        if (auto* v = getAttr("align")) {
            if (auto* vbox = dynamic_cast<VBoxWidget*>(w)) vbox->crossAlign_ = ParseLayoutAlign(*v);
            if (auto* hbox = dynamic_cast<HBoxWidget*>(w)) hbox->crossAlign_ = ParseLayoutAlign(*v);
        }
        if (auto* v = getAttr("justify")) {
            if (auto* vbox = dynamic_cast<VBoxWidget*>(w)) vbox->mainJustify_ = ParseLayoutJustify(*v);
            if (auto* hbox = dynamic_cast<HBoxWidget*>(w)) hbox->mainJustify_ = ParseLayoutJustify(*v);
        }
    }

    // Label
    if (tag == "Label") {
        auto* lbl = dynamic_cast<LabelWidget*>(w);
        if (!lbl) return;
        if (auto* v = getAttr("fontSize"))  lbl->FontSize(ParseFloat(*v));
        if (auto* v = getAttr("bold"))      { if (ParseBool(*v)) lbl->Bold(); }
        if (auto* v = getAttr("textColor")) {
            auto themeFn = FindThemeColor(*v);
            if (themeFn) {
                lbl->SetTextColorFn([themeFn]() { return themeFn(); });
            } else {
                D2D1_COLOR_F c;
                if (ResolveColor(*v, c)) lbl->TextColor(c);
            }
        }
        if (auto* v = getAttr("textAlign")) {
            int a = 0; // DWRITE_TEXT_ALIGNMENT_LEADING
            if (*v == "center") a = 2;
            else if (*v == "right" || *v == "trailing") a = 1;
            lbl->Align(a);
        }
        if (auto* v = getAttr("wrap")) lbl->SetWrap(ParseBool(*v));
        if (auto* v = getAttr("maxLines")) lbl->SetMaxLines(ParseInt(*v));

        // Text binding
        if (auto* v = getAttr("text")) {
            std::string bindVar;
            if (IsBinding(*v, bindVar)) {
                bindings.Register(bindVar, [lbl](const BindingValue& val) {
                    if (auto* ws = std::get_if<std::wstring>(&val)) lbl->SetText(*ws);
                });
            }
        }
    }

    // Button
    if (tag == "Button") {
        auto* btn = dynamic_cast<ButtonWidget*>(w);
        if (!btn) return;
        if (auto* v = getAttr("fontSize")) btn->FontSize(ParseFloat(*v));
        if (auto* v = getAttr("icon"))     btn->SetIcon(ToWide(*v));
        if (auto* v = getAttr("textColor")) {
            D2D1_COLOR_F c; if (ResolveColor(*v, c)) btn->SetTextColor(c);
        }
        if (auto* v = getAttr("bgColor")) {
            D2D1_COLOR_F c; if (ResolveColor(*v, c)) btn->SetCustomBgColor(c);
        }
    }

    // CheckBox
    if (tag == "CheckBox") {
        auto* cb = dynamic_cast<CheckBoxWidget*>(w);
        if (!cb) return;
        if (auto* v = getAttr("checked")) {
            std::string bindVar;
            if (IsBinding(*v, bindVar)) {
                bindings.Register(bindVar, [cb](const BindingValue& val) {
                    if (auto* b = std::get_if<bool>(&val)) cb->SetCheckedImmediate(*b);
                });
            } else {
                cb->SetCheckedImmediate(ParseBool(*v));
            }
        }
    }

    // Toggle
    if (tag == "Toggle") {
        auto* tg = dynamic_cast<ToggleWidget*>(w);
        if (!tg) return;
        if (auto* v = getAttr("on")) {
            std::string bindVar;
            if (IsBinding(*v, bindVar)) {
                bindings.Register(bindVar, [tg](const BindingValue& val) {
                    if (auto* b = std::get_if<bool>(&val)) tg->SetOnImmediate(*b);
                });
            } else {
                tg->SetOnImmediate(ParseBool(*v));
            }
        }
    }

    // Slider
    if (tag == "Slider") {
        auto* sl = dynamic_cast<SliderWidget*>(w);
        if (!sl) return;
        if (auto* v = getAttr("value")) {
            std::string bindVar;
            if (IsBinding(*v, bindVar)) {
                bindings.Register(bindVar, [sl](const BindingValue& val) {
                    if (auto* f = std::get_if<float>(&val)) sl->SetValue(*f);
                    else if (auto* i = std::get_if<int>(&val)) sl->SetValue((float)*i);
                });
            }
        }
    }

    // ProgressBar
    if (tag == "ProgressBar") {
        auto* pb = dynamic_cast<ProgressBarWidget*>(w);
        if (!pb) return;
        if (auto* v = getAttr("value")) {
            std::string bindVar;
            if (IsBinding(*v, bindVar)) {
                bindings.Register(bindVar, [pb](const BindingValue& val) {
                    if (auto* f = std::get_if<float>(&val)) pb->SetValueImmediate(*f);
                    else if (auto* i = std::get_if<int>(&val)) pb->SetValueImmediate((float)*i);
                });
            }
        }
        if (auto* v = getAttr("indeterminate")) pb->SetIndeterminate(ParseBool(*v));
    }

    // ComboBox
    if (tag == "ComboBox") {
        auto* combo = dynamic_cast<ComboBoxWidget*>(w);
        if (!combo) return;
        if (auto* v = getAttr("selected")) combo->SetSelectedIndex(ParseInt(*v));
    }

    // RadioButton
    if (tag == "RadioButton") {
        auto* rb = dynamic_cast<RadioButtonWidget*>(w);
        if (!rb) return;
        if (auto* v = getAttr("selected")) {
            if (ParseBool(*v)) rb->SetSelectedImmediate(true);
        }
    }

    // TextInput
    if (tag == "TextInput") {
        auto* ti = dynamic_cast<TextInputWidget*>(w);
        if (!ti) return;
        if (auto* v = getAttr("text")) {
            std::string bindVar;
            if (IsBinding(*v, bindVar)) {
                bindings.Register(bindVar, [ti](const BindingValue& val) {
                    if (auto* ws = std::get_if<std::wstring>(&val)) ti->SetText(*ws);
                });
            } else {
                ti->SetText(ToWide(*v));
            }
        }
        if (auto* v = getAttr("maxLength")) ti->maxLength = ParseInt(*v);
    }

    // TextArea
    if (tag == "TextArea") {
        auto* ta = dynamic_cast<TextAreaWidget*>(w);
        if (!ta) return;
        if (auto* v = getAttr("text")) {
            std::string bindVar;
            if (IsBinding(*v, bindVar)) {
                bindings.Register(bindVar, [ta](const BindingValue& val) {
                    if (auto* ws = std::get_if<std::wstring>(&val)) ta->SetText(*ws);
                });
            } else {
                ta->SetText(ToWide(*v));
            }
        }
        if (auto* v = getAttr("maxLength")) ta->maxLength = ParseInt(*v);
    }

    // IconButton
    if (tag == "IconButton") {
        auto* ib = dynamic_cast<IconButtonWidget*>(w);
        if (!ib) return;
        if (auto* v = getAttr("iconPadding")) ib->SetIconPadding(ParseFloat(*v));
    }

    // ImageView
    if (tag == "ImageView") {
        auto* iv = dynamic_cast<ImageViewWidget*>(w);
        if (!iv) return;
        if (auto* v = getAttr("checkerboard")) iv->SetCheckerboard(ParseBool(*v));
        if (auto* v = getAttr("zoomMin")) {
            float mx = iv->Zoom();  // just need current max
            iv->SetZoomRange(ParseFloat(*v), 64.0f);
        }
        if (auto* v = getAttr("zoomMax")) {
            // re-read min after potential zoomMin set
            iv->SetZoomRange(0.01f, ParseFloat(*v));
        }
        // Handle both together
        {
            auto* vmin = getAttr("zoomMin");
            auto* vmax = getAttr("zoomMax");
            if (vmin || vmax) {
                float mn = vmin ? ParseFloat(*vmin, 0.01f) : 0.01f;
                float mx = vmax ? ParseFloat(*vmax, 64.0f) : 64.0f;
                iv->SetZoomRange(mn, mx);
            }
        }
    }

    // TitleBar
    if (tag == "TitleBar") {
        auto* tb = dynamic_cast<TitleBarWidget*>(w);
        if (!tb) return;
        if (auto* v = getAttr("showMin"))   tb->SetShowMin(ParseBool(*v));
        if (auto* v = getAttr("showMax"))   tb->SetShowMax(ParseBool(*v));
        if (auto* v = getAttr("showClose")) tb->SetShowClose(ParseBool(*v));
        if (auto* v = getAttr("showIcon"))  tb->SetShowIcon(ParseBool(*v));
    }
}

} // anonymous namespace

// ---- Recursive tree builder ----

WidgetPtr BuildWidgetTree(
    const UiNode& node,
    const std::vector<StyleRule>& styles,
    BindingContext& bindings,
    HandlerMap& handlers,
    std::string& errorOut,
    const std::string& baseDir,
    int depth)
{
    // Include: load external .ui fragment
    if (node.tag == "Include") {
        if (depth > 8) { errorOut = "Include depth limit exceeded"; return nullptr; }
        auto srcIt = node.attrs.find("src");
        if (srcIt == node.attrs.end()) { errorOut = "<Include> missing src attribute"; return nullptr; }

        std::string path = baseDir.empty() ? srcIt->second : baseDir + "/" + srcIt->second;

        // Read file
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) { errorOut = "Include: cannot open " + path; return nullptr; }
        std::ostringstream ss; ss << f.rdbuf();
        std::string source = ss.str();
        // Skip UTF-8 BOM
        if (source.size() >= 3 && (unsigned char)source[0]==0xEF && (unsigned char)source[1]==0xBB && (unsigned char)source[2]==0xBF)
            source = source.substr(3);

        // Parse as fragment (wrap in dummy <ui> if needed)
        bool isFragment = source.find("<ui") == std::string::npos;
        if (isFragment) source = "<ui version=\"1\">" + source + "</ui>";

        UiDocument fragDoc;
        if (!ParseUiMarkup(source, fragDoc, errorOut)) return nullptr;

        // Substitute {props.xxx} in fragment attrs with Include's attrs
        std::function<void(UiNode&)> substituteProps = [&](UiNode& n) {
            for (auto& [k, v] : n.attrs) {
                if (v.size() > 8 && v.substr(0, 7) == "{props.") {
                    std::string propName = v.substr(7, v.size() - 8);
                    auto it = node.attrs.find(propName);
                    if (it != node.attrs.end()) v = it->second;
                }
            }
            for (auto& child : n.children) substituteProps(child);
        };
        substituteProps(fragDoc.root);

        // Compute base dir for nested includes
        std::string fragDir = baseDir;
        auto slashPos = path.find_last_of("/\\");
        if (slashPos != std::string::npos) fragDir = path.substr(0, slashPos);

        return BuildWidgetTree(fragDoc.root, styles, bindings, handlers, errorOut, fragDir, depth + 1);
    }

    // Repeater: clone template for each item in list
    if (node.tag == "Repeater") {
        auto modelIt = node.attrs.find("model");
        if (modelIt == node.attrs.end() || node.children.empty()) return nullptr;

        // Extract list name from {listName}
        std::string listName;
        IsBinding(modelIt->second, listName);
        if (listName.empty()) listName = modelIt->second;

        auto* items = bindings.GetList(listName);
        auto container = std::make_shared<VBoxWidget>();
        container->gap_ = 0;

        if (items) {
            for (auto& item : *items) {
                // Clone template node and substitute {item.xxx}
                for (auto& tmpl : node.children) {
                    UiNode clone = tmpl;
                    std::function<void(UiNode&)> subst = [&](UiNode& n) {
                        for (auto& [k, v] : n.attrs) {
                            if (v.size() > 7 && v.substr(0, 6) == "{item.") {
                                std::string field = v.substr(6, v.size() - 7);
                                auto it = item.find(field);
                                if (it != item.end()) v = it->second;
                            }
                        }
                        for (auto& c : n.children) subst(c);
                    };
                    subst(clone);
                    auto cw = BuildWidgetTree(clone, styles, bindings, handlers, errorOut, baseDir, depth);
                    if (cw) container->AddChild(cw);
                }
            }
        }
        return container;
    }

    // Ancestor tracking for descendant selectors / style inheritance
    static thread_local std::vector<const UiNode*> s_ancestors;
    s_ancestors.push_back(&node);
    // RAII pop on all exit paths
    struct AncestorGuard { ~AncestorGuard() { s_ancestors.pop_back(); } } _ag;

    // Merge style rules into attrs (with ancestor chain for descendant selectors)
    // Pass ancestors excluding self (all except last element)
    std::vector<const UiNode*> parentChain(s_ancestors.begin(), s_ancestors.end() - 1);
    auto attrs = MergeStyles(node, styles, parentChain);

    // Create the widget
    WidgetPtr widget = CreateWidget(node.tag, attrs);
    if (!widget) {
        errorOut = "line " + std::to_string(node.line) + ": unknown element <" + node.tag + ">";
        return nullptr;
    }

    // Validate attributes (warn on typos)
    ValidateAttributes(node, attrs, errorOut);

    // Apply attributes
    ApplyCommonAttrs(widget.get(), attrs, bindings, handlers);
    ApplySpecificAttrs(widget.get(), node.tag, attrs, bindings);
    ApplyStateStyles(widget.get(), node, styles);

    // i18n: if text starts with @, store the key for language lookup
    {
        auto textIt = attrs.find("text");
        if (textIt != attrs.end() && !textIt->second.empty() && textIt->second[0] == '@') {
            widget->i18nKey = textIt->second.substr(1);  // strip @
        }
    }

    // i18n: TitleBar title="@key" — store key so ApplyLanguage can re-translate
    if (node.tag == "TitleBar") {
        auto titleIt = attrs.find("title");
        if (titleIt != attrs.end() && !titleIt->second.empty() && titleIt->second[0] == '@') {
            widget->titleI18nKey = titleIt->second.substr(1);
        }
    }

    // Handle children
    if (node.tag == "ScrollView") {
        // ScrollView expects one child as content
        auto* sv = dynamic_cast<ScrollViewWidget*>(widget.get());
        if (sv && !node.children.empty()) {
            auto content = BuildWidgetTree(node.children[0], styles, bindings, handlers, errorOut, baseDir, depth);
            if (content) sv->SetContent(content);
        }
    }
    else if (node.tag == "TabControl") {
        // Children are <Tab title="..."> pseudo-elements
        auto* tc = dynamic_cast<TabControlWidget*>(widget.get());
        if (tc) {
            for (auto& child : node.children) {
                if (child.tag == "Tab") {
                    auto titleIt = child.attrs.find("title");
                    std::wstring title = titleIt != child.attrs.end() ? ToWide(titleIt->second) : L"";

                    // Tab's first child (or a VBox wrapping all children) is the content
                    WidgetPtr tabContent;
                    if (child.children.size() == 1) {
                        tabContent = BuildWidgetTree(child.children[0], styles, bindings, handlers, errorOut, baseDir, depth);
                    } else if (child.children.size() > 1) {
                        auto vbox = std::make_shared<VBoxWidget>();
                        for (auto& gc : child.children) {
                            auto cw = BuildWidgetTree(gc, styles, bindings, handlers, errorOut, baseDir, depth);
                            if (cw) vbox->AddChild(cw);
                        }
                        tabContent = vbox;
                    }
                    if (tabContent) tc->AddTab(title, tabContent);
                } else {
                    // Non-Tab child — treat as regular child
                    auto cw = BuildWidgetTree(child, styles, bindings, handlers, errorOut, baseDir, depth);
                    if (cw) widget->AddChild(cw);
                }
            }
        }
    }
    else if (node.tag == "TitleBar") {
        // Children are custom widgets added to the title bar (between title and caption buttons)
        auto* tb = dynamic_cast<TitleBarWidget*>(widget.get());
        if (tb) {
            for (auto& child : node.children) {
                auto cw = BuildWidgetTree(child, styles, bindings, handlers, errorOut, baseDir, depth);
                if (cw) tb->AddCustomWidget(cw);
            }
        }
    }
    else if (node.tag == "Flyout") {
        auto* fw = dynamic_cast<FlyoutWidget*>(widget.get());
        if (fw && !node.children.empty()) {
            auto content = BuildWidgetTree(node.children[0], styles, bindings, handlers, errorOut, baseDir, depth);
            if (content) fw->SetContent(content);
        }
    }
    else if (node.tag == "SplitView") {
        // First child = Pane, Second child = Content
        auto* sv = dynamic_cast<SplitViewWidget*>(widget.get());
        if (sv) {
            if (node.children.size() >= 1) {
                auto pane = BuildWidgetTree(node.children[0], styles, bindings, handlers, errorOut, baseDir, depth);
                if (pane) sv->SetPane(pane);
            }
            if (node.children.size() >= 2) {
                auto content = BuildWidgetTree(node.children[1], styles, bindings, handlers, errorOut, baseDir, depth);
                if (content) sv->SetContent(content);
            }
        }
    }
    else if (node.tag == "MenuBar") {
        // Children are <Menu text="..."> with <MenuItem> sub-elements
        auto* mb = dynamic_cast<MenuBarWidget*>(widget.get());
        if (mb) {
            for (auto& menuNode : node.children) {
                if (menuNode.tag != "Menu") continue;
                auto textIt = menuNode.attrs.find("text");
                std::wstring menuText = textIt != menuNode.attrs.end() ? ToWide(textIt->second) : L"";

                auto ctxMenu = std::make_shared<ContextMenu>();
                for (auto& itemNode : menuNode.children) {
                    if (itemNode.tag == "Separator") {
                        ctxMenu->AddSeparator();
                        continue;
                    }
                    if (itemNode.tag != "MenuItem") continue;
                    auto idIt = itemNode.attrs.find("id");
                    auto txtIt = itemNode.attrs.find("text");
                    auto scIt  = itemNode.attrs.find("shortcut");
                    int itemId = idIt != itemNode.attrs.end() ? ParseInt(idIt->second) : 0;
                    std::wstring itemText = txtIt != itemNode.attrs.end() ? ToWide(txtIt->second) : L"";
                    std::wstring shortcut = scIt != itemNode.attrs.end() ? ToWide(scIt->second) : L"";

                    if (shortcut.empty())
                        ctxMenu->AddItem(itemId, itemText);
                    else
                        ctxMenu->AddItemEx(itemId, itemText, shortcut, "", nullptr);

                    // Wire onClick handler
                    auto onClickIt = itemNode.attrs.find("onClick");
                    if (onClickIt != itemNode.attrs.end()) {
                        std::string handlerName = onClickIt->second;
                        // Store mapping: itemId → handler name (resolved via menubar callback)
                    }
                }
                mb->AddMenu(menuText, ctxMenu);
            }
        }
    }
    else {
        // Regular children
        for (auto& child : node.children) {
            auto cw = BuildWidgetTree(child, styles, bindings, handlers, errorOut, baseDir, depth);
            if (cw) widget->AddChild(cw);
        }
    }

    return widget;
}

} // namespace ui
