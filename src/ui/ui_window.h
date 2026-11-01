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
#include <memory>
#include <string>
#include <vector>

#include "renderer.h"
#include "measure_context.h"
#include "widget.h"
#include "event.h"
#include "context_menu.h"
#include "animation.h"
#include "animation_host.h"
#include "display_list.h"
#include "frame_scheduler.h"
#include "render_window.h"

namespace ui {


class UI_API UiWindowImpl {
public:
    UiWindowImpl();
    ~UiWindowImpl();

    bool Create(const wchar_t* title, int width, int height,
                bool borderless, bool resizable, bool acceptFiles,
                int x = CW_USEDEFAULT, int y = CW_USEDEFAULT,
                bool toolWindow = false,
                HWND ownerHwnd = nullptr);  /* Build 65+ (L14) */
    void Show();
    void ShowImmediate(bool activate = true);  /* 跳过开场动画 */
    /* PrepareRT 已完成 frame 确立 (premax 的 SW_SHOWMAXIMIZED 或普通分支的
     * SWP_FRAMECHANGED) — ShowImmediate 跳过重复的 SetWindowPos(FRAMECHANGED)
     * (~10ms DWM 事务)。未走 prepare 的独立调用方仍执行, 行为不变。 */
    bool framePrepared_ = false;
    void SetIconFromPixels(const uint8_t* rgba, int w, int h);
    void PrepareRT();      /* 预创建渲染目标 */
    void Hide();
    void Invalidate();
    void InvalidateNow();
    void BeginCanvasVisualTransaction();
    void EndCanvasVisualTransaction();
    void SetRoot(WidgetPtr root);
    void SetTitle(const std::wstring& title);
    HWND Handle() const { return hwnd_; }
    Renderer& GetRenderer() { return renderer_; }

    // Window ID in the context registry
    uint64_t windowId = 0;
    void RegisterRenderWindow(uint64_t id);

    // Callbacks
    std::function<void()> onClose;
    std::function<bool()> onCloseRequest;
    std::function<void(int, int)> onResize;
    std::function<void(int, int)> onPageResize;
    std::function<void(const std::wstring&)> onDrop;
    std::function<void(int)> onKey;
    std::function<void(float, float)> onRightClick;
    /* L297: 中键 / 侧键 X1 / X2 按下。参数为 Win32 VK 码 + 客户区 DIP 坐标。 */
    std::function<void(int, float, float)> onMouseButton;
    std::function<void(const MenuClickInfo*)> onMenuItemClick;  // 点击项 id+属性载荷
    /* L219: 标题栏背景被"拖动"(按下后超过系统拖动阈值)时触发。仅当
     * titlebarDragIntercept_=true(宿主开启, 如全屏态)时拦截 —— 此时不进系统移动
     * 循环, lib 自己跟踪位移, 超阈值才 fire(纯点击不 fire)。宿主用它在全屏拖标题栏
     * 时退出全屏。intercept=false 时标题栏拖动走正常系统移动(拖窗)。 */
    std::function<void()> onTitleBarDrag;
    bool titlebarDragIntercept_ = false;
    bool tbDragTracking_ = false;
    POINT tbDragStartScreen_{};

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
    // anim: 0=slide(默认, 旧行为), 1=fade(纯渐入渐出, y 不变)
    void ShowToast(const std::wstring& text, int durationMs = 2000, int position = 0, int icon = 0, int anim = 0);
    // Toast 由 OverlayService 的独立窗口线程承载, 主窗只发请求/销毁归属弹层.

    static bool RegisterWindowClass();

    // Dialog (public for C API access)
    void LayoutRoot();
    WidgetPtr Root() const { return root_; }
    bool skipOpenAnimation_ = false;
    /* Build 105+ (L25): config.start_maximized 状态. ui_window_create 拷,
     * Show / ShowImmediate 消费一次 (走 SW_SHOWMAXIMIZED 路径) 后清零, 不
     * 残留影响后续 ShowWindow(SW_HIDE)+Show() 循环. */
    bool startMaximizedPending_ = false;
    /* config.copy_swap_chain: 建窗即定的交换链类型 (复制模式), 见 RegisterRenderWindow。 */
    bool copySwapChain_ = false;
    /* config.layered_canvas: 分层画布窗口 —— WS_EX_LAYERED, 渲染线程离屏绘制读回,
     * UI 线程 UpdateLayeredWindow 一次提交位置 + 尺寸 + 画面 (见 layered_frame.h)。
     * 建窗即定; 不走开场动画 / 透明度动画 (SetLayeredWindowAttributes 会让 ULW 失效)。 */
    bool layeredCanvas_ = false;
    /* 测试 / 诊断用: 分层画布窗口的帧缓冲 (非分层窗口为空)。 */
    const std::shared_ptr<LayeredFrameBuffer>& LayeredFrames() const { return layeredFrames_; }

    // Public so Context::UpdateAnimTimers can fire it after a binding
    // application flips a widget into animating_=true outside an event handler.
    void UpdateToggleAnimTimer();
    void RegisterAnimatingWidget(Widget* w,
                                 AnimationInvalidation invalidation = AnimationInvalidation::Paint);
    void AnimateProperty(Widget* target, AnimProperty prop, float from, float to,
                         float durationMs = 300.0f,
                         EasingFunction easing = EasingFunction::EaseOutCubic,
                         std::function<void()> onComplete = nullptr);
    // 掐掉 target 上驱动 prop 的那条动画 (其他属性不受影响)。
    // 任何"绕过动画直接写属性值"的路径都必须先调它, 否则在飞的动画下一帧会把
    // 值覆盖回去 —— 表现成"最后一次写丢了"(下游 GuoheView bug-070)。
    void CancelPropertyAnimation(Widget* target, AnimProperty prop);

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void ActivateForMouseInput();
    DisplayList OnPaint(uint64_t frameGeneration = 0);
    bool HasPendingPaint() const;
    void RequestRenderFrame(FrameReason reason, PresentPolicy policy = PresentPolicy::Deferred);
    void FlushRenderFrameNow(FrameReason reason, PresentPolicy policy);
    void SubmitFrameJob(const FrameRequest& frame, DisplayList displayList);
    void ActivateRenderThreadPresent();
    bool WaitForRenderGeneration(const FrameRequest& frame, bool force, bool prepared = false);
    void SubmitRenderThreadFrameNow(FrameReason reason, PresentPolicy policy);
    bool FlushVisualTransactionFrame(bool resizeDirty, bool paintDirty, bool final = false,
                                     bool deferPresent = false);
    void PrepareDeferredVisualWindowRect(int xScreen, int yScreen, int wDip, int hDip,
                                         int wPx, int hPx);
    bool WaitForVisualTransactionDwmBarrier(uint64_t generation);
    void CommitDeferredVisualWindowRect();
    bool PresentPreparedVisualWindowFrame(uint64_t generation);
    void BeginAnimationFrame();
    void EnsureAnimationTimer();
    void StopAnimationTimer();
    /* 动画心跳订阅按原因计: 控件动画 / 窗口开关动画各占一位, 全部清掉才真正退订,
     * 免得一方结束把另一方的心跳也停了。 */
    enum TickReason : uint32_t { kTickWidgets = 1u << 0, kTickWindowAnim = 1u << 1 };
    void SetTickReason(uint32_t reason, bool on);
    void StepWindowOpenAnimation();
    void StepWindowCloseAnimation();
    void RequestInputPaint();
    bool HasAnimationFrameWork() const;
    void PaintAndValidate();
    void OnResize(UINT width, UINT height);
    void OnDpiChanged(UINT dpi, const RECT* suggested);
    void RefreshClientSizeCache();
    /* 按 borderless_ / resizable_ 设 Win11 圆角偏好 (见 window_corner.h)。 */
    void ApplyCornerPreference();
    void UpdateClientSizeCache(UINT widthPx, UINT heightPx);
    D2D1_SIZE_F CachedClientSizeDip() const;
    void OnMouseMove(float x, float y);
    void OnMouseDown(float x, float y);
    void OnMouseUp(float x, float y);
    /* 鼠标 capture 被外部夺走 (WM_CAPTURECHANGED, 典型 DoDragDrop 起拖) 时调:
     * 复位 press 中的 widget (清 pressedWidget_ + OnMouseUp 取消其 drag 状态),
     * 但不 fire onClick. 否则 widget 收不到 WM_LBUTTONUP 卡在 drag 态, 后续
     * hover 仍被路由 OnMouseMove → 误拖/误 pan.
     * build 308 起也用于"松开丢在窗口外": 没抓 capture 的控件 (TextArea 等)
     * 按下后移出窗口松开, 窗口收不到 WM_LBUTTONUP; 下一条真 WM_MOUSEMOVE 不带
     * MK_LBUTTON 时由 WndProc 调本函数补一次取消。 */
    void CancelMouseCapture();
    void OnMouseDoubleClick(float x, float y);
    void OnMouseWheel(float x, float y, int delta);
    void OnDropFiles(HDROP hDrop);
    LRESULT OnNcCalcSize(WPARAM wParam, LPARAM lParam);
    LRESULT OnNcHitTest(int x, int y);
    void OnGetMinMaxInfo(MINMAXINFO* mmi);
    /* 跨屏最大化兜底: 最大化态窗口矩形超出当前屏工作区就纠回。
     * 返 true = 已发起纠正 (真正的 layout 在嵌套 WM_SIZE 里做)。 */
    bool ClampMaximizedRectToWorkArea();
    bool IsCanvasDragHit(int sx, int sy) const;
    void UpdateDwmFrameMargins();
    void NotifyResizeCallback(UINT width, UINT height);
    bool HasFocusedTextInput() const;
    void UpdateCaretBlinkTimer();
    void StartWindowOpenAnimation();
    void StartWindowCloseAnimation();
    void UnregisterRenderWindow();
    /* 分层画布窗口: 显示 (先画好并提交一帧再 SW_SHOWNA) / 提交前台帧 /
     * 视觉事务收尾 (帧准备好后一次 ULW 带上新位置)。 */
    void ShowLayeredCanvas(bool activate);
    bool CommitLayeredFrame(const POINT* screenPos);
    void EndLayeredCanvasTransaction(bool resizeDirty, bool paintDirty);

    HWND        hwnd_ = nullptr;
    HICON       hIcon_ = nullptr;
    bool        borderless_ = false;
    bool        resizable_ = true;
    bool        canvasMode_ = false;
    bool        toolWindow_ = false;
    bool        maximized_ = false;
    /* ClampMaximizedRectToWorkArea 的重入闸: 纠正用的 SetWindowPos 会同步
     * 递归回 WM_SIZE, 不闸住会无限套娃。 */
    bool        clampingMaximizedRect_ = false;
    int         configWidth_ = 0, configHeight_ = 0;
    /* 用户覆盖的最小窗口尺寸（0 = 用 theme::kMin* 默认值）。
     * 无边框画布模式下可以小于默认。 */
    int         minWOverride_ = 0, minHOverride_ = 0;
    /* build 328: 高度跟随内容。applying 闸住 SetWindowPos → WM_SIZE → LayoutRoot
     * 的同步递归, 递归那趟只排版不再改尺寸。 */
    bool        autoHeight_ = false;
    bool        autoHeightApplying_ = false;
    bool        ApplyAutoHeight();
    /* 背景模式：0=主题色填充（默认），1=透明/不擦背景（widget 自己全覆盖画）。
     * 无边框画布模式下设 1，避免 SetWindowPos 扩窗口时的背景闪烁。 */
    int         bgMode_ = 0;
    /* L57: aspect ratio lock — 用户拖窗 resize 时按这个比例锁另一边. 0/0
     * = disable (默认). 看图器 borderless 模式 enter 时设 = (image_w, image_h),
     * exit 时清零. WM_SIZING 收到 user 拖出的 RECT 后, 按 ratio 算合法 size
     * 写回, Win32 把这个 RECT 当 user 实际拖的 size. */
    int         aspectLockW_ = 0, aspectLockH_ = 0;
    /* build 321: 分层画布窗口透明底 (清屏用 alpha 0, 透明像素鼠标点穿)。 */
    bool        layeredTransparent_ = false;
    /* build 321: 缩放边框内缩 (dip)。窗口四周留透明边距 (画阴影) 时, resize 感应带
     * 落在内缩后的内容矩形边缘; aspect lock 也按内容矩形算。边距里其余区域不响应。 */
    float       resizeInset_ = 0.0f;
    UINT        dpi_ = 96;
    float       dpiScale_ = 1.0f;
    UINT        clientWidthPx_ = 0;
    UINT        clientHeightPx_ = 0;
    float       clientWidthDip_ = 0.0f;
    float       clientHeightDip_ = 0.0f;
    std::wstring title_;
    uint64_t    hwndGeneration_ = 0;
    RenderWindowId renderWindowId_{};
    bool        renderThreadPresentActive_ = false;
    std::shared_ptr<LayeredFrameBuffer> layeredFrames_;
    /* ULW 提交期间 (会同步收到 WM_SIZE): 画面已是新尺寸, 不再另请一帧。 */
    bool        layeredCommitting_ = false;
    uint64_t    lastDeviceRecoveryGeneration_ = 0;

    Renderer    renderer_;
    FrameScheduler frameScheduler_;
    MeasureContext measureContext_;
    WidgetPtr   root_;
    uint64_t    layoutGenSeen_ = 0;   // build 306: 已处理到的排版请求代号 (见 ui::LayoutGeneration)
    Widget*     hoveredWidget_ = nullptr;
    Widget*     pressedWidget_ = nullptr;
    /* build 308: 当前这次按下来自 SimMouseDown (调试管道 / 测试注入)。注入的按下
     * 没有真实左键, 期间物理鼠标划过窗口产生的 WM_MOUSEMOVE 不带 MK_LBUTTON,
     * 不能据此判"松开丢失"把注入的拖动取消掉。 */
    bool        pressFromSim_ = false;
    ContextMenuPtr activeMenu_;

    void     DestroyToast();

    bool        startupRevealPending_ = false;
    bool        startupRevealPosted_ = false;
    bool        caretBlinkTimerRunning_ = false;
    uint32_t    tickReasons_ = 0;   // TickReason 位; 非 0 = 已订阅 FrameTicker
    /* 鼠标移动重绘合并 (RequestInputPaint): 已约好 vblank 前那一帧 / 有未画的变化 /
     * 上一帧提交的 QPC 时刻。 */
    bool        inputPaintPending_ = false;
    bool        inputPaintDirty_ = false;
    int         inputMoveDepth_ = 0;     // >0: 正在处理鼠标移动, Invalidate 只记脏
    int64_t     inputMoveStartQpc_ = 0;  // 最外层这次移动开始处理的时刻 (判嵌套消息循环)
    int64_t     inputPaintDeadline_ = 0; // 已约好那一帧的时刻 (过点太久没到就重新约)
    bool        testIsolated_ = false;   // 建窗时 UI_CORE_TEST_ISOLATE=1: 丢弃真实鼠标消息
    void BeginInputMove() {
        if (inputMoveDepth_++ == 0) {
            LARGE_INTEGER q;
            QueryPerformanceCounter(&q);
            inputMoveStartQpc_ = q.QuadPart;
        }
    }
    int64_t     lastSubmitQpc_ = 0;
    bool        renderFramePosted_ = false;
    AnimationHost animationHost_;
    AnimationManager propertyAnimations_;
    uint64_t    lastAnimationFrameTick_ = 0;   // QPC 计数, 只给 trace 量帧间隔
    bool        animFrameFromTimer_ = false;   // 本次绘制是动画定时器触发的 (trace 用)
    bool        moveFromSim_ = false;          // 调试模拟的移动: 不登记 WM_MOUSELEAVE 跟踪

    // Drag & drop (HTML5-style; see Widget draggable/onDrop* hooks)
    Widget*     dragSource_ = nullptr;      // draggable widget under the press
    Widget*     dragOverTarget_ = nullptr;  // drop target currently hovered
    bool        dragActive_ = false;        // press exceeded the drag threshold
    float       dragStartX_ = 0, dragStartY_ = 0;
    float       dragX_ = 0, dragY_ = 0;     // current drag cursor pos (ghost)
    std::wstring dragPayload_;
    std::wstring dragGhostText_;  // ghost pill 显示文本: 源 Label 文本优先, 回落 payload
    static constexpr float kDragThresholdDip = 4.0f;
    void UpdateDragOverTarget(const MouseEvent& e);
    void FinishDrag(bool dropped, const MouseEvent& e);
    /* 拖放贴边自动滚动 (build 303, 对齐浏览器): 光标停在可滚动 ScrollView 的
     * 上/下边缘带里时按 dragscroll::kTickMs 节拍持续滚动, 每拍重算落点。 */
    bool        dragAutoScrollTimer_ = false;
    void UpdateDragAutoScroll();   // 拖动中调用: 需要滚就开表, 不需要就停
    void TickDragAutoScroll();     // 定时器节拍: 滚一步 + 刷新 dragover
    void StopDragAutoScroll();
    /* 按下 / 起拖时拿系统鼠标捕获。注入的按下 (pressFromSim_) 不拿: 输入不经系统,
     * 拿了没用, 反倒在用户点别处时收到 WM_CAPTURECHANGED 把模拟操作取消。 */
    void CaptureForPress() { if (!pressFromSim_ && hwnd_) SetCapture(hwnd_); }

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
    int         visualUpdateDepth_ = 0;
    bool        visualPaintDirty_ = false;
    bool        visualResizeDirty_ = false;
    bool        deferNextFramePresent_ = false;
    uint64_t    deferredPresentGeneration_ = 0;
    bool        deferredPresentPrepared_ = false;
    bool        deferredVisualWindowRectPending_ = false;
    int         deferredVisualWindowXScreen_ = 0;
    int         deferredVisualWindowYScreen_ = 0;
    int         deferredVisualWindowWDip_ = 0;
    int         deferredVisualWindowHDip_ = 0;
    int         deferredVisualWindowWPx_ = 0;
    int         deferredVisualWindowHPx_ = 0;
    bool        preparedVisualResizeNotified_ = false;
    UINT        preparedVisualResizeWPx_ = 0;
    UINT        preparedVisualResizeHPx_ = 0;
    bool        canvasDragTracking_ = false;
    POINT       canvasDragStartScreen_{};
    RECT        canvasDragStartRect_{};

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
    /* Screenshot a sub-region (in DIP). region.right/bottom are exclusive.
     * Empty / invalid region → falls back to full window. */
    int  ScreenshotRegion(D2D1_RECT_F region, const wchar_t* outPath);

    /* ---- Debug event simulation (DIP coordinates) ---- */
    /* 这些方法走和真实 Win32 消息一样的路径（命中测试、焦点、下拉、Flyout…）， */
    /* 用于自动化测试和 pipe 命令；内部会把 DIP 乘以 dpiScale_ 后调用私有 handler。 */
    void SimMouseMove(float dipX, float dipY);
    void SimMouseDown(float dipX, float dipY);
    void SimMouseUp(float dipX, float dipY);
    void SimMouseWheel(float dipX, float dipY, float delta);
    /* 走真实的 WM_LBUTTONDBLCLK 路径 (OnMouseDoubleClick → @dblclick hook +
     * Widget::OnMouseDoubleClick 冒泡)。连点两次 SimMouseDown/Up 不等价 ——
     * 那条路只触发 onClick, 永远进不了双击分支。 */
    void SimMouseDoubleClick(float dipX, float dipY);
    void SimRightClick(float dipX, float dipY);
    void DispatchContextMenu(float dipX, float dipY);
    /* IME: 把系统输入法 composition/候选窗钉到焦点控件光标处
     * (WM_IME_STARTCOMPOSITION / WM_IME_COMPOSITION 时调用). */
    void UpdateImeCompositionPos();
    /* IME 组字进行中 (START..ENDCOMPOSITION 之间)。组字期间的按键属于输入法
     * (删组字串/翻页/选字), 不得再转发给焦点控件 — 否则退格会同时删掉已落定
     * 的文本。 */
    bool imeComposing_ = false;
    void SimKeyDown(int vk);
    void SimKeyChar(wchar_t ch);

    /* 共用的键盘分发（Tab / 快捷键 / Enter/Space 激活 / 方向键 Slider/Radio / Esc 关 ComboBox）
       返回 true 表示事件被消费。WM_KEYDOWN 和 SimKeyDown 都走这里。 */
    bool DispatchKeyDown(int vk);

    /* 在 UI 线程上同步执行 fn(ud)。跨线程调用时内部用 SendMessageW；
       已在 UI 线程时直接调用。返回前 fn 必已执行完成。 */
    void InvokeSync(void (*fn)(void* ud), void* ud);

    /* 无边框画布模式相关：覆盖最小尺寸 / 背景擦除行为。0 = 恢复默认。 */
    void SetMinSize(int wDip, int hDip) { minWOverride_ = wDip; minHOverride_ = hDip; }
    /* build 328: <window height="auto"> / ui_window_set_auto_height。开启后每次
     * LayoutRoot 都把客户区高度定成根节点 HeightForWidth(当前宽度)。开启时立即
     * 按当前内容定一次, 宿主在 show 前就能读到最终尺寸算居中。 */
    void SetAutoHeight(bool on);
    bool AutoHeight() const { return autoHeight_; }
    void SetBackgroundMode(int mode)    { bgMode_ = mode; }
    int  BackgroundMode() const         { return bgMode_; }
    /* L57: 锁定窗口 aspect 比例. 0/0 = disable. 看 aspectLockW_/H_ 注释. */
    void SetAspectLock(int ratioW, int ratioH) { aspectLockW_ = ratioW; aspectLockH_ = ratioH; }
    void SetLayeredTransparent(bool on);
    void SetResizeInset(float dip) { resizeInset_ = dip > 0.0f ? dip : 0.0f; }

    /* 窗口几何（DIP-native，内部乘 dpiScale_ 转物理像素）。
     * x/y 是屏幕物理坐标（Win32 惯例）；w/h 是 DIP（按当前 DPI 换算）。
     * 包含一次 SetWindowPos + InvalidateRect + UpdateWindow，确保 resize 后
     * 同帧内把 widget 内容画到新尺寸，减少扩大窗口时的背景闪烁。 */
    void SetWindowRect(int xScreen, int yScreen, int wDip, int hDip);
    /* 宽高也是物理像素 (非整数缩放下 DIP 取整到不了某些像素尺寸)。 */
    void SetWindowRectPx(int xScreen, int yScreen, int wPx, int hPx);
    void SetWindowSize(int wDip, int hDip);             /* 保持位置不变 */
    void SetWindowPosition(int xScreen, int yScreen);   /* 保持尺寸不变 */
    void GetWindowRectScreen(int* x, int* y, int* wDip, int* hDip) const;
    int  Dpi() const;                                   /* GetDpiForWindow 值, 96/120/144/... */
    /* 以 client(ax_dip, ay_dip) 点与屏幕(sx, sy) 对齐的方式 resize。 */
    void ResizeWithAnchor(int wDip, int hDip,
                          float anchorClientXDip, float anchorClientYDip,
                          int anchorScreenX, int anchorScreenY);
    /* 一键开/关无边框画布模式（见 ui_core.h 中的 ui_window_enable_canvas_mode）。 */
    void EnableCanvasMode(bool enable);

    /* 运行时切换窗口边框：
     *   frameless=true  → 无系统边框 / 标题栏（需 HTML / 代码里自己提供 TitleBar）
     *   frameless=false → 系统原生边框 + 标题栏 + 最小化 / 最大化 / 关闭
     * 内部用 SetWindowLongPtr 改 GWL_STYLE 再 SetWindowPos(SWP_FRAMECHANGED) 刷新。 */
    void SetFrameless(bool frameless);
    bool IsFrameless() const { return borderless_; }
    void SetResizable(bool resizable);

    /* Active context menu popup (nullptr if none open) */
    ContextMenuPtr ActiveMenu() const { return activeMenu_; }

    /* Focused widget accessor for debug API */
    Widget* FocusedWidget() const { return focusedWidget_; }

    /* build 174: 编程式设焦点 + 亮焦点环 (键盘导航可见)。供 ui_window_focus_widget
     * C API (msgbox 初始焦点落在按钮上 + 方向键移焦点用)。w=null 清焦点。 */
    void FocusWidget(Widget* w);

    // Called from ui::Context::NotifyWidgetDestroyed when a widget dies
    // unexpectedly (v-for iter destroyed while cursor on it).
    void NotifyWidgetDestroyed(class Widget* w);

    /* DPI scale for coordinate conversion */
    float DpiScale() const { return dpiScale_; }
};

} // namespace ui
