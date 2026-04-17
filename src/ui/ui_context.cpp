#include "ui_context.h"
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

void Context::RemoveWindow(uint64_t id) {
    windows_.erase(id);
}

void Context::InvalidateAllWindows() {
    for (auto& [id, win] : windows_) {
        if (win) win->Invalidate();
    }
}

} // namespace ui
