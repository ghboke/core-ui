#pragma once
#include "widget.h"
#include "renderer.h"
#include "theme.h"
#include "context_menu.h"
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace ui {

// ---- Easing Functions ----
enum class EasingFunction {
    Linear,
    EaseInQuad,
    EaseOutQuad,
    EaseInOutQuad,
    EaseInCubic,
    EaseOutCubic,
    EaseInOutCubic,
    EaseInElastic,
    EaseOutElastic,
    EaseInBounce,
    EaseOutBounce,
    EaseOutBack
};

// ---- Label ----
class UI_API LabelWidget : public Widget {
public:
    explicit LabelWidget(const std::wstring& text) : text_(text) {}

    WidgetPtr FontSize(float s) override { fontSize_ = s; return shared_from_this(); }
    WidgetPtr Bold() override { bold_ = true; return shared_from_this(); }
    WidgetPtr TextColor(const D2D1_COLOR_F& c) override { color_ = c; customColor_ = true; return shared_from_this(); }
    WidgetPtr Align(int a) override { align_ = (DWRITE_TEXT_ALIGNMENT)a; return shared_from_this(); }

    void SetText(const std::wstring& t) { text_ = t; }
    const std::wstring& Text() const { return text_; }
    void SetWrap(bool w) { wrap_ = w; }
    void SetMaxLines(int n) { maxLines_ = n; }

    // Dynamic text color (for theme-aware colors)
    void SetTextColorFn(std::function<D2D1_COLOR_F()> fn) { textColorFn_ = std::move(fn); customColor_ = true; }

    void OnDraw(Renderer& r) override;
    D2D1_SIZE_F SizeHint() const override;

private:
    std::wstring text_;
    float fontSize_ = theme::kFontSizeNormal;
    bool bold_ = false;
    bool wrap_ = false;
    int maxLines_ = 0;  /* 0 = 不限制 */
    bool customColor_ = false;
    D2D1_COLOR_F color_ = {};
    std::function<D2D1_COLOR_F()> textColorFn_;
    DWRITE_TEXT_ALIGNMENT align_ = DWRITE_TEXT_ALIGNMENT_LEADING;
};

// ---- Button ----
// Button type: "default" = standard gray, "primary" = accent filled
enum class ButtonType { Default, Primary };

class UI_API ButtonWidget : public Widget {
public:
    explicit ButtonWidget(const std::wstring& text) : text_(text) { focusable = true; }

    WidgetPtr FontSize(float s) override { fontSize_ = s; return shared_from_this(); }

    void SetText(const std::wstring& t) { text_ = t; }
    void SetIcon(const std::wstring& icon) { icon_ = icon; }
    const std::wstring& Text() const { return text_; }
    void SetType(ButtonType t) { type_ = t; }
    ButtonType Type() const { return type_; }
    void SetTextColor(const D2D1_COLOR_F& c) { customTextColor_ = c; hasCustomTextColor_ = true; }
    void SetCustomBgColor(const D2D1_COLOR_F& c) { customBgColor_ = c; hasCustomBgColor_ = true; }

    void OnDraw(Renderer& r) override;
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
        checkAnimProgress_ = 0.0f;
        lastTick_ = 0;
        animating_ = false;
        animDurationMs_ = 200.0f;
        easingFunc_ = EasingFunction::EaseOutCubic;
    }

    bool Checked() const { return checked_; }
    const std::wstring& Text() const { return text_; }
    void SetText(const std::wstring& t) { text_ = t; }
    void SetChecked(bool v);
    void SetCheckedImmediate(bool v) { checked_ = v; checkAnimProgress_ = checked_ ? 1.0f : 0.0f; animating_ = false; }

    void SetAnimationDuration(float durationMs) { animDurationMs_ = durationMs; }
    float GetAnimationDuration() const { return animDurationMs_; }

    void SetEasingFunction(EasingFunction func) { easingFunc_ = func; }
    EasingFunction GetEasingFunction() const { return easingFunc_; }

    void UpdateAnimation();

    void OnDraw(Renderer& r) override;
    bool OnMouseUp(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    D2D1_SIZE_F SizeHint() const override;

private:
    std::wstring text_;
    bool checked_ = false;
    float checkAnimProgress_ = 0.0f;
    uint64_t lastTick_ = 0;
    bool animating_ = false;
    float animDurationMs_ = 200.0f;
    EasingFunction easingFunc_ = EasingFunction::EaseOutCubic;
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

    void OnDraw(Renderer& r) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    D2D1_SIZE_F SizeHint() const override;

private:
    float min_, max_, value_;
    bool dragging_ = false;
    float ValueFromX(float x) const;

    // Thumb scale animation: smoothly transitions between rest/hover/press sizes
    float thumbScaleCurrent_ = 0.86f;   // current animated scale
    float thumbScaleTarget_  = 0.86f;   // target scale for current state
    uint64_t thumbAnimLastTick_ = 0;
    static constexpr float kThumbAnimSpeed = 8.0f;  // ~120ms for full transition

    friend class UiWindowImpl;
};

// ---- Panel (container with background) ----
class UI_API PanelWidget : public Widget {
public:
    PanelWidget() { bgColor = {0, 0, 0, 0}; }
    explicit PanelWidget(const D2D1_COLOR_F& bg) { bgColor = bg; }
    // bgColorFn inherited from Widget — theme-aware bg works on all widgets now
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
    Renderer* cachedRenderer_ = nullptr;

    int CharIndexFromX(float x) const;
    void DeleteSelection();
    bool HasSelection() const { return selectionStart_ >= 0 && selectionEnd_ >= 0 && selectionStart_ != selectionEnd_; }
    void ClearSelection() { selectionStart_ = selectionEnd_ = -1; }
    void EnsureCursorVisible();
    std::wstring GetSelectedText() const;
    void SetClipboardText(const std::wstring& text);
    std::wstring GetClipboardText();
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

    void OnDraw(Renderer& r) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    bool OnMouseWheel(const MouseEvent& e) override;
    bool OnKeyChar(wchar_t ch) override;
    bool OnKeyDown(int vk) override;
    D2D1_SIZE_F SizeHint() const override;

    bool focused = false;
    bool readOnly = false;
    std::function<bool(wchar_t)> inputFilter;
    int maxLength = -1;

    bool NeedsScrollbar() const;

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
    Renderer* cachedRenderer_ = nullptr;
    float lineHeight_ = 20.0f;
    static constexpr float kScrollBarWidth = 4.0f;

    float ContentHeight() const;
    float VisibleHeight() const;
    D2D1_RECT_F ThumbRect() const;
    void ClampScroll();

    std::vector<std::wstring> GetLines() const;
    int GetLineFromPos(int pos) const;
    int GetPosFromLineCol(int line, int col) const;
    void GetLineCol(int pos, int& line, int& col) const;
    int CharIndexFromXY(float x, float y) const;
    void DeleteSelection();
    bool HasSelection() const { return selectionStart_ >= 0 && selectionEnd_ >= 0 && selectionStart_ != selectionEnd_; }
    void ClearSelection() { selectionStart_ = selectionEnd_ = -1; }
    void EnsureCursorVisible();
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
    D2D1_SIZE_F SizeHint() const override;

    std::function<void(int)> onSelectionChanged;

    float ItemHeight() const { return itemHeight_; }
    int ItemCount() const { return (int)items_.size(); }
    D2D1_RECT_F DropdownRect() const;

private:
    std::vector<std::wstring> items_;
    int selectedIndex_ = 0;
    int hoveredIndex_ = -1;
    bool open_ = false;
    float itemHeight_ = 28.0f;
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
    void SetScrollY(float y) { scrollY_ = y; ClampScroll(); }

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
    float VisibleHeight() const;
    D2D1_RECT_F ThumbRect() const;
};

// ---- RadioButton ----
class UI_API RadioButtonWidget : public Widget {
public:
    RadioButtonWidget(const std::wstring& text, const std::string& group)
        : text_(text), group_(group) {
        focusable = true;
        selectAnimProgress_ = 0.0f;
        lastTick_ = 0;
        animating_ = false;
        animDurationMs_ = 200.0f;
        easingFunc_ = EasingFunction::EaseOutCubic;
    }

    bool Selected() const { return selected_; }
    const std::wstring& Text() const { return text_; }
    void SetText(const std::wstring& t) { text_ = t; }
    void SetSelected(bool v);
    void SetSelectedImmediate(bool v) { selected_ = v; selectAnimProgress_ = selected_ ? 1.0f : 0.0f; animating_ = false; }
    const std::string& Group() const { return group_; }

    void SetAnimationDuration(float durationMs) { animDurationMs_ = durationMs; }
    float GetAnimationDuration() const { return animDurationMs_; }

    void SetEasingFunction(EasingFunction func) { easingFunc_ = func; }
    EasingFunction GetEasingFunction() const { return easingFunc_; }

    void UpdateAnimation();

    void OnDraw(Renderer& r) override;
    bool OnMouseUp(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    D2D1_SIZE_F SizeHint() const override;

private:
    std::wstring text_;
    std::string group_;
    bool selected_ = false;
    float selectAnimProgress_ = 0.0f;
    uint64_t lastTick_ = 0;
    bool animating_ = false;
    float animDurationMs_ = 200.0f;
    EasingFunction easingFunc_ = EasingFunction::EaseOutCubic;

    void DeselectSiblings();
    float ContentRight_() const;

    friend class UiWindowImpl;
};

// ---- Toggle (Switch) ----
class UI_API ToggleWidget : public Widget {
public:
    explicit ToggleWidget(const std::wstring& text = L"") : text_(text) {
        focusable = true;
        animProgress_ = 0.0f;
        lastTick_ = 0;
        animating_ = false;
        animDurationMs_ = 200.0f;
        easingFunc_ = EasingFunction::EaseOutCubic;
        CachedTrackColorOff_ = theme::kInputBorder();
        CachedTrackColorOn_ = theme::kAccent();
        CachedThumbColorOff_ = theme::kBtnText();
        CachedThumbColorOn_ = D2D1_COLOR_F{1, 1, 1, 1};
    }

    bool On() const { return on_; }
    const std::wstring& Text() const { return text_; }
    void SetText(const std::wstring& t) { text_ = t; }
    void SetOn(bool v);
    void SetOnImmediate(bool v) { on_ = v; animProgress_ = on_ ? 1.0f : 0.0f; animating_ = false; }

    void SetAnimationDuration(float durationMs) { animDurationMs_ = durationMs; }
    float GetAnimationDuration() const { return animDurationMs_; }

    void SetEasingFunction(EasingFunction func) { easingFunc_ = func; }
    EasingFunction GetEasingFunction() const { return easingFunc_; }

    void UpdateAnimation();
    void UpdateCachedColors();

    static float ApplyEasing(EasingFunction func, float t);

    void OnDraw(Renderer& r) override;
    bool OnMouseUp(const MouseEvent& e) override;
    bool OnMouseMove(const MouseEvent& e) override;
    D2D1_SIZE_F SizeHint() const override;

private:
    std::wstring text_;
    bool on_ = false;
    float animProgress_ = 0.0f;
    uint64_t lastTick_ = 0;
    bool animating_ = false;
    float animDurationMs_ = 200.0f;
    EasingFunction easingFunc_ = EasingFunction::EaseOutCubic;

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
        : min_(min), max_(max), value_(value), targetValue_(value) {
        animProgress_ = value;
        lastTick_ = 0;
        animating_ = false;
        animDurationMs_ = 300.0f;
        easingFunc_ = EasingFunction::EaseOutCubic;
    }

    float Value() const { return value_; }
    void SetValue(float v, bool animate = true);
    void SetValueImmediate(float v) { targetValue_ = std::clamp(v, min_, max_); value_ = targetValue_; animProgress_ = targetValue_; animating_ = false; }
    void SetIndeterminate(bool v) { indeterminate_ = v; }
    bool IsIndeterminate() const { return indeterminate_; }

    void SetAnimationDuration(float durationMs) { animDurationMs_ = durationMs; }
    float GetAnimationDuration() const { return animDurationMs_; }

    void SetEasingFunction(EasingFunction func) { easingFunc_ = func; }
    EasingFunction GetEasingFunction() const { return easingFunc_; }

    void UpdateAnimation();

    void OnDraw(Renderer& r) override;
    D2D1_SIZE_F SizeHint() const override;

private:
    float min_, max_, value_, targetValue_;
    float animProgress_ = 0.0f;
    uint64_t lastTick_ = 0;
    bool animating_ = false;
    float animDurationMs_ = 300.0f;
    EasingFunction easingFunc_ = EasingFunction::EaseOutCubic;
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

// ---- Dialog (modal confirm/alert dialog) ----
class UI_API DialogWidget : public Widget {
public:
    using ResultCallback = std::function<void(bool confirmed)>;

    DialogWidget() {
        expanding = true;
        enabled = false;
        visible = false;  // hidden until Show()
    }

    void SetTitle(const std::wstring& t) { title_ = t; }
    void SetMessage(const std::wstring& m) { message_ = m; }
    void SetOkText(const std::wstring& t) { okText_ = t; }
    void SetCancelText(const std::wstring& t) { cancelText_ = t; }
    void SetShowCancel(bool show) { showCancel_ = show; }
    void SetCallback(ResultCallback cb) { callback_ = std::move(cb); }

    void Show(const std::wstring& title, const std::wstring& message, ResultCallback cb);
    void Hide();
    bool IsActive() const { return active_; }

    void OnDraw(Renderer& r) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    bool OnMouseWheel(const MouseEvent& e) override;
    bool OnKeyDown(int vk) override;
    D2D1_SIZE_F SizeHint() const override;

private:
    D2D1_RECT_F BtnRect(int idx) const;  // 0=ok, 1=cancel
    int HitBtn(float x, float y) const;  // -1=none

    std::wstring title_;
    std::wstring message_;
    std::wstring okText_ = L"确定";
    std::wstring cancelText_ = L"取消";
    bool showCancel_ = true;
    bool active_ = false;
    ResultCallback callback_;

    int hoveredBtn_ = -1;
    int pressedBtn_ = -1;
public:
    std::function<void()> onHide_;  // called when dialog hides itself
private:

    // layout cache (computed in OnDraw)
    mutable D2D1_RECT_F panelRect_ = {};
};

// ---- ImageView (zoomable, pannable image canvas) ----
class UI_API ImageViewWidget : public Widget {
public:
    ImageViewWidget();

    // Image source
    void LoadFromFile(const std::wstring& path, Renderer& r);
    void SetBitmap(ComPtr<ID2D1Bitmap> bmp);
    void SetBitmapFromPixels(const void* pixels, int w, int h, int stride, Renderer& r);
    void CreateEmpty(int w, int h, Renderer& r);
    void UpdateRegion(int x, int y, const void* pixels, int w, int h, int stride);
    bool HasImage() const { return bitmap_ != nullptr || tiledMode_; }
    ID2D1Bitmap* GetBitmap() const { return bitmap_.Get(); }

    // Tiled rendering mode (each tile = independent GPU texture)
    void SetTiled(int fullWidth, int fullHeight, int tileSize, Renderer& r);
    void SetTile(int tileX, int tileY, const void* pixels, int w, int h, int stride, Renderer& r);
    void SetTilePreview(ComPtr<ID2D1Bitmap> bmp, int w, int h);
    void ClearTiles();
    bool IsTiled() const { return tiledMode_; }

    // Zoom / pan
    float Zoom() const { return zoom_; }
    void SetZoom(float z);
    void SetPan(float x, float y) { panX_ = x; panY_ = y; }
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

    /* mouse down/move 钩子：返回 true 吞掉事件（mouse_move 钩子吞事件时 ui-core
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

    void DrawCheckerboard(Renderer& r, const D2D1_RECT_F& area);
    void EnsureCheckerboardTile(Renderer& r);
    void NotifyViewport();
    void ConstrainPan();

    ComPtr<ID2D1Bitmap> checkerTile_;
    int checkerTheme_ = -1;  /* 缓存主题，切换时重建 */

    // Loading spinner
    bool loading_ = false;
    float loadingAngle_ = 0.0f;
    UINT_PTR loadingTimerId_ = 0;
    static constexpr UINT_PTR kLoadingTimerId = 9998;

    // Tiled rendering mode
    struct TileBitmap {
        ComPtr<ID2D1Bitmap> bmp;
        int w = 0, h = 0;
    };
    bool tiledMode_ = false;
    int tiledFullW_ = 0, tiledFullH_ = 0;
    int tiledTileSize_ = 512;
    std::map<std::pair<int,int>, TileBitmap> tiles_;
    ComPtr<ID2D1Bitmap> tiledPreview_;  /* 全图预览，兜底未加载的区域 */
    int tiledPreviewW_ = 0, tiledPreviewH_ = 0;

    void DrawTiled(Renderer& r);

    // Animation (GIF) —— 按需解码：bitmap_ 是唯一 GPU 位图，timer 触发时
    // 让 player 把下一帧合成进 CPU 画布，然后 CopyFromMemory 上传。
    std::unique_ptr<Renderer::AnimatedPlayer> gif_;
    int currentFrame_ = 0;
    UINT_PTR animTimerId_ = 0;
    static constexpr UINT_PTR kAnimTimerId = 9997;
    static void CALLBACK AnimTimerProc(HWND, UINT, UINT_PTR id, DWORD);
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
    void SetIconColor(const D2D1_COLOR_F& c) { iconColor_ = c; customColor_ = true; }
    void SetIconPadding(float p) { iconPad_ = p; }
    void SetCornerRadius(float r) { cornerRadius_ = r; }

    void OnDraw(Renderer& r) override;
    bool OnMouseMove(const MouseEvent& e) override;
    bool OnMouseDown(const MouseEvent& e) override;
    bool OnMouseUp(const MouseEvent& e) override;
    D2D1_SIZE_F SizeHint() const override;

private:
    SvgIcon icon_;
    std::string svgContent_;
    bool ghost_ = false;
    bool customColor_ = false;
    D2D1_COLOR_F iconColor_ = {};
    float iconPad_ = 6.0f;
    float cornerRadius_ = 4.0f;
    bool iconParsed_ = false;
};

// ---- TitleBar (borderless window title bar with caption buttons) ----
class UI_API TitleBarWidget : public Widget {
public:
    explicit TitleBarWidget(const std::wstring& title = L"");

    void SetTitle(const std::wstring& t) { title_ = t; }
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
    void SetExpandedImmediate(bool v) { expanded_ = v; animProgress_ = v ? 1.0f : 0.0f; animating_ = false; }
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

    float animProgress_ = 1.0f;  // 0=collapsed, 1=expanded
    bool animating_ = false;
    uint64_t animLastTick_ = 0;
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
    Renderer* cachedRenderer_ = nullptr;

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
    void SetPaneOpenImmediate(bool open) { paneOpen_ = open; animProgress_ = open ? 1.0f : 0.0f; animating_ = false; }
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
    float animProgress_ = 0.0f;   // 0=closed, 1=open
    bool animating_ = false;
    uint64_t animLastTick_ = 0;

    float CurrentPaneWidth() const;

    friend class UiWindowImpl;
};

} // namespace ui
