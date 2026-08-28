#include "widget.h"
#include "renderer.h"
#include "event.h"
#include "theme.h"
#include "ui_context.h"

namespace ui {
void RequestLayout() {
    LayoutDirtyFlag() = true;
    GetContext().InvalidateAllWindows();
}

void Widget::ApplyDynamicPositionOverrides() {
    if (dynamicPosLeftSet) {
        posLeft = dynamicPosLeft;
        posLeftRaw = dynamicPosLeftRaw;
    }
    if (dynamicPosTopSet) {
        posTop = dynamicPosTop;
        posTopRaw = dynamicPosTopRaw;
    }
    if (dynamicPosRightSet) {
        posRight = dynamicPosRight;
        posRightRaw = dynamicPosRightRaw;
    }
    if (dynamicPosBottomSet) {
        posBottom = dynamicPosBottom;
        posBottomRaw = dynamicPosBottomRaw;
    }
}
}  // namespace ui
#include "css/value.h"

#include <d2d1_1.h>
#include <wrl/client.h>
#include <cmath>

namespace ui {

using Microsoft::WRL::ComPtr;

namespace {
// Resolve an absolute-position side value at layout time. raw is the CSS
// string stashed at factory time when the value uses % or calc() (which
// can't resolve without the parent size). Empty raw → return fallback (the
// already-resolved px from posLeft/etc, or -1 sentinel).
float ResolveAbsSide(const std::string& raw, float fallback, float parentSize) {
    if (raw.empty()) return fallback;
    auto pv = ui::css::ParseValue(raw);
    for (const auto& c : pv.components) {
        double px = 0;
        if (c.kind == ui::css::ComponentKind::Length) {
            if (ui::css::ResolveLengthPx(c.length, parentSize, 14, 14, 1920, 1080, px))
                return static_cast<float>(px);
        } else if (c.kind == ui::css::ComponentKind::Function && c.ident == "calc") {
            if (ui::css::EvalCalc(c, parentSize, 14, 14, 1920, 1080, px))
                return static_cast<float>(px);
        }
    }
    return fallback;
}

void ResolvePercentSizing(Widget* child, float parentW, float parentH) {
    if (!child) return;
    if (child->percentW    >= 0) child->fixedW = parentW * child->percentW    / 100.0f;
    if (child->percentH    >= 0) child->fixedH = parentH * child->percentH    / 100.0f;
    if (child->percentMinW >= 0) child->minW   = parentW * child->percentMinW / 100.0f;
    if (child->percentMinH >= 0) child->minH   = parentH * child->percentMinH / 100.0f;
    if (child->percentMaxW >= 0) child->maxW   = parentW * child->percentMaxW / 100.0f;
    if (child->percentMaxH >= 0) child->maxH   = parentH * child->percentMaxH / 100.0f;
}
}  // namespace

// Out-of-line so the dtor body can reach Context::NotifyWidgetDestroyed
// (the header only forward-declares Context). Required for windows to
// null out hovered/pressed/focused/tooltip pointers when a widget dies
// — otherwise OnMouseMove dereferences a freed pointer after a v-for
// row destroys itself out from under the cursor.
Widget::~Widget() {
    GetContext().NotifyWidgetDestroyed(this);
}

// ---- Widget tree ----

void Widget::AddChild(WidgetPtr child) {
    child->parent_ = this;
    children_.push_back(std::move(child));
}

void Widget::InsertChild(size_t index, WidgetPtr child) {
    if (!child) return;
    child->parent_ = this;
    if (index > children_.size()) index = children_.size();
    children_.insert(children_.begin() + index, std::move(child));
}

void Widget::RemoveChild(Widget* child) {
    children_.erase(
        std::remove_if(children_.begin(), children_.end(),
            [child](const WidgetPtr& p) { return p.get() == child; }),
        children_.end());
}

void Widget::RemoveChildren(const std::unordered_set<Widget*>& victims) {
    if (victims.empty() || children_.empty()) return;
    children_.erase(
        std::remove_if(children_.begin(), children_.end(),
            [&victims](const WidgetPtr& p) {
                return p && victims.count(p.get()) != 0;
            }),
        children_.end());
}

void Widget::ReplaceChildRange(size_t at, size_t removeCount,
                               std::vector<WidgetPtr> replacement) {
    if (at > children_.size()) at = children_.size();
    if (removeCount > children_.size() - at) removeCount = children_.size() - at;

    /* 一趟 erase (整段, 一次 memmove) */
    if (removeCount > 0) {
        children_.erase(children_.begin() + static_cast<ptrdiff_t>(at),
                        children_.begin() + static_cast<ptrdiff_t>(at + removeCount));
    }

    /* 空指针剔掉再整块插入 —— 保证一次 range-insert, 不退化成逐个插 */
    replacement.erase(
        std::remove_if(replacement.begin(), replacement.end(),
                       [](const WidgetPtr& p) { return !p; }),
        replacement.end());
    if (replacement.empty()) return;

    for (auto& c : replacement) c->parent_ = this;
    children_.insert(children_.begin() + static_cast<ptrdiff_t>(at),
                     std::make_move_iterator(replacement.begin()),
                     std::make_move_iterator(replacement.end()));
}

// ---- Default draw ----

namespace {

void DrawLayeredBoxShadowFallback(Renderer& r, const D2D1_RECT_F& base,
                                  float baseRadius,
                                  const Widget::BoxShadow& sh) {
    constexpr int LAYERS = 12;
    for (int i = LAYERS - 1; i >= 0; --i) {
        float t = static_cast<float>(i) / static_cast<float>(LAYERS - 1);  // 0..1
        float expand = sh.blur * (t * 1.5f - 0.5f);
        D2D1_RECT_F lyr{
            base.left   - expand,
            base.top    - expand,
            base.right  + expand,
            base.bottom + expand,
        };
        // 内缩过深(小 widget + 大 blur)会让矩形反转, 跳过该层。
        if (lyr.right <= lyr.left || lyr.bottom <= lyr.top) continue;
        // Alpha decays quadratically outward; innermost layer at full alpha.
        float a = sh.color.a * (1.0f - t) * (1.0f - t) / static_cast<float>(LAYERS);
        D2D1_COLOR_F c{ sh.color.r, sh.color.g, sh.color.b, a };
        float rad = baseRadius + expand; if (rad < 0.0f) rad = 0.0f;
        r.FillRoundedRect(lyr, rad, rad, c);
    }
}

// Draw a soft drop shadow behind the rect. Prefer a real D2D Gaussian blur; the
// old layered approximation remains only as a fallback for unavailable effects.
void DrawBoxShadow(Renderer& r, const D2D1_RECT_F& rect, const Widget::BoxShadow& sh, float radius) {
    if (!sh.set || sh.color.a <= 0.0f) return;
    D2D1_RECT_F base{
        rect.left   - sh.spread + sh.offsetX,
        rect.top    - sh.spread + sh.offsetY,
        rect.right  + sh.spread + sh.offsetX,
        rect.bottom + sh.spread + sh.offsetY,
    };
    float baseRadius = radius + sh.spread;
    if (sh.blur < 0.5f) {
        r.FillRoundedRect(base, baseRadius, baseRadius, sh.color);
        return;
    }
    if (!r.DrawBlurredRoundedRect(base, baseRadius, baseRadius, sh.blur, sh.color)) {
        DrawLayeredBoxShadowFallback(r, base, baseRadius, sh);
    }
}

D2D1_RECT_F ExpandForOutsetShadow(D2D1_RECT_F bounds, const D2D1_RECT_F& rect,
                                  float offsetX, float offsetY,
                                  float blur, float spread,
                                  D2D1_COLOR_F color) {
    if (color.a <= 0.0f) return bounds;
    const float extent = std::max(0.0f, spread) + std::max(0.0f, blur);
    bounds.left = std::min(bounds.left, rect.left + offsetX - extent);
    bounds.top = std::min(bounds.top, rect.top + offsetY - extent);
    bounds.right = std::max(bounds.right, rect.right + offsetX + extent);
    bounds.bottom = std::max(bounds.bottom, rect.bottom + offsetY + extent);
    return bounds;
}

D2D1_RECT_F WidgetOpacityBounds(const Widget& w) {
    D2D1_RECT_F bounds = w.rect;
    if (!w.boxShadows.empty()) {
        for (const auto& sh : w.boxShadows) {
            if (sh.inset) continue;
            bounds = ExpandForOutsetShadow(bounds, w.rect, sh.offsetX, sh.offsetY,
                                           sh.blur, sh.spread, sh.color);
        }
    } else if (w.boxShadow.set) {
        bounds = ExpandForOutsetShadow(bounds, w.rect, w.boxShadow.offsetX,
                                       w.boxShadow.offsetY, w.boxShadow.blur,
                                       w.boxShadow.spread, w.boxShadow.color);
    }
    return bounds;
}

// Draw an inset shadow inside the widget rect, approximating CSS
// `box-shadow: inset ...`. Sharp (blur=0) inset shadows are drawn as
// per-edge fill strips — this is the path used for underline / accent-block
// tab tricks like `box-shadow: inset 0 -2px 0 #2563eb`. Blurred inset
// shadows fall back to a concentric-stroke fade for the soft recessed look.
void DrawInsetBoxShadow(Renderer& r, const D2D1_RECT_F& rect,
                        const Widget::BoxShadowEx& sh, float radius) {
    if (sh.color.a <= 0.0f) return;
    if (radius > 0) r.PushRoundedClip(rect, radius, radius);
    else            r.PushClip(rect);

    if (sh.blur <= 0.5f) {
        // Sharp path: shadow = rect minus translated/shrunk hole.
        // Compute the 4 edge strips and fill them in shadow color.
        D2D1_RECT_F hole{
            rect.left   + sh.spread + sh.offsetX,
            rect.top    + sh.spread + sh.offsetY,
            rect.right  - sh.spread + sh.offsetX,
            rect.bottom - sh.spread + sh.offsetY,
        };
        auto clamp = [](float v, float lo, float hi) {
            return v < lo ? lo : (v > hi ? hi : v);
        };
        float hL = clamp(hole.left,   rect.left, rect.right);
        float hR = clamp(hole.right,  rect.left, rect.right);
        float hT = clamp(hole.top,    rect.top,  rect.bottom);
        float hB = clamp(hole.bottom, rect.top,  rect.bottom);

        // Top strip
        if (hT > rect.top)
            r.FillRect(D2D1::RectF(rect.left, rect.top, rect.right, hT), sh.color);
        // Bottom strip
        if (hB < rect.bottom)
            r.FillRect(D2D1::RectF(rect.left, hB, rect.right, rect.bottom), sh.color);
        // Left strip (only between hT and hB to avoid double-fill at corners)
        if (hL > rect.left && hB > hT)
            r.FillRect(D2D1::RectF(rect.left, hT, hL, hB), sh.color);
        // Right strip
        if (hR < rect.right && hB > hT)
            r.FillRect(D2D1::RectF(hR, hT, rect.right, hB), sh.color);
    } else {
        int LAYERS = std::max(2, (int)std::ceil(sh.blur));
        if (LAYERS > 16) LAYERS = 16;
        for (int i = 0; i < LAYERS; ++i) {
            float t = (float)i / (float)LAYERS;
            float inset = sh.spread + sh.blur * (1.0f - t);
            D2D1_RECT_F lyr{
                rect.left   + inset + sh.offsetX,
                rect.top    + inset + sh.offsetY,
                rect.right  - inset + sh.offsetX,
                rect.bottom - inset + sh.offsetY,
            };
            if (lyr.right <= lyr.left || lyr.bottom <= lyr.top) continue;
            float a = sh.color.a * (1.0f - t) / static_cast<float>(LAYERS);
            D2D1_COLOR_F c{ sh.color.r, sh.color.g, sh.color.b, a };
            float lyrR = std::max(0.0f, radius - inset);
            r.DrawRoundedRect(lyr, lyrR, lyrR, c, 1.0f);
        }
    }

    if (radius > 0) r.PopRoundedClip();
    else            r.PopClip();
}

GradientRef ToGradientRef(const Widget::BgGradient& layer) {
    GradientRef ref;
    ref.radial = layer.kind == Widget::BgGradient::Radial;
    ref.angle_deg = layer.angleDeg;
    ref.cx_pct = layer.cxPct;
    ref.cy_pct = layer.cyPct;
    ref.radius_pct = layer.radiusPct;
    ref.tile_w = layer.tileW;
    ref.tile_h = layer.tileH;
    ref.pos_x = layer.posX;
    ref.pos_y = layer.posY;
    ref.stops.reserve(layer.stops.size());
    for (const auto& stop : layer.stops) {
        ref.stops.push_back(GradientStopRef{stop.position, stop.color});
    }
    return ref;
}

}  // namespace

void Widget::OnDraw(Renderer& r) {
    if (bgColorFn) bgColor = bgColorFn();

    // Apply state-dependent background overrides
    D2D1_COLOR_F drawBg = bgColor;
    if (!enabled && stateColors.disabledBg) drawBg = stateColors.disabledBg();
    else if (pressed && stateColors.pressedBg) drawBg = stateColors.pressedBg();
    else if (hovered && stateColors.hoverBg)   drawBg = stateColors.hoverBg();

    // CSS spec: solid bgColor is the bottom layer; gradients paint on top.
    // Previously this was an `if/else if` so any gradient hid the bgColor —
    // user couldn't put a custom base color under a checker-pattern overlay.
    float radius = (css.borderRadius >= 0) ? css.borderRadius : 0.0f;
    if (drawBg.a > 0) {
        if (radius > 0) r.FillRoundedRect(rect, radius, radius, drawBg);
        else            r.FillRect(rect, drawBg);
    }
    if (hasBgGradient) {
        for (const auto& layer : bgGradients) {
            r.FillGradientRect(ToGradientRef(layer), rect, radius);
        }
    }

    // Inset shadows paint AFTER bg so they sit on top of the fill.
    for (auto& sh : boxShadows) {
        if (sh.inset) DrawInsetBoxShadow(r, rect, sh, radius);
    }

    // CSS border (drawn after bg + inset, before content).
    if ((css.hasBorderColor || css.borderWidth > 0)) {
        float bw = (css.borderWidth >= 0) ? css.borderWidth : 1.0f;
        if (bw > 0) {
            D2D1_COLOR_F bc = css.hasBorderColor ? css.borderColor
                                                  : D2D1_COLOR_F{0, 0, 0, 0.16f};
            if (radius > 0) r.DrawRoundedRect(rect, radius, radius, bc, bw);
            else            r.DrawRect(rect, bc, bw);
        }
    }
}

// ---- Default event handlers ----

uint32_t Widget::CurrentStateBits() const {
    uint32_t b = 0;
    if (hovered)   b |= (1u << 0);  // ui::css::state::Hover
    if (pressed)   b |= (1u << 1);  // Pressed
    if (focused_)  b |= (1u << 2);  // Focus
    if (!enabled)  b |= (1u << 3);  // Disabled
    if (dragOver)  b |= (1u << 6);  // DragOver
    return b;
}

// 状态位变化后, 除自身外还要让整棵子树重算样式 —— 后代选择器可以引用祖先
// 伪类 (如 `.zone:hover .btn`), 祖先 hover/pressed 翻转时后代自身 bits 不变,
// 不级联的话后代样式永远不刷新。
static void RecomputeSubtreeStyles(Widget* w) {
    for (auto& child : w->Children()) {
        if (child->recomputeStyle) {
            child->lastStateBits = child->CurrentStateBits();
            child->recomputeStyle(child->lastStateBits);
        }
        RecomputeSubtreeStyles(child.get());
    }
}

void Widget::RefreshCssState() {
    if (!recomputeStyle) return;
    uint32_t bits = CurrentStateBits();
    if (bits == lastStateBits) return;
    lastStateBits = bits;
    recomputeStyle(bits);
    RecomputeSubtreeStyles(this);
}

// NOTE: hook firing (onMouseXxxHook → @mousedown / @mouseup / etc.) used to
// happen here. Many widget subclasses override these methods without
// forwarding to Widget::OnMouseXxx, so half the hooks silently never fired.
// The dispatch site in ui_window.cpp now fires the hooks before invoking
// the widget's own OnMouseXxx, regardless of whether subclasses forward.
// Returning false here keeps event-bubble logic intact (subclass overrides
// can still claim the event by returning true).
bool Widget::OnMouseMove(const MouseEvent&)        { return false; }
bool Widget::OnMouseDown(const MouseEvent&)        { return false; }
bool Widget::OnMouseUp(const MouseEvent&)          { return false; }
bool Widget::OnMouseWheel(const MouseEvent&)       { return false; }
bool Widget::OnMouseDoubleClick(const MouseEvent&) { return false; }

// ---- Default layout (just position children at content area) ----

void Widget::DoLayout() {
    float cx = ContentLeft(), cy = ContentTop();
    float cw = ContentWidth(), ch = ContentHeight();
    for (auto& child : children_) {
        if (!child->visible) continue;

        // Resolve percentage sizes (incl. min/max constraints expressed as %)
        if (child->percentW    >= 0) child->fixedW = cw * child->percentW    / 100.0f;
        if (child->percentH    >= 0) child->fixedH = ch * child->percentH    / 100.0f;
        if (child->percentMinW >= 0) child->minW   = cw * child->percentMinW / 100.0f;
        if (child->percentMinH >= 0) child->minH   = ch * child->percentMinH / 100.0f;
        if (child->percentMaxW >= 0) child->maxW   = cw * child->percentMaxW / 100.0f;
        if (child->percentMaxH >= 0) child->maxH   = ch * child->percentMaxH / 100.0f;

        if (child->positionAbsolute) {
            if (child->layoutPinned &&
                child->rect.right - child->rect.left > 0) {
                child->DoLayout();   /* build 161: 钉住 — rect 不改写 */
                continue;
            }
            auto hint = child->SizeHint();
            float w = child->fixedW > 0 ? child->fixedW : (hint.width > 0 ? hint.width : cw);
            float h = child->fixedH > 0 ? child->fixedH : (hint.height > 0 ? hint.height : 24.0f);
            float pl = ResolveAbsSide(child->posLeftRaw,   child->posLeft,   cw);
            float pr = ResolveAbsSide(child->posRightRaw,  child->posRight,  cw);
            float pt = ResolveAbsSide(child->posTopRaw,    child->posTop,    ch);
            float pb = ResolveAbsSide(child->posBottomRaw, child->posBottom, ch);
            float x = cx, y = cy;
            if (pl >= 0) x = cx + pl;
            else if (pr >= 0) x = cx + cw - w - pr;
            if (pt >= 0) y = cy + pt;
            else if (pb >= 0) y = cy + ch - h - pb;
            child->rect = {x, y, x + w, y + h};
        } else if (child->fixedW > 0 && child->fixedH > 0) {
            child->rect = {cx, cy, cx + child->fixedW, cy + child->fixedH};
        } else {
            child->rect = {cx, cy, cx + cw, cy + ch};
        }
        child->DoLayout();
    }
}

// ---- Draw tree ----

// Recursively shift a widget's rect and all descendants' rects by (dx, dy).
// Used to apply CSS transform: translate(x, y) before drawing, then undone
// after; ScrollViewWidget also uses it to move content on pure scroll without
// re-running layout (build 285).
void ShiftSubtreeRects(Widget* w, float dx, float dy) {
    if (!w) return;
    w->rect.left += dx; w->rect.right += dx;
    w->rect.top  += dy; w->rect.bottom += dy;
    for (auto& c : w->Children()) ShiftSubtreeRects(c.get(), dx, dy);
}

/* 该 widget 会画到自己 rect 之外多远 (只算外扩型 box-shadow)。剔除判定按这个
 * 量放宽, 否则贴着视口边缘的行, 阴影会被整条剪掉。 */
static float OutsetPaintExtent(const Widget& w) {
    float ext = 0.0f;
    auto acc = [&ext](float ox, float oy, float blur, float spread) {
        const float reach = (std::max)(std::abs(ox), std::abs(oy)) + blur + spread;
        if (reach > ext) ext = reach;
    };
    for (const auto& sh : w.boxShadows) {
        if (sh.inset) continue;          /* inset 画在内部, 不外扩 */
        acc(sh.offsetX, sh.offsetY, sh.blur, sh.spread);
    }
    if (w.boxShadows.empty() && w.boxShadow.set) {
        acc(w.boxShadow.offsetX, w.boxShadow.offsetY,
            w.boxShadow.blur, w.boxShadow.spread);
    }
    return ext;
}

void Widget::DrawTree(Renderer& r) {
    if (!visible || opacity <= 0.0f) return;

    /* 视口剔除 (build 285, opt-in — 只在祖先 PushCull 过时生效)。
     * 带 transform 的 widget 不参与: translate 会在下面 ShiftSubtreeRects 里
     * 改写整棵子树的 rect, rotate/scale 更会让实际覆盖范围超出 rect, 此时
     * 拿未变换的 rect 判定会误剪。列表行不带 transform, 不影响收益。 */
    const bool hasAnyTransform = (transformX != 0.0f || transformY != 0.0f) ||
                                 (rotateDeg != 0.0f) ||
                                 (scaleX != 1.0f) || (scaleY != 1.0f);
    if (!hasAnyTransform) {
        const float ext = OutsetPaintExtent(*this);
        const D2D1_RECT_F probe{rect.left - ext, rect.top - ext,
                                rect.right + ext, rect.bottom + ext};
        if (r.IsCulled(probe)) return;
    }

    bool useLayer = (opacity < 1.0f);
    if (useLayer) r.PushOpacity(opacity, WidgetOpacityBounds(*this));

    const bool hasTranslate = (transformX != 0.0f || transformY != 0.0f);
    if (hasTranslate) ShiftSubtreeRects(this, transformX, transformY);

    // CSS transform: rotate / scale around the widget's center. Done via
    // ID2D1RenderTarget transform — does NOT affect layout (no reflow).
    bool hasMatrix = (rotateDeg != 0.0f) || (scaleX != 1.0f) || (scaleY != 1.0f);
    if (hasMatrix) {
        D2D1_POINT_2F pivot = {
            (rect.left + rect.right) * 0.5f,
            (rect.top  + rect.bottom) * 0.5f
        };
        D2D1::Matrix3x2F m = D2D1::Matrix3x2F::Translation(-pivot.x, -pivot.y);
        if (scaleX != 1.0f || scaleY != 1.0f)
            m = m * D2D1::Matrix3x2F::Scale(scaleX, scaleY);
        if (rotateDeg != 0.0f)
            m = m * D2D1::Matrix3x2F::Rotation(rotateDeg);
        m = m * D2D1::Matrix3x2F::Translation(pivot.x, pivot.y);
        r.PushTransform(m);
    }

    // Outset box-shadows paint behind the widget. Iterate the multi list
    // first; if it's empty fall back to the legacy single shadow.
    float bgRadius = (css.borderRadius >= 0) ? css.borderRadius : 0.0f;
    if (!boxShadows.empty()) {
        for (auto& sh : boxShadows) {
            if (sh.inset) continue;
            BoxShadow legacy;
            legacy.set = true;
            legacy.offsetX = sh.offsetX; legacy.offsetY = sh.offsetY;
            legacy.blur    = sh.blur;    legacy.spread  = sh.spread;
            legacy.color   = sh.color;
            DrawBoxShadow(r, rect, legacy, bgRadius);
        }
    } else if (boxShadow.set) {
        DrawBoxShadow(r, rect, boxShadow, bgRadius);
    }

    if (backdropBlur > 0.5f) {
        r.DrawBackdropBlur(rect, bgRadius, backdropBlur);
    }

    OnDraw(r);
    paintedOnce_ = true;   // L45: mount-phase transition gate, 见 widget.h PaintedOnce()
    DrawFocusRing(r);

    // CSS overflow:hidden — clip child paint to this widget's (rounded) shape
    // so e.g. per-tab hover backgrounds don't bleed past a rounded bar's
    // corners. Skipped when there are no children to avoid the clip overhead.
    bool clipChildren = overflowHidden && !children_.empty();
    if (clipChildren) {
        if (bgRadius > 0) r.PushRoundedClip(rect, bgRadius, bgRadius);
        else              r.PushClip(rect);
    }
    // 先画非绝对定位子节点(DOM 顺序), 再把绝对定位的画在最上层(各自 DOM 稳定序)。
    // CSS 里 positioned(absolute)元素默认盖在非定位元素之上; core-ui 不解析 z-index,
    // 用"绝对定位置顶"近似该语义。否则早 DOM 的绝对浮层(如全屏顶部标题栏)会被后面的
    // 兄弟(画布/image)覆盖 —— 只有晚 DOM 的浮层(底部 toolbar)能正常盖在画布上。
    bool hasAbsChild = false;
    for (auto& child : children_) {
        if (child->positionAbsolute) { hasAbsChild = true; continue; }
        child->DrawTree(r);
    }
    if (hasAbsChild) {
        for (auto& child : children_) {
            if (child->positionAbsolute) child->DrawTree(r);
        }
    }
    if (clipChildren) {
        if (bgRadius > 0) r.PopRoundedClip();
        else              r.PopClip();
    }

    if (hasMatrix) r.PopTransform();
    if (hasTranslate) ShiftSubtreeRects(this, -transformX, -transformY);

    if (useLayer) r.PopOpacity();
}

// ---- Draw overlays (e.g. dropdowns) on top of everything ----

void Widget::DrawOverlays(Renderer& r) {
    if (!visible) return;
    OnDrawOverlay(r);
    for (auto& child : children_) {
        child->DrawOverlays(r);
    }
}

// ---- Hit test (deepest child first) ----

Widget* Widget::HitTest(float x, float y) {
    if (!visible || !enabled || opacity <= 0.0f || !Contains(x, y)) return nullptr;
    // L219: 命中顺序必须跟 DrawTree 的绘制顺序一致 —— 绝对定位子节点画在最上层,
    // 所以也要先测它们(各自逆 DOM 序), 否则早 DOM 的绝对浮层(如全屏顶部标题栏)
    // 虽然画在画布之上, 点击却先命中"逻辑在它之后"的画布 → 浮层按钮点不到。
    for (int i = (int)children_.size() - 1; i >= 0; --i) {
        if (!children_[i]->positionAbsolute) continue;
        if (auto* hit = children_[i]->HitTest(x, y)) return hit;
    }
    for (int i = (int)children_.size() - 1; i >= 0; --i) {
        if (children_[i]->positionAbsolute) continue;
        if (auto* hit = children_[i]->HitTest(x, y)) return hit;
    }
    return hitTransparent ? nullptr : this;
}

// ---- Find by ID ----

Widget* Widget::FindById(const std::string& targetId) {
    if (id == targetId) return this;
    for (auto& child : children_) {
        if (auto* found = child->FindById(targetId)) return found;
    }
    return nullptr;
}

// ---- Focus ----

void Widget::DrawFocusRing(Renderer& r) {
    if (!focused_ || !focusable || !ShowFocusRing()) return;
    D2D1_RECT_F ring = {rect.left - 1, rect.top - 1, rect.right + 1, rect.bottom + 1};
    r.DrawRoundedRect(ring, 3, 3, theme::kAccent(), 1.5f);
}

void Widget::CollectFocusable(std::vector<Widget*>& out) {
    if (!visible || !enabled) return;
    if (focusable && tabStop) out.push_back(this);
    for (auto& child : children_)
        child->CollectFocusable(out);
}

// ---- VBoxWidget layout ----
// Main axis = vertical (Y), Cross axis = horizontal (X)

void VBoxWidget::DoLayout() {
    float cx = ContentLeft();
    float cy = ContentTop();
    float cw = ContentWidth();
    float totalH = ContentHeight();

    // Collect visible children (skip absolute-positioned)
    std::vector<Widget*> vis;
    std::vector<Widget*> absChildren;
    for (auto& child : children_) {
        if (!child->visible) continue;
        if (child->positionAbsolute) absChildren.push_back(child.get());
        else vis.push_back(child.get());
    }

    // Resolve percentage sizes (incl. min/max constraints expressed as %)
    for (auto* child : vis) {
        if (child->percentW    >= 0) child->fixedW = cw      * child->percentW    / 100.0f;
        if (child->percentH    >= 0) child->fixedH = totalH  * child->percentH    / 100.0f;
        if (child->percentMinW >= 0) child->minW   = cw      * child->percentMinW / 100.0f;
        if (child->percentMinH >= 0) child->minH   = totalH  * child->percentMinH / 100.0f;
        if (child->percentMaxW >= 0) child->maxW   = cw      * child->percentMaxW / 100.0f;
        if (child->percentMaxH >= 0) child->maxH   = totalH  * child->percentMaxH / 100.0f;
    }
    for (auto* child : absChildren) {
        ResolvePercentSizing(child, cw, totalH);
    }

    // First pass: measure fixed children, sum flex weights
    float usedH = 0;
    float totalFlex = 0;
    for (auto* child : vis) {
        usedH += child->marginT + child->marginB;
        if (child->expanding && child->fixedH <= 0) {
            totalFlex += child->flex;
        } else {
            float h = child->fixedH > 0 ? child->fixedH : child->SizeHint().height;
            if (h <= 0) h = 24.0f;
            h = std::clamp(h, child->minH, child->maxH);
            usedH += h;
        }
    }
    float gapTotal = (int)vis.size() > 1 ? gap_ * ((int)vis.size() - 1) : 0;
    float remaining = std::max(0.0f, totalH - usedH - gapTotal);

    // Compute each child's main-axis size (height)
    std::vector<float> heights(vis.size());
    for (size_t i = 0; i < vis.size(); i++) {
        auto* child = vis[i];
        float h;
        if (child->expanding && child->fixedH <= 0) {
            h = totalFlex > 0 ? remaining * (child->flex / totalFlex) : 0;
        } else {
            h = child->fixedH > 0 ? child->fixedH : child->SizeHint().height;
            if (h <= 0) h = 24.0f;
        }
        heights[i] = std::clamp(h, child->minH, child->maxH);
    }

    // Compute total content height (for justify)
    float contentH = 0;
    for (size_t i = 0; i < vis.size(); i++)
        contentH += heights[i] + vis[i]->marginT + vis[i]->marginB;
    contentH += gapTotal;

    // Main-axis start position + gap override for justify
    float y = cy;
    float extraGap = 0;
    float freeSpace = totalH - contentH;
    if (freeSpace > 0 && totalFlex == 0) {
        switch (mainJustify_) {
            case LayoutJustify::Start:        break;
            case LayoutJustify::Center:       y += freeSpace * 0.5f; break;
            case LayoutJustify::End:          y += freeSpace; break;
            case LayoutJustify::SpaceBetween:
                if (vis.size() > 1) extraGap = freeSpace / ((int)vis.size() - 1);
                break;
            case LayoutJustify::SpaceAround:
                extraGap = freeSpace / (float)vis.size();
                y += extraGap * 0.5f;
                break;
        }
    }

    // Second pass: position children
    for (size_t i = 0; i < vis.size(); i++) {
        auto* child = vis[i];
        float h = heights[i];

        y += child->marginT;

        // Cross-axis (width + horizontal position). align-self overrides
        // container's align-items per-child when set.
        LayoutAlign effAlign = (child->alignSelfOverride < 0)
            ? crossAlign_
            : static_cast<LayoutAlign>(child->alignSelfOverride);
        // CSS display: inline / inline-block items have intrinsic cross-size
        // and don't honor align-items: stretch (use SizeHint instead). User
        // explicit align-self: stretch opt-in still wins.
        bool intrinsic = (child->display == Display::Inline ||
                          child->display == Display::InlineBlock) &&
                         child->alignSelfOverride < 0;
        float availW = cw - child->marginL - child->marginR;
        float childW;
        if (child->fixedW > 0) {
            childW = std::clamp(child->fixedW, child->minW, child->maxW);
        } else if (effAlign == LayoutAlign::Stretch && !intrinsic) {
            childW = std::clamp(availW, child->minW, child->maxW);
        } else {
            float hintW = child->SizeHint().width;
            if (hintW <= 0) hintW = availW;
            childW = std::clamp(hintW, child->minW, child->maxW);
        }

        float childX = cx + child->marginL;
        switch (effAlign) {
            case LayoutAlign::Stretch:
            case LayoutAlign::Start:   break;
            case LayoutAlign::Center:  childX += (availW - childW) * 0.5f; break;
            case LayoutAlign::End:     childX += availW - childW; break;
        }

        child->rect = {childX, y, childX + childW, y + h};
        child->DoLayout();

        y += h + child->marginB + gap_ + extraGap;
    }

    // Layout absolute-positioned children
    for (auto* child : absChildren) {
        if (child->layoutPinned &&
            child->rect.right - child->rect.left > 0) {
            child->DoLayout();   /* build 161: 钉住 — rect 不改写 */
            continue;
        }
        auto hint = child->SizeHint();
        float w = child->fixedW > 0 ? child->fixedW : (hint.width > 0 ? hint.width : cw);
        float h = child->fixedH > 0 ? child->fixedH : (hint.height > 0 ? hint.height : 24.0f);
        float pl = ResolveAbsSide(child->posLeftRaw,   child->posLeft,   cw);
        float pr = ResolveAbsSide(child->posRightRaw,  child->posRight,  cw);
        float pt = ResolveAbsSide(child->posTopRaw,    child->posTop,    totalH);
        float pb = ResolveAbsSide(child->posBottomRaw, child->posBottom, totalH);
        float x = cx, y2 = cy;
        if (pl >= 0) x = cx + pl;
        else if (pr >= 0) x = cx + cw - w - pr;
        if (pt >= 0) y2 = cy + pt;
        else if (pb >= 0) y2 = cy + totalH - h - pb;
        child->rect = {x, y2, x + w, y2 + h};
        child->DoLayout();
    }
}

D2D1_SIZE_F VBoxWidget::SizeHint() const {
    float w = fixedW, h = 0;
    int count = 0;
    for (auto& child : children_) {
        if (!child->visible) continue;
        auto hint = child->SizeHint();
        float cw = (hint.width > 0 ? hint.width : 0) + child->marginL + child->marginR;
        if (cw > w) w = cw;
        float ch = hint.height > 0 ? hint.height : 24.0f;
        ch = std::clamp(ch, child->minH, child->maxH);
        h += ch + child->marginT + child->marginB;
        count++;
    }
    if (count > 1) h += gap_ * (count - 1);
    h += padT + padB;
    w += padL + padR;
    return {fixedW > 0 ? fixedW : w, fixedH > 0 ? fixedH : h};
}

// ---- HBoxWidget layout ----
// Main axis = horizontal (X), Cross axis = vertical (Y)

void HBoxWidget::DoLayout() {
    if (flexWrap_) { DoLayoutWrap(); return; }
    float cx = ContentLeft();
    float cy = ContentTop();
    float ch = ContentHeight();
    float totalW = ContentWidth();

    // Collect visible children (skip absolute-positioned)
    std::vector<Widget*> vis;
    std::vector<Widget*> absChildren;
    for (auto& child : children_) {
        if (!child->visible) continue;
        if (child->positionAbsolute) absChildren.push_back(child.get());
        else vis.push_back(child.get());
    }
    if (vis.empty() && absChildren.empty()) return;

    // Resolve percentage sizes (incl. min/max constraints expressed as %)
    for (auto* child : vis) {
        if (child->percentW    >= 0) child->fixedW = totalW  * child->percentW    / 100.0f;
        if (child->percentH    >= 0) child->fixedH = ch      * child->percentH    / 100.0f;
        if (child->percentMinW >= 0) child->minW   = totalW  * child->percentMinW / 100.0f;
        if (child->percentMinH >= 0) child->minH   = ch      * child->percentMinH / 100.0f;
        if (child->percentMaxW >= 0) child->maxW   = totalW  * child->percentMaxW / 100.0f;
        if (child->percentMaxH >= 0) child->maxH   = ch      * child->percentMaxH / 100.0f;
    }
    for (auto* child : absChildren) {
        ResolvePercentSizing(child, totalW, ch);
    }

    // First pass: measure fixed children, sum flex weights
    float usedW = 0;
    float totalFlex = 0;
    for (auto* child : vis) {
        usedW += child->marginL + child->marginR;
        if (child->expanding && child->fixedW <= 0) {
            totalFlex += child->flex;
        } else {
            float w = child->fixedW > 0 ? child->fixedW : child->SizeHint().width;
            if (w <= 0) w = 60.0f;
            w = std::clamp(w, child->minW, child->maxW);
            usedW += w;
        }
    }
    float gapTotal = (int)vis.size() > 1 ? gap_ * ((int)vis.size() - 1) : 0;
    float remaining = std::max(0.0f, totalW - usedW - gapTotal);

    // Compute each child's main-axis size (width)
    std::vector<float> widths(vis.size());
    for (size_t i = 0; i < vis.size(); i++) {
        auto* child = vis[i];
        float w;
        if (child->expanding && child->fixedW <= 0) {
            w = totalFlex > 0 ? remaining * (child->flex / totalFlex) : 0;
        } else {
            w = child->fixedW > 0 ? child->fixedW : child->SizeHint().width;
            if (w <= 0) w = 60.0f;
        }
        widths[i] = std::clamp(w, child->minW, child->maxW);
    }

    // Compute total content width (for justify)
    float contentW = 0;
    for (size_t i = 0; i < vis.size(); i++)
        contentW += widths[i] + vis[i]->marginL + vis[i]->marginR;
    contentW += gapTotal;

    // CSS flex-shrink: when natural content exceeds the row, shrink shrinkable
    // children (flexShrink > 0) proportionally to flexShrink × width, down to
    // minW (default 0). Lets a fixed-width row (e.g. a menu item) ellipsis its
    // label instead of overflowing the card; icons opt out via flex-shrink:0 /
    // flex:none. Iterate a few passes so children that bottom out at minW hand
    // their remaining shrink demand to the others (matches CSS distribution).
    float overflow = contentW - totalW;
    if (overflow > 0.5f) {
        for (int pass = 0; pass < 4 && overflow > 0.5f; pass++) {
            float weightSum = 0;
            for (size_t i = 0; i < vis.size(); i++)
                if (vis[i]->flexShrink > 0 && widths[i] > vis[i]->minW)
                    weightSum += vis[i]->flexShrink * widths[i];
            if (weightSum <= 0) break;
            float shrunk = 0;
            for (size_t i = 0; i < vis.size(); i++) {
                auto* child = vis[i];
                if (child->flexShrink <= 0 || widths[i] <= child->minW) continue;
                float want = overflow * (child->flexShrink * widths[i]) / weightSum;
                float neww = std::max(child->minW, widths[i] - want);
                shrunk += widths[i] - neww;
                widths[i] = neww;
            }
            overflow -= shrunk;
            if (shrunk < 0.5f) break;
        }
        // Recompute so justify below works off the shrunk total.
        contentW = 0;
        for (size_t i = 0; i < vis.size(); i++)
            contentW += widths[i] + vis[i]->marginL + vis[i]->marginR;
        contentW += gapTotal;
    }

    // Main-axis start position + gap override for justify
    float x = cx;
    float extraGap = 0;
    float freeSpace = totalW - contentW;
    if (freeSpace > 0 && totalFlex == 0) {
        switch (mainJustify_) {
            case LayoutJustify::Start:        break;
            case LayoutJustify::Center:       x += freeSpace * 0.5f; break;
            case LayoutJustify::End:          x += freeSpace; break;
            case LayoutJustify::SpaceBetween:
                if (vis.size() > 1) extraGap = freeSpace / ((int)vis.size() - 1);
                break;
            case LayoutJustify::SpaceAround:
                extraGap = freeSpace / (float)vis.size();
                x += extraGap * 0.5f;
                break;
        }
    }

    // Second pass: position children
    for (size_t i = 0; i < vis.size(); i++) {
        auto* child = vis[i];
        float w = widths[i];

        x += child->marginL;

        // Cross-axis (height + vertical position). align-self overrides
        // container's align-items per-child when set.
        LayoutAlign effAlign = (child->alignSelfOverride < 0)
            ? crossAlign_
            : static_cast<LayoutAlign>(child->alignSelfOverride);
        // CSS display: inline / inline-block items have intrinsic cross-size
        // and don't honor align-items: stretch (use SizeHint instead). User
        // explicit align-self: stretch opt-in still wins.
        bool intrinsic = (child->display == Display::Inline ||
                          child->display == Display::InlineBlock) &&
                         child->alignSelfOverride < 0;
        float availH = ch - child->marginT - child->marginB;
        float childH;
        if (child->fixedH > 0) {
            childH = std::clamp(child->fixedH, child->minH, child->maxH);
        } else if (effAlign == LayoutAlign::Stretch && !intrinsic) {
            childH = std::clamp(availH, child->minH, child->maxH);
        } else {
            float hintH = child->SizeHint().height;
            if (hintH <= 0) hintH = availH;
            childH = std::clamp(hintH, child->minH, child->maxH);
        }

        float childY = cy + child->marginT;
        switch (effAlign) {
            case LayoutAlign::Stretch:
            case LayoutAlign::Start:   break;
            case LayoutAlign::Center:  childY += (availH - childH) * 0.5f; break;
            case LayoutAlign::End:     childY += availH - childH; break;
        }

        child->rect = {x, childY, x + w, childY + childH};
        child->DoLayout();

        x += w + child->marginR + gap_ + extraGap;
    }

    // Layout absolute-positioned children
    for (auto* child : absChildren) {
        if (child->layoutPinned &&
            child->rect.right - child->rect.left > 0) {
            child->DoLayout();   /* build 161: 钉住 — rect 不改写 */
            continue;
        }
        auto hint = child->SizeHint();
        float w = child->fixedW > 0 ? child->fixedW : (hint.width > 0 ? hint.width : totalW);
        float h = child->fixedH > 0 ? child->fixedH : (hint.height > 0 ? hint.height : ch);
        float pl = ResolveAbsSide(child->posLeftRaw,   child->posLeft,   totalW);
        float pr = ResolveAbsSide(child->posRightRaw,  child->posRight,  totalW);
        float pt = ResolveAbsSide(child->posTopRaw,    child->posTop,    ch);
        float pb = ResolveAbsSide(child->posBottomRaw, child->posBottom, ch);
        float x2 = cx, y2 = cy;
        if (pl >= 0) x2 = cx + pl;
        else if (pr >= 0) x2 = cx + totalW - w - pr;
        if (pt >= 0) y2 = cy + pt;
        else if (pb >= 0) y2 = cy + ch - h - pb;
        child->rect = {x2, y2, x2 + w, y2 + h};
        child->DoLayout();
    }
}

// CSS flex-wrap: wrap path. Greedy line packing — natural width per child,
// break to a new row when adding the child would overflow content width.
// Each row's height is the max child height; rows stack vertically with
// the same gap. Inside each row we run the same justify-content / cross-align
// logic as the no-wrap path. Flex (expanding) children inherit standard CSS:
// they keep their natural width and don't grow within a wrapped row (matching
// the most common "tile grid via min-width %" pattern in our demo).
void HBoxWidget::DoLayoutWrap() {
    float cx = ContentLeft();
    float cy = ContentTop();
    float ch = ContentHeight();
    float totalW = ContentWidth();

    std::vector<Widget*> vis;
    std::vector<Widget*> absChildren;
    for (auto& child : children_) {
        if (!child->visible) continue;
        if (child->positionAbsolute) absChildren.push_back(child.get());
        else vis.push_back(child.get());
    }
    if (vis.empty() && absChildren.empty()) return;

    // Resolve percentage sizes (incl. min/max constraints expressed as %)
    for (auto* child : vis) {
        if (child->percentW    >= 0) child->fixedW = totalW * child->percentW    / 100.0f;
        if (child->percentH    >= 0) child->fixedH = ch     * child->percentH    / 100.0f;
        if (child->percentMinW >= 0) child->minW   = totalW * child->percentMinW / 100.0f;
        if (child->percentMinH >= 0) child->minH   = ch     * child->percentMinH / 100.0f;
        if (child->percentMaxW >= 0) child->maxW   = totalW * child->percentMaxW / 100.0f;
        if (child->percentMaxH >= 0) child->maxH   = ch     * child->percentMaxH / 100.0f;
    }
    for (auto* child : absChildren) {
        ResolvePercentSizing(child, totalW, ch);
    }

    // Per-child natural width (clamped to [min, max]).
    std::vector<float> widths(vis.size());
    std::vector<float> heights(vis.size());
    for (size_t i = 0; i < vis.size(); i++) {
        auto* c = vis[i];
        float w = c->fixedW > 0 ? c->fixedW : c->SizeHint().width;
        if (w <= 0) w = 60.0f;
        widths[i]  = std::clamp(w, c->minW, c->maxW);
        float h = c->fixedH > 0 ? c->fixedH : c->SizeHint().height;
        if (h <= 0) h = c->minH > 0 ? c->minH : 24.0f;
        heights[i] = std::clamp(h, c->minH, c->maxH);
    }

    // Pack into rows. row[r] = {first, last, totalW (incl gaps), maxH}
    struct Row { size_t first; size_t last; float used; float maxH; };
    std::vector<Row> rows;
    {
        Row cur{0, 0, 0, 0};
        bool started = false;
        for (size_t i = 0; i < vis.size(); i++) {
            float wi = widths[i] + vis[i]->marginL + vis[i]->marginR;
            float hi = heights[i] + vis[i]->marginT + vis[i]->marginB;
            float prospective = started ? (cur.used + gap_ + wi) : wi;
            if (started && prospective > totalW + 0.5f) {
                cur.last = i - 1;
                rows.push_back(cur);
                cur = Row{i, i, wi, hi};
            } else {
                if (!started) { cur = Row{i, i, wi, hi}; started = true; }
                else { cur.used = prospective; if (hi > cur.maxH) cur.maxH = hi; }
            }
            // Track tallest item that *was* added on this row above; the
            // initialization assigned hi only at row start.
            if (started && hi > cur.maxH) cur.maxH = hi;
        }
        if (started) {
            cur.last = vis.size() - 1;
            rows.push_back(cur);
        }
    }

    // Position each row.
    float y = cy;
    for (size_t r = 0; r < rows.size(); r++) {
        const Row& row = rows[r];
        size_t cnt = row.last - row.first + 1;

        // justify on this row's free space
        float freeSpace = totalW - row.used;
        float x = cx;
        float extraGap = 0;
        if (freeSpace > 0) {
            switch (mainJustify_) {
                case LayoutJustify::Start:        break;
                case LayoutJustify::Center:       x += freeSpace * 0.5f; break;
                case LayoutJustify::End:          x += freeSpace; break;
                case LayoutJustify::SpaceBetween:
                    if (cnt > 1) extraGap = freeSpace / (float)(cnt - 1);
                    break;
                case LayoutJustify::SpaceAround:
                    extraGap = freeSpace / (float)cnt;
                    x += extraGap * 0.5f;
                    break;
            }
        }

        for (size_t i = row.first; i <= row.last; i++) {
            auto* child = vis[i];
            float w = widths[i];
            x += child->marginL;

            // Cross-axis (height + vertical position within row). align-self
            // overrides container's align-items per-child when set.
            LayoutAlign effAlign = (child->alignSelfOverride < 0)
                ? crossAlign_
                : static_cast<LayoutAlign>(child->alignSelfOverride);
            // CSS display: inline / inline-block — see no-wrap path comment.
            bool intrinsic = (child->display == Display::Inline ||
                              child->display == Display::InlineBlock) &&
                             child->alignSelfOverride < 0;
            float availH = row.maxH - child->marginT - child->marginB;
            float childH;
            if (child->fixedH > 0) {
                childH = std::clamp(child->fixedH, child->minH, child->maxH);
            } else if (effAlign == LayoutAlign::Stretch && !intrinsic) {
                childH = std::clamp(availH, child->minH, child->maxH);
            } else {
                childH = std::clamp(heights[i], child->minH, child->maxH);
            }
            float childY = y + child->marginT;
            switch (effAlign) {
                case LayoutAlign::Stretch:
                case LayoutAlign::Start:   break;
                case LayoutAlign::Center:  childY += (availH - childH) * 0.5f; break;
                case LayoutAlign::End:     childY += availH - childH; break;
            }

            child->rect = {x, childY, x + w, childY + childH};
            child->DoLayout();

            x += w + child->marginR + gap_ + extraGap;
        }
        y += row.maxH + (r + 1 < rows.size() ? gap_ : 0);
    }

    // Absolute children (same as no-wrap path).
    for (auto* child : absChildren) {
        if (child->layoutPinned &&
            child->rect.right - child->rect.left > 0) {
            child->DoLayout();   /* build 161: 钉住 — rect 不改写 */
            continue;
        }
        auto hint = child->SizeHint();
        float w = child->fixedW > 0 ? child->fixedW : (hint.width > 0 ? hint.width : totalW);
        float h = child->fixedH > 0 ? child->fixedH : (hint.height > 0 ? hint.height : ch);
        float pl = ResolveAbsSide(child->posLeftRaw,   child->posLeft,   totalW);
        float pr = ResolveAbsSide(child->posRightRaw,  child->posRight,  totalW);
        float pt = ResolveAbsSide(child->posTopRaw,    child->posTop,    ch);
        float pb = ResolveAbsSide(child->posBottomRaw, child->posBottom, ch);
        float x2 = cx, y2 = cy;
        if (pl >= 0) x2 = cx + pl;
        else if (pr >= 0) x2 = cx + totalW - w - pr;
        if (pt >= 0) y2 = cy + pt;
        else if (pb >= 0) y2 = cy + ch - h - pb;
        child->rect = {x2, y2, x2 + w, y2 + h};
        child->DoLayout();
    }
}

D2D1_SIZE_F HBoxWidget::SizeHint() const {
    float w = 0, h = fixedH;
    int count = 0;

    // Collect per-child sizes so wrap path can pack into rows.
    struct Item { float w, h; float marginL, marginR; };
    std::vector<Item> items;
    items.reserve(children_.size());

    for (auto& child : children_) {
        if (!child->visible) continue;
        auto hint = child->SizeHint();
        float cw = hint.width > 0 ? hint.width : 60.0f;
        cw = std::clamp(cw, child->minW, child->maxW);
        w += cw + child->marginL + child->marginR;
        float ch = (hint.height > 0 ? hint.height : 0) + child->marginT + child->marginB;
        if (ch > h) h = ch;
        count++;
        items.push_back({cw, ch, child->marginL, child->marginR});
    }
    if (count > 1) w += gap_ * (count - 1);
    w += padL + padR;
    h += padT + padB;

    // When wrapping, height grows with rows. Use last-known rect width
    // (from a prior layout pass) to estimate row count; first frame falls
    // back to single-row height.
    if (flexWrap_ && !items.empty()) {
        float availW = (rect.right - rect.left) - padL - padR;
        if (fixedW > 0) availW = fixedW - padL - padR;
        if (availW > 0) {
            float used = 0;
            float rowH = 0;
            float total = 0;
            int rows = 0;
            for (size_t i = 0; i < items.size(); i++) {
                float wi = items[i].w + items[i].marginL + items[i].marginR;
                float prosp = (used == 0) ? wi : (used + gap_ + wi);
                if (used > 0 && prosp > availW + 0.5f) {
                    total += rowH;
                    if (rows > 0) total += gap_;
                    rows++;
                    used = wi;
                    rowH = items[i].h;
                } else {
                    used = prosp;
                    if (items[i].h > rowH) rowH = items[i].h;
                }
            }
            if (rowH > 0) {
                total += rowH;
                if (rows > 0) total += gap_;
                rows++;
            }
            h = total + padT + padB;
        }
    }

    return {fixedW > 0 ? fixedW : w, fixedH > 0 ? fixedH : h};
}

// ---- GridWidget layout ----

void GridWidget::DoLayout() {
    float cx = ContentLeft();
    float cy = ContentTop();
    float totalW = ContentWidth();

    // Collect visible children
    std::vector<Widget*> vis;
    for (auto& child : children_)
        if (child->visible) vis.push_back(child.get());
    if (vis.empty()) return;

    int cols = std::max(1, cols_);

    // Column widths: equal share of available width
    float colGapTotal = (cols > 1) ? colGap_ * (cols - 1) : 0;
    float colW = (totalW - colGapTotal) / cols;

    // Arrange children into grid cells, respecting colspan
    // Build row info: each row's height = max child height in that row
    struct Cell { Widget* w; int col; int colspan; };
    std::vector<std::vector<Cell>> rows;
    {
        int col = 0;
        std::vector<Cell> currentRow;
        for (auto* child : vis) {
            int span = std::clamp(child->gridColSpan, 1, cols);
            if (col + span > cols) {
                // Move to next row
                rows.push_back(std::move(currentRow));
                currentRow.clear();
                col = 0;
            }
            currentRow.push_back({child, col, span});
            col += span;
            if (col >= cols) {
                rows.push_back(std::move(currentRow));
                currentRow.clear();
                col = 0;
            }
        }
        if (!currentRow.empty()) rows.push_back(std::move(currentRow));
    }

    // Layout each row
    float y = cy;
    for (auto& row : rows) {
        // Determine row height
        float rowH = 0;
        for (auto& cell : row) {
            float h = cell.w->fixedH > 0 ? cell.w->fixedH : cell.w->SizeHint().height;
            if (h <= 0) h = 24.0f;
            h = std::clamp(h, cell.w->minH, cell.w->maxH);
            h += cell.w->marginT + cell.w->marginB;
            if (h > rowH) rowH = h;
        }

        // Position each cell
        for (auto& cell : row) {
            float cellX = cx + cell.col * (colW + colGap_) + cell.w->marginL;
            float cellW = colW * cell.colspan + colGap_ * (cell.colspan - 1)
                          - cell.w->marginL - cell.w->marginR;
            cellW = std::clamp(cellW, cell.w->minW, cell.w->maxW);

            float cellY = y + cell.w->marginT;
            float h = cell.w->fixedH > 0 ? cell.w->fixedH : rowH - cell.w->marginT - cell.w->marginB;
            h = std::clamp(h, cell.w->minH, cell.w->maxH);

            cell.w->rect = {cellX, cellY, cellX + cellW, cellY + h};
            cell.w->DoLayout();
        }

        y += rowH + rowGap_;
    }
}

D2D1_SIZE_F GridWidget::SizeHint() const {
    int cols = std::max(1, cols_);
    float maxChildW = 0, totalH = 0;
    int idx = 0, rowCount = 0;
    float rowH = 0;

    for (auto& child : children_) {
        if (!child->visible) continue;
        auto hint = child->SizeHint();
        float w = hint.width > 0 ? hint.width : 60.0f;
        if (w > maxChildW) maxChildW = w;
        float h = hint.height > 0 ? hint.height : 24.0f;
        if (h > rowH) rowH = h;

        idx++;
        if (idx >= cols) {
            totalH += rowH;
            rowCount++;
            rowH = 0;
            idx = 0;
        }
    }
    if (idx > 0) { totalH += rowH; rowCount++; }
    if (rowCount > 1) totalH += rowGap_ * (rowCount - 1);

    float w = maxChildW * cols + colGap_ * (cols - 1) + padL + padR;
    totalH += padT + padB;
    return {fixedW > 0 ? fixedW : w, fixedH > 0 ? fixedH : totalH};
}

// ---- StackWidget ----

void StackWidget::SetActiveIndex(int i) {
    if (i < 0 || i >= (int)children_.size()) return;
    activeIndex_ = i;
    // Update visibility
    for (int j = 0; j < (int)children_.size(); j++)
        children_[j]->visible = (j == activeIndex_);
    if (onActiveChanged) onActiveChanged(activeIndex_);
}

void StackWidget::DoLayout() {
    for (int i = 0; i < (int)children_.size(); i++) {
        auto& child = children_[i];
        child->visible = (i == activeIndex_);
        child->rect = {ContentLeft(), ContentTop(), ContentRight(), ContentBottom()};
        if (child->visible) child->DoLayout();
    }
}

void StackWidget::DrawTree(Renderer& r) {
    if (!visible) return;
    OnDraw(r);
    paintedOnce_ = true;   // L45: mount-phase transition gate
    DrawFocusRing(r);
    if (activeIndex_ >= 0 && activeIndex_ < (int)children_.size()) {
        children_[activeIndex_]->DrawTree(r);
    }
}

D2D1_SIZE_F StackWidget::SizeHint() const {
    // Size = max of all children
    float w = 0, h = 0;
    for (auto& child : children_) {
        auto hint = child->SizeHint();
        if (hint.width > w) w = hint.width;
        if (hint.height > h) h = hint.height;
    }
    w += padL + padR;
    h += padT + padB;
    return {fixedW > 0 ? fixedW : w, fixedH > 0 ? fixedH : h};
}

} // namespace ui
