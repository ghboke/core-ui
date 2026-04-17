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

#include "widget.h"
#include <unordered_map>
#include <cstdint>

namespace ui {

class UI_API HandleTable {
public:
    uint64_t Insert(std::shared_ptr<Widget> widget);
    std::shared_ptr<Widget> Lookup(uint64_t handle) const;
    Widget* LookupRaw(uint64_t handle) const;
    bool Remove(uint64_t handle);
    void Clear();

    // Reverse lookup: find the handle for a given widget pointer
    uint64_t FindHandle(const Widget* widget) const;

private:
    std::unordered_map<uint64_t, std::shared_ptr<Widget>> handles_;
    uint64_t nextId_ = 1;
};

} // namespace ui
