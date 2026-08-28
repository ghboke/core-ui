#pragma once
//
// ocr_text_model.h — 图片划词的纯逻辑模型（无渲染/D2D 依赖）
//
// 职责：存 OCR 文本项（quad + 层级）、命中测试、阅读序选择状态机、
// 按拼接规则组装选中文本。被 OcrImgViewWidget 持有；因为不碰渲染，
// 未来 gh_img_view 等其他 widget 想复用划词能力时直接挂同一个 model。
//
// 坐标承诺：所有 quad 均为【顶级图像像素坐标】——视口 zoom/pan/旋转是
// widget 层的事，model 永远只认图像空间。
//
// 选择模型：items 数组顺序 == 阅读序（宿主保证）。选区是阅读序上的
// 连续闭区间，内部存区间 list（v1 恒 0/1 个区间；预留将来 Ctrl+ 多选区）。
//
// 拼接规则（SelectedText）：
//   同 block 同 line：直连；该词带 kOcrItemSpaceAfter 时其后加 " "
//   跨 line（同 block）：加 "\n"
//   跨 block：加 "\n\n"
//   区间末词的 SpaceAfter 不输出（不留尾部空格）
//
// 线程：仅 UI 线程。

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ui {

// 复制拼接 flags（对齐未来 C API 的 UiOcrTextItem.flags 位定义）
inline constexpr uint32_t kOcrItemSpaceAfter = 1u << 0;

struct OcrTextItem {
    std::string text;      // UTF-8
    // 图像像素坐标 4 顶点: TL(x,y) TR BR BL。轴对齐 bbox 是其特例；
    // 允许任意凸四边形（倾斜/透视文本）。
    float    quad[8] = {};
    uint32_t block = 0;    // 段落分组
    uint32_t line  = 0;    // 行分组
    uint32_t word  = 0;    // 词分组（同 block+line+word 的相邻 item = 一个词）
    uint32_t flags = 0;    // kOcrItem* 位
};

// 把"每个 item 是词/行/块"的输入拆成"每个 item 是一个字"，让选择粒度统一
// 到字级。拆法：文本按 UTF-8 码点切，quad 沿字向按字宽权重插值。
//
// 精度说明：中文全角等宽，均匀切分几乎精确 —— 而整行返回正是中文 OCR 的
// 常态，也是最需要字级的场景。拉丁文按字宽权重估算（跟 controls.cpp 的
// SizeHint 同一套：CJK 记 1.0、其余 0.55），是近似；但拉丁引擎普遍已按词
// 返回，块本身就很短，误差影响小。
//
// 词分组：源 item 内以 ASCII 空白为界递增 word，所以 "Hello world" 拆出的
// 字会分成两个词，双击能只选中一个。中文无法分词，整块归一个词（双击选中
// 整块，与拆分前行为一致）。
//
// 空文本 item 直接跳过。传入已是字级的数据也安全（单字 item 拆出它自己）。
void SplitItemsIntoChars(const OcrTextItem* items, size_t count,
                         std::vector<OcrTextItem>& out);

class OcrTextModel {
public:
    // ---- 数据 ----
    // 深拷贝 items（宿主缓冲调用后可释放）。清空现有选区。
    void SetItems(const OcrTextItem* items, size_t count);
    void Clear();                       // 清数据 + 清选区
    size_t ItemCount() const            { return items_.size(); }
    const OcrTextItem& Item(size_t i) const { return items_[i]; }

    // ---- 命中 ----
    // 图像空间点 → 命中的 item 下标；无命中返 -1。
    // 重叠 quad 取阅读序靠前者。边界点算命中（闭区间语义）。
    int HitTest(float ix, float iy) const;
    // 最近 item（quad 中心欧氏距离），拖动划词越过空白时的吸附；空数据返 -1。
    int NearestItem(float ix, float iy) const;

    // ---- 选择状态机（阅读序连续区间）----
    // BeginSelect: 落锚并产生 [anchor,anchor] 选区（替换旧选区）。
    // UpdateSelect: 拖动中更新活动端 → 选区 = [min(anchor,cur), max(...)]。
    // 未 Begin 时 Update/End 为 no-op。
    void BeginSelect(uint32_t anchorItem);
    void UpdateSelect(uint32_t currentItem);
    void EndSelect();
    bool IsSelecting() const            { return selecting_; }

    void SelectAll();
    // 选中单个 item（双击手势用）。item 的粒度就是宿主喂的 OCR 粒度 ——
    // 英文通常是词, 中文 OCR 常整行返回一个块, 双击即选中那一整行。
    void SelectItem(uint32_t item);
    // 选中 item 所在的词（同 block+line+word 的连续段），双击手势用。
    void SelectWord(uint32_t item);
    // 选中 item 所在整行（同 block 同 line 的连续段），三击手势用。
    void SelectLine(uint32_t item);
    void ClearSelection();

    bool HasSelection() const           { return !selection_.empty(); }
    // 阅读序闭区间。v1 恒 0/1 个；预留多选区。
    struct Interval { uint32_t first; uint32_t last; };
    const std::vector<Interval>& Selection() const { return selection_; }
    bool IsItemSelected(uint32_t i) const;

    // ---- 文本组装 ----
    std::string SelectedText() const;

    // 选区实际变化时同步 fire（选区没变的 Update / 空转 Clear 不 fire）。
    std::function<void()> onSelectionChanged;

private:
    void ReplaceSelection_(std::vector<Interval> next);  // diff + fire callback
    void AppendIntervalText_(const Interval& iv, std::string& out) const;

    std::vector<OcrTextItem> items_;
    std::vector<Interval>    selection_;
    bool     selecting_ = false;
    uint32_t anchor_    = 0;
};

} // namespace ui
