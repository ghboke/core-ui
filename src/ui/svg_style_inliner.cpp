#include "svg_style_inliner.h"

#include "css/css_parser.h"
#include "css/selector.h"
#include "css/value.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ui {

namespace {

inline bool IsWs(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

inline bool IsClassNameChar(unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_';
}

/* 找以 start ('<' 位置) 起的 tag 结束 '>' 位置. 引号内的 '>' 不算 (XML 规范).
 * 找不到返 npos. */
size_t FindTagEnd(const std::string& s, size_t start) {
    size_t i = start + 1;
    char quote = 0;
    while (i < s.size()) {
        char c = s[i];
        if (quote) {
            if (c == quote) quote = 0;
        } else {
            if (c == '"' || c == '\'') quote = c;
            else if (c == '>') return i;
        }
        i++;
    }
    return std::string::npos;
}

/* 在 [tag_start, tag_end] 范围内 (s[tag_start]=='<', s[tag_end]=='>') 找名为
 * name 的属性. 返 true + value 区间 [vstart, vend) (不含开闭引号).
 * 只匹配 ASCII attr name 大小写敏感 (XML spec). */
bool FindAttr(const std::string& s, size_t tag_start, size_t tag_end,
              const char* name, size_t& vstart, size_t& vend) {
    size_t name_len = std::strlen(name);
    size_t i = tag_start + 1;
    // 跳元素名
    while (i < tag_end && !IsWs((unsigned char)s[i]) && s[i] != '/' && s[i] != '>') i++;

    while (i < tag_end) {
        while (i < tag_end && IsWs((unsigned char)s[i])) i++;
        if (i >= tag_end) break;
        if (s[i] == '/') { i++; continue; }

        size_t an_s = i;
        while (i < tag_end && s[i] != '=' && !IsWs((unsigned char)s[i])
               && s[i] != '/' && s[i] != '>') i++;
        size_t an_e = i;
        if (an_s == an_e) { i++; continue; }  // 不应该, 保险跳一步

        // 跳到 '=' (允许 attr= value 中间空白)
        while (i < tag_end && IsWs((unsigned char)s[i])) i++;
        if (i >= tag_end || s[i] != '=') continue;  // 无值属性, 跳过
        i++;  // skip '='
        while (i < tag_end && IsWs((unsigned char)s[i])) i++;
        if (i >= tag_end) break;

        size_t v_s, v_e;
        char qc = s[i];
        if (qc == '"' || qc == '\'') {
            i++;
            v_s = i;
            while (i < tag_end && s[i] != qc) i++;
            v_e = i;
            if (i < tag_end) i++;  // skip closing quote
        } else {
            v_s = i;
            while (i < tag_end && !IsWs((unsigned char)s[i])
                   && s[i] != '/' && s[i] != '>') i++;
            v_e = i;
        }

        if (an_e - an_s == name_len &&
            std::memcmp(s.data() + an_s, name, name_len) == 0) {
            vstart = v_s;
            vend = v_e;
            return true;
        }
    }
    return false;
}

// CSS scanner: 跳空白 + C 风格注释.
void SkipCssWs(const std::string& css, size_t& i) {
    while (i < css.size()) {
        if (IsWs((unsigned char)css[i])) { i++; }
        else if (i + 1 < css.size() && css[i] == '/' && css[i + 1] == '*') {
            i += 2;
            while (i + 1 < css.size() && !(css[i] == '*' && css[i + 1] == '/')) i++;
            if (i + 1 < css.size()) i += 2;
        } else break;
    }
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && IsWs((unsigned char)s[a])) a++;
    while (b > a && IsWs((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

/* 从 SVG 文本中抽出所有 <style ...>...</style> 块的 CSS 内容拼接.
 * 剥掉 <![CDATA[ ... ]]>. */
std::string ExtractStyleBlocks(const std::string& svg) {
    std::string css;
    size_t pos = 0;
    while (true) {
        size_t s = svg.find("<style", pos);
        if (s == std::string::npos) break;
        // 确认 <style 后是空白 / '>' (避免 <styleX> 误识)
        size_t after = s + 6;
        if (after < svg.size() && svg[after] != ' ' && svg[after] != '\t'
            && svg[after] != '\n' && svg[after] != '\r' && svg[after] != '>'
            && svg[after] != '/') {
            pos = after;
            continue;
        }
        size_t gt = FindTagEnd(svg, s);
        if (gt == std::string::npos) break;
        // 自闭合 <style/> 没内容
        if (gt > 0 && svg[gt - 1] == '/') { pos = gt + 1; continue; }
        size_t end = svg.find("</style>", gt + 1);
        if (end == std::string::npos) break;
        std::string block = svg.substr(gt + 1, end - gt - 1);
        // 剥 CDATA
        size_t cda = block.find("<![CDATA[");
        if (cda != std::string::npos) {
            size_t cdb = block.find("]]>", cda + 9);
            if (cdb != std::string::npos) {
                block = block.substr(cda + 9, cdb - cda - 9);
            }
        }
        css += block;
        css += '\n';
        pos = end + 8;  // strlen("</style>")
    }
    return css;
}

/* 解析 CSS 文本 → ".classname" / "*" 选择器 → properties 字符串.
 * 不接受复杂选择器. "*" 用来覆盖 Matplotlib 的全局 stroke-linejoin/cap 规则,
 * D2D SvgDocument 不会可靠应用 <style> 块, 需要提前内联。 */
void ParseCssRules(const std::string& css,
                   std::unordered_map<std::string, std::string>& rules) {
    size_t i = 0;
    while (i < css.size()) {
        SkipCssWs(css, i);
        if (i >= css.size()) break;

        // 读 selector list (到 '{')
        size_t sel_s = i;
        while (i < css.size() && css[i] != '{' && css[i] != '}') {
            // 在 selector 里也可能有 /* */ 注释, 简单跳过
            if (i + 1 < css.size() && css[i] == '/' && css[i + 1] == '*') {
                i += 2;
                while (i + 1 < css.size() && !(css[i] == '*' && css[i + 1] == '/')) i++;
                if (i + 1 < css.size()) i += 2;
            } else {
                i++;
            }
        }
        if (i >= css.size()) break;
        if (css[i] == '}') { i++; continue; }  // stray

        size_t sel_e = i;
        i++;  // skip '{'

        // 读 properties (到匹配的 '}'), 处理嵌套 (@media etc.)
        size_t prop_s = i;
        int depth = 1;
        while (i < css.size() && depth > 0) {
            char c = css[i];
            if (c == '{') depth++;
            else if (c == '}') {
                depth--;
                if (depth == 0) break;
            }
            i++;
        }
        if (depth != 0) break;
        size_t prop_e = i;
        i++;  // skip '}'

        // 跳 @ 开头的 at-rule (整段忽略)
        size_t s = sel_s;
        while (s < sel_e && IsWs((unsigned char)css[s])) s++;
        if (s < sel_e && css[s] == '@') continue;

        std::string props = Trim(css.substr(prop_s, prop_e - prop_s));
        if (props.empty()) continue;

        // 按逗号分割 selector
        size_t cs = sel_s;
        while (cs < sel_e) {
            while (cs < sel_e && IsWs((unsigned char)css[cs])) cs++;
            size_t ce = cs;
            while (ce < sel_e && css[ce] != ',') ce++;
            size_t tail = ce;
            while (tail > cs && IsWs((unsigned char)css[tail - 1])) tail--;

            if (tail == cs + 1 && css[cs] == '*') {
                auto it = rules.find("*");
                if (it == rules.end()) {
                    rules.emplace("*", props);
                } else {
                    if (!it->second.empty() && it->second.back() != ';') {
                        it->second += ';';
                    }
                    it->second += props;
                }
            } else if (tail > cs + 1 && css[cs] == '.') {
                // 只接受 ".classname" — 必须 '.' + 全部 IsClassNameChar
                bool ok = true;
                for (size_t k = cs + 1; k < tail; k++) {
                    if (!IsClassNameChar((unsigned char)css[k])) { ok = false; break; }
                }
                if (ok) {
                    std::string cls(css.data() + cs + 1, tail - cs - 1);
                    auto it = rules.find(cls);
                    if (it == rules.end()) {
                        rules.emplace(std::move(cls), props);
                    } else {
                        // 后定义追加 → CSS 语义: 后者覆盖前者 (在 inline style 里
                        // 同样是后者覆盖前者). 添加分隔符 ';' 避免上一条没收尾.
                        if (!it->second.empty() && it->second.back() != ';') {
                            it->second += ';';
                        }
                        it->second += props;
                    }
                }
            }
            if (ce < sel_e) cs = ce + 1; else break;
        }
    }
}

bool IsInlineStyleTarget(const std::string& s, size_t tag_s, size_t tag_e) {
    if (tag_s + 1 >= tag_e) return false;
    char c = s[tag_s + 1];
    if (c == '/' || c == '!' || c == '?') return false;
    size_t ns = tag_s + 1;
    size_t ne = ns;
    while (ne < tag_e && !IsWs((unsigned char)s[ne]) && s[ne] != '/' && s[ne] != '>') {
        ne++;
    }
    const size_t len = ne - ns;
    return !(len == 5 && std::memcmp(s.data() + ns, "style", 5) == 0);
}

size_t AttrInsertPos(const std::string& s, size_t tag_s, size_t tag_e);
bool TagElementName(const std::string& s, size_t tag_s, size_t tag_e,
                    size_t& ns, size_t& ne, bool& closing);
bool NameEq(const std::string& s, size_t ns, size_t ne, const char* name);
bool IsSelfClosing(const std::string& s, size_t ne, size_t tag_e);

std::string ToLowerAscii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool IEqualsAscii(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i]; i++) {
        char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
        char cb = static_cast<char>(std::tolower(static_cast<unsigned char>(b[i])));
        if (ca != cb) return false;
    }
    return i == a.size() && b[i] == '\0';
}

bool StripImportantSuffix(std::string& value) {
    std::string v = Trim(value);
    size_t bang = v.rfind('!');
    if (bang == std::string::npos) {
        value = std::move(v);
        return false;
    }

    std::string tail = Trim(v.substr(bang + 1));
    if (!IEqualsAscii(tail, "important")) {
        value = std::move(v);
        return false;
    }

    value = Trim(v.substr(0, bang));
    return true;
}

bool IsSvgInlineStyleProperty(const std::string& prop) {
    if (prop.empty()) return false;
    if (prop.size() >= 2 && prop[0] == '-' && prop[1] == '-') return false;

    static const char* kProps[] = {
        "alignment-baseline",
        "background-color",
        "baseline-shift",
        "clip",
        "clip-path",
        "clip-rule",
        "color",
        "color-interpolation",
        "color-interpolation-filters",
        "direction",
        "display",
        "dominant-baseline",
        "fill",
        "fill-opacity",
        "fill-rule",
        "filter",
        "flood-color",
        "flood-opacity",
        "font",
        "font-family",
        "font-size",
        "font-size-adjust",
        "font-stretch",
        "font-style",
        "font-variant",
        "font-weight",
        "glyph-orientation-horizontal",
        "glyph-orientation-vertical",
        "image-rendering",
        "letter-spacing",
        "lighting-color",
        "marker",
        "marker-end",
        "marker-mid",
        "marker-start",
        "mask",
        "opacity",
        "overflow",
        "paint-order",
        "pointer-events",
        "shape-rendering",
        "stop-color",
        "stop-opacity",
        "stroke",
        "stroke-dasharray",
        "stroke-dashoffset",
        "stroke-linecap",
        "stroke-linejoin",
        "stroke-miterlimit",
        "stroke-opacity",
        "stroke-width",
        "text-anchor",
        "text-decoration",
        "text-rendering",
        "transform",
        "unicode-bidi",
        "vector-effect",
        "visibility",
        "white-space",
        "word-spacing",
        "writing-mode",
    };

    for (const char* p : kProps) {
        if (prop == p) return true;
    }
    return false;
}

bool IsSvgPresentationAttribute(const std::string& prop) {
    if (prop.empty()) return false;

    static const char* kAttrs[] = {
        "alignment-baseline",
        "baseline-shift",
        "clip-path",
        "clip-rule",
        "color",
        "color-interpolation",
        "color-interpolation-filters",
        "direction",
        "display",
        "dominant-baseline",
        "fill",
        "fill-opacity",
        "fill-rule",
        "filter",
        "flood-color",
        "flood-opacity",
        "font-family",
        "font-size",
        "font-size-adjust",
        "font-stretch",
        "font-style",
        "font-variant",
        "font-weight",
        "glyph-orientation-horizontal",
        "glyph-orientation-vertical",
        "image-rendering",
        "letter-spacing",
        "lighting-color",
        "marker",
        "marker-end",
        "marker-mid",
        "marker-start",
        "mask",
        "opacity",
        "overflow",
        "paint-order",
        "pointer-events",
        "shape-rendering",
        "stop-color",
        "stop-opacity",
        "stroke",
        "stroke-dasharray",
        "stroke-dashoffset",
        "stroke-linecap",
        "stroke-linejoin",
        "stroke-miterlimit",
        "stroke-opacity",
        "stroke-width",
        "text-anchor",
        "text-decoration",
        "text-rendering",
        "transform",
        "unicode-bidi",
        "vector-effect",
        "visibility",
        "word-spacing",
        "writing-mode",
    };

    for (const char* p : kAttrs) {
        if (prop == p) return true;
    }
    return false;
}

void ParseClassList(const std::string& value, std::vector<std::string>& out) {
    size_t i = 0;
    while (i < value.size()) {
        while (i < value.size() && IsWs(static_cast<unsigned char>(value[i]))) i++;
        size_t j = i;
        while (j < value.size() && !IsWs(static_cast<unsigned char>(value[j]))) j++;
        if (j > i) out.emplace_back(value.data() + i, j - i);
        i = j;
    }
}

void ParseTagAttributes(const std::string& s, size_t tag_start, size_t tag_end,
                        ui::css::MatchNode& node) {
    size_t i = tag_start + 1;
    while (i < tag_end && !IsWs(static_cast<unsigned char>(s[i]))
           && s[i] != '/' && s[i] != '>') {
        i++;
    }

    while (i < tag_end) {
        while (i < tag_end && IsWs(static_cast<unsigned char>(s[i]))) i++;
        if (i >= tag_end) break;
        if (s[i] == '/') { i++; continue; }

        size_t an_s = i;
        while (i < tag_end && s[i] != '=' && !IsWs(static_cast<unsigned char>(s[i]))
               && s[i] != '/' && s[i] != '>') {
            i++;
        }
        size_t an_e = i;
        if (an_s == an_e) { i++; continue; }

        while (i < tag_end && IsWs(static_cast<unsigned char>(s[i]))) i++;

        std::string name(s.data() + an_s, an_e - an_s);
        std::string value;
        if (i < tag_end && s[i] == '=') {
            i++;
            while (i < tag_end && IsWs(static_cast<unsigned char>(s[i]))) i++;
            if (i < tag_end) {
                char qc = s[i];
                if (qc == '"' || qc == '\'') {
                    i++;
                    size_t v_s = i;
                    while (i < tag_end && s[i] != qc) i++;
                    value.assign(s.data() + v_s, i - v_s);
                    if (i < tag_end) i++;
                } else {
                    size_t v_s = i;
                    while (i < tag_end && !IsWs(static_cast<unsigned char>(s[i]))
                           && s[i] != '/' && s[i] != '>') {
                        i++;
                    }
                    value.assign(s.data() + v_s, i - v_s);
                }
            }
        }

        node.attrs[name] = value;
        if (name == "id") {
            node.id = value;
        } else if (name == "class") {
            ParseClassList(value, node.classes);
        }
    }
}

struct SvgResolvedDecl {
    std::string property;
    std::string value;
    bool important = false;
    int specificity = 0;
    int sourceOrder = 0;
};

struct SvgStyleResult {
    std::vector<SvgResolvedDecl> decls;
    std::unordered_map<std::string, size_t> index;
};

void ApplySvgDecl(SvgStyleResult& out, SvgResolvedDecl decl) {
    auto it = out.index.find(decl.property);
    if (it == out.index.end()) {
        out.index.emplace(decl.property, out.decls.size());
        out.decls.push_back(std::move(decl));
        return;
    }

    SvgResolvedDecl& cur = out.decls[it->second];
    const bool wins =
        (decl.important != cur.important) ? decl.important :
        (decl.specificity != cur.specificity) ? (decl.specificity > cur.specificity) :
        (decl.sourceOrder >= cur.sourceOrder);
    if (wins) cur = std::move(decl);
}

bool AddDeclaration(SvgStyleResult& out, const ui::css::Declaration& d,
                    int specificity, int sourceOrder) {
    std::string prop = ToLowerAscii(Trim(d.property));
    if (!IsSvgInlineStyleProperty(prop)) return false;

    std::string value = d.value;
    bool important = StripImportantSuffix(value);
    if (value.empty()) return false;

    SvgResolvedDecl resolved;
    resolved.property = std::move(prop);
    resolved.value = std::move(value);
    resolved.important = important;
    resolved.specificity = specificity;
    resolved.sourceOrder = sourceOrder;
    ApplySvgDecl(out, std::move(resolved));
    return true;
}

bool ComputeSvgStyleForNode(const ui::css::Stylesheet& sheet,
                            const ui::css::MatchNode& node,
                            const std::vector<ui::css::Declaration>& inlineDecls,
                            SvgStyleResult& out) {
    bool sawStylesheetDecl = false;
    int order = 0;

    for (const auto& rule : sheet.rules) {
        int bestSpec = -1;
        for (const auto& sel : rule.selectors) {
            if (ui::css::Matches(sel, node)) {
                bestSpec = std::max(bestSpec, ui::css::Specificity(sel));
            }
        }

        for (const auto& d : rule.declarations) {
            if (bestSpec >= 0) {
                if (AddDeclaration(out, d, bestSpec, order)) {
                    sawStylesheetDecl = true;
                }
            }
            order++;
        }
    }

    for (const auto& d : inlineDecls) {
        AddDeclaration(out, d, 1000000, order++);
    }

    return sawStylesheetDecl;
}

std::string SerializeSvgStyle(const SvgStyleResult& style) {
    std::string out;
    for (const auto& d : style.decls) {
        if (d.property.empty() || d.value.empty()) continue;
        if (!out.empty()) out += ';';
        out += d.property;
        out += ':';
        out += d.value;
    }
    return out;
}

std::string EscapeStyleAttr(const std::string& value, char quote) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (c == '<') {
            out += "&lt;";
        } else if (c == '"' && quote == '"') {
            out += "&quot;";
        } else if (c == '\'' && quote == '\'') {
            out += "&apos;";
        } else {
            out += c;
        }
    }
    return out;
}

std::string EscapeXmlAttr(const std::string& value, char quote) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (c == '&') {
            out += "&amp;";
        } else if (c == '<') {
            out += "&lt;";
        } else if (c == '"' && quote == '"') {
            out += "&quot;";
        } else if (c == '\'' && quote == '\'') {
            out += "&apos;";
        } else {
            out += c;
        }
    }
    return out;
}

void UpsertTagAttr(std::string& tag_xml, const std::string& name, const std::string& value) {
    if (tag_xml.empty() || tag_xml.front() != '<') return;

    const size_t tag_e = tag_xml.size() - 1;
    size_t v_s = 0, v_e = 0;
    if (FindAttr(tag_xml, 0, tag_e, name.c_str(), v_s, v_e)) {
        char quote = '"';
        if (v_s > 0 && (tag_xml[v_s - 1] == '"' || tag_xml[v_s - 1] == '\'')) {
            quote = tag_xml[v_s - 1];
            tag_xml.replace(v_s, v_e - v_s, EscapeXmlAttr(value, quote));
        } else {
            tag_xml.replace(v_s, v_e - v_s, "\"" + EscapeXmlAttr(value, '"') + "\"");
        }
        return;
    }

    size_t insert_at = AttrInsertPos(tag_xml, 0, tag_e);
    std::string attr = " ";
    attr += name;
    attr += "=\"";
    attr += EscapeXmlAttr(value, '"');
    attr += '"';
    tag_xml.insert(insert_at, attr);
}

std::string BuildStyledTag(const std::string& svg_xml, size_t tag_s, size_t tag_e,
                           bool has_style, size_t sty_vs, size_t sty_ve,
                           const std::string& style_text,
                           const SvgStyleResult& style) {
    std::string tag_xml(svg_xml, tag_s, tag_e - tag_s + 1);

    if (has_style) {
        char quote = '"';
        if (sty_vs > tag_s && (svg_xml[sty_vs - 1] == '"' || svg_xml[sty_vs - 1] == '\'')) {
            quote = svg_xml[sty_vs - 1];
        }
        tag_xml.replace(sty_vs - tag_s, sty_ve - sty_vs,
                        EscapeStyleAttr(style_text, quote));
    } else {
        size_t insert_at = AttrInsertPos(svg_xml, tag_s, tag_e) - tag_s;
        std::string attr = " style=\"";
        attr += EscapeStyleAttr(style_text, '"');
        attr += '"';
        tag_xml.insert(insert_at, attr);
    }

    // D2D can let presentation attrs like fill="none" win over inline style.
    // Mirror resolved CSS values into attrs so Mermaid rough paths stay filled.
    for (const auto& d : style.decls) {
        if (IsSvgPresentationAttribute(d.property)) {
            UpsertTagAttr(tag_xml, d.property, d.value);
        }
    }
    return tag_xml;
}

struct SvgPaintNormalization {
    bool        changed    = false;
    std::string color;
    bool        hasOpacity = false;
    std::string opacity;
};

std::string SvgHexColor(const ui::css::Color& color) {
    auto channel = [](float v) -> int {
        return static_cast<int>(std::round(std::clamp(v, 0.0f, 1.0f) * 255.0f));
    };
    char buf[8] = {};
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X",
                  channel(color.r), channel(color.g), channel(color.b));
    return std::string(buf);
}

std::string SvgOpacityText(float opacity) {
    char buf[32] = {};
    std::snprintf(buf, sizeof(buf), "%.6g", std::clamp(opacity, 0.0f, 1.0f));
    return std::string(buf);
}

float ParseSvgOpacityText(const std::string& value) {
    const std::string trimmed = Trim(value);
    if (trimmed.empty()) return 1.0f;
    char* end = nullptr;
    const float opacity = std::strtof(trimmed.c_str(), &end);
    if (end == trimmed.c_str() || !end || *end != '\0') return 1.0f;
    return std::clamp(opacity, 0.0f, 1.0f);
}

/* Direct2D 的 SVG 解析器只认 SVG 1.1 命名色, 不认 CSS 的 transparent:
 * fill="transparent" 会让 GetAttributeValue(L"fill") 直接返回 E_INVALIDARG,
 * 跟"根本没写"一样, 于是 fill 回落到初始值黑色。ProcessOn 思维导图导出的
 * 整幅背景就是 <rect fill="transparent">, 不转就是一整块黑底盖住全图。
 * transparent 在视觉上等价于 none, 直接换掉即可 (不必补 fill-opacity)。 */
bool IsTransparentPaintKeyword(const std::string& value) {
    return ToLowerAscii(Trim(value)) == "transparent";
}

/* D2D 认得的 paint server 只有 linearGradient / radialGradient。fill="url(#p)"
 * 指到 <pattern> 或指空时, D2D 既不报错也不跳过, 而是**画成黑色** (实测: 离屏
 * 渲染 url(#pattern) / url(#missing) 中心像素都是 0,0,0,255)。ProcessOn 导出的
 * 水印层就是一块盖满全图的 fill="url(#pattern_mark_0)", 于是整幅变黑。
 *
 * SVG 的 <paint> 语法允许在 url() 后跟一个回退值, D2D 是认这个回退的
 * (url(#missing) none 实测不画)。所以对 D2D 解不出来的引用补一个 " none"。
 *
 * 例外: 带 <image> 的 pattern 不动 —— 这类文件整图会回落 LunaSVG 光栅渲染
 * (见 gh_img_view.cpp 的 NeedsLunaSvgMainRenderer_), 那条路径能正常画 pattern。 */
struct SvgPaintRefs {
    std::unordered_set<std::string> d2d_resolvable;   // 渐变 id
    std::unordered_set<std::string> leave_alone;      // 图片填充的 pattern id
};

bool TagNameIs(const std::string& s, size_t tag_start, size_t tag_end,
               const char* name) {
    const size_t n = std::strlen(name);
    if (tag_start + 1 + n > tag_end) return false;
    if (s.compare(tag_start + 1, n, name) != 0) return false;
    const char after = s[tag_start + 1 + n];
    return IsWs((unsigned char)after) || after == '>' || after == '/';
}

std::string TagAttrValue(const std::string& s, size_t tag_start, size_t tag_end,
                         const char* name) {
    size_t vs = 0, ve = 0;
    if (!FindAttr(s, tag_start, tag_end, name, vs, ve)) return {};
    return s.substr(vs, ve - vs);
}

SvgPaintRefs CollectSvgPaintRefs(const std::string& svg_xml) {
    SvgPaintRefs refs;
    size_t i = 0;
    while ((i = svg_xml.find('<', i)) != std::string::npos) {
        const size_t tag_end = FindTagEnd(svg_xml, i);
        if (tag_end == std::string::npos) break;

        const bool is_gradient = TagNameIs(svg_xml, i, tag_end, "linearGradient") ||
                                 TagNameIs(svg_xml, i, tag_end, "radialGradient");
        const bool is_pattern = TagNameIs(svg_xml, i, tag_end, "pattern");
        if (is_gradient || is_pattern) {
            std::string id = TagAttrValue(svg_xml, i, tag_end, "id");
            if (!id.empty()) {
                if (is_gradient) {
                    refs.d2d_resolvable.insert(std::move(id));
                } else {
                    // pattern 里有 <image> = 位图填充, 整图会走 LunaSVG, 别动它。
                    const size_t close = svg_xml.find("</pattern>", tag_end);
                    const size_t scan_end = (close == std::string::npos) ? svg_xml.size() : close;
                    const size_t img = svg_xml.find("<image", tag_end);
                    if (img != std::string::npos && img < scan_end) {
                        refs.leave_alone.insert(std::move(id));
                    }
                }
            }
        }
        i = tag_end + 1;
    }
    return refs;
}

/* url(#id) 且 D2D 解不出来时回填 "url(#id) none"。已经自带回退值的不动。 */
bool AddUrlPaintFallback(const std::string& value, const SvgPaintRefs& refs,
                         std::string& out_value) {
    const std::string trimmed = Trim(value);
    if (ToLowerAscii(trimmed.substr(0, 4)) != "url(") return false;
    const size_t rp = trimmed.find(')');
    if (rp == std::string::npos) return false;
    if (Trim(trimmed.substr(rp + 1)).size() > 0) return false;   // 已有回退值

    std::string id = Trim(trimmed.substr(4, rp - 4));
    if (!id.empty() && (id.front() == '"' || id.front() == '\'')) {
        if (id.size() < 2) return false;
        id = id.substr(1, id.size() - 2);
    }
    if (id.empty() || id.front() != '#') return false;
    id.erase(0, 1);
    if (refs.d2d_resolvable.count(id) || refs.leave_alone.count(id)) return false;

    out_value = trimmed + " none";
    return true;
}

bool NormalizeSvgRgbPaint(const std::string& value,
                          std::string& out_color, float& out_alpha) {
    const std::string trimmed = Trim(value);
    const std::string lower = ToLowerAscii(trimmed);
    const bool is_rgb = lower.compare(0, 4, "rgb(") == 0;
    const bool is_rgba = lower.compare(0, 5, "rgba(") == 0;
    if (!is_rgb && !is_rgba) return false;

    ui::css::Color color;
    if (!ui::css::ParseColor(trimmed, color)) return false;
    out_color = SvgHexColor(color);
    out_alpha = std::clamp(color.a, 0.0f, 1.0f);
    return true;
}

float SvgTagOpacity(const std::string& tag_xml, const char* name) {
    size_t value_start = 0, value_end = 0;
    if (!FindAttr(tag_xml, 0, tag_xml.size() - 1, name, value_start, value_end)) {
        return 1.0f;
    }
    return ParseSvgOpacityText(tag_xml.substr(value_start, value_end - value_start));
}

void NormalizeSvgPaintAttribute(std::string& tag_xml,
                                const char* paint_name,
                                const char* opacity_name,
                                float original_opacity,
                                const SvgPaintRefs& refs) {
    size_t value_start = 0, value_end = 0;
    if (!FindAttr(tag_xml, 0, tag_xml.size() - 1,
                  paint_name, value_start, value_end)) {
        return;
    }

    const std::string raw = tag_xml.substr(value_start, value_end - value_start);
    if (IsTransparentPaintKeyword(raw)) {
        UpsertTagAttr(tag_xml, paint_name, "none");
        return;
    }
    std::string url_fallback;
    if (AddUrlPaintFallback(raw, refs, url_fallback)) {
        UpsertTagAttr(tag_xml, paint_name, url_fallback);
        return;
    }

    std::string color;
    float alpha = 1.0f;
    if (!NormalizeSvgRgbPaint(raw, color, alpha)) {
        return;
    }
    UpsertTagAttr(tag_xml, paint_name, color);
    if (alpha < 0.999999f) {
        UpsertTagAttr(tag_xml, opacity_name,
                      SvgOpacityText(alpha * original_opacity));
    }
}

void NormalizeSvgInlinePaint(std::vector<ui::css::Declaration>& declarations,
                             const char* paint_name,
                             const char* opacity_name,
                             float original_opacity,
                             const SvgPaintRefs& refs,
                             SvgPaintNormalization& out) {
    size_t paint_index = declarations.size();
    size_t opacity_index = declarations.size();
    for (size_t i = 0; i < declarations.size(); ++i) {
        const std::string property = ToLowerAscii(Trim(declarations[i].property));
        if (property == paint_name) paint_index = i;
        if (property == opacity_name) opacity_index = i;
    }
    if (paint_index == declarations.size()) return;

    std::string paint_value = declarations[paint_index].value;
    const bool paint_important = StripImportantSuffix(paint_value);
    if (IsTransparentPaintKeyword(paint_value)) {
        declarations[paint_index].value = paint_important ? "none !important" : "none";
        out.changed = true;
        out.color = "none";
        out.hasOpacity = false;
        return;
    }
    std::string url_fallback;
    if (AddUrlPaintFallback(paint_value, refs, url_fallback)) {
        declarations[paint_index].value =
            paint_important ? url_fallback + " !important" : url_fallback;
        out.changed = true;
        out.color = std::move(url_fallback);
        out.hasOpacity = false;
        return;
    }
    std::string color;
    float alpha = 1.0f;
    if (!NormalizeSvgRgbPaint(paint_value, color, alpha)) return;

    float opacity = original_opacity;
    bool has_opacity = false;
    bool opacity_important = false;
    if (opacity_index != declarations.size()) {
        std::string opacity_value = declarations[opacity_index].value;
        opacity_important = StripImportantSuffix(opacity_value);
        opacity = ParseSvgOpacityText(opacity_value);
        has_opacity = true;
    }
    const float combined_opacity = alpha * opacity;

    declarations[paint_index].value = color;
    if (paint_important) declarations[paint_index].value += " !important";
    if (has_opacity) {
        declarations[opacity_index].value = SvgOpacityText(combined_opacity);
        if (opacity_important) declarations[opacity_index].value += " !important";
    } else if (combined_opacity < 0.999999f) {
        declarations.push_back({opacity_name, SvgOpacityText(combined_opacity), 0, 0});
        has_opacity = true;
    }

    out.changed = true;
    out.color = std::move(color);
    out.hasOpacity = has_opacity || combined_opacity < 0.999999f ||
                     original_opacity < 0.999999f;
    if (out.hasOpacity) out.opacity = SvgOpacityText(combined_opacity);
}

std::string SerializeInlineSvgStyle(const std::vector<ui::css::Declaration>& declarations) {
    std::string out;
    for (const auto& decl : declarations) {
        const std::string property = Trim(decl.property);
        const std::string value = Trim(decl.value);
        if (property.empty() || value.empty()) continue;
        if (!out.empty()) out += ';';
        out += property;
        out += ':';
        out += value;
    }
    return out;
}

void NormalizeSvgPaintColorsInTag(std::string& tag_xml, const SvgPaintRefs& refs) {
    if (tag_xml.size() < 3 || tag_xml.front() != '<' || tag_xml[1] == '/') return;

    const float fill_opacity = SvgTagOpacity(tag_xml, "fill-opacity");
    const float stroke_opacity = SvgTagOpacity(tag_xml, "stroke-opacity");
    NormalizeSvgPaintAttribute(tag_xml, "fill", "fill-opacity", fill_opacity, refs);
    NormalizeSvgPaintAttribute(tag_xml, "stroke", "stroke-opacity", stroke_opacity, refs);

    size_t style_start = 0, style_end = 0;
    if (!FindAttr(tag_xml, 0, tag_xml.size() - 1,
                  "style", style_start, style_end)) {
        return;
    }

    auto declarations = ui::css::ParseInlineStyle(
        tag_xml.substr(style_start, style_end - style_start));
    SvgPaintNormalization fill;
    SvgPaintNormalization stroke;
    NormalizeSvgInlinePaint(declarations, "fill", "fill-opacity", fill_opacity, refs, fill);
    NormalizeSvgInlinePaint(declarations, "stroke", "stroke-opacity", stroke_opacity, refs, stroke);
    if (!fill.changed && !stroke.changed) return;

    UpsertTagAttr(tag_xml, "style", SerializeInlineSvgStyle(declarations));
    if (fill.changed) {
        UpsertTagAttr(tag_xml, "fill", fill.color);
        if (fill.hasOpacity) UpsertTagAttr(tag_xml, "fill-opacity", fill.opacity);
    }
    if (stroke.changed) {
        UpsertTagAttr(tag_xml, "stroke", stroke.color);
        if (stroke.hasOpacity) UpsertTagAttr(tag_xml, "stroke-opacity", stroke.opacity);
    }
}

struct SvgStackFrame {
    std::string tag;
    ui::css::MatchNode node;
};

void PopSvgStack(std::deque<SvgStackFrame>& stack, const std::string& tag) {
    while (!stack.empty()) {
        bool same = stack.back().tag == tag;
        stack.pop_back();
        if (same) break;
    }
}

}  // namespace

std::string InlineSvgStyleClasses(const std::string& svg_xml) {
    // 短路: 大多数 icon SVG 没 <style> 块
    if (svg_xml.find("<style") == std::string::npos) {
        return svg_xml;
    }

    std::string css = ExtractStyleBlocks(svg_xml);
    if (css.empty()) return svg_xml;

    auto parsed = ui::css::ParseStylesheet(css);
    if (parsed.stylesheet.rules.empty()) return svg_xml;

    std::string out;
    out.reserve(svg_xml.size() + svg_xml.size() / 8);  // ~12% 增长预算

    size_t i = 0;
    const size_t N = svg_xml.size();
    bool saw_root = false;
    std::deque<SvgStackFrame> stack;

    while (i < N) {
        if (svg_xml[i] != '<') {
            out += svg_xml[i++];
            continue;
        }

        size_t tag_s = i;
        size_t tag_e = FindTagEnd(svg_xml, tag_s);
        if (tag_e == std::string::npos) {
            // 损坏 SVG: dump 剩下原样
            out.append(svg_xml, tag_s, std::string::npos);
            break;
        }

        size_t ns = 0, ne = 0;
        bool closing = false;
        if (!TagElementName(svg_xml, tag_s, tag_e, ns, ne, closing)) {
            out.append(svg_xml, tag_s, tag_e - tag_s + 1);
            i = tag_e + 1;
            continue;
        }

        std::string tag(svg_xml.data() + ns, ne - ns);
        if (closing) {
            out.append(svg_xml, tag_s, tag_e - tag_s + 1);
            PopSvgStack(stack, tag);
            i = tag_e + 1;
            continue;
        }

        ui::css::MatchNode node;
        node.tag = tag;
        node.parent = stack.empty() ? nullptr : &stack.back().node;
        if (!saw_root) {
            node.stateBits |= ui::css::state::Root;
            saw_root = true;
        }
        ParseTagAttributes(svg_xml, tag_s, tag_e, node);

        const bool inline_target = IsInlineStyleTarget(svg_xml, tag_s, tag_e);
        const bool self_close = IsSelfClosing(svg_xml, ne, tag_e);

        if (!inline_target) {
            out.append(svg_xml, tag_s, tag_e - tag_s + 1);
            if (!self_close) stack.push_back(SvgStackFrame{std::move(tag), std::move(node)});
            i = tag_e + 1;
            continue;
        }

        // 已有 style="..." 吗?
        size_t sty_vs, sty_ve;
        bool has_style = FindAttr(svg_xml, tag_s, tag_e, "style", sty_vs, sty_ve);

        std::vector<ui::css::Declaration> inline_decls;
        if (has_style) {
            inline_decls = ui::css::ParseInlineStyle(svg_xml.substr(sty_vs, sty_ve - sty_vs));
        }

        SvgStyleResult style;
        const bool has_stylesheet_style =
            ComputeSvgStyleForNode(parsed.stylesheet, node, inline_decls, style);
        std::string style_text = SerializeSvgStyle(style);

        if (!has_stylesheet_style || style_text.empty()) {
            out.append(svg_xml, tag_s, tag_e - tag_s + 1);
            if (!self_close) stack.push_back(SvgStackFrame{std::move(tag), std::move(node)});
            i = tag_e + 1;
            continue;
        }

        out += BuildStyledTag(svg_xml, tag_s, tag_e,
                              has_style, sty_vs, sty_ve,
                              style_text, style);

        if (!self_close) stack.push_back(SvgStackFrame{std::move(tag), std::move(node)});
        i = tag_e + 1;
    }

    return out;
}

namespace {

/* 给定标签区间 [tag_s..tag_e] (s=='<', e=='>'), 返回插入新属性的位置:
 * 闭合 '>' 或 '/>' 之前 (跳过尾随空白和 '/'). 跟 InlineSvgStyleClasses 里
 * 没有 style 时的插入逻辑一致. */
size_t AttrInsertPos(const std::string& s, size_t tag_s, size_t tag_e) {
    size_t insert_at = tag_e;  // 此处是 '>'
    while (insert_at > tag_s + 1 && IsWs((unsigned char)s[insert_at - 1])) insert_at--;
    if (insert_at > tag_s + 1 && s[insert_at - 1] == '/') {
        insert_at--;
        while (insert_at > tag_s + 1 && IsWs((unsigned char)s[insert_at - 1])) insert_at--;
    }
    return insert_at;
}

/* 取标签元素名范围 [ns, ne). closing 置位表示是 `</name>` 闭合标签.
 * 注释 / CDATA / PI / DOCTYPE (`<!` / `<?` 开头) 返 false. */
bool TagElementName(const std::string& s, size_t tag_s, size_t tag_e,
                    size_t& ns, size_t& ne, bool& closing) {
    size_t i = tag_s + 1;
    closing = false;
    if (i < tag_e && s[i] == '/') { closing = true; i++; }
    if (i >= tag_e) return false;
    unsigned char c0 = (unsigned char)s[i];
    if (c0 == '!' || c0 == '?') return false;
    ns = i;
    while (i < tag_e && !IsWs((unsigned char)s[i]) && s[i] != '/' && s[i] != '>') i++;
    ne = i;
    return ne > ns;
}

bool NameEq(const std::string& s, size_t ns, size_t ne, const char* name) {
    size_t len = std::strlen(name);
    return (ne - ns == len) && std::memcmp(s.data() + ns, name, len) == 0;
}

/* 标签是否自闭合 (`.../>`), 容忍 '/' 前后空白. */
bool IsSelfClosing(const std::string& s, size_t ne, size_t tag_e) {
    size_t k = tag_e;  // '>'
    while (k > ne && IsWs((unsigned char)s[k - 1])) k--;
    return k > ne && s[k - 1] == '/';
}

}  // namespace

std::string NormalizeSvgRefsAndSymbols(const std::string& xml) {
    // 短路: 既无 href 又无 <symbol> 就没活干 (覆盖绝大多数 inline-fill 图标).
    const bool any_href   = xml.find("href") != std::string::npos;
    const bool any_symbol = xml.find("<symbol") != std::string::npos;
    if (!any_href && !any_symbol) return xml;

    static const char* kXlinkNs =
        " xmlns:xlink=\"http://www.w3.org/1999/xlink\"";

    std::string out;
    out.reserve(xml.size() + xml.size() / 16 + 64);

    int defs_depth = 0;                 // 当前嵌套 <defs> 深度 (含注入的)
    std::vector<bool> sym_wrapped;      // 每个未闭合 <symbol> 是否套了注入 <defs>

    size_t i = 0;
    const size_t N = xml.size();
    while (i < N) {
        if (xml[i] != '<') { out += xml[i++]; continue; }

        size_t tag_s = i;
        size_t tag_e = FindTagEnd(xml, tag_s);
        if (tag_e == std::string::npos) {
            out.append(xml, tag_s, std::string::npos);  // 损坏 SVG, 原样 dump
            break;
        }

        size_t ns, ne; bool closing;
        if (!TagElementName(xml, tag_s, tag_e, ns, ne, closing)) {
            out.append(xml, tag_s, tag_e - tag_s + 1);   // 注释 / CDATA / PI
            i = tag_e + 1;
            continue;
        }

        // ---- 闭合标签 ----
        if (closing) {
            if (NameEq(xml, ns, ne, "symbol")) {
                out += "</g>";
                if (!sym_wrapped.empty()) {
                    bool wrapped = sym_wrapped.back();
                    sym_wrapped.pop_back();
                    if (wrapped) { out += "</defs>"; if (defs_depth > 0) defs_depth--; }
                }
            } else {
                if (NameEq(xml, ns, ne, "defs") && defs_depth > 0) defs_depth--;
                out.append(xml, tag_s, tag_e - tag_s + 1);
            }
            i = tag_e + 1;
            continue;
        }

        // ---- 起始 / 自闭合标签 ----
        const bool is_symbol = NameEq(xml, ns, ne, "symbol");
        const bool is_svg    = NameEq(xml, ns, ne, "svg");
        const bool is_defs   = NameEq(xml, ns, ne, "defs");
        const bool self_close = IsSelfClosing(xml, ne, tag_e);

        // 顶层 (defs_depth==0) 的 symbol 要套一层注入 <defs> 防止单独绘制.
        const bool wrap_symbol = is_symbol && (defs_depth == 0);
        if (wrap_symbol) { out += "<defs>"; defs_depth++; }

        // 计算要插入的属性: xlink:href 补全 + root svg 的 xmlns:xlink.
        std::string attr_ins;
        if (!is_symbol) {
            size_t hvs, hve, xvs, xve;
            bool has_href  = FindAttr(xml, tag_s, tag_e, "href", hvs, hve);
            bool has_xlink = FindAttr(xml, tag_s, tag_e, "xlink:href", xvs, xve);
            if (has_href && !has_xlink) {
                attr_ins += " xlink:href=\"";
                attr_ins.append(xml, hvs, hve - hvs);
                attr_ins += '"';
            }
        }
        if (is_svg) {
            size_t d0, d1;
            if (!FindAttr(xml, tag_s, tag_e, "xmlns:xlink", d0, d1)) attr_ins += kXlinkNs;
        }

        size_t insert_at = AttrInsertPos(xml, tag_s, tag_e);

        out.append(xml, tag_s, ns - tag_s);          // "<"  (闭合在上面已分流)
        if (is_symbol) out += 'g'; else out.append(xml, ns, ne - ns);  // 名 (symbol→g)
        out.append(xml, ne, insert_at - ne);         // 原属性串
        out += attr_ins;                             // 注入属性
        out.append(xml, insert_at, tag_e - insert_at + 1);  // 尾部 (含 '/' 和 '>')

        if (is_symbol && self_close && wrap_symbol) { out += "</defs>"; if (defs_depth > 0) defs_depth--; }

        if (is_defs && !self_close) defs_depth++;
        if (is_symbol && !self_close) sym_wrapped.push_back(wrap_symbol);

        i = tag_e + 1;
    }

    return out;
}

std::string NormalizeSvgPaintColorsForD2D(const std::string& svg_xml) {
    std::string out;
    out.reserve(svg_xml.size() + svg_xml.size() / 16);

    const SvgPaintRefs refs = CollectSvgPaintRefs(svg_xml);
    size_t i = 0;
    while (i < svg_xml.size()) {
        if (svg_xml[i] != '<') {
            out += svg_xml[i++];
            continue;
        }

        const size_t tag_start = i;
        const size_t tag_end = FindTagEnd(svg_xml, tag_start);
        if (tag_end == std::string::npos) {
            out.append(svg_xml, tag_start, std::string::npos);
            break;
        }

        std::string tag_xml(svg_xml, tag_start, tag_end - tag_start + 1);
        NormalizeSvgPaintColorsInTag(tag_xml, refs);
        out += tag_xml;
        i = tag_end + 1;
    }
    return out;
}

std::string LoadSvgWithInlinedStyles(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (1LL << 30)) {
        CloseHandle(h);
        return {};
    }
    std::string raw(static_cast<size_t>(sz.QuadPart), '\0');
    DWORD read = 0;
    BOOL ok = ReadFile(h, raw.data(), static_cast<DWORD>(sz.QuadPart), &read, nullptr);
    CloseHandle(h);
    if (!ok || read != static_cast<DWORD>(sz.QuadPart)) return {};
    /* 两道纯文本预处理, 顺序无关:
     *   ① InlineSvgStyleClasses — <style>.class{} → inline style (L48)
     *   ② NormalizeSvgRefsAndSymbols — href→xlink:href + <symbol>→<defs><g> (L86)
     * 处理后的 xml 同时喂 D2D 原生路径和 ParseSvgIcon fallback. */
    return NormalizeSvgRefsAndSymbols(InlineSvgStyleClasses(raw));
}

}  // namespace ui
