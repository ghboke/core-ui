#pragma once
#include "../widget.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ui::page {

enum class SvgShapeKind { Circle, Rect, Ellipse, Line, Polygon, Polyline, Path };

// One color stop inside a <linearGradient> or <radialGradient>.
struct SvgGradientStop {
    float offset = 0.0f;      // 0..1
    D2D1_COLOR_F color = {0, 0, 0, 1};
    float opacity = 1.0f;
};

// A <linearGradient> or <radialGradient> declaration from <defs>.
// Coordinates are in user (SVG) space, matching SVG's default
// gradientUnits="userSpaceOnUse".
struct SvgGradient {
    enum class Kind { Linear, Radial } kind = Kind::Linear;
    std::string id;

    // Linear: start/end points.
    float x1 = 0, y1 = 0, x2 = 1, y2 = 0;

    // Radial: center + radius. focalX/Y default to center.
    float cx = 0.5f, cy = 0.5f, radius = 0.5f;
    float fx = 0.5f, fy = 0.5f;

    std::vector<SvgGradientStop> stops;
};

// A single drawable primitive inside an <svg> element. Parameters that don't
// apply to a given kind are ignored.
struct SvgShape {
    SvgShapeKind kind = SvgShapeKind::Circle;

    // Paint state (SVG defaults: fill=black, stroke=none).
    // currentColor 标志：fill: currentColor / stroke: currentColor 时取
    // 最近 SvgWidget 的 css.fg（"color" CSS 属性的解析值），匹配浏览器的
    // 文本色继承语义。currentColor 优先级：颜色字面量 > gradient id >
    // currentColor flag > 默认黑。
    bool hasFill = true;
    bool fillIsCurrentColor = false;
    D2D1_COLOR_F fill = {0, 0, 0, 1};
    std::string fillGradientId;         // non-empty → use gradient, ignore color
    float fillOpacity = 1.0f;

    bool hasStroke = false;
    bool strokeIsCurrentColor = false;
    D2D1_COLOR_F stroke = {0, 0, 0, 1};
    std::string strokeGradientId;
    float strokeOpacity = 1.0f;
    float strokeWidth = 1.0f;
    std::vector<float> strokeDashArray;   // empty = solid
    enum class Cap  { Butt, Round, Square } strokeLineCap = Cap::Butt;
    enum class Join { Miter, Round, Bevel } strokeLineJoin = Join::Miter;

    float opacity = 1.0f;

    // Geometry (by kind)
    float cx = 0, cy = 0, r = 0;          // circle
    float rx = 0, ry = 0;                 // ellipse
    float x = 0, y = 0, w = 0, h = 0;     // rect
    float rectRx = 0, rectRy = 0;         // rect rounded corners
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0; // line
    std::vector<D2D1_POINT_2F> points;    // polygon/polyline
    std::string pathData;                 // path d="..." (supports M/L/H/V/Z)

    // Simple transform (single rotate(deg [cx cy]))
    bool  hasTransform = false;
    float transformAngle = 0;
    float transformCx = 0, transformCy = 0;
};

// Container widget for a single <svg> element. Holds a flat list of SvgShape
// primitives and draws them each frame. Children are NOT regular child widgets;
// they are stored as SvgShape entries via AddShape at compile time.
class UI_API SvgWidget : public Widget {
public:
    SvgWidget() = default;

    // Viewport dimensions (from <svg width="..." height="..."> or width/height
    // via CSS). The widget's rect is scaled to fit the viewport while
    // preserving aspect ratio.
    float vpWidth = 100.0f;
    float vpHeight = 100.0f;

    std::vector<SvgShape>& Shapes() { return shapes_; }
    const std::vector<SvgShape>& Shapes() const { return shapes_; }
    void AddShape(SvgShape s) { shapes_.push_back(std::move(s)); }

    // Apply a string attribute value to a shape. Used by compiled bindings
    // whose target is a shape[N].attr address — the page_state dispatcher
    // calls this when the bound expression changes.
    void SetShapeProperty(size_t shapeIdx, const std::string& name, const std::string& value);

    // 把运行时反应式 binding (经 SetShapeProperty) 设过的属性值补回到 `s` 上。
    // recomputeShapes (hover/press/focus 时从 static + CSS 重建 shape) 不含 Bind
    // 属性, 会丢掉反应式 :fill/:stroke 等设的值; 重建后调本方法补回 (最高优先级,
    // 覆盖 static/CSS), 否则 hover 一下反应式 SVG 颜色/几何就没了。无缓存则 no-op。
    void ReapplyBoundAttrs(size_t shapeIdx, SvgShape& s) const;

    // Register a gradient defined in <defs>. The gradient is stored by id and
    // referenced from shape fill/stroke via "url(#id)".
    void AddGradient(SvgGradient g) { gradients_[g.id] = std::move(g); }
    const SvgGradient* FindGradient(const std::string& id) const {
        auto it = gradients_.find(id);
        return it == gradients_.end() ? nullptr : &it->second;
    }

    /* build 304: <use href="#id"> / <use href="file.svg#id"> —— 图形与 viewBox 取自
     * 引用的 <symbol> (见 svg_symbols.h)。href 记下后在绘制前才解析, 因为 sprite
     * 可能写在文档后面; :href 绑定变化时重新解析。 */
    void SetUseHref(const std::string& href);
    const std::string& UseHref() const { return useHref_; }
    // 解析 href 并装入图形。true = 已拿到 symbol (无 <use> 时恒 true)。
    bool ResolveUse();

    void OnDraw(Renderer& r) override;
    D2D1_SIZE_F SizeHint() const override { return {vpWidth, vpHeight}; }

    // 由 compiler 安装：根据当前 ancestor 链 + state bits 重新跑 CSS-to-shape
    // 通道。Widget::recomputeStyle hook 在 hover/press/focus/类切换 时调用，
    // SvgWidget 借此让 :hover path / .sel .icon path 等 selector 真正动态生效。
    std::function<void()> recomputeShapes;

private:
    std::string BuildDocumentXml() const;

    std::vector<SvgShape> shapes_;
    std::unordered_map<std::string, SvgGradient> gradients_;
    std::string useHref_;
    bool useResolved_ = true;   // 没有 <use>, 或当前 href 已装入

    // shapeIdx → (attr name → 最后由 SetShapeProperty 应用的值). 反应式 :attr
    // binding 设值时同步缓存; recomputeShapes 重建 shape 后用 ReapplyBoundAttrs
    // 把 bind 值补回, 避免 hover/state 重渲染丢掉反应式 SVG 属性。
    std::unordered_map<size_t, std::unordered_map<std::string, std::string>> boundShapeAttrs_;
};

// Parse a single SVG-element attribute into a shape. Used by both the
// compiler (static attrs) and the runtime dispatcher (bound attrs).
void ApplySvgShapeAttr(SvgShape& s, const std::string& name, const std::string& value);

}  // namespace ui::page
