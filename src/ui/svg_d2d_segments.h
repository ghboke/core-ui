#pragma once

#include <string>
#include <vector>

#include "svg_dom.h"

namespace ui {
namespace svgseg {

/* L173 Phase 5: 把一份 SVG 拆成 z 正确的 D2D 绘制分段。
 *
 * 背景: D2D 的 ID2D1SvgDocument 完全不支持 <filter>, 而且一份文档只能整张
 * DrawSvgDocument —— 没有元素级合成的钩子。要在 D2D 上正确画带 filter 的 SVG,
 * 只能把文档按绘制顺序切成若干子文档, 逐段 DrawSvgDocument, 轮到带 filter 的
 * 元素时改走离屏 + effect 合成。
 *
 * 关键实现选择 —— **分段靠"裁剪"而不是"摘取"**:
 * 每一段都是**整份文档的一份裁剪副本**, 只保留本段要画的元素, 其余渲染元素删掉,
 * 但**完整保留祖先链和全部资源定义**(defs / 渐变 / clipPath / style ...)。
 *   - 祖先链保留 => <g transform=...> 之类的变换、继承 paint 自动生效, 不会再出现
 *     元素被摘出来后画到原点的问题;
 *   - 资源留在原位 => 不做 hoist, 也就不会破坏 userSpaceOnUse 这类依赖位置的语义。
 * 代价是段数 × 文档体积的序列化开销, 由下面的 gate 封顶。
 *
 * gate (红线) —— 命中即 usable=false, 调用方回落 LunaSVG, 绝不硬切:
 *   - DOM 结构不良 / 找不到根 <svg>;
 *   - 带 filter 的元素的祖先上有 opacity: 组透明度作用于"合成后的整组", 把组切开
 *     会变成逐段各乘一次, 重叠处颜色就错了;
 *   - 祖先上有 mask 或 filter (嵌套滤镜), D2D 本就画不对;
 *   - 文档里同时存在 <use> 或 <switch> 与 filter: 引用型内容跨段无法可靠追踪;
 *   - filter 走 style="filter:..." 而非属性: 本模块只认属性形式;
 *   - 段数或生成字节数超上限: 防大文件被放大成几十份副本。
 *
 * 只依赖 svg_dom + 标准库, 可被测试目标直接编译。
 */

struct Step {
    // true = 这一步只画一个带 filter 的元素, 需要离屏 + effect 合成。
    bool filtered = false;
    // filtered 时: filter="url(#id)" 里的 id。
    std::string filterId;
    // filtered 时: 该元素的原样开标签 (调用方据此判断 fill/stroke 并生成阴影版)。
    std::string openTag;

    // 本段的裁剪副本。用 StepXml() 序列化。
    svgdom::Document doc;
    // filtered 时: 被保留的那个元素在 doc 里的前序序号, 供 StepXmlWithOpenTag 定位。
    int filteredIndex = -1;
};

struct Plan {
    bool usable = false;
    // usable=false 时说明命中了哪条红线 (诊断 / 日志用; 空串表示 DOM 无效)。
    std::string reject;
    std::vector<Step> steps;
};

/* 上限。超过即判 usable=false —— 与其把一份 4MB 的图放大成几十份副本喂给
 * D2D 解析, 不如直接交给 LunaSVG 光栅。 */
constexpr size_t kMaxSteps = 32;
constexpr size_t kMaxTotalBytes = 8u * 1024u * 1024u;

/* 构建绘制计划。svg 应当是已经过各 D2D 预处理 pass 的文本。 */
Plan BuildPlan(const std::string& svg);

/* 第 i 步的子文档 XML。带 filter 的元素的 filter 属性已被去掉。 */
std::string StepXml(const Step& step);

/* 同上, 但把那个带 filter 的元素的开标签换成 replacementOpenTag —— 调用方用它
 * 生成"阴影版"(换掉 fill/stroke 为阴影色)。非 filtered 步等同 StepXml。 */
std::string StepXmlWithOpenTag(const Step& step, const std::string& replacementOpenTag);

} // namespace svgseg
} // namespace ui
