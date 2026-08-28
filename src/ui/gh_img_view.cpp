// gh_img_view.cpp — 通用瓦块画布 widget 实现。
//
// 渲染思路：
//   1. ComputeDestRect 算出"图像在屏幕上占多大"
//   2. preview ResourceKey 存在则先画一张兜底
//   3. 遍历可见瓦块（active level 网格），有 ResourceKey 就画上去
//   4. 缺的瓦块就让 preview 透出来 → 渐进式 LOD 体验
//
// auto-level：zoom 跨越某 level 阈值时自动切 active level，但**不**主动清旧
// 数据 —— 由调用方决定何时清（Begin / ClearLevel / Clear）。这样切级时不会
// 黑屏 (旧 level 瓦块还在 + preview 仍兜底)。

#include "gh_img_view.h"
#include "event.h"
#include "display_list.h"
#include "svg_style_inliner.h"
#include "svg_d2d_segments.h"
#include "image_shadow_geom.h"
#include "ui_context.h"
#include "debug_trace.h"
#include "resource_store.h"
#include "theme.h"

#include <lunasvg.h>     /* SVG thumbnail/fallback data; main interactive view uses
                          * D2D ID2D1SvgDocument when it can be created. */
#include <d2d1effects.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <Windows.h>

namespace ui {

namespace {

// CW rotation of (x, y) by angle (must be 0/90/180/270) around origin.
// Screen y-axis is down, so 90° CW maps (1,0) → (0,1).
inline void RotateCW(int angle, float x, float y, float& rx, float& ry) {
    switch (angle) {
        case 90:  rx = -y; ry =  x; break;
        case 180: rx = -x; ry = -y; break;
        case 270: rx =  y; ry = -x; break;
        default:  rx =  x; ry =  y; break;
    }
}

// Inverse of RotateCW (= CCW by same angle).
inline void RotateCCW(int angle, float x, float y, float& rx, float& ry) {
    switch (angle) {
        case 90:  rx =  y; ry = -x; break;
        case 180: rx = -x; ry = -y; break;
        case 270: rx = -y; ry =  x; break;
        default:  rx =  x; ry =  y; break;
    }
}

// Pick D2D interpolation by draw scale (screen px per source px). 三档 (L190 + L191):
//   <0.5  大幅缩小  → HQ_CUBIC: D2D 对大幅下采带 prefilter, 抗欠采样。此时源边缘已被
//         prefilter 抹成渐变, cubic 负权【不会 ring】(ring 只在锐边发生)。若改用 LINEAR,
//         2×2 双线性在大幅下采漏采源像素 → 文字/细节锯齿发硬 (单层图 / 文档扫描)。
//   0.5~1.0 适度缩小 → LINEAR: 边缘仍锐, HQ_CUBIC 负权会过冲 ring 出光晕 ("锐化过头");
//         LINEAR 2×2 无负权不过冲。fit 看图主场景 (金字塔活动层略缩小) 落此档。
//   >=1.0 放大       → HQ_CUBIC: 上采取其平滑。
// (D2D 没有 HIGH_QUALITY_LINEAR 常量, LINEAR 即标准 bilinear。)
//
// 演进: 初版 <0.5 用 LINEAR (想当然"大幅下采 cubic ring", 实际 prefilter 已抹平锐边,
// 反而 LINEAR 欠采样发硬); L190 一度把 <1.0 全改 LINEAR (修 0.5~1.0 文字过冲) 顺带把
// <0.5 也锁死 LINEAR; L191 恢复 <0.5 → HQ_CUBIC, 修文档扫描 / 单层大图 fit 发硬。
inline D2D1_INTERPOLATION_MODE PickInterp(float screen_per_source) {
    if (screen_per_source < 0.5f)
        return D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC;  // 大幅缩小: prefilter 抗欠采样
    return screen_per_source < 1.0f
        ? D2D1_INTERPOLATION_MODE_LINEAR                    // 适度缩小: 无过冲 (L190)
        : D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC;       // 放大: 平滑
}

inline ImageSampling SamplingForInterp(D2D1_INTERPOLATION_MODE interp) {
    if (interp == D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR) return ImageSampling::Nearest;
    if (interp == D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC) return ImageSampling::HighQualityCubic;
    return ImageSampling::Linear;
}

// 瓦片专用: 越界采样 clamp 到边缘像素。DrawBitmap 默认按透明采样边界外,
// 放大时每块瓦片边缘插值淡出 → 拼缝一条半透明缝线 (透明图上尤其明显)。
inline ImageSampling TileSamplingForInterp(D2D1_INTERPOLATION_MODE interp) {
    if (interp == D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR) return ImageSampling::Nearest;
    if (interp == D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC) return ImageSampling::HighQualityCubicClamp;
    return ImageSampling::LinearClamp;
}

inline int NormalizeAngle(int a) {
    // 任意 int → 最近 90° 的 0/90/180/270.
    int m = ((a % 360) + 360) % 360;
    // round to nearest 90, then mod 360.
    return ((m + 45) / 90 * 90) % 360;
}

} // namespace

GhImgViewWidget::GhImgViewWidget() {
    // bgColor 沿用基类默认 {0,0,0,0} = 透明，跟其它 core-ui widget 一致。
    // 宿主需要自定义底色，在 .uix 里写
    //     gh_img_view { background: <color> | <gradient>; }
    // CSS 的 background 不会自动从父容器继承（CSS 标准行为），但 widget 默认
    // 透明意味着父容器的 background（含 linear-gradient 棋盘格）会透出来 ——
    // 这是看图软件常见的浅灰/棋盘格画布外观需要的 baseline。
    cssTag  = "gh_img_view";
    focusable = true;
}

// L173 Phase 4: 后台 SVG 栅格化结果槽。widget 与后台渲染线程经 shared_ptr 共享,
// 线程只碰这个 slot + 自己持有的 Document/HWND (不碰 widget), 故 widget 析构 /
// 换图都不悬空; slot 在双方都释放后销毁。
struct SvgRenderSlot {
    struct Request {
        float    srcL = 0.0f;
        float    srcT = 0.0f;
        float    srcR = 0.0f;
        float    srcB = 0.0f;
        uint32_t w = 0;
        uint32_t h = 0;
    };

    std::mutex            mu;            // 保护 bgra / request / ready
    std::vector<uint8_t>  bgra;          // 渲好的紧凑 BGRA premul (w*h*4)
    Request               readyReq;
    Request               wantReq;
    uint32_t              w = 0, h = 0;
    bool                  ready = false;
    std::atomic<bool>     inFlight{false};    // 是否有后台线程在渲
};

namespace {

inline bool SvgReqValid(const SvgRenderSlot::Request& req) {
    return req.w > 0 && req.h > 0 &&
           req.srcR > req.srcL && req.srcB > req.srcT;
}

inline bool SvgReqSame(const SvgRenderSlot::Request& a,
                       const SvgRenderSlot::Request& b) {
    return a.w == b.w && a.h == b.h &&
           std::fabs(a.srcL - b.srcL) < 0.01f &&
           std::fabs(a.srcT - b.srcT) < 0.01f &&
           std::fabs(a.srcR - b.srcR) < 0.01f &&
           std::fabs(a.srcB - b.srcB) < 0.01f;
}

inline void InvalidateSvgRenderSlot_(const std::shared_ptr<SvgRenderSlot>& slot) {
    if (!slot) return;
    std::lock_guard<std::mutex> lk(slot->mu);
    slot->wantReq = {};
    slot->ready = false;
}

inline bool SvgRenderRequestToBgra(const std::shared_ptr<lunasvg::Document>& doc,
                                   const SvgRenderSlot::Request& req,
                                   std::vector<uint8_t>& out,
                                   uint32_t& outW, uint32_t& outH) {
    if (!doc || !SvgReqValid(req)) return false;

    const float srcW = req.srcR - req.srcL;
    const float srcH = req.srcB - req.srcT;
    const float sx = static_cast<float>(req.w) / srcW;
    const float sy = static_cast<float>(req.h) / srcH;
    lunasvg::Matrix matrix(sx, 0.0f, 0.0f, sy,
                           -req.srcL * sx, -req.srcT * sy);

    lunasvg::Bitmap bmp(static_cast<int>(req.w), static_cast<int>(req.h));
    if (bmp.isNull() || !bmp.data()) return false;
    doc->render(bmp, matrix);

    const uint32_t bw = static_cast<uint32_t>(std::max(0, bmp.width()));
    const uint32_t bh = static_cast<uint32_t>(std::max(0, bmp.height()));
    const int stride = bmp.stride();
    if (bw == 0 || bh == 0 || stride <= 0) return false;

    out.assign(static_cast<size_t>(bw) * bh * 4, 0);
    const uint8_t* src = bmp.data();
    const size_t dstRowBytes = static_cast<size_t>(bw) * 4;
    const size_t srcRowBytes = std::min(dstRowBytes, static_cast<size_t>(stride));
    for (uint32_t y = 0; y < bh; ++y) {
        memcpy(out.data() + static_cast<size_t>(y) * dstRowBytes,
               src + static_cast<size_t>(y) * stride,
               srcRowBytes);
    }
    outW = bw;
    outH = bh;
    return true;
}

std::string ExtractXmlAttr_(const std::string& tag, const char* name) {
    std::string key(name);
    size_t p = tag.find(key);
    while (p != std::string::npos) {
        const bool leftOk = (p == 0) || std::isspace(static_cast<unsigned char>(tag[p - 1])) || tag[p - 1] == '<';
        const size_t q = p + key.size();
        if (leftOk && q < tag.size() && tag[q] == '=') {
            size_t v = q + 1;
            if (v >= tag.size()) return {};
            char quote = tag[v];
            if (quote == '"' || quote == '\'') {
                size_t e = tag.find(quote, v + 1);
                if (e == std::string::npos) return {};
                return tag.substr(v + 1, e - v - 1);
            }
            size_t e = tag.find_first_of(" \t\r\n/>", v);
            return tag.substr(v, e == std::string::npos ? std::string::npos : e - v);
        }
        p = tag.find(key, p + 1);
    }
    return {};
}

std::string LowerAscii_(std::string s) {
    for (char& ch : s)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

bool IsXmlNameChar_(char ch);

size_t FindMatchingParen_(const std::string& s, size_t open) {
    int depth = 0;
    char quote = 0;
    for (size_t i = open; i < s.size(); ++i) {
        char ch = s[i];
        if (quote) {
            if (ch == quote) quote = 0;
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == '(') {
            ++depth;
        } else if (ch == ')') {
            --depth;
            if (depth == 0) return i;
        }
    }
    return std::string::npos;
}

size_t FindTopLevelComma_(const std::string& s, size_t begin, size_t end) {
    int depth = 0;
    char quote = 0;
    for (size_t i = begin; i < end; ++i) {
        char ch = s[i];
        if (quote) {
            if (ch == quote) quote = 0;
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == '(') {
            ++depth;
        } else if (ch == ')') {
            --depth;
        } else if (ch == ',' && depth == 0) {
            return i;
        }
    }
    return std::string::npos;
}

std::string TrimAscii_(std::string_view v) {
    size_t b = 0, e = v.size();
    while (b < e && std::isspace(static_cast<unsigned char>(v[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(v[e - 1]))) --e;
    return std::string(v.substr(b, e - b));
}

std::string ExtractSvgRootTag_(const std::string& xml) {
    size_t pos = 0;
    while ((pos = xml.find("<svg", pos)) != std::string::npos) {
        const size_t nameEnd = pos + 4;
        if (nameEnd < xml.size() && IsXmlNameChar_(xml[nameEnd])) {
            pos = nameEnd;
            continue;
        }
        size_t end = xml.find('>', nameEnd);
        if (end == std::string::npos) return {};
        return xml.substr(pos, end - pos + 1);
    }
    return {};
}

float ParseSvgCssPxLength_(const std::string& value, float fallback = 0.0f) {
    std::string s = TrimAscii_(value);
    if (s.empty()) return fallback;
    char* end = nullptr;
    float v = std::strtof(s.c_str(), &end);
    if (end == s.c_str() || !std::isfinite(v) || v <= 0.0f) return fallback;
    std::string unit = TrimAscii_(std::string_view(end, s.c_str() + s.size() - end));
    unit = LowerAscii_(unit);
    if (unit.empty() || unit == "px") return v;
    if (unit == "pt") return v * (96.0f / 72.0f);
    if (unit == "pc") return v * 16.0f;
    if (unit == "in") return v * 96.0f;
    if (unit == "cm") return v * (96.0f / 2.54f);
    if (unit == "mm") return v * (96.0f / 25.4f);
    if (unit == "q")  return v * (96.0f / 101.6f);
    return fallback;
}

bool ParseSvgViewBoxSize_(const std::string& tag, float& w, float& h) {
    std::string vb = ExtractXmlAttr_(tag, "viewBox");
    float minX = 0.0f, minY = 0.0f, vbW = 0.0f, vbH = 0.0f;
    if (vb.empty() ||
        std::sscanf(vb.c_str(), "%f %f %f %f", &minX, &minY, &vbW, &vbH) != 4 ||
        vbW <= 0.0f || vbH <= 0.0f) {
        return false;
    }
    w = vbW;
    h = vbH;
    return true;
}

std::string ReplaceCssLightDark_(std::string svg) {
    size_t pos = 0;
    while ((pos = svg.find("light-dark(", pos)) != std::string::npos) {
        size_t open = pos + strlen("light-dark");
        size_t close = FindMatchingParen_(svg, open);
        if (close == std::string::npos) break;
        size_t comma = FindTopLevelComma_(svg, open + 1, close);
        if (comma == std::string::npos) {
            pos = close + 1;
            continue;
        }
        std::string light = TrimAscii_(std::string_view(svg).substr(open + 1,
                                                                     comma - open - 1));
        svg.replace(pos, close + 1 - pos, light);
        pos += light.size();
    }
    return svg;
}

std::string ExpandSvgSwitchImageFallbacksForD2D_(const std::string& svg) {
    std::string out;
    out.reserve(svg.size());
    size_t pos = 0;
    while (true) {
        size_t sw = svg.find("<switch", pos);
        if (sw == std::string::npos) {
            out.append(svg, pos, std::string::npos);
            break;
        }
        out.append(svg, pos, sw - pos);
        size_t openEnd = svg.find('>', sw);
        size_t close = openEnd == std::string::npos
            ? std::string::npos
            : svg.find("</switch>", openEnd + 1);
        if (openEnd == std::string::npos || close == std::string::npos) {
            out.append(svg, sw, std::string::npos);
            break;
        }
        size_t img = svg.find("<image", openEnd + 1);
        if (img != std::string::npos && img < close) {
            size_t imgEnd = svg.find('>', img);
            if (imgEnd != std::string::npos && imgEnd < close) {
                out.append(svg, img, imgEnd + 1 - img);
            }
        } else {
            out.append(svg, sw, close + 9 - sw);
        }
        pos = close + 9;
    }
    return out;
}

bool HasSvgFilter_(const std::string& svg) {
    std::string lower = LowerAscii_(svg);
    return lower.find("<filter") != std::string::npos ||
           lower.find("<fedropshadow") != std::string::npos ||
           lower.find("<fegaussianblur") != std::string::npos ||
           lower.find(" filter=") != std::string::npos ||
           lower.find("\tfilter=") != std::string::npos ||
           lower.find("\nfilter=") != std::string::npos ||
           lower.find("\rfilter=") != std::string::npos;
}

bool HasSvgGaussianBlurFilter_(const std::string& svg) {
    return LowerAscii_(svg).find("<fegaussianblur") != std::string::npos;
}

bool NeedsLunaSvgMainRenderer_(const std::string& svg);

float ParseSvgFloat_(const std::string& value, float fallback = 0.0f) {
    if (value.empty()) return fallback;
    char* end = nullptr;
    float v = std::strtof(value.c_str(), &end);
    return end == value.c_str() ? fallback : v;
}

std::vector<float> ParseSvgFloatList_(const std::string& value) {
    std::vector<float> out;
    const char* p = value.c_str();
    while (*p) {
        while (*p && (std::isspace(static_cast<unsigned char>(*p)) || *p == ',')) {
            ++p;
        }
        if (!*p) break;
        char* end = nullptr;
        float v = std::strtof(p, &end);
        if (end == p) {
            ++p;
            continue;
        }
        out.push_back(v);
        p = end;
    }
    return out;
}

std::string SvgHexColorFromUnitRgb_(float r, float g, float b) {
    auto channel = [](float v) -> int {
        v = std::clamp(v, 0.0f, 1.0f);
        return static_cast<int>(std::round(v * 255.0f));
    };
    char buf[8] = {};
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X",
                  channel(r), channel(g), channel(b));
    return std::string(buf);
}

std::string ParseSvgColor_(const std::string& value) {
    if (value.size() == 7 && value[0] == '#') return value;
    if (value.size() == 4 && value[0] == '#') {
        std::string out = "#000000";
        out[1] = out[2] = value[1];
        out[3] = out[4] = value[2];
        out[5] = out[6] = value[3];
        return out;
    }
    return "#000000";
}

void EraseXmlAttr_(std::string& attrs, std::string_view name) {
    size_t pos = 0;
    while ((pos = attrs.find(name, pos)) != std::string::npos) {
        const bool leftOk = pos == 0 ||
            std::isspace(static_cast<unsigned char>(attrs[pos - 1]));
        size_t p = pos + name.size();
        while (p < attrs.size() &&
               std::isspace(static_cast<unsigned char>(attrs[p]))) {
            ++p;
        }
        if (!leftOk || p >= attrs.size() || attrs[p] != '=') {
            pos += name.size();
            continue;
        }

        ++p;
        while (p < attrs.size() &&
               std::isspace(static_cast<unsigned char>(attrs[p]))) {
            ++p;
        }
        if (p < attrs.size() && (attrs[p] == '"' || attrs[p] == '\'')) {
            const char quote = attrs[p++];
            size_t q = attrs.find(quote, p);
            p = (q == std::string::npos) ? attrs.size() : q + 1;
        } else {
            while (p < attrs.size() &&
                   !std::isspace(static_cast<unsigned char>(attrs[p]))) {
                ++p;
            }
        }
        size_t start = pos;
        while (start > 0 &&
               std::isspace(static_cast<unsigned char>(attrs[start - 1]))) {
            --start;
        }
        attrs.erase(start, p - start);
        pos = start;
    }
}

bool TagHasAttr_(const std::string& tag, const char* name) {
    return !ExtractXmlAttr_(tag, name).empty();
}

bool TagNameIs_(const std::string& name, const char* a, const char* b = nullptr,
                const char* c = nullptr, const char* d = nullptr) {
    return name == a || (b && name == b) || (c && name == c) || (d && name == d);
}

struct SvgTagInfo_ {
    std::string name;
    bool closing = false;
    bool selfClosing = false;
    bool special = false;
};

SvgTagInfo_ ParseSvgTag_(const std::string& svg, size_t lt, size_t gt) {
    SvgTagInfo_ info;
    if (lt + 1 >= svg.size() || lt >= gt) {
        info.special = true;
        return info;
    }

    size_t p = lt + 1;
    if (svg[p] == '/' && p + 1 < gt) {
        info.closing = true;
        ++p;
    } else if (svg[p] == '?' || svg[p] == '!') {
        info.special = true;
        return info;
    }

    while (p < gt && std::isspace(static_cast<unsigned char>(svg[p]))) ++p;
    size_t nameStart = p;
    while (p < gt && IsXmlNameChar_(svg[p])) ++p;
    if (p == nameStart) {
        info.special = true;
        return info;
    }
    info.name = LowerAscii_(svg.substr(nameStart, p - nameStart));

    size_t tail = gt;
    while (tail > lt && std::isspace(static_cast<unsigned char>(svg[tail - 1]))) --tail;
    info.selfClosing = tail > lt && svg[tail - 1] == '/';
    return info;
}

bool IsD2DUnsupportedSvgElement_(const std::string& tagName) {
    return tagName == "filter" || tagName == "foreignobject";
}

size_t FindSvgElementEnd_(const std::string& svg, size_t openLt, size_t openGt,
                          const std::string& tagName) {
    size_t pos = openGt + 1;
    int depth = 1;
    while (pos < svg.size()) {
        size_t lt = svg.find('<', pos);
        if (lt == std::string::npos) break;
        size_t gt = svg.find('>', lt);
        if (gt == std::string::npos) break;

        SvgTagInfo_ info = ParseSvgTag_(svg, lt, gt);
        if (!info.special && info.name == tagName) {
            if (info.closing) {
                --depth;
                if (depth == 0) return gt + 1;
            } else if (!info.selfClosing) {
                ++depth;
            }
        }
        pos = gt + 1;
    }
    return openGt + 1;
}

std::string SvgLocalName_(const std::string& tagName) {
    size_t colon = tagName.rfind(':');
    return colon == std::string::npos ? tagName : tagName.substr(colon + 1);
}

bool IsD2DLayerResourceElement_(const std::string& tagName) {
    const std::string local = SvgLocalName_(tagName);
    return local == "defs" ||
           local == "style" ||
           local == "lineargradient" ||
           local == "radialgradient" ||
           local == "pattern" ||
           local == "clippath" ||
           local == "mask" ||
           local == "marker" ||
           local == "symbol";
}

void CollectSvgUrlRefs_(const std::string& text, std::vector<std::string>& out) {
    size_t pos = 0;
    while (pos + 3 <= text.size()) {
        if (std::tolower(static_cast<unsigned char>(text[pos])) != 'u' ||
            std::tolower(static_cast<unsigned char>(text[pos + 1])) != 'r' ||
            std::tolower(static_cast<unsigned char>(text[pos + 2])) != 'l') {
            ++pos;
            continue;
        }

        const size_t afterName = pos + 3;
        if (pos > 0 && IsXmlNameChar_(text[pos - 1])) {
            pos = afterName;
            continue;
        }
        if (afterName < text.size() && IsXmlNameChar_(text[afterName])) {
            pos = afterName;
            continue;
        }

        size_t open = afterName;
        while (open < text.size() && std::isspace(static_cast<unsigned char>(text[open]))) ++open;
        if (open >= text.size() || text[open] != '(') {
            pos = afterName;
            continue;
        }
        size_t p = open + 1;
        while (p < text.size() && std::isspace(static_cast<unsigned char>(text[p]))) ++p;
        if (p < text.size() && (text[p] == '"' || text[p] == '\'')) ++p;
        if (p >= text.size() || text[p] != '#') {
            pos = open + 1;
            continue;
        }

        size_t idStart = p + 1;
        size_t idEnd = idStart;
        while (idEnd < text.size() && IsXmlNameChar_(text[idEnd])) ++idEnd;
        if (idEnd > idStart) {
            out.push_back(text.substr(idStart, idEnd - idStart));
        }
        pos = idEnd;
    }
}

std::string ExtractSvgHrefRefId_(const std::string& tag) {
    std::string href = ExtractXmlAttr_(tag, "href");
    if (href.empty()) href = ExtractXmlAttr_(tag, "xlink:href");
    href = TrimAscii_(href);
    if (href.size() > 1 && href[0] == '#') return href.substr(1);
    return {};
}

bool SvgPatternContainsImageRef_(const std::string& patternXml,
                                 const std::unordered_set<std::string>& imageIds) {
    size_t pos = 0;
    while (pos < patternXml.size()) {
        size_t lt = patternXml.find('<', pos);
        if (lt == std::string::npos) break;
        size_t gt = patternXml.find('>', lt);
        if (gt == std::string::npos) break;

        SvgTagInfo_ info = ParseSvgTag_(patternXml, lt, gt);
        if (!info.special && !info.closing) {
            std::string local = SvgLocalName_(info.name);
            if (local == "image") return true;

            std::string tag = patternXml.substr(lt, gt - lt + 1);
            std::string refId = ExtractSvgHrefRefId_(tag);
            if (!refId.empty() && imageIds.find(refId) != imageIds.end()) {
                return true;
            }
        }
        pos = gt + 1;
    }
    return false;
}

bool HasUsedImageBackedSvgPattern_(const std::string& svg) {
    std::unordered_set<std::string> imageIds;
    size_t pos = 0;
    while (pos < svg.size()) {
        size_t lt = svg.find('<', pos);
        if (lt == std::string::npos) break;
        size_t gt = svg.find('>', lt);
        if (gt == std::string::npos) break;

        SvgTagInfo_ info = ParseSvgTag_(svg, lt, gt);
        if (!info.special && !info.closing && SvgLocalName_(info.name) == "image") {
            std::string id = ExtractXmlAttr_(svg.substr(lt, gt - lt + 1), "id");
            if (!id.empty()) imageIds.insert(std::move(id));
        }
        pos = gt + 1;
    }

    std::unordered_set<std::string> imageBackedPatternIds;
    pos = 0;
    while (pos < svg.size()) {
        size_t lt = svg.find('<', pos);
        if (lt == std::string::npos) break;
        size_t gt = svg.find('>', lt);
        if (gt == std::string::npos) break;

        SvgTagInfo_ info = ParseSvgTag_(svg, lt, gt);
        if (!info.special && !info.closing && SvgLocalName_(info.name) == "pattern") {
            std::string tag = svg.substr(lt, gt - lt + 1);
            std::string id = ExtractXmlAttr_(tag, "id");
            size_t end = info.selfClosing ? gt + 1 : FindSvgElementEnd_(svg, lt, gt, info.name);
            if (!id.empty() && end > lt) {
                std::string patternXml = svg.substr(lt, end - lt);
                if (SvgPatternContainsImageRef_(patternXml, imageIds)) {
                    imageBackedPatternIds.insert(std::move(id));
                }
            }
            pos = end;
            continue;
        }
        pos = gt + 1;
    }

    if (imageBackedPatternIds.empty()) return false;

    std::vector<std::string> refs;
    pos = 0;
    while (pos < svg.size()) {
        size_t lt = svg.find('<', pos);
        if (lt == std::string::npos) break;
        size_t gt = svg.find('>', lt);
        if (gt == std::string::npos) break;

        SvgTagInfo_ info = ParseSvgTag_(svg, lt, gt);
        if (!info.special && !info.closing && SvgLocalName_(info.name) != "pattern") {
            refs.clear();
            CollectSvgUrlRefs_(svg.substr(lt, gt - lt + 1), refs);
            for (const std::string& ref : refs) {
                if (imageBackedPatternIds.find(ref) != imageBackedPatternIds.end()) {
                    return true;
                }
            }
        }
        pos = gt + 1;
    }
    return false;
}

struct SvgD2DMaskConversion_ {
    std::string xml;
    bool hasUnconvertedMaskRefs = false;
};

bool SvgStyleDeclIsAlphaMask_(std::string style) {
    style = LowerAscii_(std::move(style));
    style.erase(std::remove_if(style.begin(), style.end(),
                               [](unsigned char c) { return std::isspace(c); }),
                style.end());
    return style.find("mask-type:alpha") != std::string::npos;
}

bool IsAlphaSvgMaskTag_(const std::string& tag) {
    std::string maskType = LowerAscii_(TrimAscii_(ExtractXmlAttr_(tag, "mask-type")));
    if (maskType == "alpha") return true;
    return SvgStyleDeclIsAlphaMask_(ExtractXmlAttr_(tag, "style"));
}

bool IsD2DClipConvertibleMaskShape_(const std::string& localName) {
    return localName == "path" ||
           localName == "rect" ||
           localName == "circle" ||
           localName == "ellipse" ||
           localName == "polygon" ||
           localName == "polyline";
}

bool SvgTagHasComplexMaskPaint_(const std::string& tag) {
    if (!ExtractXmlAttr_(tag, "style").empty()) return true;
    if (!ExtractXmlAttr_(tag, "filter").empty()) return true;
    if (!ExtractXmlAttr_(tag, "mask").empty()) return true;
    if (!ExtractXmlAttr_(tag, "clip-path").empty()) return true;
    if (!ExtractXmlAttr_(tag, "opacity").empty() &&
        ParseSvgFloat_(ExtractXmlAttr_(tag, "opacity"), 1.0f) < 0.999f) {
        return true;
    }
    if (!ExtractXmlAttr_(tag, "fill-opacity").empty() &&
        ParseSvgFloat_(ExtractXmlAttr_(tag, "fill-opacity"), 1.0f) < 0.999f) {
        return true;
    }
    std::string fill = LowerAscii_(TrimAscii_(ExtractXmlAttr_(tag, "fill")));
    if (fill == "none" || fill.find("url(") != std::string::npos) {
        return true;
    }
    std::string stroke = LowerAscii_(TrimAscii_(ExtractXmlAttr_(tag, "stroke")));
    if (!stroke.empty() && stroke != "none") return true;
    return false;
}

bool SvgTextOnlyWhitespace_(std::string_view text) {
    for (char c : text) {
        if (!std::isspace(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

bool ExtractSingleSimpleMaskShape_(const std::string& content,
                                   std::string& shapeXml) {
    bool found = false;
    size_t pos = 0;
    while (pos < content.size()) {
        size_t lt = content.find('<', pos);
        if (lt == std::string::npos) {
            return SvgTextOnlyWhitespace_(std::string_view(content).substr(pos)) && found;
        }
        if (!SvgTextOnlyWhitespace_(std::string_view(content).substr(pos, lt - pos))) {
            return false;
        }
        size_t gt = content.find('>', lt);
        if (gt == std::string::npos) return false;

        SvgTagInfo_ info = ParseSvgTag_(content, lt, gt);
        if (info.special) {
            pos = gt + 1;
            continue;
        }
        if (info.closing || found) return false;

        std::string local = SvgLocalName_(info.name);
        if (!IsD2DClipConvertibleMaskShape_(local)) return false;

        const size_t end = info.selfClosing
            ? gt + 1
            : FindSvgElementEnd_(content, lt, gt, info.name);
        if (end <= lt || end > content.size()) return false;

        std::string shape = content.substr(lt, end - lt);
        std::string shapeTag = content.substr(lt, gt - lt + 1);
        if (SvgTagHasComplexMaskPaint_(shapeTag)) return false;
        if (!info.selfClosing) {
            const size_t closeLt = content.rfind("</", end - 1);
            if (closeLt == std::string::npos || closeLt <= gt) {
                return false;
            }
            if (!SvgTextOnlyWhitespace_(std::string_view(content).substr(
                    gt + 1, closeLt - gt - 1))) {
                return false;
            }
        }

        shapeXml = std::move(shape);
        found = true;
        pos = end;
    }
    return found;
}

bool BuildD2DClipPathFromSimpleMask_(const std::string& maskXml,
                                     std::string& id,
                                     std::string& clipPathXml) {
    size_t lt = maskXml.find('<');
    size_t gt = lt == std::string::npos ? std::string::npos : maskXml.find('>', lt);
    if (lt == std::string::npos || gt == std::string::npos) return false;

    std::string maskTag = maskXml.substr(lt, gt - lt + 1);
    SvgTagInfo_ info = ParseSvgTag_(maskXml, lt, gt);
    if (info.special || info.closing || info.selfClosing ||
        SvgLocalName_(info.name) != "mask") {
        return false;
    }
    if (!IsAlphaSvgMaskTag_(maskTag)) return false;

    id = ExtractXmlAttr_(maskTag, "id");
    if (id.empty()) return false;

    size_t closeLt = maskXml.rfind("</");
    if (closeLt == std::string::npos || closeLt <= gt) return false;

    std::string shapeXml;
    if (!ExtractSingleSimpleMaskShape_(maskXml.substr(gt + 1, closeLt - gt - 1),
                                       shapeXml)) {
        return false;
    }

    clipPathXml = "<clipPath id=\"" + id +
                  "\" clipPathUnits=\"userSpaceOnUse\">" +
                  shapeXml + "</clipPath>";
    return true;
}

std::string ExtractSvgUrlRefId_(const std::string& value) {
    std::vector<std::string> refs;
    CollectSvgUrlRefs_(value, refs);
    return refs.empty() ? std::string{} : refs.front();
}

void InsertXmlAttr_(std::string& tag, const std::string& attr) {
    size_t insert = tag.rfind('>');
    if (insert == std::string::npos) return;
    size_t slash = insert;
    while (slash > 0 && std::isspace(static_cast<unsigned char>(tag[slash - 1]))) {
        --slash;
    }
    if (slash > 0 && tag[slash - 1] == '/') insert = slash - 1;
    tag.insert(insert, " " + attr);
}

bool HasSvgMaskAttribute_(const std::string& svg) {
    size_t pos = 0;
    while (pos < svg.size()) {
        size_t lt = svg.find('<', pos);
        if (lt == std::string::npos) break;
        size_t gt = svg.find('>', lt);
        if (gt == std::string::npos) break;

        SvgTagInfo_ info = ParseSvgTag_(svg, lt, gt);
        if (!info.special && !info.closing) {
            std::string tag = svg.substr(lt, gt - lt + 1);
            if (!ExtractXmlAttr_(tag, "mask").empty()) return true;
        }
        pos = gt + 1;
    }
    return false;
}

SvgD2DMaskConversion_ ConvertSimpleSvgMasksToClipPathsForD2D_(
    const std::string& svg) {
    std::unordered_map<std::string, std::string> clipByMaskId;
    struct Replacement {
        size_t start = 0;
        size_t end = 0;
        std::string text;
    };
    std::vector<Replacement> replacements;

    size_t pos = 0;
    while (pos < svg.size()) {
        size_t lt = svg.find('<', pos);
        if (lt == std::string::npos) break;
        size_t gt = svg.find('>', lt);
        if (gt == std::string::npos) break;

        SvgTagInfo_ info = ParseSvgTag_(svg, lt, gt);
        if (!info.special && !info.closing && SvgLocalName_(info.name) == "mask") {
            const size_t end = info.selfClosing ? gt + 1
                : FindSvgElementEnd_(svg, lt, gt, info.name);
            std::string id;
            std::string clipPathXml;
            if (end > lt &&
                BuildD2DClipPathFromSimpleMask_(svg.substr(lt, end - lt),
                                                id, clipPathXml) &&
                clipByMaskId.find(id) == clipByMaskId.end()) {
                clipByMaskId.emplace(id, clipPathXml);
                replacements.push_back(Replacement{lt, end, std::move(clipPathXml)});
            }
            pos = end;
            continue;
        }
        pos = gt + 1;
    }

    if (clipByMaskId.empty()) {
        return SvgD2DMaskConversion_{svg, HasSvgMaskAttribute_(svg)};
    }

    std::string converted;
    converted.reserve(svg.size());
    pos = 0;
    size_t replIndex = 0;
    while (pos < svg.size()) {
        if (replIndex < replacements.size() && pos == replacements[replIndex].start) {
            converted += replacements[replIndex].text;
            pos = replacements[replIndex].end;
            ++replIndex;
            continue;
        }

        size_t lt = svg.find('<', pos);
        if (lt == std::string::npos) {
            converted.append(svg, pos, std::string::npos);
            break;
        }
        converted.append(svg, pos, lt - pos);
        if (replIndex < replacements.size() && lt == replacements[replIndex].start) {
            converted += replacements[replIndex].text;
            pos = replacements[replIndex].end;
            ++replIndex;
            continue;
        }
        size_t gt = svg.find('>', lt);
        if (gt == std::string::npos) {
            converted.append(svg, lt, std::string::npos);
            break;
        }

        std::string tag = svg.substr(lt, gt - lt + 1);
        SvgTagInfo_ info = ParseSvgTag_(svg, lt, gt);
        if (!info.special && !info.closing && SvgLocalName_(info.name) != "mask") {
            std::string maskId = ExtractSvgUrlRefId_(ExtractXmlAttr_(tag, "mask"));
            if (!maskId.empty() &&
                clipByMaskId.find(maskId) != clipByMaskId.end() &&
                ExtractXmlAttr_(tag, "clip-path").empty()) {
                EraseXmlAttr_(tag, "mask");
                InsertXmlAttr_(tag, "clip-path=\"url(#" + maskId + ")\"");
            }
        }
        converted += tag;
        pos = gt + 1;
    }

    return SvgD2DMaskConversion_{std::move(converted),
                                 HasSvgMaskAttribute_(converted)};
}

bool NeedsLunaSvgMainRenderer_(const std::string& svg) {
    /* D2D SvgDocument 是主交互视图的优先路径: 轴线、曲线、文字轮廓保持矢量
     * 直绘。之前遇到 data:image/png 就整图切 LunaSVG 栅格, 会把本来可矢量画
     * 的 Matplotlib 坐标轴/标签一起栅格化, 横轴小字和刻度出现毛刺。
     * 但 D2D 对位图 pattern 填充支持不完整, 会把这类 SVG 的背景图案丢掉。
     * 因此只在元素实际 url(#patternId) 引用了 image-backed pattern 时回退。
     *
     * feGaussianBlur / SourceAlpha 链式阴影需要按 SVG 文档顺序对单个元素做
     * 离屏合成; D2D overlay 层只能画在整张基础 SVG 之前或之后, 会被背景
     * 盖住或盖住后续内容。LunaSVG 的元素级 filter hook 更适合这类文件。 */
    return HasUsedImageBackedSvgPattern_(svg) || HasSvgGaussianBlurFilter_(svg);
}

std::string BuildD2DDropShadowSvg_(const std::string& svg,
                                   float& dx, float& dy, float& stdDeviation) {
    size_t rootLt = svg.find("<svg");
    size_t rootGt = rootLt == std::string::npos ? std::string::npos : svg.find('>', rootLt);
    if (rootLt == std::string::npos || rootGt == std::string::npos) return {};

    std::string lower = LowerAscii_(svg);
    size_t feLt = lower.find("<fedropshadow");
    size_t feGt = feLt == std::string::npos ? std::string::npos : svg.find('>', feLt);
    if (feLt == std::string::npos || feGt == std::string::npos) return {};

    std::string feTag = svg.substr(feLt, feGt - feLt + 1);
    dx = ParseSvgFloat_(ExtractXmlAttr_(feTag, "dx"));
    dy = ParseSvgFloat_(ExtractXmlAttr_(feTag, "dy"));
    stdDeviation = ParseSvgFloat_(ExtractXmlAttr_(feTag, "stdDeviation"));
    std::string color = ParseSvgColor_(ExtractXmlAttr_(feTag, "flood-color"));
    float opacity = std::clamp(ParseSvgFloat_(ExtractXmlAttr_(feTag, "flood-opacity"), 1.0f),
                               0.0f, 1.0f);
    if (stdDeviation <= 0.0f) stdDeviation = 0.01f;

    std::string out = svg.substr(rootLt, rootGt - rootLt + 1);
    size_t pos = rootGt + 1;
    while (true) {
        size_t lt = svg.find('<', pos);
        if (lt == std::string::npos) break;
        size_t gt = svg.find('>', lt);
        if (gt == std::string::npos) break;
        if (lt + 1 >= svg.size() || svg[lt + 1] == '/' || svg[lt + 1] == '?' ||
            svg[lt + 1] == '!') {
            pos = gt + 1;
            continue;
        }

        std::string tag = svg.substr(lt, gt - lt + 1);
        if (ExtractXmlAttr_(tag, "filter").empty()) {
            pos = gt + 1;
            continue;
        }

        size_t nameStart = lt + 1;
        size_t nameEnd = nameStart;
        while (nameEnd < gt && IsXmlNameChar_(svg[nameEnd])) ++nameEnd;
        std::string tagName = svg.substr(nameStart, nameEnd - nameStart);
        std::string attrs = svg.substr(nameEnd, gt - nameEnd);
        if (!attrs.empty() && attrs.back() == '>') attrs.pop_back();
        if (!attrs.empty() && attrs.back() == '/') attrs.pop_back();

        const bool hadStroke = TagHasAttr_(tag, "stroke") ||
                               TagNameIs_(tagName, "line", "polyline");
        const bool hadFill = TagHasAttr_(tag, "fill") ||
                             TagNameIs_(tagName, "circle", "ellipse", "rect", "polygon");
        EraseXmlAttr_(attrs, "filter");
        EraseXmlAttr_(attrs, "style");
        EraseXmlAttr_(attrs, "fill");
        EraseXmlAttr_(attrs, "stroke");
        EraseXmlAttr_(attrs, "fill-opacity");
        EraseXmlAttr_(attrs, "stroke-opacity");

        out += "<";
        out += tagName;
        out += attrs;
        if (hadFill) {
            out += " fill=\"";
            out += color;
            out += "\" fill-opacity=\"";
            out += std::to_string(opacity);
            out += "\"";
        } else {
            out += " fill=\"none\"";
        }
        if (hadStroke) {
            out += " stroke=\"";
            out += color;
            out += "\" stroke-opacity=\"";
            out += std::to_string(opacity);
            out += "\"";
        }
        out += "/>";
        pos = gt + 1;
    }
    out += "</svg>";
    return out;
}

std::string BuildD2DFilteredElementsSvg_(const std::string& svg) {
    size_t rootLt = svg.find("<svg");
    size_t rootGt = rootLt == std::string::npos ? std::string::npos : svg.find('>', rootLt);
    if (rootLt == std::string::npos || rootGt == std::string::npos) return {};

    std::string out = svg.substr(rootLt, rootGt - rootLt + 1);
    bool any = false;
    size_t pos = rootGt + 1;
    while (true) {
        size_t lt = svg.find('<', pos);
        if (lt == std::string::npos) break;
        size_t gt = svg.find('>', lt);
        if (gt == std::string::npos) break;
        if (lt + 1 >= svg.size() || svg[lt + 1] == '/' || svg[lt + 1] == '?' ||
            svg[lt + 1] == '!') {
            pos = gt + 1;
            continue;
        }

        std::string tag = svg.substr(lt, gt - lt + 1);
        if (ExtractXmlAttr_(tag, "filter").empty()) {
            pos = gt + 1;
            continue;
        }

        size_t nameStart = lt + 1;
        size_t nameEnd = nameStart;
        while (nameEnd < gt && IsXmlNameChar_(svg[nameEnd])) ++nameEnd;
        std::string tagName = svg.substr(nameStart, nameEnd - nameStart);
        std::string attrs = svg.substr(nameEnd, gt - nameEnd);
        if (!attrs.empty() && attrs.back() == '>') attrs.pop_back();
        if (!attrs.empty() && attrs.back() == '/') attrs.pop_back();
        EraseXmlAttr_(attrs, "filter");

        out += "<";
        out += tagName;
        out += attrs;
        out += "/>";
        any = true;
        pos = gt + 1;
    }
    if (!any) return {};
    out += "</svg>";
    return out;
}

enum class D2DFilterPaintMode_ {
    SourcePaintBlur,
    RecolorSourceAlpha
};

struct D2DFilterSpec_ {
    std::string id;
    D2DFilterPaintMode_ paintMode = D2DFilterPaintMode_::RecolorSourceAlpha;
    std::string color = "#000000";
    float opacity = 1.0f;
    float dx = 0.0f;
    float dy = 0.0f;
    float stdDeviation = 0.0f;
};

std::string BuildSvgThumbnailXml_(const std::string& svg) {
    std::string out;
    out.reserve(svg.size());
    size_t pos = 0;
    while (true) {
        size_t lt = svg.find('<', pos);
        if (lt == std::string::npos) {
            out.append(svg, pos, std::string::npos);
            break;
        }
        size_t gt = svg.find('>', lt);
        if (gt == std::string::npos) {
            out.append(svg, pos, std::string::npos);
            break;
        }

        std::string tag = svg.substr(lt, gt - lt + 1);
        SvgTagInfo_ info = ParseSvgTag_(svg, lt, gt);
        if (!info.special && !info.closing &&
            !ExtractXmlAttr_(tag, "filter").empty()) {
            EraseXmlAttr_(tag, "filter");
        }
        out.append(svg, pos, lt - pos);
        out += tag;
        pos = gt + 1;
    }
    return out;
}

bool FindSvgStartTag_(const std::string& xml, const std::string& localName,
                      size_t start, size_t* outLt, size_t* outGt,
                      std::string* outTag) {
    size_t pos = start;
    while (pos < xml.size()) {
        size_t lt = xml.find('<', pos);
        if (lt == std::string::npos) break;
        size_t gt = xml.find('>', lt);
        if (gt == std::string::npos) break;
        SvgTagInfo_ info = ParseSvgTag_(xml, lt, gt);
        if (!info.special && !info.closing &&
            SvgLocalName_(info.name) == localName) {
            if (outLt) *outLt = lt;
            if (outGt) *outGt = gt;
            if (outTag) *outTag = xml.substr(lt, gt - lt + 1);
            return true;
        }
        pos = gt + 1;
    }
    return false;
}

std::string ExtractFirstSvgUrlRefId_(const std::string& value) {
    std::vector<std::string> refs;
    CollectSvgUrlRefs_(value, refs);
    return refs.empty() ? std::string{} : refs.front();
}

bool ParseSvgColorMatrixShadow_(const std::string& tag,
                                std::string& color,
                                float& opacity) {
    std::vector<float> values = ParseSvgFloatList_(ExtractXmlAttr_(tag, "values"));
    if (values.size() < 20) return false;
    color = SvgHexColorFromUnitRgb_(values[4], values[9], values[14]);
    const float alphaMul = values[18];
    const float alphaAdd = values[19];
    opacity = std::clamp(std::fabs(alphaMul) > 0.0001f ? alphaMul : alphaAdd,
                         0.0f, 1.0f);
    return true;
}

D2DFilterSpec_ ParseD2DFilterSpec_(const std::string& filterXml) {
    D2DFilterSpec_ spec;

    std::string dropTag;
    if (FindSvgStartTag_(filterXml, "fedropshadow", 0, nullptr, nullptr, &dropTag)) {
        spec.paintMode = D2DFilterPaintMode_::RecolorSourceAlpha;
        spec.dx = ParseSvgFloat_(ExtractXmlAttr_(dropTag, "dx"));
        spec.dy = ParseSvgFloat_(ExtractXmlAttr_(dropTag, "dy"));
        spec.stdDeviation = ParseSvgFloat_(ExtractXmlAttr_(dropTag, "stdDeviation"));
        spec.color = ParseSvgColor_(ExtractXmlAttr_(dropTag, "flood-color"));
        spec.opacity = std::clamp(ParseSvgFloat_(ExtractXmlAttr_(dropTag, "flood-opacity"),
                                                1.0f),
                                  0.0f, 1.0f);
        if (spec.stdDeviation <= 0.0f) spec.stdDeviation = 0.01f;
        return spec;
    }

    std::string blurTag;
    if (!FindSvgStartTag_(filterXml, "fegaussianblur", 0, nullptr, nullptr, &blurTag)) {
        return {};
    }

    spec.stdDeviation = ParseSvgFloat_(ExtractXmlAttr_(blurTag, "stdDeviation"));
    if (spec.stdDeviation <= 0.0f) spec.stdDeviation = 0.01f;

    std::string offsetTag;
    if (FindSvgStartTag_(filterXml, "feoffset", 0, nullptr, nullptr, &offsetTag)) {
        spec.dx = ParseSvgFloat_(ExtractXmlAttr_(offsetTag, "dx"));
        spec.dy = ParseSvgFloat_(ExtractXmlAttr_(offsetTag, "dy"));
    }

    std::string lower = LowerAscii_(filterXml);
    const bool shadowChain = lower.find("sourcealpha") != std::string::npos ||
                             lower.find("dropshadow") != std::string::npos;
    if (!shadowChain) {
        spec.paintMode = D2DFilterPaintMode_::SourcePaintBlur;
        return spec;
    }

    spec.paintMode = D2DFilterPaintMode_::RecolorSourceAlpha;
    size_t scan = 0;
    std::string matrixTag;
    while (FindSvgStartTag_(filterXml, "fecolormatrix", scan, &scan, nullptr,
                            &matrixTag)) {
        ParseSvgColorMatrixShadow_(matrixTag, spec.color, spec.opacity);
        ++scan;
    }
    return spec;
}

std::vector<D2DFilterSpec_> CollectD2DFilterSpecs_(const std::string& svg) {
    std::vector<D2DFilterSpec_> out;
    size_t pos = 0;
    while (pos < svg.size()) {
        size_t lt = svg.find('<', pos);
        if (lt == std::string::npos) break;
        size_t gt = svg.find('>', lt);
        if (gt == std::string::npos) break;
        SvgTagInfo_ info = ParseSvgTag_(svg, lt, gt);
        if (!info.special && !info.closing && SvgLocalName_(info.name) == "filter") {
            std::string tag = svg.substr(lt, gt - lt + 1);
            std::string id = ExtractXmlAttr_(tag, "id");
            size_t end = info.selfClosing ? gt + 1
                                          : FindSvgElementEnd_(svg, lt, gt, info.name);
            if (!id.empty() && end > lt) {
                D2DFilterSpec_ spec = ParseD2DFilterSpec_(svg.substr(lt, end - lt));
                if (spec.stdDeviation > 0.0f) {
                    spec.id = std::move(id);
                    out.push_back(std::move(spec));
                }
            }
            pos = end;
            continue;
        }
        pos = gt + 1;
    }
    return out;
}

const D2DFilterSpec_* FindD2DFilterSpec_(const std::vector<D2DFilterSpec_>& specs,
                                         const std::string& id) {
    for (const auto& spec : specs) {
        if (spec.id == id) return &spec;
    }
    return nullptr;
}

/* 把一个带 filter 的元素的开标签改写成"阴影版": 去掉自身 paint, 换成滤镜的
 * flood 颜色。元素的子树和祖先链由分段器保留, 这里只动这一个开标签。
 *
 * 限界: 对 <g filter=...> 这种容器, 换 g 的 fill 盖不住子元素自己写死的 fill,
 * 阴影会带上原色。真正的做法是按 SourceAlpha 取整组 alpha 做剪影 —— 归 P2 的
 * filter → D2D effect 图编译器。此前的实现更糟 (直接把子树整个丢掉)。 */
std::string BuildShadowOpenTag_(const std::string& openTag,
                                const D2DFilterSpec_& spec) {
    size_t nameStart = 1;
    size_t nameEnd = nameStart;
    while (nameEnd < openTag.size() && IsXmlNameChar_(openTag[nameEnd])) ++nameEnd;
    if (nameEnd == nameStart) return openTag;
    const std::string tagName = openTag.substr(nameStart, nameEnd - nameStart);

    size_t tail = openTag.size();
    while (tail > nameEnd && openTag[tail - 1] != '>') --tail;
    if (tail == nameEnd) return openTag;
    --tail;                                     // 指向 '>'
    bool selfClosing = false;
    size_t attrEnd = tail;
    while (attrEnd > nameEnd &&
           std::isspace(static_cast<unsigned char>(openTag[attrEnd - 1]))) {
        --attrEnd;
    }
    if (attrEnd > nameEnd && openTag[attrEnd - 1] == '/') {
        selfClosing = true;
        --attrEnd;
    }
    std::string attrs = openTag.substr(nameEnd, attrEnd - nameEnd);

    const bool hadStroke = TagHasAttr_(openTag, "stroke") ||
                           TagNameIs_(tagName, "line", "polyline");
    const bool hadFill = TagHasAttr_(openTag, "fill") ||
                         TagNameIs_(tagName, "circle", "ellipse", "rect", "polygon");

    if (spec.paintMode == D2DFilterPaintMode_::RecolorSourceAlpha) {
        EraseXmlAttr_(attrs, "style");
        EraseXmlAttr_(attrs, "fill");
        EraseXmlAttr_(attrs, "stroke");
        EraseXmlAttr_(attrs, "fill-opacity");
        EraseXmlAttr_(attrs, "stroke-opacity");
        if (hadFill) {
            attrs += " fill=\"" + spec.color + "\" fill-opacity=\"" +
                     std::to_string(spec.opacity) + "\"";
        } else {
            attrs += " fill=\"none\"";
        }
        if (hadStroke) {
            attrs += " stroke=\"" + spec.color + "\" stroke-opacity=\"" +
                     std::to_string(spec.opacity) + "\"";
        }
    }
    return "<" + tagName + attrs + (selfClosing ? "/>" : ">");
}

/* SVG 文本 -> 有序 D2D 绘制步骤。分段器命中红线时置 *rasterOnly 并返回空,
 * 调用方据此回落 LunaSVG 光栅。 */
std::vector<SvgDocumentRef::Step> BuildD2DSvgSteps_(const std::string& svg,
                                                    bool* rasterOnly) {
    std::vector<SvgDocumentRef::Step> out;
    svgseg::Plan plan = svgseg::BuildPlan(svg);
    if (!plan.usable) {
        if (rasterOnly) *rasterOnly = true;
        return out;
    }

    const std::vector<D2DFilterSpec_> filterSpecs = CollectD2DFilterSpecs_(svg);
    out.reserve(plan.steps.size());
    for (const auto& segStep : plan.steps) {
        SvgDocumentRef::Step step;
        step.xml = svgseg::StepXml(segStep);
        if (segStep.filtered) {
            const D2DFilterSpec_* spec =
                FindD2DFilterSpec_(filterSpecs, segStep.filterId);
            if (spec) {
                step.shadow_xml = svgseg::StepXmlWithOpenTag(
                    segStep, BuildShadowOpenTag_(segStep.openTag, *spec));
                step.dx = spec->dx;
                step.dy = spec->dy;
                step.std_deviation = spec->stdDeviation;
            }
        }
        out.push_back(std::move(step));
    }
    return out;
}

} // namespace

namespace test {

/* 返回每一步的 (shadow_xml, xml)。普通段 shadow_xml 为空。顺序即绘制顺序。 */
std::vector<std::pair<std::string, std::string>> BuildD2DSvgStepsForTest(
    const std::string& svg) {
    std::vector<std::pair<std::string, std::string>> out;
    bool rasterOnly = false;
    for (auto& step : BuildD2DSvgSteps_(svg, &rasterOnly))
        out.emplace_back(std::move(step.shadow_xml), std::move(step.xml));
    return out;
}

std::pair<std::string, bool> ConvertSimpleSvgMasksToClipPathsForD2DForTest(
    const std::string& svg) {
    auto converted = ConvertSimpleSvgMasksToClipPathsForD2D_(svg);
    return {std::move(converted.xml), converted.hasUnconvertedMaskRefs};
}

}  // namespace test

namespace {

std::vector<unsigned char> DecodeBase64_(std::string_view input) {
    unsigned char table[256];
    std::fill(table, table + 256, 255);
    for (int i = 0; i < 26; ++i) {
        table[static_cast<unsigned char>('A' + i)] = static_cast<unsigned char>(i);
        table[static_cast<unsigned char>('a' + i)] = static_cast<unsigned char>(26 + i);
    }
    for (int i = 0; i < 10; ++i)
        table[static_cast<unsigned char>('0' + i)] = static_cast<unsigned char>(52 + i);
    table[static_cast<unsigned char>('+')] = 62;
    table[static_cast<unsigned char>('/')] = 63;

    std::vector<unsigned char> out;
    out.reserve(input.size() * 3 / 4);
    int value = 0;
    int bits = -8;
    for (unsigned char ch : input) {
        if (ch == '=') break;
        if (std::isspace(ch)) continue;
        unsigned char d = table[ch];
        if (d == 255) continue;
        value = (value << 6) | d;
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<unsigned char>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

bool IsXmlNameChar_(char ch) {
    unsigned char c = static_cast<unsigned char>(ch);
    return std::isalnum(c) || ch == '_' || ch == '-' || ch == ':' || ch == '.';
}

bool IsSvgPlacementAttr_(const std::string& attrs, size_t start, size_t len) {
    return (len == 1 && attrs[start] == 'x') ||
           (len == 1 && attrs[start] == 'y') ||
           (len == 5 && attrs.compare(start, len, "width") == 0) ||
           (len == 6 && attrs.compare(start, len, "height") == 0);
}

std::string RemoveSvgPlacementAttrs_(const std::string& attrs) {
    std::string out;
    out.reserve(attrs.size());
    size_t i = 0;
    while (i < attrs.size()) {
        const size_t itemStart = i;
        while (i < attrs.size() &&
               std::isspace(static_cast<unsigned char>(attrs[i]))) {
            ++i;
        }
        if (i >= attrs.size()) {
            out.append(attrs, itemStart, attrs.size() - itemStart);
            break;
        }

        const size_t nameStart = i;
        while (i < attrs.size() && IsXmlNameChar_(attrs[i])) ++i;
        if (nameStart == i) {
            out.push_back(attrs[i++]);
            continue;
        }

        const size_t nameEnd = i;
        size_t j = i;
        while (j < attrs.size() &&
               std::isspace(static_cast<unsigned char>(attrs[j]))) {
            ++j;
        }
        if (j >= attrs.size() || attrs[j] != '=') {
            out.append(attrs, itemStart, nameEnd - itemStart);
            continue;
        }

        ++j;
        while (j < attrs.size() &&
               std::isspace(static_cast<unsigned char>(attrs[j]))) {
            ++j;
        }
        if (j < attrs.size() && (attrs[j] == '"' || attrs[j] == '\'')) {
            const char quote = attrs[j++];
            size_t q = attrs.find(quote, j);
            j = (q == std::string::npos) ? attrs.size() : q + 1;
        } else {
            while (j < attrs.size() &&
                   !std::isspace(static_cast<unsigned char>(attrs[j]))) {
                ++j;
            }
        }

        if (!IsSvgPlacementAttr_(attrs, nameStart, nameEnd - nameStart)) {
            out.append(attrs, itemStart, j - itemStart);
        }
        i = j;
    }
    return out;
}

std::string ExpandEmbeddedSvgImagesForD2D_(const std::string& svg) {
    std::string out;
    out.reserve(svg.size());
    size_t pos = 0;
    while (true) {
        size_t lt = svg.find("<image", pos);
        if (lt == std::string::npos) {
            out.append(svg, pos, std::string::npos);
            break;
        }
        out.append(svg, pos, lt - pos);
        size_t gt = svg.find('>', lt);
        if (gt == std::string::npos) {
            out.append(svg, lt, std::string::npos);
            break;
        }
        std::string tag = svg.substr(lt, gt - lt + 1);
        bool selfClose = (gt > lt && svg[gt - 1] == '/');
        size_t elemEnd = gt + 1;
        if (!selfClose) {
            size_t close = svg.find("</image>", gt + 1);
            if (close != std::string::npos)
                elemEnd = close + 8;
        }

        std::string href = ExtractXmlAttr_(tag, "href");
        if (href.empty()) href = ExtractXmlAttr_(tag, "xlink:href");
        std::string lowerHref = LowerAscii_(href.substr(0, std::min<size_t>(href.size(), 64)));
        const size_t comma = href.find(',');
        if (comma != std::string::npos &&
            lowerHref.find("data:image/svg+xml") == 0 &&
            lowerHref.find(";base64") != std::string::npos) {
            auto bytes = DecodeBase64_(std::string_view(href).substr(comma + 1));
            std::string child(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            size_t cLt = child.find("<svg");
            size_t cGt = cLt == std::string::npos ? std::string::npos : child.find('>', cLt);
            size_t cClose = child.rfind("</svg>");
            if (cLt != std::string::npos && cGt != std::string::npos && cClose != std::string::npos) {
                std::string x = ExtractXmlAttr_(tag, "x");
                std::string y = ExtractXmlAttr_(tag, "y");
                std::string w = ExtractXmlAttr_(tag, "width");
                std::string h = ExtractXmlAttr_(tag, "height");
                std::string transform = ExtractXmlAttr_(tag, "transform");
                std::string attrs = RemoveSvgPlacementAttrs_(
                    child.substr(cLt + 4, cGt - (cLt + 4)));
                std::string body = child.substr(cGt + 1, cClose - (cGt + 1));
                if (!transform.empty()) {
                    out += "<g transform=\"";
                    out += transform;
                    out += "\">";
                }
                out += "<svg";
                out += attrs;
                if (!x.empty()) out += " x=\"" + x + "\"";
                if (!y.empty()) out += " y=\"" + y + "\"";
                if (!w.empty()) out += " width=\"" + w + "\"";
                if (!h.empty()) out += " height=\"" + h + "\"";
                out += ">";
                out += body;
                out += "</svg>";
                if (!transform.empty()) out += "</g>";
            } else {
                out.append(svg, lt, elemEnd - lt);
            }
        } else {
            out.append(svg, lt, elemEnd - lt);
        }
        pos = elemEnd;
    }
    return out;
}

Microsoft::WRL::ComPtr<IStream> CreateStreamFromString_(const std::string& s) {
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, s.size());
    if (!h) return nullptr;
    void* p = GlobalLock(h);
    if (!p) {
        GlobalFree(h);
        return nullptr;
    }
    memcpy(p, s.data(), s.size());
    GlobalUnlock(h);
    Microsoft::WRL::ComPtr<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(h, TRUE, stream.GetAddressOf()))) {
        GlobalFree(h);
        return nullptr;
    }
    return stream;
}

}  // namespace

GhImgViewWidget::~GhImgViewWidget() {
    for (const auto& kv : tiles_) {
        GlobalResourceStore().Remove(kv.second.resourceKey);
    }
    if (previewResourceKey_.IsValid()) {
        GlobalResourceStore().Remove(previewResourceKey_);
    }
    if (checkerTileResourceKey_.IsValid()) {
        GlobalResourceStore().Remove(checkerTileResourceKey_);
    }
    ClearSvgRaster_();
}

// ===== 棋盘垫层 (透明像素对比背景) =====

namespace {
/* 棋盘瓦片的保留 generation — 见 gh_img_view.h 成员注释。 */
constexpr uint64_t kCheckerTileGeneration = ~0ull;
}  // namespace

void GhImgViewWidget::EnsureCheckerboardTile_(Renderer& r) {
    const int curTheme = (int)theme::CurrentMode();
    if (checkerTileResourceKey_.IsValid() && checkerTheme_ == curTheme) {
        if (!checkerTile_) {
            auto res = GlobalResourceStore().Acquire(checkerTileResourceKey_);
            if (res && res->bytes) {
                checkerTile_ = r.CreateBitmapFromPixels(
                    res->bytes->data(), res->width, res->height, res->stride);
            }
        }
        return;
    }

    if (checkerTileResourceKey_.IsValid()) {
        GlobalResourceStore().Remove(checkerTileResourceKey_);
        checkerTileResourceKey_ = {};
    }
    checkerTile_.Reset();

    /* 16×16 / 8px 方格, 配色对齐 ImageViewPlusWidget::EnsureCheckerboardTile。 */
    const int sz = 16;
    uint8_t pixels[sz * sz * 4];
    uint32_t c1, c2;
    if (theme::CurrentMode() == theme::Mode::Dark) {
        c1 = (255u << 24) | (71u << 16)  | (64u << 8)  | 64u;
        c2 = (255u << 24) | (56u << 16)  | (51u << 8)  | 51u;
    } else {
        /* build 280: 浅色格子提亮到 #FAFAFA/#E6E6E6 —— 旧的 #CCCCCC/#999999
         * 在浅色主题下压得太重, 透明区看起来比图还抢眼。 */
        c1 = (255u << 24) | (250u << 16) | (250u << 8) | 250u;
        c2 = (255u << 24) | (230u << 16) | (230u << 8) | 230u;
    }
    for (int y = 0; y < sz; ++y)
        for (int x = 0; x < sz; ++x) {
            int ix = x / 8, iy = y / 8;
            uint32_t c = ((ix + iy) % 2 == 0) ? c1 : c2;
            memcpy(pixels + (y * sz + x) * 4, &c, 4);
        }

    ResourceKey key = GlobalResourceStore().AddImage(
        ResourceKind::Bitmap, kCheckerTileGeneration, sz, sz, sz * 4,
        PixelFormat::BgraPremul, pixels, true);
    if (!key.IsValid()) return;

    checkerTileResourceKey_ = key;
    auto res = GlobalResourceStore().Acquire(checkerTileResourceKey_);
    if (res && res->bytes) {
        checkerTile_ = r.CreateBitmapFromPixels(
            res->bytes->data(), res->width, res->height, res->stride);
    }
    checkerTheme_ = curTheme;
}

void GhImgViewWidget::DrawCheckerboard_(Renderer& r, const D2D1_RECT_F& area) {
    EnsureCheckerboardTile_(r);
    if (!checkerTile_ && !checkerTileResourceKey_.IsValid()) return;
    r.FillRectWithImagePattern(checkerTileResourceKey_, checkerTile_.Get(), area);
}

void GhImgViewWidget::DrawImageShadow_(Renderer& r, const D2D1_RECT_F& visual,
                                       const D2D1_RECT_F& viewport) {
    namespace ish = imgshadow;

    /* 图片在屏上太小时常规阴影会盖过图片本身 —— 按短边降档 / 隐藏。
     * 注意用的是 visual (屏幕尺寸), 不是原图分辨率: 决定观感的是屏上多大。 */
    const ish::Tier tier = ish::ResolveTier(visual.right - visual.left,
                                            visual.bottom - visual.top);
    if (tier == ish::Tier::Hidden) return;

    const bool dark = theme::CurrentMode() == theme::Mode::Dark;
    const ish::Params p = ish::ScaleParams(ish::ParamsForTheme(dark),
                                           ish::TierScale(tier));

    const ish::RectF img{visual.left, visual.top, visual.right, visual.bottom};
    const ish::RectF vp{viewport.left, viewport.top, viewport.right, viewport.bottom};

    if (p.shadowAlpha > 0.0f && p.blur > 0.0f) {
        ish::RectF bands[4];
        const int n = ish::ShadowBands(img, vp, p.blur, p.offsetY, bands);
        if (n > 0) {
            const ish::RectF s = ish::ShadowBounds(img, p.blur, p.offsetY);
            const D2D1_RECT_F shadowRect{s.left, s.top, s.right, s.bottom};
            const D2D1_COLOR_F shadowColor = D2D1::ColorF(0.0f, 0.0f, 0.0f,
                                                          p.shadowAlpha);
            /* 每条边带各裁剪一次再画同一个模糊矩形 —— 图片矩形被挖空, 透明图
             * 的透明区不会透出暗块。D2D 的 effect 按输出区域惰性求值, 裁到窄
             * 边带后四次的总开销约等于一次全幅模糊, 不必自建离屏复用。 */
            for (int i = 0; i < n; ++i) {
                const D2D1_RECT_F clip{bands[i].left, bands[i].top,
                                       bands[i].right, bands[i].bottom};
                r.PushClip(clip);
                r.DrawBlurredRoundedRect(shadowRect, 0.0f, 0.0f, p.blur, shadowColor);
                r.PopClip();
            }
        }
    }

    /* 描边落在图片矩形外侧半个 dip 处, 1dip 笔画整条在图片之外 (D2D 描边沿路径
     * 居中), 不吃掉图片最外圈那行像素。纯投影对"白底画布上的白图"基本无效,
     * 这道描边补的就是那个洞; 深色主题下它是边界感的主力。 */
    if (p.hairlineAlpha > 0.0f) {
        const ish::RectF h = ish::HairlineRect(img);
        const D2D1_COLOR_F c = p.hairlineLight
            ? D2D1::ColorF(1.0f, 1.0f, 1.0f, p.hairlineAlpha)
            : D2D1::ColorF(0.0f, 0.0f, 0.0f, p.hairlineAlpha);
        r.DrawRect(D2D1_RECT_F{h.left, h.top, h.right, h.bottom}, c, 1.0f);
    }
}

// ===== 数据形状 =====

void GhImgViewWidget::Begin(const Info& info, Renderer& r) {
    const uint64_t oldGeneration = imageGeneration_;
    ++imageGeneration_;
    if (oldGeneration != 0) {
        if (info.keepPreview) {
            for (const auto& kv : tiles_) {
                GlobalResourceStore().SetPinnedVisible(kv.second.resourceKey, false);
            }
        }
        if (info.keepPreview) GlobalResourceStore().MarkGenerationEvictable(oldGeneration);
        else                  GlobalResourceStore().PurgeGeneration(oldGeneration);
    }

    tiles_.clear();
    /* L168: keepPreview 时不清 preview 兜底层 — preview 在 OnDraw 永远先画兜底,
     * tile 逐级盖上, 清晰度物理单调, 消除切金字塔时的闪烁。 */
    if (!info.keepPreview) {
        previewResourceKey_ = {};
        previewW_ = previewH_ = 0;
    }
    /* 切瓦块 source 前清 SVG 状态. svgSlot_ 置空 = 放弃任何在飞后台渲染 (其线程
     * 写到的是旧 slot, 自然丢弃)。 */
    svgDoc_.reset();
    svgD2DSteps_.clear();
    svgD2DStepDocs_.clear();
    svgD2DStepShadowDocs_.clear();
    svgThumbXml_.clear();
    svgRasterMain_ = false;
    ClearSvgRaster_();
    svgW_ = svgH_ = 0;
    svgSlot_.reset();

    /* Cache DPI scale for PickAutoLevel — 算物理像素密度需要 zoom 乘上
     * dpi_scale, 不然 DPI>100% 时按 DIP 算阈值会偏向上采样 level → fit
     * 大图模糊. 用户实测原话 "默认糊, 放大缩小后清晰" 就是这条 — wheel
     * cycle 时 widget 切到高分辨率 mip, cached tile 留在 tiles_ 被 OnDraw
     * 覆盖渲染, 等效用了下采样 level. 修了 PickAutoLevel 后默认就走下
     * 采样 level, 不需要 wheel 操作. */
    if (auto* ctx = r.RT()) {
        float dx = 96.0f, dy = 96.0f;
        ctx->GetDpi(&dx, &dy);
        if (dx > 1e-3f) dpi_scale_ = dx / 96.0f;
    }

    info_ = info;
    if (info_.tileSize == 0) info_.tileSize = 256;
    if (info_.levels   == 0) info_.levels   = 1;
    activeLevel_ = autoLevel_ ? PickAutoLevel() : 0;

    // 默认 fit 到视口（如果已 layout）. 新图片默认 rotation=0 — 看图器
    // 翻图时不继承上一张的旋转, 跟 Win11 照片 / IrfanView 行为一致.
    rotation_ = 0;
    if (rect.right > rect.left && rect.bottom > rect.top) {
        Fit();
    } else {
        zoom_ = 1.0f; panX_ = 0; panY_ = 0;
    }
    NotifyViewport();
    InvalidateAllWindows();
}

void GhImgViewWidget::SetPreview(const void* bgra, uint32_t pw, uint32_t ph,
                                  uint32_t stride, Renderer& r) {
    (void)r;
    if (!bgra || pw == 0 || ph == 0) return;
    if (stride == 0) stride = pw * 4;
    if (previewResourceKey_.IsValid() &&
        previewResourceKey_.image_generation != imageGeneration_) {
        GlobalResourceStore().PurgeGeneration(previewResourceKey_.image_generation);
        previewResourceKey_ = {};
    }
    if (previewResourceKey_.IsValid()) {
        /* Do not UpdateImage in place: render-thread/GPU caches are keyed by
         * ResourceKey, so reusing the key can leave animated previews visually
         * stuck on the previous bitmap. Remove the old preview first, then add
         * a fresh key; ResourceStore still retains only one preview frame. */
        GlobalResourceStore().Remove(previewResourceKey_);
        previewResourceKey_ = {};
    }

    ResourceKey key = GlobalResourceStore().AddImage(
        ResourceKind::Preview, imageGeneration_, (int)pw, (int)ph, (int)stride,
        PixelFormat::BgraStraight, bgra, true);
    if (!key.IsValid()) return;
    previewResourceKey_ = key;
    previewW_ = pw;
    previewH_ = ph;
    InvalidateAllWindows();
}

void GhImgViewWidget::SetTile(uint32_t level, uint32_t tx, uint32_t ty,
                               const void* bgra, uint32_t tw, uint32_t th,
                               uint32_t stride, Renderer& r) {
    (void)r;
    if (!bgra || tw == 0 || th == 0) return;
    if (level >= info_.levels) return;
    if (stride == 0) stride = tw * 4;

    ResourceKey resourceKey = GlobalResourceStore().AddImage(
        ResourceKind::Tile, imageGeneration_, (int)tw, (int)th, (int)stride,
        PixelFormat::BgraStraight, bgra, true);
    if (!resourceKey.IsValid()) return;

    // L48: 不再做 LRU evict — viewport trim 在 NotifyViewport 内做, 跟 viewport
    // 边界严格绑定. SetTile 只负责装新 tile, 不主动 evict.
    Tile t;
    t.resourceKey = resourceKey;
    t.w   = tw;
    t.h   = th;
    TileKey key{level, tx, ty};
    auto old = tiles_.find(key);
    if (old != tiles_.end()) GlobalResourceStore().Remove(old->second.resourceKey);
    tiles_[key] = std::move(t);
    if (!tileBatch_) InvalidateAllWindows();   // L115: batch 内不刷, EndTileBatch 一次刷
}

void GhImgViewWidget::EndTileBatch() {
    tileBatch_ = false;
    InvalidateAllWindows();
}

void GhImgViewWidget::EndViewUpdate() {
    if (viewUpdateDepth_ <= 0) return;
    --viewUpdateDepth_;
    if (viewUpdateDepth_ != 0) return;

    const bool notify = pendingViewportNotify_;
    const bool invalidate = pendingInvalidate_;
    {
        TraceEvent("gh_img_view", "end_view_update",
                   {TraceBool("notify", notify),
                    TraceBool("invalidate", invalidate),
                    TraceF64("zoom", zoom_),
                    TraceF64("pan_x", panX_),
                    TraceF64("pan_y", panY_)});
    }
    pendingViewportNotify_ = false;
    pendingInvalidate_ = false;
    if (notify) NotifyViewport();
    if (invalidate) InvalidateAllWindows();
}

void GhImgViewWidget::RequestViewportCommit() {
    if (viewUpdateDepth_ > 0) {
        pendingViewportNotify_ = true;
        pendingInvalidate_ = true;
        TraceEvent("gh_img_view", "request_viewport_deferred");
        return;
    }
    TraceEvent("gh_img_view", "request_viewport_immediate");
    NotifyViewport();
    InvalidateAllWindows();
}

void GhImgViewWidget::TrimToViewport_(uint32_t active_level,
                                       uint32_t visible_tx0, uint32_t visible_tx1,
                                       uint32_t visible_ty0, uint32_t visible_ty1) {
    // L48: 清掉 tiles_ 中 (1) 非 active_level 的全部 tile + (2) active_level
    // 内但不在 viewport [tx0,tx1) × [ty0,ty1) 范围的 tile. 每个被清的 tile
    // fire onTileEvicted, caller 同步自己端 pushed_tiles_ erase.
    //
    // 内存稳态 ≈ viewport 内 tile 数 × 256KB, 不随 zoom 历史累积. zoom out /
    // pan 远 → trim → caller pushed 同步清 → 下次 viewport callback 重新 enqueue
    // worker decode (单 tile 0.38ms × 4 worker 并发 ~10ms 不可感知).
    for (auto it = tiles_.begin(); it != tiles_.end(); ) {
        const TileKey& k = it->first;
        const bool in_viewport = (k.level == active_level &&
                                    k.tx >= visible_tx0 && k.tx < visible_tx1 &&
                                    k.ty >= visible_ty0 && k.ty < visible_ty1);
        // L115: 保留上一个 active level 的 tile — OnDraw 多级 fallback 用它覆盖新级
        // 未到达的 tile, 切级清晰→更清晰无波浪 (旧级 tile 数约新级 1/4, 内存可忽略)。
        const bool keep_prev = (k.level == prevActiveLevel_);
        if (in_viewport || keep_prev) { ++it; continue; }
        if (onTileEvicted) {
            onTileEvicted(k.level, k.tx, k.ty);
        }
        GlobalResourceStore().Remove(it->second.resourceKey);
        it = tiles_.erase(it);
    }
}

void GhImgViewWidget::ClearLevel(uint32_t level) {
    for (auto it = tiles_.begin(); it != tiles_.end(); ) {
        if (it->first.level == level) {
            GlobalResourceStore().Remove(it->second.resourceKey);
            it = tiles_.erase(it);
        } else {
            ++it;
        }
    }
    InvalidateAllWindows();
}

void GhImgViewWidget::Clear() {
    if (imageGeneration_ != 0) GlobalResourceStore().PurgeGeneration(imageGeneration_);
    if (previewResourceKey_.IsValid() &&
        previewResourceKey_.image_generation != imageGeneration_) {
        GlobalResourceStore().PurgeGeneration(previewResourceKey_.image_generation);
    }
    ++imageGeneration_;
    tiles_.clear();
    previewResourceKey_ = {};
    previewW_ = previewH_ = 0;
    svgDoc_.reset();
    svgD2DSteps_.clear();
    svgD2DStepDocs_.clear();
    svgD2DStepShadowDocs_.clear();
    svgThumbXml_.clear();
    svgRasterMain_ = false;
    ClearSvgRaster_();
    svgW_ = svgH_ = 0;
    svgSlot_.reset();
    info_ = Info{};
    activeLevel_ = 0;
    zoom_ = 1.0f; panX_ = 0; panY_ = 0;
    rotation_ = 0;
    InvalidateAllWindows();
}

// ---- SVG 矢量源 (Build 70+ L20) ----

/* L195: LunaSVG 无内置字体、也不解析 SVG 内嵌 @font-face → 不注册任何字体则
 * <text> 全部渲染不出 (getFontFace 落空 → null face → 无字形)。给它注册一个
 * 系统 CJK fallback 字体, 注册成 empty family "" (LunaSVG 的通配回退: 任何没注册
 * 的 font-family 都落到它)。lazy 一次 — FontFaceCache 是进程级全局静态。 */
static void EnsureLunaSvgFallbackFont_() {
    static bool done = false;
    if (done) return;
    done = true;
    wchar_t winDir[MAX_PATH] = {};
    UINT n = GetWindowsDirectoryW(winDir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    std::wstring fonts = std::wstring(winDir) + L"\\Fonts\\";
    /* 找第一个存在的 CJK 字体 (中英全覆盖): 微软雅黑(Win7+) → 等线(Win10+) → 黑体 → 宋体。 */
    const wchar_t* cands[] = { L"msyh.ttc", L"Deng.ttf", L"simhei.ttf", L"simsun.ttc" };
    std::wstring wpath;
    for (const wchar_t* fn : cands) {
        std::wstring p = fonts + fn;
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) { wpath = p; break; }
    }
    if (wpath.empty()) return;
    /* 读一次到 leaked buffer (进程级 fallback 字体, 永不释放), 多 family 共享同一份数据。 */
    std::ifstream ff(wpath.c_str(), std::ios::binary);
    if (!ff) return;
    auto* buf = new std::string((std::istreambuf_iterator<char>(ff)),
                                 std::istreambuf_iterator<char>());
    if (buf->empty()) return;
    /* 覆盖 lunasvg 的 generic fallback 目标 (lunasvg graphics.cpp generic_fallbacks:
     * sans-serif→Arial / serif→Times New Roman / monospace→Courier New) + empty family。
     * 这些默认是纯拉丁字体, SVG 文字的 font-family 列表 fallback 到它们时, 中文字形落空
     * 渲染成豆腐 (lunasvg 选定单一字体、无逐字形回退)。用 CJK 字体覆盖它们 (FontFaceCache
     * 把新条目插链表头, 即盖过 load_sys 加载的系统同名字体) → 中英文都正常渲染。
     * bold/italic 4 档全注册, 覆盖任意 weight 的文字。 */
    const char* fams[] = { "Arial", "Times New Roman", "Courier New", "" };
    for (const char* fam : fams)
        for (int bi = 0; bi < 4; ++bi)
            lunasvg_add_font_face_from_data(fam, (bi & 1) != 0, (bi & 2) != 0,
                                            buf->data(), buf->size(), nullptr, nullptr);
}

bool GhImgViewWidget::SetSvgFromFile(const std::wstring& path, Renderer& r) {
    const bool hasImmediateD2D = r.RT5() != nullptr;

    /* D2D SvgDocument remains the preferred interactive view, but build 223+
     * can release the UI thread render target after render-thread present takes
     * over. Loading must therefore validate/store XML without requiring a UI
     * RT; the render thread will recreate the SvgDocument while replaying the
     * DisplayList. */
    std::string xml;
    std::vector<SvgDocumentRef::Step> d2dSteps;
    std::string thumbXml;
    bool needsRasterMain = false;
    Microsoft::WRL::ComPtr<ID2D1SvgDocument> probeDoc;   /* 只用来探 natural size */
    auto tryPrepareSvg = [&](std::string candidateXml) -> bool {
        if (candidateXml.empty()) return false;
        EnsureLunaSvgFallbackFont_();
        candidateXml = ReplaceCssLightDark_(std::move(candidateXml));
        candidateXml = NormalizeSvgPaintColorsForD2D(std::move(candidateXml));
        candidateXml = ExpandSvgSwitchImageFallbacksForD2D_(candidateXml);
        candidateXml = ExpandEmbeddedSvgImagesForD2D_(candidateXml);
        candidateXml = r.SvgInlineTextAsPaths(candidateXml);
        auto maskConversion = ConvertSimpleSvgMasksToClipPathsForD2D_(candidateXml);
        std::string candidateD2DSource = std::move(maskConversion.xml);

        /* 带 filter 的 SVG 按文档顺序切段 (顺序即 z 序); 分段器命中红线时
         * rasterOnly 置位, 主视图回落 LunaSVG —— 不硬切, 见 svg_d2d_segments.h。 */
        bool rasterOnly = false;
        std::vector<SvgDocumentRef::Step> candidateSteps;
        if (HasSvgFilter_(candidateXml)) {
            candidateSteps = BuildD2DSvgSteps_(candidateD2DSource, &rasterOnly);
        }
        if (candidateSteps.empty()) {
            candidateSteps.resize(1);
            candidateSteps[0].xml = candidateD2DSource;
        }
        std::string candidateThumbXml = HasSvgFilter_(candidateXml)
            ? BuildSvgThumbnailXml_(candidateXml)
            : std::string{};
        /* 第一段能被 D2D 解析 = 这份 SVG 走得通 D2D 路径。有 UI RT 时顺便留下
         * 这份 document 供下面探 natural size (viewBox / width / height), 不入缓存。 */
        Microsoft::WRL::ComPtr<ID2D1SvgDocument> candidateProbe;
        if (!r.CreateSvgDocumentFromXml(candidateSteps[0].xml, 1024.0f, 1024.0f,
                                        hasImmediateD2D ? &candidateProbe : nullptr)) {
            return false;
        }
        xml = std::move(candidateXml);
        d2dSteps = std::move(candidateSteps);
        probeDoc = std::move(candidateProbe);
        thumbXml = std::move(candidateThumbXml);
        needsRasterMain = rasterOnly || NeedsLunaSvgMainRenderer_(xml) ||
                          maskConversion.hasUnconvertedMaskRefs;
        return true;
    };

    auto inlined = LoadSvgWithInlinedStyles(path);
    bool created = tryPrepareSvg(std::move(inlined));
    if (!created) {
        std::ifstream f(path.c_str(), std::ios::binary);
        if (f) {
            std::string raw((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
            created = tryPrepareSvg(std::move(raw));
        }
    }
    if (!created) return false;

    uint32_t natW = 1024, natH = 1024;
    const std::string rootTag = ExtractSvgRootTag_(xml);
    float viewBoxW = 0.0f, viewBoxH = 0.0f;
    const bool haveViewBox = ParseSvgViewBoxSize_(rootTag, viewBoxW, viewBoxH);
    const float cssW = ParseSvgCssPxLength_(ExtractXmlAttr_(rootTag, "width"));
    const float cssH = ParseSvgCssPxLength_(ExtractXmlAttr_(rootTag, "height"));
    if (cssW > 0.0f && cssH > 0.0f) {
        natW = static_cast<uint32_t>(cssW + 0.5f);
        natH = static_cast<uint32_t>(cssH + 0.5f);
    } else if (cssW > 0.0f && haveViewBox) {
        natW = static_cast<uint32_t>(cssW + 0.5f);
        natH = static_cast<uint32_t>(cssW * viewBoxH / viewBoxW + 0.5f);
    } else if (cssH > 0.0f && haveViewBox) {
        natW = static_cast<uint32_t>(cssH * viewBoxW / viewBoxH + 0.5f);
        natH = static_cast<uint32_t>(cssH + 0.5f);
    } else if (haveViewBox) {
        natW = static_cast<uint32_t>(viewBoxW + 0.5f);
        natH = static_cast<uint32_t>(viewBoxH + 0.5f);
    }
    bool sizedExplicit = haveViewBox || cssW > 0.0f || cssH > 0.0f;
    Microsoft::WRL::ComPtr<ID2D1SvgElement> root;
    if (probeDoc) probeDoc->GetRoot(root.GetAddressOf());
    if (root && !sizedExplicit) {
        D2D1_SVG_VIEWBOX vb{};
        if (SUCCEEDED(root->GetAttributeValue(L"viewBox",
                                              D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX,
                                              &vb, sizeof(vb))) &&
            vb.width > 0.0f && vb.height > 0.0f) {
            natW = static_cast<uint32_t>(vb.width + 0.5f);
            natH = static_cast<uint32_t>(vb.height + 0.5f);
            sizedExplicit = true;
        } else {
            D2D1_SVG_LENGTH lw{}, lh{};
            if (SUCCEEDED(root->GetAttributeValue(L"width",
                                                  D2D1_SVG_ATTRIBUTE_POD_TYPE_LENGTH,
                                                  &lw, sizeof(lw))) &&
                lw.value > 0.0f) {
                natW = static_cast<uint32_t>(lw.value + 0.5f);
                sizedExplicit = true;
            }
            if (SUCCEEDED(root->GetAttributeValue(L"height",
                                                  D2D1_SVG_ATTRIBUTE_POD_TYPE_LENGTH,
                                                  &lh, sizeof(lh))) &&
                lh.value > 0.0f) {
                natH = static_cast<uint32_t>(lh.value + 0.5f);
                sizedExplicit = true;
            }
        }

    }
    auto doc = lunasvg::Document::loadFromData(xml);
    /* 根节点无 viewBox / width / height (常见于 Scratch 积木等工具导出) 时,
     * 1024×1024 兜底会与实际绘制范围脱节 — 内容溢出声明矩形, natural size
     * 相关的一切 (fit / zoom / minimap / 棋盘垫层) 都按错误边界算。用 LunaSVG
     * 的内容包围盒替代: 视口取 (0,0)..(bbox 右下角), 绘制原点不动。 */
    if (!sizedExplicit && doc) {
        const lunasvg::Box box = doc->boundingBox();
        const float right  = box.x + box.w;
        const float bottom = box.y + box.h;
        if (right > 1.0f && bottom > 1.0f) {
            natW = static_cast<uint32_t>(std::ceil(right));
            natH = static_cast<uint32_t>(std::ceil(bottom));
        }
    }
    if (natW == 0) natW = 1024;
    if (natH == 0) natH = 1024;
    /* 早前这里还会把 natW/natH 写回 probeDoc 的根节点 width/height。那份 document
     * 现在只用于探尺寸、探完就丢, 而且真正决定布局的是绘制时的 SetViewportSize
     * (display list 回放路径从头到尾就没做过这个写回)。故不再写。 */

    /* 进入 SVG 模式 — 清瓦块 state, 把 natural size 写进 info_ 让 Fit / zoom
     * 几何全部复用瓦块路径的代码. levels = 1 (SVG 不分级). */
    tiles_.clear();
    if (previewResourceKey_.IsValid()) {
        GlobalResourceStore().Remove(previewResourceKey_);
        previewResourceKey_ = {};
    }
    previewW_ = previewH_ = 0;
    ClearSvgRaster_();               /* 新文件 — 作废旧栅格缓存 */
    svgSlot_ = std::make_shared<SvgRenderSlot>();   /* 新文件 → 新结果槽 */
    svgRasterMain_ = needsRasterMain;
    svgThumbXml_ = std::move(thumbXml);
    svgD2DStepDocs_.clear();
    svgD2DStepShadowDocs_.clear();
    if (svgRasterMain_) {
        svgD2DSteps_.clear();
    } else {
        svgD2DSteps_ = std::move(d2dSteps);
    }
    svgDoc_  = std::move(doc);       /* unique_ptr → shared_ptr */
    svgW_    = natW;
    svgH_    = natH;
    info_ = Info{};
    info_.fullWidth   = natW;
    info_.fullHeight  = natH;
    info_.tileSize    = 256;
    info_.levels      = 1;
    info_.pixelFormat = 0;
    activeLevel_ = 0;
    rotation_ = 0;
    if (rect.right > rect.left && rect.bottom > rect.top) {
        Fit();
    } else {
        zoom_ = 1.0f; panX_ = 0; panY_ = 0;
    }
    NotifyViewport();
    InvalidateAllWindows();
    return true;
}

int GhImgViewWidget::RenderSvgToBgra(uint32_t target_w, uint32_t target_h,
                                       uint8_t* out_bgra,
                                       uint32_t* out_w, uint32_t* out_h,
                                       Renderer& r) {
    if (!svgDoc_ || svgW_ == 0 || svgH_ == 0) return -1;
    if (!out_bgra || target_w == 0 || target_h == 0) return -2;
    /* fit 保 aspect, 长边贴到 target 边 */
    double src_aspect = (double)svgW_ / (double)svgH_;
    double dst_aspect = (double)target_w / (double)target_h;
    uint32_t out_w_v, out_h_v;
    if (src_aspect > dst_aspect) {
        out_w_v = target_w;
        out_h_v = (uint32_t)((double)target_w / src_aspect + 0.5);
    } else {
        out_h_v = target_h;
        out_w_v = (uint32_t)((double)target_h * src_aspect + 0.5);
    }
    if (out_w_v == 0) out_w_v = 1;
    if (out_h_v == 0) out_h_v = 1;

    auto copyBitmap = [&](lunasvg::Bitmap& bmp) -> bool {
        if (bmp.isNull() || !bmp.data()) return false;
        const uint8_t* src = bmp.data();
        const int stride = bmp.stride();
        const uint32_t bw = static_cast<uint32_t>(bmp.width());
        const uint32_t bh = static_cast<uint32_t>(bmp.height());
        const uint32_t cw = (bw < out_w_v) ? bw : out_w_v;
        const uint32_t ch = (bh < out_h_v) ? bh : out_h_v;
        if (cw == 0 || ch == 0 || stride <= 0) return false;
        memset(out_bgra, 0, static_cast<size_t>(out_w_v) * out_h_v * 4u);
        for (uint32_t y = 0; y < ch; ++y) {
            memcpy(out_bgra + static_cast<size_t>(y) * out_w_v * 4u,
                   src + static_cast<size_t>(y) * stride,
                   static_cast<size_t>(cw) * 4u);
        }
        return true;
    };

    if (!svgRasterMain_ && !svgD2DSteps_.empty()) {
        if (r.RenderSvgDocumentToBgra(svgD2DSteps_, static_cast<float>(svgW_),
                                      static_cast<float>(svgH_),
                                      out_w_v, out_h_v, out_bgra)) {
            if (out_w) *out_w = out_w_v;
            if (out_h) *out_h = out_h_v;
            return 0;
        }
    }

    if (!svgThumbXml_.empty()) {
        std::unique_ptr<lunasvg::Document> stripped =
            lunasvg::Document::loadFromData(svgThumbXml_);
        if (stripped) {
            lunasvg::Bitmap bmp = stripped->renderToBitmap(
                static_cast<int>(out_w_v), static_cast<int>(out_h_v),
                0x00000000u);
            if (copyBitmap(bmp)) {
                if (out_w) *out_w = out_w_v;
                if (out_h) *out_h = out_h_v;
                return 0;
            }
        }
    }

    lunasvg::Bitmap bmp = svgDoc_->renderToBitmap(
        static_cast<int>(out_w_v), static_cast<int>(out_h_v), 0x00000000u);
    if (!copyBitmap(bmp)) return -5;

    if (out_w) *out_w = out_w_v;
    if (out_h) *out_h = out_h_v;
    return 0;
}

// L196: SVG 主视图按可见源矩形重栅到 svgRaster_。整图单 bitmap 有固定 cap,
// 高倍放大文字时必然上采样发糊; 视口重栅让输出尺寸只跟当前窗口/zoom 档有关。
void GhImgViewWidget::EnsureSvgRaster(float srcL, float srcT, float srcR, float srcB,
                                      uint32_t renW, uint32_t renH, Renderer& r) {
    if (!svgDoc_ || svgW_ == 0 || svgH_ == 0) return;
    SvgRenderSlot::Request req{srcL, srcT, srcR, srcB, renW, renH};
    std::vector<uint8_t> buf;
    uint32_t bw = 0, bh = 0;
    if (!SvgRenderRequestToBgra(svgDoc_, req, buf, bw, bh)) return;

    ResourceKey resourceKey = GlobalResourceStore().AddImage(
        ResourceKind::SvgRaster, imageGeneration_, static_cast<int>(bw),
        static_cast<int>(bh), static_cast<int>(bw * 4),
        PixelFormat::BgraPremul, buf.data(), true);
    auto res = GlobalResourceStore().Acquire(resourceKey);
    if (!res || !res->bytes) {
        if (resourceKey.IsValid()) GlobalResourceStore().Remove(resourceKey);
        return;
    }

    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    auto* rt = r.RT();
    if (rt) {
        float dpiX = 96.0f, dpiY = 96.0f;
        rt->GetDpi(&dpiX, &dpiY);
        const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            dpiX, dpiY);
        rt->CreateBitmap(D2D1::SizeU(bw, bh), res->bytes->data(),
                         static_cast<UINT32>(res->stride), &props, &bitmap);
    }
    ClearSvgRaster_();
    svgRaster_  = std::move(bitmap);
    svgRasterResourceKey_ = resourceKey;
    svgRasterW_ = bw;
    svgRasterH_ = bh;
    svgRasterSrcL_ = srcL;
    svgRasterSrcT_ = srcT;
    svgRasterSrcR_ = srcR;
    svgRasterSrcB_ = srcB;
}

// 起 (或更新) 一个后台栅格化请求。非阻塞: 后台线程渲 LunaSVG → BGRA 存进 slot
// → InvalidateRect 唤醒 UI 线程 (下次 OnDraw 由 ConsumeSvgRasterResult_ 换上)。
// 线程只碰 slot + 自己持有的 doc/hwnd (shared_ptr 保活), 不碰 this → 析构/换图安全。
// 渲染期间又改缩放 (want 变) → 渲完再渲最新档 (合并到最后一次), inFlight 保证单线程。
void GhImgViewWidget::RequestSvgRasterAsync_(float srcL, float srcT,
                                             float srcR, float srcB,
                                             uint32_t renW, uint32_t renH,
                                             unsigned long uiThreadId) {
    auto slot = svgSlot_;
    auto doc  = svgDoc_;
    SvgRenderSlot::Request req{srcL, srcT, srcR, srcB, renW, renH};
    if (!slot || !doc || !SvgReqValid(req)) return;
    {
        std::lock_guard<std::mutex> lk(slot->mu);
        slot->wantReq = req;
    }
    bool expected = false;
    if (!slot->inFlight.compare_exchange_strong(expected, true))
        return;   /* 已有后台线程在渲, 它渲完会看 want 再渲最新 */

    std::thread([slot, doc, uiThreadId]() {
        for (;;) {
            SvgRenderSlot::Request req;
            {
                std::lock_guard<std::mutex> lk(slot->mu);
                req = slot->wantReq;
            }
            std::vector<uint8_t> buf;
            uint32_t bw = 0, bh = 0;
            const bool rendered = SvgRenderRequestToBgra(doc, req, buf, bw, bh);
            bool publish = false;
            bool done = false;
            {
                std::lock_guard<std::mutex> lk(slot->mu);
                done = SvgReqSame(slot->wantReq, req);
                if (rendered && done) {
                    slot->bgra.swap(buf);
                    slot->readyReq = req;
                    slot->w = bw; slot->h = bh; slot->ready = true;
                    publish = true;
                }
                if (done) slot->inFlight.store(false);
            }
            if (publish) {
                /* 跨线程唤醒 UI 重绘 (InvalidateRect 线程安全; 同
                 * InvalidateAllWindows 的按线程枚举窗口模式)。 */
                EnumThreadWindows(uiThreadId, [](HWND hwnd, LPARAM) -> BOOL {
                    wchar_t cls[64];
                    GetClassNameW(hwnd, cls, 64);
                    if (wcscmp(cls, L"UiCore_Window") == 0)
                        InvalidateRect(hwnd, nullptr, FALSE);
                    return TRUE;
                }, 0);
            }
            /* 期间又改了缩放 → 渲最新; 否则收工。 */
            if (done) {
                break;
            }
        }
    }).detach();
}

// UI 线程: 后台结果就绪则写入 CPU SvgRaster resource, 并在有 RT 时建 D2D 缓存。
void GhImgViewWidget::ConsumeSvgRasterResult_(Renderer& r) {
    if (!svgSlot_) return;
    std::vector<uint8_t> buf;
    uint32_t bw = 0, bh = 0;
    SvgRenderSlot::Request req;
    {
        std::lock_guard<std::mutex> lk(svgSlot_->mu);
        if (!svgSlot_->ready) return;
        buf.swap(svgSlot_->bgra);
        req = svgSlot_->readyReq;
        bw = svgSlot_->w; bh = svgSlot_->h;
        svgSlot_->ready = false;
    }
    if (!SvgReqValid(req) || bw == 0 || bh == 0 ||
        buf.size() < static_cast<size_t>(bw) * bh * 4) return;

    ResourceKey resourceKey = GlobalResourceStore().AddImage(
        ResourceKind::SvgRaster, imageGeneration_, static_cast<int>(bw),
        static_cast<int>(bh), static_cast<int>(bw * 4),
        PixelFormat::BgraPremul, buf.data(), true);
    auto res = GlobalResourceStore().Acquire(resourceKey);
    if (!res || !res->bytes) {
        if (resourceKey.IsValid()) GlobalResourceStore().Remove(resourceKey);
        return;
    }

    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    auto* rt = r.RT();
    if (rt) {
        float dpiX = 96.0f, dpiY = 96.0f;
        rt->GetDpi(&dpiX, &dpiY);
        const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            dpiX, dpiY);
        rt->CreateBitmap(D2D1::SizeU(bw, bh), res->bytes->data(),
                         static_cast<UINT32>(res->stride), &props, &bitmap);
    }
    ClearSvgRaster_();
    svgRaster_  = std::move(bitmap);
    svgRasterResourceKey_ = resourceKey;
    svgRasterW_ = bw;
    svgRasterH_ = bh;
    svgRasterSrcL_ = req.srcL;
    svgRasterSrcT_ = req.srcT;
    svgRasterSrcR_ = req.srcR;
    svgRasterSrcB_ = req.srcB;
}

void GhImgViewWidget::ClearSvgRaster_() {
    if (svgRasterResourceKey_.IsValid()) {
        GlobalResourceStore().Remove(svgRasterResourceKey_);
        svgRasterResourceKey_ = {};
    }
    svgRaster_.Reset();
    svgRasterW_ = svgRasterH_ = 0;
    svgRasterSrcL_ = svgRasterSrcT_ = svgRasterSrcR_ = svgRasterSrcB_ = 0.0f;
}

void GhImgViewWidget::SetActiveLevel(uint32_t level) {
    if (level >= info_.levels) return;
    SwitchLevel(level);
    autoLevel_ = false;
    RequestViewportCommit();
}

// ===== 视口 =====

void GhImgViewWidget::SetZoom(float z) {
    z = std::clamp(z, minZoom_, maxZoom_);
    if (zoom_ == z) return;
    {
        TraceEvent("gh_img_view", "set_zoom",
                   {TraceF64("old", zoom_),
                    TraceF64("new", z),
                    TraceI64("depth", viewUpdateDepth_)});
    }
    zoom_ = z;
    if (autoLevel_) SwitchLevel(PickAutoLevel());
    ConstrainPan();
    InvalidateSvgRenderSlot_(svgSlot_);
    RequestViewportCommit();
}

/* L47 follow-up: 之前 SetPan 是 .h inline 单纯赋值 panX_/Y_, 没 fire
 * NotifyViewport — 跟 SetZoom 不对称. caller 调 ui_gh_img_view_set_pan
 * (典型场景 minimap click 切换显示区域) 后 viewport callback 不 fire,
 * caller 不知道要 push_visible_tiles_ 给新可见区, OnDraw fallback 显
 * preview / 老 level → 用户看到模糊, 要点击画布触发 OnMouseMove 才
 * fire callback 才清晰. 修: 跟 SetZoom 同款 fire NotifyViewport +
 * InvalidateAllWindows. ConstrainPan 也跟 SetZoom 路径对齐.
 *
 * 早 return 优化: (x, y) 等于当前 pan 时跳过 — caller 端可能反复调
 * (例如 minimap drag 时连续 set_pan), 避免无效 callback. */
void GhImgViewWidget::SetPan(float x, float y) {
    if (panX_ == x && panY_ == y) return;
    {
        TraceEvent("gh_img_view", "set_pan",
                   {TraceF64("old_x", panX_),
                    TraceF64("old_y", panY_),
                    TraceF64("new_x", x),
                    TraceF64("new_y", y),
                    TraceI64("depth", viewUpdateDepth_)});
    }
    panX_ = x;
    panY_ = y;
    ConstrainPan();
    InvalidateSvgRenderSlot_(svgSlot_);
    RequestViewportCommit();
}

void GhImgViewWidget::SetZoomAround(float z, float anchorX, float anchorY) {
    z = std::clamp(z, minZoom_, maxZoom_);
    if (zoom_ == z) return;

    // 1) 当前 state 下 anchor 屏幕点对应的图像坐标 (顶级, rotation-aware)
    float ix = 0, iy = 0;
    ScreenToImage(anchorX, anchorY, ix, iy);

    // 2) 切新 zoom
    zoom_ = z;
    if (autoLevel_) SwitchLevel(PickAutoLevel());

    // 3) 反算 pan 使 (ix, iy) 仍落在 (anchorX, anchorY)
    //
    // Forward image (ix, iy) → screen (sx, sy):
    //   fx = (ix - fullW/2) * zoom_new
    //   fy = (iy - fullH/2) * zoom_new
    //   (rx, ry) = RotateCW(rotation_, fx, fy)
    //   sx = widget_cx + panX + rx
    //   sy = widget_cy + panY + ry
    //
    // 要求 sx = anchorX, sy = anchorY → panX/Y = anchor - widget_center - (rx/ry).
    float fullW = (float)info_.fullWidth;
    float fullH = (float)info_.fullHeight;
    float fx = (ix - fullW * 0.5f) * zoom_;
    float fy = (iy - fullH * 0.5f) * zoom_;
    float rx, ry;
    RotateCW(rotation_, fx, fy, rx, ry);
    float cx = (rect.left + rect.right) * 0.5f;
    float cy = (rect.top  + rect.bottom) * 0.5f;
    panX_ = anchorX - cx - rx;
    panY_ = anchorY - cy - ry;

    ConstrainPan();
    InvalidateSvgRenderSlot_(svgSlot_);
    RequestViewportCommit();
}

void GhImgViewWidget::Fit() {
    if (info_.fullWidth == 0 || info_.fullHeight == 0) return;
    float w = std::max(1.0f, rect.right - rect.left);
    float h = std::max(1.0f, rect.bottom - rect.top);
    /* 留内边距: 不留的话图片长边正好贴满视口, 图片阴影整条画在视口外看不见
     * (下游 GuoheView 报的"拖入图片后顶边阴影超出看不到")。默认 0, 宿主按需开。 */
    const float pad = imgshadow::ClampFitPadding(fitPadding_, w, h);
    w = std::max(1.0f, w - pad * 2.0f);
    h = std::max(1.0f, h - pad * 2.0f);
    // rotation-aware: 90/270 时图像视觉宽高互换, fit 用 effective 尺寸.
    float effW = (float)EffectiveImageWidth();
    float effH = (float)EffectiveImageHeight();
    float sx = w / effW;
    float sy = h / effH;
    zoom_ = std::min(sx, sy);
    zoom_ = std::clamp(zoom_, minZoom_, maxZoom_);
    panX_ = 0; panY_ = 0;
    if (autoLevel_) SwitchLevel(PickAutoLevel());
    InvalidateSvgRenderSlot_(svgSlot_);
    RequestViewportCommit();
}

void GhImgViewWidget::SetRotation(int angle) {
    int n = NormalizeAngle(angle);
    if (n == rotation_) return;
    rotation_ = n;
    // pan / zoom 都保留 (用户预期: 旋转不丢视图状态). pan 在屏幕空间, 跟
    // rotation 解耦, 不需要旋转 pan 向量 — 详见类注释.
    InvalidateSvgRenderSlot_(svgSlot_);
    RequestViewportCommit();
}

void GhImgViewWidget::Reset() {
    zoom_ = 1.0f;
    panX_ = 0; panY_ = 0;
    if (autoLevel_) SwitchLevel(PickAutoLevel());
    InvalidateSvgRenderSlot_(svgSlot_);
    RequestViewportCommit();
}

// ===== Widget 虚函数 =====

void GhImgViewWidget::OnDraw(Renderer& r) {
    {
        TraceEvent("gh_img_view", "on_draw",
                   {TraceF64("l", rect.left),
                    TraceF64("t", rect.top),
                    TraceF64("r", rect.right),
                    TraceF64("b", rect.bottom),
                    TraceF64("zoom", zoom_),
                    TraceF64("pan_x", panX_),
                    TraceF64("pan_y", panY_)});
    }
    // 画布底色 (旋转不影响, 永远填满 widget rect)
    r.FillRect(rect, bgColor);

    if (info_.fullWidth == 0 || info_.fullHeight == 0) return;

    // L48 followup: rect 变化 (典型 window resize 让 parent layout 重 size
     // widget) 自动 fire NotifyViewport, caller 不需要在 resize handler 里
     // 手动 invalidate viewport. visible tile 范围 = f(rect, zoom, pan), rect
     // 变 → visible 变 → 需要 push 新 viewport tile.
    if (rect.left   != lastNotifiedRect_.left   ||
        rect.top    != lastNotifiedRect_.top    ||
        rect.right  != lastNotifiedRect_.right  ||
        rect.bottom != lastNotifiedRect_.bottom) {
        if (viewUpdateDepth_ > 0) {
            pendingViewportNotify_ = true;
            TraceEvent("gh_img_view", "draw_viewport_deferred");
        } else {
            NotifyViewport();   // 内部 set lastNotifiedRect_ = rect 防重 fire
        }
    }

    // 视觉 AABB (rotation 应用后的可见框) 做早期剔除. logical dest 给 tile
    // 算位置用 (preview/tile dest 在未旋转坐标系里, D2D transform 完成旋转).
    D2D1_RECT_F visual = ComputeVisualDestRect();
    if (visual.right <= rect.left || visual.left >= rect.right ||
        visual.bottom <= rect.top || visual.top  >= rect.bottom) {
        return;  // 旋转后仍完全在视口外
    }

    D2D1_RECT_F dest = ComputeDestRect();   // logical (pre-rotation) dest

    r.PushClip(rect);

    /* 棋盘垫层: 画在一切图像内容之下, 只覆盖图片区域 (visual = 旋转后可见
     * AABB, 90° 倍数旋转下就是图片精确边界)。与视口求交限制 fill 面积 —
     * 大图高倍 zoom 时 dest 可达百万级像素, pattern brush 锚定屏幕原点,
     * 求交不会移动花纹。 */
    /* 图片阴影: 画在棋盘和一切图像内容之下。只画图片矩形外圈, 所以跟棋盘
     * (只画图片矩形内) 在空间上不重叠, 先后顺序无所谓。 */
    if (shadow_) DrawImageShadow_(r, visual, rect);

    if (checkerboard_) {
        D2D1_RECT_F cb = visual;
        cb.left   = std::max(cb.left,   rect.left);
        cb.top    = std::max(cb.top,    rect.top);
        cb.right  = std::min(cb.right,  rect.right);
        cb.bottom = std::min(cb.bottom, rect.bottom);
        if (cb.right > cb.left && cb.bottom > cb.top) DrawCheckerboard_(r, cb);
    }

    /* SVG 模式: 主视图优先 D2D SvgDocument 直绘, 避免线框图在默认缩放下
     * 先栅格化再采样导致发虚。D2D 不可用或创建失败时再走下方 LunaSVG
     * 视口栅格缓存路径。 */
    if (!svgD2DSteps_.empty()) {
        float dcx = (dest.left + dest.right ) * 0.5f;
        float dcy = (dest.top  + dest.bottom) * 0.5f;
        D2D1_MATRIX_3X2_F xf =
            D2D1::Matrix3x2F::Scale(zoom_, zoom_) *
            D2D1::Matrix3x2F::Translation(dest.left, dest.top);
        if (rotation_ != 0) {
            xf = xf * D2D1::Matrix3x2F::Rotation((float)rotation_,
                                                 D2D1::Point2F(dcx, dcy));
        }
        bool recordedSvgDocument = false;
        if (r.IsRecordingDisplayList()) {
            r.RecordSvgDocument(svgD2DSteps_, (float)svgW_, (float)svgH_, xf);
            recordedSvgDocument = true;
        }

        if (auto* ctx5 = r.RT5()) {
            D2D1_MATRIX_3X2_F oldXf;
            ctx5->GetTransform(&oldXf);
            /* steps 的顺序就是 z 序 —— 逐段画, 带 filter 的那步先画自己的阴影
             * 再画本体。三个绘制点共用 Renderer::DrawSvgStepsOrdered。 */
            Renderer::DrawSvgStepsOrdered(ctx5, svgD2DSteps_, (float)svgW_,
                                          (float)svgH_, xf * oldXf,
                                          &svgD2DStepDocs_, &svgD2DStepShadowDocs_);
            ctx5->SetTransform(oldXf);
            r.PopClip();
            return;
        }
        if (recordedSvgDocument) {
            r.PopClip();
            return;
        }
    }

    if (svgDoc_) {
        /* 后台栅格化: UI 线程只画缓存位图, 永不阻塞输入。
         *  1) 先消费后台已渲好的结果 (若有) → 换上 svgRaster_。
         *  2) 反算当前 widget 覆盖的 SVG 源坐标, 加 overscan, 只渲视口块。
         *  3) 已有缓存覆盖当前可见区且像素密度匹配 → 直接画; 否则后台渲最新块。
         *     没缓存时同步渲首块, 避免 SVG 首帧空白。 */
        ConsumeSvgRasterResult_(r);

        float ix[4], iy[4];
        ScreenToImage(rect.left,  rect.top,    ix[0], iy[0]);
        ScreenToImage(rect.right, rect.top,    ix[1], iy[1]);
        ScreenToImage(rect.right, rect.bottom, ix[2], iy[2]);
        ScreenToImage(rect.left,  rect.bottom, ix[3], iy[3]);

        float visL = std::min(std::min(ix[0], ix[1]), std::min(ix[2], ix[3]));
        float visR = std::max(std::max(ix[0], ix[1]), std::max(ix[2], ix[3]));
        float visT = std::min(std::min(iy[0], iy[1]), std::min(iy[2], iy[3]));
        float visB = std::max(std::max(iy[0], iy[1]), std::max(iy[2], iy[3]));
        visL = std::clamp(visL, 0.0f, static_cast<float>(svgW_));
        visR = std::clamp(visR, 0.0f, static_cast<float>(svgW_));
        visT = std::clamp(visT, 0.0f, static_cast<float>(svgH_));
        visB = std::clamp(visB, 0.0f, static_cast<float>(svgH_));

        if (visR > visL && visB > visT) {
            const float z = (zoom_ > 1e-6f) ? zoom_ : 1e-6f;
            const float padX = std::max((visR - visL) * 0.35f, 8.0f / z);
            const float padY = std::max((visB - visT) * 0.35f, 8.0f / z);
            float reqL = std::clamp(visL - padX, 0.0f, static_cast<float>(svgW_));
            float reqR = std::clamp(visR + padX, 0.0f, static_cast<float>(svgW_));
            float reqT = std::clamp(visT - padY, 0.0f, static_cast<float>(svgH_));
            float reqB = std::clamp(visB + padY, 0.0f, static_cast<float>(svgH_));
            if (reqR <= reqL) reqR = std::min(static_cast<float>(svgW_), reqL + 1.0f);
            if (reqB <= reqT) reqB = std::min(static_cast<float>(svgH_), reqT + 1.0f);

            float rasterScale = z * std::max(dpi_scale_, 1.0f);
            if (svgRasterMain_) {
                /* 含滤镜 SVG 会走 LunaSVG 栅格主路径。小图标如果只按原始
                 * 200/300px 或 512px 栅格化, 圆形边缘会比 D2D 矢量路径更容易显
                 * 锯齿; 给滤镜主路径一个更高的超采样底线, 再由 D2D 高质量下采样。 */
                constexpr float kFilteredSmallSvgRasterLongEdge = 1024.0f;
                const uint32_t svgLongEdge = std::max(svgW_, svgH_);
                if (svgLongEdge > 0 &&
                    svgLongEdge < static_cast<uint32_t>(kFilteredSmallSvgRasterLongEdge)) {
                    rasterScale = std::max(
                        rasterScale,
                        kFilteredSmallSvgRasterLongEdge / static_cast<float>(svgLongEdge));
                }
                /* 内嵌位图 SVG 的真实细节上限来自包内 PNG/JPG。极端 zoom 时继续把
                 * LunaSVG matrix scale 拉高只会触发 plutovg 的大比例栅格风险; 超过
                 * 16 px/SVG-unit 后交给 D2D 位图采样放大。纯矢量主路径仍走 D2D。 */
                rasterScale = std::min(rasterScale, 16.0f);
            } else {
                const uint32_t svgLongEdge = std::max(svgW_, svgH_);
                constexpr float kSmallSvgRasterLongEdge = 512.0f;
                if (svgLongEdge > 0 &&
                    svgLongEdge < static_cast<uint32_t>(kSmallSvgRasterLongEdge)) {
                    rasterScale = std::max(
                        rasterScale,
                        kSmallSvgRasterLongEdge / static_cast<float>(svgLongEdge));
                }
            }

            uint32_t renW = static_cast<uint32_t>(std::ceil((reqR - reqL) * rasterScale));
            uint32_t renH = static_cast<uint32_t>(std::ceil((reqB - reqT) * rasterScale));
            const uint32_t kViewportCap = 8192;
            if (renW > kViewportCap || renH > kViewportCap) {
                const double s = static_cast<double>(kViewportCap) /
                    static_cast<double>(renW > renH ? renW : renH);
                renW = static_cast<uint32_t>(std::max(1.0, std::floor(renW * s)));
                renH = static_cast<uint32_t>(std::max(1.0, std::floor(renH * s)));
            }
            if (renW == 0) renW = 1;
            if (renH == 0) renH = 1;

            const float targetScaleX = renW / (reqR - reqL);
            const float targetScaleY = renH / (reqB - reqT);
            const bool hasRaster = svgRaster_ || svgRasterResourceKey_.IsValid();
            const bool coversVisible = hasRaster &&
                svgRasterSrcL_ <= visL + 0.5f &&
                svgRasterSrcT_ <= visT + 0.5f &&
                svgRasterSrcR_ >= visR - 0.5f &&
                svgRasterSrcB_ >= visB - 0.5f;
            bool scaleOk = false;
            if (coversVisible) {
                const float cw = svgRasterSrcR_ - svgRasterSrcL_;
                const float ch = svgRasterSrcB_ - svgRasterSrcT_;
                if (cw > 0.0f && ch > 0.0f) {
                    const float cachedScaleX = svgRasterW_ / cw;
                    const float cachedScaleY = svgRasterH_ / ch;
                    scaleOk = cachedScaleX >= targetScaleX * 0.92f &&
                              cachedScaleX <= targetScaleX * 1.35f &&
                              cachedScaleY >= targetScaleY * 0.92f &&
                              cachedScaleY <= targetScaleY * 1.35f;
                }
            }
            if (!coversVisible || !scaleOk) {
                if (!hasRaster) {
                    EnsureSvgRaster(reqL, reqT, reqR, reqB, renW, renH, r);
                } else {
                    RequestSvgRasterAsync_(reqL, reqT, reqR, reqB, renW, renH,
                                           GetCurrentThreadId());
                }
            }
        }
        if (svgRaster_ || svgRasterResourceKey_.IsValid()) {
            bool rotated = (rotation_ != 0);
            if (rotated) {
                float dcx = (dest.left + dest.right ) * 0.5f;
                float dcy = (dest.top  + dest.bottom) * 0.5f;
                r.PushTransform(D2D1::Matrix3x2F::Rotation(
                    (float)rotation_, D2D1::Point2F(dcx, dcy)));
            }
            const float dpr = dpi_scale_ > 1e-3f ? dpi_scale_ : 1.0f;
            auto snapDev = [&](float v) { return std::round(v * dpr) / dpr; };
            D2D1_RECT_F rasterDest{
                snapDev(dest.left + svgRasterSrcL_ * zoom_),
                snapDev(dest.top  + svgRasterSrcT_ * zoom_),
                snapDev(dest.left + svgRasterSrcR_ * zoom_),
                snapDev(dest.top  + svgRasterSrcB_ * zoom_),
            };
            const float pixelW = svgRasterW_ > 0 ? static_cast<float>(svgRasterW_) : 1.0f;
            float pscale = pixelW > 0
                ? (rasterDest.right - rasterDest.left) * dpr / pixelW
                : 1.0f;
            auto interp = (pscale > 0.98f && pscale < 1.02f)
                ? D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                : (!antialias_ && pscale >= 1.0f)
                ? D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                : PickInterp(pscale);
            r.DrawImageResource(svgRasterResourceKey_, rasterDest, SamplingForInterp(interp));
            if (rotated) r.PopTransform();
        }
        r.PopClip();
        return;
    }

    // rotation != 0: 套一层绕 dest 中心的旋转 transform. 内部 DrawBitmap 仍
    // 用 logical dest, D2D 会把 bitmap 围着中心旋转, 视觉 AABB 自然变成
    // effW×effH×zoom. rotation == 0 时跳过 GetTransform/SetTransform 开销.
    bool rotated = (rotation_ != 0);
    if (rotated) {
        float dcx = (dest.left + dest.right ) * 0.5f;
        float dcy = (dest.top  + dest.bottom) * 0.5f;
        auto xf = D2D1::Matrix3x2F::Rotation((float)rotation_,
                                              D2D1::Point2F(dcx, dcy));
        r.PushTransform(xf);
    }

    // L230/L231 透明图垫层策略: preview 兜底层和低清 fallback 层画在瓦块
    // 【下方】, 带 alpha 的图瓦块透明/半透明像素挡不住它们, 低清内容会从
    // logo/文字边缘透出模糊毛边。所以垫层只画在 active level 缺瓦片的格子
    // 矩形内 (PushClip): 已覆盖区域永远不透底, 缺口内保持渐进加载过渡。
    // rotation != 0 时 PushClip 在旋转 transform 下不可靠 → 回退整层垫底
    // (旋转是少见瞬态操作)。
    std::vector<D2D1_RECT_F> missing;
    int presentTiles = 0;
    const bool haveRange = (info_.levels > 0) &&
        MissingCellRects_(activeLevel_, dest, missing, &presentTiles);
    const bool activeCovers  = haveRange && missing.empty();
    // clip 垫层只在【部分】瓦块已到时有意义; 可见区一块瓦块都没有 (fast
    // path levels=1 永远无瓦片 / 慢路径首帧) 时按旧行为整张画 preview,
    // 免得整屏拆成一格格 clip 绘制。
    const bool clipUnderlay  =
        haveRange && !missing.empty() && presentTiles > 0 && !rotated;

    // 1) preview 兜底（最粗，先画; 只画在缺瓦片区域）
    // Interpolation 走 PickInterp: 大幅下采 (zoom < 0.5) 退 HQ_LINEAR 避免
    // CUBIC negative-lobe 在高对比边缘振铃出色边. 适度缩放 / 上采保持
    // HQ_CUBIC 的锐度优势.
    if (previewResourceKey_.IsValid() && !activeCovers) {
        float pscale = previewW_ > 0
            ? (dest.right - dest.left) / static_cast<float>(previewW_)
            : 1.0f;
        // antialias_=false 且上采 (pscale >= 1) → NEAREST 像素清晰; 否则平滑.
        auto pinterp = (!antialias_ && pscale >= 1.0f)
            ? D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR
            : PickInterp(pscale);
        const ImageSampling ps = SamplingForInterp(pinterp);
        if (clipUnderlay) {
            for (const auto& rc : missing) {
                r.PushClipAliased(rc);
                r.DrawImageResource(previewResourceKey_, dest, ps);
                r.PopClip();
            }
        } else {
            r.DrawImageResource(previewResourceKey_, dest, ps);
        }
    }

    // 2) 多级金字塔：从最粗到最细，每级把已加载的可见瓦块画上去。
    //    后画的覆盖先画的 → active level（最细）压在最上面，旧级在下兜底。
    //    切级瞬间新级瓦块还没全到时，已有的旧级仍盖在 preview 之上 —— 无缝过渡。
    if (info_.levels > 0) {
        // 只画 活动层 + 比它更低清的层 (lvl >= active) —— 低清层在活动层瓦块未到达时
        // 画在其【下方】兜底 gap。不画比活动层【更高清】的残留层 (lvl < active, 如放大
        // 后保留的 L0): 它们在当前缩放下要大幅缩小, 画在最上层会 ring/锯齿/糊
        // (放大→缩小后文字发硬、部分区域看着"坏了"就是这个高清残留层盖在上面)。
        if (clipUnderlay) {
            for (const auto& rc : missing) {
                r.PushClipAliased(rc);
                for (int lvl = (int)info_.levels - 1; lvl > (int)activeLevel_; --lvl) {
                    DrawLevel(r, (uint32_t)lvl, dest, &rc);
                }
                r.PopClip();
            }
            DrawLevel(r, activeLevel_, dest);
        } else {
            const int coarsest = activeCovers ? (int)activeLevel_
                                              : (int)info_.levels - 1;
            for (int lvl = coarsest; lvl >= (int)activeLevel_; --lvl) {
                DrawLevel(r, (uint32_t)lvl, dest);
            }
        }
    }

    if (rotated) r.PopTransform();

    r.PopClip();
}

bool GhImgViewWidget::VisibleTileRange_(uint32_t level, const D2D1_RECT_F& dest,
                                        int& tx0, int& ty0, int& tx1, int& ty1) const {
    uint32_t lw = LevelWidth(level);
    uint32_t lh = LevelHeight(level);
    if (lw == 0 || lh == 0) return false;

    uint32_t ts = info_.tileSize;
    uint32_t txMax = (lw + ts - 1) / ts;
    uint32_t tyMax = (lh + ts - 1) / ts;

    // L194: 分轴 scale。极端宽高比图 (超长/超宽) 在 coarse level 宽比≠高比,
    // 用单一 (宽) scale 缩 Y 会让该级溢出 dest 底边 → X 用 scaleX, Y 用 scaleY。
    float scaleX = LevelToScreenScale (level) * zoom_;
    float scaleY = LevelToScreenScaleY(level) * zoom_;
    if (scaleX <= 1e-6f || scaleY <= 1e-6f) return false;

    // 可见瓦块范围 — rotation-aware: widget rect 4 角通过反旋转回 logical
    // 空间, 取 AABB, 再换算到 level 像素. rotation=0 时退化为旧路径
    // (RotateCCW 此时是 identity), 无功能差.
    float destLeft = dest.left;
    float destTop  = dest.top;
    float dcx = (dest.left + dest.right ) * 0.5f;
    float dcy = (dest.top  + dest.bottom) * 0.5f;
    auto cornerToLevel = [&](float sx, float sy, float& lx, float& ly) {
        float dx = sx - dcx;
        float dy = sy - dcy;
        float rdx, rdy;
        RotateCCW(rotation_, dx, dy, rdx, rdy);
        lx = (dcx + rdx - destLeft) / scaleX;
        ly = (dcy + rdy - destTop ) / scaleY;
    };
    float lx[4], ly[4];
    cornerToLevel(rect.left,  rect.top,    lx[0], ly[0]);
    cornerToLevel(rect.right, rect.top,    lx[1], ly[1]);
    cornerToLevel(rect.right, rect.bottom, lx[2], ly[2]);
    cornerToLevel(rect.left,  rect.bottom, lx[3], ly[3]);
    float vlx0 = std::min({lx[0], lx[1], lx[2], lx[3]});
    float vlx1 = std::max({lx[0], lx[1], lx[2], lx[3]});
    float vly0 = std::min({ly[0], ly[1], ly[2], ly[3]});
    float vly1 = std::max({ly[0], ly[1], ly[2], ly[3]});

    tx0 = std::max(0, (int)std::floor(vlx0 / ts));
    ty0 = std::max(0, (int)std::floor(vly0 / ts));
    tx1 = std::min((int)txMax, (int)std::floor((vlx1 - 1) / ts) + 1);
    ty1 = std::min((int)tyMax, (int)std::floor((vly1 - 1) / ts) + 1);
    return true;
}

bool GhImgViewWidget::MissingCellRects_(uint32_t level, const D2D1_RECT_F& dest,
                                        std::vector<D2D1_RECT_F>& out,
                                        int* presentTiles) const {
    out.clear();
    if (presentTiles) *presentTiles = 0;
    int tx0, ty0, tx1, ty1;
    if (!VisibleTileRange_(level, dest, tx0, ty0, tx1, ty1)) return false;
    if (tx0 >= tx1 || ty0 >= ty1) {
        // 可见区没瓦块格 (viewport 完全在图外) → 报 false, 走整层垫底旧路径。
        return false;
    }

    const uint32_t ts = info_.tileSize;
    const uint32_t lw = LevelWidth(level);
    const uint32_t lh = LevelHeight(level);
    const float scaleX = LevelToScreenScale (level) * zoom_;
    const float scaleY = LevelToScreenScaleY(level) * zoom_;
    const float dpr = dpi_scale_ > 1e-3f ? dpi_scale_ : 1.0f;
    auto snapDev = [dpr](float v) { return std::round(v * dpr) / dpr; };
    // 格子边界公式与 DrawLevel 的瓦块 dest 完全一致 (同 snap), 保证缺口
    // 矩形与相邻瓦块像素级对齐, 不留缝也不重叠。
    auto cellEdgeX = [&](int tx) {
        return snapDev(dest.left + (float)std::min((uint32_t)tx * ts, lw) * scaleX);
    };
    auto cellEdgeY = [&](int ty) {
        return snapDev(dest.top  + (float)std::min((uint32_t)ty * ts, lh) * scaleY);
    };

    for (int ty = ty0; ty < ty1; ++ty) {
        int runStart = -1;
        for (int tx = tx0; tx <= tx1; ++tx) {
            const bool miss = tx < tx1 &&
                tiles_.find(TileKey{level, (uint32_t)tx, (uint32_t)ty}) == tiles_.end();
            if (tx < tx1 && !miss && presentTiles) ++*presentTiles;
            if (miss && runStart < 0) runStart = tx;
            if (!miss && runStart >= 0) {
                out.push_back(D2D1_RECT_F{cellEdgeX(runStart), cellEdgeY(ty),
                                          cellEdgeX(tx),       cellEdgeY(ty + 1)});
                runStart = -1;
            }
        }
    }
    return true;
}

void GhImgViewWidget::DrawLevel(Renderer& r, uint32_t level, const D2D1_RECT_F& dest,
                                const D2D1_RECT_F* cull) {
    int tx0, ty0, tx1, ty1;
    if (!VisibleTileRange_(level, dest, tx0, ty0, tx1, ty1)) return;

    uint32_t ts = info_.tileSize;
    float scaleX = LevelToScreenScale (level) * zoom_;
    float scaleY = LevelToScreenScaleY(level) * zoom_;
    float destLeft = dest.left;
    float destTop  = dest.top;

    // cull: 垫层按缺口矩形 clip 绘制时, 只遍历与缺口相交的瓦块 (正确性由
    // PushClip 保证, 这里纯粹省 draw call)。cull 仅在 rotation==0 下传入。
    if (cull) {
        tx0 = std::max(tx0, (int)std::floor((cull->left  - destLeft) / scaleX / ts));
        ty0 = std::max(ty0, (int)std::floor((cull->top   - destTop ) / scaleY / ts));
        tx1 = std::min(tx1, (int)std::floor((cull->right  - destLeft) / scaleX / ts) + 1);
        ty1 = std::min(ty1, (int)std::floor((cull->bottom - destTop ) / scaleY / ts) + 1);
    }

    // Interpolation: 大幅下采 (scale < 0.5) 退 HQ_LINEAR 避免 cubic 振铃
    // 出色边. 适度缩放 / 上采保持 HQ_CUBIC. scale 局部变量已是 screen-per-
    // source-px, 见上面 LevelToScreenScale * zoom_.
    // antialias_=false 且上采 (scale >= 1) → NEAREST 像素清晰; 否则平滑.
    auto interp = (!antialias_ && scaleX >= 1.0f)
        ? D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR
        : PickInterp(scaleX);   // X/Y scale 差 <5%, interp 判定用 scaleX 即可
    // 瓦块用 aliased-edge 采样变体: DrawBitmap 矩形边缘 AA 在相邻瓦块共享
    // 边界各画一条部分覆盖边 → 拼缝半透明缝线; aliased 光栅化无缝。采样
    // 内核 (含 HQ prefilter) 与普通路径一致, 只是边缘光栅化不同。
    const ImageSampling sampling = TileSamplingForInterp(interp);
    // snap 屏幕坐标到整数【设备像素】(dest rect 是 DIP, ctx 按 dpi_scale_ 缩到设备).
    const float dpr = dpi_scale_ > 1e-3f ? dpi_scale_ : 1.0f;
    auto snapDev = [dpr](float v) { return std::round(v * dpr) / dpr; };

    for (int ty = ty0; ty < ty1; ++ty) {
        for (int tx = tx0; tx < tx1; ++tx) {
            auto it = tiles_.find(TileKey{level, (uint32_t)tx, (uint32_t)ty});
            if (it == tiles_.end()) continue;
            float x0 = snapDev(destLeft + (float)(tx * (int)ts)                     * scaleX);
            float y0 = snapDev(destTop  + (float)(ty * (int)ts)                     * scaleY);
            float x1 = snapDev(destLeft + (float)(tx * (int)ts + (int)it->second.w) * scaleX);
            float y1 = snapDev(destTop  + (float)(ty * (int)ts + (int)it->second.h) * scaleY);
            D2D1_RECT_F tileDest = {x0, y0, x1, y1};
            r.DrawImageResource(it->second.resourceKey, tileDest, sampling);
        }
    }

}

bool GhImgViewWidget::OnMouseDown(const MouseEvent& e) {
    if (!Contains(e.x, e.y)) return false;
    dragging_   = true;
    dragStartX_ = e.x;
    dragStartY_ = e.y;
    dragPanX_   = panX_;
    dragPanY_   = panY_;
    return true;
}

bool GhImgViewWidget::OnMouseMove(const MouseEvent& e) {
    if (!dragging_) return false;
    // drag 期间 fire 基类 onMouseMoveHook 让宿主观察拖动 (实现"图片拖出"等
    // 自定义手势). 窗口的 pressed 分支直接调本方法、不 fire hook (只有 hover
    // 路径 fire), 故在此补 fire, 否则宿主在 drag 期间收不到 move.
    // 宿主若在 hook 内发起 DoDragDrop, 其夺鼠标 capture → WM_CAPTURECHANGED →
    // CancelMouseCapture → 本 widget OnMouseUp → dragging_=false; DoDragDrop
    // 返回后下面复检 dragging_ 为假 → 不再 pan (拖出后图不被 pan 走).
    if (onMouseMoveHook) onMouseMoveHook(e);
    if (!dragging_) return true;
    // 拖动平移按轴锁 (锁定/只读视图): 锁住的轴拖动不动。放在 hook 之后 → 宿主仍能
    // 观察拖动手势 (如"拖出图片"), 只是被锁轴不平移; 命令式 SetPan 不受影响。
    // 长图锁水平 (lockX,!lockY) → 左右固定居中、上下仍可拖动阅读。
    if (panLockX_ && panLockY_) return true;   // 全锁: 无平移、无重绘 (同原 early-return)
    if (!panLockX_) panX_ = dragPanX_ + (e.x - dragStartX_);
    if (!panLockY_) panY_ = dragPanY_ + (e.y - dragStartY_);
    ConstrainPan();
    /* L47 follow-up: drag 期间完全不 fire NotifyViewport, drag 结束 OnMouseUp
     * 才 fire 一次精解. 之前实测节流 100ms 也不够 — drag 跨越 tile 数决定
     * 总 enqueue 量, 不取决于 callback fire 频率. 1 秒 drag 实测 234 tile
     * decode + set_tile (UI 线程 D2D CreateBitmap ~1-2ms/tile = ~470ms 阻塞) →
     * 用户感知卡.
     *
     * drag 期间 OnDraw 仍跟随 pan 渲染 cache 中已有的 tile (DrawLevel pyramid
     * fallback), 视觉是 "preview 模糊跟着 pan, drag 结束才精解新区域" —
     * 跟浏览器 / Photoshop drag 大图体验一致, 不卡 UI. tile cache 在 drag
     * 期间也不被打扰 (无新 SetTile, 无 evict), 旧 viewport 内 tile 保留.
     *
     * 重绘由 UiWindowImpl 的 pressed-widget mousemove 分支负责。这里不能再
     * InvalidateAllWindows: 打开信息/设置等 owned 窗口后, 每次 pan 都把其它
     * 顶层窗也标脏, 主窗口 WM_PAINT 更容易被高频 WM_MOUSEMOVE 饿到松手后
     * 才执行。 */
    return true;
}

bool GhImgViewWidget::OnMouseUp(const MouseEvent& /*e*/) {
    if (!dragging_) return false;
    dragging_ = false;
    /* L47 follow-up: drag 结束强 fire NotifyViewport — drag 期间 OnMouseMove
     * 完全不 fire callback (避免 tile enqueue 风暴, 跨多 tile 边界拖动会让
     * UI 线程 set_tile 卡几百 ms). drag 释放后这条 fire 让 caller 拿到最终
     * viewport, push_visible_tiles_ 精解新可见区 tile, 画面立即清晰. */
    NotifyViewport();
    return true;
}

bool GhImgViewWidget::OnMouseWheel(const MouseEvent& e) {
    if (!Contains(e.x, e.y)) return false;
    // wheelZoomEnabled_=false: 不内部缩放, 让宿主 (ui_widget_on_mouse_wheel
    // hook 已在分发开头无条件 fire) 自行决定滚轮行为 (如切图). 返 false = 未消费.
    if (!wheelZoomEnabled_) return false;
    float factor = (e.delta > 0) ? 1.25f : 1.0f / 1.25f;
    SetZoomAround(zoom_ * factor, e.x, e.y);
    return true;
}

D2D1_SIZE_F GhImgViewWidget::SizeHint() const {
    return {200.0f, 200.0f};
}

// ===== 几何辅助 =====

uint32_t GhImgViewWidget::LevelWidth(uint32_t level) const {
    uint32_t w = info_.fullWidth;
    for (uint32_t i = 0; i < level; ++i) w = std::max(1u, w / 2);
    return w;
}

uint32_t GhImgViewWidget::LevelHeight(uint32_t level) const {
    uint32_t h = info_.fullHeight;
    for (uint32_t i = 0; i < level; ++i) h = std::max(1u, h / 2);
    return h;
}

float GhImgViewWidget::LevelToScreenScale(uint32_t level) const {
    // 顶级宽 / level 宽 —— level 像素到顶级像素的放大倍数 (X 轴)
    if (info_.fullWidth == 0) return 1.0f;
    uint32_t lw = LevelWidth(level);
    if (lw == 0) return 1.0f;
    return (float)info_.fullWidth / (float)lw;
}

float GhImgViewWidget::LevelToScreenScaleY(uint32_t level) const {
    // L194: 顶级高 / level 高 —— Y 轴独立比例。LevelW/LevelH 各自 floor 折半,
    // 极端宽高比 (如 1080x29679) 在 coarse level 宽比≠高比 (lvl5: 32.7 vs 32.0),
    // 用宽比缩 Y 会让该级比图像真高多出几百~上千 px, 从底边溢出 → 必须分轴。
    if (info_.fullHeight == 0) return 1.0f;
    uint32_t lh = LevelHeight(level);
    if (lh == 0) return 1.0f;
    return (float)info_.fullHeight / (float)lh;
}

void GhImgViewWidget::SetDpiScale(float s) {
    if (s <= 1e-3f || s == dpi_scale_) return;
    dpi_scale_ = s;
    if (autoLevel_) activeLevel_ = PickAutoLevel();
}

D2D1_RECT_F GhImgViewWidget::ComputeDestRect() const {
    float fullW = (float)info_.fullWidth;
    float fullH = (float)info_.fullHeight;
    float w = fullW * zoom_;
    float h = fullH * zoom_;
    float cx = (rect.left + rect.right) * 0.5f + panX_;
    float cy = (rect.top  + rect.bottom) * 0.5f + panY_;
    float l = cx - w * 0.5f;
    float t = cy - h * 0.5f;
    /* 物理像素对齐: zoom×dpr 接近整数倍 (100%/200%, 或 borderless 下
     * 1/dpi 补偿后的"实际大小") 时把图像原点吸附到物理像素网格。居中
     * letterbox 奇偶差会让原点落在 0.5 物理像素上, 1:1 显示整图被双线性
     * 采样糊掉 (实测梯度能量掉到 53%)。非整数倍缩放本来就要重采样, 不吸附
     * 以保持 pan/zoom 连续。 */
    const float dpr = dpi_scale_ > 1e-3f ? dpi_scale_ : 1.0f;
    const float physScale = zoom_ * dpr;
    const float nearestInt = std::round(physScale);
    if (nearestInt >= 1.0f && std::fabs(physScale - nearestInt) < 1e-3f) {
        l = std::round(l * dpr) / dpr;
        t = std::round(t * dpr) / dpr;
    }
    return D2D1_RECT_F{l, t, l + w, t + h};
}

D2D1_RECT_F GhImgViewWidget::ComputeVisualDestRect() const {
    float effW = (float)EffectiveImageWidth();
    float effH = (float)EffectiveImageHeight();
    float w = effW * zoom_;
    float h = effH * zoom_;
    float cx = (rect.left + rect.right) * 0.5f + panX_;
    float cy = (rect.top  + rect.bottom) * 0.5f + panY_;
    return D2D1_RECT_F{cx - w * 0.5f, cy - h * 0.5f,
                       cx + w * 0.5f, cy + h * 0.5f};
}

uint32_t GhImgViewWidget::EffectiveImageWidth() const {
    return (rotation_ == 90 || rotation_ == 270) ? info_.fullHeight : info_.fullWidth;
}

uint32_t GhImgViewWidget::EffectiveImageHeight() const {
    return (rotation_ == 90 || rotation_ == 270) ? info_.fullWidth : info_.fullHeight;
}

void GhImgViewWidget::ScreenToImage(float sx, float sy, float& ix, float& iy) const {
    // 反向变换: screen → (减 widget center + pan) → CCW 反旋转 → 除 zoom →
    // 加图像中心. 顶级图像坐标系 (fullW × fullH).
    float fullW = (float)info_.fullWidth;
    float fullH = (float)info_.fullHeight;
    float cx = (rect.left + rect.right) * 0.5f;
    float cy = (rect.top  + rect.bottom) * 0.5f;
    float fx = sx - cx - panX_;
    float fy = sy - cy - panY_;
    float rx, ry;
    RotateCCW(rotation_, fx, fy, rx, ry);
    float z = (zoom_ > 1e-6f) ? zoom_ : 1e-6f;
    ix = rx / z + fullW * 0.5f;
    iy = ry / z + fullH * 0.5f;
}

void GhImgViewWidget::ImageToScreen(float ix, float iy, float& sx, float& sy) const {
    // 正向: image (顶级) → 减图像中心 → 乘 zoom → CW 旋转 → 加 widget center + pan.
    float fullW = (float)info_.fullWidth;
    float fullH = (float)info_.fullHeight;
    float fx = (ix - fullW * 0.5f) * zoom_;
    float fy = (iy - fullH * 0.5f) * zoom_;
    float rx, ry;
    RotateCW(rotation_, fx, fy, rx, ry);
    float cx = (rect.left + rect.right) * 0.5f;
    float cy = (rect.top  + rect.bottom) * 0.5f;
    sx = cx + panX_ + rx;
    sy = cy + panY_ + ry;
}

uint32_t GhImgViewWidget::PickAutoLevel() const {
    // 算 "屏幕物理像素 / level 像素" 比例 (= LevelToScreenScale * zoom *
    // dpi_scale), 选 ≤ 1.0 中最大的 level — 物理上"下采样最少且不上采样".
    // D2D HQ_CUBIC 下采样质量远好于上采样 (上采样无新信息只是插值, 下采样
    // 自带 mipmap LOD + 低通滤波), 所以宁可下采样几倍, 也别上采样 1.x.
    //
    // 实例 (世界地图 9934px fit zoom=0.108, DPI 150% dpi_scale=1.5):
    //   L0 scale_phys = 1.0  * 0.108 * 1.5 = 0.162   (下采样 6.2x)
    //   L1            = 2.0  * ...        = 0.324   (下采样 3.1x)
    //   L2            = 4.0  * ...        = 0.648   (下采样 1.5x) ⭐ 选这个
    //   L3            = 8.0  * ...        = 1.296   (上采样 1.3x = 旧算法选)
    //   L4            = 16.0 * ...        = 2.592   (上采样 2.6x)
    //
    // 算法: lvl=0 升序遍历, scale_phys 单调增. 一旦 > 1.0 退出, 返上一个
    // (= 满足 ≤ 1.0 的最大 lvl). 极小 zoom (全部 ≤ 1) 自然返 levels-1
    // (最高 lvl = 最小 mip, 兜底). zoom 大 (L0 已 > 1) 返 0 (初始, 最高
    // 分辨率 mip, 必然上采样但 caller 主动放大).
    //
    // 历史: build 52 修过算法方向 (旧是反的). build 100 阈值 1.0→0.5 缓解
    // 但没感知 DPI, 150% scaling 下按 DIP 算 0.5 = 物理 0.75 仍偏向上采样.
    // 本版直接按 物理px 算 + 改"≤ 1.0 最大 lvl"逻辑, 根治.
    if (info_.levels == 0) return 0;
    uint32_t result = 0;
    for (uint32_t lvl = 0; lvl < info_.levels; ++lvl) {
        float screen_per_level_phys = LevelToScreenScale(lvl) * zoom_ * dpi_scale_;
        if (screen_per_level_phys > 1.0f) break;
        result = lvl;
    }
    /* 兜底: zoom_*dpi 极大让 L0 已超阈值, for 循环第一次就 break, result=0
     * (初始). 这是要的 — L0 最高分辨率 mip, caller 正在主动放大查看细节. */
    return result;
}

void GhImgViewWidget::SwitchLevel(uint32_t level) {
    if (level >= info_.levels) level = info_.levels - 1;
    // L115: 切级时记下旧级, TrimToViewport_ 据此保留旧级 tile 供 OnDraw fallback 覆盖。
    if (level != activeLevel_) prevActiveLevel_ = activeLevel_;
    activeLevel_ = level;
}

void GhImgViewWidget::ConstrainPan() {
    // v1：完全自由 pan，不约束（小图能拖到画布外，方便对照参考）
    // 后续若要"小图居中、大图限边界"再加。
}

void GhImgViewWidget::NotifyViewport() {
    if (!onViewportChanged) return;
    if (info_.fullWidth == 0 || info_.fullHeight == 0) return;
    {
        TraceEvent("gh_img_view", "notify_viewport",
                   {TraceF64("zoom", zoom_),
                    TraceF64("pan_x", panX_),
                    TraceF64("pan_y", panY_),
                    TraceF64("l", rect.left),
                    TraceF64("t", rect.top),
                    TraceF64("r", rect.right),
                    TraceF64("b", rect.bottom)});
    }

    Viewport vp{};
    vp.activeLevel = activeLevel_;
    vp.zoom = zoom_;
    vp.panX = panX_;
    vp.panY = panY_;

    uint32_t lw = LevelWidth(activeLevel_);
    uint32_t lh = LevelHeight(activeLevel_);
    uint32_t ts = info_.tileSize;
    uint32_t txMax = (lw + ts - 1) / ts;
    uint32_t tyMax = (lh + ts - 1) / ts;

    D2D1_RECT_F dest = ComputeDestRect();
    float scaleX = LevelToScreenScale (activeLevel_) * zoom_;   // L194: 分轴, 同 DrawLevel
    float scaleY = LevelToScreenScaleY(activeLevel_) * zoom_;
    if (scaleX <= 1e-6f || scaleY <= 1e-6f) {
        vp.visibleTx0 = vp.visibleTy0 = 0;
        vp.visibleTx1 = txMax;
        vp.visibleTy1 = tyMax;
    } else {
        // rotation-aware: 4 角反旋转回 logical, 取 level-px AABB.
        float dcx = (dest.left + dest.right ) * 0.5f;
        float dcy = (dest.top  + dest.bottom) * 0.5f;
        auto corner = [&](float sx, float sy, float& lx, float& ly) {
            float dx = sx - dcx, dy = sy - dcy;
            float rdx, rdy;
            RotateCCW(rotation_, dx, dy, rdx, rdy);
            lx = (dcx + rdx - dest.left) / scaleX;
            ly = (dcy + rdy - dest.top ) / scaleY;
        };
        float lx[4], ly[4];
        corner(rect.left,  rect.top,    lx[0], ly[0]);
        corner(rect.right, rect.top,    lx[1], ly[1]);
        corner(rect.right, rect.bottom, lx[2], ly[2]);
        corner(rect.left,  rect.bottom, lx[3], ly[3]);
        float lx0 = std::min({lx[0], lx[1], lx[2], lx[3]});
        float lx1 = std::max({lx[0], lx[1], lx[2], lx[3]});
        float ly0 = std::min({ly[0], ly[1], ly[2], ly[3]});
        float ly1 = std::max({ly[0], ly[1], ly[2], ly[3]});
        int tx0 = std::max(0, (int)std::floor(lx0 / ts));
        int ty0 = std::max(0, (int)std::floor(ly0 / ts));
        int tx1 = std::min((int)txMax, (int)std::floor((lx1 - 1) / ts) + 1);
        int ty1 = std::min((int)tyMax, (int)std::floor((ly1 - 1) / ts) + 1);
        vp.visibleTx0 = (uint32_t)tx0;
        vp.visibleTy0 = (uint32_t)ty0;
        vp.visibleTx1 = (uint32_t)tx1;
        vp.visibleTy1 = (uint32_t)ty1;
    }

    // L48: 主动 trim viewport 外的 tile + 非 active level 的全部 tile.
    // 每个被清的 tile fire onTileEvicted → caller 同步 pushed_tiles_ erase.
    // 跟用户记忆的"按区加载内存小"行为对齐, 内存稳态 = viewport tile × 256KB.
    TrimToViewport_(vp.activeLevel, vp.visibleTx0, vp.visibleTx1,
                                     vp.visibleTy0, vp.visibleTy1);

    // L48 followup: 记 rect, OnDraw 比较防重 fire (resize-detect 用).
    lastNotifiedRect_ = rect;

    onViewportChanged(vp);
}

void GhImgViewWidget::InvalidateAllWindows() {
    GetContext().InvalidateAllWindows();
}

} // namespace ui
