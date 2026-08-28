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

#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <unordered_map>
#include <memory>
#include <cstdint>

#include "animation_host.h"
#include "handle_table.h"
#include "context_menu.h"

using Microsoft::WRL::ComPtr;

namespace ui {

class UiWindowImpl;  // forward

class UI_API Context {
public:
    // ctor/dtor 必须显式声明在这里 + 定义在 ui_context.cpp。否则编译器会在
    // 每个包含本头的 TU 里自动展开 windows_ 的析构 →
    // std::unique_ptr<UiWindowImpl>::~unique_ptr() 要求 UiWindowImpl 完整类型，
    // 而本文件只 forward-declare 了它（MSVC/clang-cl 严格，会报
    // "invalid application of 'sizeof' to incomplete type"）。
    Context();
    ~Context();
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    bool Init();
    void Shutdown();
    bool IsShuttingDown() const { return shuttingDown_; }

    // Shared COM factories
    ID2D1Factory1*      D2DFactory()  { return d2dFactory_.Get(); }
    IDWriteFactory*     DWFactory()   { return dwFactory_.Get(); }
    IWICImagingFactory* WICFactory()  { return wicFactory_.Get(); }

    // Handle table for widgets
    HandleTable handles;

    // Menu registry
    uint64_t RegisterMenu(ContextMenuPtr menu);
    ContextMenuPtr GetMenu(uint64_t id) const;
    void RemoveMenu(uint64_t id);

    // Window registry
    uint64_t RegisterWindow(std::unique_ptr<UiWindowImpl> win);
    UiWindowImpl* GetWindow(uint64_t id) const;
    UiWindowImpl* FirstWindow() const;
    // 给定子 widget, 找它所属的 window. 走 parent 链到 root, 在 windows_ 表里
    // 反查哪个 window 的 root 是它. 找不到 (widget 不在树里 / root 还没 attach)
    // 返 nullptr. 用于把"widget 级状态变化"投影到"window 级动作", 例如
    // ui_custom_set_focused 需要调 owner window 的 SetFocus.
    UiWindowImpl* FindWindowByWidget(class Widget* w);
    void RemoveWindow(uint64_t id);
    void InvalidateAllWindows();
    // Re-run layout (LayoutRoot) + repaint on every window. Use after a bulk
    // change that alters widgets' intrinsic sizes but goes through a path that
    // only repaints — e.g. PageState::SetLocale swaps every $t() label's text,
    // which changes their measured widths; without a relayout the labels keep
    // their previously-laid-out rects and longer translations wrap/overflow.
    void RelayoutAllWindows();
    // Re-evaluates per-window animation timers (toggle/checkbox/etc.). Needed
    // whenever a binding application or external setter flips a widget into
    // an animating state outside an event handler — without this, the timer
    // never starts and the animation flag is set but nobody ticks it.
    void UpdateAnimTimers();
    void RegisterAnimatingWidget(
        class Widget* w,
        AnimationInvalidation invalidation = AnimationInvalidation::Paint);
    bool HasWindows() const { return !windows_.empty(); }

    /* ---- 批量绑定合批 (build 285) ---------------------------------------
     * 背景: PageState::ApplyBindingToWidget 每应用一条绑定就
     * InvalidateAllWindows() + UpdateAnimTimers()。后者会**遍历整棵 widget 树**
     * 并对每个节点做 6 次以上 dynamic_cast —— 单次是 O(树大小)。
     * v-for 一次性建 2000 行时要应用约 16000 条绑定, 树里又正好有约 12000 个
     * widget, 于是整体退化成 O(n^2): 实测 2000 行 8.9 秒, 5000 行 56 秒。
     *
     * 合批: 批量期间只记"待办", 批结束统一各做一次。语义不变 —— 中途没有
     * 任何一帧会上屏。
     *
     * 用 BatchScope 而不要手工配对 Begin/End: 调用点有多处 early return。 */
    void BeginBatch();
    void EndBatch();
    bool InBatch() const { return batchDepth_ > 0; }
    /* 批量期间记待办, 否则立即执行。 */
    void RequestInvalidateAll();
    void RequestAnimTimerUpdate();

    struct BatchScope {
        Context& ctx;
        explicit BatchScope(Context& c) : ctx(c) { ctx.BeginBatch(); }
        ~BatchScope() { ctx.EndBatch(); }
        BatchScope(const BatchScope&) = delete;
        BatchScope& operator=(const BatchScope&) = delete;
    };

    // Called by Widget destructor so windows can null out any cached
    // raw pointer to the dying widget (hovered / pressed / focused /
    // tooltip). Avoids UAF when v-for / v-if destroys an iteration
    // while the cursor is still on it.
    void NotifyWidgetDestroyed(class Widget* w);

    // ---- Persistent C-callback registry, keyed by widget HTML id ----
    // ui_widget_on_click and friends record the callback here in addition to
    // setting it on the widget instance. When v-if/v-for tears the widget
    // down and re-mounts a fresh one with the same id, the runtime calls
    // RebindWidgetCallbacks(newWidget) to copy the entry back onto the new
    // instance. Without this the C handler dies with the old widget.
    struct WidgetCallbacks {
        std::function<void()>                       onClick;
        std::function<void(bool)>                   onValueChanged;
        std::function<void(const std::wstring&)>    onTextChanged;
        std::function<void(float)>                  onFloatChanged;
    };
    void SetClickCallback(const std::string& id, std::function<void()> cb);
    void SetValueCallback(const std::string& id, std::function<void(bool)> cb);
    void SetTextCallback(const std::string& id, std::function<void(const std::wstring&)> cb);
    void SetFloatCallback(const std::string& id, std::function<void(float)> cb);
    // Walk a widget subtree and re-attach any persistent callbacks. Called
    // by page_state v-if mount and v-for iteration build paths.
    void RebindWidgetCallbacks(class Widget* root);

private:
    ComPtr<ID2D1Factory1>      d2dFactory_;
    ComPtr<IDWriteFactory>     dwFactory_;
    ComPtr<IWICImagingFactory> wicFactory_;

    std::unordered_map<uint64_t, std::unique_ptr<UiWindowImpl>> windows_;
    uint64_t nextWindowId_ = 1;

    /* 批量绑定合批 (build 285) — 见上方 BeginBatch 注释 */
    int  batchDepth_             = 0;
    bool batchPendingInvalidate_ = false;
    bool batchPendingAnimTimers_ = false;

    // Persistent widget callback registry — survives v-if/v-for remount.
    std::unordered_map<std::string, WidgetCallbacks> widgetCallbacks_;

    std::unordered_map<uint64_t, ContextMenuPtr> menus_;
    uint64_t nextMenuId_ = 1;

    bool initialized_ = false;
    bool comInitialized_ = false;
    bool shuttingDown_ = false;
};

// Global context singleton
UI_API Context& GetContext();

} // namespace ui
