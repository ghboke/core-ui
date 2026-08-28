#include "ocr_img_view.h"
#include "event.h"
#include "resource_store.h"
#include "ui_context.h"
#include "theme.h"

#include <windows.h>
#include <algorithm>
#include <cmath>

namespace ui {

namespace {
constexpr float kClickSlopPx = 3.0f;   // down→up 位移小于此值算"单击"

// 旋转辅助 (与 gh_img_view 同一套约定, 便于两个 widget 行为对齐)
inline void RotateCW(int angle, float x, float y, float& rx, float& ry) {
    switch (angle) {
        case 90:  rx = -y; ry =  x; break;
        case 180: rx = -x; ry = -y; break;
        case 270: rx =  y; ry = -x; break;
        default:  rx =  x; ry =  y; break;
    }
}
inline void RotateCCW(int angle, float x, float y, float& rx, float& ry) {
    switch (angle) {
        case 90:  rx =  y; ry = -x; break;
        case 180: rx = -x; ry = -y; break;
        case 270: rx = -y; ry =  x; break;
        default:  rx =  x; ry =  y; break;
    }
}
inline int NormalizeAngle(int a) {
    const int m = ((a % 360) + 360) % 360;
    return ((m + 45) / 90 * 90) % 360;   // round 到最近的 90°
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(),
                                      nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
} // namespace

OcrImgViewWidget::OcrImgViewWidget() {
    focusable = true;                       // Ctrl+C / Ctrl+A / Esc 需要键盘焦点
    model_.onSelectionChanged = [this] { OnModelSelectionChanged(); };
}

OcrImgViewWidget::~OcrImgViewWidget() {
    if (imageResourceKey_.IsValid())
        GlobalResourceStore().Remove(imageResourceKey_);
}

// ---------------------------------------------------------------- 图像

int OcrImgViewWidget::SetImage(const void* bgra, uint32_t w, uint32_t h,
                               uint32_t stride, Renderer& r) {
    if (!bgra || w == 0 || h == 0) return kSetImageBadArgs;
    if (stride == 0) stride = w * 4;

    /* 单张纹理上限。优先问 D2D 拿真实值; 渲染线程模式下 UI 线程可能拿不到
     * RT, 退回按 D3D feature level 11 的保守常量 16384 判。宁可早退报错,
     * 也好过喂进去之后画面一片空白、宿主无从排查。 */
    uint32_t maxDim = 16384u;
    if (auto* rt = r.RT()) {
        const UINT32 m = rt->GetMaximumBitmapSize();
        if (m > 0) maxDim = m;
    }
    if (w > maxDim || h > maxDim) return kSetImageTooLarge;

    // 换图语义: 旧资源/旧文本/旧选区全部作废
    const uint64_t oldGen = imageGeneration_;
    ++imageGeneration_;
    if (imageResourceKey_.IsValid()) {
        GlobalResourceStore().PurgeGeneration(oldGen);
        imageResourceKey_ = {};
    }
    model_.Clear();

    ResourceKey key = GlobalResourceStore().AddImage(
        ResourceKind::Bitmap, imageGeneration_, (int)w, (int)h, (int)stride,
        PixelFormat::BgraPremul, bgra, true);
    if (!key.IsValid()) return kSetImageResourceErr;
    imageResourceKey_ = key;
    imgW_ = w;
    imgH_ = h;
    Fit();
    Invalidate_();
    return kSetImageOk;
}

void OcrImgViewWidget::ClearImage() {
    if (imageResourceKey_.IsValid()) {
        GlobalResourceStore().PurgeGeneration(imageGeneration_);
        imageResourceKey_ = {};
    }
    imgW_ = imgH_ = 0;
    model_.Clear();
    Invalidate_();
}

// ---------------------------------------------------------------- 文本/剪贴板

void OcrImgViewWidget::OnModelSelectionChanged() {
    selectedUtf8_.clear();                  // 缓存失效
    Invalidate_();
}

const std::string& OcrImgViewWidget::SelectedTextUtf8() const {
    if (selectedUtf8_.empty() && model_.HasSelection())
        selectedUtf8_ = model_.SelectedText();
    return selectedUtf8_;
}

bool OcrImgViewWidget::CopySelectionToClipboard() const {
    if (!model_.HasSelection()) return false;
    const std::wstring text = Utf8ToWide(model_.SelectedText());
    if (text.empty()) return false;
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (auto* p = (wchar_t*)GlobalLock(h)) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
        }
    }
    CloseClipboard();
    return true;
}

// ---------------------------------------------------------------- 视口

D2D1_RECT_F OcrImgViewWidget::ComputeDestRect() const {
    const float cw = rect.right - rect.left;
    const float ch = rect.bottom - rect.top;
    const float w = imgW_ * zoom_;
    const float h = imgH_ * zoom_;
    const float cx = rect.left + cw * 0.5f + panX_;
    const float cy = rect.top + ch * 0.5f + panY_;
    return { cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f };
}

uint32_t OcrImgViewWidget::EffectiveImageWidth() const {
    return (rotation_ == 90 || rotation_ == 270) ? imgH_ : imgW_;
}

uint32_t OcrImgViewWidget::EffectiveImageHeight() const {
    return (rotation_ == 90 || rotation_ == 270) ? imgW_ : imgH_;
}

D2D1_RECT_F OcrImgViewWidget::ComputeVisualDestRect() const {
    const float w = EffectiveImageWidth() * zoom_;
    const float h = EffectiveImageHeight() * zoom_;
    const float cx = (rect.left + rect.right) * 0.5f + panX_;
    const float cy = (rect.top + rect.bottom) * 0.5f + panY_;
    return { cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f };
}

void OcrImgViewWidget::ScreenToImage(float sx, float sy,
                                     float& ix, float& iy) const {
    // 反向: screen → 减 widget center 与 pan → CCW 反旋转 → 除 zoom → 加图像中心
    const float cx = (rect.left + rect.right) * 0.5f;
    const float cy = (rect.top + rect.bottom) * 0.5f;
    float rx, ry;
    RotateCCW(rotation_, sx - cx - panX_, sy - cy - panY_, rx, ry);
    const float z = (zoom_ > 1e-6f) ? zoom_ : 1e-6f;
    ix = rx / z + imgW_ * 0.5f;
    iy = ry / z + imgH_ * 0.5f;
}

void OcrImgViewWidget::ImageToScreen(float ix, float iy,
                                     float& sx, float& sy) const {
    // 正向: image → 减图像中心 → 乘 zoom → CW 旋转 → 加 widget center 与 pan
    float rx, ry;
    RotateCW(rotation_, (ix - imgW_ * 0.5f) * zoom_,
             (iy - imgH_ * 0.5f) * zoom_, rx, ry);
    const float cx = (rect.left + rect.right) * 0.5f;
    const float cy = (rect.top + rect.bottom) * 0.5f;
    sx = cx + panX_ + rx;
    sy = cy + panY_ + ry;
}

void OcrImgViewWidget::SetRotation(int angle) {
    const int n = NormalizeAngle(angle);
    if (n == rotation_) return;
    rotation_ = n;
    // zoom / pan 都保留 —— 用户预期"转一下不丢视图状态"。pan 存屏幕空间,
    // 与 rotation 解耦, 不需要跟着旋转 (见头文件说明)。
    Invalidate_();
}

void OcrImgViewWidget::SetZoom(float z) {
    z = std::clamp(z, minZoom_, maxZoom_);
    if (z == zoom_) return;
    zoom_ = z;
    Invalidate_();
}

void OcrImgViewWidget::SetZoomAround(float z, float anchorX, float anchorY) {
    z = std::clamp(z, minZoom_, maxZoom_);
    if (z == zoom_ || zoom_ <= 0) return;
    // 锚点在图像上的位置保持不动: pan 补偿缩放前后锚点的屏幕位移
    float ix, iy;
    ScreenToImage(anchorX, anchorY, ix, iy);
    zoom_ = z;
    float nsx, nsy;
    ImageToScreen(ix, iy, nsx, nsy);
    panX_ += anchorX - nsx;
    panY_ += anchorY - nsy;
    Invalidate_();
}

void OcrImgViewWidget::SetPan(float x, float y) {
    if (x == panX_ && y == panY_) return;
    panX_ = x;
    panY_ = y;
    Invalidate_();
}

void OcrImgViewWidget::Fit() {
    if (!HasImage()) return;
    const float cw = rect.right - rect.left;
    const float ch = rect.bottom - rect.top;
    if (cw <= 0 || ch <= 0) { zoom_ = 1.0f; panX_ = panY_ = 0; return; }
    // 用旋转后的有效宽高 —— 竖图转 90° 后该按它的"视觉宽"贴合视口
    const float ew = (float)EffectiveImageWidth();
    const float eh = (float)EffectiveImageHeight();
    if (ew <= 0 || eh <= 0) return;
    zoom_ = std::clamp(std::min(cw / ew, ch / eh), minZoom_, maxZoom_);
    panX_ = panY_ = 0;
    Invalidate_();
}

void OcrImgViewWidget::Reset() {
    zoom_ = 1.0f;
    panX_ = panY_ = 0;
    Invalidate_();
}

// ---------------------------------------------------------------- 绘制

void OcrImgViewWidget::OnDraw(Renderer& r) {
    if (!HasImage() || !imageResourceKey_.IsValid()) return;

    const D2D1_RECT_F dest = ComputeDestRect();
    r.PushClip(rect);

    // 图像: 放大用 HQ cubic, 缩小用 linear (跟随 image viewer 惯例)
    const ImageSampling sampling = zoom_ >= 1.0f
        ? ImageSampling::HighQualityCubicClamp
        : ImageSampling::LinearClamp;
    /* 位图在【逻辑】dest 内按未旋转姿态绘制, 外面叠一层绕 dest 中心的 D2D
     * 旋转 transform 完成视觉旋转 —— 与 gh_img_view 同一手法。
     * 选区高亮不在这个 transform 里: 它的顶点走 ImageToScreen 已经是旋转
     * 后的屏幕坐标, 再套一层会转两次。 */
    const bool rotated = (rotation_ != 0);
    if (rotated) {
        const float dcx = (dest.left + dest.right) * 0.5f;
        const float dcy = (dest.top + dest.bottom) * 0.5f;
        r.PushTransform(D2D1::Matrix3x2F::Rotation((float)rotation_,
                                                   D2D1::Point2F(dcx, dcy)));
    }
    r.DrawImageResource(imageResourceKey_, dest, sampling, 1.0f);
    if (rotated) r.PopTransform();

    // 选区高亮: 图像空间 quad 的 4 顶点各自换算到屏幕空间, 半透明填充。
    //
    // 用 Renderer::FillQuad (真几何填充) 而不是 PushTransform + FillRect —
    // 后者只能表达【平行四边形】(仿射变换保持边的平行性), 对透视 quad
    // (翻拍书页 / 侧拍招牌那种近大远小的一行字) 会漏掉整个"变宽"的那头。
    // 实测强透视下偏差达 quad 对角线的 19%, 末尾几个字完全露在高亮外,
    // 是明显视觉缺陷而非可忽略误差 (demo scene 3 + quad_err 命令可复现)。
    if (model_.HasSelection()) {
        D2D1_COLOR_F hl = highlight_;
        if (!hasCustomHighlight_) {
            hl = theme::Current().accent;
            hl.a = 0.35f;
        }
        /* 字级选择下, 一行中文就是几十个相邻的字 quad。逐个填充有两个问题:
         * 慢, 且相邻半透明块在交界处叠加透出接缝。所以把连续、同 block+line、
         * 且朝向一致的字合并成一个 quad 再填 —— 合并后仍是任意四边形,
         * 透视块里合并出来的子区间照样是梯形, FillQuad 照画不误。 */
        for (const auto& iv : model_.Selection()) {
            const uint32_t last = std::min(iv.last,
                                           (uint32_t)model_.ItemCount() - 1);
            uint32_t runStart = iv.first;
            for (uint32_t i = iv.first; i <= last; ++i) {
                const bool isLast = (i == last);
                const bool breakAfter = isLast || !CanMergeQuads_(i, i + 1);
                if (!breakAfter) continue;
                FillMergedRun_(r, runStart, i, hl);
                runStart = i + 1;
            }
        }
    }

    r.PopClip();
    if (IsFocused()) DrawFocusRing(r);
}

// 相邻两个 item 能否合并成一个高亮 quad。要求同 block 同 line, 且 a 的右边
// 与 b 的左边基本重合 —— 后者才是关键: 排版换列、跨图元或曲面文字上, 两个
// 字虽同行却不首尾相接, 强行合并会把中间不属于选区的区域也涂上。
bool OcrImgViewWidget::CanMergeQuads_(uint32_t a, uint32_t b) const {
    if (b >= model_.ItemCount()) return false;
    const OcrTextItem& ia = model_.Item(a);
    const OcrTextItem& ib = model_.Item(b);
    if (ia.block != ib.block || ia.line != ib.line) return false;
    /* a 的 TR/BR 与 b 的 TL/BL 距离阈值: 取字高的 1/4, 随缩放自适应,
     * 不写死像素 (源坐标是图像空间, 与 zoom 无关)。
     * 字高必须取 TL→BL 的【边长】而非 y 分量 —— 旋转文字下 y 分量随角度
     * 趋近 0 (90° 时恰为 0), 阈值会紧到永不合并, 相邻字高亮就叠出接缝。 */
    const float ex = ia.quad[6] - ia.quad[0];
    const float ey = ia.quad[7] - ia.quad[1];
    const float ah = std::sqrt(ex * ex + ey * ey);
    const float tol = (ah > 0 ? ah : 8.0f) * 0.25f;
    const float dTop = std::fabs(ia.quad[2] - ib.quad[0]) +
                       std::fabs(ia.quad[3] - ib.quad[1]);
    const float dBot = std::fabs(ia.quad[4] - ib.quad[6]) +
                       std::fabs(ia.quad[5] - ib.quad[7]);
    return dTop <= tol && dBot <= tol;
}

// 把 [first,last] 这段合并成一个 quad 填充: 取首项的左边 + 末项的右边。
void OcrImgViewWidget::FillMergedRun_(Renderer& r, uint32_t first, uint32_t last,
                                      const D2D1_COLOR_F& color) {
    if (first >= model_.ItemCount() || last >= model_.ItemCount()) return;
    const float* qf = model_.Item(first).quad;
    const float* ql = model_.Item(last).quad;
    const float merged[8] = {
        qf[0], qf[1],   // TL = 首项 TL
        ql[2], ql[3],   // TR = 末项 TR
        ql[4], ql[5],   // BR = 末项 BR
        qf[6], qf[7],   // BL = 首项 BL
    };
    D2D1_POINT_2F pts[4];
    for (int k = 0; k < 4; ++k)
        ImageToScreen(merged[k * 2], merged[k * 2 + 1], pts[k].x, pts[k].y);
    r.FillQuad(pts, color);
}

// ---------------------------------------------------------------- 手势

bool OcrImgViewWidget::OnMouseDown(const MouseEvent& e) {
    if (!HasImage()) return false;
    downX_ = e.x;
    downY_ = e.y;
    moved_ = false;

    float ix, iy;
    ScreenToImage(e.x, e.y, ix, iy);
    const int hit = model_.HitTest(ix, iy);

    /* 三击 = 双击之后紧跟的这一下 down (Win32 只发到 WM_LBUTTONDBLCLK 为止)。
     * 必须在下面清选区之前判, 否则刚被双击选中的块会先被清掉。 */
    if (hit >= 0 && lastDblTick_ != 0 &&
        GetTickCount() - lastDblTick_ <= GetDoubleClickTime() &&
        std::fabs(e.x - lastDblX_) <= kClickSlopPx &&
        std::fabs(e.y - lastDblY_) <= kClickSlopPx) {
        lastDblTick_ = 0;                       // 四击不再继续升级
        model_.SelectLine((uint32_t)hit);
        return true;
    }

    if (hit >= 0) {
        /* PDF 惯例: 按下先清旧选区, 但【不】立刻选中当前块 —— 位移越过
         * 阈值才起选区 (OnMouseMove 里)。所以单击文字 = 清选区, 跟点空白
         * 一致; 想只取一个块用双击。 */
        model_.ClearSelection();
        pendingSelect_ = true;
        pendingAnchor_ = (uint32_t)hit;
    } else {
        panning_ = true;
        dragPanX_ = panX_;
        dragPanY_ = panY_;
    }
    return true;
}

bool OcrImgViewWidget::OnMouseMove(const MouseEvent& e) {
    /* 按在文字上但还没越过阈值: 位移够了才真正起选区 (PDF 惯例, 避免
     * 手抖的单击也刷出高亮)。 */
    if (pendingSelect_) {
        if (std::fabs(e.x - downX_) <= kClickSlopPx &&
            std::fabs(e.y - downY_) <= kClickSlopPx)
            return true;
        pendingSelect_ = false;
        selectingDrag_ = true;
        model_.BeginSelect(pendingAnchor_);
    }
    if (selectingDrag_) {
        moved_ = moved_ || std::fabs(e.x - downX_) > kClickSlopPx ||
                 std::fabs(e.y - downY_) > kClickSlopPx;
        float ix, iy;
        ScreenToImage(e.x, e.y, ix, iy);
        int cur = model_.HitTest(ix, iy);
        if (cur < 0) cur = model_.NearestItem(ix, iy);  // 越过空白吸附最近词
        if (cur >= 0) model_.UpdateSelect((uint32_t)cur);
        return true;
    }
    if (panning_) {
        moved_ = moved_ || std::fabs(e.x - downX_) > kClickSlopPx ||
                 std::fabs(e.y - downY_) > kClickSlopPx;
        SetPan(dragPanX_ + (e.x - downX_), dragPanY_ + (e.y - downY_));
        return true;
    }
    UpdateHoverCursor_(e.x, e.y);
    return false;
}

bool OcrImgViewWidget::OnMouseUp(const MouseEvent& e) {
    if (pendingSelect_) {
        /* 按下抬起都没动 = 单击文字。选区已在 down 时清掉, 这里不再起选区
         * (PDF 惯例)。想选单个块用双击。 */
        pendingSelect_ = false;
        return true;
    }
    if (selectingDrag_) {
        selectingDrag_ = false;
        model_.EndSelect();
        return true;
    }
    if (panning_) {
        panning_ = false;
        if (!moved_) model_.ClearSelection();  // 空白单击 = 清选区
        (void)e;
        return true;
    }
    return false;
}

bool OcrImgViewWidget::OnMouseDoubleClick(const MouseEvent& e) {
    if (!HasImage()) return false;
    float ix, iy;
    ScreenToImage(e.x, e.y, ix, iy);
    const int hit = model_.HitTest(ix, iy);
    if (hit < 0) return false;
    selectingDrag_ = false;      // 双击前的 down 起的手势作废
    pendingSelect_ = false;
    model_.SelectWord((uint32_t)hit);   // 字级数据下双击选整词, 而非单个字
    /* 记下双击时刻/位置, 供 OnMouseDown 判定紧随其后的第三下 = 三击选整行 */
    lastDblTick_ = GetTickCount();
    lastDblX_ = e.x;
    lastDblY_ = e.y;
    return true;
}

bool OcrImgViewWidget::OnMouseWheel(const MouseEvent& e) {
    if (!HasImage()) return false;
    const float factor = e.delta > 0 ? 1.25f : 0.8f;
    SetZoomAround(zoom_ * factor, e.x, e.y);
    return true;
}

bool OcrImgViewWidget::OnKeyDown(int vk) {
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (ctrl && vk == 'C') return CopySelectionToClipboard();
    if (ctrl && vk == 'A') { model_.SelectAll(); return true; }
    if (vk == VK_ESCAPE && model_.HasSelection()) {
        model_.ClearSelection();
        return true;
    }
    return false;
}

D2D1_SIZE_F OcrImgViewWidget::SizeHint() const {
    return { (float)(imgW_ ? imgW_ : 240), (float)(imgH_ ? imgH_ : 160) };
}

// ---------------------------------------------------------------- 内部

void OcrImgViewWidget::UpdateHoverCursor_(float sx, float sy) {
    float ix, iy;
    ScreenToImage(sx, sy, ix, iy);
    const CursorKind want =
        model_.HitTest(ix, iy) >= 0 ? CursorKind::Text : CursorKind::Default;
    if (cursor != want) cursor = want;   // 窗口在 WM_SETCURSOR 时读该字段
}

void OcrImgViewWidget::Invalidate_() {
    GetContext().InvalidateAllWindows();
}

} // namespace ui
