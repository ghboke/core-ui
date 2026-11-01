#include "image_shadow_geom.h"

namespace ui {
namespace imgshadow {

namespace {

float MaxOf(float a, float b) { return a > b ? a : b; }
float MinOf(float a, float b) { return a < b ? a : b; }
float AbsOf(float a) { return a < 0.0f ? -a : a; }

/* 视觉规格。三档由宿主选 —— 这纯属观感, 库替用户拍板必错 (build 283 那轮
 * blur 14 -> 6 -> 10 连猜三轮全错, 教训见 CHANGELOG)。
 *
 * Hairline: build 283 的原始规格, 约 2px 的 CSS 阴影 + 一道 1dip 实描边。
 *   浅色 blur 2 / y+1 / spread 2 / 黑 22% + 描边 黑 10%
 *   深色 同几何 / 黑 24% + 描边 白 14%
 *   spread 2 是把当时"外扩一个 blur"的行为按 CSS 语义写明, 观感等价。
 *
 * Soft / Window: 窗口投影那一路 —— 两层, 近层紧实定形、远层大而淡铺开,
 *   不画实描边 (那道硬线正是"生硬"的来源), 靠渐隐托出边界。
 *   深色下黑投影几乎不可见, 所以仍留一道很淡的亮描边保底, 否则深色画布上的
 *   深色图会彻底失去边界。 */

// Hairline (build 283 原始规格)
constexpr float kHairBlur = 2.0f;
constexpr float kHairOffsetY = 1.0f;
constexpr float kHairSpread = 2.0f;
constexpr float kHairAlphaLight = 0.22f;
constexpr float kHairAlphaDark = 0.24f;
constexpr float kHairStrokeLight = 0.10f;
constexpr float kHairStrokeDark = 0.14f;

// Soft: 0 2px 6px rgba(0,0,0,.16) + 0 8px 20px rgba(0,0,0,.14)
constexpr Layer kSoftNear{2.0f, 6.0f, 0.0f, 0.16f};
constexpr Layer kSoftFar{8.0f, 20.0f, 0.0f, 0.14f};

// Window: 0 4px 12px rgba(0,0,0,.18) + 0 14px 36px rgba(0,0,0,.18)
constexpr Layer kWindowNear{4.0f, 12.0f, 0.0f, 0.18f};
constexpr Layer kWindowFar{14.0f, 36.0f, 0.0f, 0.18f};

/* 深色主题下投影提一点 (画布本身暗, 同样 alpha 的黑几乎看不出来), 并补一道
 * 淡亮描边。数值刻意保守: 描边一重就又回到"生硬"。 */
constexpr float kSoftDarkAlphaBoost = 0.06f;
constexpr float kSoftStrokeDark = 0.08f;
constexpr float kWindowStrokeDark = 0.10f;

/* 档位阈值 (dip, 图片在屏上的短边)。悬浮按钮那套要 120dip 才放得下, 阴影
 * 48dip 就还有意义, 所以阈值比 chrome_policy.h 的低。 */
constexpr float kFullMinShortEdge = 48.0f;
constexpr float kCompactMinShortEdge = 20.0f;

/* 建议内边距只留 3/4 个 blur: 最外那截尾巴 alpha 不足 5%, 为它多吃画布不划算。 */
constexpr float kFitPaddingBlurShare = 0.75f;

}  // namespace

RectF Intersect(const RectF& a, const RectF& b) {
    RectF out;
    out.left = MaxOf(a.left, b.left);
    out.top = MaxOf(a.top, b.top);
    out.right = MinOf(a.right, b.right);
    out.bottom = MinOf(a.bottom, b.bottom);
    return out;
}

RectF Union(const RectF& a, const RectF& b) {
    if (!Valid(a)) return b;
    if (!Valid(b)) return a;
    RectF out;
    out.left = MinOf(a.left, b.left);
    out.top = MinOf(a.top, b.top);
    out.right = MaxOf(a.right, b.right);
    out.bottom = MaxOf(a.bottom, b.bottom);
    return out;
}

Tier ResolveTier(float imgW, float imgH) {
    if (imgW <= 0.0f || imgH <= 0.0f) return Tier::Hidden;
    const float shortEdge = MinOf(imgW, imgH);
    if (shortEdge >= kFullMinShortEdge) return Tier::Full;
    if (shortEdge >= kCompactMinShortEdge) return Tier::Compact;
    return Tier::Hidden;
}

float TierScale(Tier tier) {
    switch (tier) {
    case Tier::Full:    return 1.0f;
    case Tier::Compact: return 0.5f;
    case Tier::Hidden:  break;
    }
    return 0.0f;
}

Style StyleFromInt(int v) {
    switch (v) {
    case 0: return Style::Hairline;
    case 2: return Style::Window;
    case 1: break;
    default: break;   // 越界: 回落到默认档, 不当作错误
    }
    return Style::Soft;
}

Params ParamsFor(Style style, bool dark) {
    Params p;
    switch (style) {
    case Style::Hairline: {
        Layer l;
        l.offsetY = kHairOffsetY;
        l.blur = kHairBlur;
        l.spread = kHairSpread;
        l.alpha = dark ? kHairAlphaDark : kHairAlphaLight;
        p.layers[0] = l;
        p.layerCount = 1;
        p.hairlineAlpha = dark ? kHairStrokeDark : kHairStrokeLight;
        p.hairlineLight = dark;
        return p;
    }
    case Style::Window:
        p.layers[0] = kWindowNear;
        p.layers[1] = kWindowFar;
        p.layerCount = 2;
        p.hairlineAlpha = dark ? kWindowStrokeDark : 0.0f;
        p.hairlineLight = dark;
        break;
    case Style::Soft:
    default:
        p.layers[0] = kSoftNear;
        p.layers[1] = kSoftFar;
        p.layerCount = 2;
        p.hairlineAlpha = dark ? kSoftStrokeDark : 0.0f;
        p.hairlineLight = dark;
        break;
    }
    if (dark) {
        for (int i = 0; i < p.layerCount; ++i) {
            p.layers[i].alpha += kSoftDarkAlphaBoost;
        }
    }
    return p;
}

Params ScaleParams(const Params& p, float scale) {
    Params out = p;
    if (scale <= 0.0f) {
        for (int i = 0; i < kMaxLayers; ++i) out.layers[i] = Layer{};
        out.layerCount = 0;
        out.hairlineAlpha = 0.0f;
        return out;
    }
    /* 只缩几何, 不缩不透明度 —— 小图上的阴影该变窄, 不该变淡到看不见。 */
    for (int i = 0; i < out.layerCount; ++i) {
        out.layers[i].offsetY *= scale;
        out.layers[i].blur *= scale;
        out.layers[i].spread *= scale;
    }
    return out;
}

float RecommendedFitPadding(const Params& p) {
    float need = 0.0f;
    for (int i = 0; i < p.layerCount; ++i) {
        const Layer& l = p.layers[i];
        if (l.alpha <= 0.0f) continue;
        const float extent = MaxOf(0.0f, l.spread) + AbsOf(l.offsetY) +
                             MaxOf(0.0f, l.blur) * kFitPaddingBlurShare;
        need = MaxOf(need, extent);
    }
    return need;
}

RectF LayerGeomRect(const RectF& img, const Layer& l) {
    RectF out;
    out.left = img.left - l.spread;
    out.top = img.top - l.spread + l.offsetY;
    out.right = img.right + l.spread;
    out.bottom = img.bottom + l.spread + l.offsetY;
    return out;
}

RectF LayerBounds(const RectF& img, const Layer& l) {
    const RectF g = LayerGeomRect(img, l);
    const float blur = MaxOf(0.0f, l.blur);
    RectF out;
    out.left = g.left - blur;
    out.top = g.top - blur;
    out.right = g.right + blur;
    out.bottom = g.bottom + blur;
    return out;
}

RectF ShadowBounds(const RectF& img, const Params& p) {
    RectF out;
    for (int i = 0; i < p.layerCount; ++i) {
        if (p.layers[i].alpha <= 0.0f) continue;
        out = Union(out, LayerBounds(img, p.layers[i]));
    }
    return out;
}

float ClampFitPadding(float padding, float viewportW, float viewportH) {
    if (padding <= 0.0f) return 0.0f;
    if (viewportW <= 0.0f || viewportH <= 0.0f) return 0.0f;
    const float shortEdge = MinOf(viewportW, viewportH);
    const float cap = shortEdge * 0.25f;   // 内缩 2*cap 后至少还剩一半
    return MinOf(padding, cap);
}

RectF HairlineRect(const RectF& img) {
    RectF out;
    out.left = img.left - 0.5f;
    out.top = img.top - 0.5f;
    out.right = img.right + 0.5f;
    out.bottom = img.bottom + 0.5f;
    return out;
}

int ShadowBands(const RectF& img, const RectF& viewport, const RectF& bounds,
                RectF* out) {
    if (!out) return 0;
    if (!Valid(viewport)) return 0;
    if (!Valid(bounds)) return 0;

    const RectF& s = bounds;

    /* s 减去 img, 分解成互不重叠的四条。上下两条横跨整个 s 宽度, 左右两条只占
     * 图片的纵向区间 —— 这样四条天然不重叠。
     *
     * 每条边界都要夹进 s: offsetY 大于 blur 时阴影整体落到图片下方, 上边带会
     * 退化成倒置矩形, 靠 Valid() 丢掉即可, 但纵向范围必须先夹好, 否则左右两条
     * 会伸到 s 之外。 */
    const float midTop = MaxOf(s.top, img.top);
    const float midBottom = MinOf(s.bottom, img.bottom);

    RectF cand[4];
    // 上
    cand[0] = RectF{s.left, s.top, s.right, MinOf(img.top, s.bottom)};
    // 下
    cand[1] = RectF{s.left, MaxOf(img.bottom, s.top), s.right, s.bottom};
    // 左
    cand[2] = RectF{s.left, midTop, MinOf(img.left, s.right), midBottom};
    // 右
    cand[3] = RectF{MaxOf(img.right, s.left), midTop, s.right, midBottom};

    int n = 0;
    for (int i = 0; i < 4; ++i) {
        if (!Valid(cand[i])) continue;
        const RectF clipped = Intersect(cand[i], viewport);
        if (!Valid(clipped)) continue;
        out[n++] = clipped;
    }
    return n;
}

}  // namespace imgshadow
}  // namespace ui
