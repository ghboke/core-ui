#include "page_state.h"
#include "compiler.h"
#include "text_binding.h"
#include "../asset.h"
#include "../expression/json.h"
#include "../uix/template_parser.h"
#include "../uix/sfc_parser.h"
#include "../css/css_parser.h"

#ifdef small
#undef small
#endif
#include "../handle_table.h"
#include "../ui_context.h"

#ifndef UI_CORE_BUILDING
#define UI_CORE_BUILDING
#endif
#include "../../../include/ui_core.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <windows.h>    // OutputDebugStringA

namespace ui::page {

// ---- Page registry (separate from widget HandleTable to avoid id collisions) ----

struct PageEntry {
    std::unique_ptr<PageState> state;
    std::string sourceText;        // for reload
    std::string sourcePath;        // for reload from file
    bool hasStyle = false;         // extracted <style> block (concatenated)
    std::string lastError;
};

class PageRegistry {
public:
    uint64_t Insert(std::unique_ptr<PageEntry> e) {
        std::lock_guard<std::mutex> lk(m_);
        uint64_t id = next_++;
        entries_[id] = std::move(e);
        return id;
    }
    PageEntry* Get(uint64_t id) {
        std::lock_guard<std::mutex> lk(m_);
        auto it = entries_.find(id);
        return it == entries_.end() ? nullptr : it->second.get();
    }
    void Erase(uint64_t id) {
        std::lock_guard<std::mutex> lk(m_);
        entries_.erase(id);
    }
    // 摘走全部页面 (锁内只交换, 析构在调用方锁外做 —— 控件析构可能回调进来)。
    std::unordered_map<uint64_t, std::unique_ptr<PageEntry>> TakeAll() {
        std::lock_guard<std::mutex> lk(m_);
        std::unordered_map<uint64_t, std::unique_ptr<PageEntry>> out;
        out.swap(entries_);
        return out;
    }
    // Visit every live page under the registry lock. The callback receives
    // the entry by reference and must not call back into the registry
    // (would deadlock).
    template <class Fn>
    void ForEach(Fn fn) {
        std::lock_guard<std::mutex> lk(m_);
        for (auto& kv : entries_) {
            if (kv.second) fn(*kv.second);
        }
    }

private:
    std::mutex m_;
    std::unordered_map<uint64_t, std::unique_ptr<PageEntry>> entries_;
    uint64_t next_ = 1;
};

// 永不析构 (build 332): 没被 destroy 的页面不能拖到 DLL 卸载时析构 —— 那时控件
// 依赖的 Store / 设备可能已先析构, 且加载器锁里释放 D3D 本身就危险。正常收尾由
// ui_shutdown → DestroyAllPages 完成, 漏掉的交给系统回收。
static PageRegistry& Registry() {
    static PageRegistry* r = new PageRegistry;
    return *r;
}

// ---- HTML <style> block extraction ----
// One record per <style> block so we can honor 'scoped' per-block independently.
struct StyleBlock {
    std::string cssText;
    bool scoped = false;
};

// ---- UTF-8 → UTF-16 (wide) for window attrs ----
std::wstring Utf8ToWide(const std::string& s) {
    std::wstring r;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0; int len = 0;
        if ((c & 0x80) == 0)          { cp = c;        len = 1; }
        else if ((c & 0xE0) == 0xC0)  { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0)  { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0)  { cp = c & 0x07; len = 4; }
        else                           { i++; continue; }
        if (i + len > s.size()) break;
        for (int k = 1; k < len; k++) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        i += len;
        if (cp < 0x10000) r += static_cast<wchar_t>(cp);
        else {
            cp -= 0x10000;
            r += static_cast<wchar_t>(0xD800 | (cp >> 10));
            r += static_cast<wchar_t>(0xDC00 | (cp & 0x3FF));
        }
    }
    return r;
}

// Build a WindowHints struct from a <window> tag's parsed attributes.
WindowHints WindowHintsFromAttrs(const std::vector<ui::uix::SfcAttr>& attrs) {
    WindowHints hints;
    hints.present = true;
    auto toBool = [](const std::string& v) -> int {
        if (v == "true" || v == "1" || v == "yes") return 1;
        if (v == "false" || v == "0" || v == "no") return 0;
        return -1;
    };
    /* `width / height / min-width / min-height` 单位都是 DIP (1/96 inch),
     * 跟 UiWindowConfig.width/height 同款语义. HiDPI 上 lib 会乘 dpi/96 转
     * 物理 px (ui_window.cpp::Create 里 MulDiv). 例: 100% 缩放 width=400
     * → 400 物理 px; 150% → 600 物理 px. 详见 ui_core.h UiWindowConfig 顶部. */
    for (const auto& a : attrs) {
        if      (a.name == "title")      hints.title = Utf8ToWide(a.value);
        else if (a.name == "width")      { try { hints.width     = std::stoi(a.value); } catch (...) {} }
        else if (a.name == "height") {
            /* build 328: height="auto" = 窗口高度跟随内容, 见 ui_window_set_auto_height。 */
            if (a.value == "auto") hints.autoHeight = true;
            else { try { hints.height = std::stoi(a.value); } catch (...) {} }
        }
        else if (a.name == "min-width")  { try { hints.minWidth  = std::stoi(a.value); } catch (...) {} }
        else if (a.name == "min-height") { try { hints.minHeight = std::stoi(a.value); } catch (...) {} }
        else if (a.name == "resizable")  hints.resizable = toBool(a.value);
        else if (a.name == "frameless")  hints.frameless = toBool(a.value);
        else if (a.name == "centered")   hints.centered  = toBool(a.value);
        else if (a.name == "theme") {
            if      (a.value == "dark")  hints.theme = 0;
            else if (a.value == "light") hints.theme = 1;
        }
    }
    return hints;
}

// Resolve <link rel="stylesheet" href="..."/> entries into CSS text via the
// asset registry. Failures are silent — dev workflow may register the asset
// later, and missing one external sheet shouldn't abort the page load.
//
// Link blocks come **before** inline <style> blocks in the cascade so inline
// can override external (browser convention).
std::vector<StyleBlock> ResolveLinkBlocks(const std::vector<ui::uix::SfcLink>& links) {
    std::vector<StyleBlock> out;
    for (const auto& lnk : links) {
        if (lnk.rel != "stylesheet" || lnk.href.empty()) continue;
        const void* bytes = nullptr;
        size_t size = 0;
        ui::asset::DataOwnerPtr owner;
        if (ui::asset::Resolve(lnk.href, &bytes, &size, &owner) && bytes) {
            StyleBlock blk;
            blk.cssText.assign(static_cast<const char*>(bytes), size);
            blk.scoped = false;
            out.push_back(std::move(blk));
        }
    }
    return out;
}

// Walk an HtmlNode tree and append scopeId to every element's data-scope attr.
// If the element already has data-scope (e.g., in multi-component), space-joined.
void TagElementsWithScope(ui::uix::Node& root, const std::string& scopeId) {
    if (root.kind == ui::uix::NodeKind::Element) {
        bool found = false;
        for (auto& a : root.attrs) {
            if (a.kind == ui::uix::AttrKind::Static && a.name == "data-scope") {
                if (!a.rawValue.empty()) a.rawValue += ' ';
                a.rawValue += scopeId;
                found = true;
                break;
            }
        }
        if (!found) {
            ui::uix::Attr a;
            a.kind = ui::uix::AttrKind::Static;
            a.name = "data-scope";
            a.rawValue = scopeId;
            root.attrs.push_back(std::move(a));
        }
    }
    for (auto& c : root.children) {
        if (c) TagElementsWithScope(*c, scopeId);
    }
}

// Append [data-scope~="<id>"] to every compound selector's parts in the stylesheet.
// This requires elements to have data-scope matching the id for the rule to apply.
void ApplyScopeToStylesheet(ui::css::Stylesheet& sheet, const std::string& scopeId) {
    using namespace ui::css;
    for (auto& rule : sheet.rules) {
        for (auto& sel : rule.selectors) {
            // Skip :root-only selectors (those define CSS vars)
            bool isRoot = (sel.compounds.size() == 1 &&
                           sel.compounds[0].parts.size() == 1 &&
                           sel.compounds[0].parts[0].kind == SimpleKind::Pseudo &&
                           sel.compounds[0].parts[0].name == "root");
            if (isRoot) continue;
            // Append to the RIGHTMOST compound (Vue convention).
            if (sel.compounds.empty()) continue;
            auto& lastCmpd = sel.compounds.back();
            SimpleSelector attrSel;
            attrSel.kind = SimpleKind::Attr;
            attrSel.name = "data-scope";
            attrSel.attrOp = AttrOp::Includes;
            attrSel.attrValue = scopeId;
            lastCmpd.parts.push_back(attrSel);
        }
    }
}

// Read a UTF-8 text file; returns "" if not found.
bool ReadFileUtf8(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream oss;
    oss << f.rdbuf();
    out = oss.str();
    return true;
}

// ---- Core compile + attach helper ----
static uint64_t g_nextScopeId = 1;

bool CompileAndAttachPage(PageEntry& e, const std::string& rawText) {
    auto sfc = ui::uix::ParseSfc(rawText);
    if (!sfc.ok) {
        std::ostringstream os;
        os << "SFC parse failed:\n";
        for (auto& err : sfc.errors) os << ui::uix::FormatError(err) << "\n";
        e.lastError = os.str();
        return false;
    }
    if (!sfc.hasTemplate) {
        e.lastError = "page is missing a <template> block";
        return false;
    }

    WindowHints hints;
    if (sfc.hasWindow) hints = WindowHintsFromAttrs(sfc.windowAttrs);

    // Stash raw script text for the QuickJS path. AttachQuickJS reads
    // CompiledPage::scriptSource and runs EvalModule directly.
    std::string rawScript = sfc.hasScript ? sfc.scriptContent : std::string{};

    /* link-resolve first: external sheets cascade BEFORE inline <style>, so
       inline can override external (browser convention). */
    std::vector<StyleBlock> blocks = ResolveLinkBlocks(sfc.links);
    for (const auto& s : sfc.styles) {
        StyleBlock blk;
        blk.cssText = s.content;
        blk.scoped  = s.scoped;
        blocks.push_back(std::move(blk));
    }

    auto tplResult = ui::uix::ParseTemplate(sfc.templateContent);
    if (!tplResult.ok || !tplResult.root) {
        std::ostringstream os;
        if (tplResult.errors.empty()) {
            os << "template parse failed: no root element";
        } else {
            for (auto& err : tplResult.errors) os << ui::uix::FormatError(err) << "\n";
        }
        e.lastError = os.str();
        return false;
    }

    ui::css::Stylesheet sheet;
    bool anyScoped = false;
    std::string pageScopeId = "p" + std::to_string(g_nextScopeId++);

    /* build 77 (L17 跟进): lib 内置 menu 默认 CSS — 给 compiler 合成的
     * <div class="menuitem-row"> 包装 + 内部 svg / label 提供合理 baseline.
     * 放在用户 <style> 块之前 parse, rules 先压入 sheet → 用户后写的同选择器
     * 规则因为 source order 较后, cascade 自动覆盖 (e.g. 用户写
     * .menuitem-row { max-width: 400px } 就放宽限制). */
    {
        constexpr const char* kLibMenuDefaultsCSS = R"(
.menuitem-row {
  flex-direction: row;
  align-items: center;
  gap: 8px;
  padding: 0 8px;
  /* 控制单 item 内容最大宽度. 用户在 <style> 写 .menuitem-row { max-width: 400px }
   * 覆盖. 默认 140 适合短中文 + 常见 shortcut 组合; build 80 收紧 gap+padding
   * 配合 kMinWidth=235 目标 ~400 px on-screen (163% DPI). */
  max-width: 140px;
}
.menuitem-row svg {
  width: 18px;
  height: 18px;
  flex: none;
}
.menuitem-row label {
  font-size: 13.5px;
  /* 菜单项单行 (L100): 配合 HBoxWidget flex-shrink (build 141), 超长 label 被
   * 收缩到行宽后走单行 ellipsis "abc…", 而非默认软换行折成多行、撑高 item
   * 溢出压到相邻项。需要多行菜单项的调用方用户 CSS 覆盖 white-space: normal。 */
  white-space: nowrap;
}
)";
        auto def = ui::css::ParseStylesheet(kLibMenuDefaultsCSS);
        if (def.ok) {
            for (auto& r : def.stylesheet.rules) {
                sheet.rules.push_back(std::move(r));
            }
        }
    }

    for (auto& blk : blocks) {
        if (blk.cssText.empty()) continue;
        auto cssResult = ui::css::ParseStylesheet(blk.cssText);
        if (!cssResult.ok) {
            std::ostringstream os;
            os << "CSS parse failed:\n";
            for (auto& err : cssResult.errors) os << ui::css::FormatError(err) << "\n";
            e.lastError = os.str();
            return false;
        }
        if (blk.scoped) {
            ApplyScopeToStylesheet(cssResult.stylesheet, pageScopeId);
            anyScoped = true;
        }
        for (auto& r : cssResult.stylesheet.rules) {
            sheet.rules.push_back(std::move(r));
        }
    }

    // Only tag elements when at least one scoped block exists (saves attr memory).
    if (anyScoped) {
        TagElementsWithScope(*tplResult.root, pageScopeId);
    }

    // Heap-allocate the stylesheet BEFORE Compile so widget recompute lambdas
    // can capture a stable pointer into the final resting place. Move-into
    // CompiledPage afterwards would change the object's address otherwise.
    auto sheetOwned = std::make_unique<ui::css::Stylesheet>(std::move(sheet));

    auto compiled = Compile(*tplResult.root, *sheetOwned);
    if (!compiled.root) {
        std::ostringstream os;
        os << "Compile failed:\n";
        for (auto& err : compiled.errors) os << err << "\n";
        e.lastError = os.str();
        return false;
    }

    compiled.ownedHtml       = std::move(tplResult.root);
    compiled.ownedStylesheet = std::move(sheetOwned);
    compiled.scriptSource    = std::move(rawScript);
    compiled.windowHints     = std::move(hints);

    if (!sfc.imports.empty()) {
        compiled.errors.push_back("<import> is no longer supported; component "
                                  "system was removed in 1.5.0");
    }

    if (!e.state) e.state = std::make_unique<PageState>();

    // 即使 compile 成功（root 非空），compiled.errors 里仍可能有非致命警告
    // （比如嵌套 inline 元素被跳过编译）。把它们摆到 lastError，让调用方能
    // 通过 ui_page_last_error() 看见——load 返回值仍是成功，但开发期 / CI
    // 能立刻发现"渲染没崩，但语义有问题"。
    std::string warnings;
    for (auto& err : compiled.errors) {
        if (!warnings.empty()) warnings += "\n";
        warnings += err;
    }
    // Stderr + OutputDebugString 双通道，IDE / 终端都能看到。
    if (!warnings.empty()) {
        std::fprintf(stderr, "[ui_page] %s\n", warnings.c_str());
        OutputDebugStringA("[ui_page] ");
        OutputDebugStringA(warnings.c_str());
        OutputDebugStringA("\n");
    }
    e.state->Attach(std::move(compiled));
    e.lastError = std::move(warnings);
    return true;
}

// Re-runs RefreshThemeStyles on every live page. Called from
// `ui_theme_set_mode` so a Light/Dark flip propagates through every loaded
// .uix without needing per-page glue.
void RefreshAllPageThemes() {
    Registry().ForEach([](PageEntry& e) {
        if (e.state) e.state->RefreshThemeStyles();
    });
}

void DestroyAllPages() {
    auto leaked = Registry().TakeAll();
    if (!leaked.empty()) {
        char msg[96];
        std::snprintf(msg, sizeof(msg),
                      "[core-ui] ui_shutdown: %zu page(s) not destroyed by host\n",
                      leaked.size());
        OutputDebugStringA(msg);
    }
    leaked.clear();   // 锁外析构控件树
}

}  // namespace ui::page

// ---- C API entry points ----

namespace {
/* 用宽路径打开二进制文件 — MSVC 走 wchar_t 重载，MinGW 走 UTF-8 narrow。
   page_api.cpp 里 ui_page_load_file 用过同款逻辑，提取出来共用。 */
bool OpenWideBinary(const wchar_t* path, std::ifstream& out) {
#ifdef _MSC_VER
    out.open(path, std::ios::binary);
#else
    std::wstring wpath(path);
    std::string narrow;
    for (wchar_t c : wpath) {
        if (c < 0x80) narrow += static_cast<char>(c);
        else if (c < 0x800) {
            narrow += static_cast<char>(0xC0 | (c >> 6));
            narrow += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            narrow += static_cast<char>(0xE0 | (c >> 12));
            narrow += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            narrow += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    out.open(narrow, std::ios::binary);
#endif
    return out.is_open();
}

/* 解析 ".lang" 风格内容: 每行 key=value，UTF-8。注释（#... / ;...）和空行
   忽略；UTF-8 BOM、行尾 \r 自动去除。跟 .ui markup 的 LoadLanguageString
   语义一致，迁移用户不用改 .lang 文件。
   放在 extern "C" 块外是因为返回值是 std::unordered_map（不能 C-link）。 */
std::unordered_map<std::string, std::string> ParseLangContent(const std::string& content_in) {
    std::unordered_map<std::string, std::string> pairs;

    /* BOM 只在文件起点出现一次：在循环外预先剥掉，避免每行都做无意义检查 */
    const char* p = content_in.data();
    size_t n = content_in.size();
    if (n >= 3 && (unsigned char)p[0] == 0xEF &&
                  (unsigned char)p[1] == 0xBB &&
                  (unsigned char)p[2] == 0xBF) {
        p += 3; n -= 3;
    }

    size_t i = 0;
    while (i < n) {
        const char* lineStart = p + i;
        size_t lineLen = 0;
        while (i < n && p[i] != '\n') { ++lineLen; ++i; }
        if (i < n) ++i;  // skip '\n'

        // strip trailing \r
        if (lineLen > 0 && lineStart[lineLen - 1] == '\r') --lineLen;
        if (lineLen == 0) continue;
        if (lineStart[0] == '#' || lineStart[0] == ';') continue;

        std::string line(lineStart, lineLen);
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(val.begin());
        if (!key.empty()) pairs.emplace(std::move(key), std::move(val));
    }
    return pairs;
}
}  // namespace

namespace {
ui::page::PageState::KeyText MakeKeyText(const char* key, const wchar_t* const* args, int argc) {
    ui::page::PageState::KeyText t;
    t.key = key ? key : "";
    for (int i = 0; args && i < argc; ++i) t.args.emplace_back(args[i] ? args[i] : L"");
    return t;
}
ui::page::PageState* StateOf(UiPage p) {
    auto* e = ui::page::Registry().Get(p);
    return (e && e->state) ? e->state.get() : nullptr;
}
}  // namespace

extern "C" {

UI_API UiPage ui_page_load_file(const wchar_t* path) {
    if (!path) return 0;
    std::wstring wpath(path);
    // Open the file with wide-filename fstream (MSVC extension; on MinGW use narrow fallback).
    std::ifstream f;
#ifdef _MSC_VER
    f.open(wpath.c_str(), std::ios::binary);
#else
    // MinGW: convert to UTF-8 path
    std::string narrow;
    for (wchar_t c : wpath) {
        if (c < 0x80) narrow += static_cast<char>(c);
        else if (c < 0x800) { narrow += static_cast<char>(0xC0 | (c >> 6)); narrow += static_cast<char>(0x80 | (c & 0x3F)); }
        else { narrow += static_cast<char>(0xE0 | (c >> 12)); narrow += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); narrow += static_cast<char>(0x80 | (c & 0x3F)); }
    }
    f.open(narrow, std::ios::binary);
#endif
    if (!f) return 0;
    std::ostringstream oss;
    oss << f.rdbuf();
    std::string src = oss.str();

    auto entry = std::make_unique<ui::page::PageEntry>();
    // Store path for reload
#ifdef _MSC_VER
    // Store UTF-8 of path
    for (wchar_t c : wpath) {
        if (c < 0x80) entry->sourcePath += static_cast<char>(c);
        else if (c < 0x800) { entry->sourcePath += static_cast<char>(0xC0 | (c >> 6)); entry->sourcePath += static_cast<char>(0x80 | (c & 0x3F)); }
        else { entry->sourcePath += static_cast<char>(0xE0 | (c >> 12)); entry->sourcePath += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); entry->sourcePath += static_cast<char>(0x80 | (c & 0x3F)); }
    }
#else
    entry->sourcePath = narrow;
#endif
    entry->sourceText = src;
    if (!ui::page::CompileAndAttachPage(*entry, src)) {
        // Keep the entry for error access
        uint64_t id = ui::page::Registry().Insert(std::move(entry));
        return id;
    }
    return ui::page::Registry().Insert(std::move(entry));
}

UI_API UiPage ui_page_load_string(const char* html_source) {
    if (!html_source) return 0;
    auto entry = std::make_unique<ui::page::PageEntry>();
    entry->sourceText = html_source;
    ui::page::CompileAndAttachPage(*entry, entry->sourceText);
    return ui::page::Registry().Insert(std::move(entry));
}

UI_API void ui_page_destroy(UiPage p) {
    ui::page::Registry().Erase(p);
}

UI_API int ui_page_reload(UiPage p) {
    auto* e = ui::page::Registry().Get(p);
    if (!e) return 0;
    if (e->sourcePath.empty()) {
        return ui::page::CompileAndAttachPage(*e, e->sourceText) ? 1 : 0;
    }
    std::ifstream f(e->sourcePath.c_str(), std::ios::binary);
    if (!f) return 0;
    std::ostringstream oss;
    oss << f.rdbuf();
    e->sourceText = oss.str();
    return ui::page::CompileAndAttachPage(*e, e->sourceText) ? 1 : 0;
}

/* 共用 create-setup-attach 内部实现. show=true 走完整 open (跟 fade-in 动画),
 * show=false 仅停在 prepare_rt, caller 自己 ui_window_show_immediate. */
static UiWindow page_open_or_prepare_(UiPage p,
                                         const UiWindowConfig* override_defaults,
                                         bool show) {
    auto* e = ui::page::Registry().Get(p);
    if (!e || !e->state) return 0;
    const auto& hints = e->state->PageData().windowHints;

    UiWindowConfig cfg = {0};
    if (override_defaults) cfg = *override_defaults;
    if (cfg.width  == 0) cfg.width  = 800;
    if (cfg.height == 0) cfg.height = 600;
    if (!cfg.title) cfg.title = L"Core UI";
    cfg.resizable = 1;

    if (!hints.title.empty())   cfg.title      = hints.title.c_str();
    if (hints.width > 0)        cfg.width      = hints.width;
    if (hints.height > 0)       cfg.height     = hints.height;
    if (hints.resizable == 0)   cfg.resizable  = 0;
    else if (hints.resizable == 1) cfg.resizable = 1;
    if (hints.frameless == 0)   cfg.system_frame = 1;
    else if (hints.frameless == 1) cfg.system_frame = 0;
    if (hints.centered == 1)    { cfg.x = 0; cfg.y = 0; }

    if (hints.theme == 0)       ui_theme_set_mode(UI_THEME_DARK);
    else if (hints.theme == 1)  ui_theme_set_mode(UI_THEME_LIGHT);

    UiWindow win = ui_window_create(&cfg);
    if (!win) return 0;

    if (hints.minWidth > 0 || hints.minHeight > 0) {
        ui_window_set_min_size(win,
                               hints.minWidth  > 0 ? hints.minWidth  : 0,
                               hints.minHeight > 0 ? hints.minHeight : 0);
    }

    auto root = e->state->Root();
    if (root) {
        UiWidget h = ui::GetContext().handles.Insert(root);
        ui_window_set_root(win, h);
    }
    e->state->AttachWindow(win);
    /* 挂好根节点再开: 开启时立即按内容定一次高, prepare 之后宿主读到的就是终值。 */
    if (hints.autoHeight) ui_window_set_auto_height(win, 1);

    if (show) {
        ui_window_show(win);
    } else {
        /* prepare-only 路径 (build 99+ L27): RT 提前创建好, 等 caller 同步预热
         * (decode + 上 bitmap 等) 之后再 ui_window_show_immediate 一次性出图.
         * 没 RT 时 first paint 慢, 提前 prepare 把这部分提到 show 之前. */
        ui_window_prepare_rt(win);
    }
    return win;
}

UI_API UiWindow ui_page_open_window(UiPage p, const UiWindowConfig* override_defaults) {
    return page_open_or_prepare_(p, override_defaults, /*show=*/true);
}

UI_API UiWindow ui_page_prepare_window(UiPage p, const UiWindowConfig* override_defaults) {
    return page_open_or_prepare_(p, override_defaults, /*show=*/false);
}

UI_API UiWidget ui_page_root(UiPage p) {
    auto* e = ui::page::Registry().Get(p);
    if (!e || !e->state) return 0;
    auto root = e->state->Root();
    if (!root) return 0;
    return ui::GetContext().handles.Insert(root);
}

UI_API void ui_page_set_bool(UiPage p, const char* name, int value) {
    auto* e = ui::page::Registry().Get(p);
    if (!e || !e->state || !name) return;
    e->state->SetBool(name, value != 0);
}
UI_API void ui_page_set_int(UiPage p, const char* name, int value) {
    auto* e = ui::page::Registry().Get(p);
    if (!e || !e->state || !name) return;
    e->state->SetNumber(name, static_cast<double>(value));
}
UI_API void ui_page_set_float(UiPage p, const char* name, double value) {
    auto* e = ui::page::Registry().Get(p);
    if (!e || !e->state || !name) return;
    e->state->SetNumber(name, value);
}
UI_API void ui_page_set_text(UiPage p, const char* name, const wchar_t* value) {
    auto* e = ui::page::Registry().Get(p);
    if (!e || !e->state || !name) return;
    std::string utf8;
    if (value) {
        for (; *value; ++value) {
            wchar_t c = *value;
            if (c < 0x80) utf8 += static_cast<char>(c);
            else if (c < 0x800) { utf8 += static_cast<char>(0xC0 | (c >> 6)); utf8 += static_cast<char>(0x80 | (c & 0x3F)); }
            else { utf8 += static_cast<char>(0xE0 | (c >> 12)); utf8 += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); utf8 += static_cast<char>(0x80 | (c & 0x3F)); }
        }
    }
    e->state->UnbindTextVar(name);   /* build 316: 字面值覆盖 key 绑定 */
    e->state->SetString(name, utf8);
}

UI_API int ui_page_set_json(UiPage p, const char* name, const char* utf8_json) {
    auto* e = ui::page::Registry().Get(p);
    if (!e || !e->state || !name || !utf8_json) return -1;
    ui::expr::Value v;
    std::string err;
    if (!ui::expr::ParseJson(std::string(utf8_json), v, err)) {
        e->lastError = std::string("ui_page_set_json: ") + err;
        return -1;
    }
    e->state->SetValue(name, std::move(v));
    return 0;
}

UI_API char* ui_page_get_json(UiPage p, const char* name) {
    auto* e = ui::page::Registry().Get(p);
    if (!e || !e->state || !name) return nullptr;
    ui::expr::Value v;
    if (!e->state->GetValue(name, v)) return nullptr;
    if (v.IsFunction()) return nullptr;
    std::string s = ui::expr::EmitJson(v);
    /* malloc 兼容: 调用方用 ui_page_free 释放 */
    char* buf = (char*)std::malloc(s.size() + 1);
    if (!buf) return nullptr;
    std::memcpy(buf, s.data(), s.size());
    buf[s.size()] = 0;
    return buf;
}

UI_API void ui_page_free(void* ptr) {
    std::free(ptr);
}

UI_API void ui_page_set_text_list(UiPage p, const char* name, const wchar_t** items, int count) {
    auto* e = ui::page::Registry().Get(p);
    if (!e || !e->state || !name || !items || count < 0) return;
    ui::expr::Array arr;
    arr.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        std::string utf8;
        const wchar_t* w = items[i];
        if (w) {
            for (; *w; ++w) {
                wchar_t c = *w;
                if (c < 0x80) utf8 += static_cast<char>(c);
                else if (c < 0x800) { utf8 += static_cast<char>(0xC0 | (c >> 6)); utf8 += static_cast<char>(0x80 | (c & 0x3F)); }
                else { utf8 += static_cast<char>(0xE0 | (c >> 12)); utf8 += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); utf8 += static_cast<char>(0x80 | (c & 0x3F)); }
            }
        }
        arr.emplace_back(std::move(utf8));
    }
    e->state->SetValue(name, ui::expr::Value::MakeArray(std::move(arr)));
}

UI_API void ui_page_load_translations(UiPage p, const char* locale,
                                      const char** keys_values, int count) {
    auto* e = ui::page::Registry().Get(p);
    if (!e || !e->state || !locale || !keys_values || count <= 0) return;
    std::unordered_map<std::string, std::string> pairs;
    pairs.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const char* k = keys_values[i * 2 + 0];
        const char* v = keys_values[i * 2 + 1];
        if (k && v) pairs.emplace(k, v);
    }
    e->state->LoadTranslations(locale, pairs);
}

UI_API void ui_page_load_language_string(UiPage p, const char* locale,
                                          const char* utf8_content) {
    auto* e = ui::page::Registry().Get(p);
    if (!e || !e->state || !locale || !utf8_content) return;
    auto pairs = ParseLangContent(std::string(utf8_content));
    e->state->LoadTranslations(locale, pairs);
}

UI_API int ui_page_load_language_file(UiPage p, const char* locale,
                                       const wchar_t* path) {
    auto* e = ui::page::Registry().Get(p);
    if (!e || !e->state || !locale || !path) return 0;
    std::ifstream f;
    if (!OpenWideBinary(path, f)) {
        e->lastError = "cannot open language file";
        return 0;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    auto pairs = ParseLangContent(ss.str());
    e->state->LoadTranslations(locale, pairs);
    return 1;
}

UI_API UiMenu ui_page_menu(UiPage p, const char* name) {
    auto* e = ui::page::Registry().Get(p);
    if (!e || !e->state || !name) return 0;
    /* 用 PageState 缓存: 多次调用同一 name 复用 handle, 不在 Context::menus_
       里堆 N 份相同 shared_ptr. Attach() 会在 reload 时清缓存. */
    return e->state->GetOrRegisterMenuHandle(std::string(name));
}

UI_API void ui_page_set_locale(UiPage p, const char* locale) {
    auto* e = ui::page::Registry().Get(p);
    if (!e || !e->state || !locale) return;
    e->state->SetLocale(locale);
}

// ---- build 316: 宿主按 key 设的文字 ---------------------------------------

UI_API void ui_page_bind_text(UiPage p, UiWidget w, const char* key,
                              const wchar_t* const* args, int argc) {
    auto* s = StateOf(p);
    auto wp = ui::GetContext().handles.Lookup(w);
    if (!s || !wp || !key) return;
    s->BindWidgetText(wp, ui::page::PageState::TextSlot::Text, MakeKeyText(key, args, argc));
}

UI_API void ui_page_bind_tooltip(UiPage p, UiWidget w, const char* key,
                                 const wchar_t* const* args, int argc) {
    auto* s = StateOf(p);
    auto wp = ui::GetContext().handles.Lookup(w);
    if (!s || !wp || !key) return;
    s->BindWidgetText(wp, ui::page::PageState::TextSlot::Tooltip, MakeKeyText(key, args, argc));
}

UI_API void ui_page_bind_titlebar_title(UiPage p, UiWidget titlebar, const char* key,
                                        const wchar_t* const* args, int argc) {
    auto* s = StateOf(p);
    auto wp = ui::GetContext().handles.Lookup(titlebar);
    if (!s || !wp || !key) return;
    s->BindWidgetText(wp, ui::page::PageState::TextSlot::TitlebarTitle,
                      MakeKeyText(key, args, argc));
}

UI_API void ui_page_bind_window_title(UiPage p, UiWindow win, const char* key,
                                      const wchar_t* const* args, int argc) {
    auto* s = StateOf(p);
    if (!s || !win || !key) return;
    s->BindWindowTitle(win, MakeKeyText(key, args, argc));
}

UI_API void ui_page_bind_combobox_items(UiPage p, UiWidget w, const char* const* keys, int count) {
    auto* s = StateOf(p);
    auto wp = ui::GetContext().handles.Lookup(w);
    if (!s || !wp || (!keys && count > 0)) return;
    std::vector<std::string> ks;
    for (int i = 0; i < count; ++i) ks.emplace_back(keys[i] ? keys[i] : "");
    s->BindComboItems(wp, std::move(ks));
}

UI_API void ui_page_set_text_key(UiPage p, const char* name, const char* key,
                                 const wchar_t* const* args, int argc) {
    auto* s = StateOf(p);
    if (!s || !name || !key) return;
    s->SetTextKey(name, MakeKeyText(key, args, argc));
}


UI_API const char* ui_page_last_error(UiPage p) {
    auto* e = ui::page::Registry().Get(p);
    if (!e) return "invalid page handle";
    return e->lastError.c_str();
}

UI_API void ui_page_on_widget_mount(UiPage p, const char* widget_id,
                                     UiWidgetLifecycleCallback cb, void* userdata) {
    auto* e = ui::page::Registry().Get(p);
    if (!e || !e->state || !widget_id) return;
    if (!cb) {
        e->state->OnWidgetMount(widget_id, nullptr);
        return;
    }
    UiPage pageHandle = p;
    e->state->OnWidgetMount(widget_id, [cb, userdata, pageHandle](ui::Widget* w) {
        // Reuse / register a handle for the freshly-mounted widget so the
        // C callback gets a stable identifier (matches ui_widget_find_by_id
        // semantics).
        uint64_t h = ui::GetContext().handles.FindHandle(w);
        if (!h && w) h = ui::GetContext().handles.Insert(w->shared_from_this());
        cb(pageHandle, h, userdata);
    });
}

UI_API void ui_page_watch(UiPage p, const char* name, UiPageWatchCallback cb, void* userdata) {
    auto* e = ui::page::Registry().Get(p);
    if (!e || !e->state || !name || !*name) return;
    if (!cb) {
        e->state->Watch(name, nullptr);
        return;
    }
    UiPage pageHandle = p;
    e->state->Watch(name, [cb, userdata, pageHandle](const std::string& n, const std::string& json) {
        cb(pageHandle, n.c_str(), json.c_str(), userdata);
    });
}

UI_API void ui_page_on_widget_unmount(UiPage p, const char* widget_id,
                                       UiWidgetLifecycleCallback cb, void* userdata) {
    auto* e = ui::page::Registry().Get(p);
    if (!e || !e->state || !widget_id) return;
    if (!cb) {
        e->state->OnWidgetUnmount(widget_id, nullptr);
        return;
    }
    UiPage pageHandle = p;
    e->state->OnWidgetUnmount(widget_id, [cb, userdata, pageHandle](ui::Widget* w) {
        uint64_t h = ui::GetContext().handles.FindHandle(w);
        // Unmount fires before teardown; widget pointer is still alive,
        // but the handle may not exist if user never queried it. Skip the
        // invalidate-after-callback case — caller should treat the handle
        // as one-shot anyway.
        cb(pageHandle, h, userdata);
    });
}

UI_API void ui_page_set_quickjs_enabled(int v) {
    ui::page::PageState::SetGlobalUseQuickJS(v != 0);
}

}  // extern "C"

namespace ui::page {
void UnbindHostText(Widget* w, HostTextSlot slot) {
    if (!w) return;
    Registry().ForEach([&](PageEntry& e) {
        if (e.state) e.state->UnbindWidgetText(w, static_cast<PageState::TextSlot>(slot));
    });
}
void UnbindHostWindowTitle(uint64_t win) {
    Registry().ForEach([&](PageEntry& e) {
        if (e.state) e.state->UnbindWindowTitle(win);
    });
}
}  // namespace ui::page
