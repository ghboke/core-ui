#pragma once

#include <string>

namespace ui {

/* 一站式: 读 UTF-8 SVG 文件 + InlineSvgStyleClasses 预处理.
 * 失败 (文件不存在 / 太大 / 读失败) 返空 string.
 * 用 Win32 CreateFileW 避开 std::ifstream 宽字符不兼容. */
std::string LoadSvgWithInlinedStyles(const std::wstring& path);


/* L48 — SVG <style>.class{...} → inline style="..." 预处理.
 *
 * D2D ID2D1SvgDocument (Win10 1607+ SVG 渲染) 是 SVG 1.1 严格子集,
 * **不识别** <style> 元素内的 CSS 规则 (不论 class / id / tag 选择器).
 * 只认元素自身的 style="..." 属性 + presentation attribute (fill=/stroke=).
 *
 * Adobe Illustrator / Figma / Sketch / Inkscape 默认导出格式都是:
 *     <style type="text/css"> .st0 { fill: #xxx; } ... </style>
 *     <path class="st0" d="..."/>
 * 这种 SVG 进 D2D 后所有 fill 解析失败, 默认 fill=black, 渲染全黑.
 *
 * 本预处理 pass 把 <style> 块里 D2D 不会可靠应用的 CSS 规则展开到
 * 匹配元素的 inline style="props" 属性里. 改写后的 SVG 文本喂给 D2D
 * 即可正确出色.
 *
 * 行为约定:
 *   - 输入不含 "<style" 子串 → 零开销原样返回 (覆盖所有 inline-fill 图标 SVG).
 *   - 支持 tag / .class / #id / 复合选择器 / 后代选择器 / 子选择器 / 属性选择器,
 *     以及 selector list 的 specificity + source order.
 *   - 支持 !important. 输出到 inline style 时剥掉 "!important" 文本, 保留其
 *     优先级效果；stylesheet important 可覆盖普通 inline style.
 *   - @media / @import / hover 等动态上下文不展开, 遇到时 skip 不破坏文本.
 *   - 多个 <style> 块累加; stylesheet 与元素原有 style="..." 冲突时按 CSS
 *     cascade 计算最终值.
 *   - 元素 class="..." 属性保留 (D2D 反正忽略), 不删省事.
 *
 * 复杂度: O(N + R*M), N = SVG 文本字节数, R = CSS rule 数, M = 元素数.
 * 单 pass SVG scanner, CSS 解析/匹配复用 core-ui 内建模块.
 */
std::string InlineSvgStyleClasses(const std::string& svg_xml);


/* L86 — SVG <use href> (无 xlink) + <symbol> 兼容 D2D 的预处理 pass.
 *
 * D2D ID2D1SvgDocument 是 SVG 1.1 子集, 有两个会让"引用型"内容整组消失的坑:
 *   ① 只解析 SVG 1.1 流儀的 `xlink:href`, **不认** SVG 2 的裸 `href` —— 用
 *      `<use href="#id">` 的 SVG, use 整组静默不渲染 (实测白屏).
 *   ② **完全不支持 `<symbol>`** —— 即便 use 用 xlink:href 正确指过去, symbol
 *      里的内容也一概不画 (实测: defs>g 能被 use 引用渲染, symbol 不能).
 * 雪碧图 / 图标集常见的 `<symbol>` + `<use href>` 写法因此双重踩坑.
 *
 * 本 pass 把这类写法改写成 D2D 认识的等价形式:
 *   ① 元素若有 `href` 但无 `xlink:href` → 镜像补一个 `xlink:href="<同值>"`
 *      (保留原 href, D2D 忽略未知属性; fallback ParseSvgIcon 不读 href 不受影响).
 *      root <svg> 若缺 `xmlns:xlink` 声明 → 补上 (xlink: 前缀需命名空间已声明).
 *   ② `<symbol …>`/`</symbol>` 改写成 `<g …>`/`</g>`; 若 symbol 在顶层
 *      (不在任何 <defs> 内) → 外套一层 <defs> 防止单独绘制 (defs>g 可被 use
 *      引用但不直接渲染); 已在 defs 内则只换名, 不再嵌套 defs.
 *
 * 行为约定:
 *   - 输入既无 "href" 也无 "<symbol" 子串 → 零开销原样返回.
 *   - 注释 / CDATA / PI / DOCTYPE 原样保留.
 *   - 限界: symbol 自带 viewBox 且靠 <use> 的 width/height 做视口缩放的场景,
 *     本简单改写不复刻视口变换 (共享 g 没法塞 per-use 缩放). 无 viewBox 的
 *     symbol (绝大多数雪碧图) 靠 use 的 x/y 平移即可几何一致, 完全修复.
 *
 * 复杂度 O(N), 单 pass scanner, 复用 InlineSvgStyleClasses 的标签扫描器, 不引依赖.
 */
std::string NormalizeSvgRefsAndSymbols(const std::string& svg_xml);

/* 将 D2D 不稳定的 `rgb()` / `rgba()` 实色 paint 规范化为 SVG 1.1 的
 * #RRGGBB + {fill,stroke}-opacity。只处理 fill/stroke 的纯色函数值，保留
 * none、currentColor、url(#gradient) 等非纯色语义。 */
std::string NormalizeSvgPaintColorsForD2D(const std::string& svg_xml);

} // namespace ui
