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

struct Params {
    float blur = 0.0f;             // 高斯模糊半径
    float offsetY = 0.0f;          // 投影下移量
    float shadowAlpha = 0.0f;      // 投影不透明度 (黑)
    float hairlineAlpha = 0.0f;    // 描边不透明度
    bool  hairlineLight = false;   // true = 亮色描边 (深色主题), false = 暗色描边
};

/* 浅色: 投影为主。深色: 描边为主 + 弱投影 —— 黑投影在深色画布上几乎不可见,
 * 边界感要靠那道淡亮描边撑起来。 */
Params ParamsForTheme(bool dark);

/* 按档位缩放。Hidden 档返回全零 (调用方据此整体跳过)。 */
Params ScaleParams(const Params& p, float scale);

// ---------------------------------------------------------------------------
// 几何
// ---------------------------------------------------------------------------

/* 投影的外扩范围 = 图片矩形四周外扩 blur, 再整体下移 offsetY。 */
RectF ShadowBounds(const RectF& img, float blur, float offsetY);

/* 把 ShadowBounds 减去图片矩形, 分解成互不重叠的四条边带 (上/下/左/右),
 * 各自再与 viewport 求交; 空的丢弃。返回写入 out 的条数 (0..4)。
 * out 至少要能放 4 个 RectF。
 *
 * 对任意 img / viewport / 参数组合都成立:
 *   - 返回的边带彼此不重叠;
 *   - 返回的边带都不与 img 相交 (挖空保证);
 *   - 返回的边带都在 viewport 内。 */
int ShadowBands(const RectF& img, const RectF& viewport,
                float blur, float offsetY, RectF* out);

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
