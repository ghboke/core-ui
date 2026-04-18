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

#include <windows.h>
#include <shellapi.h>
#include <cstdint>
#include <functional>
#include <string>

#include "renderer.h"
#include "widget.h"
#include "event.h"
#include "context_menu.h"

namespace ui {

class DialogWidget;  // forward declaration

class UI_API UiWindowImpl {
public:
    UiWindowImpl();
    ~UiWindowImpl();

    bool Create(const wchar_t* title, int width, int height,
                bool borderless, bool resizable, bool acceptFiles,
                int x = CW_USEDEFAULT, int y = CW_USEDEFAULT,
                bool toolWindow = false);
    void Show();
    void ShowImmediate();  /* 跳过开场动画 */
    void SetIconFromPixels(const uint8_t* rgba, int w, int h);
    void PrepareRT();      /* 预创建渲染目标 */
    void Hide();
    void Invalidate();
    void SetRoot(WidgetPtr root);
    void SetTitle(const std::wstring& title);
    HWND Handle() const { return hwnd_; }
    Renderer& GetRenderer() { return renderer_; }

    // Window ID in the context registry
    uint64_t windowId = 0;

    // Callbacks
    std::function<void()> onClose;
    std::function<void(int, int)> onResize;
    std::function<void(const std::wstring&)> onDrop;
    std::function<void(int)> onKey;
    std::function<void(float, float)> onRightClick;
    std::function<void(int)> onMenuItemClick;

    // Focus management
    Widget* focusedWidget_ = nullptr;
    bool showFocusRing_ = false;   // only show focus ring when navigating via keyboard
    bool tabNavigationEnabled_ = false; // must be explicitly enabled (e.g. from .ui file)
    void FocusNext(bool reverse = false);
    void SetFocus(Widget* w);
    void ClearFocus();

    // Shortcut system
    struct Shortcut { int modifiers; int vk; std::function<void()> callback; };
    std::vector<Shortcut> shortcuts_;
    void RegisterShortcut(int modifiers, int vk, std::function<void()> cb);

    // Context menu
    void ShowMenu(ContextMenuPtr menu, float x, float y);
    void CloseMenu();

    // Toast notification
    // position: 0=top, 1=center, 2=bottom
    // icon: 0=none, 1=success(green✓), 2=error(red✕), 3=warning(yellow⚠)
    void ShowToast(const std::wstring& text, int durationMs = 2000, int position = 0, int icon = 0);
    void DrawToast(Renderer& r);

    static bool RegisterWindowClass();

    // Dialog (public for C API access)
    DialogWidget* activeDialog_ = nullptr;
    void LayoutRoot();
    WidgetPtr Root() const { return root_; }
    bool skipOpenAnimation_ = false;

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void OnPaint();
    void OnResize(UINT width, UINT height);
    void OnDpiChanged(UINT dpi, const RECT* suggested);
    void OnMouseMove(float x, float y);
    void OnMouseDown(float x, float y);
    void OnMouseUp(float x, float y);
    void OnMouseWheel(float x, float y, int delta);
    void OnDropFiles(HDROP hDrop);
    LRESULT OnNcCalcSize(WPARAM wParam, LPARAM lParam);
    LRESULT OnNcHitTest(int x, int y);
    void OnGetMinMaxInfo(MINMAXINFO* mmi);
    bool HasFocusedTextInput() const;
    void UpdateCaretBlinkTimer();

    void UpdateToggleAnimTimer();
    void StartWindowOpenAnimation();
    void StartWindowCloseAnimation();

    // Window drag cache
    void CreateDragCache();
    void ReleaseDragCache();

    HWND        hwnd_ = nullptr;
    HICON       hIcon_ = nullptr;
    bool        borderless_ = false;
    bool        resizable_ = true;
    bool        toolWindow_ = false;
    bool        maximized_ = false;
    int         configWidth_ = 0, configHeight_ = 0;
    UINT        dpi_ = 96;
    float       dpiScale_ = 1.0f;
    std::wstring title_;

    Renderer    renderer_;
    WidgetPtr   root_;
    Widget*     hoveredWidget_ = nullptr;
    Widget*     pressedWidget_ = nullptr;
    ContextMenuPtr activeMenu_;

    // Toast
    std::wstring toastText_;
    float toastAlpha_ = 0.0f;
    float toastSlide_ = 0.0f;  // 0=隐藏位置, 1=完全显示
    int   toastPos_ = 0;       // 0=top, 1=center, 2=bottom
    int   toastPhase_ = 0;     // 0=idle, 1=slideIn, 2=hold, 3=slideOut
    int   toastIcon_ = 0;     // 0=none, 1=success, 2=error, 3=warning
    UINT_PTR toastTimerId_ = 0;
    bool  toastFading_ = false;
    int   holdDurationMs_ = 2000;
    int   holdElapsed_ = 0;

    bool        startupRevealPending_ = false;
    bool        startupRevealPosted_ = false;
    bool        caretBlinkTimerRunning_ = false;
    bool        toggleAnimTimerRunning_ = false;

    // Tooltip
    Widget*     tooltipWidget_ = nullptr;
    DWORD       hoverStartTick_ = 0;
    float       tooltipX_ = 0, tooltipY_ = 0;
    float       mouseX_ = 0, mouseY_ = 0;
    bool        tooltipVisible_ = false;
    UINT_PTR    tooltipTimerId_ = 0;
    static constexpr DWORD kTooltipDelayMs = 500;
    static constexpr UINT_PTR kTooltipTimerId = 9999;

    bool        windowAnimating_ = false;
    bool        windowClosing_ = false;
    bool        isMoving_ = false;   // 窗口正在移动/调整大小
    bool        isResizing_ = false; // 本次 sizemove 是 resize（非纯移动）

    // Window drag cache (避免拖动时掉帧)
    Microsoft::WRL::ComPtr<ID2D1Bitmap> dragCacheBitmap_;
    float       dragCacheDpi_ = 96.0f;
    float       dragCacheWidth_ = 0.0f;
    float       dragCacheHeight_ = 0.0f;

    float       windowAnimProgress_ = 0.0f;
    LARGE_INTEGER windowAnimStartTick_ = {};   // 动画起始时间（高精度）
    float       windowTargetWidth_ = 0.0f;
    float       windowTargetHeight_ = 0.0f;
    int         windowTargetX_ = 0;
    int         windowTargetY_ = 0;
    int         windowCloseStartX_ = 0;
    int         windowCloseStartY_ = 0;
    static constexpr float kWindowOpenAnimDurationMs = 180.0f;
    static constexpr float kWindowCloseAnimDurationMs = 180.0f;

    static bool classRegistered_;

public:
    /* Debug highlight */
    std::string debugHighlightId_;
    void SetDebugHighlight(const char* widgetId);
    int  Screenshot(const wchar_t* outPath);

    /* ---- Debug event simulation (DIP coordinates) ---- */
    /* 这些方法走和真实 Win32 消息一样的路径（命中测试、焦点、下拉、Flyout…）， */
    /* 用于自动化测试和 pipe 命令；内部会把 DIP 乘以 dpiScale_ 后调用私有 handler。 */
    void SimMouseMove(float dipX, float dipY);
    void SimMouseDown(float dipX, float dipY);
    void SimMouseUp(float dipX, float dipY);
    void SimMouseWheel(float dipX, float dipY, float delta);
    void SimRightClick(float dipX, float dipY);
    void SimKeyDown(int vk);
    void SimKeyChar(wchar_t ch);

    /* 共用的键盘分发（Tab / 快捷键 / Enter/Space 激活 / 方向键 Slider/Radio / Esc 关 ComboBox）
       返回 true 表示事件被消费。WM_KEYDOWN 和 SimKeyDown 都走这里。 */
    bool DispatchKeyDown(int vk);

    /* 在 UI 线程上同步执行 fn(ud)。跨线程调用时内部用 SendMessageW；
       已在 UI 线程时直接调用。返回前 fn 必已执行完成。 */
    void InvokeSync(void (*fn)(void* ud), void* ud);

    /* Active context menu popup (nullptr if none open) */
    ContextMenuPtr ActiveMenu() const { return activeMenu_; }

    /* Focused widget accessor for debug API */
    Widget* FocusedWidget() const { return focusedWidget_; }

    /* DPI scale for coordinate conversion */
    float DpiScale() const { return dpiScale_; }
};

} // namespace ui
