#pragma once
#include "markup_parser.h"
#include "../widget.h"
#include <functional>
#include <map>
#include <variant>
#include <string>

namespace ui {

// ---- Binding value type ----
using BindingValue = std::variant<bool, int, float, std::wstring>;

// ---- Binding context: push values to bound widgets ----
class BindingContext {
public:
    using SetterFn = std::function<void(const BindingValue&)>;

    void Register(const std::string& name, SetterFn fn);
    void Set(const std::string& name, const BindingValue& val);

    void SetBool(const std::string& name, bool v)             { Set(name, BindingValue(v)); }
    void SetInt(const std::string& name, int v)                { Set(name, BindingValue(v)); }
    void SetFloat(const std::string& name, float v)            { Set(name, BindingValue(v)); }
    void SetText(const std::string& name, const std::wstring& v) { Set(name, BindingValue(v)); }

    // List data for Repeater
    using ListItem = std::map<std::string, std::string>;
    void SetList(const std::string& name, const std::vector<ListItem>& items) { lists_[name] = items; }
    const std::vector<ListItem>* GetList(const std::string& name) const {
        auto it = lists_.find(name);
        return it != lists_.end() ? &it->second : nullptr;
    }

private:
    std::multimap<std::string, SetterFn> bindings_;
    std::map<std::string, std::vector<ListItem>> lists_;
};

// ---- Handler map: named event handlers ----
class HandlerMap {
public:
    void SetClick(const std::string& name, std::function<void()> fn);
    void SetValue(const std::string& name, std::function<void(bool)> fn);
    void SetFloat(const std::string& name, std::function<void(float)> fn);
    void SetSelection(const std::string& name, std::function<void(int)> fn);
    void SetString(const std::string& name, std::function<void(const std::wstring&)> fn);

    std::function<void()>*    FindClick(const std::string& name);
    std::function<void(bool)>* FindValue(const std::string& name);
    std::function<void(float)>* FindFloat(const std::string& name);
    std::function<void(int)>*  FindSelection(const std::string& name);
    std::function<void(const std::wstring&)>* FindString(const std::string& name);

private:
    std::map<std::string, std::function<void()>>    clicks_;
    std::map<std::string, std::function<void(bool)>> values_;
    std::map<std::string, std::function<void(float)>> floats_;
    std::map<std::string, std::function<void(int)>>   selections_;
    std::map<std::string, std::function<void(const std::wstring&)>> strings_;
};

// ---- Build widget tree from parsed UiDocument ----
// Applies styles, resolves theme colors, creates bindings.
// baseDir: directory of the .ui file (for resolving Include paths)
// depth: recursion depth (to prevent infinite Include loops)
WidgetPtr BuildWidgetTree(
    const UiNode& node,
    const std::vector<StyleRule>& styles,
    BindingContext& bindings,
    HandlerMap& handlers,
    std::string& errorOut,
    const std::string& baseDir = "",
    int depth = 0
);

} // namespace ui
