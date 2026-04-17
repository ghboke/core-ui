#pragma once
#include "widget.h"
#include "controls.h"
#include <initializer_list>

namespace ui {

// ---- Layout containers ----

inline WidgetPtr VBox(std::initializer_list<WidgetPtr> children) {
    auto w = std::make_shared<VBoxWidget>();
    for (auto& c : children) w->AddChild(c);
    return w;
}

inline WidgetPtr HBox(std::initializer_list<WidgetPtr> children) {
    auto w = std::make_shared<HBoxWidget>();
    for (auto& c : children) w->AddChild(c);
    return w;
}

inline WidgetPtr Spacer(float size = 0) {
    return std::make_shared<SpacerWidget>(size);
}

inline WidgetPtr Panel(const D2D1_COLOR_F& bg = {0,0,0,0}) {
    return std::make_shared<PanelWidget>(bg);
}

inline WidgetPtr Panel(const D2D1_COLOR_F& bg, std::initializer_list<WidgetPtr> children) {
    auto w = std::make_shared<PanelWidget>(bg);
    for (auto& c : children) w->AddChild(c);
    return w;
}

// ---- Basic controls ----

inline WidgetPtr Label(const std::wstring& text) {
    return std::make_shared<LabelWidget>(text);
}

inline WidgetPtr Button(const std::wstring& text) {
    return std::make_shared<ButtonWidget>(text);
}

inline WidgetPtr CheckBox(const std::wstring& text) {
    return std::make_shared<CheckBoxWidget>(text);
}

inline WidgetPtr Slider(float min, float max, float value) {
    return std::make_shared<SliderWidget>(min, max, value);
}

inline WidgetPtr Separator() {
    return std::make_shared<SeparatorWidget>();
}

inline WidgetPtr VSeparator() {
    return std::make_shared<SeparatorWidget>(true);
}

// ---- Advanced controls ----

inline WidgetPtr TextInput(const std::wstring& placeholder = L"") {
    return std::make_shared<TextInputWidget>(placeholder);
}

inline WidgetPtr TextArea(const std::wstring& placeholder = L"") {
    return std::make_shared<TextAreaWidget>(placeholder);
}

inline WidgetPtr ComboBox(std::initializer_list<std::wstring> items) {
    return std::make_shared<ComboBoxWidget>(std::vector<std::wstring>(items));
}

inline WidgetPtr ComboBox(std::vector<std::wstring> items) {
    return std::make_shared<ComboBoxWidget>(std::move(items));
}

inline WidgetPtr RadioButton(const std::wstring& text, const std::string& group) {
    return std::make_shared<RadioButtonWidget>(text, group);
}

inline WidgetPtr Toggle(const std::wstring& text = L"") {
    return std::make_shared<ToggleWidget>(text);
}

inline WidgetPtr ProgressBar(float min, float max, float value) {
    return std::make_shared<ProgressBarWidget>(min, max, value);
}

// ---- Container controls ----

// TabControl — use ->AddTab() after creation
inline std::shared_ptr<TabControlWidget> TabControl() {
    return std::make_shared<TabControlWidget>();
}

// ScrollView — use ->SetContent() after creation
inline std::shared_ptr<ScrollViewWidget> ScrollView() {
    return std::make_shared<ScrollViewWidget>();
}

// ScrollView with content
inline std::shared_ptr<ScrollViewWidget> ScrollView(WidgetPtr content) {
    auto sv = std::make_shared<ScrollViewWidget>();
    sv->SetContent(content);
    return sv;
}

// ---- Image view ----

inline std::shared_ptr<ImageViewWidget> ImageView() {
    return std::make_shared<ImageViewWidget>();
}

// ---- Icon button ----

inline WidgetPtr IconButton(const std::string& svg, bool ghost = false) {
    return std::make_shared<IconButtonWidget>(svg, ghost);
}

} // namespace ui
