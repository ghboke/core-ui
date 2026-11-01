#pragma once
//
// svg_symbols.h — SVG sprite: <symbol> 定义一次, <use href> 按代号引用 (build 304)。
//
// 对齐 HTML 的两种写法:
//   页内:   <svg><symbol id="a" viewBox="0 0 24 24">...</symbol></svg>
//           <svg><use href="#a"/></svg>
//   外部:   <svg><use href="icons.svg#a"/></svg>   (icons.svg 走资源解析器 ui_asset_*)
//
// 与浏览器的差异: 页内 symbol 的 id 是进程级的 (编译到 <symbol> 就登记, 同名后者
// 覆盖), 不按文档隔离 —— 多个页面共用的图标库推荐写成外部 sprite 文件。
// <use> 实例只带 symbol 自身的呈现属性 (含 <g> 逐层继承), 不参与页面 CSS 选择器,
// 颜色用 currentColor 跟随 <svg> 的 color。只在 UI 线程调用。
//
#include "svg_widget.h"
#include "../uix/template_ast.h"

#include <string>
#include <vector>

namespace ui::page {

struct SvgSymbol {
    float vpWidth  = 0.0f;   // viewBox 宽高 (min-x/min-y 视为 0, 同 <svg> 的处理)
    float vpHeight = 0.0f;
    std::vector<SvgShape> shapes;
};

// 把一个 <symbol> 节点折成图形列表 (静态属性; symbol / <g> 上的呈现属性向下继承)。
SvgSymbol BuildSvgSymbol(const ui::uix::Node& symbolNode);

// 页内 <symbol id> 编译时登记, 供 "#id" 引用。
void RegisterSvgSymbol(const std::string& id, SvgSymbol symbol);

// "#id" → 页内登记的 symbol; "file.svg#id" → 首次用到时经资源解析器加载并解析
// 整个 sprite 文件 (结果缓存, 含"文件不存在"); 其它形式或找不到 → nullptr。
// 返回的指针在下一次 Register / Reset 之前有效。
const SvgSymbol* ResolveSvgSymbol(const std::string& href);

// 清空全部登记与缓存 (测试 / 资源整体替换后用)。
void ResetSvgSymbols();

}  // namespace ui::page
