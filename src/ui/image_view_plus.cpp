#include "image_view_plus.h"
#include "event.h"
#include "resource_store.h"
#include "theme.h"
#include <algorithm>
#include <unordered_map>
#include <cstring>
#include <cmath>

namespace ui {

// ========= 全局 timer 映射（Win32 SetTimer 要求静态回调，通过 id → this）=========
static std::unordered_map<UINT_PTR, ImageViewPlusWidget*> g_loadingMap;
static std::unordered_map<UINT_PTR, ImageViewPlusWidget*> g_animMap;

// forward decl —— 声明在 controls.cpp 里（已有 DrawSpinner / ConstrainPan 通用实现可复用，
// 这里为了零依赖 controls.cpp，内部重实现 spinner）
static void DrawSpinner(Renderer& r, float cx, float cy, float radius,
                        float angleDeg, const D2D1_COLOR_F& color, float stroke);

// ========= ctor / dtor =========

ImageViewPlusWidget::ImageViewPlusWidget() {
    expanding = true;
}

ImageViewPlusWidget::~ImageViewPlusWidget() {
    StopAnimation();
    if (checkerTileResourceKey_.IsValid()) {
        GlobalResourceStore().Remove(checkerTileResourceKey_);
    }
    if (loadingTimerId_) {
        KillTimer(nullptr, loadingTimerId_);
        g_loadingMap.erase(loadingTimerId_);
        loadingTimerId_ = 0;
    }
}

// ========= 加载 =========

bool ImageViewPlusWidget::LoadFromFile(const std::wstring& path, Renderer& r) {
    auto src = IImageSource::CreateFromFile(path, r);
    if (!src) {
        if (onLoadFailed) onLoadFailed(path);
        return false;
    }
    SetSource(std::move(src));
    if (onLoaded) onLoaded();
    return true;
}

/*
 * 异步加载说明：
 * 本 widget 不自带后台线程调度，避免把线程语义绑死。正确用法：
 *   widget->SetLoading(true);
 *   std::thread([widget, path, renderer, uiWindow]{
 *       auto src = ui::IImageSource::CreateFromFile(path, *renderer);
 *       ui_window_invoke(uiWindow, [widget, s = std::move(src)]() mutable {
 *           if (s) widget->SetSource(std::move(s));
 *           else   widget->SetLoading(false);  // 加载失败
 *       });
 *   }).detach();
 *
 * UiWindow 的 InvokeSync/InvokeAsync 机制（WM_APP+120）会把 lambda 跑在 UI 线程。
 */

void ImageViewPlusWidget::SetSource(std::unique_ptr<IImageSource> src) {
    StopAnimation();
    source_ = std::move(src);
    SetLoading(false);
    zoom_ = 1.0f;
    panX_ = panY_ = 0;
    if (source_ && source_->Caps().animated) {
        StartAnimation();
    }
    InvalidateAllWindows();
}

void ImageViewPlusWidget::SetPixels(const void* pixels, int w, int h, int stride, Renderer& r) {
    SetSource(IImageSource::CreateFromPixels(pixels, w, h, stride, r));
}

IImageSource::ITiledSource*
ImageViewPlusWidget::BeginTiled(int fullW, int fullH, int tileSize, Renderer& r) {
    auto src = IImageSource::CreateTiled(fullW, fullH, tileSize, r);
    auto* tiled = dynamic_cast<IImageSource::ITiledSource*>(src.get());
    SetSource(std::move(src));
    return tiled;
}

void ImageViewPlusWidget::Clear() {
    StopAnimation();
    source_.reset();
    zoom_ = 1.0f;
    panX_ = panY_ = 0;
    InvalidateAllWindows();
}

// ========= 视口信息 =========

int ImageViewPlusWidget::ImageWidth() const {
    if (!source_) return 0;
    return (rotation_ == 90 || rotation_ == 270) ? source_->Height() : source_->Width();
}
int ImageViewPlusWidget::ImageHeight() const {
    if (!source_) return 0;
    return (rotation_ == 90 || rotation_ == 270) ? source_->Width() : source_->Height();
}

void ImageViewPlusWidget::SetZoom(float z) {
    zoom_ = std::clamp(z, minZoom_, maxZoom_);
    NotifyViewport();
}

void ImageViewPlusWidget::FitToView() {
    int iw = ImageWidth(), ih = ImageHeight();
    if (iw <= 0 || ih <= 0) return;
    float areaW = rect.right - rect.left;
    float areaH = rect.bottom - rect.top;
    if (areaW <= 0 || areaH <= 0) return;
    float z = std::min(areaW / (float)iw, areaH / (float)ih);
    if (z > 1.0f) z = 1.0f;
    zoom_ = std::clamp(z, minZoom_, maxZoom_);
    panX_ = panY_ = 0;
    NotifyViewport();
}

void ImageViewPlusWidget::ResetView() {
    zoom_ = 1.0f;
    panX_ = panY_ = 0;
    NotifyViewport();
}

void ImageViewPlusWidget::NotifyViewport() {
    if (onViewportChanged) onViewportChanged(zoom_, panX_, panY_);
}

D2D1_RECT_F ImageViewPlusWidget::ComputeDestRect() const {
    float areaW = rect.right - rect.left;
    float areaH = rect.bottom - rect.top;
    float drawW = ImageWidth()  * zoom_;
    float drawH = ImageHeight() * zoom_;
    float cx = rect.left + areaW / 2.0f + panX_;
    float cy = rect.top  + areaH / 2.0f + panY_;
    return { cx - drawW / 2, cy - drawH / 2,
             cx + drawW / 2, cy + drawH / 2 };
}

// ========= 棋盘背景 =========

void ImageViewPlusWidget::EnsureCheckerboardTile(Renderer& r) {
    int curTheme = (int)theme::CurrentMode();
    if (checkerTileResourceKey_.IsValid() && checkerTheme_ == curTheme) {
        if (!checkerTile_) {
            auto res = GlobalResourceStore().Acquire(checkerTileResourceKey_);
            if (res && res->bytes) {
                checkerTile_ = r.CreateBitmapFromPixels(
                    res->bytes->data(), res->width, res->height, res->stride);
            }
        }
        return;
    }

    if (checkerTileResourceKey_.IsValid()) {
        GlobalResourceStore().Remove(checkerTileResourceKey_);
        checkerTileResourceKey_ = {};
    }
    checkerTile_.Reset();

    const int sz = 16;
    uint8_t pixels[sz * sz * 4];
    uint32_t c1, c2;
    if (theme::CurrentMode() == theme::Mode::Dark) {
        c1 = (255u << 24) | (71u << 16)  | (64u << 8)  | 64u;
        c2 = (255u << 24) | (56u << 16)  | (51u << 8)  | 51u;
    } else {
        /* build 280: 浅色格子提亮到 #FAFAFA/#E6E6E6 —— 旧的 #CCCCCC/#999999
         * 在浅色主题下压得太重, 透明区看起来比图还抢眼。 */
        c1 = (255u << 24) | (250u << 16) | (250u << 8) | 250u;
        c2 = (255u << 24) | (230u << 16) | (230u << 8) | 230u;
    }
    for (int y = 0; y < sz; ++y)
        for (int x = 0; x < sz; ++x) {
            int ix = x / 8, iy = y / 8;
            uint32_t c = ((ix + iy) % 2 == 0) ? c1 : c2;
            memcpy(pixels + (y * sz + x) * 4, &c, 4);
        }

    const uint64_t nextGeneration = checkerTileGeneration_ + 1;
    ResourceKey resourceKey = GlobalResourceStore().AddImage(
        ResourceKind::Bitmap, nextGeneration, sz, sz, sz * 4,
        PixelFormat::BgraPremul, pixels, true);
    if (!resourceKey.IsValid()) return;

    checkerTileResourceKey_ = resourceKey;
    checkerTileGeneration_ = nextGeneration;
    auto res = GlobalResourceStore().Acquire(checkerTileResourceKey_);
    if (res && res->bytes) {
        checkerTile_ = r.CreateBitmapFromPixels(
            res->bytes->data(), res->width, res->height, res->stride);
    }
    checkerTheme_ = curTheme;
}

void ImageViewPlusWidget::DrawCheckerboard(Renderer& r, const D2D1_RECT_F& area) {
    EnsureCheckerboardTile(r);
    if (checkerTile_ || checkerTileResourceKey_.IsValid()) {
        r.FillRectWithImagePattern(checkerTileResourceKey_, checkerTile_.Get(), area);
    }
}

// ========= OnDraw 主流程 =========

void ImageViewPlusWidget::OnDraw(Renderer& r) {
    // L18: letterbox 完全透明 —— 应用层用父容器 CSS background 装饰
    // (跟 button/label/svg 等其他 widget 设计哲学一致：只画自己的内容，
    // 不画自己的"留白")。set_checkerboard 退化为"只在图区+图带 alpha 时
    // 画棋盘"，给 PNG 透明像素位置一个对比背景。
    if (loading_) {
        float cx = (rect.left + rect.right) / 2;
        float cy = (rect.top + rect.bottom) / 2;
        auto col = theme::kAccent(); col.a = 0.8f;
        DrawSpinner(r, cx, cy, 20.0f, loadingAngle_, col, 2.5f);
        loadingAngle_ += 8.0f;
        if (loadingAngle_ >= 360.0f) loadingAngle_ -= 360.0f;
        return;
    }

    if (!source_) return;

    r.PushClip(rect);

    D2D1_RECT_F dest = ComputeDestRect();

    // 棋盘只在图区 (dest) + 图带 alpha 时画 —— letterbox 区跳过，保持透明。
    auto caps = source_->Caps();
    if (checkerboard_ && caps.alpha) DrawCheckerboard(r, dest);

    ImageDrawContext ctx;
    ctx.dest         = dest;
    ctx.zoom         = zoom_;
    ctx.rotation     = rotation_;
    ctx.antialias    = antialias_;
    ctx.checkerboard = checkerboard_;
    source_->Draw(r, ctx);

    if (cropMode_) DrawCropOverlay(r);

    r.PopClip();
}

// ========= 鼠标事件 =========

bool ImageViewPlusWidget::OnMouseDown(const MouseEvent& e) {
    if (!Contains(e.x, e.y) || !source_) return false;

    if (cropMode_) {
        auto handle = HitTestCropHandle(e.x, e.y);
        if (handle != CH_None) {
            cropDragHandle_ = handle;
            cropDragStartX_ = e.x; cropDragStartY_ = e.y;
            cropDragOrigX_ = cropX_; cropDragOrigY_ = cropY_;
            cropDragOrigW_ = cropW_; cropDragOrigH_ = cropH_;
            return true;
        }
    }

    // L16: pan 只在图实际显示矩形内响应；letterbox 区按下不启动拖动 ——
    // 跟标准看图软件 (Photos / IrfanView) 行为一致。crop mode 已经在
    // 上面的 hit-test 处理过了，走到这里说明非 crop。
    D2D1_RECT_F dest = ComputeDestRect();
    if (e.x < dest.left || e.x >= dest.right ||
        e.y < dest.top  || e.y >= dest.bottom) {
        return false;
    }

    ConstrainPan();
    dragging_ = true;
    dragStartX_ = e.x; dragStartY_ = e.y;
    dragPanX_ = panX_; dragPanY_ = panY_;
    return true;
}

bool ImageViewPlusWidget::OnMouseMove(const MouseEvent& e) {
    if (cropMode_ && cropDragHandle_ != CH_None) {
        float dx = (e.x - cropDragStartX_) / zoom_;
        float dy = (e.y - cropDragStartY_) / zoom_;
        float nx = cropDragOrigX_, ny = cropDragOrigY_;
        float nw = cropDragOrigW_, nh = cropDragOrigH_;
        switch (cropDragHandle_) {
            case CH_Move:        nx += dx; ny += dy; break;
            case CH_TopLeft:     nx += dx; ny += dy; nw -= dx; nh -= dy; break;
            case CH_Top:                   ny += dy;           nh -= dy; break;
            case CH_TopRight:              ny += dy; nw += dx; nh -= dy; break;
            case CH_Right:                           nw += dx;           break;
            case CH_BottomRight:                     nw += dx; nh += dy; break;
            case CH_Bottom:                                    nh += dy; break;
            case CH_BottomLeft:  nx += dx;           nw -= dx; nh += dy; break;
            case CH_Left:        nx += dx;           nw -= dx;           break;
            default: break;
        }
        if (cropAspectRatio_ > 0 && cropDragHandle_ != CH_Move)
            nh = nw / cropAspectRatio_;
        if (nw >= 10 && nh >= 10) {
            cropX_ = nx; cropY_ = ny; cropW_ = nw; cropH_ = nh;
            ClampCrop();
            if (onCropChanged) onCropChanged(cropX_, cropY_, cropW_, cropH_);
        }
        return true;
    }

    if (dragging_) {
        panX_ = dragPanX_ + (e.x - dragStartX_);
        panY_ = dragPanY_ + (e.y - dragStartY_);
        ConstrainPan();
        NotifyViewport();
        return true;
    }
    return false;
}

bool ImageViewPlusWidget::OnMouseUp(const MouseEvent&) {
    dragging_ = false;
    cropDragHandle_ = CH_None;
    return true;
}

void ImageViewPlusWidget::SetZoomAround(float z, float anchorX, float anchorY) {
    if (!source_) return;
    float oldZoom = zoom_;
    float newZoom = std::clamp(z, minZoom_, maxZoom_);
    if (newZoom == oldZoom) return;
    zoom_ = newZoom;

    // 锚点是 widget 像素坐标（rect 已减出去）。imgC 是该锚点在 image 空
    // 间的像素位置，zoom 前后保持不变 → 反推新的 panX/panY 让锚点像素
    // 落在原 widget 位置。
    float areaW = rect.right - rect.left;
    float areaH = rect.bottom - rect.top;
    float imgCx = (anchorX - areaW / 2.0f - panX_) / oldZoom;
    float imgCy = (anchorY - areaH / 2.0f - panY_) / oldZoom;
    panX_ = anchorX - areaW / 2.0f - imgCx * zoom_;
    panY_ = anchorY - areaH / 2.0f - imgCy * zoom_;

    NotifyViewport();
}

bool ImageViewPlusWidget::OnMouseWheel(const MouseEvent& e) {
    if (!Contains(e.x, e.y) || !source_) return false;
    float factor = (e.delta > 0) ? 1.15f : (1.0f / 1.15f);
    float newZoom = std::clamp(zoom_ * factor, minZoom_, maxZoom_);
    // 跨过 1.0 时 snap 到 1.0，给"刚好原始尺寸"一个 tick stop
    if ((zoom_ < 1.0f && newZoom > 1.0f) || (zoom_ > 1.0f && newZoom < 1.0f))
        newZoom = 1.0f;
    SetZoomAround(newZoom, e.x - rect.left, e.y - rect.top);
    return true;
}

void ImageViewPlusWidget::ConstrainPan() {
    // FreePan 模式：任意尺寸都可拖到画布外，完全不约束
    if (freePan_) return;

    // 默认策略（与 ImageViewWidget 一致）：
    //   图片 ≤ 画布 → 强制居中 panX/Y = 0
    //   图片 > 画布 → 允许在 ±(diff/2) 边界内平移
    float areaW = rect.right - rect.left;
    float areaH = rect.bottom - rect.top;
    float drawW = ImageWidth()  * zoom_;
    float drawH = ImageHeight() * zoom_;
    if (drawW <= areaW) panX_ = 0;
    else {
        float limit = (drawW - areaW) / 2.0f;
        panX_ = std::clamp(panX_, -limit, limit);
    }
    if (drawH <= areaH) panY_ = 0;
    else {
        float limit = (drawH - areaH) / 2.0f;
        panY_ = std::clamp(panY_, -limit, limit);
    }
}

D2D1_SIZE_F ImageViewPlusWidget::SizeHint() const { return {0, 0}; }

// ========= Crop =========

void ImageViewPlusWidget::SetCropMode(bool on) {
    cropMode_ = on;
    if (on && source_) {
        cropX_ = 0; cropY_ = 0;
        cropW_ = (float)source_->Width();
        cropH_ = (float)source_->Height();
    }
}
void ImageViewPlusWidget::SetCropRect(float x, float y, float w, float h) {
    cropX_ = x; cropY_ = y; cropW_ = w; cropH_ = h;
    ClampCrop();
}
void ImageViewPlusWidget::GetCropRect(float& x, float& y, float& w, float& h) const {
    x = cropX_; y = cropY_; w = cropW_; h = cropH_;
}
void ImageViewPlusWidget::ResetCrop() {
    if (!source_) { cropX_=cropY_=cropW_=cropH_=0; return; }
    cropX_ = 0; cropY_ = 0;
    cropW_ = (float)source_->Width();
    cropH_ = (float)source_->Height();
}
void ImageViewPlusWidget::ClampCrop() {
    if (!source_) return;
    float iw = (float)source_->Width();
    float ih = (float)source_->Height();
    if (cropX_ < 0) { cropW_ += cropX_; cropX_ = 0; }
    if (cropY_ < 0) { cropH_ += cropY_; cropY_ = 0; }
    if (cropX_ + cropW_ > iw) cropW_ = iw - cropX_;
    if (cropY_ + cropH_ > ih) cropH_ = ih - cropY_;
    cropW_ = std::max(10.0f, cropW_);
    cropH_ = std::max(10.0f, cropH_);
}

D2D1_RECT_F ImageViewPlusWidget::CropScreenRect() const {
    float sx1, sy1, sx2, sy2;
    ImageToScreen(cropX_, cropY_, sx1, sy1);
    ImageToScreen(cropX_ + cropW_, cropY_ + cropH_, sx2, sy2);
    return {sx1, sy1, sx2, sy2};
}

ImageViewPlusWidget::CropHandle
ImageViewPlusWidget::HitTestCropHandle(float sx, float sy) const {
    auto rc = CropScreenRect();
    const float t = 8.0f;
    bool left   = std::fabs(sx - rc.left ) < t;
    bool right  = std::fabs(sx - rc.right) < t;
    bool top    = std::fabs(sy - rc.top  ) < t;
    bool bottom = std::fabs(sy - rc.bottom) < t;
    if (top && left)   return CH_TopLeft;
    if (top && right)  return CH_TopRight;
    if (bottom && left)  return CH_BottomLeft;
    if (bottom && right) return CH_BottomRight;
    if (top)    return CH_Top;
    if (bottom) return CH_Bottom;
    if (left)   return CH_Left;
    if (right)  return CH_Right;
    if (sx >= rc.left && sx <= rc.right && sy >= rc.top && sy <= rc.bottom)
        return CH_Move;
    return CH_None;
}

void ImageViewPlusWidget::DrawCropOverlay(Renderer& r) {
    auto rc = CropScreenRect();
    D2D1_COLOR_F dim = {0, 0, 0, 0.5f};
    // 4 块遮罩
    r.FillRect({rect.left, rect.top, rect.right, rc.top}, dim);
    r.FillRect({rect.left, rc.bottom, rect.right, rect.bottom}, dim);
    r.FillRect({rect.left, rc.top, rc.left, rc.bottom}, dim);
    r.FillRect({rc.right, rc.top, rect.right, rc.bottom}, dim);
    // 边框
    r.DrawRect(rc, D2D1::ColorF(D2D1::ColorF::White), 1.0f);
    // 8 个手柄
    auto drawHandle = [&](float cx, float cy) {
        r.FillRect({cx-4, cy-4, cx+4, cy+4}, D2D1::ColorF(D2D1::ColorF::White));
    };
    drawHandle(rc.left, rc.top);
    drawHandle((rc.left + rc.right)/2, rc.top);
    drawHandle(rc.right, rc.top);
    drawHandle(rc.right, (rc.top + rc.bottom)/2);
    drawHandle(rc.right, rc.bottom);
    drawHandle((rc.left + rc.right)/2, rc.bottom);
    drawHandle(rc.left, rc.bottom);
    drawHandle(rc.left, (rc.top + rc.bottom)/2);
}

// ========= 坐标映射 =========

void ImageViewPlusWidget::ImageToScreen(float ix, float iy,
                                         float& sx, float& sy) const {
    auto d = ComputeDestRect();
    float iw = (float)source_->Width();
    float ih = (float)source_->Height();
    // 简化：不考虑 rotation（Crop 基本都在 rotation=0 下用）
    sx = d.left + ix / iw * (d.right - d.left);
    sy = d.top  + iy / ih * (d.bottom - d.top);
}
void ImageViewPlusWidget::ScreenToImage(float sx, float sy,
                                         float& ix, float& iy) const {
    auto d = ComputeDestRect();
    float iw = (float)source_->Width();
    float ih = (float)source_->Height();
    ix = (sx - d.left) / (d.right - d.left) * iw;
    iy = (sy - d.top)  / (d.bottom - d.top) * ih;
}

// ========= Loading spinner =========

void ImageViewPlusWidget::SetLoading(bool on) {
    if (loading_ == on) return;
    loading_ = on;
    if (on) {
        loadingTimerId_ = SetTimer(nullptr, 0, 16, LoadingTimerProc);
        if (loadingTimerId_) g_loadingMap[loadingTimerId_] = this;
    } else {
        if (loadingTimerId_) {
            KillTimer(nullptr, loadingTimerId_);
            g_loadingMap.erase(loadingTimerId_);
            loadingTimerId_ = 0;
        }
    }
    InvalidateAllWindows();
}

void CALLBACK ImageViewPlusWidget::LoadingTimerProc(HWND, UINT, UINT_PTR id, DWORD) {
    auto it = g_loadingMap.find(id);
    if (it != g_loadingMap.end()) it->second->InvalidateAllWindows();
}

// ========= 动画 =========

bool ImageViewPlusWidget::IsAnimated() const {
    return source_ && source_->Caps().animated;
}
int  ImageViewPlusWidget::FrameCount() const {
    return source_ ? source_->FrameCount() : 0;
}
int  ImageViewPlusWidget::CurrentFrame() const {
    return source_ ? source_->CurrentFrame() : 0;
}

void ImageViewPlusWidget::StartAnimation() {
    if (!IsAnimated() || animRunning_) return;
    animRunning_ = true;
    lastTickMs_ = (double)GetTickCount64();
    animTimerId_ = SetTimer(nullptr, 0, 16, AnimTimerProc);  // ~60 Hz
    if (animTimerId_) g_animMap[animTimerId_] = this;
}

void ImageViewPlusWidget::StopAnimation() {
    animRunning_ = false;
    if (animTimerId_) {
        KillTimer(nullptr, animTimerId_);
        g_animMap.erase(animTimerId_);
        animTimerId_ = 0;
    }
}

void CALLBACK ImageViewPlusWidget::AnimTimerProc(HWND, UINT, UINT_PTR id, DWORD) {
    auto it = g_animMap.find(id);
    if (it != g_animMap.end()) it->second->OnAnimTick();
}

void ImageViewPlusWidget::OnAnimTick() {
    if (!source_) return;
    double now = (double)GetTickCount64();
    double dt  = now - lastTickMs_;
    lastTickMs_ = now;
    if (source_->Tick(dt)) InvalidateAllWindows();
}

// ========= 工具 =========

void ImageViewPlusWidget::InvalidateAllWindows() {
    EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM) -> BOOL {
        wchar_t cls[64];
        GetClassNameW(hwnd, cls, 64);
        if (wcscmp(cls, L"UiCore_Window") == 0)
            InvalidateRect(hwnd, nullptr, FALSE);
        return TRUE;
    }, 0);
}

// ========= Spinner（复制 controls.cpp 里的版本，避免跨文件依赖）=========

static void DrawSpinner(Renderer& r, float cx, float cy, float radius,
                        float angleDeg, const D2D1_COLOR_F& color, float stroke) {
    // 简单圆弧 spinner：12 段，亮度随 angle 旋转
    const int N = 12;
    for (int i = 0; i < N; ++i) {
        float a = (angleDeg + i * (360.0f / N)) * 3.14159265f / 180.0f;
        float x1 = cx + std::cos(a) * (radius - stroke * 2);
        float y1 = cy + std::sin(a) * (radius - stroke * 2);
        float x2 = cx + std::cos(a) * radius;
        float y2 = cy + std::sin(a) * radius;
        D2D1_COLOR_F c = color;
        c.a *= (float)i / (N - 1);
        r.DrawLine(x1, y1, x2, y2, c, stroke);
    }
}

} // namespace ui
