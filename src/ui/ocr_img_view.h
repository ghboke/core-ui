#pragma once
//
// ocr_img_view.h — 图片划词 widget（OCR 文本选择画布）
//
// 场景：宿主加载一张图 → 调任意 OCR 引擎拿到文本 + quad 坐标 → 喂给本
// widget → 用户像 PDF 阅读器一样在图上划词选择、Ctrl+C 复制。
//
// 职责边界（对齐 gh_img_view 哲学）：
//   - 不做 OCR / 文件 IO / 解码 —— 宿主喂单张 BGRA8 premul 整图 + 文本项
//   - 选择逻辑全部在 OcrTextModel（纯逻辑, 可单测, 可被其他 widget 复用）
//   - widget 层只管：视口 (zoom/pan/fit)、手势路由、高亮渲染、剪贴板
//
// 交互约定：
//   对齐 PDF 阅读器惯例（Adobe / PDF.js / 系统阅读器都是这套）：
//     单击（无论文字上还是空白）= 清选区
//     文本上按下并拖动         = 阅读序划词（超过 3px 阈值才起选区）
//     空白按下并拖动           = 平移画布
//     双击                     = 选中该文本块
//     三击                     = 选中整行
//     Ctrl+C 复制 / Ctrl+A 全选 / Esc 清选区 / 滚轮以光标为锚缩放
//   注意"文本块"的粒度就是宿主喂的 OCR 粒度 —— 英文通常是词，中文 OCR
//   常整行返回一个块，此时双击与三击结果相同（合理退化）。
//
// v1 范围（预留不实现）：单张整图（无瓦块 pyramid）、rotation 恒 0
// （坐标变换函数签名已 rotation-aware）、整词粒度（char 级见 model 注释）。

#include "widget.h"
#include "renderer.h"
#include "render_handles.h"
#include "ocr_text_model.h"

#include <cstdint>
#include <functional>
#include <string>

namespace ui {

class UI_API OcrImgViewWidget : public Widget {
public:
    OcrImgViewWidget();
    ~OcrImgViewWidget() override;

    // ---- 图像 ----
    // SetImage 返回码
    enum SetImageResult {
        kSetImageOk          =  0,
        kSetImageBadArgs     = -1,  // 空指针 / 宽高为 0
        kSetImageTooLarge    = -2,  // 超出 GPU 单张纹理上限（典型 16384）
        kSetImageResourceErr = -3,  // 资源创建失败（内存不足等）
    };

    // 喂单张 BGRA8 premultiplied 整图（深拷入资源存储, 宿主缓冲随后可释放）。
    // stride == 0 时按 w*4。喂图后自动 Fit。会清空旧图与选区（换图语义）。
    //
    // 返回 kSetImageOk 或负的错误码。尺寸检查放在这里而不是留到绘制时 ——
    // 资源存储只存 CPU 字节, D2D 位图要到 OnDraw 才建, 超限的话表现为
    // "喂图成功但画面空白", 宿主完全无从得知。超限时宿主应先降采样
    // （或等瓦块分级支持）。
    int SetImage(const void* bgra, uint32_t w, uint32_t h, uint32_t stride,
                 Renderer& r);
    void ClearImage();
    bool     HasImage() const     { return imgW_ > 0; }
    uint32_t ImageWidth() const   { return imgW_; }
    uint32_t ImageHeight() const  { return imgH_; }

    // ---- OCR 文本（转发 OcrTextModel, 坐标 = 图像像素）----
    OcrTextModel&       Model()       { return model_; }
    const OcrTextModel& Model() const { return model_; }

    // 选中文本 UTF-8（内部缓冲, 下次选区变化前有效）。C API selected_text 用。
    const std::string& SelectedTextUtf8() const;
    // 选区变化时 widget 必做的内务（清文本缓存 + 重绘）。构造时挂在
    // model_.onSelectionChanged 上；C API 宿主覆盖该 callback 后必须先转调
    // 本方法再回自己的 cb（ui_api.cpp 的包装已保证）。
    void OnModelSelectionChanged();
    // 把当前选中文本写入系统剪贴板 (CF_UNICODETEXT)。无选区返 false。
    bool CopySelectionToClipboard() const;

    // ---- 视口 ----
    float Zoom() const            { return zoom_; }
    void  SetZoom(float z);
    void  SetZoomAround(float z, float anchorX, float anchorY);
    float PanX() const            { return panX_; }
    float PanY() const            { return panY_; }
    void  SetPan(float x, float y);
    void  Fit();                  // 长边贴合视口, 居中（rotation-aware）
    void  Reset();                // 1:1 居中
    void  SetZoomRange(float lo, float hi) { minZoom_ = lo; maxZoom_ = hi; }

    // ---- 旋转（90° 倍数）----
    // 语义与 gh_img_view 一致: pan 存【屏幕空间】, 不随 rotation 变换 ——
    // 鼠标拖动方向永远匹配视觉方向。zoom/pan 在旋转后保留（用户预期"转一下
    // 不丢视图状态"）, Fit 用旋转后的视觉 AABB。
    // 选区高亮走 ImageToScreen, 自动跟随旋转, 无需另行处理。
    void  SetRotation(int angle);   // 任意 int → ((a%360)+360)%360, 非 90° 倍数 round-to-90
    int   Rotation() const          { return rotation_; }

    // 高亮填充色。默认取主题 accent + 0.35 alpha; 宿主可覆盖。
    void  SetHighlightColor(const D2D1_COLOR_F& c) { highlight_ = c; hasCustomHighlight_ = true; }

    // ---- 坐标变换（rotation-aware 签名, v1 rotation 恒 0）----
    void ScreenToImage(float sx, float sy, float& ix, float& iy) const;
    void ImageToScreen(float ix, float iy, float& sx, float& sy) const;

    // ---- Widget 虚函数 ----
    void OnDraw(Renderer& r) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    bool OnMouseWheel(const MouseEvent& e) override;
    bool OnMouseDoubleClick(const MouseEvent& e) override;
    bool OnKeyDown(int vk) override;
    D2D1_SIZE_F SizeHint() const override;

private:
    // 逻辑 dest: 未旋转位图在屏幕上的 AABB。OnDraw 在此矩形内画图,
    // 再叠一层 D2D 旋转 transform 完成视觉旋转。
    D2D1_RECT_F ComputeDestRect() const;
    // 视觉 dest: 旋转后图像可见区域的 AABB（90/270 时宽高互换）。Fit 用它。
    D2D1_RECT_F ComputeVisualDestRect() const;
    uint32_t EffectiveImageWidth() const;   // 90/270 互换
    uint32_t EffectiveImageHeight() const;
    void Invalidate_();
    void UpdateHoverCursor_(float sx, float sy);
    // 高亮合并: 相邻两字能否并成一个 quad（同行且首尾相接）
    bool CanMergeQuads_(uint32_t a, uint32_t b) const;
    // 把 [first,last] 合并成一个 quad 填充（首项左边 + 末项右边）
    void FillMergedRun_(Renderer& r, uint32_t first, uint32_t last,
                        const D2D1_COLOR_F& color);

    // ---- 图像 ----
    ResourceKey imageResourceKey_;
    uint32_t imgW_ = 0, imgH_ = 0;
    uint64_t imageGeneration_ = 0;

    // ---- 文本/选择 ----
    OcrTextModel model_;
    mutable std::string selectedUtf8_;     // SelectedTextUtf8 缓冲

    // ---- 视口 ----
    float zoom_ = 1.0f;
    float panX_ = 0, panY_ = 0;
    float minZoom_ = 0.05f, maxZoom_ = 32.0f;
    int   rotation_ = 0;                   // 预留, v1 恒 0

    // ---- 手势 ----
    bool  panning_    = false;             // 空白拖动平移中
    bool  selectingDrag_ = false;          // 划词拖动中（已越过阈值）
    bool  pendingSelect_ = false;          // 按在文字上, 等位移超阈值才起选区
    uint32_t pendingAnchor_ = 0;           // pendingSelect_ 时记下的锚点 item
    bool  moved_      = false;             // down→up 之间是否超过点击阈值
    float downX_ = 0, downY_ = 0;
    float dragPanX_ = 0, dragPanY_ = 0;
    /* 三击判定: Win32 只发到双击为止, 第三下是普通 WM_LBUTTONDOWN。
     * 记下双击时刻与位置, 下一次 down 若在系统双击时间内且位置相近 → 三击。*/
    unsigned long lastDblTick_ = 0;
    float lastDblX_ = 0, lastDblY_ = 0;

    // ---- 高亮 ----
    D2D1_COLOR_F highlight_{};
    bool hasCustomHighlight_ = false;
};

} // namespace ui
