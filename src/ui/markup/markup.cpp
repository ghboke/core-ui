#include "markup.h"
#include "../controls.h"
#include <fstream>
#include <sstream>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace ui {

// ---- File loading ----

bool UiMarkup::LoadFile(const std::wstring& path) {
    // Read file as UTF-8
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f.is_open()) {
        lastError_ = "cannot open file";
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    sourceCache_ = ss.str();
    filePath_ = path;

    // Skip UTF-8 BOM if present
    if (sourceCache_.size() >= 3 &&
        (unsigned char)sourceCache_[0] == 0xEF &&
        (unsigned char)sourceCache_[1] == 0xBB &&
        (unsigned char)sourceCache_[2] == 0xBF) {
        sourceCache_ = sourceCache_.substr(3);
    }

    return Build();
}

bool UiMarkup::LoadString(const std::string& source) {
    sourceCache_ = source;
    filePath_.clear();
    return Build();
}

// ---- Internal build ----

bool UiMarkup::Build() {
    // Parse
    doc_ = {};
    if (!ParseUiMarkup(sourceCache_, doc_, lastError_)) {
        return false;
    }

    // Reset bindings (keep handlers)
    bindings_ = BindingContext();

    // Compute base directory for Include resolution
    std::string baseDir;
    if (!filePath_.empty()) {
        // Convert wstring path to string, extract directory
        std::string fp(filePath_.begin(), filePath_.end());
        auto pos = fp.find_last_of("/\\");
        if (pos != std::string::npos) baseDir = fp.substr(0, pos);
    }

    // Build widget tree
    root_ = BuildWidgetTree(doc_.root, doc_.styles, bindings_, handlers_, lastError_, baseDir);
    return root_ != nullptr;
}

// ---- Lookup ----

Widget* UiMarkup::FindById(const std::string& id) const {
    return root_ ? root_->FindById(id) : nullptr;
}

// ---- Handlers ----

void UiMarkup::SetHandler(const std::string& name, std::function<void()> fn)     { handlers_.SetClick(name, std::move(fn)); }
void UiMarkup::SetHandler(const std::string& name, std::function<void(bool)> fn)  { handlers_.SetValue(name, std::move(fn)); }
void UiMarkup::SetHandler(const std::string& name, std::function<void(float)> fn) { handlers_.SetFloat(name, std::move(fn)); }
void UiMarkup::SetHandler(const std::string& name, std::function<void(int)> fn)   { handlers_.SetSelection(name, std::move(fn)); }
void UiMarkup::SetHandler(const std::string& name, std::function<void(const std::wstring&)> fn) { handlers_.SetString(name, std::move(fn)); }

// ---- Bindings ----

void UiMarkup::SetBool(const std::string& name, bool value)                { bindings_.SetBool(name, value); }
void UiMarkup::SetFloat(const std::string& name, float value)              { bindings_.SetFloat(name, value); }
void UiMarkup::SetInt(const std::string& name, int value)                  { bindings_.SetInt(name, value); }
void UiMarkup::SetText(const std::string& name, const std::wstring& value) { bindings_.SetText(name, value); }
void UiMarkup::SetList(const std::string& name, const std::vector<ListItem>& items) { bindings_.SetList(name, items); }

// ---- Hot reload ----

bool UiMarkup::Reload() {
    if (!filePath_.empty()) {
        return LoadFile(filePath_);
    }
    if (!sourceCache_.empty()) {
        return Build();
    }
    lastError_ = "no source to reload";
    return false;
}

// ---- Responsive ----

void UiMarkup::OnResize(float windowWidth, float windowHeight) {
    if (doc_.mediaQueries.empty() || !root_) return;

    // Rebuild is heavy; for now just re-build (future: diff and patch)
    // Collect matching media query rules
    std::vector<StyleRule> activeStyles = doc_.styles;
    for (auto& mq : doc_.mediaQueries) {
        if (windowWidth >= mq.minWidth && windowWidth <= mq.maxWidth &&
            windowHeight >= mq.minHeight && windowHeight <= mq.maxHeight) {
            activeStyles.insert(activeStyles.end(), mq.rules.begin(), mq.rules.end());
        }
    }

    // Store active styles for next build
    // For simplicity, rebuild the widget tree with combined styles
    // This preserves the handler registrations but loses widget state
    bindings_ = BindingContext();
    std::string baseDir;
    if (!filePath_.empty()) {
        std::string fp(filePath_.begin(), filePath_.end());
        auto pos = fp.find_last_of("/\\");
        if (pos != std::string::npos) baseDir = fp.substr(0, pos);
    }
    std::string buildErr;
    auto newRoot = BuildWidgetTree(doc_.root, activeStyles, bindings_, handlers_, buildErr, baseDir);
    if (newRoot) root_ = newRoot;
}

// ---- i18n ----

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring ws(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &ws[0], len);
    return ws;
}

bool UiMarkup::LoadLanguage(const std::wstring& path) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f.is_open()) { lastError_ = "cannot open language file"; return false; }
    std::ostringstream ss;
    ss << f.rdbuf();
    return LoadLanguageString(ss.str());
}

bool UiMarkup::LoadLanguageString(const std::string& content) {
    langStrings_.clear();
    std::istringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        // Skip BOM, comments, empty lines
        if (line.size() >= 3 && (unsigned char)line[0] == 0xEF) line = line.substr(3);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        // Trim \r
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        // Trim whitespace
        while (!key.empty() && key.back() == ' ') key.pop_back();
        while (!val.empty() && val.front() == ' ') val.erase(val.begin());
        if (!key.empty()) langStrings_[key] = Utf8ToWide(val);
    }
    return true;
}

std::wstring UiMarkup::Tr(const std::string& key) const {
    auto it = langStrings_.find(key);
    if (it != langStrings_.end()) return it->second;
    return Utf8ToWide(key);  // fallback: return key itself
}

void UiMarkup::ApplyLanguage() {
    if (!root_) return;

    // Walk widget tree and update all @key text references
    std::function<void(Widget*)> walk = [&](Widget* w) {
        if (!w) return;

        // Check widget's i18n key (stored in a custom field)
        if (!w->i18nKey.empty()) {
            auto translated = Tr(w->i18nKey);
            if (auto* lbl = dynamic_cast<LabelWidget*>(w))       lbl->SetText(translated);
            else if (auto* btn = dynamic_cast<ButtonWidget*>(w))  btn->SetText(translated);
            else if (auto* cb = dynamic_cast<CheckBoxWidget*>(w)) cb->SetText(translated);
            else if (auto* tg = dynamic_cast<ToggleWidget*>(w))   tg->SetText(translated);
            else if (auto* rb = dynamic_cast<RadioButtonWidget*>(w)) rb->SetText(translated);
            else if (auto* nav = dynamic_cast<NavItemWidget*>(w)) nav->SetText(translated);
        }
        if (!w->tooltipI18nKey.empty()) {
            w->tooltip = Tr(w->tooltipI18nKey);
        }
        if (!w->titleI18nKey.empty()) {
            if (auto* tb = dynamic_cast<TitleBarWidget*>(w)) tb->SetTitle(Tr(w->titleI18nKey));
        }

        for (auto& child : w->Children()) walk(child.get());
    };
    walk(root_.get());
}

} // namespace ui
