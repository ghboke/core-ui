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

#include "handle_table.h"
#include "context_menu.h"

using Microsoft::WRL::ComPtr;

namespace ui {

class UiWindowImpl;  // forward

class UI_API Context {
public:
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
    void RemoveWindow(uint64_t id);
    void InvalidateAllWindows();
    bool HasWindows() const { return !windows_.empty(); }

private:
    ComPtr<ID2D1Factory1>      d2dFactory_;
    ComPtr<IDWriteFactory>     dwFactory_;
    ComPtr<IWICImagingFactory> wicFactory_;

    std::unordered_map<uint64_t, std::unique_ptr<UiWindowImpl>> windows_;
    uint64_t nextWindowId_ = 1;

    std::unordered_map<uint64_t, ContextMenuPtr> menus_;
    uint64_t nextMenuId_ = 1;

    bool initialized_ = false;
    bool comInitialized_ = false;
    bool shuttingDown_ = false;
};

// Global context singleton
UI_API Context& GetContext();

} // namespace ui
