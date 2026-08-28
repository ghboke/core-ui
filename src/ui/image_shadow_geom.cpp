#include "image_shadow_geom.h"

namespace ui {
namespace imgshadow {

namespace {

float MaxOf(float a, float b) { return a > b ? a : b; }
float MinOf(float a, float b) { return a < b ? a : b; }

/* 视觉规格 (2026-08-07 用户定): **CSS box-shadow 那种量级, 宽度约 2px**。
 *   浅色: blur 2 / y+1 / 黑 22%; 描边 黑 10%
 *   深色: blur 2 / y+1 / 黑 24%; 描边 白 14%
 * 大致等价于 `box-shadow: 0 1px 2px rgba(0,0,0,.22)` + 1px 描边。
 *
 * 深色画布上黑投影几乎不可见, 边界感靠那道淡亮描边撑。浅色也留了一道极淡暗
 * 描边 —— 纯投影对"白底画布上的白图"基本无效, 而那恰是最需要看清边界的场景。
 * 收到 2px 这一档后, 描边其实成了主角, 投影只负责托一下。
 *
 * 调参教训 (前后错了三轮, 都是我自己臆测"好看"的量级): 初版 blur 14 / y+3 /
 * 黑 20% -> "又大又宽"; 收到 blur 6 -> 仍不对; 放宽到 blur 10 但调淡 -> "一样个蛋"。
 * 真实需求一直是 **2px 级的 CSS 阴影**, 比我猜的小一个数量级。教训: 这类纯观感
 * 参数不要靠自己审美迭代, 要么拿到明确数值, 要么一次给足档位让用户挑。
 * 另注意 blur 是 dip, 1.5x DPI 上实际约 3px。 */
constexpr float kLightBlur = 2.0f;
constexpr float kLightOffsetY = 1.0f;
constexpr float kLightShadowAlpha = 0.22f;
constexpr float kLightHairlineAlpha = 0.10f;

constexpr float kDarkBlur = 2.0f;
constexpr float kDarkOffsetY = 1.0f;
constexpr float kDarkShadowAlpha = 0.24f;
constexpr float kDarkHairlineAlpha = 0.14f;

/* 档位阈值 (dip, 图片在屏上的短边)。悬浮按钮那套要 120dip 才放得下, 阴影
 * 48dip 就还有意义, 所以阈值比 chrome_policy.h 的低。 */
constexpr float kFullMinShortEdge = 48.0f;
constexpr float kCompactMinShortEdge = 20.0f;

}  // namespace

RectF Intersect(const RectF& a, const RectF& b) {
    RectF out;
    out.left = MaxOf(a.left, b.left);
    out.top = MaxOf(a.top, b.top);
    out.right = MinOf(a.right, b.right);
    out.bottom = MinOf(a.bottom, b.bottom);
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

Params ParamsForTheme(bool dark) {
    Params p;
    if (dark) {
        p.blur = kDarkBlur;
        p.offsetY = kDarkOffsetY;
        p.shadowAlpha = kDarkShadowAlpha;
        p.hairlineAlpha = kDarkHairlineAlpha;
        p.hairlineLight = true;
    } else {
        p.blur = kLightBlur;
        p.offsetY = kLightOffsetY;
        p.shadowAlpha = kLightShadowAlpha;
        p.hairlineAlpha = kLightHairlineAlpha;
        p.hairlineLight = false;
    }
    return p;
}

Params ScaleParams(const Params& p, float scale) {
    Params out = p;
    if (scale <= 0.0f) {
        out.blur = 0.0f;
        out.offsetY = 0.0f;
        out.shadowAlpha = 0.0f;
        out.hairlineAlpha = 0.0f;
        return out;
    }
    /* 只缩几何, 不缩不透明度 —— 小图上的阴影该变窄, 不该变淡到看不见。 */
    out.blur = p.blur * scale;
    out.offsetY = p.offsetY * scale;
    return out;
}

RectF ShadowBounds(const RectF& img, float blur, float offsetY) {
    RectF out;
    out.left = img.left - blur;
    out.top = img.top - blur + offsetY;
    out.right = img.right + blur;
    out.bottom = img.bottom + blur + offsetY;
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

int ShadowBands(const RectF& img, const RectF& viewport,
                float blur, float offsetY, RectF* out) {
    if (!out) return 0;
    if (!Valid(viewport)) return 0;

    const RectF s = ShadowBounds(img, blur, offsetY);
    if (!Valid(s)) return 0;

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
