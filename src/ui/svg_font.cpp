#include "svg_font.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ui {
namespace svgfont {
namespace {

// 上限: 防构造出来的文件把内存撑爆。正常导出远低于此。
constexpr size_t kMaxFonts  = 64;
constexpr size_t kMaxGlyphs = 65536;

bool IsWs(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

bool IsNameChar(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_';
}

std::string TrimAscii(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && IsWs((unsigned char)s[a])) ++a;
    while (b > a && IsWs((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string LowerAscii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

void AppendUtf8(std::string& out, unsigned long cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp <= 0x10FFFF) {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

std::string DecodeEntities(const std::string& s) {
    if (s.find('&') == std::string::npos) return s;
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] != '&') { out += s[i++]; continue; }
        size_t semi = s.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 12) { out += s[i++]; continue; }
        std::string ent = s.substr(i + 1, semi - i - 1);
        if (!ent.empty() && ent[0] == '#') {
            const bool hex = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X');
            unsigned long cp = strtoul(ent.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10);
            if (cp == 0) { out += s[i++]; continue; }
            AppendUtf8(out, cp);
        } else {
            std::string low = LowerAscii(ent);
            if      (low == "amp")  out += '&';
            else if (low == "lt")   out += '<';
            else if (low == "gt")   out += '>';
            else if (low == "quot") out += '"';
            else if (low == "apos") out += '\'';
            else { out += s[i++]; continue; }
        }
        i = semi + 1;
    }
    return out;
}

/* 解析 UTF-8 串的**唯一**码点。串为空、编码非法, 或不止一个码点 (连字字形,
 * 本模块不支持) 时返回 false。 */
bool SingleCodepoint(const std::string& s, char32_t* out) {
    if (s.empty()) return false;
    const unsigned char* p = (const unsigned char*)s.data();
    const size_t n = s.size();
    unsigned long cp = 0;
    size_t len = 0;
    if (p[0] < 0x80)                        { cp = p[0];        len = 1; }
    else if ((p[0] & 0xE0) == 0xC0)         { cp = p[0] & 0x1F; len = 2; }
    else if ((p[0] & 0xF0) == 0xE0)         { cp = p[0] & 0x0F; len = 3; }
    else if ((p[0] & 0xF8) == 0xF0)         { cp = p[0] & 0x07; len = 4; }
    else return false;
    if (len != n) return false;             // 多于一个码点 (或尾部有杂字节)
    for (size_t i = 1; i < len; ++i) {
        if ((p[i] & 0xC0) != 0x80) return false;
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *out = (char32_t)cp;
    return true;
}

// 从 '<' 起找到本标签的 '>', 跳过引号内的内容。
size_t TagEnd(const std::string& s, size_t start) {
    char quote = 0;
    for (size_t i = start; i < s.size(); ++i) {
        const char c = s[i];
        if (quote) { if (c == quote) quote = 0; continue; }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (c == '>') return i;
    }
    return std::string::npos;
}

std::string AttrOf(const std::string& tag, const char* name) {
    const size_t n = strlen(name);
    size_t i = 1;                                   // 跳过 '<' 和元素名
    while (i < tag.size()) {
        const size_t p = tag.find(name, i);
        if (p == std::string::npos) return {};
        i = p + n;
        if (p == 0 || !IsWs((unsigned char)tag[p - 1])) continue;
        size_t j = p + n;
        while (j < tag.size() && IsWs((unsigned char)tag[j])) ++j;
        if (j >= tag.size() || tag[j] != '=') continue;   // unicode-range 之类
        ++j;
        while (j < tag.size() && IsWs((unsigned char)tag[j])) ++j;
        if (j >= tag.size()) return {};
        const char q = tag[j];
        if (q == '"' || q == '\'') {
            const size_t e = tag.find(q, j + 1);
            if (e == std::string::npos) return {};
            return DecodeEntities(tag.substr(j + 1, e - j - 1));
        }
        size_t e = j;
        while (e < tag.size() && !IsWs((unsigned char)tag[e]) && tag[e] != '>' && tag[e] != '/') ++e;
        return DecodeEntities(tag.substr(j, e - j));
    }
    return {};
}

// CSS 声明块里取一条属性值。prop 必须是完整 token (前后不能接名字字符)。
std::string CssDecl(const std::string& block, const char* prop) {
    const size_t n = strlen(prop);
    size_t i = 0;
    while (i < block.size()) {
        const size_t p = block.find(prop, i);
        if (p == std::string::npos) return {};
        i = p + n;
        if (p > 0 && IsNameChar((unsigned char)block[p - 1])) continue;
        size_t j = p + n;
        while (j < block.size() && IsWs((unsigned char)block[j])) ++j;
        if (j >= block.size() || block[j] != ':') continue;
        ++j;
        const size_t e = block.find(';', j);
        return TrimAscii(block.substr(j, (e == std::string::npos ? block.size() : e) - j));
    }
    return {};
}

std::string StripQuotes(std::string v) {
    v = TrimAscii(v);
    if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front())
        v = v.substr(1, v.size() - 2);
    return TrimAscii(v);
}

std::string FamilyKey(const std::string& family) {
    return LowerAscii(StripQuotes(family));
}

float ParseFloatAttr(const std::string& v, float fallback) {
    if (v.empty()) return fallback;
    char* end = nullptr;
    const double d = strtod(v.c_str(), &end);
    if (end == v.c_str()) return fallback;
    return (float)d;
}

int ParseWeightAttr(const std::string& v, int fallback) {
    if (v.empty()) return fallback;
    const std::string low = LowerAscii(TrimAscii(v));
    if (low == "normal") return 400;
    if (low == "bold")   return 700;
    const int n = atoi(low.c_str());
    return n > 0 ? n : fallback;
}

/* CSS 里 `@font-face { font-family:X; src:url("#FontID0") format(svg) }` 建立的
 * "font id → CSS family" 映射。只认内部引用 (#id) —— data: URL 的 WOFF2 由
 * renderer.cpp 的 DirectWrite 通道处理, 不归本模块管。 */
std::map<std::string, std::vector<std::string>> ParseAtFontFaceRefs(const std::string& svg) {
    std::map<std::string, std::vector<std::string>> byId;
    size_t pos = 0;
    while ((pos = svg.find("@font-face", pos)) != std::string::npos) {
        const size_t lb = svg.find('{', pos);
        const size_t rb = (lb == std::string::npos) ? std::string::npos : svg.find('}', lb + 1);
        if (lb == std::string::npos || rb == std::string::npos) break;
        const std::string block = svg.substr(lb + 1, rb - lb - 1);
        pos = rb + 1;

        const std::string family = StripQuotes(CssDecl(block, "font-family"));
        const std::string src    = CssDecl(block, "src");
        if (family.empty() || src.empty()) continue;

        const size_t u = LowerAscii(src).find("url(");
        if (u == std::string::npos) continue;
        const size_t e = src.find(')', u + 4);
        if (e == std::string::npos) continue;
        std::string url = StripQuotes(src.substr(u + 4, e - u - 4));
        if (url.size() < 2 || url[0] != '#') continue;
        byId[LowerAscii(url.substr(1))].push_back(family);
    }
    return byId;
}

void AppendNum(std::string& out, double v) {
    char buf[48];
    int n = snprintf(buf, sizeof(buf), "%.3f", v);
    if (n <= 0) { out += '0'; return; }
    int e = n;
    bool dot = false;
    for (int i = 0; i < n; ++i) if (buf[i] == '.') { dot = true; break; }
    if (dot) {
        while (e > 0 && buf[e - 1] == '0') --e;
        if (e > 0 && buf[e - 1] == '.') --e;
    }
    if (e <= 0 || (e == 1 && buf[0] == '-')) { out += '0'; return; }
    if (e == 2 && buf[0] == '-' && buf[1] == '0') { out += '0'; return; }   // "-0" → "0"
    out.append(buf, (size_t)e);
}

void SkipSep(const std::string& s, size_t& i) {
    while (i < s.size() && (IsWs((unsigned char)s[i]) || s[i] == ',')) ++i;
}

bool ReadNum(const std::string& s, size_t& i, double* out) {
    SkipSep(s, i);
    const size_t start = i;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
    bool digits = false;
    while (i < s.size() && isdigit((unsigned char)s[i])) { ++i; digits = true; }
    if (i < s.size() && s[i] == '.') {
        ++i;
        while (i < s.size() && isdigit((unsigned char)s[i])) { ++i; digits = true; }
    }
    if (!digits) return false;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        const size_t save = i;
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        bool expDigits = false;
        while (i < s.size() && isdigit((unsigned char)s[i])) { ++i; expDigits = true; }
        if (!expDigits) i = save;
    }
    *out = strtod(s.c_str() + start, nullptr);
    return true;
}

} // namespace

bool TransformPathData(const std::string& d, float sx, float sy, float tx, float ty,
                       std::string* out) {
    if (!out) return false;
    out->clear();
    std::string res;
    res.reserve(d.size() + d.size() / 4);

    double curX = 0, curY = 0, subX = 0, subY = 0;
    char cmd = 0;
    size_t i = 0;

    auto put = [&](double x, double y) {
        res += ' ';
        AppendNum(res, (double)sx * x + tx);
        res += ' ';
        AppendNum(res, (double)sy * y + ty);
    };

    while (true) {
        SkipSep(d, i);
        if (i >= d.size()) break;
        const char c = d[i];
        if (isalpha((unsigned char)c)) {
            cmd = c;
            ++i;
        } else if (cmd == 0) {
            return false;                       // 数据以数字开头, 没有指令
        } else if (cmd == 'M') {
            cmd = 'L';                          // 隐式重复: M 之后是 L
        } else if (cmd == 'm') {
            cmd = 'l';
        }

        double a[6];
        switch (cmd) {
        case 'Z': case 'z':
            res += 'Z';
            curX = subX; curY = subY;
            break;

        case 'M': case 'L':
            if (!ReadNum(d, i, &a[0]) || !ReadNum(d, i, &a[1])) return false;
            curX = a[0]; curY = a[1];
            if (cmd == 'M') { subX = curX; subY = curY; res += 'M'; } else res += 'L';
            put(curX, curY);
            break;

        case 'm': case 'l':
            if (!ReadNum(d, i, &a[0]) || !ReadNum(d, i, &a[1])) return false;
            curX += a[0]; curY += a[1];
            if (cmd == 'm') { subX = curX; subY = curY; res += 'M'; } else res += 'L';
            put(curX, curY);
            break;

        // H/V 一律转成 L: 对角变换下语义等价, 少两条分支。
        case 'H':
            if (!ReadNum(d, i, &a[0])) return false;
            curX = a[0];
            res += 'L'; put(curX, curY);
            break;
        case 'h':
            if (!ReadNum(d, i, &a[0])) return false;
            curX += a[0];
            res += 'L'; put(curX, curY);
            break;
        case 'V':
            if (!ReadNum(d, i, &a[0])) return false;
            curY = a[0];
            res += 'L'; put(curX, curY);
            break;
        case 'v':
            if (!ReadNum(d, i, &a[0])) return false;
            curY += a[0];
            res += 'L'; put(curX, curY);
            break;

        case 'C': case 'c': {
            for (int k = 0; k < 6; ++k) if (!ReadNum(d, i, &a[k])) return false;
            const double bx = (cmd == 'c') ? curX : 0.0, by = (cmd == 'c') ? curY : 0.0;
            res += 'C';
            put(bx + a[0], by + a[1]);
            put(bx + a[2], by + a[3]);
            curX = bx + a[4]; curY = by + a[5];
            put(curX, curY);
            break;
        }

        case 'S': case 's': case 'Q': case 'q': {
            for (int k = 0; k < 4; ++k) if (!ReadNum(d, i, &a[k])) return false;
            const bool rel = (cmd == 's' || cmd == 'q');
            const double bx = rel ? curX : 0.0, by = rel ? curY : 0.0;
            /* S/T 的"反射控制点"由消费方按前一段算出: 反射是 2*cur - prevCtrl,
             * 仿射变换满足 T(2c-p) = 2T(c)-T(p), 所以指令原样保留即可。 */
            res += (cmd == 'S' || cmd == 's') ? 'S' : 'Q';
            put(bx + a[0], by + a[1]);
            curX = bx + a[2]; curY = by + a[3];
            put(curX, curY);
            break;
        }

        case 'T': case 't': {
            if (!ReadNum(d, i, &a[0]) || !ReadNum(d, i, &a[1])) return false;
            const double bx = (cmd == 't') ? curX : 0.0, by = (cmd == 't') ? curY : 0.0;
            res += 'T';
            curX = bx + a[0]; curY = by + a[1];
            put(curX, curY);
            break;
        }

        // 弧线: 镜像会翻 sweep flag, 字形轮廓又从不用它 —— 直接回落, 不冒险。
        case 'A': case 'a':
        default:
            return false;
        }
    }

    if (res.empty()) return false;
    *out = std::move(res);
    return true;
}

FontSet Parse(const std::string& svg) {
    FontSet set;
    if (svg.find("<font") == std::string::npos) return set;

    const auto idToFamilies = ParseAtFontFaceRefs(svg);
    size_t glyphBudget = kMaxGlyphs;
    size_t pos = 0;

    while ((pos = svg.find("<font", pos)) != std::string::npos) {
        const size_t after = pos + 5;
        // <font-face> / <font-face-src> 等: 名字还没结束, 不是 <font> 本身
        if (after < svg.size() && IsNameChar((unsigned char)svg[after])) { pos = after; continue; }

        const size_t te = TagEnd(svg, pos);
        if (te == std::string::npos) break;
        const std::string openTag = svg.substr(pos, te - pos + 1);
        if (te > pos && svg[te - 1] == '/') { pos = te + 1; continue; }   // <font/> 没有字形

        const size_t close = svg.find("</font>", te + 1);
        if (close == std::string::npos) break;
        const std::string body = svg.substr(te + 1, close - te - 1);
        pos = close + 7;

        if (set.byFamily.size() >= kMaxFonts) break;

        Font font;
        font.defaultAdvance = ParseFloatAttr(AttrOf(openTag, "horiz-adv-x"), 0.0f);
        font.weight = ParseWeightAttr(AttrOf(openTag, "font-weight"), 400);
        font.italic = LowerAscii(AttrOf(openTag, "font-style")).find("italic") != std::string::npos;

        std::string faceFamily;
        const size_t ff = body.find("<font-face");
        if (ff != std::string::npos) {
            const size_t ffEnd = TagEnd(body, ff);
            if (ffEnd != std::string::npos) {
                const std::string faceTag = body.substr(ff, ffEnd - ff + 1);
                faceFamily = StripQuotes(AttrOf(faceTag, "font-family"));
                font.unitsPerEm = ParseFloatAttr(AttrOf(faceTag, "units-per-em"), 1000.0f);
                if (font.unitsPerEm <= 0.0f) font.unitsPerEm = 1000.0f;
                font.ascent  = ParseFloatAttr(AttrOf(faceTag, "ascent"),  font.unitsPerEm * 0.8f);
                font.descent = ParseFloatAttr(AttrOf(faceTag, "descent"), font.unitsPerEm * 0.2f);
                if (font.descent < 0.0f) font.descent = -font.descent;   // 规范允许写成负值
                font.weight = ParseWeightAttr(AttrOf(faceTag, "font-weight"), font.weight);
                if (LowerAscii(AttrOf(faceTag, "font-style")).find("italic") != std::string::npos)
                    font.italic = true;
            }
        }

        size_t gp = 0;
        while ((gp = body.find("<glyph", gp)) != std::string::npos) {
            const size_t gAfter = gp + 6;
            if (gAfter < body.size() && IsNameChar((unsigned char)body[gAfter])) { gp = gAfter; continue; }
            const size_t ge = TagEnd(body, gp);
            if (ge == std::string::npos) break;
            const std::string gTag = body.substr(gp, ge - gp + 1);
            gp = ge + 1;
            if (glyphBudget == 0) break;

            char32_t cp = 0;
            if (!SingleCodepoint(AttrOf(gTag, "unicode"), &cp)) continue;   // 连字 / 无 unicode
            Glyph g;
            g.d = TrimAscii(AttrOf(gTag, "d"));
            g.advance = ParseFloatAttr(AttrOf(gTag, "horiz-adv-x"), font.defaultAdvance);
            font.glyphs.emplace(cp, std::move(g));
            --glyphBudget;
        }

        if (font.glyphs.empty()) continue;

        /* 同一个 <font> 按两条线登记 family: <font-face font-family> 自身,
         * 以及 CSS @font-face 里 src:url(#id) 指向它的那些 family 名。
         * CorelDRAW 两者相同, 都登记是为了容忍别的导出器只写其中一边。 */
        std::vector<std::string> keys;
        if (!faceFamily.empty()) keys.push_back(FamilyKey(faceFamily));
        const std::string id = LowerAscii(AttrOf(openTag, "id"));
        if (!id.empty()) {
            auto it = idToFamilies.find(id);
            if (it != idToFamilies.end())
                for (const auto& fam : it->second) keys.push_back(FamilyKey(fam));
        }
        for (size_t k = 0; k < keys.size(); ++k) {
            if (keys[k].empty()) continue;
            bool seen = false;                       // 两条线取到同一个名字时只登记一次
            for (size_t j = 0; j < k; ++j) if (keys[j] == keys[k]) { seen = true; break; }
            if (!seen) set.byFamily[keys[k]].push_back(font);
        }
    }
    return set;
}

const Font* Find(const FontSet& set, const std::string& family, int weight, bool italic) {
    if (set.byFamily.empty() || family.empty()) return nullptr;
    const auto it = set.byFamily.find(FamilyKey(family));
    if (it == set.byFamily.end() || it->second.empty()) return nullptr;

    const std::vector<Font>& v = it->second;
    if (v.size() == 1) return &v[0];

    const Font* best = &v[0];
    int bestScore = -1;
    for (const Font& f : v) {
        int score = (f.italic == italic) ? 2 : 0;
        const int delta = f.weight > weight ? f.weight - weight : weight - f.weight;
        score += (delta == 0) ? 4 : (delta <= 100 ? 2 : 0);
        if (score > bestScore) { bestScore = score; best = &f; }
    }
    return best;
}

bool LayoutToPath(const Font& font, const std::u32string& text, float fontSize,
                  float originX, float originY, int anchor, bool block,
                  std::string* outPath, float* outAdvance) {
    if (outPath) outPath->clear();
    if (outAdvance) *outAdvance = 0.0f;
    if (text.empty() || fontSize <= 0.0f) return false;

    const float upm = font.unitsPerEm > 0.0f ? font.unitsPerEm : 1000.0f;
    const float scale = fontSize / upm;

    struct Item { const Glyph* glyph; float advance; };   // advance: 设计单位
    std::vector<Item> items;
    items.reserve(text.size());
    float total = 0.0f;

    for (char32_t cp : text) {
        if (cp == U'\r') continue;
        if (cp == U'\n') return false;                    // 多行布局交给 DirectWrite
        const Glyph* g = font.Find(cp);
        if (!g) {
            if (cp == U' ' || cp == U'\t') {
                float adv = font.defaultAdvance > 0.0f ? font.defaultAdvance : upm * 0.33f;
                if (cp == U'\t') adv = upm;
                items.push_back({nullptr, adv});
                total += adv;
                continue;
            }
            return false;                                 // 缺字形: 回落
        }
        float adv = g->advance > 0.0f ? g->advance : font.defaultAdvance;
        if (adv <= 0.0f) adv = upm * 0.5f;
        items.push_back({g, adv});
        total += adv;
    }
    if (items.empty()) return false;

    const float totalUser = total * scale;
    if (outAdvance) *outAdvance = totalUser;

    float x0 = originX;
    float baseline = originY;
    if (block) {
        const float ascentUser = font.ascent * scale;
        const float h = (font.ascent + font.descent) * scale;
        x0 = originX - totalUser * 0.5f;
        baseline = originY - (h > 0.0f ? h : fontSize) * 0.5f + ascentUser;
    } else if (anchor == 1) {
        x0 = originX - totalUser * 0.5f;
    } else if (anchor == 2) {
        x0 = originX - totalUser;
    }

    /* 字形轮廓是 y 轴向上、原点在基线上的设计坐标, 所以 y 要翻: sy = -scale。 */
    std::string all;
    float pen = 0.0f;
    for (const Item& it : items) {
        if (it.glyph && !it.glyph->d.empty()) {
            std::string piece;
            if (!TransformPathData(it.glyph->d, scale, -scale, x0 + pen * scale, baseline, &piece))
                return false;
            all += piece;
        }
        pen += it.advance;
    }
    if (all.empty()) return false;

    if (outPath) *outPath = std::move(all);
    return true;
}

} // namespace svgfont
} // namespace ui
