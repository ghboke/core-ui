#include "ocr_text_model.h"

#include <algorithm>

namespace ui {

// ---------------------------------------------------------------- 块 → 字拆分

namespace {

// UTF-8: 返回起始字节 b 的码点字节数 (非法字节按 1 处理, 不会卡死循环)
int Utf8Len(unsigned char b) {
    if (b < 0x80) return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1;
}

uint32_t Utf8Decode(const char* s, int len) {
    const unsigned char* u = (const unsigned char*)s;
    switch (len) {
        case 2: return ((u[0] & 0x1Fu) << 6) | (u[1] & 0x3Fu);
        case 3: return ((u[0] & 0x0Fu) << 12) | ((u[1] & 0x3Fu) << 6) | (u[2] & 0x3Fu);
        case 4: return ((u[0] & 0x07u) << 18) | ((u[1] & 0x3Fu) << 12) |
                       ((u[2] & 0x3Fu) << 6)  | (u[3] & 0x3Fu);
        default: return u[0];
    }
}

// 宽字符 (CJK / 全角 / 假名 / 谚文): 占一个字宽; 其余按 0.55 估。
// 跟 controls.cpp LabelWidget::SizeHint 的估算法一致。
bool IsWideChar(uint32_t cp) {
    return (cp >= 0x1100  && cp <= 0x115F) ||   // 谚文字母
           (cp >= 0x2E80  && cp <= 0xA4CF) ||   // 部首 / 假名 / CJK 统一表意
           (cp >= 0xA960  && cp <= 0xA97F) ||
           (cp >= 0xAC00  && cp <= 0xD7A3) ||   // 谚文音节
           (cp >= 0xF900  && cp <= 0xFAFF) ||   // CJK 兼容表意
           (cp >= 0xFE30  && cp <= 0xFE6F) ||
           (cp >= 0xFF00  && cp <= 0xFF60) ||   // 全角
           (cp >= 0xFFE0  && cp <= 0xFFE6) ||
           (cp >= 0x20000 && cp <= 0x3FFFD);    // CJK 扩展
}

bool IsAsciiSpace(uint32_t cp) {
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r';
}

// 沿字向按 [t0,t1] 从源 quad 切出子 quad。顶边与底边【各自】插值 ——
// 梯形/透视块的上下边不平行, 只按一条边算会让子 quad 歪掉。
void SliceQuad(const float q[8], float t0, float t1, float out[8]) {
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const float tlx = q[0], tly = q[1], trx = q[2], try_ = q[3];
    const float brx = q[4], bry = q[5], blx = q[6], bly = q[7];
    out[0] = lerp(tlx, trx, t0);  out[1] = lerp(tly, try_, t0);   // TL
    out[2] = lerp(tlx, trx, t1);  out[3] = lerp(tly, try_, t1);   // TR
    out[4] = lerp(blx, brx, t1);  out[5] = lerp(bly, bry, t1);    // BR
    out[6] = lerp(blx, brx, t0);  out[7] = lerp(bly, bry, t0);    // BL
}

} // namespace

void SplitItemsIntoChars(const OcrTextItem* items, size_t count,
                         std::vector<OcrTextItem>& out) {
    out.clear();
    if (!items) return;
    uint32_t wordSeq = 0;          // 全局递增, 保证跨源 item 的词不会撞号
    for (size_t i = 0; i < count; ++i) {
        const OcrTextItem& src = items[i];
        if (src.text.empty()) continue;

        // 一遍: 切码点 + 算权重
        struct Piece { size_t off; int len; float w; bool space; };
        std::vector<Piece> pieces;
        float total = 0.0f;
        for (size_t p = 0; p < src.text.size();) {
            const int len = Utf8Len((unsigned char)src.text[p]);
            const int use = (p + (size_t)len <= src.text.size()) ? len : 1;
            const uint32_t cp = Utf8Decode(src.text.data() + p, use);
            const bool sp = IsAsciiSpace(cp);
            const float w = sp ? 0.35f : (IsWideChar(cp) ? 1.0f : 0.55f);
            pieces.push_back({ p, use, w, sp });
            total += w;
            p += (size_t)use;
        }
        if (pieces.empty() || total <= 0.0f) continue;

        // 二遍: 按累计权重插值出每个字的 quad
        ++wordSeq;
        bool prevSpace = false;
        float acc = 0.0f;
        for (const Piece& pc : pieces) {
            const float t0 = acc / total;
            acc += pc.w;
            const float t1 = acc / total;

            /* 空白【自成一词】, 且其后的字另起一词。不能让空白跟着前一个词 ——
             * 那样双击 "ab cd" 的 b 会选出 "ab " 带个尾随空格。 */
            if (pc.space)         ++wordSeq;   // 空白单独一词
            else if (prevSpace)   ++wordSeq;   // 空白之后开新词
            prevSpace = pc.space;

            OcrTextItem ch;
            ch.text.assign(src.text, pc.off, (size_t)pc.len);
            SliceQuad(src.quad, t0, t1, ch.quad);
            ch.block = src.block;
            ch.line  = src.line;
            ch.word  = wordSeq;
            // 源块的 SPACE_AFTER 只落到最后一个字上 (块与块之间才补空格)
            ch.flags = 0;
            out.push_back(std::move(ch));
        }
        if (!out.empty()) out.back().flags = src.flags;
    }
}

// ---------------------------------------------------------------- 数据

void OcrTextModel::SetItems(const OcrTextItem* items, size_t count) {
    items_.assign(items, items + count);
    ReplaceSelection_({});
    selecting_ = false;
}

void OcrTextModel::Clear() {
    items_.clear();
    ReplaceSelection_({});
    selecting_ = false;
}

// ---------------------------------------------------------------- 命中

// 凸四边形闭区间命中：点在所有边的同侧（含边上）。顶点序 TL→TR→BR→BL，
// 两种绕向都容忍（按首个非零叉积定符号基准）。
static bool PointInQuad(const float q[8], float px, float py) {
    int sign = 0;
    for (int i = 0; i < 4; ++i) {
        const float ax = q[i * 2],           ay = q[i * 2 + 1];
        const float bx = q[((i + 1) & 3) * 2], by = q[((i + 1) & 3) * 2 + 1];
        const float cross = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
        if (cross == 0.0f) continue;          // 点在这条边的直线上
        const int s = cross > 0.0f ? 1 : -1;
        if (sign == 0) sign = s;
        else if (s != sign) return false;
    }
    return true;
}

int OcrTextModel::HitTest(float ix, float iy) const {
    for (size_t i = 0; i < items_.size(); ++i)
        if (PointInQuad(items_[i].quad, ix, iy)) return (int)i;
    return -1;
}

int OcrTextModel::NearestItem(float ix, float iy) const {
    int best = -1;
    float bestD2 = 0.0f;
    for (size_t i = 0; i < items_.size(); ++i) {
        const float* q = items_[i].quad;
        const float cx = (q[0] + q[2] + q[4] + q[6]) * 0.25f;
        const float cy = (q[1] + q[3] + q[5] + q[7]) * 0.25f;
        const float d2 = (cx - ix) * (cx - ix) + (cy - iy) * (cy - iy);
        if (best < 0 || d2 < bestD2) { best = (int)i; bestD2 = d2; }
    }
    return best;
}

// ---------------------------------------------------------------- 选择

void OcrTextModel::ReplaceSelection_(std::vector<Interval> next) {
    const bool changed =
        next.size() != selection_.size() ||
        !std::equal(next.begin(), next.end(), selection_.begin(),
                    [](const Interval& a, const Interval& b) {
                        return a.first == b.first && a.last == b.last;
                    });
    selection_ = std::move(next);
    if (changed && onSelectionChanged) onSelectionChanged();
}

void OcrTextModel::BeginSelect(uint32_t anchorItem) {
    if (anchorItem >= items_.size()) return;
    selecting_ = true;
    anchor_ = anchorItem;
    ReplaceSelection_({{anchorItem, anchorItem}});
}

void OcrTextModel::UpdateSelect(uint32_t currentItem) {
    if (!selecting_ || currentItem >= items_.size()) return;
    ReplaceSelection_({{std::min(anchor_, currentItem),
                        std::max(anchor_, currentItem)}});
}

void OcrTextModel::EndSelect() {
    selecting_ = false;
}

void OcrTextModel::SelectAll() {
    if (items_.empty()) return;
    ReplaceSelection_({{0, (uint32_t)items_.size() - 1}});
}

void OcrTextModel::SelectItem(uint32_t item) {
    if (item >= items_.size()) return;
    ReplaceSelection_({{item, item}});
}

void OcrTextModel::SelectWord(uint32_t item) {
    if (item >= items_.size()) return;
    const OcrTextItem& it = items_[item];
    auto same = [&](const OcrTextItem& o) {
        return o.block == it.block && o.line == it.line && o.word == it.word;
    };
    uint32_t first = item, last = item;
    while (first > 0 && same(items_[first - 1])) --first;
    while (last + 1 < items_.size() && same(items_[last + 1])) ++last;
    ReplaceSelection_({{first, last}});
}

void OcrTextModel::SelectLine(uint32_t item) {
    if (item >= items_.size()) return;
    const uint32_t line  = items_[item].line;
    const uint32_t block = items_[item].block;
    uint32_t first = item, last = item;
    while (first > 0 && items_[first - 1].line == line &&
           items_[first - 1].block == block)
        --first;
    while (last + 1 < items_.size() && items_[last + 1].line == line &&
           items_[last + 1].block == block)
        ++last;
    ReplaceSelection_({{first, last}});
}

void OcrTextModel::ClearSelection() {
    ReplaceSelection_({});
}

bool OcrTextModel::IsItemSelected(uint32_t i) const {
    for (const Interval& iv : selection_)
        if (i >= iv.first && i <= iv.last) return true;
    return false;
}

// ---------------------------------------------------------------- 文本组装

void OcrTextModel::AppendIntervalText_(const Interval& iv, std::string& out) const {
    for (uint32_t i = iv.first; i <= iv.last && i < items_.size(); ++i) {
        const OcrTextItem& it = items_[i];
        out += it.text;
        if (i == iv.last) break;                    // 末词后不加分隔
        const OcrTextItem& next = items_[i + 1];
        if (next.block != it.block)      out += "\n\n";
        else if (next.line != it.line)   out += "\n";
        else if (it.flags & kOcrItemSpaceAfter) out += " ";
    }
}

std::string OcrTextModel::SelectedText() const {
    std::string out;
    for (size_t k = 0; k < selection_.size(); ++k) {
        if (k) out += "\n";                          // 多选区间以换行分隔（预留）
        AppendIntervalText_(selection_[k], out);
    }
    return out;
}

} // namespace ui
