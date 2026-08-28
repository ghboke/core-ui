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
#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <cfloat>
#include <algorithm>
#include <unordered_set>

namespace ui {

class Renderer;
struct MouseEvent;

using WidgetPtr = std::shared_ptr<class Widget>;

// ---- Layout enums ----
enum class LayoutAlign   { Start, Center, End, Stretch };
enum class LayoutJustify { Start, Center, End, SpaceBetween, SpaceAround };

// CSS display semantic. Matters for flex cross-axis sizing:
// - Block: default. cross-axis stretches with align-items: stretch (CSS spec
//   for `display: block` flex items with auto cross-size).
// - Inline / InlineBlock: intrinsic cross-size. Doesn't stretch even when the
//   container is align-items: stretch — uses SizeHint instead. Position still
//   honors align-items / align-self. Matches CSS `display: inline*` behavior
//   for flex items.
// - None: collapses (visible=false).
enum class Display { Block, Inline, InlineBlock, None };

// ---- CSS cursor keyword → logical cursor ----
// The window translates this to a Win32 IDC_* when painting the hover cursor.
enum class CursorKind {
    Default,     // IDC_ARROW (inherit / unset)
    Pointer,     // IDC_HAND ("pointer" in CSS)
    Text,        // IDC_IBEAM
    Crosshair,   // IDC_CROSS
    Wait,        // IDC_WAIT
    Move,        // IDC_SIZEALL
    NotAllowed,  // IDC_NO
    EwResize,    // IDC_SIZEWE (ew-resize / col-resize)
    NsResize,    // IDC_SIZENS (ns-resize / row-resize)
    NeswResize,  // IDC_SIZENESW
    NwseResize,  // IDC_SIZENWSE
    Help,        // IDC_HELP
    None,        // hide cursor
};

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
    virtual ~Widget();

    // ---- Tree ----
    void AddChild(WidgetPtr child);
    void InsertChild(size_t index, WidgetPtr child);   // clamps index to [0, size()]
    void RemoveChild(Widget* child);

    /* 一次性把 [at, at+removeCount) 换成 replacement (build 285)。
     *
     * 为什么需要: v-for 重排原本是 "n 次 RemoveChild + n 次 InsertChild",
     * 而两者各自都是 O(n) (RemoveChild 用 remove_if 扫全表, InsertChild 是
     * vector 中间插入的 memmove) —— 合起来 O(n^2)。2000 行实测 6.6 秒。
     * 本接口把整块一次性换掉: 一趟 erase + 一次 range-insert = O(n)。
     *
     * at 会夹到 [0, size()]; removeCount 夹到不越界。replacement 里的空指针
     * 会被跳过。 */
    void ReplaceChildRange(size_t at, size_t removeCount,
                           std::vector<WidgetPtr> replacement);

    /* 批量摘除 (build 285)。一趟 remove_if + 一次 erase = O(n), 取代
     * "k 次 RemoveChild" 的 k*O(n)。victims 里不存在的项忽略。
     * 与 ReplaceChildRange 配对使用: 先整体摘除, 再整块插回。 */
    void RemoveChildren(const std::unordered_set<Widget*>& victims);

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
    float flexShrink = 1.0f;        // CSS flex-shrink: >0 shrinks when an HBox row
                                    // overflows its width; 0 = never shrink (icons).
    float percentW = -1, percentH = -1;  // -1 = not percentage, 0-100 = % of parent
    // Percentage min/max constraints. -1 = unset. Resolved at layout time
    // against parent content size (same as percentW/percentH). Useful for
    // patterns like `min-width: 48%` to make 2 cells per row via flex-wrap.
    float percentMinW = -1, percentMinH = -1;
    float percentMaxW = -1, percentMaxH = -1;
    // CSS align-self override on a flex item. -1 = inherit container's
    // align-items (default). Otherwise cast back to LayoutAlign
    // (Start/Center/End/Stretch) — used by VBox/HBox cross-axis layout.
    int alignSelfOverride = -1;
    // CSS display. Block = stretch in cross-axis; Inline/InlineBlock = use
    // SizeHint cross-axis (intrinsic-sized, no stretch). Set by factory per
    // tag default (label/span/a/etc → Inline, others → Block) and overridable
    // via CSS `display:`.
    Display display = Display::Block;

    // ---- Absolute positioning ----
    bool positionAbsolute = false;
    /* build 161: 布局豁免 — true 且已有有效 rect 时, 布局系统不改写本
     * widget 的 rect (子树仍照常布局)。给"用户可拖动的浮动面板"用: 拖后
     * 任何 relayout (反应式重建/菜单/RequestLayout) 不再把它打回 CSS
     * 默认位。显式 ui_widget_set_rect 仍有效。仅对 positionAbsolute
     * 组件有意义 (流式布局里钉住会破坏兄弟排布, 布局只在 absolute
     * 分支检查本标志)。 */
    bool layoutPinned = false;
    float posLeft = -1, posTop = -1, posRight = -1, posBottom = -1;  // -1 = not set
    // Raw CSS for sides whose value depends on parent size (% or calc()).
    // Resolved at layout time (parent rect known); empty string falls back to
    // the eager pos*Left/etc above. Lets `top: calc(50% - 20px)` work.
    std::string posLeftRaw, posTopRaw, posRightRaw, posBottomRaw;
    // Reactive absolute-position overrides from .uix bindings such as
    // `:left="x"`. They are re-applied after style recomputation so hover /
    // class changes do not reset script-driven popover placement back to the
    // static CSS baseline.
    bool dynamicPosLeftSet = false, dynamicPosTopSet = false;
    bool dynamicPosRightSet = false, dynamicPosBottomSet = false;
    float dynamicPosLeft = -1, dynamicPosTop = -1;
    float dynamicPosRight = -1, dynamicPosBottom = -1;
    std::string dynamicPosLeftRaw, dynamicPosTopRaw;
    std::string dynamicPosRightRaw, dynamicPosBottomRaw;
    void ApplyDynamicPositionOverrides();

    // ---- Padding (inner space) ----
    float padL = 0, padT = 0, padR = 0, padB = 0;

    // ---- Margin (outer space) ----
    float marginL = 0, marginT = 0, marginR = 0, marginB = 0;

    // ---- State ----
    bool visible = true;
    bool hitTransparent = false;  /* true 时 HitTest 只看子不返回自身，让事件穿透到下层 */
    bool dragWindow = false;      /* true 时命中该 widget 触发窗口拖动（WM_NCHITTEST → HTCAPTION） */
    bool hitClient = false;       /* true 时命中该 widget (或其子) 强制 HTCLIENT — canvas mode 拖窗
                                     区域里的交互 overlay (如 borderless 关闭热区) 用它拿回
                                     WM_MOUSEMOVE/CLICK。NCHITTEST ancestor walk 里优先于 dragWindow。 */
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

    // ---- CSS transform ----
    // Applied at draw time: shift this widget and its subtree by (transformX, transformY).
    // Only translate is supported in v0; rotate/scale not implemented.
    float transformX = 0.0f;
    float transformY = 0.0f;

    // ---- CSS box-shadow ----
    // Rendered as a softened rounded rect before the widget's own background.
    struct BoxShadow {
        float offsetX = 0, offsetY = 0;
        float blur = 0;           // radius of soft edge (0 = hard shadow)
        float spread = 0;         // outward expansion past the widget bounds
        D2D1_COLOR_F color = {0, 0, 0, 0};
        bool set = false;
    };
    BoxShadow boxShadow;
    // Multiple shadows (CSS comma-separated list). Each can be inset.
    struct BoxShadowEx {
        float offsetX = 0, offsetY = 0;
        float blur = 0, spread = 0;
        D2D1_COLOR_F color = {0, 0, 0, 0};
        bool inset = false;
    };
    std::vector<BoxShadowEx> boxShadows;

    // ---- CSS transform extensions: rotate (degrees) + non-uniform scale ----
    float rotateDeg = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;

    // ---- Background ----
    D2D1_COLOR_F bgColor = {0, 0, 0, 0};
    std::function<D2D1_COLOR_F()> bgColorFn;  // dynamic color (e.g. theme-aware)
    float backdropBlur = 0.0f;                 // CSS backdrop-filter: blur(...)

    // CSS gradient background. A single CSS rule may produce multiple layers
    // (comma-separated linear-gradient(...), linear-gradient(...), ...). Each
    // layer carries its own tile size / position so patterns like the classic
    // 4-layer checkerboard work.
    struct BgGradient {
        enum Kind { Linear, Radial };
        Kind kind = Linear;
        // Linear: angle in degrees per CSS spec (0deg = bottom→top, 90deg = left→right,
        // 180deg = top→bottom — the default for `linear-gradient(...)` without an angle).
        float angleDeg = 180.0f;
        // Radial: center as percentage of widget size; radius as percentage.
        float cxPct = 50.0f, cyPct = 50.0f, radiusPct = 50.0f;
        struct Stop {
            D2D1_COLOR_F color{0, 0, 0, 1};
            float position = -1.0f;  // 0..1 explicit, -1 = auto-distribute
        };
        std::vector<Stop> stops;
        // CSS background-size + background-position per layer.
        // tileW / tileH < 0 → no tiling, paint once across the full widget rect
        //                    (default — single-layer gradients keep old behavior).
        // tileW / tileH > 0 → render one tile of that size and repeat-wrap it
        //                    across the widget rect at offset (posX, posY).
        float tileW = -1.0f, tileH = -1.0f;
        float posX = 0.0f, posY = 0.0f;
    };
    bool hasBgGradient = false;
    std::vector<BgGradient> bgGradients;

    // ---- CSS style overrides (used when non-sentinel). Controls in their
    //      OnDraw should prefer these before falling back to theme defaults.
    //      Sentinel for missing colors is alpha==0 (except hasAccent etc.).
    //      Sentinel for missing scalars is a negative value. ----
    struct CssOverride {
        bool hasFg = false;               D2D1_COLOR_F fg{};
        bool hasAccent = false;           D2D1_COLOR_F accent{};
        bool hasBorderColor = false;      D2D1_COLOR_F borderColor{};
        bool hasPlaceholderColor = false; D2D1_COLOR_F placeholderColor{};
        bool hasCaretColor = false;       D2D1_COLOR_F caretColor{};
        // For items in list-like controls (ComboBox dropdown, etc.):
        // when set, the currently-selected item's label is drawn in this color
        // instead of `fg`. Lets users distinguish selection by typography alone
        // (e.g. when accent-color: transparent hides the side indicator bar).
        bool hasSelectedColor = false;    D2D1_COLOR_F selectedColor{};
        // Per-item visuals in list-like controls. All default to widget-level
        // bg / hover-tint / no divider if unset.
        bool hasItemBg        = false;    D2D1_COLOR_F itemBg{};
        bool hasItemHoverBg   = false;    D2D1_COLOR_F itemHoverBg{};
        bool hasItemBorderColor = false;  D2D1_COLOR_F itemBorderColor{};
        // Active-item background (Tab / list selection). Distinct from
        // selectedColor (text). Lets pill-style tabs put a colored chip
        // behind the active label without affecting other tabs.
        bool hasSelectedBg    = false;    D2D1_COLOR_F selectedBg{};
        float selectedRadius = -1.0f;     // border-radius of the selected pill
        // 文本选区颜色（TextInput / TextArea / Label 拖选高亮）。
        // active = 控件有焦点时；inactive = 失焦但选区保留时（变灰）。
        bool hasSelTextBg          = false; D2D1_COLOR_F selTextBg{};
        bool hasSelTextFg          = false; D2D1_COLOR_F selTextFg{};
        bool hasSelTextBgInactive  = false; D2D1_COLOR_F selTextBgInactive{};
        bool hasSelTextFgInactive  = false; D2D1_COLOR_F selTextFgInactive{};
        float borderWidth  = -1.0f;       // -1 = unset (use theme)
        float borderRadius = -1.0f;
        float fontSize     = -1.0f;
    };
    CssOverride css;

    // ---- CSS cursor: logical kind mapped to Win32 cursor by ui_window ----
    CursorKind cursor = CursorKind::Default;

    // ---- CSS overflow: when true, children paint clipped to this widget's
    //      (rounded) rect. Used so e.g. a rounded tab bar's per-tab hover
    //      backgrounds don't bleed past the bar's rounded corners.
    bool overflowHidden = false;

    // ---- Identity for runtime CSS matching ----
    // Stored at compile time so recomputeStyle can rebuild a fresh MatchNode
    // chain (with proper parent links) every time state / class changes.
    std::string cssTag;
    std::vector<std::string> classList;       // static classes from class="..."
    std::vector<std::string> dynamicClasses;  // runtime classes from :class=expr

    // ---- Runtime CSS state machine (for :hover / :pressed / :focus / :disabled) ----
    // The compiler installs `recomputeStyle` on each widget with a closure over
    // the page's stylesheet + MatchNode + cssVars. Call `RefreshCssState()` after
    // any state bit changes (hovered/pressed/focused_/enabled) — it only runs
    // the recompute when the bit set actually differs from the last snapshot.
    std::function<void(uint32_t stateBits)> recomputeStyle;
    uint32_t lastStateBits = 0;
    uint32_t CurrentStateBits() const;
    void RefreshCssState();

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

    // ---- Mouse event hooks ----
    // Called by the default Widget::OnMouse* handlers when set. Subclasses
    // that override OnMouse* are responsible for forwarding if they want
    // hooks to fire — most built-in controls bypass hooks, but passive
    // containers (div/svg/etc.) run them, which is exactly where custom
    // widgets (color picker, draggable handles) plug in.
    std::function<void(const MouseEvent&)> onMouseDownHook;
    std::function<void(const MouseEvent&)> onMouseMoveHook;
    std::function<void(const MouseEvent&)> onMouseUpHook;
    std::function<void(const MouseEvent&)> onMouseWheelHook;
    std::function<void(const MouseEvent&)> onMouseDblClickHook;
    // Fires when cursor leaves this widget. Two trigger paths share this hook:
    //   1) cursor moves to another widget in the same window (hover transition,
    //      see UiWindowImpl::OnMouseMove) — leave fires on widgets in the old
    //      hovered ancestor chain that are no longer in the new chain.
    //   2) cursor leaves the window entirely (WM_MOUSELEAVE) — leave fires on
    //      every widget in the current hovered ancestor chain.
    // Mirrors CSS / web `mouseleave` event semantics. No args because mouseleave
    // has no meaningful cursor position (cursor is either elsewhere or gone).
    std::function<void()>                  onMouseLeaveHook;
    // Fires when cursor enters this widget (added to the hovered ancestor
    // chain). Mirrors web `mouseenter` (non-bubbling — fires per widget in
    // the chain individually, like DOM mouseenter on each element).
    std::function<void(const MouseEvent&)> onMouseEnterHook;
    // Fires on right-button release over this widget (deepest hit widget).
    // Mirrors web `contextmenu`. e.button == 2.
    std::function<void(const MouseEvent&)> onContextMenuHook;

    // ---- Drag & drop (HTML5-style, dispatched by UiWindowImpl) ----
    // Source side: set `draggable` (+ optional declarative `dragData`
    // payload). After the press moves past the drag threshold the window
    // fires onDragStartHook — the handler may rewrite `data` (equivalent of
    // DOM dataTransfer.setData). onDragEndHook fires when the drag finishes,
    // dropped=true iff a target accepted it.
    bool draggable = false;
    std::wstring dragData;
    std::function<void(const MouseEvent&, std::wstring& data)> onDragStartHook;
    std::function<void(bool dropped)> onDragEndHook;
    // Target side: a widget accepts drops when `dropTarget` is set or an
    // onDropHook is installed (DOM: listening for drop implies droppable —
    // without the dragover-preventDefault dance). `dragOver` is maintained
    // by the window while a drag hovers the widget and maps to the CSS
    // `:drag-over` pseudo-class.
    bool dropTarget = false;
    bool dragOver = false;
    std::function<void(const MouseEvent&)> onDragEnterHook;
    std::function<void(const MouseEvent&)> onDragOverHook;
    std::function<void()> onDragLeaveHook;
    std::function<void(const std::wstring& data, const MouseEvent&)> onDropHook;
    bool CanAcceptDrop() const { return dropTarget || (bool)onDropHook; }
    // Form-style submit. Fired when a TextInput/TextArea sees ENTER pressed
    // while focused. Other widgets ignore it; pages can listen on the form
    // wrapper element itself by hooking directly through @submit.
    std::function<void()> onSubmitHook;
    // Focus transitions. Fired by SetFocused() when state actually flips —
    // safe to invoke on a non-focusable widget that gets a stale call.
    std::function<void()> onFocusHook;
    std::function<void()> onBlurHook;

    // ---- Control-specific DSL (overridden by subclasses, base is no-op) ----
    virtual WidgetPtr FontSize(float) { return shared_from_this(); }
    virtual WidgetPtr Bold()          { return shared_from_this(); }
    virtual WidgetPtr Gap(float)      { return shared_from_this(); }
    virtual WidgetPtr TextColor(const D2D1_COLOR_F&) { return shared_from_this(); }
    virtual WidgetPtr Align(int)      { return shared_from_this(); }

    // ---- Mount-phase transition gate (L45 / GuoheView L46) ----
    // True after this widget has been painted to RT at least once. DrawTree
    // (and every subclass override) sets it right after OnDraw. Used by
    // PageState::ApplyBindingToWidget to decide between Immediate (snap) and
    // animated setters: while a widget has never been painted, all reactive
    // binding pushes go through Immediate so the page can mount in its final
    // state without ".uix data() default → caller real value" transition. Once
    // the user has actually seen a frame, subsequent reactive updates animate.
    //
    // Not reset on visibility flips (v-show off→on) or detach/reattach — the
    // semantic is "has this widget ever been on screen". v-if rebuilds the
    // widget instance entirely, so a fresh paintedOnce_=false naturally falls
    // out without an explicit reset.
    bool PaintedOnce() const { return paintedOnce_; }
    void MarkPainted() { paintedOnce_ = true; }

    // ---- Virtual interface ----
    virtual void OnDraw(Renderer& r);
    virtual bool OnMouseMove(const MouseEvent& e);
    virtual bool OnMouseDown(const MouseEvent& e);
    virtual bool OnMouseUp(const MouseEvent& e);
    virtual bool OnMouseWheel(const MouseEvent& e);
    virtual bool OnMouseDoubleClick(const MouseEvent& e);
    virtual bool OnKeyDown(int vk)   { (void)vk; return false; }
    virtual bool OnKeyChar(wchar_t c){ (void)c;  return false; }
    // IME anchor: caret rect in window DIP coords. Text-editing widgets
    // override; UiWindowImpl uses it to pin the IME composition/candidate
    // window at the caret (WM_IME_STARTCOMPOSITION/WM_IME_COMPOSITION).
    // Requires an active measure context; return false when unknown.
    virtual bool GetCaretRect(D2D1_RECT_F* out) const { (void)out; return false; }
    virtual D2D1_SIZE_F SizeHint() const { return {fixedW, fixedH}; }

    // ---- Focus ----
    bool IsFocused() const { return focused_; }
    void SetFocused(bool f) {
        if (focused_ == f) return;
        focused_ = f;
        if (f) { if (onFocusHook) onFocusHook(); }
        else   { if (onBlurHook)  onBlurHook();  }
    }
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
    bool paintedOnce_ = false;   // see PaintedOnce() / MarkPainted() above
};

/* 递归平移 w 及其全部后代的 rect (build 285 起对外)。
 * 两个用途: ① CSS transform: translate 绘制前应用、绘制后撤销;
 *          ② ScrollViewWidget 的纯滚动 —— 内容没变、只是偏移变了时, 平移 rect
 *             比重跑一遍 flex 布局便宜一个量级。 */
UI_API void ShiftSubtreeRects(Widget* w, float dx, float dy);

// ---- Layout containers ----

// 默认 flex gap (VBox/HBox/Grid 共用). recomputeStyle (compiler.cpp) 在
// hover/press/focus 重算样式时, 会先把容器 gap 重设到这个默认值, 再让 CSS 覆盖。
// 编译时默认值与 recomputeStyle 的重设值必须是同一个常量 —— 否则 "CSS 没写
// gap 的容器" 编译时 (默认 4) 和首次交互后 (旧代码重设成 0) 不一致, 会在首次
// hover/click 瞬间布局跳变 (L119: toolbar 原图按钮首次点击右移 8px)。
inline constexpr float kDefaultFlexGap = 4.0f;

class UI_API VBoxWidget : public Widget {
public:
    float gap_ = kDefaultFlexGap;
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
    float gap_ = kDefaultFlexGap;
    LayoutAlign   crossAlign_ = LayoutAlign::Stretch;   // vertical alignment of children
    LayoutJustify mainJustify_ = LayoutJustify::Start;  // horizontal distribution
    // CSS flex-wrap: wrap. When true and total natural child width exceeds
    // container width, children are packed into multiple rows; each row's
    // height is the max child height; rows stack vertically with the same
    // gap. Default false matches `flex-wrap: nowrap`.
    bool flexWrap_ = false;

    WidgetPtr Gap(float g) override { gap_ = g; return shared_from_this(); }
    WidgetPtr CrossAlign(LayoutAlign a)   { crossAlign_ = a; return shared_from_this(); }
    WidgetPtr MainJustify(LayoutJustify j){ mainJustify_ = j; return shared_from_this(); }
    WidgetPtr FlexWrap(bool w)            { flexWrap_ = w; return shared_from_this(); }
    void DoLayout() override;
    D2D1_SIZE_F SizeHint() const override;

private:
    void DoLayoutWrap();  // multi-row packing path
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
    float rowGap_ = kDefaultFlexGap;
    float colGap_ = kDefaultFlexGap;

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

// ---- Layout-dirty signaling ----
// Dynamic widget mutations (v-if mount/unmount, v-for re-keying) need to
// re-trigger layout. The window's paint loop checks this global flag; the
// reactive system flips it after any subtree rewrite.
inline bool& LayoutDirtyFlag() {
    static thread_local bool dirty = false;
    return dirty;
}
/* 置脏 + 失效所有窗口 (催生一帧)。修复前只置旗标 — 旗标仅在 OnPaint 里
 * 被消费, 完全空闲的窗口永远等不到那次 paint, 导致 set_title / set_text
 * 等"纯数据变更"在无其他活动时永不上屏 (L127: 慢加载期标题提示丢失,
 * 调试 IPC 的强制渲染一直在掩护这个缺陷)。实现在 widget.cpp (需 Context)。 */
UI_API void RequestLayout();

} // namespace ui
