#include "display_list.h"

#include "resource_store.h"

#include <algorithm>
#include <utility>

namespace ui {

DisplayList::~DisplayList() {
    ReleaseOwnedResources();
}

DisplayList::DisplayList(DisplayList&& other) noexcept
    : window_id(other.window_id),
      generation(other.generation),
      width_px(other.width_px),
      height_px(other.height_px),
      dpi_scale(other.dpi_scale),
      commands(std::move(other.commands)),
      text_pool(std::move(other.text_pool)),
      image_refs(std::move(other.image_refs)),
      gradient_refs(std::move(other.gradient_refs)),
      svg_icon_refs(std::move(other.svg_icon_refs)),
      svg_document_refs(std::move(other.svg_document_refs)),
      svg_text_refs(std::move(other.svg_text_refs)),
      quad_refs(std::move(other.quad_refs)),
      owned_resources_(std::move(other.owned_resources_)) {
    other.owned_resources_.clear();
}

DisplayList& DisplayList::operator=(DisplayList&& other) noexcept {
    if (this == &other) return *this;
    ReleaseOwnedResources();
    window_id = other.window_id;
    generation = other.generation;
    width_px = other.width_px;
    height_px = other.height_px;
    dpi_scale = other.dpi_scale;
    commands = std::move(other.commands);
    text_pool = std::move(other.text_pool);
    image_refs = std::move(other.image_refs);
    gradient_refs = std::move(other.gradient_refs);
    svg_icon_refs = std::move(other.svg_icon_refs);
    svg_document_refs = std::move(other.svg_document_refs);
    svg_text_refs = std::move(other.svg_text_refs);
    quad_refs = std::move(other.quad_refs);
    owned_resources_ = std::move(other.owned_resources_);
    other.owned_resources_.clear();
    return *this;
}

void DisplayList::Clear() {
    commands.clear();
    text_pool.clear();
    image_refs.clear();
    gradient_refs.clear();
    svg_icon_refs.clear();
    svg_document_refs.clear();
    svg_text_refs.clear();
    quad_refs.clear();
    ReleaseOwnedResources();
}

void DisplayList::AddOwnedResource(ResourceKey key) {
    if (!key.IsValid()) return;
    const auto it = std::find(owned_resources_.begin(), owned_resources_.end(), key);
    if (it != owned_resources_.end()) return;
    owned_resources_.push_back(key);
}

void DisplayList::ReleaseOwnedResources() {
    for (const auto& key : owned_resources_) {
        GlobalResourceStore().Remove(key);
    }
    owned_resources_.clear();
}

DisplayListRecorder::DisplayListRecorder(DisplayList base)
    : list_(std::move(base)) {}

void DisplayListRecorder::Reset(DisplayList base) {
    list_ = std::move(base);
}

void DisplayListRecorder::Clear(D2D1_COLOR_F color) {
    auto& cmd = AddCommand(DrawCommandType::Clear);
    cmd.color = color;
}

void DisplayListRecorder::PushClip(D2D1_RECT_F rect) {
    auto& cmd = AddCommand(DrawCommandType::PushClip);
    cmd.rect = rect;
}

void DisplayListRecorder::PushClipAliased(D2D1_RECT_F rect) {
    auto& cmd = AddCommand(DrawCommandType::PushClipAliased);
    cmd.rect = rect;
}

void DisplayListRecorder::PopClip() {
    AddCommand(DrawCommandType::PopClip);
}

void DisplayListRecorder::PushRoundedClip(D2D1_RECT_F rect, float rx, float ry) {
    auto& cmd = AddCommand(DrawCommandType::PushRoundedClip);
    cmd.rect = rect;
    cmd.radius_x = rx;
    cmd.radius_y = ry;
}

void DisplayListRecorder::PopRoundedClip() {
    AddCommand(DrawCommandType::PopRoundedClip);
}

void DisplayListRecorder::PushOpacity(float opacity, D2D1_RECT_F bounds) {
    auto& cmd = AddCommand(DrawCommandType::PushOpacity);
    cmd.rect = bounds;
    cmd.opacity = opacity;
}

void DisplayListRecorder::PopOpacity() {
    AddCommand(DrawCommandType::PopOpacity);
}

void DisplayListRecorder::PushTransform(D2D1_MATRIX_3X2_F transform) {
    auto& cmd = AddCommand(DrawCommandType::PushTransform);
    cmd.transform = transform;
}

void DisplayListRecorder::PopTransform() {
    AddCommand(DrawCommandType::PopTransform);
}

void DisplayListRecorder::FillRect(D2D1_RECT_F rect, D2D1_COLOR_F color) {
    auto& cmd = AddCommand(DrawCommandType::FillRect);
    cmd.rect = rect;
    cmd.color = color;
}

void DisplayListRecorder::FillQuad(const D2D1_POINT_2F pts[4], D2D1_COLOR_F color) {
    QuadRef quad;
    for (int i = 0; i < 4; ++i) quad.pts[i] = pts[i];
    list_.quad_refs.push_back(quad);
    auto& cmd = AddCommand(DrawCommandType::FillQuad);
    cmd.quad_index = (uint32_t)(list_.quad_refs.size() - 1);
    cmd.color = color;
}

void DisplayListRecorder::DrawRect(D2D1_RECT_F rect, D2D1_COLOR_F color, float strokeWidth) {
    auto& cmd = AddCommand(DrawCommandType::DrawRect);
    cmd.rect = rect;
    cmd.color = color;
    cmd.stroke_width = strokeWidth;
}

void DisplayListRecorder::FillRoundedRect(D2D1_RECT_F rect, float rx, float ry, D2D1_COLOR_F color) {
    auto& cmd = AddCommand(DrawCommandType::FillRoundedRect);
    cmd.rect = rect;
    cmd.radius_x = rx;
    cmd.radius_y = ry;
    cmd.color = color;
}

void DisplayListRecorder::DrawRoundedRect(D2D1_RECT_F rect, float rx, float ry,
                                          D2D1_COLOR_F color, float strokeWidth) {
    auto& cmd = AddCommand(DrawCommandType::DrawRoundedRect);
    cmd.rect = rect;
    cmd.radius_x = rx;
    cmd.radius_y = ry;
    cmd.color = color;
    cmd.stroke_width = strokeWidth;
}

void DisplayListRecorder::DrawBlurredRoundedRect(D2D1_RECT_F rect, float rx, float ry,
                                                 float blurRadius, D2D1_COLOR_F color) {
    auto& cmd = AddCommand(DrawCommandType::DrawBlurredRoundedRect);
    cmd.rect = rect;
    cmd.radius_x = rx;
    cmd.radius_y = ry;
    cmd.blur_radius = blurRadius;
    cmd.color = color;
}

void DisplayListRecorder::DrawLine(D2D1_POINT_2F p0, D2D1_POINT_2F p1,
                                   D2D1_COLOR_F color, float strokeWidth) {
    auto& cmd = AddCommand(DrawCommandType::DrawLine);
    cmd.p0 = p0;
    cmd.p1 = p1;
    cmd.color = color;
    cmd.stroke_width = strokeWidth;
}

void DisplayListRecorder::DrawText(std::wstring text, D2D1_RECT_F rect, TextStyle style) {
    auto& cmd = AddCommand(DrawCommandType::DrawText);
    cmd.rect = rect;
    cmd.text_index = AddText(std::move(text));
    cmd.text_style = style;
}

void DisplayListRecorder::DrawImage(ImageRef image, D2D1_RECT_F dst,
                                    ImageSampling sampling, float opacity) {
    auto& cmd = AddCommand(DrawCommandType::DrawImage);
    cmd.rect = dst;
    cmd.image_ref_index = AddImageRef(image);
    cmd.sampling = sampling;
    cmd.opacity = opacity;
}

void DisplayListRecorder::FillImagePattern(ImageRef image, D2D1_RECT_F rect) {
    auto& cmd = AddCommand(DrawCommandType::FillImagePattern);
    cmd.rect = rect;
    cmd.image_ref_index = AddImageRef(image);
    cmd.sampling = ImageSampling::Nearest;
}

void DisplayListRecorder::FillGradient(GradientRef gradient, D2D1_RECT_F rect, float radius) {
    auto& cmd = AddCommand(DrawCommandType::FillGradient);
    cmd.rect = rect;
    cmd.radius_x = radius;
    cmd.gradient_ref_index = AddGradientRef(std::move(gradient));
}

void DisplayListRecorder::DrawSvgIcon(SvgIconRef icon, D2D1_RECT_F dst, D2D1_COLOR_F color) {
    auto& cmd = AddCommand(DrawCommandType::DrawSvgIcon);
    cmd.rect = dst;
    cmd.color = color;
    cmd.svg_ref_index = AddSvgIconRef(std::move(icon));
}

void DisplayListRecorder::DrawSvgDocument(SvgDocumentRef doc, D2D1_MATRIX_3X2_F transform) {
    auto& cmd = AddCommand(DrawCommandType::DrawSvgDocument);
    cmd.transform = transform;
    cmd.svg_document_ref_index = AddSvgDocumentRef(std::move(doc));
}

void DisplayListRecorder::DrawSvgTextRuns(SvgTextRunListRef textRuns) {
    auto& cmd = AddCommand(DrawCommandType::DrawSvgTextRuns);
    cmd.svg_text_ref_index = AddSvgTextRunRef(std::move(textRuns));
}

void DisplayListRecorder::DrawBackdropBlur(D2D1_RECT_F rect, float radius, float blurRadius) {
    auto& cmd = AddCommand(DrawCommandType::DrawBackdropBlur);
    cmd.rect = rect;
    cmd.radius_x = radius;
    cmd.blur_radius = blurRadius;
}

void DisplayListRecorder::OwnResource(ResourceKey key) {
    list_.AddOwnedResource(key);
}

DisplayList DisplayListRecorder::Finish() {
    DisplayList out = std::move(list_);
    list_ = {};
    return out;
}

uint32_t DisplayListRecorder::AddText(std::wstring text) {
    const uint32_t index = static_cast<uint32_t>(list_.text_pool.size());
    list_.text_pool.push_back(std::move(text));
    return index;
}

uint32_t DisplayListRecorder::AddImageRef(ImageRef image) {
    const uint32_t index = static_cast<uint32_t>(list_.image_refs.size());
    list_.image_refs.push_back(image);
    return index;
}

uint32_t DisplayListRecorder::AddGradientRef(GradientRef gradient) {
    const uint32_t index = static_cast<uint32_t>(list_.gradient_refs.size());
    list_.gradient_refs.push_back(std::move(gradient));
    return index;
}

uint32_t DisplayListRecorder::AddSvgIconRef(SvgIconRef icon) {
    const uint32_t index = static_cast<uint32_t>(list_.svg_icon_refs.size());
    list_.svg_icon_refs.push_back(std::move(icon));
    return index;
}

uint32_t DisplayListRecorder::AddSvgDocumentRef(SvgDocumentRef doc) {
    const uint32_t index = static_cast<uint32_t>(list_.svg_document_refs.size());
    list_.svg_document_refs.push_back(std::move(doc));
    return index;
}

uint32_t DisplayListRecorder::AddSvgTextRunRef(SvgTextRunListRef textRuns) {
    const uint32_t index = static_cast<uint32_t>(list_.svg_text_refs.size());
    list_.svg_text_refs.push_back(std::move(textRuns));
    return index;
}

DrawCommand& DisplayListRecorder::AddCommand(DrawCommandType type) {
    DrawCommand cmd;
    cmd.type = type;
    list_.commands.push_back(cmd);
    return list_.commands.back();
}

} // namespace ui
