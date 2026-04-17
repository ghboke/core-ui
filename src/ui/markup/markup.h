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

#include "markup_parser.h"
#include "markup_builder.h"
#include "../widget.h"
#include <string>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace ui {

class UI_API UiMarkup {
public:
    // ---- Load ----
    bool LoadFile(const std::wstring& path);
    bool LoadString(const std::string& source);

    // ---- Widget tree ----
    WidgetPtr Root() const { return root_; }

    // ---- ID-based lookup ----
    Widget* FindById(const std::string& id) const;

    template<typename T>
    T* FindAs(const std::string& id) const {
        return dynamic_cast<T*>(FindById(id));
    }

    // ---- Event handlers (register before or after Load) ----
    void SetHandler(const std::string& name, std::function<void()> fn);
    void SetHandler(const std::string& name, std::function<void(bool)> fn);
    void SetHandler(const std::string& name, std::function<void(float)> fn);
    void SetHandler(const std::string& name, std::function<void(int)> fn);
    void SetHandler(const std::string& name, std::function<void(const std::wstring&)> fn);

    // ---- Property bindings (push values to bound widgets) ----
    void SetBool(const std::string& name, bool value);
    void SetFloat(const std::string& name, float value);
    void SetInt(const std::string& name, int value);
    void SetText(const std::string& name, const std::wstring& value);

    // ---- List data for Repeater ----
    using ListItem = BindingContext::ListItem;
    void SetList(const std::string& name, const std::vector<ListItem>& items);

    // ---- Window hints (from <ui> attributes) ----
    const WindowHints& Window() const { return doc_.window; }

    // ---- Hot reload ----
    bool Reload();

    // ---- Responsive: evaluate @media queries on resize ----
    void OnResize(float windowWidth, float windowHeight);

    // ---- Internationalization (i18n) ----
    // Load a language file (key=value format, UTF-8)
    bool LoadLanguage(const std::wstring& path);
    bool LoadLanguageString(const std::string& content);
    // Apply loaded language: replace all @key references in the UI tree
    void ApplyLanguage();
    // Get translated string for a key (returns key itself if not found)
    std::wstring Tr(const std::string& key) const;

    // ---- Error info ----
    const std::string& LastError() const { return lastError_; }

private:
    bool Build();

    WidgetPtr root_;
    UiDocument doc_;
    HandlerMap handlers_;
    BindingContext bindings_;
    std::string lastError_;

    // For reload
    std::wstring filePath_;
    std::string sourceCache_;

    // i18n
    std::unordered_map<std::string, std::wstring> langStrings_;
    // Track which widgets have @key references: widget* → {attr, key}
    struct I18nBinding { Widget* widget; std::string attr; std::string key; };
    std::vector<I18nBinding> i18nBindings_;
};

} // namespace ui
