#include "svg_symbols.h"

#include "../asset.h"
#include "../uix/template_parser.h"

#include <set>
#include <string>
#include <unordered_map>
#include <utility>

namespace ui::page {

namespace {

using InheritedAttrs = std::vector<std::pair<std::string, std::string>>;

/* SVG 1.1 §6.4 默认继承的呈现属性 (与 compiler.cpp 折叠 <svg> 时同一张表)。 */
const std::set<std::string>& InheritableAttrs() {
    static const std::set<std::string> k = {
        "fill", "stroke",
        "fill-opacity", "stroke-opacity",
        "stroke-width", "stroke-dasharray",
        "stroke-linecap", "stroke-linejoin",
        "color",
    };
    return k;
}

void MergeInherited(const ui::uix::Node& el, InheritedAttrs& vec) {
    for (const auto& a : el.attrs) {
        if (a.kind != ui::uix::AttrKind::Static) continue;
        if (!InheritableAttrs().count(a.name)) continue;
        bool replaced = false;
        for (auto& p : vec) {
            if (p.first == a.name) { p.second = a.rawValue; replaced = true; break; }
        }
        if (!replaced) vec.emplace_back(a.name, a.rawValue);
    }
}

int ShapeKindOf(const std::string& t) {
    if (t == "circle")   return (int)SvgShapeKind::Circle;
    if (t == "rect")     return (int)SvgShapeKind::Rect;
    if (t == "ellipse")  return (int)SvgShapeKind::Ellipse;
    if (t == "line")     return (int)SvgShapeKind::Line;
    if (t == "polygon")  return (int)SvgShapeKind::Polygon;
    if (t == "polyline") return (int)SvgShapeKind::Polyline;
    if (t == "path")     return (int)SvgShapeKind::Path;
    return -1;
}

void CollectShapes(const ui::uix::Node& parent, const InheritedAttrs& inherited,
                   std::vector<SvgShape>& out) {
    for (const auto& c : parent.children) {
        if (!c || c->kind != ui::uix::NodeKind::Element) continue;
        if (c->tag == "g") {
            InheritedAttrs merged = inherited;
            MergeInherited(*c, merged);
            CollectShapes(*c, merged, out);
            continue;
        }
        int k = ShapeKindOf(c->tag);
        if (k < 0) continue;
        SvgShape s;
        s.kind = static_cast<SvgShapeKind>(k);
        for (const auto& [n, v] : inherited) ApplySvgShapeAttr(s, n, v);
        for (const auto& a : c->attrs) {
            if (a.kind == ui::uix::AttrKind::Static) ApplySvgShapeAttr(s, a.name, a.rawValue);
        }
        out.push_back(std::move(s));
    }
}

bool ParseViewBox(const std::string& text, float& w, float& h) {
    float vals[4] = {0, 0, 0, 0};
    int idx = 0;
    std::string tok;
    auto flush = [&]() {
        if (idx < 4 && !tok.empty()) {
            try { vals[idx++] = std::stof(tok); } catch (...) {}
        }
        tok.clear();
    };
    for (char c : text) {
        if (c == ' ' || c == ',' || c == '\t' || c == '\n' || c == '\r') flush();
        else tok += c;
    }
    flush();
    if (idx != 4 || vals[2] <= 0 || vals[3] <= 0) return false;
    w = vals[2];
    h = vals[3];
    return true;
}

const char* AttrOf(const ui::uix::Node& n, const char* name) {
    for (const auto& a : n.attrs) {
        if (a.kind == ui::uix::AttrKind::Static && a.name == name) return a.rawValue.c_str();
    }
    return nullptr;
}

/* 模板解析器不认 XML 声明 / DOCTYPE, 解析 sprite 文件前去掉。 */
std::string StripXmlProlog(std::string text) {
    for (const char* open : {"<?", "<!DOCTYPE", "<!doctype"}) {
        size_t pos;
        while ((pos = text.find(open)) != std::string::npos) {
            size_t end = text.find('>', pos);
            if (end == std::string::npos) break;
            text.erase(pos, end - pos + 1);
        }
    }
    return text;
}

void CollectSymbols(const ui::uix::Node& n,
                    std::unordered_map<std::string, SvgSymbol>& out) {
    for (const auto& c : n.children) {
        if (!c || c->kind != ui::uix::NodeKind::Element) continue;
        if (c->tag == "symbol") {
            if (const char* id = AttrOf(*c, "id")) out[id] = BuildSvgSymbol(*c);
            continue;
        }
        CollectSymbols(*c, out);
    }
}

struct SpriteFile {
    bool loaded = false;   // false = 资源不存在或解析失败 (负缓存)
    std::unordered_map<std::string, SvgSymbol> symbols;
};

std::unordered_map<std::string, SvgSymbol>& DocumentSymbols() {
    static std::unordered_map<std::string, SvgSymbol> m;
    return m;
}

std::unordered_map<std::string, SpriteFile>& SpriteFiles() {
    static std::unordered_map<std::string, SpriteFile> m;
    return m;
}

const SpriteFile& LoadSpriteFile(const std::string& name) {
    auto& files = SpriteFiles();
    auto it = files.find(name);
    if (it != files.end()) return it->second;

    SpriteFile file;
    const void* bytes = nullptr;
    size_t size = 0;
    ui::asset::DataOwnerPtr owner;
    if (ui::asset::Resolve(name, &bytes, &size, &owner) && bytes && size > 0) {
        std::string text(static_cast<const char*>(bytes), size);
        auto parsed = ui::uix::ParseTemplate(StripXmlProlog(std::move(text)));
        if (parsed.root) {
            if (parsed.root->tag == "symbol") {
                if (const char* id = AttrOf(*parsed.root, "id"))
                    file.symbols[id] = BuildSvgSymbol(*parsed.root);
            } else {
                CollectSymbols(*parsed.root, file.symbols);
            }
            file.loaded = true;
        }
    }
    return files.emplace(name, std::move(file)).first->second;
}

}  // namespace

SvgSymbol BuildSvgSymbol(const ui::uix::Node& symbolNode) {
    SvgSymbol sym;
    if (const char* vb = AttrOf(symbolNode, "viewBox")) {
        ParseViewBox(vb, sym.vpWidth, sym.vpHeight);
    }
    InheritedAttrs inherited;
    MergeInherited(symbolNode, inherited);
    CollectShapes(symbolNode, inherited, sym.shapes);
    return sym;
}

void RegisterSvgSymbol(const std::string& id, SvgSymbol symbol) {
    if (id.empty()) return;
    DocumentSymbols()[id] = std::move(symbol);
}

const SvgSymbol* ResolveSvgSymbol(const std::string& href) {
    const size_t hash = href.find('#');
    if (hash == std::string::npos || hash + 1 >= href.size()) return nullptr;
    const std::string id = href.substr(hash + 1);
    if (hash == 0) {
        auto& doc = DocumentSymbols();
        auto it = doc.find(id);
        return it == doc.end() ? nullptr : &it->second;
    }
    const SpriteFile& file = LoadSpriteFile(href.substr(0, hash));
    if (!file.loaded) return nullptr;
    auto it = file.symbols.find(id);
    return it == file.symbols.end() ? nullptr : &it->second;
}

void ResetSvgSymbols() {
    DocumentSymbols().clear();
    SpriteFiles().clear();
}

}  // namespace ui::page
