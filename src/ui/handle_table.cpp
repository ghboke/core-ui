#include "handle_table.h"

namespace ui {

uint64_t HandleTable::Insert(std::shared_ptr<Widget> widget) {
    if (!widget) return 0;
    uint64_t id = nextId_++;
    handles_[id] = std::move(widget);
    return id;
}

std::shared_ptr<Widget> HandleTable::Lookup(uint64_t handle) const {
    auto it = handles_.find(handle);
    if (it != handles_.end()) return it->second;
    return nullptr;
}

Widget* HandleTable::LookupRaw(uint64_t handle) const {
    auto it = handles_.find(handle);
    if (it != handles_.end()) return it->second.get();
    return nullptr;
}

bool HandleTable::Remove(uint64_t handle) {
    return handles_.erase(handle) > 0;
}

void HandleTable::Clear() {
    handles_.clear();
}

uint64_t HandleTable::FindHandle(const Widget* widget) const {
    for (auto& [id, w] : handles_) {
        if (w.get() == widget) return id;
    }
    return 0;
}

} // namespace ui
