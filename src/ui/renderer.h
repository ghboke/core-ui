#pragma once
#include <d2d1_1.h>
#include <d2d1_3.h>   /* ID2D1DeviceContext5 / ID2D1SvgDocument (Win10 1607+) */
#include <d3d11.h>
#include <d3d11_4.h>  /* ID3D11Multithread */
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <dwrite.h>
#include <dwrite_2.h>   /* IDWriteFontFallback / Builder (DWrite 1.2+) */
#include <dwrite_3.h>   /* IDWriteTextFormat3 (DWrite 1.3+, Win10) */
#include <wincodec.h>
#include <wrl/client.h>
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "theme.h"
#include "display_list.h"
#include "render_handles.h"

using Microsoft::WRL::ComPtr;

namespace ui {

// Parsed SVG icon (cached geometry for efficient rendering)
struct SvgPathLayer {
    ComPtr<ID2D1PathGeometry> geometry;
    std::vector<std::string> pathData;
    float opacity = 1.0f;     // per-path opacity from SVG
    float strokeWidth = 0.0f; // >0 means stroke instead of fill
    /* 累积的 SVG transform（来自所有外层 <g transform> + 自身 transform）。
     * 行向量乘法语义：屏幕点 = 路径局部点 * pathTransform * iconScale。 */
    D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Identity();
};

struct SvgIcon {
    ComPtr<ID2D1PathGeometry> geometry;  // combined (legacy, used if layers empty)
    std::vector<std::string> pathData;   // pure path-data for render-thread replay
    std::vector<SvgPathLayer> layers;    // per-path with opacity
    std::vector<SvgTextRun>   textRuns;  // L75: <text> / <foreignObject> 文字
    float viewBoxW = 24;
    float viewBoxH = 24;
    bool valid = false;
};

class Renderer {
public:
    class SharedD2DGuard {
    public:
        SharedD2DGuard();
        ~SharedD2DGuard();
        SharedD2DGuard(const SharedD2DGuard&) = delete;
        SharedD2DGuard& operator=(const SharedD2DGuard&) = delete;
    };

    ~Renderer();

    // Create and own factories internally (standalone mode)
    bool Init();

    // Use externally-owned factories (DLL shared mode)
    bool Init(ID2D1Factory1* factory, IDWriteFactory* dwFactory, IWICImagingFactory* wicFactory);

    bool CreateRenderTarget(HWND hwnd);
    // Composition-mode RT for transparent/layered windows (popup menus,
    // tooltips). Builds a SwapChainForComposition with premultiplied alpha
    // and binds it via DirectComposition so per-pixel alpha is honored on
    // the desktop without WS_EX_LAYERED's GDI bottleneck. Anti-aliased
    // round corners come from D2D's normal FillRoundedRectangle path.
    bool CreateRenderTargetForLayered(HWND hwnd);
    void ReleaseRenderTarget();
    void Resize(UINT width, UINT height);
    void BeginDraw();
    HRESULT EndDraw();
    HRESULT PresentPrepared(bool skipVSync);
    HRESULT SetSwapChainBackgroundColor(const D2D1_COLOR_F& color);
    void FlushAndTrimGpu();

    // Skip VSync for next Present (used during resize for immediate feedback)
    bool skipVSync = false;

    // L177: Skip the swapChain Present for the next EndDraw — still flushes the
    // D2D draw to the back buffer, just doesn't flip to DWM. For painting a
    // hidden / not-yet-shown window: DWM doesn't composite it so the flip would
    // block in some GPU drivers (AMD), yet the draw must still happen so widgets
    // settle and stop re-invalidating (else a WM_PAINT flood). Auto-reset each
    // EndDraw.
    bool skipPresent = false;

    void Clear(const D2D1_COLOR_F& color);
    void SetDisplayListRecorder(DisplayListRecorder* recorder);
    DisplayListRecorder* DisplayListRecorderTarget() const { return displayListRecorder_; }
    bool IsRecordingDisplayList() const { return ActiveDisplayListRecorder() != nullptr; }
    bool RenderSvgDocumentToBgra(const std::vector<SvgDocumentRef::Step>& steps,
                                 float viewportW, float viewportH,
                                 uint32_t width, uint32_t height,
                                 uint8_t* outBgra);

    /* 按 steps 的顺序逐段绘制到 ctx5 当前目标 —— D2D filter 的 z 正确性全在这个
     * 顺序里, 三个绘制点 (UI 线程即时 / display list 回放 / 离屏 BGRA) 必须共用
     * 这一份实现, 不要各写各的循环。
     *
     * contentDocs / shadowDocs 是可选缓存 (与 steps 等长, 懒建): UI 线程即时路径
     * 传自己的成员缓存避免每帧重解析 SVG; 回放和离屏路径传 nullptr 现建现用。
     * 调用前 ctx5 需已 BeginDraw; 返回时 transform 恢复为 svgXform。 */
    static void DrawSvgStepsOrdered(
        ID2D1DeviceContext5* ctx5,
        const std::vector<SvgDocumentRef::Step>& steps,
        float viewportW, float viewportH,
        const D2D1_MATRIX_3X2_F& svgXform,
        std::vector<ComPtr<ID2D1SvgDocument>>* contentDocs,
        std::vector<ComPtr<ID2D1SvgDocument>>* shadowDocs);
    void FillRect(const D2D1_RECT_F& rect, const D2D1_COLOR_F& color);
    /* 任意四边形填充 (pts 顺序 TL,TR,BR,BL 或任意环绕序)。
     * FillRect + PushTransform 只能画平行四边形 —— 仿射变换保持边的平行性,
     * 梯形 / 透视四边形 (拍歪的照片里的一行字) 必须走真几何。
     * 内部建 ID2D1PathGeometry 后 FillGeometry, 无接缝、数学精确。 */
    void FillQuad(const D2D1_POINT_2F pts[4], const D2D1_COLOR_F& color);
    void FillRoundedRect(const D2D1_RECT_F& rect, float rx, float ry, const D2D1_COLOR_F& color);
    bool DrawBlurredRoundedRect(const D2D1_RECT_F& rect, float rx, float ry,
                                float blurRadius, const D2D1_COLOR_F& color);
    void DrawRect(const D2D1_RECT_F& rect, const D2D1_COLOR_F& color, float width = 1.0f);
    void DrawRoundedRect(const D2D1_RECT_F& rect, float rx, float ry, const D2D1_COLOR_F& color, float width = 1.0f);
    void DrawLine(float x1, float y1, float x2, float y2, const D2D1_COLOR_F& color, float width = 1.0f);
    void DrawText(const std::wstring& text, const D2D1_RECT_F& rect, const D2D1_COLOR_F& color,
                  float fontSize, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
                  DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL,
                  DWRITE_PARAGRAPH_ALIGNMENT vAlign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                  bool wordWrap = false,
                  const wchar_t* family = nullptr);
    float MeasureTextWidth(const std::wstring& text, float fontSize,
                           const wchar_t* family = nullptr,  /* nullptr = 用 DefaultFontFamily() */
                           DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL);
    float MeasureTextHeight(const std::wstring& text, float maxWidth, float fontSize,
                            DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL);
    void DrawIcon(const std::wstring& glyph, const D2D1_RECT_F& rect, const D2D1_COLOR_F& color, float fontSize);

    // Image support
    ComPtr<ID2D1Bitmap> LoadImageFromFile(const std::wstring& path);
    // 从内存里的 PNG/JPG/BMP/ICO/... 字节解码出位图。配合 ui::asset
    // 解析器使用：HTML <img src="logo.png"> → asset::Resolve → 这里。
    // bytes 在调用期间需有效；返回的位图自带 GPU 资源、跟字节解耦。
    ComPtr<ID2D1Bitmap> LoadImageFromBytes(const void* bytes, size_t size);
    bool DecodeImageFileToBgraPremul(const std::wstring& path,
                                     std::vector<uint8_t>& pixels,
                                     int& width, int& height, int& stride);
    bool DecodeImageBytesToBgraPremul(const void* bytes, size_t size,
                                      std::vector<uint8_t>& pixels,
                                      int& width, int& height, int& stride);

    // Animated image (GIF) support — 按需解码：维护一张 CPU 画布 + 元数据表，
    // 调用方在动画 timer 里推进帧。内存随 GIF 尺寸而非帧数增长。
    class AnimatedPlayer {
    public:
        ~AnimatedPlayer();
        int FrameCount() const { return (int)meta_.size(); }
        int CanvasWidth() const { return canvasW_; }
        int CanvasHeight() const { return canvasH_; }
        int DelayMs(int frameIndex) const;
        /* 把第 frameIndex 帧合成到画布，返回 BGRA 指针（stride = width*4）。
         * 顺序前进时 O(1)；出现跳帧或循环回绕时会从头重放至目标帧。 */
        const uint8_t* ComposeTo(int frameIndex);
    private:
        friend class Renderer;
        struct FrameMeta {
            UINT x = 0, y = 0, w = 0, h = 0;
            int delayMs = 100;
            int disposal = 0;
        };
        ComPtr<IWICImagingFactory> wic_;
        ComPtr<IWICBitmapDecoder> decoder_;
        int canvasW_ = 0, canvasH_ = 0;
        std::vector<FrameMeta> meta_;
        std::vector<uint8_t> canvas_;
        std::vector<uint8_t> prevCanvas_;  // disposal=3 备份
        int lastComposed_ = -1;
        bool DecodeOne(int index);
        void ResetCanvas();
    };
    /* 打开一个动图（目前仅 GIF）。返回 nullptr 表示非动画或失败。 */
    std::unique_ptr<AnimatedPlayer> OpenAnimatedImage(const std::wstring& path);
    ComPtr<ID2D1Bitmap> CreateBitmapFromPixels(const void* pixels, int width, int height, int stride);
    /* 跟 CreateBitmapFromPixels 一样, 但接收 straight (non-premultiplied) alpha
       BGRA — 内部做 RGB *= A/255 转 premul 再创建 D2D bitmap. 适合直接从 PNG/
       JPEG 解码器拿到的 BGRA (ghde / WIC GUID_WICPixelFormat32bppBGRA / libpng
       / stb_image 等). 不这么做的话, straight 字节被 D2D PREMULTIPLIED bitmap
       按 premul 公式渲染, 抗锯齿边缘 (alpha 中间值) 颜色被错误广播 → chroma
       fringe (透明 PNG 大倍率放大尤其明显). */
    ComPtr<ID2D1Bitmap> CreateBitmapFromPixelsStraight(const void* pixels, int width, int height, int stride);
    ComPtr<ID2D1Bitmap> CreateEmptyBitmap(int width, int height);
    /* HICON → D2D 位图。用于把 EXE 嵌入的图标资源 / Win32 加载的图标转成
       D2D 可绘制位图。caller 仍持有 HICON 所有权，函数内不 DestroyIcon。 */
    bool DecodeHICONToBgraPremul(HICON hicon,
                                 std::vector<uint8_t>& pixels,
                                 int& width, int& height, int& stride);
    ComPtr<ID2D1Bitmap> CreateBitmapFromHICON(HICON hicon);
    void DrawBitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F& destRect, float opacity = 1.0f,
                    D2D1_BITMAP_INTERPOLATION_MODE interp = D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    /* 高质量插值绘制（缩小时用 HIGH_QUALITY_CUBIC，文字/线条更清晰） */
    void DrawBitmapHQ(ID2D1Bitmap* bitmap, const D2D1_RECT_F& destRect, float opacity = 1.0f,
                      D2D1_INTERPOLATION_MODE interp = D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
    /* 边缘 clamp 绘制: BitmapBrush1 + EXTEND_CLAMP, 越界采样重复边缘像素。
     * 分块瓦片放大绘制用 — DrawBitmap 的插值在位图边缘外按透明采样, 会在
     * 瓦片拼缝处淡出一条半透明缝线。 */
    void DrawBitmapClamped(ID2D1Bitmap* bitmap, const D2D1_RECT_F& destRect, float opacity = 1.0f,
                           D2D1_INTERPOLATION_MODE interp = D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
    /* 带锐化的位图绘制（用于图片查看器） */
    void DrawBitmapSharpened(ID2D1Bitmap* bitmap, const D2D1_RECT_F& destRect, float sharpenAmount = 0.3f,
                              D2D1_INTERPOLATION_MODE interp = D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
    void RecordImage(ResourceKey key, const D2D1_RECT_F& destRect,
                     ImageSampling sampling, float opacity = 1.0f);
    void DrawImageResource(ResourceKey key, const D2D1_RECT_F& destRect,
                           ImageSampling sampling, float opacity = 1.0f);
    void FillRectWithImagePattern(ResourceKey key, ID2D1Bitmap* bitmap, const D2D1_RECT_F& rect);
    void FillGradientRect(GradientRef gradient, const D2D1_RECT_F& rect, float radius);
    void RecordSvgDocument(std::vector<SvgDocumentRef::Step> steps,
                           float viewportW, float viewportH,
                           D2D1_MATRIX_3X2_F transform);
    /* 单段便利重载 — 不带 filter 的调用方 (image_source_svg / svg_widget) 用。 */
    void RecordSvgDocument(std::string xml, float viewportW, float viewportH,
                           D2D1_MATRIX_3X2_F transform);
    bool DrawBackdropBlur(const D2D1_RECT_F& rect, float radius, float blurRadius);
    void FillRectWithBitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F& rect);
    void PushClip(const D2D1_RECT_F& rect);
    /* aliased 裁剪 — 相邻 clip 矩形拼接绘制同一内容时用, 见 recorder 注释 */
    void PushClipAliased(const D2D1_RECT_F& rect);
    void PopClip();
    // Rounded-rect clip: anything drawn between Push/Pop is masked to the
    // rounded shape. Required to keep state-layer hovers (e.g. up/down buttons
    // on a rounded NumberBox, items in a rounded ComboBox popup) from spilling
    // past the container's corners. Must be paired with PopRoundedClip().
    void PushRoundedClip(const D2D1_RECT_F& rect, float rx, float ry);
    void PopRoundedClip();

    /* ---- 视口剔除 (build 285) --------------------------------------------
     * opt-in: 只有明确 PushCull 的容器 (目前只有 ScrollViewWidget) 才启用。
     * 栈为空时 IsCulled 恒为 false, 所有既有绘制路径行为不变。
     *
     * 为什么需要: 长列表里 clip 只让 D2D 把画出界的像素丢掉, 遍历子树和录
     * display list 的钱一分没省 —— 2000 行的 ScrollView 每帧要录 2000 行,
     * 实测 6.6ms/帧, 拖动滚动条明显不跟手。
     *
     * 注意不要在这里做"全局默认开": 任何画到自己 rect 之外的控件 (阴影、
     * 焦点环、溢出装饰) 都会被误剪。Widget::DrawTree 判定时会按该 widget 的
     * box-shadow 外扩量放宽, 但那只覆盖阴影这一类。 */
    void PushCull(const D2D1_RECT_F& rect);
    void PopCull();
    /* rect 与当前剔除矩形完全不相交 → true (可以整棵子树跳过)。
     * 栈为空 → 永远 false。 */
    bool IsCulled(const D2D1_RECT_F& rect) const;
    void PushOpacity(float opacity, const D2D1_RECT_F& bounds);
    void PopOpacity();
    void PushTransform(const D2D1_MATRIX_3X2_F& transform);
    void PopTransform();
    void ReplayDisplayList(const DisplayList& list);

    // SVG icon support
    SvgIcon ParseSvgIcon(const std::string& svgContent);
    void DrawSvgIcon(const SvgIcon& icon, const D2D1_RECT_F& rect, const D2D1_COLOR_F& color);
    /* L75: 只解析 SVG 文字 run (不建 path 几何) —— 给 D2D 原生 SVG 路径用:
     * D2D 画形状, 这个补文字. */
    std::vector<SvgTextRun> ParseSvgTextRuns(const std::string& svgContent);
    /* L75: DirectWrite 渲染文字 run. baseXf = SVG user-space → 屏幕 的变换
     * (跟 DrawSvgDocument / DrawSvgIcon 同一个), 每个 run 再左乘自身累积 transform.
     * 内部 save/restore 当前 RT transform. */
    void DrawSvgTextRuns(const std::vector<SvgTextRun>& runs,
                         const D2D1_MATRIX_3X2_F& baseXf);

    /* L121: 把 SVG 里的 <text>/<foreignObject> 用 DirectWrite 字形轮廓转成等价
     * <path>, 原地替换回 DOM, 让 D2D DrawSvgDocument 按文档顺序统一渲染 ——
     * 文字与形状回到同一条有序管线, z 序天然正确 (修复堆叠卡片文字溢出 / 被遮挡
     * 文字反浮到最上层). 返回改写后的 SVG xml; 无 <text> 或无 DWrite 时原样返回. */
    std::string SvgInlineTextAsPaths(const std::string& svgContent);

    ID2D1DeviceContext* RT() { return ctx_.Get(); }
    /* ID2D1DeviceContext5（支持 CreateSvgDocument / DrawSvgDocument）。
     * Windows 10 1607 以下返回 nullptr，调用方应做 null 检查并降级。
     * ctx5_ 与 ctx_ 指向同一 D2D 对象，通过 QueryInterface 获得。*/
    ID2D1DeviceContext5* RT5() { return ctx5_.Get(); }
    /* 创建/校验 D2D SVG document。渲染线程接管后 UI Renderer 可能没有窗口
     * RT，此时会临时创建无 target 的 device context 解析 SVG，避免加载阶段
     * 依赖 UI 线程 render target。outDoc 可为空，仅做校验。 */
    bool CreateSvgDocumentFromXml(const std::string& xml,
                                  float viewportW, float viewportH,
                                  ComPtr<ID2D1SvgDocument>* outDoc = nullptr);
    ID2D1Factory1* Factory() { return factory_; }
    IDWriteFactory* DWFactory() { return dwFactory_; }
    IWICImagingFactory* WIC() { return wicFactory_; }

    /* Build an IDWriteTextLayout for the given text. Used by widgets that
     * need to share one layout between hit-test, caret positioning,
     * selection rendering and draw (TextArea). `wrap` controls
     * DWRITE_WORD_WRAPPING_WRAP vs NO_WRAP. Returns nullptr on failure.
     * Layout uses theme::kFontFamily (and per-window override if set). */
    ComPtr<IDWriteTextLayout> CreateTextLayout(const std::wstring& text,
                                                float maxWidth, float maxHeight,
                                                float fontSize,
                                                bool wrap = false,
                                                DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL);

    // 临时设置渲染目标（用于缓存等场景）
    void SetTarget(ID2D1DeviceContext* target) {
        ctx_ = target;
        ctx5_.Reset();
        if (ctx_) {
            ctx_.As(&ctx5_);
        }
        targetThreadId_ = GetCurrentThreadId();
    }

    // ---- 字体 / 渲染模式 per-window 状态 (since 1.3.0) ----
    // 空串 = 跟随 theme:: 全局默认
    void        SetDefaultFontFamily(const wchar_t* family);
    void        SetCjkFonts(const wchar_t* latin, const wchar_t* cjk);
    void        SetTextRenderMode(theme::TextRenderMode mode);
    const wchar_t* DefaultFontFamily() const;   // 实际生效的默认字体族（窗口 > theme > "Segoe UI"）
    const wchar_t* LatinFontFamily() const;     // 中英分离的拉丁字体，nullptr=未设
    const wchar_t* CjkFontFamily() const;       // 中英分离的中文字体，nullptr=未设
    theme::TextRenderMode TextRenderMode() const;

    // 应用当前文字渲染模式到 ctx_（在 BeginDraw 之前调用或 CreateRenderTarget 后立即调用）
    void ApplyTextRenderMode();

private:
    /* 视口剔除矩形栈 (build 285). 嵌套 ScrollView 时取交集 —— 内层只可能比
     * 外层更小, 存交集省得判定时反复回溯整个栈。 */
    std::vector<D2D1_RECT_F> cullStack_;

    struct ColorKey {
        uint32_t r = 0;
        uint32_t g = 0;
        uint32_t b = 0;
        uint32_t a = 0;

        bool operator==(const ColorKey& other) const {
            return r == other.r && g == other.g && b == other.b && a == other.a;
        }
    };

    struct ColorKeyHash {
        size_t operator()(const ColorKey& k) const {
            size_t h = 1469598103934665603ull;
            h = (h ^ k.r) * 1099511628211ull;
            h = (h ^ k.g) * 1099511628211ull;
            h = (h ^ k.b) * 1099511628211ull;
            h = (h ^ k.a) * 1099511628211ull;
            return h;
        }
    };

    struct TextFormatKey {
        uint32_t sizeBits = 0;
        uint32_t weight = 0;
        std::wstring family;

        bool operator==(const TextFormatKey& other) const {
            return sizeBits == other.sizeBits &&
                   weight == other.weight &&
                   family == other.family;
        }
    };

    struct TextFormatKeyHash {
        size_t operator()(const TextFormatKey& k) const {
            size_t h = std::hash<uint32_t>{}(k.sizeBits);
            h ^= (std::hash<uint32_t>{}(k.weight) + 0x9e3779b9 + (h << 6) + (h >> 2));
            h ^= (std::hash<std::wstring>{}(k.family) + 0x9e3779b9 + (h << 6) + (h >> 2));
            return h;
        }
    };

    // Owned factories (standalone mode)
    ComPtr<ID2D1Factory1>      ownedFactory_;
    ComPtr<IDWriteFactory>     ownedDwFactory_;
    ComPtr<IWICImagingFactory> ownedWicFactory_;
    ComPtr<ID2D1Factory1>      sharedDeviceFactory_;

    // Active pointers (may point to owned or external)
    ID2D1Factory1*      factory_    = nullptr;
    IDWriteFactory*     dwFactory_  = nullptr;
    IWICImagingFactory* wicFactory_ = nullptr;

    // D3D11/D2D 设备（全局共享，首个窗口创建时初始化）
    static ComPtr<ID3D11Device>  s_d3dDevice;
    static ComPtr<ID2D1Device>   s_d2dDevice;
    static int                   s_deviceRefCount;
    bool EnsureSharedDevice();
    bool AcquireSharedDeviceRef();
    void BindFactoryFromSharedDevice();
    void ReleaseSharedDeviceRef();

public:
    /* 启动加速: 后台线程预创建共享 D3D11 设备 (D3D11CreateDevice 占首窗
     * RT 创建 ~39ms 且 free-threaded 可跨线程移交)。ui_init 时调一次,
     * EnsureSharedDevice 到点收割 — 窗口创建期间 (~60ms) 设备已并行建好,
     * 首窗 RT 创建从 ~39ms 降到 ~3ms。D2D 设备仍在 UI 线程创建
     * (single-threaded factory 约束)。幂等, 多次调用无害。 */
    static void PrewarmSharedDeviceAsync();
    static void ResetSharedDeviceForDeviceLost();

private:

    // 每窗口独立资源
    ComPtr<ID2D1DeviceContext>  ctx_;
    /* ctx_ QueryInterface 到 ID2D1DeviceContext5 的副本，Win10 1607 以下为空。
     * 同对象多接口，不增加实际资源，仅提供 SVG 等 1607+ API 的入口。*/
    ComPtr<ID2D1DeviceContext5> ctx5_;
    ComPtr<IDXGISwapChain1>    swapChain_;
    ComPtr<ID2D1Bitmap1>       targetBitmap_;
    HWND                       hwnd_ = nullptr;
    bool                       hasSharedDeviceRef_ = false;
    DWORD                      targetThreadId_ = 0;
    std::vector<D2D1_MATRIX_3X2_F> transformStack_;
    // DirectComposition objects — only populated for layered/composition
    // mode (CreateRenderTargetForLayered). Held alive so the visual tree
    // stays bound to the hwnd; destruction order is automatic via ComPtr.
    ComPtr<IUnknown>           dcompDevice_;
    ComPtr<IUnknown>           dcompTarget_;
    ComPtr<IUnknown>           dcompVisual_;
    std::unordered_map<ColorKey, ComPtr<ID2D1SolidColorBrush>, ColorKeyHash> brushCache_;
    std::unordered_map<TextFormatKey, ComPtr<IDWriteTextFormat>, TextFormatKeyHash> textFormatCache_;
    /* SVG 描边图标共用一个 round-cap/round-join 风格，避免锐角处出尖刺 */
    ComPtr<ID2D1StrokeStyle> roundStrokeStyle_;
    ID2D1StrokeStyle* GetRoundStrokeStyle();

    ComPtr<ID2D1SolidColorBrush> GetBrush(const D2D1_COLOR_F& color);
    ComPtr<IDWriteTextFormat> GetTextFormat(float fontSize, const wchar_t* family = nullptr,
                                            DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL);
    ComPtr<ID2D1Bitmap> GetCachedImageBitmap(ResourceKey key);
    void PruneImageBitmapCacheTo(const std::vector<ImageRef>& imageRefs);
    void DebugAssertTargetThread(const char* op) const;
    DisplayListRecorder* ActiveDisplayListRecorder() const {
        return displayListRecorderPauseDepth_ == 0 ? displayListRecorder_ : nullptr;
    }

    // ---- Per-window font / render 状态 (since 1.3.0) ----
    // 空串 = 跟随 theme:: 全局
    std::wstring          defaultFontOverride_;
    std::wstring          latinFontOverride_;
    std::wstring          cjkFontOverride_;
    bool                  hasRenderModeOverride_ = false;
    theme::TextRenderMode renderModeOverride_ = theme::TextRenderMode::Smooth;
    DisplayListRecorder*  displayListRecorder_ = nullptr;
    int                   displayListRecorderPauseDepth_ = 0;
    std::unordered_map<ResourceKey, ComPtr<ID2D1Bitmap>, ResourceKeyHash> imageBitmapCache_;

    // 根据当前 latin/cjk 设置构造 IDWriteFontFallback；如果两个都空则返回 nullptr。
    // 构造后由 Apply... 在 TextFormat3 上 SetFontFallback。
    ComPtr<IDWriteFontFallback> fontFallback_;
    void RebuildFontFallback();
};

} // namespace ui
