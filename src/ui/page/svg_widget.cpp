#include "svg_widget.h"
#include "svg_symbols.h"
#include "../renderer.h"
#include "../css/value.h"
#include "../theme.h"  // theme::Current() — fallback color for SVG currentColor

#include <d2d1_1.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

using Microsoft::WRL::ComPtr;

namespace ui::page {

namespace {

D2D1_COLOR_F ToD2D(const ui::css::Color& c) {
    return D2D1::ColorF(c.r, c.g, c.b, c.a);
}

// Parse a float list separated by commas or whitespace.
void ParseFloatList(const std::string& s, std::vector<float>& out) {
    out.clear();
    std::string tok;
    auto flush = [&]() {
        if (tok.empty()) return;
        try { out.push_back(std::stof(tok)); } catch (...) {}
        tok.clear();
    };
    for (char c : s) {
        if (c == ',' || c == ' ' || c == '\t' || c == '\n' || c == '\r') flush();
        else tok += c;
    }
    flush();
}

float ParseNum(const std::string& s, float dflt = 0.0f) {
    try { return std::stof(s); } catch (...) { return dflt; }
}

// Result of parsing fill="..." / stroke="...". Either a solid color, a gradient
// reference by id, or no-paint.
struct PaintResult {
    bool hasColor = false;
    bool isCurrentColor = false;       // fill: currentColor / stroke: currentColor
    D2D1_COLOR_F color = {0, 0, 0, 1};
    std::string gradientId;   // non-empty → use gradient
};

static std::string LowerStr(const std::string& s) {
    std::string o = s;
    for (auto& c : o) c = (char)std::tolower((unsigned char)c);
    return o;
}

PaintResult ParsePaintEx(const std::string& s) {
    PaintResult out;
    std::string t = s;
    while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(t.begin());
    while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();
    if (t.empty() || t == "none" || t == "transparent") return out;
    // currentColor — defer to draw time, take from SvgWidget's css.fg
    if (LowerStr(t) == "currentcolor") { out.isCurrentColor = true; return out; }
    // url(#id) → gradient reference
    if (t.size() > 5 && t.compare(0, 4, "url(") == 0 && t.back() == ')') {
        std::string inner = t.substr(4, t.size() - 5);
        while (!inner.empty() && (inner.front() == ' ' || inner.front() == '"' || inner.front() == '\'' || inner.front() == '#')) inner.erase(inner.begin());
        while (!inner.empty() && (inner.back()  == ' ' || inner.back()  == '"' || inner.back()  == '\'')) inner.pop_back();
        out.gradientId = inner;
        return out;
    }
    ui::css::Color c;
    if (ui::css::ParseColor(t, c)) { out.hasColor = true; out.color = ToD2D(c); }
    return out;
}

// Legacy helper kept for attribute parsing that still returns only color.
bool ParsePaint(const std::string& s, D2D1_COLOR_F& out) {
    auto r = ParsePaintEx(s);
    if (r.hasColor) { out = r.color; return true; }
    return false;
}

D2D1_CAP_STYLE ToD2DCap(SvgShape::Cap c) {
    switch (c) {
        case SvgShape::Cap::Round:  return D2D1_CAP_STYLE_ROUND;
        case SvgShape::Cap::Square: return D2D1_CAP_STYLE_SQUARE;
        default:                    return D2D1_CAP_STYLE_FLAT;
    }
}
D2D1_LINE_JOIN ToD2DJoin(SvgShape::Join j) {
    switch (j) {
        case SvgShape::Join::Round: return D2D1_LINE_JOIN_ROUND;
        case SvgShape::Join::Bevel: return D2D1_LINE_JOIN_BEVEL;
        default:                    return D2D1_LINE_JOIN_MITER;
    }
}

ComPtr<ID2D1StrokeStyle> MakeStrokeStyle(ID2D1Factory* f, const SvgShape& s, float scale) {
    D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
        ToD2DCap(s.strokeLineCap),
        ToD2DCap(s.strokeLineCap),
        ToD2DCap(s.strokeLineCap),
        ToD2DJoin(s.strokeLineJoin),
        4.0f,
        s.strokeDashArray.empty() ? D2D1_DASH_STYLE_SOLID : D2D1_DASH_STYLE_CUSTOM,
        0.0f);
    ComPtr<ID2D1StrokeStyle> style;
    std::vector<float> scaled;
    const float* dashes = nullptr;
    UINT32 dashCount = 0;
    if (!s.strokeDashArray.empty()) {
        // D2D's dash array is in stroke-width units; SVG's is in user units.
        // To keep dashes visually correct across our scale factor AND match
        // SVG semantics, pass the array divided by stroke width (before scaling).
        // SVG dashes are in user-space units; we pre-scale here.
        scaled.reserve(s.strokeDashArray.size());
        float swUser = std::max(s.strokeWidth, 0.0001f);
        for (float v : s.strokeDashArray) scaled.push_back((v * scale) / (swUser * scale));
        dashes = scaled.data();
        dashCount = (UINT32)scaled.size();
    }
    f->CreateStrokeStyle(props, dashes, dashCount, style.GetAddressOf());
    return style;
}

ComPtr<ID2D1PathGeometry> MakePathFromPoints(ID2D1Factory* f,
                                             const std::vector<D2D1_POINT_2F>& pts,
                                             bool closed) {
    ComPtr<ID2D1PathGeometry> path;
    f->CreatePathGeometry(path.GetAddressOf());
    if (!path || pts.empty()) return path;
    ComPtr<ID2D1GeometrySink> sink;
    path->Open(sink.GetAddressOf());
    if (!sink) return path;
    sink->BeginFigure(pts.front(), closed ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
    for (size_t i = 1; i < pts.size(); ++i) sink->AddLine(pts[i]);
    sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    sink->Close();
    return path;
}

// SVG path parser. Supports:
//   M/m L/l H/h V/v Z/z (move/line/horiz/vert/close)
//   C/c S/s            (cubic Bezier, smooth cubic)
//   Q/q T/t            (quadratic Bezier, smooth quadratic)
//   A/a                (elliptical arc — Material/Fluent icons rely heavily
//                       on this for rounded rectangles)
ComPtr<ID2D1PathGeometry> MakePathFromD(ID2D1Factory* f, const std::string& d) {
    ComPtr<ID2D1PathGeometry> path;
    f->CreatePathGeometry(path.GetAddressOf());
    if (!path || d.empty()) return path;
    ComPtr<ID2D1GeometrySink> sink;
    path->Open(sink.GetAddressOf());
    if (!sink) return path;

    float curX = 0, curY = 0;
    float subX = 0, subY = 0;
    bool inFigure = false;
    // Last cubic / quadratic control point — used by S/s and T/t to compute
    // the reflected control. Reset whenever the previous command wasn't a
    // matching curve type.
    float lastCx = 0, lastCy = 0; bool hasLastC = false;
    float lastQx = 0, lastQy = 0; bool hasLastQ = false;

    auto readNum = [](const std::string& s, size_t& i, float& out) -> bool {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == ',' || s[i] == '\n')) ++i;
        size_t start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
        // SVG 允许相邻数字共享小数点（如 "1.1.9" 是两个数 1.1 和 .9），
        // 因此每个数字最多只能含一个 '.'，遇到第二个 '.' 立即终止。
        bool seenDot = false;
        while (i < s.size()) {
            char ch = s[i];
            if (std::isdigit((unsigned char)ch)) { ++i; continue; }
            if (ch == '.' && !seenDot) { seenDot = true; ++i; continue; }
            break;
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            ++i;
            if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
            while (i < s.size() && std::isdigit((unsigned char)s[i])) ++i;
        }
        if (start == i) return false;
        try { out = std::stof(s.substr(start, i - start)); return true; }
        catch (...) { return false; }
    };

    // SVG arc 的 large-arc-flag 和 sweep-flag 必须是单字符 0/1，可以与
    // 邻近数字/标志直接连写（如 "a 2 2 0 00 -2 2" 实际是
    // "rx=2 ry=2 rot=0 large=0 sweep=0 x=-2 y=2"）。普通 readNum 会贪婪
    // 把 "00" 当成单个 0 读完，导致后续坐标错位。
    auto readFlag = [](const std::string& s, size_t& i, float& out) -> bool {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == ',' || s[i] == '\n')) ++i;
        if (i >= s.size()) return false;
        if (s[i] == '0') { out = 0.0f; ++i; return true; }
        if (s[i] == '1') { out = 1.0f; ++i; return true; }
        return false;
    };

    size_t i = 0;
    char lastCmd = 0;
    while (i < d.size()) {
        while (i < d.size() && (d[i] == ' ' || d[i] == '\t' || d[i] == ',' || d[i] == '\n')) ++i;
        if (i >= d.size()) break;
        char c = d[i];
        bool isCmd = (c == 'M' || c == 'm' || c == 'L' || c == 'l' ||
                      c == 'H' || c == 'h' || c == 'V' || c == 'v' ||
                      c == 'Z' || c == 'z' ||
                      c == 'C' || c == 'c' || c == 'S' || c == 's' ||
                      c == 'Q' || c == 'q' || c == 'T' || c == 't' ||
                      c == 'A' || c == 'a');
        char cmd;
        if (isCmd) { cmd = c; ++i; lastCmd = cmd; }
        else       { cmd = lastCmd; }  // implicit repeat of prior command
        bool rel = (cmd >= 'a' && cmd <= 'z');
        char upper = rel ? (char)(cmd - 'a' + 'A') : cmd;

        if (upper == 'M') {
            float x, y;
            if (!readNum(d, i, x) || !readNum(d, i, y)) break;
            if (rel) { x += curX; y += curY; }
            if (inFigure) sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->BeginFigure({x, y}, D2D1_FIGURE_BEGIN_FILLED);
            subX = x; subY = y;
            curX = x; curY = y;
            inFigure = true;
            lastCmd = rel ? 'l' : 'L';  // subsequent implicit pairs are lineto
        } else if (upper == 'L') {
            float x, y;
            if (!readNum(d, i, x) || !readNum(d, i, y)) break;
            if (rel) { x += curX; y += curY; }
            if (!inFigure) { sink->BeginFigure({curX, curY}, D2D1_FIGURE_BEGIN_FILLED); inFigure = true; }
            sink->AddLine({x, y});
            curX = x; curY = y;
        } else if (upper == 'H') {
            float x;
            if (!readNum(d, i, x)) break;
            if (rel) x += curX;
            if (!inFigure) { sink->BeginFigure({curX, curY}, D2D1_FIGURE_BEGIN_FILLED); inFigure = true; }
            sink->AddLine({x, curY});
            curX = x;
        } else if (upper == 'V') {
            float y;
            if (!readNum(d, i, y)) break;
            if (rel) y += curY;
            if (!inFigure) { sink->BeginFigure({curX, curY}, D2D1_FIGURE_BEGIN_FILLED); inFigure = true; }
            sink->AddLine({curX, y});
            curY = y;
        } else if (upper == 'Z') {
            if (inFigure) { sink->EndFigure(D2D1_FIGURE_END_CLOSED); inFigure = false; }
            curX = subX; curY = subY;
            hasLastC = hasLastQ = false;
        } else if (upper == 'C') {
            // Cubic Bezier: 3 points (c1x c1y c2x c2y x y)
            float c1x, c1y, c2x, c2y, x, y;
            if (!readNum(d, i, c1x) || !readNum(d, i, c1y) ||
                !readNum(d, i, c2x) || !readNum(d, i, c2y) ||
                !readNum(d, i, x)   || !readNum(d, i, y)) break;
            if (rel) { c1x += curX; c1y += curY; c2x += curX; c2y += curY; x += curX; y += curY; }
            if (!inFigure) { sink->BeginFigure({curX, curY}, D2D1_FIGURE_BEGIN_FILLED); inFigure = true; }
            D2D1_BEZIER_SEGMENT bz = { {c1x, c1y}, {c2x, c2y}, {x, y} };
            sink->AddBezier(bz);
            curX = x; curY = y;
            lastCx = c2x; lastCy = c2y; hasLastC = true; hasLastQ = false;
        } else if (upper == 'S') {
            // Smooth cubic: reflect previous c2 (or curX,curY if last wasn't C/S)
            float c1x = hasLastC ? (2*curX - lastCx) : curX;
            float c1y = hasLastC ? (2*curY - lastCy) : curY;
            float c2x, c2y, x, y;
            if (!readNum(d, i, c2x) || !readNum(d, i, c2y) ||
                !readNum(d, i, x)   || !readNum(d, i, y)) break;
            if (rel) { c2x += curX; c2y += curY; x += curX; y += curY; }
            if (!inFigure) { sink->BeginFigure({curX, curY}, D2D1_FIGURE_BEGIN_FILLED); inFigure = true; }
            D2D1_BEZIER_SEGMENT bz = { {c1x, c1y}, {c2x, c2y}, {x, y} };
            sink->AddBezier(bz);
            curX = x; curY = y;
            lastCx = c2x; lastCy = c2y; hasLastC = true; hasLastQ = false;
        } else if (upper == 'Q') {
            // Quadratic: 2 points (cx cy x y)
            float cx, cy, x, y;
            if (!readNum(d, i, cx) || !readNum(d, i, cy) ||
                !readNum(d, i, x)  || !readNum(d, i, y)) break;
            if (rel) { cx += curX; cy += curY; x += curX; y += curY; }
            if (!inFigure) { sink->BeginFigure({curX, curY}, D2D1_FIGURE_BEGIN_FILLED); inFigure = true; }
            D2D1_QUADRATIC_BEZIER_SEGMENT qb = { {cx, cy}, {x, y} };
            sink->AddQuadraticBezier(qb);
            curX = x; curY = y;
            lastQx = cx; lastQy = cy; hasLastQ = true; hasLastC = false;
        } else if (upper == 'T') {
            // Smooth quadratic: reflect previous control
            float cx = hasLastQ ? (2*curX - lastQx) : curX;
            float cy = hasLastQ ? (2*curY - lastQy) : curY;
            float x, y;
            if (!readNum(d, i, x) || !readNum(d, i, y)) break;
            if (rel) { x += curX; y += curY; }
            if (!inFigure) { sink->BeginFigure({curX, curY}, D2D1_FIGURE_BEGIN_FILLED); inFigure = true; }
            D2D1_QUADRATIC_BEZIER_SEGMENT qb = { {cx, cy}, {x, y} };
            sink->AddQuadraticBezier(qb);
            curX = x; curY = y;
            lastQx = cx; lastQy = cy; hasLastQ = true; hasLastC = false;
        } else if (upper == 'A') {
            // Elliptical arc: rx ry x-axis-rotation large-arc-flag sweep-flag x y
            // SVG spec maps directly to D2D1_ARC_SEGMENT.
            float rx, ry, xRot, lf, sf, x, y;
            if (!readNum(d, i, rx) || !readNum(d, i, ry) || !readNum(d, i, xRot) ||
                !readFlag(d, i, lf) || !readFlag(d, i, sf) ||
                !readNum(d, i, x)  || !readNum(d, i, y)) break;
            if (rel) { x += curX; y += curY; }
            if (!inFigure) { sink->BeginFigure({curX, curY}, D2D1_FIGURE_BEGIN_FILLED); inFigure = true; }
            // 退化情形：rx 或 ry = 0 时 SVG 规定退化为直线
            if (rx == 0.0f || ry == 0.0f) {
                sink->AddLine({x, y});
            } else {
                D2D1_ARC_SEGMENT arc = {};
                arc.point = {x, y};
                arc.size  = {std::fabs(rx), std::fabs(ry)};
                arc.rotationAngle = xRot;
                arc.sweepDirection = (sf != 0.0f) ? D2D1_SWEEP_DIRECTION_CLOCKWISE
                                                  : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
                arc.arcSize = (lf != 0.0f) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
                sink->AddArc(arc);
            }
            curX = x; curY = y;
            hasLastC = hasLastQ = false;
        } else {
            break;  // unknown command; bail
        }
    }
    if (inFigure) sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    return path;
}

// Build a D2D brush for a paint: solid color OR gradient-by-id. Caller passes
// the gradient registry. Returns nullptr if no-paint.
ComPtr<ID2D1Brush> MakeBrush(ID2D1RenderTarget* rt,
                             const D2D1_COLOR_F& color,
                             const std::string& gradientId,
                             float opacity,
                             float shapeOpacity,
                             const SvgWidget* owner,
                             float offX, float offY, float scale) {
    ComPtr<ID2D1Brush> outBrush;
    float totalA = opacity * shapeOpacity;
    if (!gradientId.empty() && owner) {
        const SvgGradient* g = owner->FindGradient(gradientId);
        if (g && !g->stops.empty()) {
            std::vector<D2D1_GRADIENT_STOP> d2dStops;
            d2dStops.reserve(g->stops.size());
            for (const auto& st : g->stops) {
                D2D1_GRADIENT_STOP d; d.position = st.offset;
                D2D1_COLOR_F c = st.color;
                c.a *= st.opacity * totalA;
                d.color = c;
                d2dStops.push_back(d);
            }
            ComPtr<ID2D1GradientStopCollection> stops;
            rt->CreateGradientStopCollection(d2dStops.data(), (UINT32)d2dStops.size(),
                                             D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP,
                                             stops.GetAddressOf());
            if (g->kind == SvgGradient::Kind::Linear) {
                D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props = {
                    {offX + g->x1 * scale, offY + g->y1 * scale},
                    {offX + g->x2 * scale, offY + g->y2 * scale}
                };
                ComPtr<ID2D1LinearGradientBrush> lin;
                rt->CreateLinearGradientBrush(props, stops.Get(), lin.GetAddressOf());
                if (lin) outBrush = lin;
            } else {
                D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES props = {};
                props.center         = {offX + g->cx * scale, offY + g->cy * scale};
                props.gradientOriginOffset = {(g->fx - g->cx) * scale, (g->fy - g->cy) * scale};
                props.radiusX        = g->radius * scale;
                props.radiusY        = g->radius * scale;
                ComPtr<ID2D1RadialGradientBrush> rad;
                rt->CreateRadialGradientBrush(props, stops.Get(), rad.GetAddressOf());
                if (rad) outBrush = rad;
            }
        }
    }
    if (!outBrush) {
        D2D1_COLOR_F c = color;
        c.a *= totalA;
        ComPtr<ID2D1SolidColorBrush> solid;
        rt->CreateSolidColorBrush(c, solid.GetAddressOf());
        if (solid) outBrush = solid;
    }
    return outBrush;
}

D2D1_COLOR_F ResolveCurrentColor(const Widget* start) {
    for (const Widget* w = start; w; w = w->Parent()) {
        if (w->css.hasFg) return w->css.fg;
    }
    return ::theme::Current().foreground1;
}

static std::string XmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        switch (ch) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default: out += ch; break;
        }
    }
    return out;
}

static std::string ColorHex(D2D1_COLOR_F c) {
    auto toByte = [](float v) {
        v = std::clamp(v, 0.0f, 1.0f);
        return static_cast<unsigned>(std::lround(v * 255.0f));
    };
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X",
                  toByte(c.r), toByte(c.g), toByte(c.b));
    return buf;
}

static const char* CapName(SvgShape::Cap c) {
    switch (c) {
    case SvgShape::Cap::Round: return "round";
    case SvgShape::Cap::Square: return "square";
    default: return "butt";
    }
}

static const char* JoinName(SvgShape::Join j) {
    switch (j) {
    case SvgShape::Join::Round: return "round";
    case SvgShape::Join::Bevel: return "bevel";
    default: return "miter";
    }
}

static void AppendFloatList(std::ostringstream& os, const std::vector<float>& values) {
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) os << ' ';
        os << values[i];
    }
}

static void AppendPaintAttrs(std::ostringstream& os,
                             const char* prefix,
                             bool hasPaint,
                             bool isCurrentColor,
                             D2D1_COLOR_F color,
                             const std::string& gradientId,
                             float opacity,
                             const SvgWidget* owner) {
    os << ' ' << prefix << "=\"";
    if (!hasPaint) {
        os << "none\"";
        return;
    }
    if (!gradientId.empty()) {
        os << "url(#" << XmlEscape(gradientId) << ")\"";
    } else {
        if (isCurrentColor) color = ResolveCurrentColor(owner);
        os << ColorHex(color) << "\"";
        opacity *= color.a;
    }
    os << ' ' << prefix << "-opacity=\"" << std::clamp(opacity, 0.0f, 1.0f) << "\"";
}

void DrawShape(Renderer& r, float offX, float offY, float scale, const SvgShape& s,
               const SvgWidget* owner) {
    auto* rt = r.RT();
    auto* factory = r.Factory();
    if (!rt || !factory) return;

    auto sx = [&](float v) { return offX + v * scale; };
    auto sy = [&](float v) { return offY + v * scale; };

    // Resolve currentColor at draw time. Browser SVG default is fill="currentColor"
    // (= the element's text color, which inherits). Library has no CSS-inherit
    // system, so walk up parent chain to find the closest widget with `color`
    // explicitly set (css.hasFg=true). Falls back to opaque black like browser
    // initial color.
    D2D1_COLOR_F effFill = s.fill;
    D2D1_COLOR_F effStroke = s.stroke;
    if (s.fillIsCurrentColor)   effFill   = ResolveCurrentColor(owner);
    if (s.strokeIsCurrentColor) effStroke = ResolveCurrentColor(owner);

    ComPtr<ID2D1Brush> fillBrush, strokeBrush;
    if (s.hasFill)
        fillBrush = MakeBrush(rt, effFill, s.fillGradientId, s.fillOpacity, s.opacity,
                              owner, offX, offY, scale);
    if (s.hasStroke)
        strokeBrush = MakeBrush(rt, effStroke, s.strokeGradientId, s.strokeOpacity, s.opacity,
                                owner, offX, offY, scale);

    D2D1_MATRIX_3X2_F savedMatrix;
    rt->GetTransform(&savedMatrix);
    if (s.hasTransform && std::fabs(s.transformAngle) > 0.001f) {
        D2D1_POINT_2F pivot = {sx(s.transformCx), sy(s.transformCy)};
        D2D1_MATRIX_3X2_F rot = D2D1::Matrix3x2F::Rotation(s.transformAngle, pivot);
        rt->SetTransform(rot * savedMatrix);
    }

    const float sw = s.strokeWidth * scale;

    switch (s.kind) {
        case SvgShapeKind::Circle: {
            D2D1_ELLIPSE e = {{sx(s.cx), sy(s.cy)}, s.r * scale, s.r * scale};
            if (fillBrush)   rt->FillEllipse(e, fillBrush.Get());
            if (strokeBrush) {
                auto style = MakeStrokeStyle(factory, s, scale);
                rt->DrawEllipse(e, strokeBrush.Get(), sw, style.Get());
            }
            break;
        }
        case SvgShapeKind::Ellipse: {
            D2D1_ELLIPSE e = {{sx(s.cx), sy(s.cy)}, s.rx * scale, s.ry * scale};
            if (fillBrush)   rt->FillEllipse(e, fillBrush.Get());
            if (strokeBrush) {
                auto style = MakeStrokeStyle(factory, s, scale);
                rt->DrawEllipse(e, strokeBrush.Get(), sw, style.Get());
            }
            break;
        }
        case SvgShapeKind::Rect: {
            D2D1_RECT_F r2 = {sx(s.x), sy(s.y), sx(s.x + s.w), sy(s.y + s.h)};
            if (s.rectRx > 0 || s.rectRy > 0) {
                D2D1_ROUNDED_RECT rr = {r2, s.rectRx * scale, s.rectRy * scale};
                if (fillBrush)   rt->FillRoundedRectangle(rr, fillBrush.Get());
                if (strokeBrush) {
                    auto style = MakeStrokeStyle(factory, s, scale);
                    rt->DrawRoundedRectangle(rr, strokeBrush.Get(), sw, style.Get());
                }
            } else {
                if (fillBrush)   rt->FillRectangle(r2, fillBrush.Get());
                if (strokeBrush) {
                    auto style = MakeStrokeStyle(factory, s, scale);
                    rt->DrawRectangle(r2, strokeBrush.Get(), sw, style.Get());
                }
            }
            break;
        }
        case SvgShapeKind::Line: {
            D2D1_POINT_2F p1 = {sx(s.x1), sy(s.y1)};
            D2D1_POINT_2F p2 = {sx(s.x2), sy(s.y2)};
            if (strokeBrush) {
                auto style = MakeStrokeStyle(factory, s, scale);
                rt->DrawLine(p1, p2, strokeBrush.Get(), sw, style.Get());
            }
            break;
        }
        case SvgShapeKind::Polygon:
        case SvgShapeKind::Polyline: {
            std::vector<D2D1_POINT_2F> worldPts;
            worldPts.reserve(s.points.size());
            for (const auto& p : s.points) worldPts.push_back({sx(p.x), sy(p.y)});
            bool closed = (s.kind == SvgShapeKind::Polygon);
            auto path = MakePathFromPoints(factory, worldPts, closed);
            if (path) {
                if (fillBrush && closed) rt->FillGeometry(path.Get(), fillBrush.Get());
                if (strokeBrush) {
                    auto style = MakeStrokeStyle(factory, s, scale);
                    rt->DrawGeometry(path.Get(), strokeBrush.Get(), sw, style.Get());
                }
            }
            break;
        }
        case SvgShapeKind::Path: {
            auto path = MakePathFromD(factory, s.pathData);
            if (path) {
                // Bake origin + scale into the transform while drawing the
                // path, since its coordinates are in user space.
                D2D1_MATRIX_3X2_F cur;
                rt->GetTransform(&cur);
                D2D1_MATRIX_3X2_F local =
                    D2D1::Matrix3x2F::Scale({scale, scale}) *
                    D2D1::Matrix3x2F::Translation(offX, offY);
                rt->SetTransform(local * cur);
                if (fillBrush)   rt->FillGeometry(path.Get(), fillBrush.Get());
                if (strokeBrush) {
                    auto style = MakeStrokeStyle(factory, s, scale);
                    // Stroke width must be in user-space units here because
                    // we've multiplied the transform by `scale`.
                    rt->DrawGeometry(path.Get(), strokeBrush.Get(), s.strokeWidth, style.Get());
                }
                rt->SetTransform(cur);
            }
            break;
        }
    }

    rt->SetTransform(savedMatrix);
}

}  // namespace

void ApplySvgShapeAttr(SvgShape& s, const std::string& name, const std::string& value) {
    if      (name == "cx") s.cx = ParseNum(value);
    else if (name == "cy") s.cy = ParseNum(value);
    else if (name == "r")  s.r  = ParseNum(value);
    else if (name == "rx") { if (s.kind == SvgShapeKind::Rect) s.rectRx = ParseNum(value); else s.rx = ParseNum(value); }
    else if (name == "ry") { if (s.kind == SvgShapeKind::Rect) s.rectRy = ParseNum(value); else s.ry = ParseNum(value); }
    else if (name == "x")      s.x  = ParseNum(value);
    else if (name == "y")      s.y  = ParseNum(value);
    else if (name == "width")  s.w  = ParseNum(value);
    else if (name == "height") s.h  = ParseNum(value);
    else if (name == "x1")     s.x1 = ParseNum(value);
    else if (name == "y1")     s.y1 = ParseNum(value);
    else if (name == "x2")     s.x2 = ParseNum(value);
    else if (name == "y2")     s.y2 = ParseNum(value);
    else if (name == "fill") {
        auto pr = ParsePaintEx(value);
        s.hasFill = pr.hasColor || !pr.gradientId.empty() || pr.isCurrentColor;
        s.fillIsCurrentColor = pr.isCurrentColor;
        s.fill = pr.color;
        s.fillGradientId = pr.gradientId;
    }
    else if (name == "fill-opacity")     s.fillOpacity = ParseNum(value, 1.0f);
    else if (name == "stroke") {
        auto pr = ParsePaintEx(value);
        s.hasStroke = pr.hasColor || !pr.gradientId.empty() || pr.isCurrentColor;
        s.strokeIsCurrentColor = pr.isCurrentColor;
        s.stroke = pr.color;
        s.strokeGradientId = pr.gradientId;
    }
    else if (name == "stroke-opacity")   s.strokeOpacity = ParseNum(value, 1.0f);
    else if (name == "stroke-width")     s.strokeWidth = ParseNum(value, 1.0f);
    else if (name == "stroke-dasharray") ParseFloatList(value, s.strokeDashArray);
    else if (name == "stroke-linecap") {
        if (value == "round")       s.strokeLineCap = SvgShape::Cap::Round;
        else if (value == "square") s.strokeLineCap = SvgShape::Cap::Square;
        else                        s.strokeLineCap = SvgShape::Cap::Butt;
    }
    else if (name == "stroke-linejoin") {
        if (value == "round")      s.strokeLineJoin = SvgShape::Join::Round;
        else if (value == "bevel") s.strokeLineJoin = SvgShape::Join::Bevel;
        else                       s.strokeLineJoin = SvgShape::Join::Miter;
    }
    else if (name == "opacity") s.opacity = ParseNum(value, 1.0f);
    else if (name == "points") {
        std::vector<float> flat;
        ParseFloatList(value, flat);
        s.points.clear();
        for (size_t i = 0; i + 1 < flat.size(); i += 2) s.points.push_back({flat[i], flat[i + 1]});
    }
    else if (name == "d") {
        s.pathData = value;
    }
    else if (name == "transform") {
        if (value.compare(0, 7, "rotate(") == 0) {
            size_t close = value.find(')', 7);
            if (close != std::string::npos) {
                std::vector<float> nums;
                ParseFloatList(value.substr(7, close - 7), nums);
                if (!nums.empty()) {
                    s.hasTransform = true;
                    s.transformAngle = nums[0];
                    if (nums.size() >= 3) { s.transformCx = nums[1]; s.transformCy = nums[2]; }
                }
            }
        }
    }
}

void SvgWidget::SetShapeProperty(size_t shapeIdx, const std::string& name, const std::string& value) {
    if (shapeIdx >= shapes_.size()) return;
    ApplySvgShapeAttr(shapes_[shapeIdx], name, value);
    // 缓存反应式 binding 设的值, 供 recomputeShapes 重建 shape 后补回 (见 .h /
    // ReapplyBoundAttrs). 否则 hover/press/focus 重建 shape 会丢掉这个值。
    boundShapeAttrs_[shapeIdx][name] = value;
}

void SvgWidget::ReapplyBoundAttrs(size_t shapeIdx, SvgShape& s) const {
    auto it = boundShapeAttrs_.find(shapeIdx);
    if (it == boundShapeAttrs_.end()) return;
    for (const auto& [name, value] : it->second) {
        ApplySvgShapeAttr(s, name, value);
    }
}

std::string SvgWidget::BuildDocumentXml() const {
    std::ostringstream os;
    os << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << vpWidth
       << "\" height=\"" << vpHeight << "\" viewBox=\"0 0 " << vpWidth
       << ' ' << vpHeight << "\">";

    if (!gradients_.empty()) {
        os << "<defs>";
        for (const auto& kv : gradients_) {
            const SvgGradient& g = kv.second;
            if (g.id.empty() || g.stops.empty()) continue;
            if (g.kind == SvgGradient::Kind::Linear) {
                os << "<linearGradient id=\"" << XmlEscape(g.id)
                   << "\" gradientUnits=\"userSpaceOnUse\" x1=\"" << g.x1
                   << "\" y1=\"" << g.y1 << "\" x2=\"" << g.x2
                   << "\" y2=\"" << g.y2 << "\">";
            } else {
                os << "<radialGradient id=\"" << XmlEscape(g.id)
                   << "\" gradientUnits=\"userSpaceOnUse\" cx=\"" << g.cx
                   << "\" cy=\"" << g.cy << "\" r=\"" << g.radius
                   << "\" fx=\"" << g.fx << "\" fy=\"" << g.fy << "\">";
            }
            for (const auto& st : g.stops) {
                D2D1_COLOR_F c = st.color;
                os << "<stop offset=\"" << std::clamp(st.offset, 0.0f, 1.0f) * 100.0f
                   << "%\" stop-color=\"" << ColorHex(c)
                   << "\" stop-opacity=\"" << std::clamp(c.a * st.opacity, 0.0f, 1.0f)
                   << "\"/>";
            }
            os << (g.kind == SvgGradient::Kind::Linear ? "</linearGradient>" : "</radialGradient>");
        }
        os << "</defs>";
    }

    for (const auto& s : shapes_) {
        switch (s.kind) {
        case SvgShapeKind::Circle:
            os << "<circle cx=\"" << s.cx << "\" cy=\"" << s.cy << "\" r=\"" << s.r << "\"";
            break;
        case SvgShapeKind::Ellipse:
            os << "<ellipse cx=\"" << s.cx << "\" cy=\"" << s.cy
               << "\" rx=\"" << s.rx << "\" ry=\"" << s.ry << "\"";
            break;
        case SvgShapeKind::Rect:
            os << "<rect x=\"" << s.x << "\" y=\"" << s.y << "\" width=\"" << s.w
               << "\" height=\"" << s.h << "\"";
            if (s.rectRx > 0.0f) os << " rx=\"" << s.rectRx << "\"";
            if (s.rectRy > 0.0f) os << " ry=\"" << s.rectRy << "\"";
            break;
        case SvgShapeKind::Line:
            os << "<line x1=\"" << s.x1 << "\" y1=\"" << s.y1
               << "\" x2=\"" << s.x2 << "\" y2=\"" << s.y2 << "\"";
            break;
        case SvgShapeKind::Polygon:
        case SvgShapeKind::Polyline: {
            os << (s.kind == SvgShapeKind::Polygon ? "<polygon points=\"" : "<polyline points=\"");
            for (size_t i = 0; i < s.points.size(); ++i) {
                if (i) os << ' ';
                os << s.points[i].x << ',' << s.points[i].y;
            }
            os << "\"";
            break;
        }
        case SvgShapeKind::Path:
            os << "<path d=\"" << XmlEscape(s.pathData) << "\"";
            break;
        }

        const bool recordFill = s.hasFill &&
            s.kind != SvgShapeKind::Line &&
            s.kind != SvgShapeKind::Polyline;
        AppendPaintAttrs(os, "fill", recordFill, s.fillIsCurrentColor, s.fill,
                         s.fillGradientId, s.fillOpacity, this);
        AppendPaintAttrs(os, "stroke", s.hasStroke, s.strokeIsCurrentColor, s.stroke,
                         s.strokeGradientId, s.strokeOpacity, this);
        os << " stroke-width=\"" << s.strokeWidth << "\"";
        os << " stroke-linecap=\"" << CapName(s.strokeLineCap) << "\"";
        os << " stroke-linejoin=\"" << JoinName(s.strokeLineJoin) << "\"";
        if (!s.strokeDashArray.empty()) {
            os << " stroke-dasharray=\"";
            AppendFloatList(os, s.strokeDashArray);
            os << "\"";
        }
        os << " opacity=\"" << std::clamp(s.opacity, 0.0f, 1.0f) << "\"";
        if (s.hasTransform && std::fabs(s.transformAngle) > 0.001f) {
            os << " transform=\"rotate(" << s.transformAngle << ' '
               << s.transformCx << ' ' << s.transformCy << ")\"";
        }
        os << "/>";
    }
    os << "</svg>";
    return os.str();
}

void SvgWidget::SetUseHref(const std::string& href) {
    if (href == useHref_ && useResolved_) return;
    useHref_ = href;
    useResolved_ = false;
    shapes_.clear();            // 旧图标立刻撤下; 新的在 ResolveUse 装入
    boundShapeAttrs_.clear();   // shape 下标换了一套, 旧的绑定缓存作废
    // 重绘由调用方负责: :href 绑定走 ApplyBindingToWidget, 它结束时统一 invalidate。
}

bool SvgWidget::ResolveUse() {
    if (useResolved_) return true;
    const SvgSymbol* sym = ResolveSvgSymbol(useHref_);
    if (!sym) return false;     // 可能 sprite 还没编译到; 下次绘制再试
    shapes_ = sym->shapes;
    if (sym->vpWidth > 0 && sym->vpHeight > 0) {
        vpWidth = sym->vpWidth;
        vpHeight = sym->vpHeight;
    }
    useResolved_ = true;
    return true;
}

void SvgWidget::OnDraw(Renderer& r) {
    Widget::OnDraw(r);
    if (!visible) return;
    ResolveUse();

    float rectW = rect.right - rect.left;
    float rectH = rect.bottom - rect.top;
    if (rectW <= 0 || rectH <= 0 || vpWidth <= 0 || vpHeight <= 0) return;

    // Preserve aspect ratio: fit viewport inside the widget's rect and center.
    float scale = std::min(rectW / vpWidth, rectH / vpHeight);
    float drawnW = vpWidth  * scale;
    float drawnH = vpHeight * scale;
    float offX = rect.left + (rectW - drawnW) * 0.5f;
    float offY = rect.top  + (rectH - drawnH) * 0.5f;

    D2D1_MATRIX_3X2_F xf =
        D2D1::Matrix3x2F::Scale(scale, scale) *
        D2D1::Matrix3x2F::Translation(offX, offY);
    r.RecordSvgDocument(BuildDocumentXml(), vpWidth, vpHeight, xf);
    if (!r.RT() && r.IsRecordingDisplayList()) return;

    for (const auto& s : shapes_) DrawShape(r, offX, offY, scale, s, this);
}

}  // namespace ui::page
