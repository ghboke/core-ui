#pragma once

// 文本框的撤销 / 重做历史 (build 333)。TextInput / TextArea / ChipsInput 共用。
//
// 用法: 每次【真的要改内容之前】调 Record(改之前的快照, 操作类型, 现在时刻),
// 再去改。Undo / Redo 传入当前快照, 拿回要恢复成的快照。
//
// 合并规则 (对齐系统 RichEdit / 浏览器 input 的手感):
//   - 连续打字合成一步; 连续退格合成一步; 连续 Delete 合成一步。
//   - 打出空白 (空格 / 换行) 而前一个字不是空白时断开 —— 按"词"撤销。
//   - 距上一次编辑超过 kMergeMs 断开; 操作类型变了断开。
//   - 光标被挪动 / 选区变化时, 调用方调 BreakMerge() 断开。
//   - Atomic (粘贴 / 剪切 / 删选区 / chip 操作) 永远单独成一步。
//   - IME 上屏的字是一串连着到的 WM_CHAR, 按打字规则自然合成一步。
// 有新编辑时清空重做; 最多 kLimit 步, 超出丢最早的。
// 宿主程序设置内容 (SetText / SetValue) 时调用方调 Clear()。

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace ui {

enum class TextEditKind { Typing, Backspace, DeleteForward, Atomic };

/* TextInput / TextArea 的快照: 内容 + 光标 + 选区, 撤销后一起恢复。 */
struct TextEditSnap {
    std::wstring text;
    int cursor = 0;
    int selStart = -1;
    int selEnd = -1;
};

template <class Snap>
class TextUndoHistory {
public:
    static constexpr size_t kLimit = 100;
    static constexpr uint64_t kMergeMs = 1000;

    void Record(const Snap& before, TextEditKind kind, uint64_t nowMs,
                bool isSpace = false) {
        redo_.clear();
        const bool wordBreak = kind == TextEditKind::Typing && isSpace && !lastSpace_;
        const bool merge = mergeOpen_ && kind != TextEditKind::Atomic &&
                           kind == lastKind_ && !wordBreak &&
                           nowMs >= lastMs_ && nowMs - lastMs_ <= kMergeMs;
        if (!merge) Push(undo_, before);
        mergeOpen_ = kind != TextEditKind::Atomic;
        lastKind_ = kind;
        lastMs_ = nowMs;
        lastSpace_ = kind == TextEditKind::Typing && isSpace;
    }

    void BreakMerge() { mergeOpen_ = false; }

    bool Undo(const Snap& current, Snap& out) { return Step(undo_, redo_, current, out); }
    bool Redo(const Snap& current, Snap& out) { return Step(redo_, undo_, current, out); }

    void Clear() {
        undo_.clear();
        redo_.clear();
        mergeOpen_ = false;
    }

    size_t UndoCount() const { return undo_.size(); }
    size_t RedoCount() const { return redo_.size(); }

private:
    static void Push(std::deque<Snap>& d, const Snap& s) {
        d.push_back(s);
        while (d.size() > kLimit) d.pop_front();
    }

    bool Step(std::deque<Snap>& from, std::deque<Snap>& to,
              const Snap& current, Snap& out) {
        mergeOpen_ = false;
        if (from.empty()) return false;
        Push(to, current);
        out = from.back();
        from.pop_back();
        return true;
    }

    std::deque<Snap> undo_;
    std::deque<Snap> redo_;
    TextEditKind lastKind_ = TextEditKind::Atomic;
    uint64_t lastMs_ = 0;
    bool mergeOpen_ = false;
    bool lastSpace_ = false;
};

}  // namespace ui
