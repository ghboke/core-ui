#pragma once

namespace ui {
namespace imgshadow {

/* 图片阴影 (gh_img_view 的"图片边缘投影 + 描边") 的纯几何与参数逻辑。
 *
 * 独立成模块的理由: 这里全是容易写错、又完全不需要 D2D 的算术 —— 边带分解、
 * 视口求交、小图降档。抽出来可以单测, 不必靠实拍去撞。
 * 刻意不引 d2d1.h, 用自带的 RectF, 测试目标零依赖。
 *
 * 核心不变式 (单测的主断言): **边带永远不与图片矩形相交**。
 * 阴影必须只画在图片外圈 —— 若整块模糊矩形铺在图片底下, 透明 PNG 在关棋盘时
 * 会透出一整块暗色矩形, 那是视觉回归。
 *
 * 语义对齐 CSS box-shadow (与 widget.cpp 的 DrawBoxShadow 同一套):
 *   几何矩形 = 图片矩形外扩 spread、整体下移 offsetY;
 *   绘制范围 = 几何矩形再外扩 blur (模糊尾巴, 必须留够, 见下)。
 */

struct RectF {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

inline bool Valid(const RectF& r) {
    return r.right > r.left && r.bottom > r.top;
}

inline float Width(const RectF& r)  { return r.right - r.left; }
inline float Height(const RectF& r) { return r.bottom - r.top; }

/* 交集。无交叠时返回的矩形 Valid() 为 false。 */
RectF Intersect(const RectF& a, const RectF& b);

/* 并集 (包围盒)。任一侧无效时返回另一侧。 */
RectF Union(const RectF& a, const RectF& b);

// ---------------------------------------------------------------------------
// 档位 —— 图片在屏上太小时, 常规阴影会盖过图片本身。
// 与 GuoheView chrome_policy.h 的 resolve_overlay_scale 同思路, 但阈值更低:
// 悬浮按钮需要 120dip 才放得下, 阴影 48dip 就还有意义。
// ---------------------------------------------------------------------------

enum class Tier {
    Full,      // 常规
    Compact,   // 参数减半
    Hidden     // 不画
};

Tier ResolveTier(float imgW, float imgH);

/* 档位对应的参数缩放系数 (Full=1, Compact=0.5, Hidden=0)。 */
float TierScale(Tier tier);

// ---------------------------------------------------------------------------
// 视觉参数 (DIP)。**不随图片缩放** —— 放大 10 倍时阴影不该跟着变成 10 倍宽,
// 它是屏幕空间的装饰, 只随 DPI 走。
// ---------------------------------------------------------------------------

/* 阴影风格档位。宿主选, 因为这纯属观感, 库无从替用户判断。
 *
 * Hairline 是 build 283 的原始规格 (约 2px 的 CSS 阴影 + 一道 1dip 实描边);
 * Soft / Window 是"窗口投影"那一路: 两层 (近层紧实 + 远层大而淡), 不画实描边,
 * 靠渐隐把边界托出来。 */
enum class Style {
    Hairline = 0,   // 2px 级 + 1dip 描边 (build 283 原始规格)
    Soft = 1,       // 柔和: 0 2px 6px + 0 8px 20px
    Window = 2      // 窗口级: 0 4px 12px + 0 14px 36px (接近系统窗口投影)
};

/* 合法化宿主传进来的整数档位 (越界回落到默认 Soft)。 */
Style StyleFromInt(int v);

/* 一层投影。CSS `box-shadow: 0 <offsetY>px <blur>px <spread>px rgba(0,0,0,alpha)`。
 * alpha <= 0 表示这一层不画。 */
struct Layer {
    float offsetY = 0.0f;
    float blur = 0.0f;
    float spread = 0.0f;
    float alpha = 0.0f;
};

constexpr int kMaxLayers = 2;

struct Params {
    Layer layers[kMaxLayers]{};
    int   layerCount = 0;
    float hairlineAlpha = 0.0f;    // 1dip 描边不透明度; 0 = 不画
    bool  hairlineLight = false;   // true = 亮色描边 (深色主题), false = 暗色描边
};

/* 按档位 + 主题取参数。
 *
 * 深色主题下黑投影几乎不可见, 所以两个软档在深色下仍保留一道很淡的亮描边 ——
 * 没有它, 深色画布上的深色图会彻底失去边界。浅色档不画描边: 那道 1dip 实线正是
 * "生硬"的来源, 软阴影自己就能托出边界。 */
Params ParamsFor(Style style, bool dark);

/* 按档位缩放 (只缩几何, 不缩不透明度)。Hidden 档返回全零。 */
Params ScaleParams(const Params& p, float scale);

/* 这套参数建议宿主给 Fit() 留多少内边距 (dip)。
 *
 * 取各层 spread + offsetY + blur*0.75 的最大值: 模糊最外那截尾巴 alpha 已不足
 * 5%, 为它多吃掉画布不划算, 但里面 3/4 必须留出来, 否则一贴边就被视口裁成硬边。
 * 宿主仍应与自己的最小内边距取 max —— 关阴影时也该留一点白。 */
float RecommendedFitPadding(const Params& p);

// ---------------------------------------------------------------------------
// 几何
// ---------------------------------------------------------------------------

/* 一层的几何矩形 = 图片矩形外扩 spread + 下移 offsetY (CSS 语义, 未含模糊)。
 * 这是要被填充然后送去高斯模糊的那个矩形。 */
RectF LayerGeomRect(const RectF& img, const Layer& l);

/* 一层的绘制范围 = 几何矩形再外扩 blur。
 *
 * 这层外扩不能省: 边带是按这个范围裁的, 裁小了模糊的外侧尾巴会被**齐刷刷切断**,
 * 阴影的外边界就成了一条硬边 —— build 283 到 294 之间"阴影生硬"的根因正是这个
 * (当时的 ShadowBounds 既当几何矩形又当绘制范围, 等于把尾巴全裁了)。 */
RectF LayerBounds(const RectF& img, const Layer& l);

/* 整套参数的绘制范围 = 各层 LayerBounds 的并集。 */
RectF ShadowBounds(const RectF& img, const Params& p);

/* 把绘制范围减去图片矩形, 分解成互不重叠的四条边带 (上/下/左/右),
 * 各自再与 viewport 求交; 空的丢弃。返回写入 out 的条数 (0..4)。
 * out 至少要能放 4 个 RectF。
 *
 * 对任意 img / viewport / bounds 组合都成立:
 *   - 返回的边带彼此不重叠;
 *   - 返回的边带都不与 img 相交 (挖空保证);
 *   - 返回的边带都在 viewport 内。 */
int ShadowBands(const RectF& img, const RectF& viewport, const RectF& bounds,
                RectF* out);

/* 描边矩形 —— 图片矩形外扩 0.5dip, 让 1dip 的笔画整条落在图片外侧,
 * 不吃掉图片最外圈那行像素 (D2D 的描边是沿路径居中的)。 */
RectF HairlineRect(const RectF& img);

/* 适应窗口时留出的内边距 —— 不留的话图片长边贴满视口, 阴影整条画在视口外看不见。
 * 放在这个模块是因为它存在的理由就是给阴影腾地方。
 *
 * 画布很小时内边距必须让路: 一个 400x40 的窄画布上留 8dip 会吃掉四成高度。
 * 按短边 25% 夹紧, 保证内缩后至少还剩一半可用空间。 */
float ClampFitPadding(float padding, float viewportW, float viewportH);

}  // namespace imgshadow
}  // namespace ui
