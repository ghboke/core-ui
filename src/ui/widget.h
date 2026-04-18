#pragma once

#ifndef UI_API
  #if defined(UI_CORE_STATIC)
    #define UI_API
  #elif defined(UI_CORE_BUILDING)
    #define UI_API __declspec(dllexport)
  #else
    #define UI_API __declspec(dllimport)
  #endif
#endif

#include <d2d1.h>
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <cfloat>
#include <algorithm>

namespace ui {

class Renderer;
struct MouseEvent;

using WidgetPtr = std::shared_ptr<class Widget>;

// ---- Layout enums ----
enum class LayoutAlign   { Start, Center, End, Stretch };
enum class LayoutJustify { Start, Center, End, SpaceBetween, SpaceAround };

// Global viewport bounds (set by main window during layout)
inline D2D1_RECT_F& Viewport() {
    static D2D1_RECT_F vp = {};
    return vp;
}

// Global flag: show focus ring only during keyboard navigation
inline bool& ShowFocusRing() {
    static bool show = false;
    return show;
}

class UI_API Widget : public std::enable_shared_from_this<Widget> {
public:
    virtual ~Widget() = default;

    // ---- Tree ----
    void AddChild(WidgetPtr child);
    void RemoveChild(Widget* child);
    Widget* Parent() const { return parent_; }
    std::vector<WidgetPtr>& Children() { return children_; }
    const std::vector<WidgetPtr>& Children() const { return children_; }

    // ---- Geometry (computed by layout) ----
    D2D1_RECT_F rect{};

    // ---- Sizing constraints ----
    float fixedW = 0, fixedH = 0;   // 0 = auto
    float minW = 0, minH = 0;
    float maxW = FLT_MAX, maxH = FLT_MAX;
    bool  expanding = false;
    float flex = 1.0f;              // flex weight (only used when expanding=true)
    float percentW = -1, percentH = -1;  // -1 = not percentage, 0-100 = % of parent

    // ---- Absolute positioning ----
    bool positionAbsolute = false;
    float posLeft = -1, posTop = -1, posRight = -1, posBottom = -1;  // -1 = not set

    // ---- Padding (inner space) ----
    float padL = 0, padT = 0, padR = 0, padB = 0;

    // ---- Margin (outer space) ----
    float marginL = 0, marginT = 0, marginR = 0, marginB = 0;

    // ---- State ----
    bool visible = true;
    bool hitTransparent = false;  /* true 时 HitTest 只看子不返回自身，让事件穿透到下层 */
    bool dragWindow = false;      /* true 时命中该 widget 触发窗口拖动（WM_NCHITTEST → HTCAPTION） */
    bool enabled = true;
    bool hovered = false;
    bool pressed = false;
    bool focusable = false;
    bool focused_ = false;
    bool tabStop = true;    // false = skip this widget in Tab traversal
    int  tabIndex = -1;     // -1 = auto (tree order), >=0 = explicit
    int  gridColSpan = 1;   // Grid: how many columns this child spans
    int  gridRowSpan = 1;   // Grid: how many rows this child spans
    std::string id;
    std::wstring tooltip;
    std::string i18nKey;        // i18n: @key reference for text translation
    std::string tooltipI18nKey; // i18n: @key reference for tooltip translation
    std::string titleI18nKey;   // i18n: @key reference for TitleBar title translation

    // ---- Declarative transitions ----
    // Parsed from transition="opacity 200ms ease-out"
    struct TransitionSpec {
        int property;       // AnimProperty enum value
        float durationMs;
        int easing;         // EasingFunction enum value
    };
    std::vector<TransitionSpec> transitions;

    // ---- Opacity (0.0 = invisible, 1.0 = fully opaque) ----
    float opacity = 1.0f;

    // ---- Background ----
    D2D1_COLOR_F bgColor = {0, 0, 0, 0};
    std::function<D2D1_COLOR_F()> bgColorFn;  // dynamic color (e.g. theme-aware)

    // ---- State-dependent styles (from :hover, :pressed, etc.) ----
    struct StateColors {
        std::function<D2D1_COLOR_F()> hoverBg;
        std::function<D2D1_COLOR_F()> pressedBg;
        std::function<D2D1_COLOR_F()> disabledBg;
    };
    StateColors stateColors;

    // ---- Callbacks ----
    std::function<void()> onClick;
    std::function<void(bool)> onValueChanged;
    std::function<void(float)> onFloatChanged;
    std::function<void(const std::wstring&)> onTextChanged;

    // ---- DSL chaining (all return WidgetPtr for fluid nesting) ----
    WidgetPtr Width(float w)    { fixedW = w; return shared_from_this(); }
    WidgetPtr Height(float h)   { fixedH = h; return shared_from_this(); }
    WidgetPtr MinWidth(float w) { minW = w; return shared_from_this(); }
    WidgetPtr MinHeight(float h){ minH = h; return shared_from_this(); }
    WidgetPtr Size(float w, float h) { fixedW = w; fixedH = h; return shared_from_this(); }
    WidgetPtr Expand(float f=1.0f) { expanding = true; flex = f; return shared_from_this(); }
    WidgetPtr Flex(float f)     { flex = f; return shared_from_this(); }
    WidgetPtr Margin(float m)   { marginL = marginT = marginR = marginB = m; return shared_from_this(); }
    WidgetPtr Margin(float h, float v) { marginL = marginR = h; marginT = marginB = v; return shared_from_this(); }
    WidgetPtr Margin(float l, float t, float r, float b) { marginL=l; marginT=t; marginR=r; marginB=b; return shared_from_this(); }
    WidgetPtr MaxWidth(float w) { maxW = w; return shared_from_this(); }
    WidgetPtr MaxHeight(float h){ maxH = h; return shared_from_this(); }
    WidgetPtr Padding(float p)  { padL = padT = padR = padB = p; return shared_from_this(); }
    WidgetPtr Padding(float h, float v) { padL = padR = h; padT = padB = v; return shared_from_this(); }
    WidgetPtr Padding(float l, float t, float r, float b) { padL=l; padT=t; padR=r; padB=b; return shared_from_this(); }
    WidgetPtr Id(const std::string& s) { id = s; return shared_from_this(); }
    WidgetPtr BgColor(const D2D1_COLOR_F& c) { bgColor = c; return shared_from_this(); }
    WidgetPtr OnClick(std::function<void()> cb) { onClick = std::move(cb); return shared_from_this(); }

    // ---- Control-specific DSL (overridden by subclasses, base is no-op) ----
    virtual WidgetPtr FontSize(float) { return shared_from_this(); }
    virtual WidgetPtr Bold()          { return shared_from_this(); }
    virtual WidgetPtr Gap(float)      { return shared_from_this(); }
    virtual WidgetPtr TextColor(const D2D1_COLOR_F&) { return shared_from_this(); }
    virtual WidgetPtr Align(int)      { return shared_from_this(); }

    // ---- Virtual interface ----
    virtual void OnDraw(Renderer& r);
    virtual bool OnMouseMove(const MouseEvent& e);
    virtual bool OnMouseDown(const MouseEvent& e);
    virtual bool OnMouseUp(const MouseEvent& e);
    virtual bool OnMouseWheel(const MouseEvent& e);
    virtual bool OnKeyDown(int vk)   { (void)vk; return false; }
    virtual bool OnKeyChar(wchar_t c){ (void)c;  return false; }
    virtual D2D1_SIZE_F SizeHint() const { return {fixedW, fixedH}; }

    // ---- Focus ----
    bool IsFocused() const { return focused_; }
    void SetFocused(bool f) { focused_ = f; }
    void DrawFocusRing(Renderer& r);

    // ---- Layout ----
    virtual void DoLayout();

    // ---- Overlay (drawn on top of everything, e.g. dropdowns) ----
    virtual void OnDrawOverlay(Renderer&) {}

    // ---- Traversal ----
    virtual void DrawTree(Renderer& r);
    void DrawOverlays(Renderer& r);
    Widget* HitTest(float x, float y);
    Widget* FindById(const std::string& id);
    void CollectFocusable(std::vector<Widget*>& out);

    // ---- Helpers ----
    float ContentLeft()   const { return rect.left + padL; }
    float ContentTop()    const { return rect.top + padT; }
    float ContentRight()  const { return rect.right - padR; }
    float ContentBottom() const { return rect.bottom - padB; }
    float ContentWidth()  const { return std::max(0.0f, rect.right - rect.left - padL - padR); }
    float ContentHeight() const { return std::max(0.0f, rect.bottom - rect.top - padT - padB); }
    bool  Contains(float x, float y) const {
        return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
    }

protected:
    Widget* parent_ = nullptr;
    std::vector<WidgetPtr> children_;
};

// ---- Layout containers ----

class UI_API VBoxWidget : public Widget {
public:
    float gap_ = 4.0f;
    LayoutAlign   crossAlign_ = LayoutAlign::Stretch;   // horizontal alignment of children
    LayoutJustify mainJustify_ = LayoutJustify::Start;  // vertical distribution

    WidgetPtr Gap(float g) override { gap_ = g; return shared_from_this(); }
    WidgetPtr CrossAlign(LayoutAlign a)   { crossAlign_ = a; return shared_from_this(); }
    WidgetPtr MainJustify(LayoutJustify j){ mainJustify_ = j; return shared_from_this(); }
    void DoLayout() override;
    D2D1_SIZE_F SizeHint() const override;
};

class UI_API HBoxWidget : public Widget {
public:
    float gap_ = 4.0f;
    LayoutAlign   crossAlign_ = LayoutAlign::Stretch;   // vertical alignment of children
    LayoutJustify mainJustify_ = LayoutJustify::Start;  // horizontal distribution

    WidgetPtr Gap(float g) override { gap_ = g; return shared_from_this(); }
    WidgetPtr CrossAlign(LayoutAlign a)   { crossAlign_ = a; return shared_from_this(); }
    WidgetPtr MainJustify(LayoutJustify j){ mainJustify_ = j; return shared_from_this(); }
    void DoLayout() override;
    D2D1_SIZE_F SizeHint() const override;
};

class UI_API SpacerWidget : public Widget {
public:
    explicit SpacerWidget(float size = 0) {
        if (size > 0) { fixedW = size; fixedH = size; }
        else { expanding = true; }
    }
};

// ---- Grid layout ----
class UI_API GridWidget : public Widget {
public:
    int   cols_ = 2;
    float rowGap_ = 4.0f;
    float colGap_ = 4.0f;

    // Per-child layout hint: colspan/rowspan stored on child via gridColSpan/gridRowSpan

    WidgetPtr Gap(float g) override { rowGap_ = colGap_ = g; return shared_from_this(); }
    WidgetPtr Cols(int c)   { cols_ = std::max(1, c); return shared_from_this(); }
    WidgetPtr RowGap(float g){ rowGap_ = g; return shared_from_this(); }
    WidgetPtr ColGap(float g){ colGap_ = g; return shared_from_this(); }

    void DoLayout() override;
    D2D1_SIZE_F SizeHint() const override;
};

// ---- Stack layout (show one child at a time) ----
class UI_API StackWidget : public Widget {
public:
    int ActiveIndex() const { return activeIndex_; }
    void SetActiveIndex(int i);

    std::function<void(int)> onActiveChanged;

    void DoLayout() override;
    void DrawTree(Renderer& r) override;
    D2D1_SIZE_F SizeHint() const override;

private:
    int activeIndex_ = 0;
};

} // namespace ui
