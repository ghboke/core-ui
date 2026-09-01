#include "widget_factory.h"
// Windows RPC header defines `small` as a macro (#define small char), conflicting with
// theme::small constexpr pulled in via controls.h → renderer.h → theme.h. Undefine it here.
#ifdef small
#undef small
#endif
#include "../controls.h"
#include "../gh_img_view.h"
#include "../ocr_img_view.h"
#include "../css/value.h"
#include "svg_widget.h"

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace ui::page {

namespace {

// UTF-8 → UTF-16 (tiny, assumes valid UTF-8)
std::wstring ToWide(const std::string& s) {
    std::wstring r;
    r.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        int len = 0;
        if ((c & 0x80) == 0)          { cp = c;        len = 1; }
        else if ((c & 0xE0) == 0xC0)  { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0)  { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0)  { cp = c & 0x07; len = 4; }
        else                           { i++; continue; }
        if (i + len > s.size()) break;
        for (int k = 1; k < len; k++) {
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        }
        i += len;
        if (cp < 0x10000) {
            r += static_cast<wchar_t>(cp);
        } else {
            cp -= 0x10000;
            r += static_cast<wchar_t>(0xD800 | (cp >> 10));
            r += static_cast<wchar_t>(0xDC00 | (cp & 0x3FF));
        }
    }
    return r;
}

// Extract the first plain text child (before any element or interpolation).
// Returns empty if no such child.
std::string FirstPlainText(const ui::uix::Node& n) {
    for (const auto& c : n.children) {
        if (c->kind == ui::uix::NodeKind::Text) return c->text;
        if (c->kind == ui::uix::NodeKind::Interpolation) return "";  // handled by binding
        if (c->kind == ui::uix::NodeKind::Element) return "";        // non-text follows
    }
    return "";
}

// Resolve a length string ("8px", "50%") to px. Returns 0 on fail.
// Uses sentinel values: parent/em/rem/vw/vh assumed reasonable defaults.
float ResolvePx(const std::string& v, float parentSize = 0.0f) {
    if (v.empty()) return 0.0f;
    ui::css::Length len;
    if (ui::css::ParseLength(v, len)) {
        if (len.unit == ui::css::Unit::Auto) return 0.0f;
        double px = 0.0;
        if (ui::css::ResolveLengthPx(len, parentSize, 14.0, 14.0, 1920.0, 1080.0, px)) {
            return static_cast<float>(px);
        }
    }
    return 0.0f;
}

bool ParseBoolAttr(const std::string& s) {
    return s == "true" || s == "1" || s == "yes";
}

// Parse a "top right bottom left" CSS shorthand (1-4 values). Returns 4 lengths in px.
void ParseBoxShorthand(const std::string& v, float& top, float& right, float& bottom, float& left) {
    auto pv = ui::css::ParseValue(v);
    std::vector<float> vals;
    for (auto& c : pv.components) {
        if (c.kind == ui::css::ComponentKind::Length) {
            double px = 0;
            if (ui::css::ResolveLengthPx(c.length, 0, 14, 14, 0, 0, px)) vals.push_back(static_cast<float>(px));
        } else if (c.kind == ui::css::ComponentKind::Number) {
            vals.push_back(static_cast<float>(c.number));
        }
    }
    if (vals.empty()) { top = right = bottom = left = 0; return; }
    if (vals.size() == 1) { top = right = bottom = left = vals[0]; return; }
    if (vals.size() == 2) { top = bottom = vals[0]; right = left = vals[1]; return; }
    if (vals.size() == 3) { top = vals[0]; right = left = vals[1]; bottom = vals[2]; return; }
    top = vals[0]; right = vals[1]; bottom = vals[2]; left = vals[3];
}

D2D1_COLOR_F ToD2DColor(const ui::css::Color& c) {
    return D2D1_COLOR_F{c.r, c.g, c.b, c.a};
}

}  // namespace

namespace {
// Parse CSS transition declaration into widget.transitions vector.
// Accepts: "opacity 200ms ease-out" or multiple comma-separated entries.
// Property id mapping (matches ui::AnimProperty enum):
//   opacity=0, posX=1, posY=2, width=3, height=4, bgColorR=5, bgColorG=6, bgColorB=7, bgColorA=8
// Easing id mapping (matches ui::EasingFunction enum, same as markup_builder):
//   linear=0, ease-in=4, ease-out=5 (default), ease-in-out=6
void ParseTransitions(Widget& w, const std::string& raw) {
    w.transitions.clear();
    std::string buf;
    auto flush = [&](const std::string& seg) {
        // Tokenize segment by whitespace
        std::istringstream iss(seg);
        std::string prop, dur, ease;
        iss >> prop >> dur >> ease;
        if (prop.empty()) return;

        int propId = -1;
        if      (prop == "opacity") propId = 0;
        else if (prop == "posX" || prop == "left") propId = 1;
        else if (prop == "posY" || prop == "top")  propId = 2;
        else if (prop == "width")  propId = 3;
        else if (prop == "height") propId = 4;
        else if (prop == "background-color" || prop == "bgColor") propId = 8;  // alpha; for color transitions
        if (propId < 0) return;

        float durationMs = 200.0f;
        if (!dur.empty()) {
            try {
                if (dur.size() > 2 && dur[dur.size()-2] == 'm' && dur.back() == 's') {
                    durationMs = std::stof(dur.substr(0, dur.size()-2));
                } else if (dur.back() == 's') {
                    durationMs = std::stof(dur.substr(0, dur.size()-1)) * 1000.0f;
                } else {
                    durationMs = std::stof(dur);
                }
            } catch (...) {}
        }
        int easingId = 5;  // EaseOutCubic
        if (ease == "linear")           easingId = 0;
        else if (ease == "ease-in")     easingId = 4;
        else if (ease == "ease-out")    easingId = 5;
        else if (ease == "ease-in-out") easingId = 6;

        Widget::TransitionSpec spec{propId, durationMs, easingId};
        w.transitions.push_back(spec);
    };

    // Split by comma (outside parens)
    std::string seg;
    int depth = 0;
    for (char c : raw) {
        if (c == '(') depth++;
        else if (c == ')') depth--;
        if (c == ',' && depth == 0) { flush(seg); seg.clear(); continue; }
        seg += c;
    }
    flush(seg);
}
}  // namespace

void ApplyCommonStyle(Widget& w, const ui::css::ComputedStyle& s) {
    // Width / Height
    if (s.Has("width")) {
        ui::css::Length len;
        if (ui::css::ParseLength(s.Get("width"), len) && len.unit != ui::css::Unit::Auto) {
            if (len.unit == ui::css::Unit::Percent) {
                w.percentW = static_cast<float>(len.value);
            } else {
                double px = 0;
                if (ui::css::ResolveLengthPx(len, 0, 14, 14, 1920, 1080, px))
                    w.fixedW = static_cast<float>(px);
            }
        }
    }
    if (s.Has("height")) {
        ui::css::Length len;
        if (ui::css::ParseLength(s.Get("height"), len) && len.unit != ui::css::Unit::Auto) {
            if (len.unit == ui::css::Unit::Percent) {
                w.percentH = static_cast<float>(len.value);
            } else {
                double px = 0;
                if (ui::css::ResolveLengthPx(len, 0, 14, 14, 1920, 1080, px))
                    w.fixedH = static_cast<float>(px);
            }
        }
    }
    auto applyMinMax = [&](const char* css, float Widget::* fixed, float Widget::* pct,
                           bool isMax) {
        if (!s.Has(css)) return;
        ui::css::Length len;
        if (!ui::css::ParseLength(s.Get(css), len) || len.unit == ui::css::Unit::Auto) return;
        if (len.unit == ui::css::Unit::Percent) {
            w.*pct = static_cast<float>(len.value);
        } else {
            double px = 0.0;
            if (ui::css::ResolveLengthPx(len, 0, 14, 14, 1920, 1080, px)) {
                if (!isMax || px > 0) w.*fixed = static_cast<float>(px);
            }
        }
    };
    applyMinMax("min-width",  &Widget::minW, &Widget::percentMinW, false);
    applyMinMax("min-height", &Widget::minH, &Widget::percentMinH, false);
    applyMinMax("max-width",  &Widget::maxW, &Widget::percentMaxW, true);
    applyMinMax("max-height", &Widget::maxH, &Widget::percentMaxH, true);

    // Padding / Margin
    if (s.Has("padding")) {
        ParseBoxShorthand(s.Get("padding"), w.padT, w.padR, w.padB, w.padL);
    }
    if (s.Has("padding-top"))    w.padT = ResolvePx(s.Get("padding-top"));
    if (s.Has("padding-right"))  w.padR = ResolvePx(s.Get("padding-right"));
    if (s.Has("padding-bottom")) w.padB = ResolvePx(s.Get("padding-bottom"));
    if (s.Has("padding-left"))   w.padL = ResolvePx(s.Get("padding-left"));

    if (s.Has("margin")) {
        ParseBoxShorthand(s.Get("margin"), w.marginT, w.marginR, w.marginB, w.marginL);
    }
    if (s.Has("margin-top"))    w.marginT = ResolvePx(s.Get("margin-top"));
    if (s.Has("margin-right"))  w.marginR = ResolvePx(s.Get("margin-right"));
    if (s.Has("margin-bottom")) w.marginB = ResolvePx(s.Get("margin-bottom"));
    if (s.Has("margin-left"))   w.marginL = ResolvePx(s.Get("margin-left"));

    // Background — solid color OR linear/radial gradient
    auto parseBgGradient = [](const ui::css::Component& fn, Widget::BgGradient& out) -> bool {
        std::string lname;
        for (char c : fn.ident) lname += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lname != "linear-gradient" && lname != "radial-gradient") return false;
        out.kind = (lname == "linear-gradient") ? Widget::BgGradient::Linear
                                                  : Widget::BgGradient::Radial;
        // Split args at top-level commas.
        std::vector<std::vector<ui::css::Component>> buckets;
        buckets.emplace_back();
        for (auto& a : fn.args) {
            if (a.kind == ui::css::ComponentKind::Comma) buckets.emplace_back();
            else buckets.back().push_back(a);
        }
        size_t startIdx = 0;
        // For radial-gradient, an optional shape/extent/position prefix precedes
        // the color stops, e.g. `radial-gradient(circle, #fff, #000)` or
        // `radial-gradient(ellipse at top, ...)`. Detect it by absence of a
        // Color component in the first bucket and skip past it as a unit.
        if (out.kind == Widget::BgGradient::Radial && !buckets.empty() && !buckets[0].empty()) {
            bool hasColor = false;
            for (auto& c : buckets[0]) {
                if (c.kind == ui::css::ComponentKind::Color) { hasColor = true; break; }
            }
            if (!hasColor) startIdx = 1;
        }
        // First bucket may be the angle / direction (linear) or shape/position (radial).
        if (out.kind == Widget::BgGradient::Linear && !buckets.empty() && !buckets[0].empty()) {
            const auto& f = buckets[0];
            // angle as Length{Deg} (preferred) or bare Number
            if (f.size() == 1 && f[0].kind == ui::css::ComponentKind::Length &&
                f[0].length.unit == ui::css::Unit::Deg) {
                out.angleDeg = static_cast<float>(f[0].length.value);
                startIdx = 1;
            } else if (f.size() >= 2 && f[0].kind == ui::css::ComponentKind::Number &&
                       f[1].kind == ui::css::ComponentKind::Ident) {
                std::string suf;
                for (char c : f[1].ident) suf += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (suf == "deg") {
                    out.angleDeg = static_cast<float>(f[0].number);
                    startIdx = 1;
                }
            } else if (f.size() >= 2 && f[0].kind == ui::css::ComponentKind::Ident) {
                std::string head;
                for (char c : f[0].ident) head += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (head == "to") {
                    // "to right" / "to bottom right" / etc.
                    std::string side1, side2;
                    if (f.size() >= 2 && f[1].kind == ui::css::ComponentKind::Ident) {
                        for (char c : f[1].ident) side1 += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    }
                    if (f.size() >= 3 && f[2].kind == ui::css::ComponentKind::Ident) {
                        for (char c : f[2].ident) side2 += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    }
                    auto toAngle = [](const std::string& a, const std::string& b) -> float {
                        // Two-axis: pick diagonal angle, otherwise pure cardinal.
                        if      ((a == "top"    && b == "right") || (a == "right"  && b == "top"))    return 45;
                        else if ((a == "bottom" && b == "right") || (a == "right"  && b == "bottom")) return 135;
                        else if ((a == "bottom" && b == "left")  || (a == "left"   && b == "bottom")) return 225;
                        else if ((a == "top"    && b == "left")  || (a == "left"   && b == "top"))    return 315;
                        if (a == "top")    return 0;
                        if (a == "right")  return 90;
                        if (a == "bottom") return 180;
                        if (a == "left")   return 270;
                        return 180;
                    };
                    out.angleDeg = toAngle(side1, side2);
                    startIdx = 1;
                }
            }
        }
        // Remaining buckets are stops: <color> [<position>]
        for (size_t i = startIdx; i < buckets.size(); ++i) {
            Widget::BgGradient::Stop st;
            st.position = -1;
            for (auto& c : buckets[i]) {
                if (c.kind == ui::css::ComponentKind::Color) {
                    st.color = D2D1::ColorF(c.color.r, c.color.g, c.color.b, c.color.a);
                } else if (c.kind == ui::css::ComponentKind::Length &&
                           c.length.unit == ui::css::Unit::Percent) {
                    st.position = static_cast<float>(c.length.value / 100.0);
                }
            }
            out.stops.push_back(st);
        }
        return !out.stops.empty();
    };

    // background-color / background-image / background — all three coexist per
    // CSS spec: solid color is the bottom layer, gradients paint on top.
    // Previously only one source was read (background-image > -color > shorthand)
    // and the renderer used `else if` so any gradient hid the bgColor entirely.
    if (s.Has("background-color")) {
        ui::css::Color c;
        if (ui::css::ParseColor(s.Get("background-color"), c)) w.bgColor = ToD2DColor(c);
    }
    // The image-ish source: prefer explicit background-image, else fall back
    // to background shorthand. Both can carry comma-separated gradient layers
    // and (per CSS spec) a trailing <color> in the LAST layer that becomes the
    // background-color.
    const std::string* bgImageish = nullptr;
    if (s.Has("background-image"))   bgImageish = &s.Get("background-image");
    else if (s.Has("background"))    bgImageish = &s.Get("background");

    if (bgImageish && !bgImageish->empty()) {
        auto pv = ui::css::ParseValue(*bgImageish);
        // Group components into per-layer buckets at top-level commas.
        std::vector<std::vector<ui::css::Component>> layers;
        layers.emplace_back();
        for (auto& c : pv.components) {
            if (c.kind == ui::css::ComponentKind::Comma) layers.emplace_back();
            else layers.back().push_back(c);
        }
        bool gotGradient = false;
        for (size_t i = 0; i < layers.size(); ++i) {
            for (auto& c : layers[i]) {
                if (c.kind == ui::css::ComponentKind::Function) {
                    Widget::BgGradient g;
                    if (parseBgGradient(c, g)) {
                        w.bgGradients.push_back(std::move(g));
                        gotGradient = true;
                    }
                } else if (c.kind == ui::css::ComponentKind::Color &&
                           i + 1 == layers.size()) {
                    // Trailing color in the last layer — CSS short-hand
                    // background-color. Only set if -color hasn't already
                    // explicitly set it.
                    if (!s.Has("background-color")) {
                        w.bgColor = D2D1_COLOR_F{c.color.r, c.color.g,
                                                 c.color.b, c.color.a};
                    }
                }
            }
        }
        w.hasBgGradient = gotGradient;
        // Plain `background: red` (no gradient, no -color set) — fall back to
        // parsing the whole string as a color.
        if (!gotGradient && !s.Has("background-color") && w.bgColor.a == 0) {
            ui::css::Color c;
            if (ui::css::ParseColor(*bgImageish, c)) w.bgColor = ToD2DColor(c);
        }
    }
    // background-size — comma-separated list of <width> [<height>] pairs.
    // Each pair binds to the corresponding gradient layer. CSS spec says the
    // shorter list cycles to fill the longer; we follow that.
    if (s.Has("background-size") && !w.bgGradients.empty()) {
        auto pv = ui::css::ParseValue(s.Get("background-size"));
        std::vector<std::vector<ui::css::Component>> buckets;
        buckets.emplace_back();
        for (auto& c : pv.components) {
            if (c.kind == ui::css::ComponentKind::Comma) buckets.emplace_back();
            else buckets.back().push_back(c);
        }
        auto resolveLen = [](const ui::css::Component& c) -> float {
            if (c.kind == ui::css::ComponentKind::Length) {
                double px = 0;
                if (ui::css::ResolveLengthPx(c.length, 0, 14, 14, 0, 0, px))
                    return static_cast<float>(px);
            }
            if (c.kind == ui::css::ComponentKind::Number) return static_cast<float>(c.number);
            return -1.0f;
        };
        for (size_t i = 0; i < w.bgGradients.size() && !buckets.empty(); ++i) {
            const auto& bucket = buckets[i % buckets.size()];
            if (bucket.empty()) continue;
            float bw = resolveLen(bucket[0]);
            float bh = bucket.size() >= 2 ? resolveLen(bucket[1]) : bw;
            if (bw > 0) w.bgGradients[i].tileW = bw;
            if (bh > 0) w.bgGradients[i].tileH = bh;
        }
    }
    // background-position — comma-separated list of <x> [<y>] pairs (px only
    // for now; keywords like "top left" are ignored). Negative offsets ok.
    if (s.Has("background-position") && !w.bgGradients.empty()) {
        auto pv = ui::css::ParseValue(s.Get("background-position"));
        std::vector<std::vector<ui::css::Component>> buckets;
        buckets.emplace_back();
        for (auto& c : pv.components) {
            if (c.kind == ui::css::ComponentKind::Comma) buckets.emplace_back();
            else buckets.back().push_back(c);
        }
        auto resolveLen = [](const ui::css::Component& c, bool& ok) -> float {
            ok = false;
            if (c.kind == ui::css::ComponentKind::Length) {
                double px = 0;
                if (ui::css::ResolveLengthPx(c.length, 0, 14, 14, 0, 0, px)) {
                    ok = true; return static_cast<float>(px);
                }
            }
            if (c.kind == ui::css::ComponentKind::Number) {
                ok = true; return static_cast<float>(c.number);
            }
            return 0.0f;
        };
        for (size_t i = 0; i < w.bgGradients.size() && !buckets.empty(); ++i) {
            const auto& bucket = buckets[i % buckets.size()];
            if (bucket.empty()) continue;
            bool okX = false, okY = false;
            float bx = resolveLen(bucket[0], okX);
            float by = bucket.size() >= 2 ? resolveLen(bucket[1], okY) : 0.0f;
            if (okX) w.bgGradients[i].posX = bx;
            if (okY) w.bgGradients[i].posY = by;
        }
    }

    // Opacity
    if (s.Has("opacity")) {
        try { w.opacity = std::stof(s.Get("opacity")); } catch (...) {}
    }

    // Display:
    // - none           → visible=false (collapse)
    // - inline / inline-block → intrinsic cross-size (don't stretch)
    // - block          → flex item that stretches (default for div etc.)
    // - flex / grid 等 → 暂忽略，按当前 widget 既定行为走
    if (s.Has("display")) {
        const std::string& v = s.Get("display");
        if      (v == "none")          w.visible = false;
        else if (v == "inline")        w.display = Display::Inline;
        else if (v == "inline-block")  w.display = Display::InlineBlock;
        else if (v == "block")         w.display = Display::Block;
    }

    // Visibility: hidden → visible=false (we collapse the distinction)
    if (s.Has("visibility") && s.Get("visibility") == "hidden") {
        w.visible = false;
    }

    // Cursor handling: core-ui doesn't expose cursor on Widget base, skip.
    // ID is assigned by the caller from the attr, not CSS.

    // CSS transition → widget.transitions (consumed by PageState::ApplyBindingToWidget)
    if (s.Has("transition")) {
        ParseTransitions(w, s.Get("transition"));
    }

    // CSS transform: translate / rotate / scale (composable list).
    if (s.Has("transform")) {
        auto pv = ui::css::ParseValue(s.Get("transform"));
        for (auto& comp : pv.components) {
            if (comp.kind != ui::css::ComponentKind::Function) continue;
            std::string lname;
            for (char c : comp.ident) lname += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            std::vector<ui::css::Component> positional;
            for (auto& a : comp.args) if (a.kind != ui::css::ComponentKind::Comma) positional.push_back(a);

            auto toPx = [](const ui::css::Component& c) -> float {
                if (c.kind == ui::css::ComponentKind::Length) {
                    double px = 0;
                    if (ui::css::ResolveLengthPx(c.length, 0, 14, 14, 0, 0, px)) return static_cast<float>(px);
                }
                if (c.kind == ui::css::ComponentKind::Number) return static_cast<float>(c.number);
                return 0.0f;
            };
            auto toNum = [](const ui::css::Component& c) -> float {
                if (c.kind == ui::css::ComponentKind::Number) return static_cast<float>(c.number);
                if (c.kind == ui::css::ComponentKind::Length) return static_cast<float>(c.length.value);
                return 0.0f;
            };
            auto toDeg = [](const ui::css::Component& c) -> float {
                if (c.kind == ui::css::ComponentKind::Length && c.length.unit == ui::css::Unit::Deg)
                    return static_cast<float>(c.length.value);
                if (c.kind == ui::css::ComponentKind::Number) return static_cast<float>(c.number);
                return 0.0f;
            };

            if (lname == "translate") {
                if (positional.size() >= 1) w.transformX = toPx(positional[0]);
                if (positional.size() >= 2) w.transformY = toPx(positional[1]);
            } else if (lname == "translatex") {
                if (positional.size() >= 1) w.transformX = toPx(positional[0]);
            } else if (lname == "translatey") {
                if (positional.size() >= 1) w.transformY = toPx(positional[0]);
            } else if (lname == "rotate") {
                if (positional.size() >= 1) w.rotateDeg = toDeg(positional[0]);
            } else if (lname == "scale") {
                if (positional.size() >= 1) {
                    float sx = toNum(positional[0]);
                    w.scaleX = sx;
                    w.scaleY = positional.size() >= 2 ? toNum(positional[1]) : sx;
                }
            } else if (lname == "scalex") {
                if (positional.size() >= 1) w.scaleX = toNum(positional[0]);
            } else if (lname == "scaley") {
                if (positional.size() >= 1) w.scaleY = toNum(positional[0]);
            }
        }
    }

    // CSS box-shadow — supports comma-separated list of shadows and the
    // `inset` keyword. Each shadow has up to 4 lengths + 1 color + optional
    // `inset`. Multiple shadows are stacked back-to-front.
    if (s.Has("box-shadow")) {
        const std::string& bs = s.Get("box-shadow");
        if (bs != "none" && !bs.empty()) {
            auto pv = ui::css::ParseValue(bs);
            // Group components into per-shadow buckets at top-level commas.
            std::vector<std::vector<ui::css::Component>> shadows;
            shadows.emplace_back();
            for (auto& c : pv.components) {
                if (c.kind == ui::css::ComponentKind::Comma) shadows.emplace_back();
                else shadows.back().push_back(c);
            }

            w.boxShadows.clear();
            for (auto& shComps : shadows) {
                if (shComps.empty()) continue;
                std::vector<float> nums;
                ui::css::Color color;
                bool hasColor = false;
                bool inset = false;
                for (auto& c : shComps) {
                    if (c.kind == ui::css::ComponentKind::Length) {
                        double px = 0;
                        if (ui::css::ResolveLengthPx(c.length, 0, 14, 14, 0, 0, px))
                            nums.push_back(static_cast<float>(px));
                    } else if (c.kind == ui::css::ComponentKind::Number) {
                        nums.push_back(static_cast<float>(c.number));
                    } else if (c.kind == ui::css::ComponentKind::Color) {
                        color = c.color;
                        hasColor = true;
                    } else if (c.kind == ui::css::ComponentKind::Ident) {
                        std::string id;
                        for (char ch : c.ident) id += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                        if (id == "inset") inset = true;
                    }
                }
                Widget::BoxShadowEx sh;
                if (nums.size() >= 1) sh.offsetX = nums[0];
                if (nums.size() >= 2) sh.offsetY = nums[1];
                if (nums.size() >= 3) sh.blur    = nums[2];
                if (nums.size() >= 4) sh.spread  = nums[3];
                sh.color = hasColor ? D2D1_COLOR_F{color.r, color.g, color.b, color.a}
                                    : D2D1_COLOR_F{0, 0, 0, 0.25f};
                sh.inset = inset;
                w.boxShadows.push_back(sh);
            }
            // Mirror the first non-inset shadow into the legacy single-shadow
            // field so widgets that read `boxShadow` still work.
            for (auto& sh : w.boxShadows) {
                if (!sh.inset) {
                    w.boxShadow.set = true;
                    w.boxShadow.offsetX = sh.offsetX;
                    w.boxShadow.offsetY = sh.offsetY;
                    w.boxShadow.blur    = sh.blur;
                    w.boxShadow.spread  = sh.spread;
                    w.boxShadow.color   = sh.color;
                    break;
                }
            }
        }
    }

    auto parseBlur = [&](const std::string& raw) -> float {
        if (raw.empty() || raw == "none") return 0.0f;
        auto pv = ui::css::ParseValue(raw);
        auto compToPx = [&](const ui::css::Component& c) -> float {
            if (c.kind == ui::css::ComponentKind::Length) {
                double px = 0.0;
                if (ui::css::ResolveLengthPx(c.length, 0, 14, 14, 1920, 1080, px))
                    return static_cast<float>(px);
            } else if (c.kind == ui::css::ComponentKind::Number) {
                return static_cast<float>(c.number);
            }
            return 0.0f;
        };
        for (const auto& c : pv.components) {
            if (c.kind == ui::css::ComponentKind::Function && c.ident == "blur" &&
                !c.args.empty()) {
                return compToPx(c.args[0]);
            }
            float px = compToPx(c);
            if (px > 0.0f) return px;
        }
        return 0.0f;
    };
    if (s.Has("backdrop-filter")) {
        w.backdropBlur = parseBlur(s.Get("backdrop-filter"));
    }
    if (s.Has("backdrop-blur")) {
        w.backdropBlur = parseBlur(s.Get("backdrop-blur"));
    }

    // ---- Style overrides that map into Widget::CssOverride so built-in
    //      controls (TextInput, Slider, CheckBox, etc.) can read them in their
    //      OnDraw and break away from theme defaults.
    auto parseColor = [](const std::string& v, D2D1_COLOR_F& out) -> bool {
        ui::css::Color c;
        if (!ui::css::ParseColor(v, c)) return false;
        out = D2D1_COLOR_F{c.r, c.g, c.b, c.a};
        return true;
    };

    if (s.Has("color")) {
        D2D1_COLOR_F c;
        if (parseColor(s.Get("color"), c)) { w.css.hasFg = true; w.css.fg = c; }
    }
    if (s.Has("accent-color")) {
        D2D1_COLOR_F c;
        if (parseColor(s.Get("accent-color"), c)) { w.css.hasAccent = true; w.css.accent = c; }
    }
    if (s.Has("border-color")) {
        D2D1_COLOR_F c;
        if (parseColor(s.Get("border-color"), c)) { w.css.hasBorderColor = true; w.css.borderColor = c; }
    }
    if (s.Has("border-width")) {
        w.css.borderWidth = ResolvePx(s.Get("border-width"));
    }
    if (s.Has("border")) {
        // Shorthand: "<width> [solid] <color>" — parse lazily.
        auto pv = ui::css::ParseValue(s.Get("border"));
        for (auto& c : pv.components) {
            if (c.kind == ui::css::ComponentKind::Length) {
                double px = 0;
                if (ui::css::ResolveLengthPx(c.length, 0, 14, 14, 0, 0, px))
                    w.css.borderWidth = static_cast<float>(px);
            } else if (c.kind == ui::css::ComponentKind::Color) {
                w.css.hasBorderColor = true;
                w.css.borderColor = D2D1_COLOR_F{c.color.r, c.color.g, c.color.b, c.color.a};
            }
        }
    }
    if (s.Has("border-radius")) {
        w.css.borderRadius = ResolvePx(s.Get("border-radius"));
    }
    if (s.Has("font-size")) {
        w.css.fontSize = ResolvePx(s.Get("font-size"));
    }
    if (s.Has("placeholder-color")) {
        D2D1_COLOR_F c;
        if (parseColor(s.Get("placeholder-color"), c)) {
            w.css.hasPlaceholderColor = true; w.css.placeholderColor = c;
        }
    }
    if (s.Has("caret-color")) {
        D2D1_COLOR_F c;
        if (parseColor(s.Get("caret-color"), c)) { w.css.hasCaretColor = true; w.css.caretColor = c; }
    }
    // Text selection colors（TextInput / TextArea / Label 拖选）
    if (s.Has("selection-background-color")) {
        D2D1_COLOR_F c;
        if (parseColor(s.Get("selection-background-color"), c)) {
            w.css.hasSelTextBg = true; w.css.selTextBg = c;
        }
    }
    if (s.Has("selection-color")) {
        D2D1_COLOR_F c;
        if (parseColor(s.Get("selection-color"), c)) {
            w.css.hasSelTextFg = true; w.css.selTextFg = c;
        }
    }
    if (s.Has("selection-inactive-background-color")) {
        D2D1_COLOR_F c;
        if (parseColor(s.Get("selection-inactive-background-color"), c)) {
            w.css.hasSelTextBgInactive = true; w.css.selTextBgInactive = c;
        }
    }
    if (s.Has("selection-inactive-color")) {
        D2D1_COLOR_F c;
        if (parseColor(s.Get("selection-inactive-color"), c)) {
            w.css.hasSelTextFgInactive = true; w.css.selTextFgInactive = c;
        }
    }
    // user-select: text 让 Label 也支持鼠标拖选 + Ctrl+C 复制
    if (s.Has("user-select")) {
        const std::string& v = s.Get("user-select");
        if (auto* lbl = dynamic_cast<LabelWidget*>(&w)) {
            lbl->SetSelectable(v == "text" || v == "all");
        }
    }
    // white-space — applies to LabelWidget (build 93, L22).
    // CSS: nowrap → 单行渲染, 文本超出容器自动 ellipsis "abc..." (DWrite
    // 在 renderer.cpp 已实现, wrap=false 时自动启 trimming). normal /
    // pre-wrap / 缺省 = 保持默认 wrap=true.
    if (s.Has("white-space")) {
        if (auto* lbl = dynamic_cast<LabelWidget*>(&w)) {
            const std::string& v = s.Get("white-space");
            if (v == "nowrap") lbl->SetWrap(false);
            else if (v == "normal" || v == "pre-wrap") lbl->SetWrap(true);
        }
    }
    // line-height — applies to LabelWidget single-row height (build 92, L20).
    // CSS spec: unitless (1.3) = multiplier of font-size; with unit (17px) = absolute.
    // 0 / 缺省 = lib default 1.3 × font-size.
    if (s.Has("line-height")) {
        if (auto* lbl = dynamic_cast<LabelWidget*>(&w)) {
            const std::string& v = s.Get("line-height");
            // 尝试解析为 px 字符串 ("17px"); 失败 fallback unitless float.
            if (v.size() > 2 && v.substr(v.size() - 2) == "px") {
                try { lbl->SetLineHeightPx(std::stof(v.substr(0, v.size() - 2))); }
                catch (...) {}
            } else if (!v.empty()) {
                try { lbl->SetLineHeightRatio(std::stof(v)); } catch (...) {}
            }
        }
    }
    // text-align — applies to LabelWidget. ButtonWidget hardcodes CENTER in
    // its OnDraw, so this is a no-op on buttons.
    if (s.Has("text-align")) {
        if (auto* lbl = dynamic_cast<LabelWidget*>(&w)) {
            const std::string& v = s.Get("text-align");
            if      (v == "center")              lbl->Align(DWRITE_TEXT_ALIGNMENT_CENTER);
            else if (v == "right" || v == "end") lbl->Align(DWRITE_TEXT_ALIGNMENT_TRAILING);
            else if (v == "justify")             lbl->Align(DWRITE_TEXT_ALIGNMENT_JUSTIFIED);
            else                                 lbl->Align(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
    }
    // align-self — per-child override of container's align-items (cross-axis).
    if (s.Has("align-self")) {
        const std::string& v = s.Get("align-self");
        if      (v == "auto")                                w.alignSelfOverride = -1;
        else if (v == "flex-start" || v == "start")          w.alignSelfOverride = (int)LayoutAlign::Start;
        else if (v == "center")                              w.alignSelfOverride = (int)LayoutAlign::Center;
        else if (v == "flex-end"  || v == "end")             w.alignSelfOverride = (int)LayoutAlign::End;
        else if (v == "stretch")                             w.alignSelfOverride = (int)LayoutAlign::Stretch;
    }
    if (s.Has("selected-color")) {
        D2D1_COLOR_F c;
        if (parseColor(s.Get("selected-color"), c)) {
            w.css.hasSelectedColor = true; w.css.selectedColor = c;
        }
    }
    if (s.Has("item-bg") || s.Has("item-background")) {
        const std::string& v = s.Has("item-bg") ? s.Get("item-bg") : s.Get("item-background");
        D2D1_COLOR_F c;
        if (parseColor(v, c)) { w.css.hasItemBg = true; w.css.itemBg = c; }
    }
    if (s.Has("item-hover-bg") || s.Has("item-hover-background")) {
        const std::string& v = s.Has("item-hover-bg") ? s.Get("item-hover-bg") : s.Get("item-hover-background");
        D2D1_COLOR_F c;
        if (parseColor(v, c)) { w.css.hasItemHoverBg = true; w.css.itemHoverBg = c; }
    }
    if (s.Has("item-border-color")) {
        D2D1_COLOR_F c;
        if (parseColor(s.Get("item-border-color"), c)) {
            w.css.hasItemBorderColor = true; w.css.itemBorderColor = c;
        }
    }
    if (s.Has("selected-bg") || s.Has("selected-background")) {
        const std::string& v = s.Has("selected-bg") ? s.Get("selected-bg") : s.Get("selected-background");
        D2D1_COLOR_F c;
        if (parseColor(v, c)) { w.css.hasSelectedBg = true; w.css.selectedBg = c; }
    }
    if (s.Has("selected-radius")) {
        w.css.selectedRadius = ResolvePx(s.Get("selected-radius"));
    }
    if (s.Has("cursor")) {
        const std::string& v = s.Get("cursor");
        if      (v == "default" || v == "auto")        w.cursor = ui::CursorKind::Default;
        else if (v == "pointer" || v == "hand")        w.cursor = ui::CursorKind::Pointer;
        else if (v == "text")                           w.cursor = ui::CursorKind::Text;
        else if (v == "crosshair")                      w.cursor = ui::CursorKind::Crosshair;
        else if (v == "wait" || v == "progress")        w.cursor = ui::CursorKind::Wait;
        else if (v == "move" || v == "grab" || v == "grabbing") w.cursor = ui::CursorKind::Move;
        else if (v == "not-allowed" || v == "no-drop")  w.cursor = ui::CursorKind::NotAllowed;
        else if (v == "ew-resize" || v == "col-resize" ||
                 v == "e-resize" || v == "w-resize")    w.cursor = ui::CursorKind::EwResize;
        else if (v == "ns-resize" || v == "row-resize" ||
                 v == "n-resize" || v == "s-resize")    w.cursor = ui::CursorKind::NsResize;
        else if (v == "nesw-resize" || v == "ne-resize" ||
                 v == "sw-resize")                      w.cursor = ui::CursorKind::NeswResize;
        else if (v == "nwse-resize" || v == "nw-resize" ||
                 v == "se-resize")                      w.cursor = ui::CursorKind::NwseResize;
        else if (v == "help")                           w.cursor = ui::CursorKind::Help;
        else if (v == "none")                           w.cursor = ui::CursorKind::None;
    }

    // overflow: hidden — clip children to this widget's (rounded) rect.
    if (s.Has("overflow")) {
        const std::string& v = s.Get("overflow");
        w.overflowHidden = (v == "hidden" || v == "clip");
    }

    // Absolute positioning. For each side, eager-resolve plain px / em / rem;
    // % and calc() depend on the parent rect so they're stashed as raw strings
    // and resolved at layout time (see ResolveAbsSide in widget.cpp).
    if (s.Has("position") && s.Get("position") == "absolute") {
        w.positionAbsolute = true;
        auto setSide = [&](const char* prop, float& slot, std::string& rawSlot) {
            if (!s.Has(prop)) return;
            const std::string& v = s.Get(prop);
            ui::css::Length len;
            if (ui::css::ParseLength(v, len) && len.unit != ui::css::Unit::Auto &&
                len.unit != ui::css::Unit::Percent) {
                double px = 0;
                if (ui::css::ResolveLengthPx(len, 0, 14, 14, 1920, 1080, px)) {
                    slot = static_cast<float>(px);
                    return;
                }
            }
            // % or calc() (or unparseable) — defer to layout time.
            rawSlot = v;
        };
        setSide("left",   w.posLeft,   w.posLeftRaw);
        setSide("top",    w.posTop,    w.posTopRaw);
        setSide("right",  w.posRight,  w.posRightRaw);
        setSide("bottom", w.posBottom, w.posBottomRaw);
    }
}

void ApplyFlexContainerStyle(Widget& w, const ui::css::ComputedStyle& s) {
    // gap
    if (s.Has("gap") || s.Has("row-gap") || s.Has("column-gap")) {
        float gap = 0;
        if (s.Has("gap")) gap = ResolvePx(s.Get("gap"));
        else if (s.Has("row-gap")) gap = ResolvePx(s.Get("row-gap"));
        else if (s.Has("column-gap")) gap = ResolvePx(s.Get("column-gap"));
        // VBox/HBox both have Gap(float) via virtual DSL
        w.Gap(gap);
    }
    // justify-content — main-axis distribution
    if (s.Has("justify-content")) {
        const std::string& v = s.Get("justify-content");
        LayoutJustify j = LayoutJustify::Start;
        if (v == "flex-start" || v == "start") j = LayoutJustify::Start;
        else if (v == "flex-end" || v == "end") j = LayoutJustify::End;
        else if (v == "center")                 j = LayoutJustify::Center;
        else if (v == "space-between")          j = LayoutJustify::SpaceBetween;
        else if (v == "space-around")           j = LayoutJustify::SpaceAround;
        if (auto* vb = dynamic_cast<VBoxWidget*>(&w))      vb->MainJustify(j);
        else if (auto* hb = dynamic_cast<HBoxWidget*>(&w)) hb->MainJustify(j);
    }
    // flex-wrap — currently only HBox honors this (rows wrap; vertical stack
    // already grows along its axis so wrap is a no-op for VBox).
    if (s.Has("flex-wrap")) {
        const std::string& v = s.Get("flex-wrap");
        bool wrap = (v == "wrap" || v == "wrap-reverse");
        if (auto* hb = dynamic_cast<HBoxWidget*>(&w)) hb->FlexWrap(wrap);
    }
    // align-items — cross-axis alignment of children
    if (s.Has("align-items")) {
        const std::string& v = s.Get("align-items");
        LayoutAlign a = LayoutAlign::Stretch;
        if      (v == "flex-start" || v == "start") a = LayoutAlign::Start;
        else if (v == "center" || v == "baseline")  a = LayoutAlign::Center;  // baseline 退化
        else if (v == "flex-end"  || v == "end")    a = LayoutAlign::End;
        else if (v == "stretch")                    a = LayoutAlign::Stretch;
        if (auto* vb = dynamic_cast<VBoxWidget*>(&w))      vb->CrossAlign(a);
        else if (auto* hb = dynamic_cast<HBoxWidget*>(&w)) hb->CrossAlign(a);
    }
}

void ApplyFlexItemStyle(Widget& w, const ui::css::ComputedStyle& s) {
    if (s.Has("flex-grow")) {
        try {
            float g = std::stof(s.Get("flex-grow"));
            if (g > 0) { w.expanding = true; w.flex = g; }
        } catch (...) {}
    }
    if (s.Has("flex")) {
        // Shorthand: "none" | "<grow>" | "<grow> <shrink>" | "<grow> <shrink> <basis>".
        // 1st number → flex-grow (expanding), 2nd number → flex-shrink. "none" = 0 0.
        const std::string& fv = s.Get("flex");
        if (fv == "none") {
            w.expanding = false; w.flex = 0.0f; w.flexShrink = 0.0f;
        } else {
            auto pv = ui::css::ParseValue(fv);
            int numIdx = 0;
            for (const auto& c : pv.components) {
                if (c.kind != ui::css::ComponentKind::Number) continue;
                float n = static_cast<float>(c.number);
                if (numIdx == 0) {
                    if (n > 0) { w.expanding = true; w.flex = n; }
                    else       { w.expanding = false; }
                } else if (numIdx == 1) {
                    w.flexShrink = (n < 0 ? 0.0f : n);
                }
                numIdx++;
            }
        }
    }
    // Longhand flex-shrink wins over the shorthand (the cascade already resolved
    // the final per-property value, so just apply it last).
    if (s.Has("flex-shrink")) {
        try {
            float sh = std::stof(s.Get("flex-shrink"));
            w.flexShrink = (sh < 0 ? 0.0f : sh);
        } catch (...) {}
    }
}

namespace {

WidgetPtr ConstructByTag(const std::string& tag, const std::string& text,
                         const ui::css::ComputedStyle& style,
                         const ui::uix::Node& node) {
    std::wstring wtext = ToWide(text);

    // Honor flex-direction for containers: row → HBox, column (default) → VBox.
    auto makeContainer = [&]() -> WidgetPtr {
        bool isRow = false;
        if (style.Has("flex-direction")) {
            const std::string& d = style.Get("flex-direction");
            if (d == "row" || d == "row-reverse") isRow = true;
        }
        if (isRow) return std::make_shared<HBoxWidget>();
        return std::make_shared<VBoxWidget>();
    };

    // Layout containers
    if (tag == "div" || tag == "section" || tag == "article" || tag == "main" ||
        tag == "header" || tag == "footer" || tag == "aside" || tag == "nav") {
        return makeContainer();
    }
    if (tag == "ul" || tag == "ol") {
        return makeContainer();
    }
    if (tag == "li") {
        return std::make_shared<HBoxWidget>();
    }
    if (tag == "Stack" || tag == "stack") {
        return std::make_shared<StackWidget>();
    }
    if (tag == "SplitView" || tag == "split-view") {
        auto split = std::make_shared<SplitViewWidget>();
        for (const auto& a : node.attrs) {
            if (a.kind != ui::uix::AttrKind::Static) continue;
            if (a.name == "mode") {
                if (a.rawValue == "overlay") split->SetDisplayMode(SplitViewMode::Overlay);
                else if (a.rawValue == "inline") split->SetDisplayMode(SplitViewMode::Inline);
                else if (a.rawValue == "compactOverlay") split->SetDisplayMode(SplitViewMode::CompactOverlay);
                else if (a.rawValue == "compactInline") split->SetDisplayMode(SplitViewMode::CompactInline);
            } else if (a.name == "openPaneLength" || a.name == "open-pane-length") {
                const float length = ResolvePx(a.rawValue);
                if (length > 0.0f) split->SetOpenPaneLength(length);
            } else if (a.name == "compactPaneLength" || a.name == "compact-pane-length") {
                const float length = ResolvePx(a.rawValue);
                if (length >= 0.0f) split->SetCompactPaneLength(length);
            } else if (a.name == "open" && ParseBoolAttr(a.rawValue)) {
                split->SetPaneOpenImmediate(true);
            }
        }
        return split;
    }

    // Text widgets
    if (tag == "span" || tag == "a" || tag == "label" || tag == "small" || tag == "strong" || tag == "em") {
        // 默认开 wrap，匹配 DOM <label> / <span> 在窄容器里换行的预期。
        // 不需要换行（如导航文案）的，CSS 设 white-space: nowrap 或显式 wrap=false。
        auto lbl = std::make_shared<LabelWidget>(wtext);
        lbl->SetWrap(true);
        // CSS `display: inline` semantics — intrinsic cross-size, doesn't
        // stretch under align-items: stretch but still honors align-items:
        // center / end for positioning. Matches DOM <label>/<span>/<a>.
        // Override via CSS `display: block` for stretching, or
        // `align-self: stretch` per-element opt-in.
        lbl->display = Display::Inline;
        return lbl;
    }
    if (tag == "p") {
        auto lbl = std::make_shared<LabelWidget>(wtext);
        lbl->SetWrap(true);
        return lbl;
    }
    if (tag == "h1" || tag == "h2" || tag == "h3" || tag == "h4" || tag == "h5" || tag == "h6") {
        auto lbl = std::make_shared<LabelWidget>(wtext);
        float size = 14.0f;
        if (tag == "h1")      size = 28.0f;
        else if (tag == "h2") size = 22.0f;
        else if (tag == "h3") size = 18.0f;
        else if (tag == "h4") size = 16.0f;
        else if (tag == "h5") size = 14.0f;
        else                  size = 12.0f;
        lbl->FontSize(size);
        lbl->Bold();
        return lbl;
    }

    // Controls
    if (tag == "button") {
        auto btn = std::make_shared<ButtonWidget>(wtext);
        btn->cursor = ui::CursorKind::Pointer;   // matches web default
        // 与 .ui markup 对齐：<button type="primary"> 走 Fluent 2 accent 风
        for (const auto& a : node.attrs) {
            if (a.kind != ui::uix::AttrKind::Static) continue;
            if (a.name == "type" && a.rawValue == "primary") {
                btn->SetType(ButtonType::Primary);
            }
        }
        return btn;
    }
    if (tag == "NavItem" || tag == "nav-item") {
        std::wstring label = wtext;
        std::string svg;
        std::wstring glyph;
        bool selected = false;
        for (const auto& a : node.attrs) {
            if (a.kind != ui::uix::AttrKind::Static) continue;
            if (a.name == "text") label = ToWide(a.rawValue);
            else if (a.name == "svg") svg = a.rawValue;
            else if (a.name == "glyph") glyph = ToWide(a.rawValue);
            else if (a.name == "selected") selected = ParseBoolAttr(a.rawValue);
        }
        auto nav = std::make_shared<NavItemWidget>(label, svg);
        if (!glyph.empty()) nav->SetGlyph(glyph);
        nav->SetSelected(selected);
        nav->cursor = ui::CursorKind::Pointer;
        return nav;
    }
    if (tag == "hr") {
        return std::make_shared<SeparatorWidget>();
    }
    // <TitleBar> / <title-bar>: frameless-window custom chrome with built-in
    // minimize/maximize/close buttons. WireTitleBar (in ui_window.cpp) will
    // automatically bind those buttons to window actions when the root is set.
    if (tag == "TitleBar" || tag == "title-bar") {
        return std::make_shared<TitleBarWidget>(wtext);
    }
    // <ScrollView>: vertical scrollable container. Compiler wraps children into
    // a single content widget after recursion (see compiler.cpp).
    if (tag == "ScrollView" || tag == "scroll-view") {
        return std::make_shared<ScrollViewWidget>();
    }
    if (tag == "gh_img_view" || tag == "gh-img-view" ||
        tag == "gh_img" || tag == "gh-img") {
        return std::make_shared<GhImgViewWidget>();
    }
    // <ocr-img-view>: 图片划词画布。数据 (图像 + OCR 文本项) 全部走
    // ui_ocr_img_view_* C API 由宿主喂 —— 跟 gh-img-view 一样, 标签只负责
    // 把 widget 放进布局树, 宿主 ui_widget_find_by_id 拿到句柄后照常调用
    // 全部 C API。静态属性 highlight-color 可直接在 .uix 里定高亮色。
    if (tag == "ocr_img_view" || tag == "ocr-img-view" ||
        tag == "OcrImgView"   || tag == "ocr-img") {
        auto ov = std::make_shared<OcrImgViewWidget>();
        for (const auto& a : node.attrs) {
            if (a.kind != ui::uix::AttrKind::Static) continue;
            if (a.name == "highlight-color") {
                ui::css::Color c{};
                if (ui::css::ParseColor(a.rawValue, c)) ov->SetHighlightColor(ToD2DColor(c));
            } else if (a.name == "rotation") {
                try { ov->SetRotation(std::stoi(a.rawValue)); } catch (...) {}
            }
        }
        return ov;
    }
    if (tag == "Toggle" || tag == "toggle") {
        auto tg = std::make_shared<ToggleWidget>(wtext);
        tg->cursor = ui::CursorKind::Pointer;
        return tg;
    }
    if (tag == "ProgressBar" || tag == "progress-bar" || tag == "progressbar") {
        // Parse min/max/value/indeterminate from static attrs; defaults 0..100 value=0.
        float mn = 0.0f, mx = 100.0f, vl = 0.0f;
        bool indet = false;
        for (const auto& a : node.attrs) {
            if (a.kind != ui::uix::AttrKind::Static) continue;
            try {
                if      (a.name == "min")   mn = std::stof(a.rawValue);
                else if (a.name == "max")   mx = std::stof(a.rawValue);
                else if (a.name == "value") vl = std::stof(a.rawValue);
                else if (a.name == "indeterminate") {
                    indet = (a.rawValue == "true" || a.rawValue == "1" || a.rawValue.empty());
                }
            } catch (...) {}
        }
        auto pb = std::make_shared<ProgressBarWidget>(mn, mx, vl);
        if (indet) pb->SetIndeterminate(true);
        return pb;
    }
    if (tag == "Expander" || tag == "expander") {
        std::wstring header = wtext;
        for (const auto& a : node.attrs) {
            if (a.kind == ui::uix::AttrKind::Static && a.name == "header") {
                header = ToWide(a.rawValue);
                break;
            }
        }
        auto ex = std::make_shared<ExpanderWidget>(header);
        ex->cursor = ui::CursorKind::Pointer;
        return ex;
    }
    if (tag == "Separator" || tag == "separator") {
        return std::make_shared<SeparatorWidget>();
    }
    if (tag == "textarea") {
        std::wstring ph;
        // wrap default = true (browser DOM <textarea wrap="soft"> 默认软换行)
        bool wrap = true;
        for (const auto& a : node.attrs) {
            if (a.kind != ui::uix::AttrKind::Static) continue;
            if (a.name == "placeholder") ph = ToWide(a.rawValue);
            else if (a.name == "wrap") {
                // soft|hard|on|off|true|false (HTML uses soft/hard, our 别名)
                wrap = !(a.rawValue == "off" || a.rawValue == "hard" ||
                         a.rawValue == "false" || a.rawValue == "0");
            }
        }
        auto ta = std::make_shared<TextAreaWidget>(ph);
        ta->SetWrap(wrap);
        (void)wtext;  // text handled separately
        return ta;
    }
    if (tag == "chips-input" || tag == "ChipsInput") {
        std::wstring ph, labels, value;
        for (const auto& a : node.attrs) {
            if (a.kind != ui::uix::AttrKind::Static) continue;
            if (a.name == "placeholder") ph = ToWide(a.rawValue);
            else if (a.name == "chip-labels" || a.name == "chipLabels")
                labels = ToWide(a.rawValue);
            else if (a.name == "value") value = ToWide(a.rawValue);
        }
        auto ci = std::make_shared<ChipsInputWidget>(ph);
        if (!labels.empty()) ci->SetChipLabels(labels);
        if (!value.empty()) ci->SetValue(value);
        return ci;
    }
    if (tag == "select") {
        // ComboBox needs an item list; populated later from <option> children in compiler.
        auto cb = std::make_shared<ComboBoxWidget>(std::vector<std::wstring>{});
        cb->cursor = ui::CursorKind::Pointer;
        return cb;
    }
    if (tag == "tabs" || tag == "TabControl") {
        // Compiler folds <tab title="..."> children into AddTab calls.
        return std::make_shared<TabControlWidget>();
    }
    if (tag == "link" || tag == "meta" || tag == "head" || tag == "title") {
        // Meta tags. <link rel="stylesheet"> is already harvested in
        // ExtractLinkStylesheets() before HTML parsing. <meta>/<head>/<title>
        // currently no-op (could become app icon / window title later).
        return nullptr;
    }

    // <custom id="..." width=".." height=".."> — host-drawn widget.
    // 工厂只造空壳, 用户拿 ui_widget_find_by_id(root, "id") 取 handle 后,
    // 用现有的 ui_custom_on_draw / on_mouse_* / on_key_* 等 C API 装回调.
    // CSS width/height 走 ApplyCommonStyle 自然生效. 大写 <Custom> 跟 .ui
    // markup 的 <Custom> 标签同名兼容.
    if (tag == "custom" || tag == "Custom") {
        return std::make_shared<CustomWidget>();
    }

    // <img src="logo.png" width="..." height="..." object-fit="contain">
    // 走资源解析器 → ImageWidget。src 是 ui::asset 的 key（不是路径），上层
    // 应用通过 ui_asset_register_dir / _blob / _resolver 决定它怎么解析。
    if (tag == "img") {
        std::string src;
        ImageWidget::Fit fit = ImageWidget::Fit::Contain;
        float fw = 0, fh = 0;
        for (const auto& a : node.attrs) {
            if (a.kind != ui::uix::AttrKind::Static) continue;
            if (a.name == "src") src = a.rawValue;
            else if (a.name == "object-fit") {
                if      (a.rawValue == "fill")    fit = ImageWidget::Fit::Fill;
                else if (a.rawValue == "cover")   fit = ImageWidget::Fit::Cover;
                else if (a.rawValue == "contain") fit = ImageWidget::Fit::Contain;
                else if (a.rawValue == "none")    fit = ImageWidget::Fit::None;
            }
            // width/height 属性 → fixedW/fixedH，跟 <svg> 分支一致。
            // ImageWidget::SizeHint() 已用 fixedW/H（>0 时优先于固有尺寸）。
            // CSS width/height 仍可在 layout 阶段覆盖。
            else if (a.name == "width")  { try { fw = std::stof(a.rawValue); } catch (...) {} }
            else if (a.name == "height") { try { fh = std::stof(a.rawValue); } catch (...) {} }
        }
        auto img = std::make_shared<ImageWidget>(src);
        img->SetFit(fit);
        if (fw > 0) img->fixedW = fw;
        if (fh > 0) img->fixedH = fh;
        return img;
    }

    // <svg>: drawing container. Compiler folds child <circle>/<rect>/... into
    // shape entries on this widget (so they are not compiled as separate
    // widgets). Static width/height attrs become the viewport; same attrs
    // in CSS still control the widget's rect size.
    if (tag == "svg") {
        auto svg = std::make_shared<SvgWidget>();
        // SVG attribute model:
        //   width/height set the rendered widget size (fixedW / fixedH).
        //   viewBox="minX minY w h" sets the path coordinate system
        //     (vpWidth / vpHeight). The OnDraw scale factor is
        //     min(rectW/vpW, rectH/vpH); without a real viewBox, paths
        //     drawn against 0..24 coords will overflow a 20×20 widget.
        for (const auto& a : node.attrs) {
            if (a.kind != ui::uix::AttrKind::Static) continue;
            if (a.name == "width") {
                try { svg->fixedW = std::stof(a.rawValue); } catch (...) {}
            } else if (a.name == "height") {
                try { svg->fixedH = std::stof(a.rawValue); } catch (...) {}
            } else if (a.name == "viewBox") {
                // "minX minY width height" — accept any whitespace separator.
                float vals[4] = {0, 0, 0, 0};
                int idx = 0;
                std::string tok;
                auto flush = [&]() {
                    if (idx < 4 && !tok.empty()) {
                        try { vals[idx++] = std::stof(tok); } catch (...) {}
                    }
                    tok.clear();
                };
                for (char c : a.rawValue) {
                    if (c == ' ' || c == ',' || c == '\t' || c == '\n') flush();
                    else tok += c;
                }
                flush();
                if (idx == 4 && vals[2] > 0 && vals[3] > 0) {
                    svg->vpWidth  = vals[2];
                    svg->vpHeight = vals[3];
                }
            }
        }
        return svg;
    }

    // Default: container
    return std::make_shared<VBoxWidget>();
}

// Classify <input type="..."> into specific widgets.
WidgetPtr ConstructInput(const ui::uix::Node& node, const std::string& text) {
    std::string type = "text";
    for (const auto& a : node.attrs) {
        if (a.kind == ui::uix::AttrKind::Static && a.name == "type") {
            type = a.rawValue;
            break;
        }
    }
    std::wstring wtext = ToWide(text);

    if (type == "checkbox") {
        auto cb = std::make_shared<CheckBoxWidget>(wtext);
        cb->cursor = ui::CursorKind::Pointer;
        return cb;
    }
    if (type == "radio") {
        std::string group;
        for (const auto& a : node.attrs) {
            if (a.kind == ui::uix::AttrKind::Static && a.name == "name") { group = a.rawValue; break; }
        }
        auto rb = std::make_shared<RadioButtonWidget>(wtext, group);
        rb->cursor = ui::CursorKind::Pointer;
        return rb;
    }
    if (type == "range") {
        // 解析 min / max / value 静态属性（缺省 0..100, value=mid）
        float mn = 0.0f, mx = 100.0f, val = 50.0f;
        bool hasVal = false;
        for (const auto& a : node.attrs) {
            if (a.kind != ui::uix::AttrKind::Static) continue;
            try {
                if      (a.name == "min")   mn  = std::stof(a.rawValue);
                else if (a.name == "max")   mx  = std::stof(a.rawValue);
                else if (a.name == "value") { val = std::stof(a.rawValue); hasVal = true; }
            } catch (...) {}
        }
        if (mx <= mn) mx = mn + 1.0f;  // sane defaults
        if (!hasVal) val = (mn + mx) * 0.5f;
        if (val < mn) val = mn;
        if (val > mx) val = mx;
        auto slider = std::make_shared<SliderWidget>(mn, mx, val);
        slider->cursor = ui::CursorKind::Pointer;
        return slider;
    }
    if (type == "number") {
        // 解析 min / max / value / step
        float mn = 0.0f, mx = 100.0f, val = 0.0f, step = 1.0f;
        bool hasVal = false;
        for (const auto& a : node.attrs) {
            if (a.kind != ui::uix::AttrKind::Static) continue;
            try {
                if      (a.name == "min")   mn  = std::stof(a.rawValue);
                else if (a.name == "max")   mx  = std::stof(a.rawValue);
                else if (a.name == "value") { val = std::stof(a.rawValue); hasVal = true; }
                else if (a.name == "step")  step = std::stof(a.rawValue);
            } catch (...) {}
        }
        if (mx <= mn) mx = mn + 1.0f;
        if (step <= 0) step = 1.0f;
        if (!hasVal) val = mn;
        if (val < mn) val = mn;
        if (val > mx) val = mx;
        // Numberbox has a text-edit area, so keep text cursor by default.
        auto nb = std::make_shared<NumberBoxWidget>(mn, mx, val, step);
        // Derive display decimals from step: 0.1 → 1, 0.01 → 2, 1 → 0.
        // 没这一步的话 decimals_ 默认 0，0.5 会被格式化成 "0"。
        int decimals = 0;
        float s = step;
        while (s > 0 && s < 1.0f && decimals < 6) {
            s *= 10.0f;
            decimals++;
        }
        if (decimals > 0) nb->SetDecimals(decimals);
        return nb;
    }
    // Default: text input — Windows already maps this to IBEAM in ui_window.
    // 读 placeholder 属性
    std::wstring ph;
    for (const auto& a : node.attrs) {
        if (a.kind == ui::uix::AttrKind::Static && a.name == "placeholder") {
            ph = ToWide(a.rawValue);
            break;
        }
    }
    return std::make_shared<TextInputWidget>(ph);
}

}  // namespace

WidgetPtr BuildWidget(const ui::uix::Node& node, const ui::css::ComputedStyle& style) {
    if (node.kind != ui::uix::NodeKind::Element) {
        // Text and Interpolation handled as children by the compiler, not directly built as widgets
        return nullptr;
    }

    const std::string& tag = node.tag;
    std::string firstText = FirstPlainText(node);

    WidgetPtr w;
    if (tag == "input") {
        w = ConstructInput(node, firstText);
    } else {
        w = ConstructByTag(tag, firstText, style, node);
    }
    if (!w) return nullptr;

    // Common attrs: id, class applied here (class already used for selector match upstream)
    for (const auto& a : node.attrs) {
        if (a.kind == ui::uix::AttrKind::Static) {
            if (a.name == "id") w->id = a.rawValue;
            else if (a.name == "hitTransparent" || a.name == "hit-transparent") {
                w->hitTransparent = ParseBoolAttr(a.rawValue);
            }
            else if (a.name == "hitClient" || a.name == "hit-client") {
                w->hitClient = ParseBoolAttr(a.rawValue);
            }
            // HTML5-style drag & drop (dispatched by UiWindowImpl)
            else if (a.name == "draggable") {
                w->draggable = ParseBoolAttr(a.rawValue);
            }
            else if (a.name == "drag-data" || a.name == "dragData") {
                w->draggable = true;  // payload implies draggable
                w->dragData = ToWide(a.rawValue);
            }
            else if (a.name == "drop-target" || a.name == "dropTarget") {
                w->dropTarget = ParseBoolAttr(a.rawValue);
            }
            // For text-like widgets: value attribute for input, alt for img, etc. — skipped in v0
        }
    }

    // CSS spec defaults for replaced / form / inline-image widgets — they all
    // want intrinsic cross-axis sizing (don't honor align-items: stretch). User
    // can opt back into stretching via CSS `display: block` or per-element
    // `align-self: stretch`. ApplyCommonStyle below will overwrite if CSS
    // explicitly sets `display:` so this default is purely a fallback.
    // (LabelWidget / IconButtonWidget already handled in their factory branches:
    //  Inline + InlineBlock respectively.)
    if (dynamic_cast<ButtonWidget*>(w.get())          ||
        dynamic_cast<CaptionButtonWidget*>(w.get())   ||
        dynamic_cast<IconButtonWidget*>(w.get())      ||
        dynamic_cast<ImageWidget*>(w.get())           ||
        dynamic_cast<SvgWidget*>(w.get())             ||
        dynamic_cast<TextInputWidget*>(w.get())       ||
        dynamic_cast<TextAreaWidget*>(w.get())        ||
        dynamic_cast<ChipsInputWidget*>(w.get())      ||
        dynamic_cast<NumberBoxWidget*>(w.get())       ||
        dynamic_cast<ComboBoxWidget*>(w.get())        ||
        dynamic_cast<ToggleWidget*>(w.get())          ||
        dynamic_cast<CheckBoxWidget*>(w.get())        ||
        dynamic_cast<RadioButtonWidget*>(w.get())     ||
        dynamic_cast<SliderWidget*>(w.get())          ||
        dynamic_cast<ProgressBarWidget*>(w.get())) {
        w->display = Display::InlineBlock;
    }

    // Apply CSS
    ApplyCommonStyle(*w, style);
    ApplyFlexContainerStyle(*w, style);
    ApplyFlexItemStyle(*w, style);

    // TitleBar-specific attrs
    if (auto* tb = dynamic_cast<TitleBarWidget*>(w.get())) {
        auto parseBool = [](const std::string& v) -> int {
            if (v == "true" || v == "1" || v == "yes") return 1;
            if (v == "false" || v == "0" || v == "no") return 0;
            return -1;
        };
        for (const auto& a : node.attrs) {
            if (a.kind != ui::uix::AttrKind::Static) continue;
            if (a.name == "title")      tb->SetTitle(ToWide(a.rawValue));
            else if (a.name == "show-minimize" || a.name == "show-min") {
                int v = parseBool(a.rawValue); if (v >= 0) tb->SetShowMin(v != 0);
            } else if (a.name == "show-maximize" || a.name == "show-max") {
                int v = parseBool(a.rawValue); if (v >= 0) tb->SetShowMax(v != 0);
            } else if (a.name == "show-close") {
                int v = parseBool(a.rawValue); if (v >= 0) tb->SetShowClose(v != 0);
            } else if (a.name == "show-icon") {
                int v = parseBool(a.rawValue); if (v >= 0) tb->SetShowIcon(v != 0);
            }
        }
    }

    // font-size / bold / text-color on text widgets
    if (auto* lbl = dynamic_cast<LabelWidget*>(w.get())) {
        if (style.Has("font-size")) lbl->FontSize(ResolvePx(style.Get("font-size")));
        if (style.Has("font-weight")) {
            const std::string& fw = style.Get("font-weight");
            if (fw == "bold" || fw == "700") lbl->Bold();
        }
        if (style.Has("color")) {
            ui::css::Color c;
            if (ui::css::ParseColor(style.Get("color"), c)) {
                lbl->TextColor(ToD2DColor(c));
            }
        }
    } else if (auto* btn = dynamic_cast<ButtonWidget*>(w.get())) {
        if (style.Has("font-size")) btn->FontSize(ResolvePx(style.Get("font-size")));
        if (style.Has("color")) {
            ui::css::Color c;
            if (ui::css::ParseColor(style.Get("color"), c)) {
                btn->SetTextColor(ToD2DColor(c));
            }
        }
        // Background: ButtonWidget draws its own bg; feed CSS bg through SetCustomBgColor
        // so accent colors take effect. Falls back to Default/Primary palette otherwise.
        if (style.Has("background-color") || style.Has("background")) {
            const std::string& bg = style.Has("background-color")
                ? style.Get("background-color") : style.Get("background");
            ui::css::Color c;
            if (ui::css::ParseColor(bg, c)) {
                btn->SetCustomBgColor(ToD2DColor(c));
            }
        }
    }

    return w;
}

}  // namespace ui::page
