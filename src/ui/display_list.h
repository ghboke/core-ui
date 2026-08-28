#pragma once

#include "render_handles.h"

#include <d2d1.h>
#include <dwrite.h>
#include <cstdint>
#include <string>
#include <vector>

namespace ui {

enum class DrawCommandType : uint8_t {
    Clear,
    PushClip,
    PushClipAliased,
    PopClip,
    PushRoundedClip,
    PopRoundedClip,
    PushOpacity,
    PopOpacity,
    PushTransform,
    PopTransform,
    FillRect,
    DrawRect,
    FillRoundedRect,
    DrawRoundedRect,
    DrawBlurredRoundedRect,
    DrawLine,
    DrawText,
    DrawImage,
    FillImagePattern,
    FillGradient,
    DrawSvgIcon,
    DrawSvgDocument,
    DrawSvgTextRuns,
    DrawBackdropBlur,
    /* 任意四边形填充。FillRect + PushTransform 只能表达平行四边形 (仿射变换
     * 保持平行性), 梯形/透视四边形必须走真几何填充。新值一律追加在末尾 —
     * 中间插值会打乱既有编号。 */
    FillQuad,
};

enum class ImageSampling : uint8_t {
    Nearest,
    Linear,
    HighQualityCubic,
    // Clamp 变体: 越界采样重复边缘像素 (D2D1_EXTEND_MODE_CLAMP), 而不是
    // DrawBitmap 默认的透明边界。分块瓦片 (gh_img tile) 放大绘制必须用
    // clamp, 否则每块瓦片边缘插值淡出 → 拼缝处一条半透明缝线。
    LinearClamp,
    HighQualityCubicClamp,
};

struct TextStyle {
    D2D1_COLOR_F color = D2D1::ColorF(0, 0, 0, 1);
    float font_size = 14.0f;
    std::wstring font_family;
    int alignment = 0;
    int paragraph_alignment = 0;
    int weight = 400;
    bool word_wrap = false;
};

struct GradientStopRef {
    float position = -1.0f;
    D2D1_COLOR_F color = D2D1::ColorF(0, 0, 0, 1);
};

struct GradientRef {
    bool radial = false;
    float angle_deg = 180.0f;
    float cx_pct = 50.0f;
    float cy_pct = 50.0f;
    float radius_pct = 50.0f;
    float tile_w = -1.0f;
    float tile_h = -1.0f;
    float pos_x = 0.0f;
    float pos_y = 0.0f;
    std::vector<GradientStopRef> stops;
};

struct ImageRef {
    ResourceKey key;
    int width = 0;
    int height = 0;
    int stride = 0;
    PixelFormat format = PixelFormat::BgraPremul;
};

struct SvgPathLayerRef {
    std::vector<std::string> path_data;
    float opacity = 1.0f;
    float stroke_width = 0.0f;
    D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Identity();
};

struct SvgGradientStop {
    float offset = 0.0f;
    D2D1_COLOR_F color = D2D1::ColorF(0, 0, 0, 1);
};

struct SvgTextGradient {
    bool radial = false;
    bool userSpace = false;
    D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Identity();
    float x1 = 0, y1 = 0, x2 = 1, y2 = 0;
    float cx = 0.5f, cy = 0.5f, r = 0.5f;
    std::vector<SvgGradientStop> stops;
};

struct SvgTextRun {
    std::wstring text;
    D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Identity();
    float x = 0.0f;
    float y = 0.0f;
    float fontSize = 16.0f;
    std::wstring fontFamily;
    DWRITE_FONT_WEIGHT fontWeight = DWRITE_FONT_WEIGHT_NORMAL;
    D2D1_COLOR_F color = D2D1::ColorF(D2D1::ColorF::Black);
    float opacity = 1.0f;
    bool hasGradient = false;
    SvgTextGradient gradient;
    int anchor = 0;
    float maxWidth = 0.0f;
    bool block = false;
};

struct SvgTextRunListRef {
    std::vector<SvgTextRun> runs;
    D2D1_MATRIX_3X2_F base_transform = D2D1::Matrix3x2F::Identity();
};

struct SvgIconRef {
    float view_box_w = 24.0f;
    float view_box_h = 24.0f;
    std::vector<std::string> path_data;
    std::vector<SvgPathLayerRef> layers;
};

/* 一份 SVG 被拆成的有序绘制步骤 (见 ui/svg_d2d_segments.h)。
 *
 * D2D 的 ID2D1SvgDocument 完全不支持 <filter>, 且一份文档只能整张
 * DrawSvgDocument —— 没有元素级合成的钩子。所以带 filter 的 SVG 要按绘制顺序
 * 切成若干子文档逐段画, 轮到带 filter 的元素时改走离屏 + effect。
 *
 * **steps 的顺序就是 z 序**: 早前的实现是"整张基础文档 + 所有阴影层画在最后",
 * 那样带 filter 的元素会盖住文档顺序在它之后的全部内容 (下游 GuoheView 的
 * pikaq.svg: 身体椭圆盖掉了肚皮、眼睛、嘴)。不要再把 shadow/cover 从这个序列里
 * 抽出来单独批处理。 */
struct SvgDocumentRef {
    struct Step {
        // 本段内容。带 filter 的步里只有那一个元素 (祖先链保留)。
        std::string xml;
        // 非空 = 本步先画阴影: 把 shadow_xml 录进离屏、高斯模糊、按 dx/dy 偏移画出,
        // 然后才画 xml 本体。阴影必须紧贴它自己这一步, 不能推迟。
        std::string shadow_xml;
        float dx = 0.0f;
        float dy = 0.0f;
        float std_deviation = 0.0f;
    };

    std::vector<Step> steps;
    float viewport_w = 0.0f;
    float viewport_h = 0.0f;
};

/* 任意四边形的 4 顶点 (顺序 TL,TR,BR,BL, 亦即沿边环绕)。放侧池而非直接塞
 * DrawCommand —— 32 字节乘以每条命令太浪费, 跟 text_pool / image_refs 一样
 * 走"侧池 + 索引"。 */
struct QuadRef {
    D2D1_POINT_2F pts[4] = {};
};

struct DrawCommand {
    DrawCommandType type = DrawCommandType::FillRect;
    D2D1_RECT_F rect = {};
    D2D1_POINT_2F p0 = {};
    D2D1_POINT_2F p1 = {};
    D2D1_COLOR_F color = D2D1::ColorF(0, 0, 0, 1);
    float stroke_width = 1.0f;
    float radius_x = 0.0f;
    float radius_y = 0.0f;
    float blur_radius = 0.0f;
    float opacity = 1.0f;
    D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Identity();
    uint32_t text_index = UINT32_MAX;
    uint32_t image_ref_index = UINT32_MAX;
    uint32_t gradient_ref_index = UINT32_MAX;
    uint32_t svg_ref_index = UINT32_MAX;
    uint32_t svg_document_ref_index = UINT32_MAX;
    uint32_t svg_text_ref_index = UINT32_MAX;
    uint32_t quad_index = UINT32_MAX;
    TextStyle text_style;
    ImageSampling sampling = ImageSampling::Linear;
};

class DisplayList {
public:
    DisplayList() = default;
    ~DisplayList();
    DisplayList(const DisplayList&) = delete;
    DisplayList& operator=(const DisplayList&) = delete;
    DisplayList(DisplayList&& other) noexcept;
    DisplayList& operator=(DisplayList&& other) noexcept;

    uint64_t window_id = 0;
    uint64_t generation = 0;
    int width_px = 0;
    int height_px = 0;
    float dpi_scale = 1.0f;

    std::vector<DrawCommand> commands;
    std::vector<std::wstring> text_pool;
    std::vector<ImageRef> image_refs;
    std::vector<GradientRef> gradient_refs;
    std::vector<SvgIconRef> svg_icon_refs;
    std::vector<SvgDocumentRef> svg_document_refs;
    std::vector<SvgTextRunListRef> svg_text_refs;
    std::vector<QuadRef> quad_refs;

    bool Empty() const { return commands.empty(); }
    void Clear();
    void AddOwnedResource(ResourceKey key);

private:
    void ReleaseOwnedResources();

    std::vector<ResourceKey> owned_resources_;
};

class DisplayListRecorder {
public:
    DisplayListRecorder() = default;
    explicit DisplayListRecorder(DisplayList base);

    void Reset(DisplayList base = {});
    void Clear(D2D1_COLOR_F color);
    void PushClip(D2D1_RECT_F rect);
    // aliased 裁剪: 边缘不做 AA。相邻 clip 矩形拼接绘制同一内容时必须用
    // 这个, 否则共享边界两侧各一条部分覆盖边 → 拼缝半透明缝线。
    void PushClipAliased(D2D1_RECT_F rect);
    void PopClip();
    void PushRoundedClip(D2D1_RECT_F rect, float rx, float ry);
    void PopRoundedClip();
    void PushOpacity(float opacity, D2D1_RECT_F bounds);
    void PopOpacity();
    void PushTransform(D2D1_MATRIX_3X2_F transform);
    void PopTransform();
    void FillRect(D2D1_RECT_F rect, D2D1_COLOR_F color);
    void FillQuad(const D2D1_POINT_2F pts[4], D2D1_COLOR_F color);
    void DrawRect(D2D1_RECT_F rect, D2D1_COLOR_F color, float strokeWidth);
    void FillRoundedRect(D2D1_RECT_F rect, float rx, float ry, D2D1_COLOR_F color);
    void DrawRoundedRect(D2D1_RECT_F rect, float rx, float ry, D2D1_COLOR_F color, float strokeWidth);
    void DrawBlurredRoundedRect(D2D1_RECT_F rect, float rx, float ry,
                                float blurRadius, D2D1_COLOR_F color);
    void DrawLine(D2D1_POINT_2F p0, D2D1_POINT_2F p1, D2D1_COLOR_F color, float strokeWidth);
    void DrawText(std::wstring text, D2D1_RECT_F rect, TextStyle style);
    void DrawImage(ImageRef image, D2D1_RECT_F dst, ImageSampling sampling, float opacity = 1.0f);
    void FillImagePattern(ImageRef image, D2D1_RECT_F rect);
    void FillGradient(GradientRef gradient, D2D1_RECT_F rect, float radius);
    void DrawSvgIcon(SvgIconRef icon, D2D1_RECT_F dst, D2D1_COLOR_F color);
    void DrawSvgDocument(SvgDocumentRef doc, D2D1_MATRIX_3X2_F transform);
    void DrawSvgTextRuns(SvgTextRunListRef textRuns);
    void DrawBackdropBlur(D2D1_RECT_F rect, float radius, float blurRadius);
    void OwnResource(ResourceKey key);

    DisplayList Finish();

private:
    uint32_t AddText(std::wstring text);
    uint32_t AddImageRef(ImageRef image);
    uint32_t AddGradientRef(GradientRef gradient);
    uint32_t AddSvgIconRef(SvgIconRef icon);
    uint32_t AddSvgDocumentRef(SvgDocumentRef doc);
    uint32_t AddSvgTextRunRef(SvgTextRunListRef textRuns);
    DrawCommand& AddCommand(DrawCommandType type);

    DisplayList list_;
};

} // namespace ui
