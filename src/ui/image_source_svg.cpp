#include "image_source.h"
#include "renderer.h"
#include "svg_style_inliner.h"
#include <d2d1_3.h>
#include <shlwapi.h>
#include <windows.h>
#include <vector>
/* shlwapi 由 CMakeLists 的 UI_CORE_SYSTEM_LIBS 统一链接，不在这里 #pragma comment
 * 因为 MinGW 不识别该 pragma（无害，但保持风格一致）。*/

namespace ui {

// Renderer 需要暴露一个 ID2D1DeviceContext5*（支持检测）
// 见 renderer.h 的增量：ID2D1DeviceContext5* RT5() { return ctx5_.Get(); }

// ========= 原生路径：ID2D1SvgDocument =========

class SvgSourceNative : public IImageSource {
public:
    SvgSourceNative(ComPtr<ID2D1SvgDocument> doc, std::string xml, int w, int h,
                    std::vector<SvgTextRun> textRuns)
        : doc_(std::move(doc)), xml_(std::move(xml)),
          w_(w), h_(h), textRuns_(std::move(textRuns)) {}

    int  Width()  const override { return w_; }
    int  Height() const override { return h_; }
    ImageCaps Caps() const override {
        ImageCaps c; c.vector = true; c.alpha = true; return c;
    }
    const char* TypeName() const override { return "SvgSourceNative"; }

    void Draw(Renderer& r, const ImageDrawContext& ctx) override {
        float drawW = ctx.dest.right  - ctx.dest.left;
        float drawH = ctx.dest.bottom - ctx.dest.top;
        if (drawW <= 0 || drawH <= 0 || w_ <= 0 || h_ <= 0) return;

        /* 关键：SVG viewport 是 SVG 内部 user-unit 的参考坐标（用于解析 % 值），
         * **不是**最终渲染像素尺寸。真正控制绘制大小用 SetTransform scale。
         * 这样 D2D 按矢量光栅化到任意缩放级别都清晰。
         *
         * viewport 设为 SVG 原生宽高（w_, h_），transform 里做 scale 到 drawW/drawH。
         */
        float sx = drawW / (float)w_;
        float sy = drawH / (float)h_;
        float cx = (ctx.dest.left + ctx.dest.right) / 2.0f;
        float cy = (ctx.dest.top  + ctx.dest.bottom) / 2.0f;

        /* 矩阵顺序（行向量 P * M）：
         *   Scale(sx, sy) 把 (0,0)-(w_,h_) 映射到 (0,0)-(drawW, drawH)
         *   Translation 平移到 dest 左上角
         *   Rotation 绕 dest 中心旋转
         *   * old 叠加外层 transform
         */
        auto xf =
            D2D1::Matrix3x2F::Scale(sx, sy) *
            D2D1::Matrix3x2F::Translation(ctx.dest.left, ctx.dest.top) *
            D2D1::Matrix3x2F::Rotation((float)ctx.rotation,
                                        D2D1::Point2F(cx, cy));
        r.RecordSvgDocument(xml_, (float)w_, (float)h_, xf);

        auto* ctx5 = r.RT5();
        if (!ctx5 || !doc_) return;

        D2D1_MATRIX_3X2_F old;
        ctx5->GetTransform(&old);
        D2D1_MATRIX_3X2_F drawXf = xf * old;
        ctx5->SetTransform(drawXf);

        doc_->SetViewportSize(D2D1::SizeF((float)w_, (float)h_));
        ctx5->DrawSvgDocument(doc_.Get());
        /* L75: D2D ID2D1SvgDocument 不渲染 <text>/<foreignObject> 文字 (shapes-only)
         * → DirectWrite 叠加补渲, 用同一个 xf 变换跟形状对齐. */
        if (!textRuns_.empty()) r.DrawSvgTextRuns(textRuns_, drawXf);
        ctx5->SetTransform(old);
    }

private:
    ComPtr<ID2D1SvgDocument> doc_;
    std::string xml_;
    int w_, h_;
    std::vector<SvgTextRun> textRuns_;   // L75: D2D 不画文字, 这里 DirectWrite 叠加
};

// ========= Fallback：用 Renderer::ParseSvgIcon，只支持 SVG 子集 =========

class SvgSourceFallback : public ISvgFallbackSource {
public:
    SvgSourceFallback(SvgIcon icon) : icon_(std::move(icon)) {
        w_ = (int)icon_.viewBoxW;
        h_ = (int)icon_.viewBoxH;
    }

    int  Width()  const override { return w_; }
    int  Height() const override { return h_; }
    ImageCaps Caps() const override {
        ImageCaps c; c.vector = true; c.alpha = true; return c;
    }
    const char* TypeName() const override { return "SvgSourceFallback"; }
    const SvgIcon& Icon() const override { return icon_; }

    void Draw(Renderer& r, const ImageDrawContext& ctx) override {
        // SvgIcon 原设计是 icon，染色统一。看图场景我们传白色让它保留原路径 fill。
        // 但 ParseSvgIcon 的 layers 里已有 path 自身的颜色信息时可按那个画。
        // 这里简化：用 DrawSvgIcon 的默认纯色绘制（这是 fallback，功能本来就打折扣）。
        auto color = D2D1::ColorF(D2D1::ColorF::Black);
        r.DrawSvgIcon(icon_, ctx.dest, color);
        /* L75: 补文字 (同 DrawSvgIcon 的 viewBox→dest fit-contain 变换). */
        if (!icon_.textRuns.empty()) {
            float destW = ctx.dest.right - ctx.dest.left;
            float destH = ctx.dest.bottom - ctx.dest.top;
            if (destW > 0 && destH > 0 && icon_.viewBoxW > 0 && icon_.viewBoxH > 0) {
                float sx = destW / icon_.viewBoxW, sy = destH / icon_.viewBoxH;
                float scale = sx < sy ? sx : sy;
                float offX = ctx.dest.left + (destW - icon_.viewBoxW * scale) / 2.0f;
                float offY = ctx.dest.top  + (destH - icon_.viewBoxH * scale) / 2.0f;
                auto xf = D2D1::Matrix3x2F::Scale(scale, scale) *
                          D2D1::Matrix3x2F::Translation(offX, offY);
                r.DrawSvgTextRuns(icon_.textRuns, xf);
            }
        }
    }

private:
    SvgIcon icon_;
    int w_ = 0, h_ = 0;
};

// ========= 工厂 =========

std::unique_ptr<IImageSource>
CreateSvgSourceFromFile(const std::wstring& path, Renderer& r) {
    /* L48 — 读一次文件 + 预处理 <style>+class 规则到 inline style. 处理后
     * 的 xml 同时喂 D2D 原生路径和 SvgIcon fallback, 避免二次读盘 + 让 fallback
     * 也享受 style 内联 (将来 ParseSvgIcon 支持 class 时一致). */
    std::string xml = LoadSvgWithInlinedStyles(path);
    if (xml.empty()) return nullptr;

    /* L292: D2D 认不出 rgb()/rgba()/transparent 这几种 CSS 写法, 解析失败后
     * fill 回落到初始值黑色 (fill="transparent" 的整幅背景就会变成黑底)。
     * 跟 gh_img_view 的主视图走同一道归一化。 */
    xml = NormalizeSvgPaintColorsForD2D(xml);

    /* L121: <text>/<foreignObject> → <path> 字形轮廓内联回 DOM, 让 D2D 按文档
     * 顺序统一渲染 (z 序正确). 转换后已无 <text>, 原生路径不再需要 DirectWrite
     * 叠加, fallback 的 ParseSvgIcon 也直接把文字当形状画 (一并修好). */
    xml = r.SvgInlineTextAsPaths(xml);

    // 先试原生路径（Win10 1607+）
    if (auto* ctx5 = r.RT5()) {
        ComPtr<IStream> stream;
        stream.Attach(SHCreateMemStream(
            reinterpret_cast<const BYTE*>(xml.data()),
            static_cast<UINT>(xml.size())));
        if (stream) {
            ComPtr<ID2D1SvgDocument> doc;
            // 初始 viewport 随意，Draw 时会改
            if (SUCCEEDED(ctx5->CreateSvgDocument(
                    stream.Get(), D2D1::SizeF(1024, 1024), &doc)))
            {
                // 从 root 的 viewBox 读真实尺寸
                ComPtr<ID2D1SvgElement> root;
                doc->GetRoot(&root);
                D2D1_SVG_VIEWBOX vb{};
                int w = 1024, h = 1024;
                if (root && SUCCEEDED(root->GetAttributeValue(
                        L"viewBox",
                        D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX,
                        &vb, sizeof(vb))) && vb.width > 0 && vb.height > 0)
                {
                    w = (int)vb.width; h = (int)vb.height;
                } else if (root) {
                    D2D1_SVG_LENGTH lw{}, lh{};
                    if (SUCCEEDED(root->GetAttributeValue(
                            L"width",
                            D2D1_SVG_ATTRIBUTE_POD_TYPE_LENGTH, &lw, sizeof(lw)))
                        && lw.value > 0) w = (int)lw.value;
                    if (SUCCEEDED(root->GetAttributeValue(
                            L"height",
                            D2D1_SVG_ATTRIBUTE_POD_TYPE_LENGTH, &lh, sizeof(lh)))
                        && lh.value > 0) h = (int)lh.value;
                }
                /* L170: 覆写 root width/height = viewBox (w/h)。原 width/height
                 * 若是物理单位 (mm/cm/in) D2D 按固有尺寸渲染, 跟 Draw 基于 w/h
                 * 的 scale 不一致 → 内容缩放错位。统一成 viewBox 数值消歧义。 */
                if (root) {
                    const D2D1_SVG_LENGTH ow{(float)w, D2D1_SVG_LENGTH_UNITS_NUMBER};
                    const D2D1_SVG_LENGTH oh{(float)h, D2D1_SVG_LENGTH_UNITS_NUMBER};
                    root->SetAttributeValue(L"width",  ow);
                    root->SetAttributeValue(L"height", oh);
                }
                /* L121: 文字已内联成 <path>, D2D 一把全画 → 无需 textRuns 叠加. */
                return std::make_unique<SvgSourceNative>(
                    doc, std::move(xml), w, h, std::vector<SvgTextRun>{});
            }
        }
    }

    // Fallback：复用预处理后的 xml → ParseSvgIcon
    SvgIcon icon = r.ParseSvgIcon(xml);
    if (!icon.valid) return nullptr;
    return std::make_unique<SvgSourceFallback>(std::move(icon));
}

} // namespace ui
