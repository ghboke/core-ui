#include "svg_d2d_segments.h"

#include <cctype>
#include <cstring>

namespace ui {
namespace svgseg {

namespace {

using svgdom::LocalName;
using svgdom::Node;
using svgdom::NodeKind;

constexpr size_t kNpos = std::string::npos;

std::string LowerAscii(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/* 从原样开标签里取属性值。与 gh_img_view 的 ExtractXmlAttr_ 同语义, 这里独立一份
 * 是为了让本模块零依赖、可单独编进测试目标。 */
std::string AttrOf(const std::string& tag, const char* name) {
    const std::string key(name);
    size_t p = tag.find(key);
    while (p != kNpos) {
        const bool leftOk = p > 0 &&
            (std::isspace(static_cast<unsigned char>(tag[p - 1])) != 0);
        size_t q = p + key.size();
        while (q < tag.size() && std::isspace(static_cast<unsigned char>(tag[q]))) ++q;
        if (leftOk && q < tag.size() && tag[q] == '=') {
            size_t v = q + 1;
            while (v < tag.size() && std::isspace(static_cast<unsigned char>(tag[v]))) ++v;
            if (v >= tag.size()) return {};
            const char quote = tag[v];
            if (quote != '"' && quote != '\'') return {};
            const size_t e = tag.find(quote, v + 1);
            if (e == kNpos) return {};
            return tag.substr(v + 1, e - v - 1);
        }
        p = tag.find(key, p + key.size());
    }
    return {};
}

bool HasAttr(const std::string& tag, const char* name) {
    return !AttrOf(tag, name).empty();
}

/* url(#id) -> id。取第一个引用。 */
std::string UrlRefId(const std::string& value) {
    const size_t u = LowerAscii(value).find("url(");
    if (u == kNpos) return {};
    size_t p = u + 4;
    while (p < value.size() && std::isspace(static_cast<unsigned char>(value[p]))) ++p;
    if (p < value.size() && (value[p] == '"' || value[p] == '\'')) ++p;
    if (p >= value.size() || value[p] != '#') return {};
    ++p;
    const size_t start = p;
    while (p < value.size() && value[p] != ')' && value[p] != '"' &&
           value[p] != '\'' && !std::isspace(static_cast<unsigned char>(value[p]))) {
        ++p;
    }
    return value.substr(start, p - start);
}

/* 非渲染的资源定义元素 —— 每一段都原样保留, 不参与分段。
 * 与 gh_img_view 的 IsD2DLayerResourceElement_ 保持一致, 另加 filter/desc 等。 */
bool IsResourceElement(const std::string& name) {
    const std::string local = LocalName(name);
    return local == "defs" || local == "style" || local == "lineargradient" ||
           local == "radialgradient" || local == "pattern" || local == "clippath" ||
           local == "mask" || local == "marker" || local == "symbol" ||
           local == "filter" || local == "desc" || local == "title" ||
           local == "metadata";
}

/* 会画出像素的元素 (含 <g>/<a> 这类容器 —— 它们本身不画, 但其子树画)。 */
bool IsContainerElement(const std::string& name) {
    const std::string local = LocalName(name);
    return local == "g" || local == "a" || local == "switch";
}

/* D2D 的 ID2D1SvgDocument 不认这些元素, 留在文档里会让 CreateSvgDocument 失败。
 * 每一段都要把它们连同子树整个删掉 —— 包括藏在 <defs> 里的。
 * 与 gh_img_view 的 IsD2DUnsupportedSvgElement_ 一致。
 * 滤镜参数在分段前已由调用方从 <filter> 里读走, 删掉不丢信息。 */
bool IsD2DUnsupportedElement(const std::string& name) {
    const std::string local = LocalName(name);
    return local == "filter" || local == "foreignobject";
}

// ---------------------------------------------------------------------------
// 前序编号 —— 给每个元素一个序号和它的子树区间 [index, end), 供裁剪时判断
// "本节点是否包含某个被选中的元素"。
// ---------------------------------------------------------------------------

struct Indexed {
    const Node* node = nullptr;
    int index = 0;
    int end = 0;               // 子树末尾 (开区间)
    int parent = -1;
    bool resource = false;
};

void IndexTree(const Node& n, int parent, std::vector<Indexed>& out) {
    if (n.kind != NodeKind::Element) return;
    const int self = static_cast<int>(out.size());
    out.push_back(Indexed{&n, self, self + 1, parent, IsResourceElement(n.name)});
    for (const auto& child : n.children) IndexTree(child, self, out);
    out[static_cast<size_t>(self)].end = static_cast<int>(out.size());
}

// ---------------------------------------------------------------------------
// 裁剪 —— 保留资源元素、被选中的元素子树, 以及它们的祖先链; 其余渲染内容删掉。
// ---------------------------------------------------------------------------

struct PruneCtx {
    const std::vector<Indexed>* flat = nullptr;
    const std::vector<char>* selected = nullptr;   // 按前序序号
    int cursor = 0;                                // 当前遍历到的序号
    int filteredSrcIndex = -1;                     // 被选中的 filter 元素(若有)
    int filteredDstIndex = -1;                     // 它在裁剪副本里的前序序号
    int dstCounter = 0;
};

bool SubtreeHasSelected(const PruneCtx& ctx, int index, int end) {
    for (int i = index; i < end; ++i) {
        if ((*ctx.selected)[static_cast<size_t>(i)]) return true;
    }
    return false;
}

/* 返回 true 表示这个节点要保留。out 收到裁剪后的副本。 */
bool PruneNode(const Node& src, PruneCtx& ctx, Node& out);

void PruneChildren(const std::vector<Node>& src, PruneCtx& ctx,
                   std::vector<Node>& out) {
    for (const auto& child : src) {
        if (child.kind != NodeKind::Element) {
            /* 文本 / 注释跟着父元素走。父元素若被保留, 这些原样保留 —— 对
             * <text>hello</text> 这类内容是必须的。 */
            out.push_back(child);
            continue;
        }
        if (IsD2DUnsupportedElement(child.name)) {
            /* D2D 不认的元素整棵丢掉, 但序号游标要跨过去。 */
            ctx.cursor = (*ctx.flat)[static_cast<size_t>(ctx.cursor)].end;
            continue;
        }
        Node copy;
        if (PruneNode(child, ctx, copy)) out.push_back(std::move(copy));
    }
}

/* 深拷贝子树, 只剔掉 D2D 不认的元素。返回实际拷进副本的元素节点数 —— 调用方靠
 * 这个精确推进 dstCounter, 否则 filteredDstIndex 之后的序号会错位。 */
int CopyDroppingUnsupported(const Node& src, Node& out) {
    out.kind = src.kind;
    out.name = src.name;
    out.openTag = src.openTag;
    out.closeTag = src.closeTag;
    out.text = src.text;
    out.selfClosing = src.selfClosing;
    out.children.clear();

    int count = (src.kind == NodeKind::Element) ? 1 : 0;
    for (const auto& child : src.children) {
        if (child.kind == NodeKind::Element && IsD2DUnsupportedElement(child.name))
            continue;
        Node copy;
        count += CopyDroppingUnsupported(child, copy);
        out.children.push_back(std::move(copy));
    }
    return count;
}

bool PruneNode(const Node& src, PruneCtx& ctx, Node& out) {
    const int index = ctx.cursor;
    const Indexed& info = (*ctx.flat)[static_cast<size_t>(index)];
    const bool isSelected = (*ctx.selected)[static_cast<size_t>(index)] != 0;

    if (info.resource || isSelected) {
        /* 整棵子树保留 (只剔 D2D 不认的元素)。 */
        if (isSelected && index == ctx.filteredSrcIndex)
            ctx.filteredDstIndex = ctx.dstCounter;
        ctx.dstCounter += CopyDroppingUnsupported(src, out);
        ctx.cursor = info.end;
        return true;
    }

    if (!SubtreeHasSelected(ctx, index, info.end)) {
        ctx.cursor = info.end;                // 整棵子树丢弃
        return false;
    }

    /* 祖先链上的节点: 保留自己的开闭标签, 递归裁剪子节点。 */
    out.kind = NodeKind::Element;
    out.name = src.name;
    out.openTag = src.openTag;
    out.closeTag = src.closeTag;
    out.selfClosing = src.selfClosing;
    ++ctx.dstCounter;
    ctx.cursor = index + 1;
    PruneChildren(src.children, ctx, out.children);
    ctx.cursor = info.end;
    return true;
}

/* 按 selected 集合裁剪出一份副本。filteredSrc 为被选中的 filter 元素序号(无则 -1),
 * 回填它在副本里的序号。 */
svgdom::Document PruneDocument(const svgdom::Document& doc,
                               const std::vector<Indexed>& flat,
                               const std::vector<char>& selected,
                               int filteredSrc, int* filteredDst) {
    PruneCtx ctx;
    ctx.flat = &flat;
    ctx.selected = &selected;
    ctx.cursor = 0;
    ctx.filteredSrcIndex = filteredSrc;
    ctx.dstCounter = 0;

    svgdom::Document out;
    out.valid = true;
    out.prologue = doc.prologue;
    out.epilogue = doc.epilogue;
    Node root;
    /* 根 <svg> 永远保留 (它是所有内容的祖先)。 */
    root.kind = NodeKind::Element;
    root.name = doc.root.name;
    root.openTag = doc.root.openTag;
    root.closeTag = doc.root.closeTag;
    root.selfClosing = doc.root.selfClosing;
    ctx.cursor = 1;
    ctx.dstCounter = 1;
    PruneChildren(doc.root.children, ctx, root.children);
    out.root = std::move(root);
    if (filteredDst) *filteredDst = ctx.filteredDstIndex;
    return out;
}

/* 在裁剪副本里按前序序号找到那个元素, 用于替换开标签。 */
Node* FindByIndex(Node& n, int target, int& cursor) {
    if (n.kind != NodeKind::Element) return nullptr;
    if (cursor == target) return &n;
    ++cursor;
    for (auto& child : n.children) {
        if (Node* hit = FindByIndex(child, target, cursor)) return hit;
    }
    return nullptr;
}

void Reject(Plan& plan, const char* why) {
    plan.usable = false;
    plan.reject = why;
    plan.steps.clear();
}

} // namespace

std::string StepXml(const Step& step) {
    return svgdom::Serialize(step.doc);
}

std::string StepXmlWithOpenTag(const Step& step,
                               const std::string& replacementOpenTag) {
    if (!step.filtered || step.filteredIndex < 0) return StepXml(step);
    svgdom::Document copy = step.doc;
    int cursor = 0;
    if (Node* target = FindByIndex(copy.root, step.filteredIndex, cursor))
        target->openTag = replacementOpenTag;
    return svgdom::Serialize(copy);
}

Plan BuildPlan(const std::string& svg) {
    Plan plan;

    svgdom::Document doc = svgdom::Parse(svg);
    if (!doc.valid) {
        Reject(plan, "");
        return plan;
    }

    std::vector<Indexed> flat;
    IndexTree(doc.root, -1, flat);
    if (flat.empty()) {
        Reject(plan, "");
        return plan;
    }

    /* 已定义的 filter id 集合 —— 引用不存在的 id 时 D2D 会忽略, 我们也当普通元素。 */
    std::vector<std::string> filterIds;
    bool hasUseOrSwitch = false;
    for (const auto& item : flat) {
        const std::string local = LocalName(item.node->name);
        if (local == "filter") {
            std::string id = AttrOf(item.node->openTag, "id");
            if (!id.empty()) filterIds.push_back(std::move(id));
        } else if (local == "use" || local == "switch") {
            hasUseOrSwitch = true;
        }
    }
    auto knownFilter = [&filterIds](const std::string& id) {
        for (const auto& f : filterIds) {
            if (f == id) return true;
        }
        return false;
    };

    /* 分段的"渲染项" = 根 <svg> 的后代里, 不在资源子树内、且父节点是容器的元素。
     * 带 filter 的元素单独成项; 其余按文档顺序合并成普通段。 */
    std::vector<char> isInsideResource(flat.size(), 0);
    for (size_t i = 0; i < flat.size(); ++i) {
        if (!flat[i].resource) continue;
        for (int j = flat[i].index; j < flat[i].end; ++j)
            isInsideResource[static_cast<size_t>(j)] = 1;
    }

    /* 先找出所有带 filter 的渲染元素。 */
    std::vector<int> filtered;
    for (size_t i = 1; i < flat.size(); ++i) {
        if (isInsideResource[i]) continue;
        const std::string& tag = flat[i].node->openTag;
        const std::string style = LowerAscii(AttrOf(tag, "style"));
        if (style.find("filter:") != kNpos) {
            Reject(plan, "filter via style attribute");
            return plan;
        }
        const std::string id = UrlRefId(AttrOf(tag, "filter"));
        if (id.empty() || !knownFilter(id)) continue;
        filtered.push_back(flat[i].index);
    }

    if (!filtered.empty() && hasUseOrSwitch) {
        Reject(plan, "<use>/<switch> alongside filters");
        return plan;
    }

    /* gate: 带 filter 的元素的祖先链检查。 */
    for (int idx : filtered) {
        for (int p = flat[static_cast<size_t>(idx)].parent; p > 0;
             p = flat[static_cast<size_t>(p)].parent) {
            const Indexed& anc = flat[static_cast<size_t>(p)];
            const std::string& tag = anc.node->openTag;
            if (HasAttr(tag, "mask")) {
                Reject(plan, "ancestor mask");
                return plan;
            }
            if (!UrlRefId(AttrOf(tag, "filter")).empty()) {
                Reject(plan, "nested filter");
                return plan;
            }
            if (HasAttr(tag, "opacity")) {
                /* 组透明度作用于合成后的整组。只有这个组会被切开时才是问题 ——
                 * 组里除了这个带 filter 的元素之外还有别的渲染内容, 就会被切。 */
                bool groupSplit = false;
                for (int j = anc.index + 1; j < anc.end; ++j) {
                    if (isInsideResource[static_cast<size_t>(j)]) continue;
                    const Indexed& sub = flat[static_cast<size_t>(j)];
                    if (IsContainerElement(sub.node->name)) continue;
                    const bool insideFiltered =
                        j >= flat[static_cast<size_t>(idx)].index &&
                        j < flat[static_cast<size_t>(idx)].end;
                    if (!insideFiltered) { groupSplit = true; break; }
                }
                if (groupSplit) {
                    Reject(plan, "ancestor opacity spanning a split");
                    return plan;
                }
            }
        }
    }

    /* 顶层渲染项序列: 深度优先, 但带 filter 的元素整棵子树算一项且不再深入。 */
    struct Item { int index; bool filtered; };
    std::vector<Item> items;
    {
        auto isFiltered = [&filtered](int idx) {
            for (int f : filtered) {
                if (f == idx) return true;
            }
            return false;
        };
        for (int i = 1; i < static_cast<int>(flat.size());) {
            if (isInsideResource[static_cast<size_t>(i)]) {
                i = flat[static_cast<size_t>(i)].end;
                continue;
            }
            if (isFiltered(i)) {
                items.push_back(Item{i, true});
                i = flat[static_cast<size_t>(i)].end;
                continue;
            }
            if (IsContainerElement(flat[static_cast<size_t>(i)].node->name)) {
                /* 容器本身不画, 拆开让子元素各自成项 —— 但前提是它确实含可画的
                 * 后代。空 <g> / 只嵌套空 <g> 的容器拆开后一个项都产生不了, 会被
                 * 裁剪当成"无人选中"整棵丢掉 (mermaid 的 <g class="clusters"></g>
                 * 和 blockly 的 <g><g/></g> 就是这样凭空消失的)。这类惰性容器
                 * 整体当作一个普通项保留, 保证无 filter 文档逐字节等于原文。 */
                const int end = flat[static_cast<size_t>(i)].end;
                bool hasRenderable = false;
                for (int j = i + 1; j < end; ++j) {
                    if (isInsideResource[static_cast<size_t>(j)]) continue;
                    if (IsContainerElement(flat[static_cast<size_t>(j)].node->name))
                        continue;
                    hasRenderable = true;
                    break;
                }
                if (!hasRenderable) {
                    items.push_back(Item{i, false});
                    i = end;
                    continue;
                }
                ++i;                      // 进入容器, 其子元素各自成项
                continue;
            }
            items.push_back(Item{i, false});
            i = flat[static_cast<size_t>(i)].end;
        }
    }

    /* 合并成 step: 连续的普通项并成一段, 带 filter 的各自一段。 */
    struct StepSpec { std::vector<int> picks; bool filtered; };
    std::vector<StepSpec> specs;
    for (const auto& item : items) {
        if (item.filtered) {
            specs.push_back(StepSpec{{item.index}, true});
            continue;
        }
        if (specs.empty() || specs.back().filtered)
            specs.push_back(StepSpec{{}, false});
        specs.back().picks.push_back(item.index);
    }
    if (specs.empty())
        specs.push_back(StepSpec{{}, false});   // 空文档 -> 一段空内容

    if (specs.size() > kMaxSteps) {
        Reject(plan, "too many steps");
        return plan;
    }

    size_t totalBytes = 0;
    for (const auto& spec : specs) {
        std::vector<char> selected(flat.size(), 0);
        for (int idx : spec.picks) selected[static_cast<size_t>(idx)] = 1;

        Step step;
        step.filtered = spec.filtered;
        int filteredDst = -1;
        step.doc = PruneDocument(doc, flat, selected,
                                 spec.filtered ? spec.picks.front() : -1,
                                 &filteredDst);
        if (spec.filtered) {
            const Indexed& info = flat[static_cast<size_t>(spec.picks.front())];
            step.openTag = info.node->openTag;
            step.filterId = UrlRefId(AttrOf(step.openTag, "filter"));
            step.filteredIndex = filteredDst;

            /* 元素自己的 filter 属性在分段文档里必须去掉 —— 滤镜由调用方用离屏
             * effect 施加, 留着只会让未来支持 filter 的 D2D 重复施加一次。 */
            int cursor = 0;
            if (Node* target = FindByIndex(step.doc.root, filteredDst, cursor)) {
                const std::string ref = AttrOf(target->openTag, "filter");
                const size_t at = target->openTag.find("filter=");
                if (!ref.empty() && at != kNpos) {
                    size_t end = target->openTag.find(ref, at);
                    end = end == kNpos ? kNpos : end + ref.size() + 1;  // 含收尾引号
                    size_t begin = at;
                    while (begin > 0 && std::isspace(static_cast<unsigned char>(
                               target->openTag[begin - 1]))) {
                        --begin;
                    }
                    if (end != kNpos) target->openTag.erase(begin, end - begin);
                }
                step.openTag = target->openTag;
            }
        }

        totalBytes += svgdom::Serialize(step.doc).size();
        if (totalBytes > kMaxTotalBytes) {
            Reject(plan, "generated documents too large");
            return plan;
        }
        plan.steps.push_back(std::move(step));
    }

    plan.usable = true;
    return plan;
}

} // namespace svgseg
} // namespace ui
