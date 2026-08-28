#include "ui_context.h"
#include "debug_trace.h"
#include "overlay_service.h"
#include "render_thread.h"
#include "ui_window.h"
#include <objbase.h>

#pragma comment(lib, "windowscodecs.lib")

#ifndef SetProcessDpiAwarenessContext
static inline BOOL SetProcessDpiAwarenessContextFallback(DPI_AWARENESS_CONTEXT ctx) {
    HMODULE hModule = LoadLibraryW(L"user32.dll");
    if (hModule) {
        typedef BOOL(WINAPI* PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
        PFN_SetProcessDpiAwarenessContext pfn = (PFN_SetProcessDpiAwarenessContext)GetProcAddress(hModule, "SetProcessDpiAwarenessContext");
        if (pfn) {
            BOOL result = pfn(ctx);
            FreeLibrary(hModule);
            return result;
        }
        FreeLibrary(hModule);
    }
    return FALSE;
}
#define SetProcessDpiAwarenessContext(ctx) SetProcessDpiAwarenessContextFallback(ctx)
#endif

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 (DPI_AWARENESS_CONTEXT)(-4)
#endif

#include <windows.h>

namespace ui {

Context& GetContext() {
    static Context ctx;
    return ctx;
}

// 与 ui_context.h 注释对应：把 ctor/dtor 落到这个 TU 里，因为这里
// #include "ui_window.h" 后 UiWindowImpl 是完整类型，
// std::unique_ptr<UiWindowImpl>::~unique_ptr() 才能展开。
Context::Context() = default;
Context::~Context() {
    // Mark shutting down so any Widget destructors that run during
    // global teardown (process exits without calling ui_shutdown) skip
    // the NotifyWidgetDestroyed → window lookup, which would otherwise
    // touch already-destroyed map entries.
    shuttingDown_ = true;
}

bool Context::Init() {
    if (initialized_) return true;

    HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(coHr) && coHr != RPC_E_CHANGED_MODE) return false;
    comInitialized_ = SUCCEEDED(coHr);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                    __uuidof(ID2D1Factory1),
                                    reinterpret_cast<void**>(d2dFactory_.GetAddressOf()));
    if (FAILED(hr)) {
        if (comInitialized_) {
            CoUninitialize();
            comInitialized_ = false;
        }
        return false;
    }

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                             __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(dwFactory_.GetAddressOf()));
    if (FAILED(hr)) {
        if (comInitialized_) {
            CoUninitialize();
            comInitialized_ = false;
        }
        return false;
    }

    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(wicFactory_.GetAddressOf()));
    if (FAILED(hr)) {
        if (comInitialized_) {
            CoUninitialize();
            comInitialized_ = false;
        }
        return false;
    }

    initialized_ = true;
    return true;
}

uint64_t Context::RegisterMenu(ContextMenuPtr menu) {
    uint64_t id = nextMenuId_++;
    menus_[id] = std::move(menu);
    return id;
}

ContextMenuPtr Context::GetMenu(uint64_t id) const {
    auto it = menus_.find(id);
    return (it != menus_.end()) ? it->second : nullptr;
}

void Context::RemoveMenu(uint64_t id) {
    menus_.erase(id);
}

void Context::Shutdown() {
    if (shuttingDown_) return;
    shuttingDown_ = true;

    menus_.clear();
    windows_.clear();
    OverlayService::Instance().Stop();
    RenderThread::Instance().Stop();
    handles.Clear();
    wicFactory_.Reset();
    dwFactory_.Reset();
    d2dFactory_.Reset();
    initialized_ = false;
    if (comInitialized_) {
        CoUninitialize();
        comInitialized_ = false;
    }
    shuttingDown_ = false;
}

uint64_t Context::RegisterWindow(std::unique_ptr<UiWindowImpl> win) {
    uint64_t id = nextWindowId_++;
    windows_[id] = std::move(win);
    return id;
}

UiWindowImpl* Context::GetWindow(uint64_t id) const {
    auto it = windows_.find(id);
    return (it != windows_.end()) ? it->second.get() : nullptr;
}

UiWindowImpl* Context::FirstWindow() const {
    for (const auto& [id, win] : windows_) {
        if (win) return win.get();
    }
    return nullptr;
}

UiWindowImpl* Context::FindWindowByWidget(Widget* w) {
    if (!w) return nullptr;
    /* 走到 root — Parent() 链终点. */
    Widget* root = w;
    while (root->Parent()) root = root->Parent();
    /* 在窗口表里反查哪个 window 持这个 root. */
    for (auto& [id, win] : windows_) {
        if (win && win->Root().get() == root) return win.get();
    }
    return nullptr;
}

void Context::RemoveWindow(uint64_t id) {
    windows_.erase(id);
}

void Context::InvalidateAllWindows() {
    int invalidated = 0;
    int skippedHidden = 0;
    for (auto& [id, win] : windows_) {
        if (!win) continue;
        HWND hwnd = win->Handle();
        if (hwnd && (!IsWindowVisible(hwnd) || IsIconic(hwnd))) {
            ++skippedHidden;
            continue;
        }
        win->Invalidate();
        ++invalidated;
    }
    if (IsTraceEnabled()) {
        TraceEvent("core_context", "invalidate_all_windows",
                   {TraceI64("invalidated", invalidated),
                    TraceI64("skipped_hidden", skippedHidden)});
    }
}

void Context::RelayoutAllWindows() {
    for (auto& [id, win] : windows_) {
        if (!win) continue;
        win->LayoutRoot();
        HWND hwnd = win->Handle();
        if (hwnd && (!IsWindowVisible(hwnd) || IsIconic(hwnd))) continue;
        win->Invalidate();
    }
}

void Context::BeginBatch() {
    ++batchDepth_;
}

void Context::EndBatch() {
    if (batchDepth_ > 0) --batchDepth_;
    if (batchDepth_ != 0) return;

    /* 先清 flag 再执行 —— 执行过程中若又触发 Request*, 此时已不在批量里,
     * 会立即执行, 不会丢。 */
    const bool doInvalidate = batchPendingInvalidate_;
    const bool doAnimTimers = batchPendingAnimTimers_;
    batchPendingInvalidate_ = false;
    batchPendingAnimTimers_ = false;
    if (doInvalidate) InvalidateAllWindows();
    if (doAnimTimers) UpdateAnimTimers();
}

void Context::RequestInvalidateAll() {
    if (batchDepth_ > 0) { batchPendingInvalidate_ = true; return; }
    InvalidateAllWindows();
}

void Context::RequestAnimTimerUpdate() {
    if (batchDepth_ > 0) { batchPendingAnimTimers_ = true; return; }
    UpdateAnimTimers();
}

void Context::UpdateAnimTimers() {
    for (auto& [id, win] : windows_) {
        if (win) win->UpdateToggleAnimTimer();
    }
}

void Context::RegisterAnimatingWidget(Widget* w, AnimationInvalidation invalidation) {
    if (auto* win = FindWindowByWidget(w)) {
        win->RegisterAnimatingWidget(w, invalidation);
        return;
    }

    UpdateAnimTimers();
}

void Context::NotifyWidgetDestroyed(Widget* w) {
    if (!w) return;
    // During global teardown the windows_ map may already be partly
    // destroyed; widgets in-flight inside ~UiWindowImpl() shouldn't
    // try to ping back through here. The shuttingDown_ flag is set at
    // the top of Shutdown(), before windows_.clear() runs.
    if (shuttingDown_) return;
    // O(n_windows) per widget destruction. Typical app has 1-2 windows;
    // a v-for tear-down with N rows is N × n_windows, which is fine.
    for (auto& [id, win] : windows_) {
        if (win) win->NotifyWidgetDestroyed(w);
    }
}

// ---- Persistent C-callback registry --------------------------------------

void Context::SetClickCallback(const std::string& id, std::function<void()> cb) {
    if (id.empty()) return;
    if (cb) widgetCallbacks_[id].onClick = std::move(cb);
    else if (auto it = widgetCallbacks_.find(id); it != widgetCallbacks_.end()) it->second.onClick = nullptr;
}

void Context::SetValueCallback(const std::string& id, std::function<void(bool)> cb) {
    if (id.empty()) return;
    if (cb) widgetCallbacks_[id].onValueChanged = std::move(cb);
    else if (auto it = widgetCallbacks_.find(id); it != widgetCallbacks_.end()) it->second.onValueChanged = nullptr;
}

void Context::SetTextCallback(const std::string& id, std::function<void(const std::wstring&)> cb) {
    if (id.empty()) return;
    if (cb) widgetCallbacks_[id].onTextChanged = std::move(cb);
    else if (auto it = widgetCallbacks_.find(id); it != widgetCallbacks_.end()) it->second.onTextChanged = nullptr;
}

void Context::SetFloatCallback(const std::string& id, std::function<void(float)> cb) {
    if (id.empty()) return;
    if (cb) widgetCallbacks_[id].onFloatChanged = std::move(cb);
    else if (auto it = widgetCallbacks_.find(id); it != widgetCallbacks_.end()) it->second.onFloatChanged = nullptr;
}

void Context::RebindWidgetCallbacks(Widget* root) {
    if (!root || widgetCallbacks_.empty()) return;
    std::function<void(Widget*)> walk = [&](Widget* w) {
        if (!w) return;
        if (!w->id.empty()) {
            auto it = widgetCallbacks_.find(w->id);
            if (it != widgetCallbacks_.end()) {
                if (it->second.onClick)         w->onClick         = it->second.onClick;
                if (it->second.onValueChanged)  w->onValueChanged  = it->second.onValueChanged;
                if (it->second.onTextChanged)   w->onTextChanged   = it->second.onTextChanged;
                if (it->second.onFloatChanged)  w->onFloatChanged  = it->second.onFloatChanged;
            }
        }
        for (auto& c : w->Children()) walk(c.get());
    };
    walk(root);
}

} // namespace ui
