#pragma once

#include <string>
#include <vector>

namespace ui {
namespace svgdom {

/* L173 Phase 5: 最小 SVG DOM。
 *
 * 存在的理由: gh_img_view 里的 SVG 预处理原本全是"字符串顺序扫描" —— 能改属性、
 * 能删元素, 但**不知道树结构**。D2D 的 filter 叠加层因此做不到两件事:
 *   ① 按文档顺序把绘制切成 z 正确的分段 (带 filter 的元素被统一画到最后, 会盖住
 *      文档顺序在它之后的内容);
 *   ② 复现祖先链 (元素被从 <g transform=...> 里摘出来单独成文档, 变换丢失)。
 * 两者都需要真正的父子关系, 所以先有这个 DOM。
 *
 * 设计取舍 —— **不建属性模型, 开标签按原样字节保留**:
 *   - 属性的读写继续复用调用方已有的 ExtractXmlAttr_ / EraseXmlAttr_ 之类工具,
 *     直接作用在 openTag 文本上;
 *   - 好处是实体转义、命名空间前缀、引号风格、属性顺序全部逐字节保真, 不会因为
 *     "解析后再序列化"引入新的转义 bug。
 *   - 代价是想按属性做结构化查询要自己扫 openTag —— 对当前用途足够。
 *
 * 因此本模块的核心不变式是 **逐字节 roundtrip**:
 *     Serialize(Parse(x)) == x   (x 为结构良好的输入)
 * 这条不变式就是单测的主断言。
 *
 * 扫描器比原有的 svg.find('>') 更严格: 尊重属性值里的引号, 正确跳过注释 /
 * CDATA / 处理指令 / DOCTYPE (含内部子集 []), 所以属性值里出现 '>' 不会再把标签
 * 截断。任何结构错误 (标签交叉嵌套、缺闭标签、多余闭标签、嵌套过深) 都让
 * Parse 返回 valid=false —— 调用方据此回落 LunaSVG, 绝不猜。
 *
 * 无依赖 (只用标准库), 可被测试目标直接编译, 不必链整个 core-ui。
 */

enum class NodeKind {
    Element,   // <g ...> ... </g> 或 <path .../>
    Text,      // 标签之间的原样文本 (含空白)
    Special    // 注释 / CDATA / 处理指令 / DOCTYPE, 一律原样透传
};

struct Node {
    NodeKind kind = NodeKind::Element;

    // Element: 小写标签名, 保留命名空间前缀 (如 "svg:g")。用 LocalName() 取本地名。
    std::string name;
    // Element: 原样开标签, 含尖括号。例 "<g transform=\"translate(1,2)\">"
    std::string openTag;
    // Element: 原样闭标签, 例 "</g>"。自闭合元素为空。
    std::string closeTag;
    // Text / Special: 原样字节。
    std::string text;

    bool selfClosing = false;
    std::vector<Node> children;
};

struct Document {
    bool valid = false;
    std::string prologue;   // 根 <svg> 之前的原样字节 (XML 声明 / 注释 / DOCTYPE)
    std::string epilogue;   // 根 </svg> 之后的原样字节
    Node root;              // 根 <svg> 元素; valid=false 时无意义
};

/* 解析 UTF-8 SVG 文本。结构错误或找不到根 <svg> 时返回 valid=false。 */
Document Parse(const std::string& svg);

/* 逐字节还原。Serialize(Parse(x)) == x (x 结构良好)。 */
std::string Serialize(const Document& doc);
std::string SerializeNode(const Node& node);
void SerializeNodeTo(const Node& node, std::string& out);

/* 去掉命名空间前缀的本地名 ("svg:g" -> "g")。输入应已小写。 */
std::string LocalName(const std::string& name);

/* 嵌套深度上限。超过即判 invalid —— 正常 SVG 远达不到, 触顶说明输入异常或恶意。 */
constexpr int kMaxDepth = 256;

} // namespace svgdom
} // namespace ui
