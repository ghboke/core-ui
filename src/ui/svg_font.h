#pragma once

#include <map>
#include <string>
#include <vector>

/* SVG 1.1 "SVG Fonts" (<font> / <font-face> / <glyph>) 解析与排版。
 *
 * 背景: CorelDRAW 导出 SVG 时不内嵌 WOFF2, 而是把用到的字形轮廓直接写成
 *   <defs>
 *     <font id="FontID0" horiz-adv-x="468">
 *       <font-face font-family="MiniMeta LCD 3"/>
 *       <glyph unicode="@" horiz-adv-x="468" d="M112 691l0 -84 ..."/>
 *   并在 CSS 里用 `src:url("#FontID0") format(svg)` 引用。
 * 这套机制浏览器早已移除, resvg / librsvg / LunaSVG 也都不支持, 但文件本身是
 * 自带轮廓的 —— 只要照着画就是像素级正确, 而靠字体回退永远画不对 (段码 LCD
 * 字体的 '@' 是"熄灭的 8", 系统里根本不存在可替代字体)。
 *
 * 本模块只依赖标准库, 可被测试目标直接编译。renderer.cpp 的
 * SvgInlineTextAsPaths() 在 WOFF2 通道之后、DirectWrite 通道之前调用它:
 * 命中就返回字形轮廓, 任何不认识的构造一律返回 false 回落到原有行为。
 */

namespace ui {
namespace svgfont {

struct Glyph {
    std::string d;           // 字形轮廓 path-data (字体设计单位, y 轴向上, baseline = 0)
    float advance = 0.0f;    // horiz-adv-x, 设计单位
};

struct Font {
    float unitsPerEm    = 1000.0f;  // <font-face units-per-em>, 缺省 1000
    float ascent        = 800.0f;   // 缺省 0.8 em
    float descent       = 200.0f;   // 缺省 0.2 em
    float defaultAdvance = 0.0f;    // <font horiz-adv-x>: 字形未声明时的默认步进
    int   weight        = 400;      // <font-face font-weight>, 同名多字体时做次级筛选
    bool  italic        = false;
    std::map<char32_t, Glyph> glyphs;

    const Glyph* Find(char32_t cp) const {
        auto it = glyphs.find(cp);
        return it == glyphs.end() ? nullptr : &it->second;
    }
};

/* family 名 → 若干 <font>。key 已做 ASCII 小写归一 (CSS family 匹配大小写不敏感)。
 * 同一个 <font> 会同时按 <font-face font-family> 和 @font-face 里引用它的 CSS
 * family 注册, 两者通常相同, 都登记是为了容忍 Corel 之外的导出器。 */
struct FontSet {
    std::map<std::string, std::vector<Font>> byFamily;
    bool Empty() const { return byFamily.empty(); }
};

/* 从整份 SVG 文本解析。没有 SVG Font 时返回空集合 (调用方据此跳过)。 */
FontSet Parse(const std::string& svg);

/* 找 family 对应的字体。weight / italic 只在同名多字体时做次级筛选 ——
 * CorelDRAW 把字重编进 family 名里 ("阿里巴巴普惠体 Medium" 是独立的一个
 * <font>), 所以字重不匹配绝不能当成 miss。找不到返回 nullptr。 */
const Font* Find(const FontSet& set, const std::string& family, int weight, bool italic);

/* 把一段文本排成 SVG 用户坐标系下的 path-data。
 *   text     码点序列 (调用方负责 UTF-16 → UTF-32)
 *   originY  基线 y (block = true 时改为围绕该 y 垂直居中)
 *   anchor   0 = start, 1 = middle, 2 = end
 *   outAdvance 回填本段的自然宽度 (用户单位), 供无显式 x 的 tspan 接续
 * 任一码点没有字形 (空白除外), 或字形轮廓含本模块不支持的指令 → 返回 false,
 * 调用方回落到 DirectWrite。 */
bool LayoutToPath(const Font& font, const std::u32string& text, float fontSize,
                  float originX, float originY, int anchor, bool block,
                  std::string* outPath, float* outAdvance);

/* path-data 的对角仿射变换: x' = sx * x + tx, y' = sy * y + ty。
 * 输出一律是绝对指令 —— 相对指令逐段累加会把量化误差滚起来, 转绝对后每个点
 * 都从全精度的源坐标一次算出, 不存在漂移。
 * 遇到弧线 (A/a) 或解析不了的写法返回 false: 字形轮廓不会用弧线, 与其为了
 * "镜像要翻 sweep flag" 这类细节冒险, 不如直接回落。
 * 对外暴露供单测直接验证。 */
bool TransformPathData(const std::string& d, float sx, float sy, float tx, float ty,
                       std::string* out);

} // namespace svgfont
} // namespace ui
