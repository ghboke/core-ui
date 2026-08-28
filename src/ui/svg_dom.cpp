#include "svg_dom.h"

#include <cctype>
#include <cstring>

namespace ui {
namespace svgdom {

namespace {

constexpr size_t kNpos = std::string::npos;

bool StartsWith(const std::string& s, size_t i, const char* lit) {
    const size_t n = std::strlen(lit);
    return s.size() >= i + n && s.compare(i, n, lit) == 0;
}

/* 从 s[i] == '"' 或 '\'' 起跳过整个带引号的串, 返回收尾引号的下一位;
 * 引号未闭合返回 kNpos。 */
size_t SkipQuoted(const std::string& s, size_t i) {
    const size_t close = s.find(s[i], i + 1);
    return close == kNpos ? kNpos : close + 1;
}

/* 扫描 s[i] 起的一个标记, 返回其结束位置 (末字符的下一位); 失败返 kNpos。
 * *isSpecial = true 表示注释 / CDATA / 处理指令 / DOCTYPE 这类原样透传的标记。
 *
 * 与旧的 svg.find('>') 相比, 这里尊重属性值内的引号, 所以 data-x="1 > 0" 不会
 * 再把标签截断; 注释和 CDATA 内的 '<' '>' 也不会被当成标签。 */
size_t ScanMarkupEnd(const std::string& s, size_t i, bool* isSpecial) {
    *isSpecial = true;
    if (StartsWith(s, i, "<!--")) {
        const size_t e = s.find("-->", i + 4);
        return e == kNpos ? kNpos : e + 3;
    }
    if (StartsWith(s, i, "<![CDATA[")) {
        const size_t e = s.find("]]>", i + 9);
        return e == kNpos ? kNpos : e + 3;
    }
    if (i + 1 < s.size() && s[i + 1] == '?') {
        const size_t e = s.find("?>", i + 2);
        return e == kNpos ? kNpos : e + 2;
    }
    if (i + 1 < s.size() && s[i + 1] == '!') {
        /* DOCTYPE 等声明: 内部子集 [...] 里可以出现 '>', 要配对跳过。 */
        size_t p = i + 2;
        int bracket = 0;
        while (p < s.size()) {
            const char c = s[p];
            if (c == '"' || c == '\'') {
                p = SkipQuoted(s, p);
                if (p == kNpos) return kNpos;
                continue;
            }
            if (c == '[') {
                ++bracket;
            } else if (c == ']') {
                if (bracket > 0) --bracket;
            } else if (c == '>' && bracket == 0) {
                return p + 1;
            }
            ++p;
        }
        return kNpos;
    }

    *isSpecial = false;
    size_t p = i + 1;
    while (p < s.size()) {
        const char c = s[p];
        if (c == '"' || c == '\'') {
            p = SkipQuoted(s, p);
            if (p == kNpos) return kNpos;
            continue;
        }
        if (c == '>') return p + 1;
        ++p;
    }
    return kNpos;
}

bool IsNameChar(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) != 0 || c == '_' || c == '-' || c == '.' || c == ':';
}

struct TagInfo {
    bool closing = false;
    bool selfClosing = false;
    std::string name;      // 已小写
};

/* 解析 [lt, end) 这一段普通标签文本。名字为空视为失败。 */
bool ParseTag(const std::string& s, size_t lt, size_t end, TagInfo* out) {
    if (end <= lt + 1) return false;
    size_t p = lt + 1;
    if (p < end && s[p] == '/') {
        out->closing = true;
        ++p;
    }
    while (p < end && std::isspace(static_cast<unsigned char>(s[p]))) ++p;

    const size_t nameStart = p;
    while (p < end && IsNameChar(s[p])) ++p;
    if (p == nameStart) return false;
    out->name.assign(s, nameStart, p - nameStart);
    for (char& c : out->name)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    /* 自闭合: '>' 之前最后一个非空白字符是 '/'。 */
    size_t tail = end - 1;                     // 指向 '>'
    while (tail > lt && std::isspace(static_cast<unsigned char>(s[tail - 1]))) --tail;
    out->selfClosing = tail > lt && s[tail - 1] == '/';
    if (out->closing && out->selfClosing) return false;
    return true;
}

/* 解析 pos 起的内容。
 *   parentName != nullptr —— 一直读到与之匹配的闭标签为止, 闭标签原文写入
 *                            *closeTagOut; 读到输入结束仍未匹配即失败。
 *   parentName == nullptr —— 读到输入结束; 中途遇到任何闭标签即失败 (多余闭标签)。
 * 任何结构错误返回 false, 调用方据此整体判 invalid。 */
bool ParseContent(const std::string& s, size_t& pos, const std::string* parentName,
                  std::vector<Node>& out, int depth, std::string* closeTagOut) {
    while (true) {
        const size_t lt = s.find('<', pos);
        if (lt == kNpos) {
            if (parentName != nullptr) return false;   // 缺闭标签
            if (pos < s.size()) {
                Node text;
                text.kind = NodeKind::Text;
                text.text.assign(s, pos, s.size() - pos);
                out.push_back(std::move(text));
            }
            pos = s.size();
            return true;
        }
        if (lt > pos) {
            Node text;
            text.kind = NodeKind::Text;
            text.text.assign(s, pos, lt - pos);
            out.push_back(std::move(text));
        }

        bool isSpecial = false;
        const size_t end = ScanMarkupEnd(s, lt, &isSpecial);
        if (end == kNpos) return false;

        if (isSpecial) {
            Node node;
            node.kind = NodeKind::Special;
            node.text.assign(s, lt, end - lt);
            out.push_back(std::move(node));
            pos = end;
            continue;
        }

        TagInfo tag;
        if (!ParseTag(s, lt, end, &tag)) return false;

        if (tag.closing) {
            if (parentName == nullptr) return false;       // 多余闭标签
            if (tag.name != *parentName) return false;     // 交叉嵌套
            if (closeTagOut) closeTagOut->assign(s, lt, end - lt);
            pos = end;
            return true;
        }

        Node node;
        node.kind = NodeKind::Element;
        node.name = tag.name;
        node.openTag.assign(s, lt, end - lt);
        node.selfClosing = tag.selfClosing;
        pos = end;

        if (!node.selfClosing) {
            if (depth >= kMaxDepth) return false;
            if (!ParseContent(s, pos, &node.name, node.children, depth + 1,
                              &node.closeTag)) {
                return false;
            }
        }
        out.push_back(std::move(node));
    }
}

} // namespace

std::string LocalName(const std::string& name) {
    const size_t colon = name.rfind(':');
    return colon == kNpos ? name : name.substr(colon + 1);
}

void SerializeNodeTo(const Node& node, std::string& out) {
    switch (node.kind) {
    case NodeKind::Text:
    case NodeKind::Special:
        out += node.text;
        return;
    case NodeKind::Element:
        out += node.openTag;
        for (const auto& child : node.children) SerializeNodeTo(child, out);
        out += node.closeTag;
        return;
    }
}

std::string SerializeNode(const Node& node) {
    std::string out;
    SerializeNodeTo(node, out);
    return out;
}

std::string Serialize(const Document& doc) {
    std::string out;
    out.reserve(doc.prologue.size() + doc.epilogue.size() + 256);
    out += doc.prologue;
    SerializeNodeTo(doc.root, out);
    out += doc.epilogue;
    return out;
}

Document Parse(const std::string& svg) {
    Document doc;
    if (svg.empty()) return doc;

    std::vector<Node> top;
    size_t pos = 0;
    if (!ParseContent(svg, pos, nullptr, top, 0, nullptr)) return doc;

    /* 顶层第一个 <svg> 元素作为根; 其前后的内容原样存进 prologue / epilogue,
     * 因为序列化是逐字节的, 拼回去必然等于原文。 */
    size_t rootIdx = top.size();
    for (size_t i = 0; i < top.size(); ++i) {
        if (top[i].kind == NodeKind::Element && LocalName(top[i].name) == "svg") {
            rootIdx = i;
            break;
        }
    }
    if (rootIdx == top.size()) return doc;

    for (size_t i = 0; i < rootIdx; ++i) SerializeNodeTo(top[i], doc.prologue);
    for (size_t i = rootIdx + 1; i < top.size(); ++i)
        SerializeNodeTo(top[i], doc.epilogue);

    doc.root = std::move(top[rootIdx]);
    doc.valid = true;
    return doc;
}

} // namespace svgdom
} // namespace ui
