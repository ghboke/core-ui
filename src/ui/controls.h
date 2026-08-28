#pragma once
#include "widget.h"
#include "renderer.h"
#include "render_handles.h"
#include "theme.h"
#include "context_menu.h"
#include "ui_context.h"  // GetContext().InvalidateAllWindows() from inline setters
#include "animation.h"
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace ui {

class MeasureContext;

// ---- Label ----
class UI_API LabelWidget : public Widget {
public:
    explicit LabelWidget(const std::wstring& text) : text_(text) {}

    WidgetPtr FontSize(float s) override { fontSize_ = s; return shared_from_this(); }
    WidgetPtr Bold() override { bold_ = true; return shared_from_this(); }
    WidgetPtr TextColor(const D2D1_COLOR_F& c) override { color_ = c; customColor_ = true; return shared_from_this(); }
    WidgetPtr Align(int a) override { align_ = (DWRITE_TEXT_ALIGNMENT)a; return shared_from_this(); }

    void SetText(const std::wstring& t) { text_ = t; ClearSelection(); ui::RequestLayout(); }
    const std::wstring& Text() const { return text_; }
    void SetWrap(bool w) { wrap_ = w; }
    void SetMaxLines(int n) { maxLines_ = n; }

    /* CSS line-height (build 92+):
     *   unitless 倍数 (推荐, 跟 font-size 联动): SetLineHeightRatio(1.3f)
     *   显式像素值:                              SetLineHeightPx(17.0f)
     * 都 0 时走默认 1.3 × font-size (跟现代浏览器 / Win11 Fluent UI 一致).
     * 历史默认 fontSize_ + 10.0f 在密集列表 / 表格里偏松, 是 lib 早期为
     * badge / pill / chip 留 breathing 的硬编码副作用. */
    void SetLineHeightRatio(float r) { lineHeightRatio_ = r; }
    void SetLineHeightPx(float px)   { lineHeightPx_ = px; }

    // Dynamic text color (for theme-aware colors)
    void SetTextColorFn(std::function<D2D1_COLOR_F()> fn) { textColorFn_ = std::move(fn); customColor_ = true; }

    // 文本选中支持。默认关闭；在 CSS 里 user-select: text 打开 → 设
    // selectable=true，并把 widget 注册为 focusable。鼠标拖选 / Ctrl+C 复制
    // 可用，文字色与选区色支持 selection-* CSS 属性 + active/inactive 区分。
    bool selectable = false;
    void SetSelectable(bool v);
    bool HasSelection() const { return selectionStart_ >= 0 && selectionEnd_ >= 0 && selectionStart_ != selectionEnd_; }
    void ClearSelection() { selectionStart_ = selectionEnd_ = -1; }
    std::wstring SelectedText() const;

    void OnDraw(Renderer& r) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    bool OnKeyDown(int vk) override;
    D2D1_SIZE_F SizeHint() const override;

private:
    std::wstring text_;
    float fontSize_ = theme::kFontSizeNormal;
    bool bold_ = false;
    bool wrap_ = false;
    int maxLines_ = 0;  /* 0 = 不限制 */
    float lineHeightRatio_ = 0.0f;  /* CSS line-height unitless (1.3 = 1.3×fontSize) */
    float lineHeightPx_    = 0.0f;  /* CSS line-height px ("17px") */
    bool customColor_ = false;
    D2D1_COLOR_F color_ = {};
    std::function<D2D1_COLOR_F()> textColorFn_;
    DWRITE_TEXT_ALIGNMENT align_ = DWRITE_TEXT_ALIGNMENT_LEADING;

    // 选区状态
    int selectionStart_ = -1;
    int selectionEnd_ = -1;
    bool dragging_ = false;
    int CharIndexAtX(MeasureContext& measure, float x) const;
};

// ---- Button ----
// Button type: "default" = standard gray, "primary" = accent filled
enum class ButtonType { Default, Primary };

// Inherits HBoxWidget so a "no text + has children" button (typical icon-only
// button — <button><svg/></button>) lays its children out with flex semantics
// (align-items / justify-content / gap from CSS), instead of stacking them at
// (0,0) like the base Widget::DoLayout. Text-bearing buttons keep the legacy
// path: OnDraw owns text + icon rendering, children pinned by Widget::DoLayout.
class UI_API ButtonWidget : public HBoxWidget {
public:
    explicit ButtonWidget(const std::wstring& text) : text_(text) {
        focusable = true;
        /* L148-4: 按钮默认手型指针 (Web 惯例) — CSS 显式 cursor 仍可覆盖
         * (page 系统只在声明了 cursor 时才写, 不会重置此默认)。 */
        cursor = CursorKind::Pointer;
    }

    WidgetPtr FontSize(float s) override { fontSize_ = s; return shared_from_this(); }

    void SetText(const std::wstring& t) { text_ = t; ui::RequestLayout(); }
    void SetIcon(const std::wstring& icon) { icon_ = icon; ui::RequestLayout(); }
    const std::wstring& Text() const { return text_; }
    void SetType(ButtonType t) { type_ = t; }
    ButtonType Type() const { return type_; }
    void SetTextColor(const D2D1_COLOR_F& c) { customTextColor_ = c; hasCustomTextColor_ = true; ui::GetContext().InvalidateAllWindows(); }
    void SetCustomBgColor(const D2D1_COLOR_F& c) { customBgColor_ = c; hasCustomBgColor_ = true; ui::GetContext().InvalidateAllWindows(); }

    void OnDraw(Renderer& r) override;
    void DoLayout() override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    D2D1_SIZE_F SizeHint() const override;

private:
    std::wstring text_;
    std::wstring icon_;
    float fontSize_ = theme::kFontSizeNormal;
    ButtonType type_ = ButtonType::Default;
    D2D1_COLOR_F customTextColor_ = {};
    D2D1_COLOR_F customBgColor_ = {};
    bool hasCustomTextColor_ = false;
    bool hasCustomBgColor_ = false;
};

// ---- CheckBox ----
class UI_API CheckBoxWidget : public Widget {
public:
    explicit CheckBoxWidget(const std::wstring& text) : text_(text) {
        focusable = true;
        animating_ = false;
    }

    bool Checked() const { return checked_; }
    const std::wstring& Text() const { return text_; }
    void SetText(const std::wstring& t) { text_ = t; ui::RequestLayout(); }
    void SetChecked(bool v);
    // Set value without animation. Used by PageState mount-phase dispatch
    // (Widget::PaintedOnce() == false) and any caller that explicitly wants
    // to skip the transition.
    void SetCheckedImmediate(bool v) { checked_ = v; checkAnim_.SetImmediate(checked_ ? 1.0f : 0.0f); animating_ = false; }

    void SetAnimationDuration(float durationMs) { checkAnim_.SetDuration(durationMs); }
    float GetAnimationDuration() const { return checkAnim_.Duration(); }

    void SetEasingFunction(EasingFunction func) { checkAnim_.SetEasing(func); }
    EasingFunction GetEasingFunction() const { return checkAnim_.Easing(); }

    void UpdateAnimation();

    void OnDraw(Renderer& r) override;
    bool OnMouseUp(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    D2D1_SIZE_F SizeHint() const override;

private:
    std::wstring text_;
    bool checked_ = false;
    AnimatedFloat checkAnim_{0.0f, 200.0f, EasingFunction::EaseOutCubic};
    bool animating_ = false;
    float ContentRight_() const;

    friend class UiWindowImpl;
};

// ---- Separator ----
class UI_API SeparatorWidget : public Widget {
public:
    explicit SeparatorWidget(bool vertical = false) : vertical_(vertical) {
        fixedH = vertical ? 0 : 1;
        fixedW = vertical ? 1 : 0;
    }
    void OnDraw(Renderer& r) override;

private:
    bool vertical_;
};

// ---- Slider ----
class UI_API SliderWidget : public Widget {
public:
    SliderWidget(float mn, float mx, float val) : min_(mn), max_(mx), value_(val) { focusable = true; }

    float Value() const { return value_; }
    void SetValue(float v) { value_ = std::clamp(v, min_, max_); }

    void UpdateThumbAnimation();
    bool ThumbAnimating() const { return thumbScale_.Active(); }

    void OnDraw(Renderer& r) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    D2D1_SIZE_F SizeHint() const override;

private:
    float min_, max_, value_;
    bool dragging_ = false;
    float ValueFromX(float x) const;
    void RetargetThumbAnimation();

    // Thumb scale animation: smoothly transitions between rest/hover/press sizes
    AnimatedFloat thumbScale_{0.86f, 120.0f, EasingFunction::EaseOutCubic};
    float thumbScaleTarget_ = 0.86f;   // target scale for current state

    friend class UiWindowImpl;
};

// ---- Panel (container with background) ----
class UI_API PanelWidget : public Widget {
public:
    PanelWidget() { bgColor = {0, 0, 0, 0}; }
    explicit PanelWidget(const D2D1_COLOR_F& bg) { bgColor = bg; }
    // bgColorFn inherited from Widget — theme-aware bg works on all widgets now
};

// ---- Image ----
// 轻量级图片 widget。HTML 里 <img src="..."> 走这个。区别于 ImageViewWidget
// （那个带缩放 / 平移 / 滚动 / SVG / GIF 等专业能力，给 ImageViewer 用）。
// 这里只做：按 src 名字从 ui::asset 取字节 → WIC 解码 → 按 rect 绘制。
//
// fit 控制如何把位图填进 rect：
//   Fill / Fit / None / Cover —— 跟 CSS object-fit 大致对齐
class UI_API ImageWidget : public Widget {
public:
    enum class Fit { Fill, Contain, Cover, None };

    ImageWidget();
    ~ImageWidget() override;
    explicit ImageWidget(const std::string& src);

    /* 让所有 src == name 且上次解析失败的实例重新尝试 (build 286)。
     *
     * 用途: 宿主的 asset resolver 是异步喂数据的 —— 第一次被问到时字节还没
     * 从磁盘读上来, 只能返回失败; 而 loadFailed_ 会把这个 widget 永久钉死,
     * 除非 src 变化。数据就绪后调这个把闩锁打开, 下一帧自然重试。
     *
     * 只在 UI 线程调。 */
    static void RetryFailedLoads(const std::string& name);

    // HTML <img src="logo.png"> 解析时调，name 是 asset registry 里的 key
    void SetSrc(const std::string& src);
    const std::string& Src() const { return src_; }

    void SetFit(Fit f) { fit_ = f; }
    Fit  GetFit() const { return fit_; }

    void OnDraw(Renderer& r) override;
    D2D1_SIZE_F SizeHint() const override;

private:
    void ClearImageResource();

    std::string src_;
    Fit fit_ = Fit::Contain;
    mutable ComPtr<ID2D1Bitmap> bitmap_;       // lazy-loaded on first draw
    ResourceKey imageResourceKey_;
    uint64_t imageGeneration_ = 0;
    mutable bool loadFailed_ = false;          // 防止每帧都重试解码
    mutable float intrinsicW_ = 0, intrinsicH_ = 0;
};

// ---- TextInput ----
class UI_API TextInputWidget : public Widget {
public:
    explicit TextInputWidget(const std::wstring& placeholder = L"")
        : placeholder_(placeholder) { focusable = true; ResetCaretBlink(); }

    const std::wstring& Text() const { return text_; }
    void SetText(const std::wstring& t) { text_ = t; cursorPos_ = (int)t.size(); selectionStart_ = selectionEnd_ = -1; ResetCaretBlink(); }
    static UINT EffectiveCaretBlinkMs();
    void ResetCaretBlink();
    bool ShouldShowCaret() const;

    void OnDraw(Renderer& r) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    bool OnKeyChar(wchar_t ch) override;
    bool OnKeyDown(int vk) override;
    D2D1_SIZE_F SizeHint() const override;
    bool GetCaretRect(D2D1_RECT_F* out) const override;

    bool focused = false;
    bool readOnly = false;
    std::function<bool(wchar_t)> inputFilter;
    int maxLength = -1;

private:
    std::wstring text_;
    std::wstring placeholder_;
    int cursorPos_ = 0;
    int selectionStart_ = -1;
    int selectionEnd_ = -1;
    float scrollX_ = 0;
    uint64_t caretBlinkStartTick_ = 0;
    bool dragging_ = false;

    int CharIndexFromX(MeasureContext& measure, float x) const;
    void DeleteSelection();
    bool HasSelection() const { return selectionStart_ >= 0 && selectionEnd_ >= 0 && selectionStart_ != selectionEnd_; }
    void ClearSelection() { selectionStart_ = selectionEnd_ = -1; }
    void EnsureCursorVisible(MeasureContext& measure);
    std::wstring GetSelectedText() const;
    void SetClipboardText(const std::wstring& text);
    std::wstring GetClipboardText();
};

// ---- ChipsInput (single-line template editor: atomic chips + free text) ----
// Value() serializes to a template string where each chip is `{key}` and free
// text is literal (e.g. `{filename} - {width}x{height}`). Chips are atomic:
// caret skips over them, Backspace/Delete first selects then removes, they can
// be reordered by dragging inside the widget, and external drags (the window
// DnD framework, payload = chip key) drop in as new chips at the caret the
// cursor points at. Chip keys can be given display labels via SetChipLabels
// ("key=label;key2=label2") — labels are presentation-only, Value() always
// serializes keys.
class UI_API ChipsInputWidget : public Widget {
public:
    explicit ChipsInputWidget(const std::wstring& placeholder = L"");

    std::wstring Value() const;
    void SetValue(const std::wstring& tpl);
    void SetChipLabels(const std::wstring& spec);
    void InsertChip(const std::wstring& key);     // at current caret
    void SetPlaceholder(const std::wstring& p) { placeholder_ = p; }

    void ResetCaretBlink();
    bool ShouldShowCaret() const;
    // Cursor dispatch (WM_SETCURSOR): true when windowX (DIP) is over a chip
    // pill / over the hovered chip's × close zone.
    bool IsOverChip(MeasureContext& m, float windowX) const;
    bool IsOverChipClose(MeasureContext& m, float windowX) const;
    // True while a chip is being drag-reordered (past threshold) — the
    // window shows the move cursor for the duration of the gesture.
    bool IsDraggingChip() const { return dragChip_ >= 0 && dragMoved_; }

    void OnDraw(Renderer& r) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    bool OnKeyChar(wchar_t ch) override;
    bool OnKeyDown(int vk) override;
    D2D1_SIZE_F SizeHint() const override;
    bool GetCaretRect(D2D1_RECT_F* out) const override;

    bool readOnly = false;

private:
    // One element = a chip (atomic, s = key) or a single text character.
    struct Elem { bool chip; std::wstring s; };
    std::vector<Elem> elems_;
    std::wstring placeholder_;
    std::vector<std::pair<std::wstring, std::wstring>> chipLabels_;
    int caret_ = 0;            // element boundary index [0..elems_.size()]
    int selAnchor_ = -1;       // selection anchor boundary (-1 = no selection)
    bool selecting_ = false;   // mouse-drag selection in progress
    int selectedChip_ = -1;    // chip armed for deletion (backspace stage 1)
    int hoverChip_ = -1;       // chip under cursor (draws the × affordance)
    int dragChip_ = -1;        // chip being reordered (internal drag)
    bool dragMoved_ = false;
    int dropCaret_ = -1;       // insertion boundary highlighted during drags
    float scrollX_ = 0;
    float pressX_ = 0, pressY_ = 0;
    float dragMx_ = 0, dragMy_ = 0;   // cursor pos during chip reorder (ghost)
    uint64_t caretBlinkStartTick_ = 0;

    const std::wstring& ChipLabel(const std::wstring& key) const;
    float ChipFontSize() const;
    float ElemWidth(MeasureContext& m, const Elem& el, float fontSize) const;
    // Boundary x positions (content-local, before scroll): out has size()+1.
    void BoundaryXs(MeasureContext& m, std::vector<float>& out) const;
    int BoundaryFromX(MeasureContext& m, float localX) const;
    int ChipIndexAt(MeasureContext& m, float localX, float localY, bool* onClose) const;
    void EnsureCaretVisible(MeasureContext& m);
    void Mutated();            // fire onTextChanged with Value()
    void RemoveChip(int idx);
    bool HasSelection() const {
        return selAnchor_ >= 0 && selAnchor_ != caret_;
    }
    void ClearSelection() { selAnchor_ = -1; selecting_ = false; }
    void DeleteSelection();    // erase selected element range, caret to start
    std::wstring SerializeRange(int a, int b) const;
};

// ---- TextArea (multi-line text input) ----
class UI_API TextAreaWidget : public Widget {
public:
    explicit TextAreaWidget(const std::wstring& placeholder = L"")
        : placeholder_(placeholder) { focusable = true; ResetCaretBlink(); }

    const std::wstring& Text() const { return text_; }
    void SetText(const std::wstring& t);
    static UINT EffectiveCaretBlinkMs();
    void ResetCaretBlink();
    bool ShouldShowCaret() const;

    /* Soft-wrap on width (matches DOM <textarea wrap="soft">, the browser
     * default). When false, long lines stay single-line and overflow with
     * ellipsis (legacy behavior). */
    void SetWrap(bool w) { wrap_ = w; layoutDirty_ = true; }
    bool Wrap() const { return wrap_; }

    void OnDraw(Renderer& r) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    bool OnMouseWheel(const MouseEvent& e) override;
    bool OnKeyChar(wchar_t ch) override;
    bool OnKeyDown(int vk) override;
    D2D1_SIZE_F SizeHint() const override;
    bool GetCaretRect(D2D1_RECT_F* out) const override;

    bool focused = false;
    bool readOnly = false;
    std::function<bool(wchar_t)> inputFilter;
    int maxLength = -1;

    bool NeedsScrollbar() const;

    /* 宿主驱动的选区 (build 278) —— 让"外部数据源 ↔ 文本"双向联动可做:
     * 例如 OCR 场景里图上划词后, 宿主把对应文本区间选中并滚到可见。
     * start/end 是 UTF-16 码元下标, 自动交换与 clamp; start==end 清选区。 */
    void SetSelectionRange(int start, int end);
    bool SelectionRange(int* start, int* end) const;   /* 无选区返 false */
    /* 用户交互 (拖选 / Shift+方向 / Ctrl+A) 导致选区变化时 fire, UI 线程。
     * 宿主自己调 SetSelectionRange 不 fire —— 避免联动回环。 */
    std::function<void()> onSelectionChanged;

private:
    std::wstring text_;
    std::wstring placeholder_;
    int cursorPos_ = 0;
    int selectionStart_ = -1;
    int selectionEnd_ = -1;
    float scrollY_ = 0;
    uint64_t caretBlinkStartTick_ = 0;
    bool dragging_ = false;
    bool draggingThumb_ = false;
    float dragStartY_ = 0;
    float dragStartScroll_ = 0;
    bool wrap_ = true;            /* default: browser-like soft-wrap */
    static constexpr float kScrollBarWidth = 4.0f;
    static constexpr float kPad = 8.0f;       /* inner padding around text */

    /* Cached IDWriteTextLayout — re-built when text / width / fontSize / wrap
     * change. All hit-tests, caret positioning, selection rendering and the
     * actual draw share this one layout, so screen and click coordinates
     * match exactly (匹配 DirectWrite shaping/kerning, 不再有 per-substring
     * MeasureTextWidth 误差).*/
    mutable ComPtr<IDWriteTextLayout> cachedLayout_;
    mutable bool layoutDirty_ = true;
    mutable std::wstring layoutText_;
    mutable float layoutMaxW_ = -1;
    mutable float layoutFontSize_ = -1;
    mutable bool  layoutWrap_ = true;
    mutable float layoutLineHeight_ = 20.0f;  /* derived from layout metrics */

    float ContentHeight() const;
    float VisibleHeight() const;
    D2D1_RECT_F ThumbRect() const;
    void ClampScroll();

    /* Layout-driven geometry. All return DIP. Layout origin is at
     * (rect.left + kPad, rect.top + kPad - scrollY_). */
    IDWriteTextLayout* EnsureLayout(MeasureContext& measure, float fontSize) const;
    int  HitTestPosFromXY(float x, float y) const;       /* x/y in widget coords */
    bool CaretXYForPos(int pos, float& outX, float& outY, float& outH) const;
    int  PosUp(int pos) const;     /* arrow up — visual line above */
    int  PosDown(int pos) const;
    int  PosLineStart(int pos) const;   /* HOME — start of visual line */
    int  PosLineEnd(int pos) const;     /* END  — end of visual line */

    void DeleteSelection();
    bool HasSelection() const { return selectionStart_ >= 0 && selectionEnd_ >= 0 && selectionStart_ != selectionEnd_; }
    void ClearSelection() { selectionStart_ = selectionEnd_ = -1; }
    void EnsureCursorVisible(MeasureContext* measure = nullptr);
    std::wstring GetSelectedText() const;
    void SetClipboardText(const std::wstring& text);
    std::wstring GetClipboardText();
};

// ---- ComboBox ----
class UI_API ComboBoxWidget : public Widget {
public:
    explicit ComboBoxWidget(std::vector<std::wstring> items)
        : items_(std::move(items)) { focusable = true; }

    int SelectedIndex() const { return selectedIndex_; }
    void SetSelectedIndex(int i) { if (i >= 0 && i < (int)items_.size()) selectedIndex_ = i; }
    void SetItems(std::vector<std::wstring> items) {
        items_ = std::move(items);
        if (selectedIndex_ >= (int)items_.size()) selectedIndex_ = items_.empty() ? -1 : 0;
    }
    const std::vector<std::wstring>& Items() const { return items_; }

    // L83 (i18n): per-item i18n key (empty string = literal item, not
    // translated). The .uix compiler sets these for `<option>@key</option>`;
    // PageState::SetLocale calls RetranslateItems on locale change so combobox
    // options translate in place like `<label>`/`<button>` @key do.
    void SetI18nItemKeys(std::vector<std::string> keys) { i18nKeys_ = std::move(keys); }
    bool HasI18nKeys() const {
        for (const auto& k : i18nKeys_) if (!k.empty()) return true;
        return false;
    }
    void RetranslateItems(const std::function<std::wstring(const std::string&)>& tr) {
        for (size_t i = 0; i < i18nKeys_.size() && i < items_.size(); ++i)
            if (!i18nKeys_[i].empty()) items_[i] = tr(i18nKeys_[i]);
    }

    const std::wstring& SelectedText() const {
        static std::wstring empty;
        return (selectedIndex_ >= 0 && selectedIndex_ < (int)items_.size())
            ? items_[selectedIndex_] : empty;
    }

    bool IsOpen() const { return open_; }
    void Close() { open_ = false; hoveredIndex_ = -1; }

    void OnDraw(Renderer& r) override;
    void OnDrawOverlay(Renderer& r) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    bool OnMouseWheel(const MouseEvent& e) override;
    D2D1_SIZE_F SizeHint() const override;

    std::function<void(int)> onSelectionChanged;

    float ItemHeight() const { return itemHeight_; }
    int ItemCount() const { return (int)items_.size(); }
    D2D1_RECT_F DropdownRect() const;
    int DropdownItemAt(float x, float y) const;

private:
    struct DropdownLayout {
        D2D1_RECT_F rect{};
        int visibleRows = 0;
        bool opensUpward = false;
    };

    DropdownLayout ComputeDropdownLayout_() const;
    void OpenDropdown_();
    void ClampDropdownScroll_(int visibleRows);

    std::vector<std::wstring> items_;
    std::vector<std::string>  i18nKeys_;   // L83: parallel to items_; "" = literal
    int selectedIndex_ = 0;
    int hoveredIndex_ = -1;
    int dropdownScrollIndex_ = 0;
    bool open_ = false;
    float itemHeight_ = 28.0f;
    static constexpr int kMaxVisibleRows = 12;
};

// ---- TabControl ----
class UI_API TabControlWidget : public Widget {
public:
    struct Tab {
        std::wstring title;
        WidgetPtr    content;
    };

    void AddTab(const std::wstring& title, WidgetPtr content);
    int ActiveIndex() const { return activeIndex_; }
    void SetActiveIndex(int i);

    void OnDraw(Renderer& r) override;
    void DrawTree(Renderer& r) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    void DoLayout() override;
    D2D1_SIZE_F SizeHint() const override;

private:
    std::vector<Tab> tabs_;
    int activeIndex_ = 0;
    int hoveredTab_ = -1;
    float tabHeight_ = 36.0f;
    int TabHitTest(float x, float y) const;
};

// ---- ScrollView ----
class UI_API ScrollViewWidget : public Widget {
public:
    void SetContent(WidgetPtr content);

    float ScrollY() const { return scrollY_; }
    /* 走与鼠标滚动同一条纯偏移路径 (build 285) —— 此前只改字段不动 rect,
     * 内容要等下一次全局布局才归位, 长列表里那次布局就是每帧 O(n) 的来源。 */
    void SetScrollY(float y) {
        const float oldScroll = scrollY_;
        scrollY_ = y;
        ClampScroll();
        ApplyScrollDelta_(oldScroll);
    }

    void OnDraw(Renderer& r) override;
    void DrawTree(Renderer& r) override;
    bool OnMouseWheel(const MouseEvent& e) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    void DoLayout() override;

    bool NeedsScrollbar() const;

private:
    WidgetPtr content_;
    float scrollY_ = 0;
    float contentHeight_ = 0;
    bool draggingThumb_ = false;
    bool hoveringBar_ = false;
    float dragStartY_ = 0;
    float dragStartScroll_ = 0;
    static constexpr float kBarSpace    = 10.0f;  // 滚动条占用的固定布局宽度
    static constexpr float kThumbThin   = 5.0f;   // 默认细条
    static constexpr float kThumbWide   = 9.0f;   // hover/drag 时粗条

    float ThumbWidth() const { return (hoveringBar_ || draggingThumb_) ? kThumbWide : kThumbThin; }
    void ClampScroll();

    /* 纯滚动路径 (build 285): 只有 scrollY_ 变了、内容一个字没动时用。
     * DoLayout() 会跑两遍 content_->DoLayout() (一遍量高度一遍定位) 外加一次
     * 全子树递归 measure —— 2000 行实测 2.0ms/帧, 而滚动根本不改变任何尺寸。
     * 这里改成把整棵子树的 rect 平移 delta, 同样 O(n) 但常数小一个量级。
     *
     * 调用方: 三个鼠标 handler (滚轮 / 拖 thumb / 点轨道跳转) —— 它们按定义
     * 只改偏移。内容变化仍然走完整的 DoLayout()。 */
    void ApplyScrollDelta_(float oldScrollY);
    float VisibleHeight() const;
    D2D1_RECT_F ThumbRect() const;
};

// ---- RadioButton ----
class UI_API RadioButtonWidget : public Widget {
public:
    RadioButtonWidget(const std::wstring& text, const std::string& group)
        : text_(text), group_(group) {
        focusable = true;
        animating_ = false;
    }

    bool Selected() const { return selected_; }
    const std::wstring& Text() const { return text_; }
    void SetText(const std::wstring& t) { text_ = t; ui::RequestLayout(); }
    void SetSelected(bool v);
    // Set value without animation. Used by PageState mount-phase dispatch.
    void SetSelectedImmediate(bool v) { selected_ = v; selectAnim_.SetImmediate(selected_ ? 1.0f : 0.0f); animating_ = false; }
    const std::string& Group() const { return group_; }

    void SetAnimationDuration(float durationMs) { selectAnim_.SetDuration(durationMs); }
    float GetAnimationDuration() const { return selectAnim_.Duration(); }

    void SetEasingFunction(EasingFunction func) { selectAnim_.SetEasing(func); }
    EasingFunction GetEasingFunction() const { return selectAnim_.Easing(); }

    void UpdateAnimation();

    void OnDraw(Renderer& r) override;
    bool OnMouseUp(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    D2D1_SIZE_F SizeHint() const override;

private:
    std::wstring text_;
    std::string group_;
    bool selected_ = false;
    AnimatedFloat selectAnim_{0.0f, 200.0f, EasingFunction::EaseOutCubic};
    bool animating_ = false;

    void DeselectSiblings();
    float ContentRight_() const;

    friend class UiWindowImpl;
};

// ---- Toggle (Switch) ----
class UI_API ToggleWidget : public Widget {
public:
    explicit ToggleWidget(const std::wstring& text = L"") : text_(text) {
        focusable = true;
        animating_ = false;
        UpdateCachedColors();
    }

    bool On() const { return on_; }
    const std::wstring& Text() const { return text_; }
    void SetText(const std::wstring& t) { text_ = t; ui::RequestLayout(); }
    void SetOn(bool v);
    // Set value without animation. Used by PageState mount-phase dispatch.
    void SetOnImmediate(bool v) { on_ = v; anim_.SetImmediate(on_ ? 1.0f : 0.0f); animating_ = false; }

    void SetAnimationDuration(float durationMs) { anim_.SetDuration(durationMs); }
    float GetAnimationDuration() const { return anim_.Duration(); }

    void SetEasingFunction(EasingFunction func) { anim_.SetEasing(func); }
    EasingFunction GetEasingFunction() const { return anim_.Easing(); }

    void UpdateAnimation();
    void UpdateCachedColors();

    void OnDraw(Renderer& r) override;
    bool OnMouseUp(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    D2D1_SIZE_F SizeHint() const override;

private:
    std::wstring text_;
    bool on_ = false;
    AnimatedFloat anim_{0.0f, 200.0f, EasingFunction::EaseOutCubic};
    bool animating_ = false;

    D2D1_COLOR_F CachedTrackColorOff_;
    D2D1_COLOR_F CachedTrackColorOn_;
    D2D1_COLOR_F CachedThumbColorOff_;
    D2D1_COLOR_F CachedThumbColorOn_;
    float ContentRight_() const;

    friend class UiWindowImpl;
};

// ---- ProgressBar ----
class UI_API ProgressBarWidget : public Widget {
public:
    ProgressBarWidget(float min, float max, float value)
        : min_(min),
          max_(max),
          value_(value),
          targetValue_(value),
          valueAnim_(value, 300.0f, EasingFunction::EaseOutCubic) {
        animating_ = false;
    }

    float Value() const { return value_; }
    void SetValue(float v, bool animate = true);
    // Set value without animation. Used by PageState mount-phase dispatch.
    void SetValueImmediate(float v) { targetValue_ = std::clamp(v, min_, max_); value_ = targetValue_; valueAnim_.SetImmediate(targetValue_); animating_ = false; }
    void SetIndeterminate(bool v) { indeterminate_ = v; }
    bool IsIndeterminate() const { return indeterminate_; }

    void SetAnimationDuration(float durationMs) { valueAnim_.SetDuration(durationMs); }
    float GetAnimationDuration() const { return valueAnim_.Duration(); }

    void SetEasingFunction(EasingFunction func) { valueAnim_.SetEasing(func); }
    EasingFunction GetEasingFunction() const { return valueAnim_.Easing(); }

    void UpdateAnimation();

    void OnDraw(Renderer& r) override;
    D2D1_SIZE_F SizeHint() const override;

private:
    float min_, max_, value_, targetValue_;
    AnimatedFloat valueAnim_;
    bool animating_ = false;
    bool indeterminate_ = false;

    friend class UiWindowImpl;
};

// ---- ToolTip ----
class UI_API ToolTipWidget : public Widget {
public:
    void Show(const std::wstring& text, float x, float y);
    void Hide();
    bool IsVisible() const { return showing_; }

    void OnDraw(Renderer& r) override;

private:
    std::wstring text_;
    bool showing_ = false;
};

// ---- Overlay (modal mask / loading hint) ----
class UI_API OverlayWidget : public Widget {
public:
    explicit OverlayWidget(const std::wstring& text = L"") : text_(text) {
        expanding = true;
        enabled = false;
    }

    void SetText(const std::wstring& t) { text_ = t; }
    const std::wstring& Text() const { return text_; }

    void SetActive(bool on) {
        active_ = on;
        SyncInputCaptureState();
    }
    bool IsActive() const { return active_; }

    void SetBlockInput(bool on) {
        blockInput_ = on;
        SyncInputCaptureState();
    }
    bool BlockInput() const { return blockInput_; }

    void SetSpinner(bool on) { showSpinner_ = on; }
    bool ShowSpinner() const { return showSpinner_; }

    void SetDismissOnClick(bool on) { dismissOnClick_ = on; }
    bool DismissOnClick() const { return dismissOnClick_; }

    void OnDraw(Renderer& r) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    bool OnMouseWheel(const MouseEvent& e) override;
    D2D1_SIZE_F SizeHint() const override;

private:
    void SyncInputCaptureState() {
        enabled = active_ && blockInput_;
    }

    std::wstring text_;
    bool active_ = false;
    bool blockInput_ = true;
    bool showSpinner_ = true;
    bool dismissOnClick_ = false;
    uint64_t spinnerTick_ = 0;
};

// ---- ImageView (zoomable, pannable image canvas) ----
class UI_API ImageViewWidget : public Widget {
public:
    ImageViewWidget();
    ~ImageViewWidget();

    // Image source
    void LoadFromFile(const std::wstring& path, Renderer& r);
    void ClearImage();
    void SetBitmapFromPixels(const void* pixels, int w, int h, int stride, Renderer& r);
    void CreateEmpty(int w, int h, Renderer& r);
    void UpdateRegion(int x, int y, const void* pixels, int w, int h, int stride);
    bool HasImage() const { return bitmap_ != nullptr || bitmapResourceKey_.IsValid() || tiledMode_; }
    bool CopyPixels(void** outPixels, int* outW, int* outH) const;

    // Tiled rendering mode (CPU resource + current renderer texture)
    void SetTiled(int fullWidth, int fullHeight, int tileSize, Renderer& r);
    void SetTile(int tileX, int tileY, const void* pixels, int w, int h, int stride, Renderer& r);
    void SetTilePreview(const void* pixels, int w, int h, int stride, Renderer& r);
    void EvictTile(int tileX, int tileY);   // 删单个 tile（LRU 淘汰用）
    void ClearTiles();
    bool IsTiled() const { return tiledMode_; }

    // Zoom / pan
    float Zoom() const { return zoom_; }
    void SetZoom(float z);
    void SetPan(float x, float y);   /* 在 .cpp 实现：赋值 + NotifyViewport */
    float PanX() const { return panX_; }
    float PanY() const { return panY_; }
    void FitToView();
    void ResetView();  // 1:1 centered

    // Image info (returns effective dimensions considering rotation)
    int ImageWidth() const {
        int w = tiledMode_ ? tiledFullW_ : imgW_;
        int h = tiledMode_ ? tiledFullH_ : imgH_;
        return (rotation_ == 90 || rotation_ == 270) ? h : w;
    }
    int ImageHeight() const {
        int w = tiledMode_ ? tiledFullW_ : imgW_;
        int h = tiledMode_ ? tiledFullH_ : imgH_;
        return (rotation_ == 90 || rotation_ == 270) ? w : h;
    }
    int RawImageWidth() const { return imgW_; }
    int RawImageHeight() const { return imgH_; }

    // Rotation (0, 90, 180, 270)
    void SetRotation(int angle) { rotation_ = ((angle % 360) + 360) % 360; }
    int Rotation() const { return rotation_; }

    // Background style
    void SetCheckerboard(bool on) { checkerboard_ = on; }

    // Anti-alias: on=放大也用 LINEAR/HQ 插值平滑；off=放大走 NEAREST 锐化像素（默认）
    void SetAntialias(bool on) { antialias_ = on; }
    bool Antialias() const { return antialias_; }

    // Zoom limits
    void SetZoomRange(float minZ, float maxZ) { minZoom_ = minZ; maxZoom_ = maxZ; }

    // Loading spinner
    void SetLoading(bool on);
    bool IsLoading() const { return loading_; }

    // Animation (GIF)
    bool IsAnimated() const;
    int FrameCount() const;
    int CurrentFrame() const { return currentFrame_; }
    void StopAnimation();

    // Crop mode
    void SetCropMode(bool on);
    bool IsCropMode() const { return cropMode_; }
    void SetCropRect(float x, float y, float w, float h);  // in image pixel coords
    void GetCropRect(float& x, float& y, float& w, float& h) const;
    void SetCropAspectRatio(float ratio) { cropAspectRatio_ = ratio; }  // 0=free, e.g. 16.0f/9.0f
    void ResetCrop();  // reset to full image

    // Crop callback: fires when crop rect changes
    std::function<void(float x, float y, float w, float h)> onCropChanged;

    // Callbacks
    std::function<void(float zoom, float panX, float panY)> onViewportChanged;

    /* mouse down/move 钩子：返回 true 吞掉事件（mouse_move 钩子吞事件时 core-ui
     * 会同时结束 pan 状态，用于 pan 中途切换到"拖出"等流程）。 */
    std::function<bool(float x, float y, int btn)> onMouseDownHook;
    std::function<bool(float x, float y)>          onMouseMoveHook;

    void OnDraw(Renderer& r) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    bool OnMouseWheel(const MouseEvent& e) override;
    D2D1_SIZE_F SizeHint() const override;

private:
    ComPtr<ID2D1Bitmap> bitmap_;
    ResourceKey bitmapResourceKey_;
    uint64_t bitmapGeneration_ = 0;
    int imgW_ = 0, imgH_ = 0;
    int rotation_ = 0;  // 0, 90, 180, 270
    float zoom_ = 1.0f;
    float panX_ = 0, panY_ = 0;
    float minZoom_ = 0.01f, maxZoom_ = 64.0f;
    bool checkerboard_ = true;
    bool antialias_ = false;  /* 放大时是否抗锯齿（默认关，与 Windows 照片查看器一致） */
    bool dragging_ = false;
    float dragStartX_ = 0, dragStartY_ = 0;
    float dragPanX_ = 0, dragPanY_ = 0;

    // Crop overlay state
    bool cropMode_ = false;
    float cropX_ = 0, cropY_ = 0, cropW_ = 0, cropH_ = 0;  // image pixel coords
    float cropAspectRatio_ = 0;  // 0=free
    enum CropHandle { None, Move, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };
    CropHandle cropDragHandle_ = None;
    float cropDragStartX_ = 0, cropDragStartY_ = 0;
    float cropDragOrigX_ = 0, cropDragOrigY_ = 0, cropDragOrigW_ = 0, cropDragOrigH_ = 0;

    // Convert between screen coords and image pixel coords
    void ScreenToImage(float sx, float sy, float& ix, float& iy) const;
    void ImageToScreen(float ix, float iy, float& sx, float& sy) const;
    D2D1_RECT_F CropScreenRect() const;
    CropHandle HitTestCropHandle(float sx, float sy) const;
    void DrawCropOverlay(Renderer& r);
    void ClampCrop();
    void ClearBitmapResource();
    bool SetBitmapResourceFromPixels(const void* pixels, int w, int h, int stride,
                                     Renderer& r, bool resetAnimation);

    void DrawCheckerboard(Renderer& r, const D2D1_RECT_F& area);
    void EnsureCheckerboardTile(Renderer& r);
    void NotifyViewport();
    void ConstrainPan();

    ComPtr<ID2D1Bitmap> checkerTile_;
    ResourceKey checkerTileResourceKey_;
    uint64_t checkerTileGeneration_ = 0;
    int checkerTheme_ = -1;  /* 缓存主题，切换时重建 */

    // Loading spinner
    bool loading_ = false;
    float loadingAngle_ = 0.0f;
    UINT_PTR loadingTimerId_ = 0;
    static constexpr UINT_PTR kLoadingTimerId = 9998;

    // Tiled rendering mode
    struct TileBitmap {
        ComPtr<ID2D1Bitmap> bmp;
        ResourceKey resourceKey;
        int w = 0, h = 0;
    };
    bool tiledMode_ = false;
    int tiledFullW_ = 0, tiledFullH_ = 0;
    int tiledTileSize_ = 512;
    std::map<std::pair<int,int>, TileBitmap> tiles_;
    ComPtr<ID2D1Bitmap> tiledPreview_;  /* 全图预览，兜底未加载的区域 */
    ResourceKey tiledPreviewResourceKey_;
    uint64_t tiledGeneration_ = 0;
    int tiledPreviewW_ = 0, tiledPreviewH_ = 0;

    void DrawTiled(Renderer& r);

    // Animation (GIF) —— 按需解码：bitmap_ 是唯一 GPU 位图，timer 触发时
    // 让 player 把下一帧合成进 CPU 画布，然后 CopyFromMemory 上传。
    std::unique_ptr<Renderer::AnimatedPlayer> gif_;
    int currentFrame_ = 0;
    UINT_PTR animTimerId_ = 0;
    static constexpr UINT_PTR kAnimTimerId = 9997;
    static void CALLBACK AnimTimerProc(HWND, UINT, UINT_PTR id, DWORD);
    static void CALLBACK LoadingTimerProc(HWND, UINT, UINT_PTR id, DWORD);
    void AdvanceFrame();
    void StartAnimation();
};

// ---- IconButton (SVG icon button with ghost mode) ----
class UI_API IconButtonWidget : public Widget {
public:
    IconButtonWidget(const std::string& svgContent, bool ghost);

    void SetSvg(const std::string& svgContent);
    void SetGhost(bool ghost) { ghost_ = ghost; }
    bool IsGhost() const { return ghost_; }
    void SetIconColor(const D2D1_COLOR_F& c) { iconColor_ = c; fixedColor_ = true; }
    void SetIconColorRole(int role) { iconColorRole_ = role; fixedColor_ = false; }
    void SetIconPadding(float p) { iconPad_ = p; }
    void SetCornerRadius(float r) { cornerRadius_ = r; }
    /* ghost 模式 hover/press bg 视觉开关. 默认 true (标准按钮反馈).
     * false: ghost 下永远只画 icon, 没 hover/press bg —— 用于 titlebar
     * 装饰按钮 / 状态指示器等"不希望按钮凸出"的场景. */
    void SetHoverVisual(bool on) { hoverVisual_ = on; }
    bool HoverVisual() const     { return hoverVisual_; }

    void OnDraw(Renderer& r) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    D2D1_SIZE_F SizeHint() const override;

private:
    SvgIcon icon_;
    std::string svgContent_;
    bool ghost_ = false;
    bool fixedColor_ = false;
    int iconColorRole_ = 0; /* UiIconColorRole; 0 = UI_ICON_COLOR_BUTTON_TEXT. */
    D2D1_COLOR_F iconColor_ = {};
    float iconPad_ = 6.0f;
    float cornerRadius_ = 4.0f;
    bool iconParsed_ = false;
    bool hoverVisual_ = true;
};

// ---- TitleBar (borderless window title bar with caption buttons) ----
class UI_API TitleBarWidget : public Widget {
public:
    explicit TitleBarWidget(const std::wstring& title = L"");
    ~TitleBarWidget() override;

    void SetTitle(const std::wstring& t) { title_ = t; ui::RequestLayout(); }
    const std::wstring& Title() const { return title_; }

    // User-added widgets go into the "custom area" between title and caption buttons
    void AddCustomWidget(WidgetPtr w) { customWidgets_.push_back(w); AddChild(w); }

    // The window pointer is set by UiWindowImpl to wire caption button actions
    void* windowHandle = nullptr;  // HWND, set externally

    void OnDraw(Renderer& r) override;
    void DoLayout() override;
    D2D1_SIZE_F SizeHint() const override;

    Widget* CloseBtn() { return closeBtn_.get(); }
    Widget* MaxBtn()   { return maxBtn_.get(); }
    Widget* MinBtn()   { return minBtn_.get(); }
    void SetShowIcon(bool show)  { showIcon_ = show; }
    void SetShowMin(bool show)   { if (minBtn_)   minBtn_->visible = show; }
    void SetShowMax(bool show)   { if (maxBtn_)   maxBtn_->visible = show; }
    void SetShowClose(bool show) { if (closeBtn_) closeBtn_->visible = show; }
    void SetCustomBgColor(const D2D1_COLOR_F& c) { customBg_ = c; hasCustomBg_ = true; }

    // 显式设置图标（覆盖 EXE 自带图标）。RGBA8888，按 caller 给的 w*h 尺寸，
    // OnDraw 时懒创建 D2D 位图。pixels=nullptr 清掉用户图标，回到"自动加载
    // EXE 嵌入图标"的默认行为。
    void SetIconFromPixels(const uint8_t* rgba, int w, int h);

    // Title font weight. Default: DWRITE_FONT_WEIGHT_NORMAL (400).
    // Set to DWRITE_FONT_WEIGHT_SEMI_BOLD (600) for a heavier title (pre-1.3.1 look).
    void SetTitleWeight(int weight) { titleWeight_ = weight; ui::RequestLayout(); }
    int  TitleWeight() const { return titleWeight_; }

private:
    std::wstring title_;
    std::vector<WidgetPtr> customWidgets_;

    // Built-in caption buttons (created in constructor)
    WidgetPtr closeBtn_;
    WidgetPtr maxBtn_;
    WidgetPtr minBtn_;
    bool showIcon_ = true;
    bool hasCustomBg_ = false;
    D2D1_COLOR_F customBg_ = {};
    int titleWeight_ = 400;   // DWRITE_FONT_WEIGHT_NORMAL

    // Icon state — three-tier lookup at OnDraw time:
    //   1. userIconResourceKey_ 有效 → 用户显式设的图，最高优先
    //   2. 否则尝试从 GetModuleHandleW(nullptr) 加载 ICON 资源 ID=1
    //   3. 都没有 → 不画图标，标题文字滑到最左
    // 加载结果缓存到 iconBitmap_。HICON 加载只尝试一次（exeIconAttempted_）。
    ComPtr<ID2D1Bitmap> iconBitmap_;
    bool exeIconAttempted_ = false;
    ResourceKey userIconResourceKey_;
    ResourceKey exeIconResourceKey_;
    int userIconW_ = 0;
    int userIconH_ = 0;
};

// Internal caption button (used by TitleBarWidget)
class UI_API CaptionButtonWidget : public Widget {
public:
    std::wstring icon;
    bool isClose = false;

    void OnDraw(Renderer& r) override;
};

// ---- CustomWidget (user-defined via C callbacks) ----

// Forward-declare callback signatures (match ui_core.h typedefs)
struct UiRect_t { float left, top, right, bottom; };

class UI_API CustomWidget : public Widget {
public:
    // Callback function pointer types (plain C-compatible)
    using DrawCb       = void (*)(uint64_t w, void* ctx, UiRect_t rect, void* ud);
    using MouseCb      = int  (*)(uint64_t w, float x, float y, int btn, void* ud);
    using MouseWheelCb = int  (*)(uint64_t w, float x, float y, float delta, void* ud);
    using KeyCb        = int  (*)(uint64_t w, int vk, void* ud);
    using CharCb       = int  (*)(uint64_t w, int ch, void* ud);
    using LayoutCb     = void (*)(uint64_t w, UiRect_t rect, void* ud);

    DrawCb       drawCb       = nullptr;  void* drawUd       = nullptr;
    MouseCb      mouseDownCb  = nullptr;  void* mouseDownUd  = nullptr;
    MouseCb      mouseMoveCb  = nullptr;  void* mouseMoveUd  = nullptr;
    MouseCb      mouseUpCb    = nullptr;  void* mouseUpUd    = nullptr;
    MouseWheelCb mouseWheelCb = nullptr;  void* mouseWheelUd = nullptr;
    KeyCb        keyDownCb    = nullptr;  void* keyDownUd    = nullptr;
    CharCb       charCb       = nullptr;  void* charUd       = nullptr;
    LayoutCb     layoutCb     = nullptr;  void* layoutUd     = nullptr;

    uint64_t apiHandle = 0;  // set after Reg() for C callbacks
    bool focused = false;

    void OnDraw(Renderer& r) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    bool OnMouseWheel(const MouseEvent& e) override;
    bool OnKeyDown(int vk) override;
    bool OnKeyChar(wchar_t ch) override;
    void DoLayout() override;
    D2D1_SIZE_F SizeHint() const override;

private:
    UiRect_t ToUiRect() const {
        return {rect.left, rect.top, rect.right, rect.bottom};
    }
};

// ---- MenuBar ----
class UI_API MenuBarWidget : public Widget {
public:
    struct Menu {
        std::wstring text;
        ContextMenuPtr menu;
        float x = 0, w = 0;  // computed during layout
    };

    MenuBarWidget() { fixedH = 30.0f; }

    void AddMenu(const std::wstring& text, ContextMenuPtr menu);
    void SetHwnd(HWND hwnd) { hwnd_ = hwnd; }

    void OnDraw(Renderer& r) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    void DoLayout() override;
    D2D1_SIZE_F SizeHint() const override { return {0, fixedH}; }

    // Called by window to close open menu
    void CloseOpenMenu();
    bool HasOpenMenu() const { return openIndex_ >= 0; }

    // Callback when a menu item is clicked
    std::function<void(int)> onMenuItemClick;

private:
    std::vector<Menu> menus_;
    int hoveredIndex_ = -1;
    int openIndex_ = -1;
    HWND hwnd_ = nullptr;
    int MenuHitTest(float x, float y) const;
};

// ---- Splitter ----
class UI_API SplitterWidget : public Widget {
public:
    explicit SplitterWidget(bool vertical = false) : vertical_(vertical) {}

    float Ratio() const { return ratio_; }
    void SetRatio(float r) { ratio_ = std::clamp(r, 0.05f, 0.95f); }
    bool IsVertical() const { return vertical_; }
    bool IsDragging() const { return dragging_; }

    void OnDraw(Renderer& r) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    void DoLayout() override;

private:
    bool vertical_ = false;   // false=horizontal split (left|right), true=vertical (top|bottom)
    float ratio_ = 0.3f;
    float barSize_ = 5.0f;
    bool dragging_ = false;
    float dragOffset_ = 0;
};

// ---- Expander (collapsible section) ----
class UI_API ExpanderWidget : public Widget {
public:
    explicit ExpanderWidget(const std::wstring& header = L"");

    bool IsExpanded() const { return expanded_; }
    void SetExpanded(bool v);
    void SetExpandedImmediate(bool v) { expanded_ = v; expandAnim_.SetImmediate(v ? 1.0f : 0.0f); animating_ = false; }
    void Toggle() { SetExpanded(!expanded_); }
    void SetHeaderText(const std::wstring& t) { headerText_ = t; }
    const std::wstring& HeaderText() const { return headerText_; }

    void UpdateAnimation();

    void OnDraw(Renderer& r) override;
    void DrawTree(Renderer& r) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    void DoLayout() override;
    D2D1_SIZE_F SizeHint() const override;

    std::function<void(bool)> onExpandedChanged;

private:
    std::wstring headerText_;
    bool expanded_ = true;
    bool headerHovered_ = false;
    float headerHeight_ = 40.0f;

    AnimatedFloat expandAnim_{1.0f, 180.0f, EasingFunction::EaseOutCubic};
    bool animating_ = false;
    float measuredContentH_ = 0;  // natural height of children

    friend class UiWindowImpl;
};

// ---- NumberBox (numeric input with up/down buttons) ----
class UI_API NumberBoxWidget : public Widget {
public:
    NumberBoxWidget(float min, float max, float value, float step = 1.0f);

    float Value() const { return value_; }
    void SetValue(float v);
    void SetRange(float mn, float mx) { min_ = mn; max_ = mx; Clamp(); }
    void SetStep(float s) { step_ = s; }
    void SetDecimals(int d) { decimals_ = d; }

    void OnDraw(Renderer& r) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    bool OnKeyChar(wchar_t ch) override;
    bool OnKeyDown(int vk) override;
    bool OnMouseWheel(const MouseEvent& e) override;
    D2D1_SIZE_F SizeHint() const override;

    bool focused = false;
    bool readOnly = false;

private:
    float min_, max_, value_, step_;
    int decimals_ = 0;
    bool editing_ = false;
    std::wstring editText_;
    int cursorPos_ = 0;
    int hoveredBtn_ = -1;  // -1=none, 0=up, 1=down

    void Clamp();
    void CommitEdit();
    std::wstring FormatValue() const;
    D2D1_RECT_F UpBtnRect() const;
    D2D1_RECT_F DownBtnRect() const;
};

// ---- NavItem (WinUI 3 NavigationViewItem) ----
// A flat navigation item for use inside SplitView pane.
// Shows: [accent indicator] [icon] [label]
// When pane is compact (narrow), label is clipped and only icon shows.
class UI_API NavItemWidget : public Widget {
public:
    NavItemWidget(const std::wstring& text, const std::string& svgIcon = "");

    void SetText(const std::wstring& t) { text_ = t; }
    const std::wstring& Text() const { return text_; }
    void SetSelected(bool sel);
    bool IsSelected() const { return selected_; }
    void SetSvgIcon(const std::string& svg);
    void SetGlyph(const std::wstring& glyph) { glyph_ = glyph; }

    void OnDraw(Renderer& r) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    D2D1_SIZE_F SizeHint() const override;

private:
    std::wstring text_;
    std::wstring glyph_;       // icon font glyph (e.g., Segoe Fluent Icons)
    std::string svgContent_;
    SvgIcon svgIcon_;
    bool svgParsed_ = false;
    bool selected_ = false;
};

// ---- Flyout (popover attached to an anchor widget) ----
enum class FlyoutPlacement { Top, Bottom, Left, Right, Auto };

class UI_API FlyoutWidget : public Widget {
public:
    FlyoutWidget();

    void SetContent(WidgetPtr content) { content_ = content; AddChild(content); }
    void SetPlacement(FlyoutPlacement p) { placement_ = p; }

    void Show(Widget* anchor);
    void Hide();
    bool IsOpen() const { return open_; }

    void OnDraw(Renderer& r) override {}  // draws nothing in normal flow
    void OnDrawOverlay(Renderer& r) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    void DoLayout() override;
    D2D1_SIZE_F SizeHint() const override { return {0, 0}; }  // invisible in flow

    std::function<void()> onDismissed;

private:
    WidgetPtr content_;
    Widget* anchor_ = nullptr;
    Widget* pressedChild_ = nullptr;
    FlyoutPlacement placement_ = FlyoutPlacement::Auto;
    bool open_ = false;
    D2D1_RECT_F flyoutRect_ = {};

    D2D1_RECT_F ComputeRect() const;
};

// ---- SplitView (WinUI 3 NavigationView-style sidebar) ----
// Display modes:
//   Overlay:        pane hidden when closed, slides over content when open
//   Inline:         pane always visible side-by-side with content
//   CompactOverlay: narrow icon strip when closed, overlays content when open
//   CompactInline:  narrow icon strip when closed, pushes content when open
enum class SplitViewMode { Overlay, Inline, CompactOverlay, CompactInline };

class UI_API SplitViewWidget : public Widget {
public:
    SplitViewWidget();

    void SetPane(WidgetPtr pane) { pane_ = pane; AddChild(pane); }
    void SetContent(WidgetPtr content) { content_ = content; AddChild(content); }

    bool IsPaneOpen() const { return paneOpen_; }
    void SetPaneOpen(bool open);
    void SetPaneOpenImmediate(bool open) { paneOpen_ = open; paneAnim_.SetImmediate(open ? 1.0f : 0.0f); animating_ = false; }
    void TogglePane() { SetPaneOpen(!paneOpen_); }

    void SetDisplayMode(SplitViewMode mode) { mode_ = mode; }
    SplitViewMode DisplayMode() const { return mode_; }

    void SetOpenPaneLength(float w) { openPaneLength_ = w; }
    float OpenPaneLength() const { return openPaneLength_; }
    void SetCompactPaneLength(float w) { compactPaneLength_ = w; }
    float CompactPaneLength() const { return compactPaneLength_; }

    void UpdateAnimation();

    void OnDraw(Renderer& r) override;
    void DrawTree(Renderer& r) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    void DoLayout() override;
    D2D1_SIZE_F SizeHint() const override;

    // Called when pane open/close state changes
    std::function<void(bool)> onPaneChanged;

private:
    WidgetPtr pane_;
    WidgetPtr content_;
    SplitViewMode mode_ = SplitViewMode::CompactInline;
    bool paneOpen_ = false;
    float openPaneLength_ = 320.0f;
    float compactPaneLength_ = 48.0f;

    // Animation
    AnimatedFloat paneAnim_{0.0f, 200.0f, EasingFunction::EaseOutCubic};
    bool animating_ = false;

    float CurrentPaneWidth() const;

    friend class UiWindowImpl;
};

} // namespace ui
