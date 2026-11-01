#pragma once
#include "widget.h"
#include "renderer.h"
#include "image_source.h"
#include <functional>
#include <memory>
#include <string>

/*
 * ImageViewPlus — 万能图像查看控件（D2D 原生）
 *
 * 对 ImageViewWidget 的升级版，职责分离：
 *   - ImageViewPlus      管 视口 (zoom/pan/rotate) / 交互 (mouse/wheel/key) /
 *                         crop overlay / loading spinner / 棋盘背景
 *   - IImageSource       管 内容本身 (位图/SVG/GIF/分块)，由 widget 委托绘制
 *
 * 相对 ImageViewWidget 的新增能力：
 *   - SVG 原生矢量（ID2D1SvgDocument，Win10 1607+，自动降级到 SvgIcon 解析）
 *   - 类型扩展开放：加新 source 类零改现有代码
 *   - 异步加载（SetFileAsync）后台线程解码 + UI 线程 SetSource
 *   - 动画统一 timer（所有 source 共用一个 tick 循环）
 *
 * 不支持的（ImageViewWidget 有但 Plus 暂不带）：
 *   - onMouseDownHook / onMouseMoveHook 拖出事件钩子
 *     （需要时可加，但避免一开始就塞业务细节）
 */

namespace ui {

class UI_API ImageViewPlusWidget : public Widget {
public:
    ImageViewPlusWidget();
    ~ImageViewPlusWidget();

    // ---- 图像源 ----
    // 同步加载（从文件），内部按扩展名分派 source
    bool LoadFromFile(const std::wstring& path, Renderer& r);

    // 异步加载推荐用法：用户自己起线程调 IImageSource::CreateFromFile，然后通过
    // ui_window_invoke 把 SetSource 调度回 UI 线程。详见 image_view_plus.cpp 注释。

    // 直接喂一个 source（高级用法，比如外部自己解码的 PSD / 异步完成回调）
    void SetSource(std::unique_ptr<IImageSource> src);

    // 便捷：像素
    void SetPixels(const void* pixels, int w, int h, int stride, Renderer& r);

    // 分块模式 —— 返回的 ITiledSource* 生命周期跟着 source_
    IImageSource::ITiledSource* BeginTiled(int fullW, int fullH,
                                            int tileSize, Renderer& r);

    void Clear();
    bool HasSource() const { return source_ != nullptr; }
    IImageSource* Source() const { return source_.get(); }

    // ---- 图像信息（考虑 rotation 的有效尺寸）----
    int ImageWidth()  const;
    int ImageHeight() const;
    int RawImageWidth()  const { return source_ ? source_->Width()  : 0; }
    int RawImageHeight() const { return source_ ? source_->Height() : 0; }

    // ---- 视口 ----
    float Zoom() const { return zoom_; }
    void  SetZoom(float z);
    // 以 widget 像素坐标 (anchorX, anchorY) 为锚点缩放。anchor 处的图像
    // 像素位置在 zoom 前后保持不变（鼠标 wheel 缩放就是这条路径）。下游
    // reload 后想保持鼠标锚点用这个 API。
    void  SetZoomAround(float z, float anchorX, float anchorY);
    void  SetPan(float x, float y) { panX_ = x; panY_ = y; }
    float PanX() const { return panX_; }
    float PanY() const { return panY_; }
    void  FitToView();
    void  ResetView();   // 1:1 居中
    void  SetZoomRange(float lo, float hi) { minZoom_ = lo; maxZoom_ = hi; }

    // ---- 旋转 (0/90/180/270) ----
    void SetRotation(int angle) { rotation_ = ((angle % 360) + 360) % 360; }
    int  Rotation() const { return rotation_; }

    // ---- 外观 ----
    void SetCheckerboard(bool on) { checkerboard_ = on; }
    void SetAntialias(bool on)    { antialias_ = on; }
    bool Antialias() const { return antialias_; }

    // 拖拽行为：
    //   false（默认）= 图片 ≤ 画布时强制居中 panX/Y=0，只有放大到溢出才能拖
    //   true          = 任意尺寸都可拖到画布外，小图也能自由平移
    void SetFreePan(bool on) { freePan_ = on; if (!on) ConstrainPan(); }
    bool FreePan() const { return freePan_; }

    // ---- Loading spinner ----
    void SetLoading(bool on);
    bool IsLoading() const { return loading_; }
    // 加载转圈靠 OnDraw 按时间画, 只要帧不推状态。
    bool AnimationActive() const override { return loading_; }
    AnimTick TickAnimation() override { return {false, loading_}; }

    // ---- 动画 ----
    bool IsAnimated() const;
    int  FrameCount() const;
    int  CurrentFrame() const;
    void StopAnimation();
    void StartAnimation();

    // ---- Crop 模式 ----
    /* 屏幕 (窗口 DIP) ↔ 图像像素。图像坐标是**原图坐标** (不含旋转),
     * 与 SetCropRect 同口径。宿主做标注 / 命中测试 / 浮层定位用。 */
    void  ScreenToImage(float sx, float sy, float& ix, float& iy) const;
    void  ImageToScreen(float ix, float iy, float& sx, float& sy) const;

    void  SetCropMode(bool on);
    bool  IsCropMode() const { return cropMode_; }
    void  SetCropRect(float x, float y, float w, float h);
    void  GetCropRect(float& x, float& y, float& w, float& h) const;
    void  SetCropAspectRatio(float ratio) { cropAspectRatio_ = ratio; }
    void  ResetCrop();
    std::function<void(float,float,float,float)> onCropChanged;

    // ---- 回调 ----
    std::function<void(float,float,float)> onViewportChanged;
    std::function<void()> onLoaded;      // 加载（含异步）完成
    std::function<void(const std::wstring&)> onLoadFailed;

    // ---- Widget 虚函数 ----
    void OnDraw(Renderer& r) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    bool OnMouseWheel(const MouseEvent& e) override;
    D2D1_SIZE_F SizeHint() const override;

private:
    std::unique_ptr<IImageSource> source_;

    // 视口
    float zoom_ = 1.0f;
    float panX_ = 0, panY_ = 0;
    float minZoom_ = 0.01f, maxZoom_ = 64.0f;
    int   rotation_ = 0;

    // 外观
    bool  checkerboard_ = true;
    bool  antialias_ = false;
    bool  freePan_ = false;   /* true 时允许图片拖到画布外 */

    // 拖拽
    bool  dragging_ = false;
    float dragStartX_ = 0, dragStartY_ = 0;
    float dragPanX_ = 0, dragPanY_ = 0;

    // Crop
    bool  cropMode_ = false;
    float cropX_ = 0, cropY_ = 0, cropW_ = 0, cropH_ = 0;
    float cropAspectRatio_ = 0;
    enum CropHandle { CH_None, CH_Move,
                      CH_TopLeft, CH_Top, CH_TopRight, CH_Right,
                      CH_BottomRight, CH_Bottom, CH_BottomLeft, CH_Left };
    CropHandle cropDragHandle_ = CH_None;
    float cropDragStartX_ = 0, cropDragStartY_ = 0;
    float cropDragOrigX_ = 0, cropDragOrigY_ = 0;
    float cropDragOrigW_ = 0, cropDragOrigH_ = 0;

    // Loading spinner
    bool       loading_ = false;
    bool       loadingHooked_ = false;   // 已登记到所在窗口的心跳 (只登记一次)
    void       HookLoadingFrames_();

    // 动画 timer（source->Tick 驱动）
    UINT_PTR   animTimerId_ = 0;
    static constexpr UINT_PTR kAnimTimerId = 9987;
    double     lastTickMs_ = 0.0;
    bool       animRunning_ = false;

    // 棋盘
    ComPtr<ID2D1Bitmap> checkerTile_;
    ResourceKey checkerTileResourceKey_;
    uint64_t checkerTileGeneration_ = 0;
    int checkerTheme_ = -1;
    void EnsureCheckerboardTile(Renderer& r);
    void DrawCheckerboard(Renderer& r, const D2D1_RECT_F& area);

    // 几何/坐标
    D2D1_RECT_F ComputeDestRect() const;
    void ConstrainPan();
    void NotifyViewport();

    // Crop 辅助
    D2D1_RECT_F CropScreenRect() const;
    CropHandle  HitTestCropHandle(float sx, float sy) const;
    void        DrawCropOverlay(Renderer& r);
    void        ClampCrop();

    // Timer
    static void CALLBACK AnimTimerProc   (HWND, UINT, UINT_PTR, DWORD);
    void OnAnimTick();
    void InvalidateAllWindows();
};

} // namespace ui
