#include "controls.h"
#include "event.h"
#include "asset.h"
#include "display_list.h"
#include "draw_api_context.h"
#include "measure_context.h"
#include "renderer.h"
#include "resource_store.h"
#include "ui_context.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <windows.h>
#include <cstring>
#include <cstdlib>
#include <limits>

namespace ui {

namespace {

ImageSampling SamplingForBitmap(bool useHQ, D2D1_BITMAP_INTERPOLATION_MODE interp) {
    if (useHQ) return ImageSampling::HighQualityCubic;
    return interp == D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
        ? ImageSampling::Nearest
        : ImageSampling::Linear;
}

/* ResourceStore generation is global across widgets: GhImgViewWidget purges
 * its retired image generation wholesale. Titlebar icons are permanent widget
 * resources, so a regular local counter (1, 2, ...) can collide with image
 * loading and be deleted while the titlebar is hidden. ResourceKey still has
 * a globally unique resource_id, therefore one reserved generation is enough
 * for every TitleBarWidget icon. Keep it distinct from GhImgView's checker
 * tile generation (~0ull). */
constexpr uint64_t kTitleBarIconGeneration =
    (std::numeric_limits<uint64_t>::max)() - 1;

/* EXE 嵌入图标 (资源 ID=1) 的加载分辨率。图标槽位是 20x20 DIP, 但 DIP ≠ 设备
 * 像素: 150% 缩放下要 30px, 200% 要 40px。以前按 24 加载, 高 DPI 下等于把
 * 24px 位图放大 → 标题栏图标发虚 (下游 GuoheView 报的)。这里一次取足 128px,
 * 配合 HighQualityCubic 缩小采样, 任何缩放比都锐, 也不用跟 WM_DPICHANGED
 * 联动重载。代价是 128*128*4 = 64KB 常驻, 每个 TitleBar 一份。 */
constexpr int kTitleBarExeIconSourcePx = 128;

/* 图标绘制槽位 (DIP, 相对 titlebar 左上角)。必须是正方 —— 之前是 18 宽 x 20
 * 高, 方形图标会被纵向拉伸 11%。titlebar 高 36, 20 高居中即上下各 8。 */
constexpr float kTitleBarIconLeft = 10.0f;
constexpr float kTitleBarIconTop  = 8.0f;
constexpr float kTitleBarIconSize = 20.0f;
constexpr float kTitleBarTitleGap = 8.0f;   /* 图标右缘到标题文字的间距 */

} // namespace

// ---- Label ----

void LabelWidget::OnDraw(Renderer& r) {
    Widget::OnDraw(r);
    if (textColorFn_) color_ = textColorFn_();
    auto weight = bold_ ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
    D2D1_COLOR_F c;
    if (css.hasFg)         c = css.fg;
    else if (customColor_) c = color_;
    else                   c = theme::kBtnText();
    float fs = (css.fontSize > 0) ? css.fontSize : fontSize_;

    // CSS padding shrinks the text-render area; bg/border still fill the
    // outer rect (Widget::OnDraw handled that already).
    D2D1_RECT_F textRect = {
        rect.left + padL,
        rect.top + padT,
        rect.right - padR,
        rect.bottom - padB,
    };

    // 选区绘制（仅 selectable + 单行场景）：先填选中背景，再覆盖一段不同色
    // 文字。多行/wrap 场景暂不支持文本选中。
    if (selectable && HasSelection() && !wrap_) {
        bool dark = theme::IsDark();
        D2D1_COLOR_F accent = css.hasAccent ? css.accent : theme::kAccent();
        int s = std::min(selectionStart_, selectionEnd_);
        int e = std::max(selectionStart_, selectionEnd_);
        std::wstring before = text_.substr(0, s);
        std::wstring sel    = text_.substr(s, e - s);
        float x1 = textRect.left + r.MeasureTextWidth(before, fs, nullptr);
        float x2 = x1 + r.MeasureTextWidth(sel, fs, nullptr);
        D2D1_COLOR_F selBg;
        if (focused_) {
            selBg = css.hasSelTextBg ? css.selTextBg : accent;
        } else {
            selBg = css.hasSelTextBgInactive ? css.selTextBgInactive
                  : (dark ? theme::Rgba(0xFF, 0xFF, 0xFF, 0.18f)
                          : theme::Rgba(0x00, 0x00, 0x00, 0.18f));
        }
        D2D1_RECT_F selRect = {x1, textRect.top + 2, x2, textRect.bottom - 2};
        r.FillRect(selRect, selBg);

        D2D1_COLOR_F selFg = c;
        bool customSelFg = false;
        if (focused_) {
            customSelFg = true;
            selFg = css.hasSelTextFg ? css.selTextFg : theme::white;
        } else if (css.hasSelTextFgInactive) {
            customSelFg = true;
            selFg = css.selTextFgInactive;
        }
        auto vAlign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
        // 选区前后用 c 画，选区内用 selFg 画（如有）
        r.PushClip({textRect.left, textRect.top, x1, textRect.bottom});
        r.DrawText(text_, textRect, c, fs, align_, weight, vAlign, false);
        r.PopClip();
        r.PushClip({x1, textRect.top, x2, textRect.bottom});
        r.DrawText(text_, textRect, customSelFg ? selFg : c, fs, align_, weight, vAlign, false);
        r.PopClip();
        r.PushClip({x2, textRect.top, textRect.right, textRect.bottom});
        r.DrawText(text_, textRect, c, fs, align_, weight, vAlign, false);
        r.PopClip();
        return;
    }

    // 始终垂直居中：rect 的高度由 SizeHint 算出（textH + 6px slack）；用 NEAR
    // 时 6px slack 全堆在底部，文字被顶到 rect 顶 → 跟 icon 等几何中心对齐的
    // 兄弟节点比，会显得"偏上"。CENTER 把 slack 平分到上下，跟 icon 完美对齐。
    // 多行 wrap：rect 紧贴内容块 → CENTER ≈ NEAR，不会让段落塌到中间。
    auto vAlign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    if (wrap_ && maxLines_ > 0) {
        r.PushClip(textRect);
        r.DrawText(text_, textRect, c, fs, align_, weight, vAlign, true);
        r.PopClip();
    } else {
        r.DrawText(text_, textRect, c, fs, align_, weight, vAlign, wrap_);
    }
}

void LabelWidget::SetSelectable(bool v) {
    selectable = v;
    focusable = v;        // 开启选中后让 widget 可获焦，才能接 Ctrl+C
    cursor = v ? CursorKind::Text : CursorKind::Default;
}

// 二分查找：x 像素位置对应文本中的字符索引
int LabelWidget::CharIndexAtX(MeasureContext& measure, float x) const {
    if (text_.empty()) return 0;
    float fs = (css.fontSize > 0) ? css.fontSize : fontSize_;
    // 文本起点偏移 padL（OnDraw 把 textRect 内缩了）—— 不减 padding，
    // padding 区域里的点击会算成下一个字符位置，跟视觉不符。
    float relX = x - rect.left - padL;
    if (relX <= 0) return 0;
    int lo = 0, hi = (int)text_.size();
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        float w = measure.MeasureTextWidth(text_.substr(0, mid), fs, nullptr);
        if (w <= relX) lo = mid;
        else           hi = mid - 1;
    }
    return lo;
}

bool LabelWidget::OnMouseDown(const MouseEvent& e) {
    // When the label is selectable, the text-selection drag owns the
    // mouse-down. Otherwise fall through to Widget::OnMouseDown so the
    // generic event hooks (onMouseDownHook → @mousedown handler) fire.
    if (!selectable) return Widget::OnMouseDown(e);
    if (!Contains(e.x, e.y)) return Widget::OnMouseDown(e);
    extern MeasureContext* g_activeMeasureContext;
    if (!g_activeMeasureContext) return Widget::OnMouseDown(e);
    int idx = CharIndexAtX(*g_activeMeasureContext, e.x);
    selectionStart_ = selectionEnd_ = idx;
    dragging_ = true;
    Widget::OnMouseDown(e);   // still fire any user @mousedown alongside selection
    return true;
}

bool LabelWidget::OnMouseMove(const MouseEvent& e) {
    if (selectable && dragging_) {
        extern MeasureContext* g_activeMeasureContext;
        if (g_activeMeasureContext) {
            selectionEnd_ = CharIndexAtX(*g_activeMeasureContext, e.x);
        }
        Widget::OnMouseMove(e);
        return true;
    }
    return Widget::OnMouseMove(e);
}

bool LabelWidget::OnMouseUp(const MouseEvent& e) {
    if (selectable) {
        dragging_ = false;
        if (selectionStart_ == selectionEnd_) ClearSelection();
        Widget::OnMouseUp(e);
        return true;
    }
    return Widget::OnMouseUp(e);
}

std::wstring LabelWidget::SelectedText() const {
    if (!HasSelection()) return L"";
    int s = std::min(selectionStart_, selectionEnd_);
    int e = std::max(selectionStart_, selectionEnd_);
    return text_.substr(s, e - s);
}

bool LabelWidget::OnKeyDown(int vk) {
    if (!selectable || !HasSelection()) return false;
    // Ctrl+C 复制选中文本到剪贴板
    if (vk == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        std::wstring sel = SelectedText();
        if (sel.empty()) return false;
        if (OpenClipboard(nullptr)) {
            EmptyClipboard();
            size_t bytes = (sel.size() + 1) * sizeof(wchar_t);
            HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (h) {
                if (auto* p = (wchar_t*)GlobalLock(h)) {
                    memcpy(p, sel.c_str(), bytes);
                    GlobalUnlock(h);
                    SetClipboardData(CF_UNICODETEXT, h);
                }
            }
            CloseClipboard();
        }
        return true;
    }
    return false;
}

D2D1_SIZE_F LabelWidget::SizeHint() const {
    /* 估算文本宽度：CJK 字符按 1.0 倍 fontSize，其他按 0.55 倍 */
    /* 加粗字体每字符多约 8% 宽度，避免末字符触发 ellipsis 截断 */
    float estW = 0;
    if (fixedW > 0) {
        estW = fixedW;
    } else {
        // 优先用 renderer 的精确测量（DWrite），估算只在 renderer 还没 attach
        // 时兜底。Estimate 对粗体小字（如 11px bold "PRO"）会少算 → wrap=true
        // 时把单行文字折成 2 行。+1px 防亚像素 ellipsis。
        extern MeasureContext* g_activeMeasureContext;
        if (g_activeMeasureContext && !text_.empty()) {
            auto weight = bold_ ? DWRITE_FONT_WEIGHT_SEMI_BOLD
                                : DWRITE_FONT_WEIGHT_NORMAL;
            estW = g_activeMeasureContext->MeasureTextWidth(text_, fontSize_, nullptr, weight) + 1.0f;
        } else {
            float asciiW = bold_ ? fontSize_ * 0.66f : fontSize_ * 0.62f;
            for (wchar_t ch : text_) {
                /* build 290: 0x2E80 这条线漏了 **U+2010–U+2027 那段通用标点**
                 * —— …(2026) —(2014) ‘’(2018/2019) “”(201C/201D) 在中文排版里
                 * 都是全角, 却因为码点小于 0x2E80 被按 0.62 倍的半角算。
                 *
                 * 后果不是"稍微窄一点"而是**文字被截掉**: 估算偏小 → 上层
                 * (如 ContextMenu::MenuWidth) 按这个宽度布局 → 真正绘制时放不
                 * 下 → DWrite 的 trimming 把末尾字符换成省略号。实测
                 * "收集箱…" 显示成 "收集...", 而同样 4 个字符的 "收集箱囧"
                 * (全部 >= 0x2E80) 完好。 */
                if (ch >= 0x2E80 || (ch >= 0x2010 && ch <= 0x2027))
                    estW += fontSize_;                     /* CJK / 全角 */
                else
                    estW += asciiW;
            }
        }
        // 不再 floor 到 60px —— 否则 pill / badge / chip 这种"包裹文字"
        // 的 label 在 align-self: flex-start 下也会被撑到 60px。短文本如
        // "3 / 8"现在会按真实测量宽度（~28px）布局。
    }
    /* SizeHint 一律返自然 intrinsic 宽 (w = estW). 不读 parent_->rect 把 w
     * cap 到父宽 — 那是 layout 期间用 layout 结果, 跟 CSS shrink-to-fit
     * (position: absolute / float / inline-block) 形成循环依赖:
     *   父宽 = 子 SizeHint.w (蛋)
     *   子 SizeHint.w = min(intrinsic, 父.previous_rect - padding) (鸡)
     * 旧代码做这个 cap 让 absolute + wrap label 在动态文本变化时 (e.g.
     * counter / i18n / status) 卡死在第一次 layout 的父宽里, 之后文本变长
     * 只会增加行数而不撑开父级. 跟浏览器规范 (CSS 2.1 §10.3.7
     * "shrink-to-fit width") 矛盾.
     *
     * 折行实际发生在 DrawText 阶段, 用 widget 真实 rect.width 作 layoutW
     * (见 EnsureLayout: layoutW = wrap_ ? maxW : 1e6f). 所以即使 SizeHint
     * 返自然宽, 受约束 (fixedW / flex shrink / 父级显式 width) 的 label 仍
     * 然在画的时候按真实分配宽度 wrap. */
    float w = estW;
    /* line-height 决定单行 label 高度 (build 92, L20):
     *   - lineHeightPx_     >0: CSS "Npx" 直接用
     *   - lineHeightRatio_  >0: CSS unitless (1.3 = 1.3×fontSize)
     *   - 否则:                  默认 1.3 × fontSize (现代浏览器 / Fluent UI)
     * 历史 fontSize_ + 10.0f 在密集列表偏松, 见 controls.h 注释. */
    float lineH = (lineHeightPx_ > 0.0f)    ? lineHeightPx_
                : (lineHeightRatio_ > 0.0f) ? (lineHeightRatio_ * fontSize_)
                                            : (fontSize_ * 1.3f);
    float h = fixedH > 0 ? fixedH : lineH;
    if (wrap_ && fixedW > 0) {
        /* 仅 fixedW 显式约束时 SizeHint 能可靠预测 wrap 后的高度. 没 fixedW
         * 时父宽未知 (要等 DoLayout 分配 rect 才知道), SizeHint 给单行高;
         * 父级层 layout 时若分配 rect.width < intrinsic, draw 时按 rect
         * 宽度 wrap, 可能视觉溢出 — 这条 limitation 由调用方负责 (给
         * fixedW 或父级提供足够 cross-axis 空间), 与之前用 parent_->rect
         * 的方案相比换来"absolute / shrink-to-fit 不死锁"的更大好处. */
        float availW = fixedW - padL - padR;
        if (availW > 10.0f) {
            extern MeasureContext* g_activeMeasureContext;
            if (g_activeMeasureContext) {
                auto weight = bold_ ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
                float textH = g_activeMeasureContext->MeasureTextHeight(text_, availW, fontSize_, weight);
                /* build 92 (L20): wrap 分支也走 line-height. DWrite textH 包含
                 * ascent + descent (单行 ≈ fontSize × 1.2). 折成行数后乘 lineH.
                 * 1.4 阈值: 单行实际 ratio ≈ 1.2, 用 1.4 当 "definitely > 1 line"
                 * 边界, 防 ceil(1.23) 错误判 2 行. */
                int numLines = std::max(1, (int)std::ceil(textH / (fontSize_ * 1.4f)));
                h = numLines * lineH;
                if (maxLines_ > 0) {
                    float maxH = lineH * maxLines_;
                    if (h > maxH) h = maxH;
                }
            }
        }
    }
    // CSS padding 撑大 label 整体尺寸（badge / pill / chip 用），文字仍画
    // 在 textRect = rect 减 padding 内。
    return {w + padL + padR, h + padT + padB};
}

// ---- Button ----

void ButtonWidget::OnDraw(Renderer& r) {
    bool dark = theme::IsDark();
    float cr = (css.borderRadius >= 0) ? css.borderRadius : theme::radius::medium;
    D2D1_COLOR_F bg, textColor;

    // When CSS owns the background, skip the theme's bg + border + bottom
    // elevation line entirely. The elevation line is a square FillRect that
    // leaks past the rounded silhouette at large radii (pill buttons), showing
    // as a dark line below the bottom corners that the later FillRoundedRect
    // can't cover (it only paints inside the silhouette).
    bool cssOwnsBg = (bgColor.a > 0);
    if (cssOwnsBg) {
        // Auto press/hover feedback: CSS users typically don't wire :pressed.
        // A small luminance adjustment gives the expected tactile feel even
        // without an explicit variant. If the user DID write :pressed, the
        // recompute already applied their color and this 0.85 multiply just
        // makes it a touch darker — acceptable drift.
        D2D1_COLOR_F finalBg = bgColor;
        if (pressed) {
            finalBg.r *= 0.85f; finalBg.g *= 0.85f; finalBg.b *= 0.85f;
        } else if (hovered) {
            finalBg.r *= 0.92f; finalBg.g *= 0.92f; finalBg.b *= 0.92f;
        }
        r.FillRoundedRect(rect, cr, cr, finalBg);
        /* 自定义底色的文字按底色相对亮度自动选黑白 — 深色底 (危险红等)
         * 不再出现暗色主题黑字/亮色主题黑字贴深底的可读性问题。跟
         * ui_theme_set_accent 的 accentText 同一规则。 */
        const float lum = 0.299f * bgColor.r + 0.587f * bgColor.g +
                          0.114f * bgColor.b;
        textColor = lum > 0.6f ? D2D1_COLOR_F{0, 0, 0, 1}
                               : D2D1_COLOR_F{1, 1, 1, 1};
    } else if (type_ == ButtonType::Primary) {
        // ---- Primary (Accent) Button: WinUI 3 FilledButton ----
        // AccentFillColor: Default / Secondary(hover) / Tertiary(press) / Disabled
        if (!enabled)      bg = dark ? theme::Rgb(0x52, 0x52, 0x52) : theme::Rgb(0xC7, 0xC7, 0xC7);
        else if (pressed)  bg = theme::Current().accentPress;
        else if (hovered)  bg = theme::kAccentHover();
        else               bg = theme::kAccent();

        r.FillRoundedRect(rect, cr, cr, bg);

        // Border: darker accent, 1px
        if (enabled && !pressed) {
            D2D1_COLOR_F border = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.08f) : theme::Rgba(0x00,0x00,0x00,0.16f);
            r.DrawRoundedRect(rect, cr, cr, border, 1.0f);
            // Bottom darker accent edge (elevation)
            D2D1_COLOR_F botEdge = dark ? theme::Rgba(0x00,0x00,0x00,0.40f) : theme::Rgba(0x00,0x00,0x00,0.20f);
            D2D1_RECT_F botLine = {rect.left + 2, rect.bottom - 1.5f, rect.right - 2, rect.bottom - 0.5f};
            r.FillRect(botLine, botEdge);
        }

        // Text: always white on accent background
        if (!enabled)     textColor = theme::Rgba(0xFF,0xFF,0xFF,0.50f);
        else if (pressed) textColor = theme::Rgba(0xFF,0xFF,0xFF,0.80f);
        else              textColor = theme::white;
    } else {
        // ---- Default Button: WinUI 3 Standard Button ----
        // ControlFillColor: Default / Secondary(hover) / Tertiary(press) / Disabled
        if (!enabled)      bg = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.04f) : theme::Rgba(0xF9,0xF9,0xF9,1.0f);
        else if (pressed)  bg = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.03f) : theme::Rgba(0xF9,0xF9,0xF9,0.95f);
        else if (hovered)  bg = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.08f) : theme::Rgba(0xF9,0xF9,0xF9,0.97f);
        else               bg = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.06f) : theme::Rgba(0xFF,0xFF,0xFF,0.95f);

        r.FillRoundedRect(rect, cr, cr, bg);

        // ControlElevationBorder
        D2D1_COLOR_F borderTop = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.09f) : theme::Rgba(0x00,0x00,0x00,0.06f);
        r.DrawRoundedRect(rect, cr, cr, borderTop, 1.0f);
        if (!pressed) {
            D2D1_COLOR_F borderBot = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.16f) : theme::Rgba(0x00,0x00,0x00,0.14f);
            D2D1_RECT_F botLine = {rect.left + 2, rect.bottom - 1.5f, rect.right - 2, rect.bottom - 0.5f};
            r.FillRect(botLine, borderBot);
        }

        // Text
        if (!enabled)     textColor = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.36f) : theme::Rgba(0x00,0x00,0x00,0.36f);
        else if (pressed) textColor = dark ? theme::Rgb(0xC8,0xC8,0xC8) : theme::Rgb(0x5D,0x5D,0x5D);
        else              textColor = theme::kBtnText();
    }

    // Legacy C API path: SetCustomBgColor without CSS → apply auto-darken.
    if (!cssOwnsBg && hasCustomBgColor_) {
        D2D1_COLOR_F cbg = customBgColor_;
        if (pressed) { cbg.r *= 0.8f; cbg.g *= 0.8f; cbg.b *= 0.8f; }
        else if (hovered) { cbg.r *= 0.9f; cbg.g *= 0.9f; cbg.b *= 0.9f; }
        r.FillRoundedRect(rect, cr, cr, cbg);
    }
    if (hasCustomTextColor_) textColor = customTextColor_;
    if (css.hasFg) textColor = css.fg;

    // CSS border override: if set, draw an explicit border on top of whatever
    // the base drew (replacing the implicit elevation line).
    if (css.hasBorderColor || css.borderWidth >= 0) {
        float bw = (css.borderWidth >= 0) ? css.borderWidth : 1.0f;
        if (bw > 0) {
            D2D1_COLOR_F bc = css.hasBorderColor ? css.borderColor
                                                  : (dark ? theme::Rgba(0xFF,0xFF,0xFF,0.16f)
                                                         : theme::Rgba(0x00,0x00,0x00,0.14f));
            r.DrawRoundedRect(rect, cr, cr, bc, bw);
        }
    }

    // Content with WinUI 3 padding: 11,5,11,6
    if (!icon_.empty()) {
        D2D1_RECT_F iconRect = {rect.left + 11, rect.top, rect.left + 31, rect.bottom};
        r.DrawIcon(icon_, iconRect, textColor, 12.0f);
        D2D1_RECT_F textRect = {rect.left + 33, rect.top, rect.right - 11, rect.bottom};
        r.DrawText(text_, textRect, textColor, fontSize_, DWRITE_TEXT_ALIGNMENT_LEADING);
    } else {
        r.DrawText(text_, rect, textColor, fontSize_, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

void ButtonWidget::DoLayout() {
    // Icon-only / pure-content button: <button><svg/></button> with no text.
    // Delegate to HBox layout so CSS align-items / justify-content / gap on
    // the button actually center the children inside its rect.
    //
    // Text-bearing button: OnDraw paints text (+ optional icon glyph) directly
    // into rect. Children — if any were appended ad-hoc — fall back to the
    // base Widget layout, matching legacy behavior.
    //
    // FirstPlainText (in widget_factory) returns the inter-tag whitespace
    // (e.g. "\n  ") for <button>\n  <svg/>\n</button>, so empty() alone isn't
    // enough — treat whitespace-only as no text.
    bool hasText = false;
    for (wchar_t c : text_) {
        if (c != L' ' && c != L'\t' && c != L'\r' && c != L'\n') { hasText = true; break; }
    }
    if (!hasText && !children_.empty()) {
        HBoxWidget::DoLayout();
    } else {
        Widget::DoLayout();
    }
}

bool ButtonWidget::OnMouseMove(const MouseEvent& e) {
    hovered = Contains(e.x, e.y);
    return hovered;
}

bool ButtonWidget::OnMouseDown(const MouseEvent& e) {
    if (Contains(e.x, e.y)) { pressed = true; return true; }
    return false;
}

bool ButtonWidget::OnMouseUp(const MouseEvent& e) {
    bool wasPressed = pressed;
    pressed = false;
    if (wasPressed && Contains(e.x, e.y) && onClick) {
        onClick();
        return true;
    }
    return false;
}

D2D1_SIZE_F ButtonWidget::SizeHint() const {
    // Auto-fit width to actual text. WinUI 3 button has 12px padding on each
    // side; min width 60 keeps short labels clickable. Falls back to a coarse
    // char-count estimate when the renderer isn't available yet (very first
    // pre-paint layout).
    float h = fixedH > 0 ? fixedH : 32.0f;
    if (fixedW > 0) return {fixedW, h};

    constexpr float kHorizPad = 24.0f;   // 12px each side
    constexpr float kMinW     = 60.0f;
    float textW = 0.0f;
    extern MeasureContext* g_activeMeasureContext;
    if (g_activeMeasureContext && !text_.empty()) {
        textW = g_activeMeasureContext->MeasureTextWidth(text_, fontSize_, nullptr);
    } else {
        // Fallback: 0.7×fontSize per char (close enough for ASCII; CJK ends
        // up wide enough on the next frame once the renderer is up).
        textW = fontSize_ * (float)text_.length() * 0.7f;
    }
    float w = std::max(kMinW, textW + kHorizPad);
    return {w, h};
}

// ---- CheckBox ----

void CheckBoxWidget::SetChecked(bool v) {
    if (checked_ == v) return;

    checked_ = v;
    checkAnim_.SetTarget(checked_ ? 1.0f : 0.0f);
    animating_ = checkAnim_.Active();
    ui::GetContext().RegisterAnimatingWidget(this);
}

void CheckBoxWidget::UpdateAnimation() {
    checkAnim_.SetIdleTarget(checked_ ? 1.0f : 0.0f);
    checkAnim_.SampleFrame(GetTickCount64());
    animating_ = checkAnim_.Active();
}

void CheckBoxWidget::OnDraw(Renderer& r) {
    Widget::OnDraw(r);
    bool dark = theme::IsDark();

    float boxSize = 20.0f;
    float cy = (rect.top + rect.bottom) / 2;
    float bx = rect.left + 2;
    float by = cy - boxSize / 2;

    D2D1_RECT_F box = {bx, by, bx + boxSize, by + boxSize};
    float cr = (css.borderRadius >= 0) ? css.borderRadius : theme::radius::medium;
    float fontSize = (css.fontSize > 0) ? css.fontSize : theme::kFontSizeNormal;
    D2D1_COLOR_F accent = css.hasAccent ? css.accent : theme::kAccent();
    D2D1_COLOR_F accentHover = css.hasAccent ? css.accent : theme::kAccentHover();
    D2D1_COLOR_F fg = css.hasFg ? css.fg : theme::kBtnText();

    checkAnim_.SetIdleTarget(checked_ ? 1.0f : 0.0f);
    float t = std::clamp(checkAnim_.SampleFrame(GetTickCount64()), 0.0f, 1.0f);
    animating_ = checkAnim_.Active();

    if (t > 0.01f) {
        D2D1_COLOR_F accentBg = hovered ? accentHover : accent;
        D2D1_COLOR_F uncheckedBg = (bgColor.a > 0) ? bgColor
                                                   : (dark ? theme::Rgba(0xFF,0xFF,0xFF,0.05f)
                                                          : theme::Rgba(0x00,0x00,0x00,0.02f));
        D2D1_COLOR_F blended = {
            uncheckedBg.r + t * (accentBg.r - uncheckedBg.r),
            uncheckedBg.g + t * (accentBg.g - uncheckedBg.g),
            uncheckedBg.b + t * (accentBg.b - uncheckedBg.b),
            uncheckedBg.a + t * (accentBg.a - uncheckedBg.a)
        };
        r.FillRoundedRect(box, cr, cr, blended);

        if (t > 0.3f) {
            float glyphAlpha = std::min(1.0f, (t - 0.3f) / 0.4f);
            D2D1_COLOR_F checkColor = dark ? D2D1_COLOR_F{0,0,0, glyphAlpha}
                                           : D2D1_COLOR_F{1,1,1, glyphAlpha};
            float glyphSize = 12.0f;
            D2D1_RECT_F glyphBox = {
                bx + (boxSize - glyphSize) / 2, by + (boxSize - glyphSize) / 2,
                bx + (boxSize + glyphSize) / 2, by + (boxSize + glyphSize) / 2
            };
            r.DrawIcon(L"\xE73E", glyphBox, checkColor, glyphSize);
        }
    } else {
        D2D1_COLOR_F bg = (bgColor.a > 0) ? bgColor
                                          : (dark ? theme::Rgba(0xFF,0xFF,0xFF,0.05f) : theme::Rgba(0x00,0x00,0x00,0.02f));
        if (hovered && bgColor.a == 0)
            bg = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.08f) : theme::Rgba(0x00,0x00,0x00,0.05f);
        r.FillRoundedRect(box, cr, cr, bg);

        float bw = (css.borderWidth >= 0) ? css.borderWidth : 1.0f;
        if (bw > 0) {
            D2D1_COLOR_F border = css.hasBorderColor ? css.borderColor
                                                      : (dark ? theme::Rgba(0xFF,0xFF,0xFF,0.54f)
                                                             : theme::Rgba(0x00,0x00,0x00,0.45f));
            r.DrawRoundedRect(box, cr, cr, border, bw);
        }
    }

    D2D1_RECT_F labelRect = {bx + boxSize + 8, rect.top, rect.right, rect.bottom};
    r.DrawText(text_, labelRect, fg, fontSize);
}

float CheckBoxWidget::ContentRight_() const {
    float boxSize = 20.0f, gap = 8.0f;
    float textW = theme::kFontSizeNormal * 0.65f * (float)text_.size();
    return rect.left + padL + boxSize + gap + textW + 8.0f;
}

bool CheckBoxWidget::OnMouseUp(const MouseEvent& e) {
    if (e.x >= rect.left && e.x < ContentRight_() && e.y >= rect.top && e.y < rect.bottom) {
        SetChecked(!checked_);
        if (onValueChanged) onValueChanged(checked_);
        return true;
    }
    return false;
}

bool CheckBoxWidget::OnMouseMove(const MouseEvent& e) {
    hovered = e.x >= rect.left && e.x < ContentRight_() && e.y >= rect.top && e.y < rect.bottom;
    return hovered;
}

D2D1_SIZE_F CheckBoxWidget::SizeHint() const {
    float boxSize = 20.0f, gap = 8.0f;
    float textW = theme::kFontSizeNormal * 0.65f * (float)text_.size();
    float w = fixedW > 0 ? fixedW : (boxSize + gap + textW + 8.0f);
    float h = fixedH > 0 ? fixedH : 32.0f;  // WinUI 3: MinHeight=32
    return {w, h};
}

// ---- Separator ----

void SeparatorWidget::OnDraw(Renderer& r) {
    if (vertical_) {
        float cx = (rect.left + rect.right) / 2;
        r.DrawLine(cx, rect.top, cx, rect.bottom, theme::kDivider());
    } else {
        float cy = (rect.top + rect.bottom) / 2;
        r.DrawLine(rect.left, cy, rect.right, cy, theme::kDivider());
    }
}

// ---- Slider ----

void SliderWidget::RetargetThumbAnimation() {
    float target = 0.86f;  // rest
    if (dragging_)     target = 0.71f;   // pressed
    else if (hovered)  target = 1.167f;  // hover

    if (std::abs(target - thumbScaleTarget_) >= 0.001f) {
        thumbScaleTarget_ = target;
        thumbScale_.SetTarget(thumbScaleTarget_);
        ui::GetContext().RegisterAnimatingWidget(this);
    }
}

void SliderWidget::UpdateThumbAnimation() {
    RetargetThumbAnimation();
    thumbScale_.SetIdleTarget(thumbScaleTarget_);
    thumbScale_.SampleFrame(GetTickCount64());
}

void SliderWidget::OnDraw(Renderer& r) {
    Widget::OnDraw(r);
    bool dark = theme::IsDark();

    UpdateThumbAnimation();

    D2D1_COLOR_F accent = css.hasAccent ? css.accent : theme::kAccent();

    float trackH = 4.0f;
    float cy = (rect.top + rect.bottom) / 2;
    float thumbOuterR = 10.0f;
    float trackL = rect.left + thumbOuterR;
    float trackR = rect.right - thumbOuterR;

    // Unfilled track: prefer CSS bgColor if set (theme=users can colorize it).
    D2D1_RECT_F trackBg = {trackL, cy - trackH/2, trackR, cy + trackH/2};
    D2D1_COLOR_F unfilled = (bgColor.a > 0) ? bgColor
                                            : (dark ? theme::Rgba(0xFF,0xFF,0xFF,0.54f)
                                                   : theme::Rgba(0x00,0x00,0x00,0.45f));
    r.FillRoundedRect(trackBg, 2, 2, unfilled);

    float pct = (max_ > min_) ? (value_ - min_) / (max_ - min_) : 0;
    float thumbX = trackL + pct * (trackR - trackL);
    D2D1_RECT_F trackFill = {trackL, cy - trackH/2, thumbX, cy + trackH/2};
    r.FillRoundedRect(trackFill, 2, 2, accent);

    float outerR = 9.0f;
    D2D1_RECT_F thumbOuter = {thumbX - outerR, cy - outerR, thumbX + outerR, cy + outerR};
    D2D1_COLOR_F thumbBg = css.hasFg ? css.fg
                                      : (dark ? theme::Rgb(0x45, 0x45, 0x45) : theme::white);
    r.FillRoundedRect(thumbOuter, outerR, outerR, thumbBg);
    D2D1_COLOR_F thumbBorder = css.hasBorderColor ? css.borderColor
                                                   : (dark ? theme::Rgba(0xFF,0xFF,0xFF,0.09f)
                                                          : theme::Rgba(0x00,0x00,0x00,0.06f));
    r.DrawRoundedRect(thumbOuter, outerR, outerR, thumbBorder, 1.0f);

    float innerBase = 6.0f;
    float innerR = innerBase * thumbScale_.Current();
    D2D1_RECT_F innerDot = {thumbX - innerR, cy - innerR, thumbX + innerR, cy + innerR};
    r.FillRoundedRect(innerDot, innerR, innerR, accent);
}

float SliderWidget::ValueFromX(float x) const {
    float thumbR = 10.0f;  // hit area radius
    float trackL = rect.left + thumbR;
    float trackR = rect.right - thumbR;
    float pct = std::clamp((x - trackL) / (trackR - trackL), 0.0f, 1.0f);
    return min_ + pct * (max_ - min_);
}

bool SliderWidget::OnMouseDown(const MouseEvent& e) {
    if (Contains(e.x, e.y)) {
        dragging_ = true;
        pressed = true;
        RetargetThumbAnimation();
        value_ = ValueFromX(e.x);
        if (onFloatChanged) onFloatChanged(value_);
        return true;
    }
    return false;
}

bool SliderWidget::OnMouseMove(const MouseEvent& e) {
    bool inBounds = Contains(e.x, e.y);
    // Only show hover animation when mouse is near the thumb, not the whole track
    float thumbR = 10.0f;
    float trackL = rect.left + thumbR;
    float trackR = rect.right - thumbR;
    float pct = (max_ > min_) ? (value_ - min_) / (max_ - min_) : 0;
    float thumbX = trackL + pct * (trackR - trackL);
    float cy = (rect.top + rect.bottom) / 2;
    float dx = e.x - thumbX, dy = e.y - cy;
    bool nearThumb = (dx * dx + dy * dy) <= (14.0f * 14.0f);  // 14px radius hit area
    hovered = inBounds && nearThumb;
    RetargetThumbAnimation();

    if (dragging_) {
        value_ = ValueFromX(e.x);
        if (onFloatChanged) onFloatChanged(value_);
        return true;
    }
    return inBounds;
}

bool SliderWidget::OnMouseUp(const MouseEvent& e) {
    if (dragging_) {
        dragging_ = false;
        pressed = false;
        RetargetThumbAnimation();
        return true;
    }
    return false;
}

D2D1_SIZE_F SliderWidget::SizeHint() const {
    return {fixedW > 0 ? fixedW : 150.0f, fixedH > 0 ? fixedH : 26.0f};
}

// ---- Image ----

namespace {

/* 存活的 ImageWidget 实例表 (build 286)。
 *
 * 只为 RetryFailedLoads 服务 —— 否则要为了找几个 <img> 去遍历所有窗口的整棵
 * widget 树。注册表按实例走, 规模等于页面里 <img> 的个数, 通常几十个。
 * 只在 UI 线程增删查, 不加锁。 */
std::unordered_set<ImageWidget*>& LiveImageWidgets() {
    static std::unordered_set<ImageWidget*> s;
    return s;
}

}  // namespace

ImageWidget::ImageWidget() {
    LiveImageWidgets().insert(this);
}

ImageWidget::ImageWidget(const std::string& src) : src_(src) {
    LiveImageWidgets().insert(this);
}

ImageWidget::~ImageWidget() {
    LiveImageWidgets().erase(this);
    ClearImageResource();
}

void ImageWidget::RetryFailedLoads(const std::string& name) {
    if (name.empty()) return;
    bool any = false;
    for (ImageWidget* w : LiveImageWidgets()) {
        if (!w || !w->loadFailed_ || w->src_ != name) continue;
        w->loadFailed_ = false;   /* 下一帧 OnDraw 会重新 Resolve */
        any = true;
    }
    if (any) GetContext().RequestInvalidateAll();
}

void ImageWidget::ClearImageResource() {
    if (imageResourceKey_.IsValid()) {
        GlobalResourceStore().Remove(imageResourceKey_);
        imageResourceKey_ = {};
    }
    bitmap_.Reset();
    intrinsicW_ = 0.0f;
    intrinsicH_ = 0.0f;
}

void ImageWidget::SetSrc(const std::string& src) {
    if (src_ == src) return;
    ClearImageResource();
    src_ = src;
    loadFailed_ = false;
}

D2D1_SIZE_F ImageWidget::SizeHint() const {
    /* CSS 给了显式 width/height 就用它（HBox/VBox 已经按这个排）。否则
     * 用图片自身的固有尺寸；还没 lazy-load 之前先返回一个非 0 占位避免
     * SizeHint 被父级当 0 折叠掉。 */
    float w = fixedW > 0 ? fixedW : (intrinsicW_ > 0 ? intrinsicW_ : 16.0f);
    float h = fixedH > 0 ? fixedH : (intrinsicH_ > 0 ? intrinsicH_ : 16.0f);
    return {w, h};
}

void ImageWidget::OnDraw(Renderer& r) {
    Widget::OnDraw(r);

    /* lazy 加载。loadFailed_ 防止每帧重试。 */
    if (!imageResourceKey_.IsValid() && !loadFailed_ && !src_.empty()) {
        const void* bytes = nullptr;
        size_t size = 0;
        ui::asset::DataOwnerPtr owner;
        if (ui::asset::Resolve(src_, &bytes, &size, &owner)) {
            std::vector<uint8_t> pixels;
            int width = 0, height = 0, stride = 0;
            if (r.DecodeImageBytesToBgraPremul(bytes, size, pixels, width, height, stride)) {
                const uint64_t nextGeneration = imageGeneration_ + 1;
                ResourceKey key = GlobalResourceStore().AddImage(
                    ResourceKind::Bitmap, nextGeneration, width, height, stride,
                    PixelFormat::BgraPremul, pixels.data(), true);
                if (key.IsValid()) {
                    imageResourceKey_ = key;
                    imageGeneration_ = nextGeneration;
                    intrinsicW_ = static_cast<float>(width);
                    intrinsicH_ = static_cast<float>(height);
                }
            }
        }
        if (!imageResourceKey_.IsValid()) {
            loadFailed_ = true;   // 不再重试，避免每帧 IO
        }
    }
    if (!imageResourceKey_.IsValid()) return;

    if (!bitmap_ && r.RT()) {
        auto res = GlobalResourceStore().Acquire(imageResourceKey_);
        if (res && res->bytes) {
            bitmap_ = r.CreateBitmapFromPixels(res->bytes->data(), res->width, res->height, res->stride);
        }
    }

    float bw = intrinsicW_;
    float bh = intrinsicH_;
    if (bw <= 0 || bh <= 0) return;

    float rw = rect.right - rect.left;
    float rh = rect.bottom - rect.top;
    if (rw <= 0 || rh <= 0) return;

    D2D1_RECT_F dst;
    if (fit_ == Fit::None) {
        /* 不缩放，左上角对齐，超出部分裁剪 */
        dst = { rect.left, rect.top, rect.left + bw, rect.top + bh };
        r.PushClip(rect);
        r.RecordImage(imageResourceKey_, dst, ImageSampling::Linear);
        if (bitmap_) r.DrawBitmap(bitmap_.Get(), dst);
        r.PopClip();
        return;
    }
    if (fit_ == Fit::Fill) {
        dst = rect;
    } else {
        /* Contain / Cover：保持纵横比 */
        float scaleX = rw / bw;
        float scaleY = rh / bh;
        float scale  = (fit_ == Fit::Cover) ? std::max(scaleX, scaleY)
                                            : std::min(scaleX, scaleY);
        float dw = bw * scale;
        float dh = bh * scale;
        float dx = rect.left + (rw - dw) * 0.5f;
        float dy = rect.top  + (rh - dh) * 0.5f;
        dst = { dx, dy, dx + dw, dy + dh };
    }
    r.PushClip(rect);
    r.RecordImage(imageResourceKey_, dst, ImageSampling::Linear);
    if (bitmap_) r.DrawBitmap(bitmap_.Get(), dst);
    r.PopClip();
}

// ---- TextInput ----

UINT TextInputWidget::EffectiveCaretBlinkMs() {
    UINT blinkMs = GetCaretBlinkTime();
    if (blinkMs == 0 || blinkMs == INFINITE) blinkMs = 530;
    constexpr double kBlinkSlowdown = 1.7;
    return static_cast<UINT>(blinkMs * kBlinkSlowdown);
}

void TextInputWidget::ResetCaretBlink() {
    caretBlinkStartTick_ = static_cast<uint64_t>(GetTickCount64());
}

bool TextInputWidget::ShouldShowCaret() const {
    UINT blinkMs = EffectiveCaretBlinkMs();
    if (caretBlinkStartTick_ == 0) return true;
    uint64_t now = static_cast<uint64_t>(GetTickCount64());
    uint64_t elapsed = now - caretBlinkStartTick_;
    return ((elapsed / blinkMs) % 2ULL) == 0ULL;
}

bool TextInputWidget::GetCaretRect(D2D1_RECT_F* out) const {
    extern MeasureContext* g_activeMeasureContext;
    if (!g_activeMeasureContext) return false;
    float fontSize = (css.fontSize > 0) ? css.fontSize : theme::kFontSizeNormal;
    int pos = std::min(std::max(cursorPos_, 0), (int)text_.size());
    float x = rect.left + 11.0f - scrollX_ +
              g_activeMeasureContext->MeasureTextWidth(text_.substr(0, pos),
                                                       fontSize);
    *out = {x, rect.top + 5, x + 1.5f, rect.bottom - 6};
    return true;
}

int TextInputWidget::CharIndexFromX(MeasureContext& measure, float x) const {
    float fontSize = (css.fontSize > 0) ? css.fontSize : theme::kFontSizeNormal;
    float textX = rect.left + 11.0f - scrollX_;
    if (x <= textX) return 0;

    for (int i = 0; i <= (int)text_.size(); i++) {
        std::wstring substr = text_.substr(0, i);
        float w = measure.MeasureTextWidth(substr, fontSize, nullptr);
        float charX = textX + w;
        if (i < (int)text_.size()) {
            std::wstring nextSubstr = text_.substr(0, i + 1);
            float nextW = measure.MeasureTextWidth(nextSubstr, fontSize, nullptr);
            float nextCharX = textX + nextW;
            if (x < (charX + nextCharX) / 2.0f) return i;
        } else {
            return i;
        }
    }
    return (int)text_.size();
}

void TextInputWidget::DeleteSelection() {
    if (!HasSelection()) return;
    int start = std::min(selectionStart_, selectionEnd_);
    int end = std::max(selectionStart_, selectionEnd_);
    text_.erase(start, end - start);
    cursorPos_ = start;
    ClearSelection();
}

void TextInputWidget::EnsureCursorVisible(MeasureContext& measure) {
    float fontSize = (css.fontSize > 0) ? css.fontSize : theme::kFontSizeNormal;
    float textAreaW = rect.right - rect.left - 22.0f;  // 11px padding each side
    std::wstring before = text_.substr(0, cursorPos_);
    float cursorX = measure.MeasureTextWidth(before, fontSize, nullptr);

    if (cursorX - scrollX_ < 0) scrollX_ = cursorX;
    else if (cursorX - scrollX_ > textAreaW) scrollX_ = cursorX - textAreaW;
    if (scrollX_ < 0) scrollX_ = 0;
}

std::wstring TextInputWidget::GetSelectedText() const {
    if (!HasSelection()) return L"";
    int start = std::min(selectionStart_, selectionEnd_);
    int end = std::max(selectionStart_, selectionEnd_);
    return text_.substr(start, end - start);
}

void TextInputWidget::SetClipboardText(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (hMem) {
        wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
        if (pMem) {
            wcscpy_s(pMem, text.size() + 1, text.c_str());
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }
    CloseClipboard();
}

std::wstring TextInputWidget::GetClipboardText() {
    if (!OpenClipboard(nullptr)) return L"";
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) { CloseClipboard(); return L""; }
    wchar_t* pData = (wchar_t*)GlobalLock(hData);
    std::wstring result = pData ? pData : L"";
    GlobalUnlock(hData);
    CloseClipboard();
    return result;
}

void TextInputWidget::OnDraw(Renderer& r) {
    bool dark = theme::IsDark();
    float cr = (css.borderRadius >= 0) ? css.borderRadius : theme::radius::medium;
    float fontSize = (css.fontSize > 0) ? css.fontSize : theme::kFontSizeNormal;
    D2D1_COLOR_F accent = css.hasAccent ? css.accent : theme::kAccent();
    D2D1_COLOR_F fg = css.hasFg ? css.fg : theme::kBtnText();

    // Background: if CSS gave us a bgColor use it as-is (no state variation);
    // otherwise fall back to the WinUI 3 state machine.
    bool customBg = bgColor.a > 0;
    D2D1_COLOR_F bg;
    if (customBg) {
        bg = bgColor;
    } else {
        if (!enabled)     bg = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.03f) : theme::Rgba(0xF0,0xF0,0xF0,0.90f);
        else if (focused) bg = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.03f) : theme::Rgba(0xFF,0xFF,0xFF,1.0f);
        else if (hovered) bg = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.08f) : theme::Rgba(0xF9,0xF9,0xF9,0.97f);
        else              bg = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.06f) : theme::Rgba(0xFF,0xFF,0xFF,0.95f);
    }
    r.FillRoundedRect(rect, cr, cr, bg);

    // Border: if user set border-color or border-width, honor literally;
    // otherwise keep the default 2-tone WinUI border + focus accent.
    bool customBorder = css.hasBorderColor || css.borderWidth >= 0;
    if (customBorder) {
        float bw = (css.borderWidth >= 0) ? css.borderWidth : 1.0f;
        if (bw > 0) {
            D2D1_COLOR_F bc = css.hasBorderColor ? css.borderColor
                                                  : (dark ? theme::Rgba(0xFF,0xFF,0xFF,0.20f)
                                                         : theme::Rgba(0x00,0x00,0x00,0.20f));
            r.DrawRoundedRect(rect, cr, cr, bc, bw);
        }
    } else {
        D2D1_COLOR_F borderTop = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.08f) : theme::Rgba(0x00,0x00,0x00,0.06f);
        r.DrawRoundedRect(rect, cr, cr, borderTop, 1.0f);
        if (focused) {
            D2D1_RECT_F bottomLine = {rect.left + 1, rect.bottom - 2, rect.right - 1, rect.bottom};
            r.FillRect(bottomLine, accent);
        } else {
            D2D1_COLOR_F borderBot = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.54f) : theme::Rgba(0x00,0x00,0x00,0.45f);
            D2D1_RECT_F botLine = {rect.left + 2, rect.bottom - 1.0f, rect.right - 2, rect.bottom};
            r.FillRect(botLine, borderBot);
        }
    }

    D2D1_RECT_F textArea = {rect.left + 11, rect.top + 5, rect.right - 11, rect.bottom - 6};
    r.PushClip(textArea);

    if (text_.empty()) {
        D2D1_COLOR_F phColor;
        if (css.hasPlaceholderColor) phColor = css.placeholderColor;
        else phColor = focused
            ? (dark ? theme::Rgba(0xFF,0xFF,0xFF,0.38f) : theme::Rgba(0x00,0x00,0x00,0.38f))
            : theme::kContentText();
        r.DrawText(placeholder_, textArea, phColor, fontSize);
    }

    D2D1_COLOR_F caret = css.hasCaretColor ? css.caretColor : accent;

    if (!text_.empty()) {
        float textX = rect.left + 11.0f - scrollX_;
        D2D1_RECT_F drawArea = {textX, rect.top, textX + 10000, rect.bottom};
        bool selDrawn = false;

        if (HasSelection()) {
            int start = std::min(selectionStart_, selectionEnd_);
            int end = std::max(selectionStart_, selectionEnd_);
            std::wstring beforeSel = text_.substr(0, start);
            std::wstring selected = text_.substr(start, end - start);
            float x1 = textX + r.MeasureTextWidth(beforeSel, fontSize, nullptr);
            float x2 = x1 + r.MeasureTextWidth(selected, fontSize, nullptr);
            D2D1_RECT_F selRect = {x1, rect.top + 5, x2, rect.bottom - 6};

            // 选区色：focused → CSS active / theme accent；
            //         !focused → CSS inactive / 半透明灰（Windows 标准）
            D2D1_COLOR_F selBg;
            if (focused) {
                selBg = css.hasSelTextBg ? css.selTextBg : accent;
            } else {
                selBg = css.hasSelTextBgInactive ? css.selTextBgInactive
                      : (dark ? theme::Rgba(0xFF, 0xFF, 0xFF, 0.18f)
                              : theme::Rgba(0x00, 0x00, 0x00, 0.18f));
            }
            r.FillRect(selRect, selBg);

            // 选中文字色：focused → CSS active / 默认白色（配 accent 背景）；
            //             !focused → CSS inactive / 保持原 fg
            D2D1_COLOR_F selFg = fg;
            bool customSelFg = false;
            if (focused) {
                customSelFg = true;
                selFg = css.hasSelTextFg ? css.selTextFg : theme::white;
            } else if (css.hasSelTextFgInactive) {
                customSelFg = true;
                selFg = css.selTextFgInactive;
            }

            if (customSelFg) {
                // 选区前 / 选区内 / 选区后 各 push 一段 clip 画对应颜色
                r.PushClip({rect.left, rect.top, x1, rect.bottom});
                r.DrawText(text_, drawArea, fg, fontSize);
                r.PopClip();
                r.PushClip({x1, rect.top, x2, rect.bottom});
                r.DrawText(text_, drawArea, selFg, fontSize);
                r.PopClip();
                r.PushClip({x2, rect.top, rect.right, rect.bottom});
                r.DrawText(text_, drawArea, fg, fontSize);
                r.PopClip();
                selDrawn = true;
            }
        }

        if (!selDrawn) {
            r.DrawText(text_, drawArea, fg, fontSize);
        }

        if (focused && !HasSelection() && ShouldShowCaret()) {
            std::wstring before = text_.substr(0, cursorPos_);
            float textWidth = r.MeasureTextWidth(before, fontSize, nullptr);
            float cursorX = textX + textWidth;
            float cy1 = rect.top + 5, cy2 = rect.bottom - 5;
            r.DrawLine(cursorX, cy1, cursorX, cy2, caret, 1.5f);
        }
    }

    if (text_.empty() && focused && ShouldShowCaret()) {
        float cursorX = rect.left + 11.0f;
        float cy1 = rect.top + 5, cy2 = rect.bottom - 5;
        r.DrawLine(cursorX, cy1, cursorX, cy2, caret, 1.5f);
    }

    r.PopClip();
    MeasureContext paintMeasure;
    paintMeasure.BindRenderer(&r);
    EnsureCursorVisible(paintMeasure);
}

bool TextInputWidget::OnMouseDown(const MouseEvent& e) {
    if (!enabled) return false;
    bool wasFocused = focused;
    focused = Contains(e.x, e.y);
    if (focused) {
        extern MeasureContext* g_activeMeasureContext;
        if (g_activeMeasureContext) {
            cursorPos_ = CharIndexFromX(*g_activeMeasureContext, e.x);
        }
        if (GetKeyState(VK_SHIFT) & 0x8000) {
            if (selectionStart_ < 0) selectionStart_ = cursorPos_;
            selectionEnd_ = cursorPos_;
        } else {
            ClearSelection();
            dragging_ = true;
            selectionStart_ = cursorPos_;
        }
        ResetCaretBlink();
    } else {
        ClearSelection();
    }
    return focused;
}

bool TextInputWidget::OnMouseMove(const MouseEvent& e) {
    hovered = Contains(e.x, e.y);
    if (dragging_ && focused) {
        extern MeasureContext* g_activeMeasureContext;
        if (g_activeMeasureContext) {
            cursorPos_ = CharIndexFromX(*g_activeMeasureContext, e.x);
        }
        selectionEnd_ = cursorPos_;
        return true;
    }
    return hovered;
}

bool TextInputWidget::OnMouseUp(const MouseEvent& e) {
    if (dragging_) {
        dragging_ = false;
        if (selectionStart_ == selectionEnd_) ClearSelection();
        return true;
    }
    return false;
}

bool TextInputWidget::OnKeyChar(wchar_t ch) {
    if (!focused || !enabled) return false;
    if (readOnly) return true;  // consume but don't modify

    if (ch == '\b') {
        if (HasSelection()) {
            DeleteSelection();
        } else if (cursorPos_ > 0) {
            text_.erase(cursorPos_ - 1, 1);
            cursorPos_--;
        }
    } else if (ch >= 32) {
        if (inputFilter && !inputFilter(ch)) return true;
        if (maxLength >= 0 && (int)text_.size() >= maxLength) return true;

        if (HasSelection()) DeleteSelection();
        text_.insert(cursorPos_, 1, ch);
        cursorPos_++;
    }

    ResetCaretBlink();
    extern MeasureContext* g_activeMeasureContext;
    if (g_activeMeasureContext) EnsureCursorVisible(*g_activeMeasureContext);
    if (onTextChanged) onTextChanged(text_);
    return true;
}

bool TextInputWidget::OnKeyDown(int vk) {
    if (!focused || !enabled) return false;

    // ENTER fires the form-submit hook (Vue's `@submit.prevent`-ish — single-
    // line input has no use for a literal newline, so submit takes priority).
    if (vk == VK_RETURN) {
        if (onSubmitHook) onSubmitHook();
        return true;
    }

    /* build 288: ESC 不是编辑键 —— 输入框对它没有任何行为, 却因为本函数末尾
     * 无条件 return true 把它吞了, 于是宿主的窗口级 on_key 在焦点落在输入框上
     * 时永远等不到 Esc。Windows 惯例里 Esc 是"取消当前编辑 / 关掉这个框",
     * 那是宿主的事, 该让它冒泡上去。行内编辑 (列表里放输入框, Enter 提交 /
     * Esc 放弃) 缺的就是这一条。 */
    if (vk == VK_ESCAPE) return false;

    bool shift = GetKeyState(VK_SHIFT) & 0x8000;
    bool ctrl = GetKeyState(VK_CONTROL) & 0x8000;

    // Clipboard operations
    if (ctrl && vk == 'C') {
        if (HasSelection()) SetClipboardText(GetSelectedText());
        return true;
    }
    if (ctrl && vk == 'X') {
        if (readOnly) return true;
        if (HasSelection()) {
            SetClipboardText(GetSelectedText());
            DeleteSelection();
        }
        return true;
    }
    if (ctrl && vk == 'V') {
        if (readOnly) return true;
        std::wstring paste = GetClipboardText();
        if (!paste.empty()) {
            if (HasSelection()) DeleteSelection();
            if (maxLength < 0 || (int)(text_.size() + paste.size()) <= maxLength) {
                text_.insert(cursorPos_, paste);
                cursorPos_ += (int)paste.size();
            }
        }
        ResetCaretBlink();
        extern MeasureContext* g_activeMeasureContext;
        if (g_activeMeasureContext) EnsureCursorVisible(*g_activeMeasureContext);
        return true;
    }
    if (ctrl && vk == 'A') {
        selectionStart_ = 0;
        selectionEnd_ = (int)text_.size();
        cursorPos_ = selectionEnd_;
        return true;
    }

    int oldPos = cursorPos_;

    if (vk == 0x25 /*VK_LEFT*/ && cursorPos_ > 0) cursorPos_--;
    else if (vk == 0x27 /*VK_RIGHT*/ && cursorPos_ < (int)text_.size()) cursorPos_++;
    else if (vk == 0x24 /*VK_HOME*/) cursorPos_ = 0;
    else if (vk == 0x23 /*VK_END*/) cursorPos_ = (int)text_.size();
    else if (vk == 0x2E /*VK_DELETE*/ && !readOnly) {
        if (HasSelection()) DeleteSelection();
        else if (cursorPos_ < (int)text_.size()) text_.erase(cursorPos_, 1);
    }

    if (shift && (vk == 0x25 || vk == 0x27 || vk == 0x24 || vk == 0x23)) {
        if (selectionStart_ < 0) selectionStart_ = oldPos;
        selectionEnd_ = cursorPos_;
    } else if (vk == 0x25 || vk == 0x27 || vk == 0x24 || vk == 0x23) {
        ClearSelection();
    }

    ResetCaretBlink();
    extern MeasureContext* g_activeMeasureContext;
    if (g_activeMeasureContext) EnsureCursorVisible(*g_activeMeasureContext);
    if ((vk == 0x2E || (ctrl && vk == 'V') || (ctrl && vk == 'X')) && onTextChanged)
        onTextChanged(text_);
    return true;
}

D2D1_SIZE_F TextInputWidget::SizeHint() const {
    return {fixedW > 0 ? fixedW : 200.0f, fixedH > 0 ? fixedH : 32.0f};  // WinUI 3: MinHeight=32
}

// ---- TextArea ----

UINT TextAreaWidget::EffectiveCaretBlinkMs() {
    return TextInputWidget::EffectiveCaretBlinkMs();
}

bool TextAreaWidget::GetCaretRect(D2D1_RECT_F* out) const {
    extern MeasureContext* g_activeMeasureContext;
    if (!g_activeMeasureContext) return false;
    float fontSize = (css.fontSize > 0) ? css.fontSize : theme::kFontSizeNormal;
    if (!EnsureLayout(*g_activeMeasureContext, fontSize)) return false;
    float x = 0, y = 0, h = fontSize * 1.3f;
    if (!CaretXYForPos(cursorPos_, x, y, h)) return false;
    *out = {x, y, x + 1.5f, y + h};
    return true;
}

void TextAreaWidget::ResetCaretBlink() {
    caretBlinkStartTick_ = static_cast<uint64_t>(GetTickCount64());
}

bool TextAreaWidget::ShouldShowCaret() const {
    UINT blinkMs = EffectiveCaretBlinkMs();
    if (caretBlinkStartTick_ == 0) return true;
    uint64_t now = static_cast<uint64_t>(GetTickCount64());
    uint64_t elapsed = now - caretBlinkStartTick_;
    return ((elapsed / blinkMs) % 2ULL) == 0ULL;
}

float TextAreaWidget::ContentHeight() const {
    if (cachedLayout_) {
        DWRITE_TEXT_METRICS m{};
        if (SUCCEEDED(cachedLayout_->GetMetrics(&m))) {
            return std::max(layoutLineHeight_, m.height) + 2 * kPad;
        }
    }
    // Fallback before first paint: estimate by newline count × est line height.
    int nl = 1;
    for (wchar_t c : text_) if (c == L'\n') nl++;
    return nl * layoutLineHeight_ + 2 * kPad;
}

float TextAreaWidget::VisibleHeight() const {
    return rect.bottom - rect.top;
}

bool TextAreaWidget::NeedsScrollbar() const {
    return ContentHeight() > VisibleHeight();
}

void TextAreaWidget::ClampScroll() {
    float maxScroll = std::max(0.0f, ContentHeight() - VisibleHeight());
    scrollY_ = std::clamp(scrollY_, 0.0f, maxScroll);
}

D2D1_RECT_F TextAreaWidget::ThumbRect() const {
    float visH = VisibleHeight();
    float contH = ContentHeight();
    if (contH <= visH) return {};

    float trackTop = rect.top + 4;
    float trackBot = rect.bottom - 4;
    float trackH = trackBot - trackTop;

    float thumbH = std::max(20.0f, trackH * (visH / contH));
    float maxScroll = contH - visH;
    float ratio = (maxScroll > 0) ? (scrollY_ / maxScroll) : 0;
    float thumbTop = trackTop + ratio * (trackH - thumbH);

    return {rect.right - kScrollBarWidth - 2, thumbTop,
            rect.right - 2, thumbTop + thumbH};
}

void TextAreaWidget::SetText(const std::wstring& t) {
    text_ = t;
    cursorPos_ = (int)t.size();
    selectionStart_ = selectionEnd_ = -1;
    layoutDirty_ = true;
    ResetCaretBlink();
}

// ----- Layout-driven helpers -----
// Single IDWriteTextLayout shared by hit-test + caret + selection + draw.
// Re-built when text / max-width / fontSize / wrap mode change.

IDWriteTextLayout* TextAreaWidget::EnsureLayout(MeasureContext& measure, float fontSize) const {
    float maxW = (rect.right - rect.left) - 2 * kPad;
    if (maxW <= 0) maxW = 1;
    // When wrap is off, give layout effectively-unbounded width so each
    // line stays single-line; clipping is handled by the parent rect.
    float layoutW = wrap_ ? maxW : 1e6f;

    bool needRebuild = layoutDirty_
                    || !cachedLayout_
                    || layoutText_     != text_
                    || layoutMaxW_     != layoutW
                    || layoutFontSize_ != fontSize
                    || layoutWrap_     != wrap_;

    if (!needRebuild) return cachedLayout_.Get();

    // Empty text → single space so we still get line metrics for caret height.
    std::wstring src = text_.empty() ? std::wstring(L" ") : text_;
    cachedLayout_ = measure.CreateTextLayout(src, layoutW, 1e6f, fontSize, wrap_);
    if (!cachedLayout_) return nullptr;

    // Sample a baseline line height from line metrics.
    UINT32 lineCount = 0;
    cachedLayout_->GetLineMetrics(nullptr, 0, &lineCount);
    if (lineCount > 0) {
        std::vector<DWRITE_LINE_METRICS> lines(lineCount);
        cachedLayout_->GetLineMetrics(lines.data(), lineCount, &lineCount);
        layoutLineHeight_ = lines[0].height > 0 ? lines[0].height : (fontSize + 6.0f);
    } else {
        layoutLineHeight_ = fontSize + 6.0f;
    }

    layoutText_     = text_;
    layoutMaxW_     = layoutW;
    layoutFontSize_ = fontSize;
    layoutWrap_     = wrap_;
    layoutDirty_    = false;
    return cachedLayout_.Get();
}

int TextAreaWidget::HitTestPosFromXY(float x, float y) const {
    if (!cachedLayout_) return cursorPos_;
    float lx = x - (rect.left + kPad);
    float ly = y - (rect.top + kPad - scrollY_);
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS m{};
    if (FAILED(cachedLayout_->HitTestPoint(lx, ly, &trailing, &inside, &m))) {
        return cursorPos_;
    }
    int pos = (int)(m.textPosition + (trailing ? m.length : 0));
    if (pos < 0) pos = 0;
    if (pos > (int)text_.size()) pos = (int)text_.size();
    return pos;
}

bool TextAreaWidget::CaretXYForPos(int pos, float& outX, float& outY, float& outH) const {
    if (!cachedLayout_) return false;
    if (pos < 0) pos = 0;
    if (pos > (int)text_.size()) pos = (int)text_.size();
    DWRITE_HIT_TEST_METRICS m{};
    float lx = 0, ly = 0;
    HRESULT hr = cachedLayout_->HitTestTextPosition(
        (UINT32)pos, FALSE /*not trailing — caret on left edge of pos*/,
        &lx, &ly, &m);
    if (FAILED(hr)) return false;
    outX = (rect.left + kPad) + lx;
    outY = (rect.top + kPad - scrollY_) + ly;
    outH = m.height > 0 ? m.height : layoutLineHeight_;
    return true;
}

int TextAreaWidget::PosUp(int pos) const {
    if (!cachedLayout_) return pos;
    float x, y, h;
    if (!CaretXYForPos(pos, x, y, h)) return pos;
    float targetY = y - h * 0.5f;
    if (targetY < (rect.top + kPad - scrollY_)) return 0;
    return HitTestPosFromXY(x, targetY);
}

int TextAreaWidget::PosDown(int pos) const {
    if (!cachedLayout_) return pos;
    float x, y, h;
    if (!CaretXYForPos(pos, x, y, h)) return pos;
    float targetY = y + h * 1.5f;
    return HitTestPosFromXY(x, targetY);
}

int TextAreaWidget::PosLineStart(int pos) const {
    if (!cachedLayout_) return pos;
    float x, y, h;
    if (!CaretXYForPos(pos, x, y, h)) return pos;
    return HitTestPosFromXY(rect.left + kPad, y + h * 0.5f);
}

int TextAreaWidget::PosLineEnd(int pos) const {
    if (!cachedLayout_) return pos;
    float x, y, h;
    if (!CaretXYForPos(pos, x, y, h)) return pos;
    return HitTestPosFromXY(rect.right + 1e6f, y + h * 0.5f);
}

// (Legacy GetLines / GetLineCol / GetPosFromLineCol / CharIndexFromXY removed —
//  superseded by IDWriteTextLayout-based EnsureLayout / HitTestPosFromXY /
//  CaretXYForPos / Pos*. The layout knows about wrapping, kerning and
//  shaping, so click position and caret position now match exactly.)

void TextAreaWidget::DeleteSelection() {
    if (!HasSelection()) return;
    int start = std::min(selectionStart_, selectionEnd_);
    int end = std::max(selectionStart_, selectionEnd_);
    text_.erase(start, end - start);
    cursorPos_ = start;
    ClearSelection();
    layoutDirty_ = true;
}

void TextAreaWidget::EnsureCursorVisible(MeasureContext* measure) {
    if (measure) {
        float fontSize = (css.fontSize > 0) ? css.fontSize : theme::kFontSizeNormal;
        EnsureLayout(*measure, fontSize);
    }
    if (!cachedLayout_) return;
    float cx, cy, ch;
    if (!CaretXYForPos(cursorPos_, cx, cy, ch)) return;
    // cy is in widget coords already (origin shifted by scrollY_); convert
    // to "in-content" Y by adding back scrollY_ and subtracting layout origin.
    float contentY = (cy + scrollY_) - (rect.top + kPad);
    float viewH = (rect.bottom - rect.top) - 2 * kPad;
    if (contentY < scrollY_) scrollY_ = contentY;
    else if (contentY + ch > scrollY_ + viewH) scrollY_ = contentY + ch - viewH;
    if (scrollY_ < 0) scrollY_ = 0;
}

std::wstring TextAreaWidget::GetSelectedText() const {
    if (!HasSelection()) return L"";
    int start = std::min(selectionStart_, selectionEnd_);
    int end = std::max(selectionStart_, selectionEnd_);
    return text_.substr(start, end - start);
}

void TextAreaWidget::SetClipboardText(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (hMem) {
        wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
        if (pMem) {
            wcscpy_s(pMem, text.size() + 1, text.c_str());
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }
    CloseClipboard();
}

std::wstring TextAreaWidget::GetClipboardText() {
    if (!OpenClipboard(nullptr)) return L"";
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) { CloseClipboard(); return L""; }
    wchar_t* pData = (wchar_t*)GlobalLock(hData);
    std::wstring result = pData ? pData : L"";
    GlobalUnlock(hData);
    CloseClipboard();
    return result;
}

void TextAreaWidget::OnDraw(Renderer& r) {
    bool dark = theme::IsDark();
    float cr = (css.borderRadius >= 0) ? css.borderRadius : theme::radius::medium;
    float fontSize = (css.fontSize > 0) ? css.fontSize : theme::kFontSizeNormal;
    D2D1_COLOR_F accent = css.hasAccent ? css.accent : theme::kAccent();
    D2D1_COLOR_F fg = css.hasFg ? css.fg : theme::kBtnText();

    bool customBg = bgColor.a > 0;
    D2D1_COLOR_F bg;
    if (customBg) {
        bg = bgColor;
    } else {
        if (focused)      bg = dark ? theme::Rgb(0x24, 0x24, 0x24) : theme::Rgb(0xFF, 0xFF, 0xFF);
        else if (hovered) bg = dark ? theme::Rgb(0x44, 0x44, 0x44) : theme::Rgb(0xFB, 0xFB, 0xFB);
        else              bg = dark ? theme::Rgb(0x3E, 0x3E, 0x3E) : theme::Rgb(0xFF, 0xFF, 0xFF);
    }
    r.FillRoundedRect(rect, cr, cr, bg);

    bool customBorder = css.hasBorderColor || css.borderWidth >= 0;
    if (customBorder) {
        float bw = (css.borderWidth >= 0) ? css.borderWidth : 1.0f;
        if (bw > 0) {
            D2D1_COLOR_F bc = css.hasBorderColor ? css.borderColor
                                                  : (dark ? theme::Rgb(0x42,0x42,0x42) : theme::Rgb(0xE8,0xE8,0xE8));
            r.DrawRoundedRect(rect, cr, cr, bc, bw);
        }
    } else {
        D2D1_COLOR_F border = dark ? theme::Rgb(0x42, 0x42, 0x42) : theme::Rgb(0xE8, 0xE8, 0xE8);
        r.DrawRoundedRect(rect, cr, cr, border, 0.5f);
        if (focused) {
            D2D1_RECT_F bottomLine = {rect.left + 1, rect.bottom - 2, rect.right - 1, rect.bottom};
            r.FillRect(bottomLine, accent);
        }
    }

    D2D1_RECT_F textArea = {rect.left + kPad, rect.top + kPad,
                             rect.right - kPad, rect.bottom - kPad};
    r.PushClip(textArea);

    if (text_.empty()) {
        D2D1_COLOR_F phColor;
        if (css.hasPlaceholderColor) phColor = css.placeholderColor;
        else phColor = focused
            ? (dark ? theme::Rgba(0xFF,0xFF,0xFF,0.38f) : theme::Rgba(0x00,0x00,0x00,0.38f))
            : theme::kContentText();
        r.DrawText(placeholder_, textArea, phColor, fontSize,
                   DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_FONT_WEIGHT_NORMAL,
                   DWRITE_PARAGRAPH_ALIGNMENT_NEAR, wrap_);
        // 空文本时也要画 caret，否则用户点击后看不到光标在哪
        if (focused && ShouldShowCaret()) {
            D2D1_COLOR_F caret = css.hasCaretColor ? css.caretColor : accent;
            float cursorX = rect.left + kPad;
            float cy1 = rect.top + kPad + 2.0f;
            float cy2 = cy1 + (fontSize + 2.0f);
            r.DrawLine(cursorX, cy1, cursorX, cy2, caret, 1.5f);
        }
    } else {
        MeasureContext paintMeasure;
        paintMeasure.BindRenderer(&r);
        IDWriteTextLayout* layout = EnsureLayout(paintMeasure, fontSize);
        if (layout) {
            float originX = rect.left + kPad;
            float originY = rect.top + kPad - scrollY_;

            // ---- Selection background + per-line glyph rects (HitTestTextRange) ----
            std::vector<DWRITE_HIT_TEST_METRICS> selHits;
            bool hasSelFg = false;
            D2D1_COLOR_F selFg = fg;
            if (HasSelection()) {
                int start = std::min(selectionStart_, selectionEnd_);
                int end   = std::max(selectionStart_, selectionEnd_);
                UINT32 rangeStart  = (UINT32)start;
                UINT32 rangeLength = (UINT32)(end - start);

                UINT32 actualCount = 0;
                layout->HitTestTextRange(rangeStart, rangeLength,
                                         originX, originY,
                                         nullptr, 0, &actualCount);
                if (actualCount > 0) {
                    selHits.resize(actualCount);
                    layout->HitTestTextRange(rangeStart, rangeLength,
                                             originX, originY,
                                             selHits.data(), actualCount, &actualCount);
                    D2D1_COLOR_F selBg;
                    if (focused) {
                        selBg = css.hasSelTextBg ? css.selTextBg : accent;
                        // Default selected text color = white (matches TextInput &
                        // browser convention on accent-colored selection bg).
                        selFg = css.hasSelTextFg ? css.selTextFg : theme::white;
                        hasSelFg = true;
                    } else {
                        selBg = css.hasSelTextBgInactive ? css.selTextBgInactive
                              : (dark ? theme::Rgba(0xFF, 0xFF, 0xFF, 0.18f)
                                      : theme::Rgba(0x00, 0x00, 0x00, 0.18f));
                        if (css.hasSelTextFgInactive) { selFg = css.selTextFgInactive; hasSelFg = true; }
                    }
                    for (UINT32 i = 0; i < actualCount; ++i) {
                        const auto& m = selHits[i];
                        D2D1_RECT_F selRect = {m.left, m.top, m.left + m.width, m.top + m.height};
                        r.FillRect(selRect, selBg);
                    }
                }
            }

            // ---- Text: draw through Renderer so the DisplayList records it ----
            const float layoutW = textArea.right - textArea.left;
            const float layoutH = std::max(textArea.bottom - textArea.top + scrollY_,
                                           ContentHeight());
            D2D1_RECT_F textRect = {originX, originY, originX + layoutW, originY + layoutH};
            r.DrawText(text_, textRect, fg, fontSize,
                       DWRITE_TEXT_ALIGNMENT_LEADING,
                       DWRITE_FONT_WEIGHT_NORMAL,
                       DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
                       wrap_);
            // Re-draw the selected glyphs in selFg by clipping to each line's
            // selection rect. Cheap enough here; selection rectangles still come
            // from the cached layout used for hit-testing/caret geometry.
            if (hasSelFg && !selHits.empty()) {
                for (const auto& m : selHits) {
                    D2D1_RECT_F clip = {m.left, m.top, m.left + m.width, m.top + m.height};
                    r.PushClip(clip);
                    r.DrawText(text_, textRect, selFg, fontSize,
                               DWRITE_TEXT_ALIGNMENT_LEADING,
                               DWRITE_FONT_WEIGHT_NORMAL,
                               DWRITE_PARAGRAPH_ALIGNMENT_NEAR,
                               wrap_);
                    r.PopClip();
                }
            }

            // ---- Caret ----
            if (focused && !HasSelection() && ShouldShowCaret()) {
                D2D1_COLOR_F caret = css.hasCaretColor ? css.caretColor : accent;
                float cx, cy, ch;
                if (CaretXYForPos(cursorPos_, cx, cy, ch)) {
                    r.DrawLine(cx, cy + 2, cx, cy + ch - 2, caret, 1.5f);
                }
            }
        }
    }

    r.PopClip();

    // Scrollbar
    if (NeedsScrollbar()) {
        bool dark = theme::IsDark();
        auto thumb = ThumbRect();
        D2D1_COLOR_F thumbColor = draggingThumb_
            ? theme::kAccent()
            : (dark ? theme::Rgba(0xFF,0xFF,0xFF,0.25f) : theme::Rgba(0x00,0x00,0x00,0.20f));
        r.FillRoundedRect(thumb, 2, 2, thumbColor);
    }
}

bool TextAreaWidget::OnMouseDown(const MouseEvent& e) {
    if (!enabled) return false;

    // Scrollbar thumb drag
    if (NeedsScrollbar()) {
        auto thumb = ThumbRect();
        if (e.x >= thumb.left && e.x <= thumb.right && e.y >= thumb.top && e.y <= thumb.bottom) {
            draggingThumb_ = true;
            dragStartY_ = e.y;
            dragStartScroll_ = scrollY_;
            return true;
        }
    }

    focused = Contains(e.x, e.y);
    if (focused) {
        extern MeasureContext* g_activeMeasureContext;
        if (g_activeMeasureContext) {
            float fontSize = (css.fontSize > 0) ? css.fontSize : theme::kFontSizeNormal;
            EnsureLayout(*g_activeMeasureContext, fontSize);
        }
        cursorPos_ = HitTestPosFromXY(e.x, e.y);
        if (GetKeyState(VK_SHIFT) & 0x8000) {
            if (selectionStart_ < 0) selectionStart_ = cursorPos_;
            selectionEnd_ = cursorPos_;
        } else {
            ClearSelection();
            dragging_ = true;
            selectionStart_ = cursorPos_;
        }
        ResetCaretBlink();
    } else {
        ClearSelection();
    }
    return focused;
}

bool TextAreaWidget::OnMouseMove(const MouseEvent& e) {
    hovered = Contains(e.x, e.y);
    if (draggingThumb_) {
        float trackH = (rect.bottom - 4) - (rect.top + 4);
        float contH = ContentHeight();
        float visH = VisibleHeight();
        float thumbH = std::max(20.0f, trackH * (visH / contH));
        float maxScroll = contH - visH;
        float dy = e.y - dragStartY_;
        scrollY_ = dragStartScroll_ + dy * (maxScroll / (trackH - thumbH));
        ClampScroll();
        return true;
    }
    if (dragging_ && focused) {
        extern MeasureContext* g_activeMeasureContext;
        if (g_activeMeasureContext) {
            float fontSize = (css.fontSize > 0) ? css.fontSize : theme::kFontSizeNormal;
            EnsureLayout(*g_activeMeasureContext, fontSize);
        }
        cursorPos_ = HitTestPosFromXY(e.x, e.y);
        selectionEnd_ = cursorPos_;
        return true;
    }
    return hovered;
}

bool TextAreaWidget::OnMouseUp(const MouseEvent& e) {
    if (draggingThumb_) {
        draggingThumb_ = false;
        return true;
    }
    if (dragging_) {
        dragging_ = false;
        if (selectionStart_ == selectionEnd_) ClearSelection();
        /* build 278: 拖选结束通知宿主 (OCR 场景要拿它反查图上位置)。 */
        if (onSelectionChanged) onSelectionChanged();
        return true;
    }
    return false;
}

void TextAreaWidget::SetSelectionRange(int start, int end) {
    const int len = (int)text_.size();
    if (start > end) { const int t = start; start = end; end = t; }
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start > len) start = len;
    if (start == end) {
        ClearSelection();
        cursorPos_ = start;
    } else {
        selectionStart_ = start;
        selectionEnd_ = end;
        cursorPos_ = end;
    }
    ResetCaretBlink();
    /* 选区滚到可见 —— 宿主联动时用户不该还要自己找。 */
    extern MeasureContext* g_activeMeasureContext;
    EnsureCursorVisible(g_activeMeasureContext);
    /* 宿主主动设的选区不回调 onSelectionChanged, 避免联动回环。 */
}

bool TextAreaWidget::SelectionRange(int* start, int* end) const {
    if (!HasSelection()) return false;
    const int lo = selectionStart_ < selectionEnd_ ? selectionStart_ : selectionEnd_;
    const int hi = selectionStart_ < selectionEnd_ ? selectionEnd_ : selectionStart_;
    if (start) *start = lo;
    if (end)   *end = hi;
    return true;
}

bool TextAreaWidget::OnMouseWheel(const MouseEvent& e) {
    if (!Contains(e.x, e.y)) return false;
    scrollY_ -= e.delta * 0.5f;
    ClampScroll();
    return true;
}

bool TextAreaWidget::OnKeyChar(wchar_t ch) {
    if (!focused || !enabled) return false;
    if (readOnly) return true;

    if (ch == '\b') {
        if (HasSelection()) {
            DeleteSelection();
        } else if (cursorPos_ > 0) {
            text_.erase(cursorPos_ - 1, 1);
            cursorPos_--;
            layoutDirty_ = true;
        }
    } else if (ch == '\r' || ch == '\n') {
        if (HasSelection()) DeleteSelection();
        text_.insert(cursorPos_, 1, L'\n');
        cursorPos_++;
        layoutDirty_ = true;
    } else if (ch >= 32) {
        if (inputFilter && !inputFilter(ch)) return true;
        if (maxLength >= 0 && (int)text_.size() >= maxLength) return true;
        if (HasSelection()) DeleteSelection();
        text_.insert(cursorPos_, 1, ch);
        cursorPos_++;
        layoutDirty_ = true;
    }

    ResetCaretBlink();
    extern MeasureContext* g_activeMeasureContext;
    EnsureCursorVisible(g_activeMeasureContext);
    if (onTextChanged) onTextChanged(text_);
    return true;
}

bool TextAreaWidget::OnKeyDown(int vk) {
    if (!focused || !enabled) return false;

    /* build 288: ESC 冒泡给宿主, 理由同 TextInputWidget::OnKeyDown。 */
    if (vk == VK_ESCAPE) return false;

    extern MeasureContext* g_activeMeasureContext;
    if (g_activeMeasureContext) {
        float fontSize = (css.fontSize > 0) ? css.fontSize : theme::kFontSizeNormal;
        EnsureLayout(*g_activeMeasureContext, fontSize);
    }

    bool shift = GetKeyState(VK_SHIFT) & 0x8000;
    bool ctrl = GetKeyState(VK_CONTROL) & 0x8000;

    // Clipboard
    if (ctrl && vk == 'C') {
        if (HasSelection()) SetClipboardText(GetSelectedText());
        return true;
    }
    if (ctrl && vk == 'X') {
        if (readOnly) return true;
        if (HasSelection()) {
            SetClipboardText(GetSelectedText());
            DeleteSelection();
        }
        return true;
    }
    if (ctrl && vk == 'V') {
        if (readOnly) return true;
        std::wstring paste = GetClipboardText();
        if (!paste.empty()) {
            if (HasSelection()) DeleteSelection();
            if (maxLength < 0 || (int)(text_.size() + paste.size()) <= maxLength) {
                text_.insert(cursorPos_, paste);
                cursorPos_ += (int)paste.size();
                layoutDirty_ = true;
            }
        }
        ResetCaretBlink();
        EnsureCursorVisible(g_activeMeasureContext);
        return true;
    }
    if (ctrl && vk == 'A') {
        selectionStart_ = 0;
        selectionEnd_ = (int)text_.size();
        cursorPos_ = selectionEnd_;
        if (onSelectionChanged) onSelectionChanged();
        return true;
    }

    int oldPos = cursorPos_;
    const int oldSelStart = selectionStart_, oldSelEnd = selectionEnd_;

    /* build 279: 不认识的键必须交还给窗口 —— 否则文本框一获焦, ESC /
     * F1 / 自定义快捷键全被吞掉 (OCR 结果窗里 ESC 关不掉窗就是这么来的)。
     * 认得的只有下面这些导航/编辑键。 */
    const bool navigation = (vk == 0x25 || vk == 0x27 || vk == 0x26 ||
                             vk == 0x28 || vk == 0x24 || vk == 0x23);
    const bool editing = (vk == 0x2E /*DELETE*/ && !readOnly);
    if (!navigation && !editing) return false;

    if (vk == 0x25 /*LEFT*/ && cursorPos_ > 0) cursorPos_--;
    else if (vk == 0x27 /*RIGHT*/ && cursorPos_ < (int)text_.size()) cursorPos_++;
    else if (vk == 0x26 /*UP*/)        cursorPos_ = PosUp(cursorPos_);
    else if (vk == 0x28 /*DOWN*/)      cursorPos_ = PosDown(cursorPos_);
    else if (vk == 0x24 /*HOME*/)      cursorPos_ = PosLineStart(cursorPos_);
    else if (vk == 0x23 /*END*/)       cursorPos_ = PosLineEnd(cursorPos_);
    else if (vk == 0x2E /*DELETE*/ && !readOnly) {
        if (HasSelection()) DeleteSelection();
        else if (cursorPos_ < (int)text_.size()) {
            text_.erase(cursorPos_, 1);
            layoutDirty_ = true;
        }
    }

    if (shift && (vk == 0x25 || vk == 0x27 || vk == 0x26 || vk == 0x28 || vk == 0x24 || vk == 0x23)) {
        if (selectionStart_ < 0) selectionStart_ = oldPos;
        selectionEnd_ = cursorPos_;
    } else if (vk == 0x25 || vk == 0x27 || vk == 0x26 || vk == 0x28 || vk == 0x24 || vk == 0x23) {
        ClearSelection();
    }

    ResetCaretBlink();
    EnsureCursorVisible(g_activeMeasureContext);
    if ((vk == 0x2E || (ctrl && vk == 'V') || (ctrl && vk == 'X')) && onTextChanged)
        onTextChanged(text_);
    if ((selectionStart_ != oldSelStart || selectionEnd_ != oldSelEnd) &&
        onSelectionChanged) {
        onSelectionChanged();       /* Shift+方向 改选区 (build 278) */
    }
    return true;
}

D2D1_SIZE_F TextAreaWidget::SizeHint() const {
    return {fixedW > 0 ? fixedW : 300.0f, fixedH > 0 ? fixedH : 150.0f};
}

void ComboBoxWidget::OnDraw(Renderer& r) {
    bool dark = theme::IsDark();
    float cr = (css.borderRadius >= 0) ? css.borderRadius : theme::radius::medium;
    float fontSize = (css.fontSize > 0) ? css.fontSize : theme::kFontSizeNormal;
    D2D1_COLOR_F fg = css.hasFg ? css.fg : theme::kBtnText();

    D2D1_COLOR_F bg;
    if (bgColor.a > 0) {
        bg = bgColor;
    } else {
        if (open_)        bg = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.03f) : theme::Rgba(0xFF,0xFF,0xFF,1.0f);
        else if (hovered) bg = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.08f) : theme::Rgba(0xF9,0xF9,0xF9,0.97f);
        else              bg = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.06f) : theme::Rgba(0xFF,0xFF,0xFF,0.95f);
    }
    r.FillRoundedRect(rect, cr, cr, bg);

    bool customBorder = css.hasBorderColor || css.borderWidth >= 0;
    if (customBorder) {
        float bw = (css.borderWidth >= 0) ? css.borderWidth : 1.0f;
        if (bw > 0) {
            D2D1_COLOR_F bc = css.hasBorderColor ? css.borderColor
                                                  : (dark ? theme::Rgba(0xFF,0xFF,0xFF,0.20f)
                                                         : theme::Rgba(0x00,0x00,0x00,0.20f));
            r.DrawRoundedRect(rect, cr, cr, bc, bw);
        }
    } else {
        D2D1_COLOR_F borderTop = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.09f) : theme::Rgba(0x00,0x00,0x00,0.06f);
        r.DrawRoundedRect(rect, cr, cr, borderTop, 1.0f);
        D2D1_COLOR_F borderBot = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.16f) : theme::Rgba(0x00,0x00,0x00,0.14f);
        D2D1_RECT_F botLine = {rect.left + 2, rect.bottom - 1.5f, rect.right - 2, rect.bottom - 0.5f};
        r.FillRect(botLine, borderBot);
    }

    D2D1_RECT_F textArea = {rect.left + 10, rect.top, rect.right - 28, rect.bottom};
    if (selectedIndex_ >= 0 && selectedIndex_ < (int)items_.size()) {
        r.DrawText(items_[selectedIndex_], textArea, fg, fontSize);
    }

    D2D1_RECT_F arrowArea = {rect.right - 28, rect.top, rect.right, rect.bottom};
    r.DrawIcon(open_ ? L"\xE70E" : L"\xE70D", arrowArea, fg, 10.0f);
}

ComboBoxWidget::DropdownLayout ComboBoxWidget::ComputeDropdownLayout_() const {
    const int totalRows = (int)items_.size();
    if (totalRows <= 0 || itemHeight_ <= 0.0f) return {};

    constexpr float gap = 2.0f;
    const D2D1_RECT_F vp = Viewport();
    const float belowTop = rect.bottom + gap;
    const float aboveBottom = rect.top - gap;
    const float belowSpace = (std::max)(0.0f, vp.bottom - belowTop);
    const float aboveSpace = (std::max)(0.0f, aboveBottom - vp.top);
    const int belowRows = static_cast<int>(std::floor(belowSpace / itemHeight_));
    const int aboveRows = static_cast<int>(std::floor(aboveSpace / itemHeight_));
    const int desiredRows = (std::min)(totalRows, kMaxVisibleRows);

    DropdownLayout layout{};
    if (belowRows >= desiredRows) {
        layout.visibleRows = desiredRows;
    } else if (aboveRows >= desiredRows) {
        layout.visibleRows = desiredRows;
        layout.opensUpward = true;
    } else if (aboveRows > belowRows) {
        layout.visibleRows = (std::min)(totalRows, aboveRows);
        layout.opensUpward = true;
    } else {
        layout.visibleRows = (std::min)(totalRows, belowRows);
    }

    if (layout.visibleRows <= 0) return layout;

    const float height = layout.visibleRows * itemHeight_;
    layout.rect = layout.opensUpward
        ? D2D1_RECT_F{rect.left, aboveBottom - height, rect.right, aboveBottom}
        : D2D1_RECT_F{rect.left, belowTop, rect.right, belowTop + height};
    return layout;
}

D2D1_RECT_F ComboBoxWidget::DropdownRect() const {
    return ComputeDropdownLayout_().rect;
}

void ComboBoxWidget::ClampDropdownScroll_(int visibleRows) {
    const int maxScroll = (std::max)(0, (int)items_.size() - visibleRows);
    dropdownScrollIndex_ = std::clamp(dropdownScrollIndex_, 0, maxScroll);
}

void ComboBoxWidget::OpenDropdown_() {
    open_ = true;
    const auto layout = ComputeDropdownLayout_();
    ClampDropdownScroll_(layout.visibleRows);
    if (selectedIndex_ < dropdownScrollIndex_) {
        dropdownScrollIndex_ = selectedIndex_;
    } else if (selectedIndex_ >= dropdownScrollIndex_ + layout.visibleRows) {
        dropdownScrollIndex_ = selectedIndex_ - layout.visibleRows + 1;
    }
    ClampDropdownScroll_(layout.visibleRows);
}

int ComboBoxWidget::DropdownItemAt(float x, float y) const {
    if (!open_) return -1;
    const auto layout = ComputeDropdownLayout_();
    const auto& drop = layout.rect;
    if (layout.visibleRows <= 0 || x < drop.left || x >= drop.right ||
        y < drop.top || y >= drop.bottom) {
        return -1;
    }
    const int visibleRow = static_cast<int>((y - drop.top) / itemHeight_);
    const int index = dropdownScrollIndex_ + visibleRow;
    return index >= 0 && index < (int)items_.size() ? index : -1;
}

void ComboBoxWidget::OnDrawOverlay(Renderer& r) {
    if (!open_) return;
    bool dark = theme::IsDark();

    const auto layout = ComputeDropdownLayout_();
    if (layout.visibleRows <= 0) return;
    D2D1_RECT_F dropBg = layout.rect;
    // Popup uses the control's border-radius if set; otherwise Fluent 8px.
    float cr = (css.borderRadius >= 0) ? css.borderRadius : 8.0f;
    float fontSize = (css.fontSize > 0) ? css.fontSize : theme::kFontSizeNormal;
    D2D1_COLOR_F accent = css.hasAccent ? css.accent : theme::kAccent();
    D2D1_COLOR_F fg = css.hasFg ? css.fg : theme::kBtnText();

    D2D1_COLOR_F popupBg = (bgColor.a > 0) ? bgColor
                                           : (dark ? theme::Rgb(0x2B, 0x2B, 0x2B) : theme::Rgb(0xFF, 0xFF, 0xFF));
    r.FillRoundedRect(dropBg, cr, cr, popupBg);

    float bw = (css.borderWidth >= 0) ? css.borderWidth : 0.5f;
    if (bw > 0) {
        D2D1_COLOR_F popupBorder = css.hasBorderColor
            ? css.borderColor
            : (dark ? theme::Rgb(0x1A, 0x1A, 0x1A) : theme::Rgb(0xBF, 0xBF, 0xBF));
        r.DrawRoundedRect(dropBg, cr, cr, popupBorder, bw);
    }

    r.PushRoundedClip(dropBg, cr, cr);

    // Item backgrounds. Base (non-hover) is item-bg if provided; else transparent.
    // Hover is item-hover-bg if provided; else an accent-derived tint or
    // neutral theme tint.
    D2D1_COLOR_F itemBaseBg = css.hasItemBg ? css.itemBg
                                             : D2D1_COLOR_F{0, 0, 0, 0};
    D2D1_COLOR_F hoverBg;
    if (css.hasItemHoverBg) {
        hoverBg = css.itemHoverBg;
    } else if (css.hasAccent && accent.a > 0.001f) {
        hoverBg = {css.accent.r, css.accent.g, css.accent.b, 0.15f};
    } else {
        hoverBg = dark ? theme::Rgba(0xFF, 0xFF, 0xFF, 0.06f)
                       : theme::Rgba(0x00, 0x00, 0x00, 0.04f);
    }

    // Indicator bar on the left is only drawn if accent has non-zero alpha.
    // Setting accent-color: transparent hides it and falls back to
    // typography-only selection marking.
    bool drawIndicator = accent.a > 0.001f;

    float itemRadius = 3.0f;

    const int totalRows = (int)items_.size();
    const bool canScroll = totalRows > layout.visibleRows;
    const float labelRight = canScroll ? dropBg.right - 12.0f : dropBg.right - 4.0f;
    for (int row = 0; row < layout.visibleRows; ++row) {
        const int i = dropdownScrollIndex_ + row;
        float iy = dropBg.top + itemHeight_ * row;
        D2D1_RECT_F itemRect = {dropBg.left + 4, iy + 1, dropBg.right - 4, iy + itemHeight_ - 1};

        // Base background (e.g. to make items visually distinct from the
        // popup bg — useful for zebra lists or chip-style items).
        if (itemBaseBg.a > 0.001f) {
            r.FillRoundedRect(itemRect, itemRadius, itemRadius, itemBaseBg);
        }
        if (i == hoveredIndex_ && hoverBg.a > 0.001f) {
            r.FillRoundedRect(itemRect, itemRadius, itemRadius, hoverBg);
        }
        // Optional per-item divider line below each row (not the last one).
        if (css.hasItemBorderColor && i < totalRows - 1) {
            D2D1_RECT_F div = {dropBg.left + 8, iy + itemHeight_ - 1,
                               dropBg.right - 8, iy + itemHeight_};
            r.FillRect(div, css.itemBorderColor);
        }
        if (i == selectedIndex_ && drawIndicator) {
            D2D1_RECT_F indicator = {dropBg.left + 5, iy + 8, dropBg.left + 8, iy + itemHeight_ - 8};
            r.FillRoundedRect(indicator, 1.5f, 1.5f, accent);
        }
        D2D1_COLOR_F itemColor = fg;
        DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
        if (i == selectedIndex_) {
            if (css.hasSelectedColor) itemColor = css.selectedColor;
            if (!drawIndicator)       weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        }
        D2D1_RECT_F labelR = {dropBg.left + 16, iy, labelRight, iy + itemHeight_};
        r.DrawText(items_[i], labelR, itemColor, fontSize,
                   DWRITE_TEXT_ALIGNMENT_LEADING, weight);
    }

    if (canScroll) {
        const float trackTop = dropBg.top + 4.0f;
        const float trackBottom = dropBg.bottom - 4.0f;
        const float trackHeight = trackBottom - trackTop;
        const float thumbHeight = (std::max)(18.0f,
            trackHeight * (float)layout.visibleRows / (float)totalRows);
        const int maxScroll = totalRows - layout.visibleRows;
        const float ratio = maxScroll > 0
            ? (float)dropdownScrollIndex_ / (float)maxScroll : 0.0f;
        const float thumbTop = trackTop + ratio * (trackHeight - thumbHeight);
        const D2D1_COLOR_F thumb = dark ? theme::Rgba(0xFF, 0xFF, 0xFF, 0.42f)
                                        : theme::Rgba(0x00, 0x00, 0x00, 0.34f);
        r.FillRoundedRect({dropBg.right - 7.0f, thumbTop, dropBg.right - 3.0f,
                           thumbTop + thumbHeight}, 2.0f, 2.0f, thumb);
    }

    r.PopRoundedClip();
}

bool ComboBoxWidget::OnMouseDown(const MouseEvent& e) {
    if (Contains(e.x, e.y)) {
        // Only open here. Closing is handled by main window's
        // outside-click logic (step 2 in OnMouseDown) to avoid
        // double-toggle when step 2 closes then OnMouseDown reopens.
        OpenDropdown_();
        return true;
    }
    return false;
}

bool ComboBoxWidget::OnMouseMove(const MouseEvent& e) {
    hovered = Contains(e.x, e.y);
    hoveredIndex_ = DropdownItemAt(e.x, e.y);
    return hovered || open_;
}

bool ComboBoxWidget::OnMouseUp(const MouseEvent& e) {
    if (open_ && hoveredIndex_ >= 0) {
        selectedIndex_ = hoveredIndex_;
        open_ = false;
        if (onSelectionChanged) onSelectionChanged(selectedIndex_);
        return true;
    }
    return false;
}

bool ComboBoxWidget::OnMouseWheel(const MouseEvent& e) {
    if (!open_ || DropdownItemAt(e.x, e.y) < 0) return false;
    const auto layout = ComputeDropdownLayout_();
    const int maxScroll = (std::max)(0, (int)items_.size() - layout.visibleRows);
    if (maxScroll <= 0) return true;

    const int notches = (std::max)(1, static_cast<int>(std::abs(e.delta) / WHEEL_DELTA));
    const int rows = notches * 3;
    dropdownScrollIndex_ += e.delta > 0 ? -rows : rows;
    ClampDropdownScroll_(layout.visibleRows);
    hoveredIndex_ = DropdownItemAt(e.x, e.y);
    return true;
}

D2D1_SIZE_F ComboBoxWidget::SizeHint() const {
    return {fixedW > 0 ? fixedW : 150.0f, fixedH > 0 ? fixedH : 32.0f};  // WinUI 3: MinHeight=32
}

// ---- TabControl ----

void TabControlWidget::AddTab(const std::wstring& title, WidgetPtr content) {
    // Hide by default, SetActiveIndex will show the active one
    content->visible = false;
    tabs_.push_back({title, content});
    AddChild(content);
    SetActiveIndex(activeIndex_);
}

void TabControlWidget::SetActiveIndex(int i) {
    activeIndex_ = i;
    for (int j = 0; j < (int)tabs_.size(); j++) {
        if (tabs_[j].content) tabs_[j].content->visible = (j == i);
    }
}

void TabControlWidget::OnDraw(Renderer& r) {
    // Resolve CSS overrides (each falls back to theme).
    float fontSize = (css.fontSize > 0) ? css.fontSize : theme::kFontSizeNormal;
    D2D1_COLOR_F headerBgColor = (bgColor.a > 0) ? bgColor : theme::kToolbarBg();
    D2D1_COLOR_F divider       = css.hasBorderColor ? css.borderColor : theme::kDivider();
    D2D1_COLOR_F accent        = css.hasAccent ? css.accent : theme::kAccent();
    D2D1_COLOR_F itemBg        = css.hasItemBg ? css.itemBg : theme::transparent;
    D2D1_COLOR_F hoverBg       = css.hasItemHoverBg ? css.itemHoverBg : theme::kBtnHover();
    D2D1_COLOR_F idleText      = css.hasFg ? css.fg : theme::kSidebarText();
    D2D1_COLOR_F activeText    = css.hasSelectedColor ? css.selectedColor
                                                     : (css.hasFg ? css.fg : theme::kTitleBarText());
    bool drawIndicator = accent.a > 0.001f;

    // Content area (below the tab header) — only fill if user gave a custom
    // bg; otherwise leave parent bg showing through (lets cards / containers
    // host tabs without a hardcoded gray content panel).
    D2D1_RECT_F contentArea = {rect.left, rect.top + tabHeight_, rect.right, rect.bottom};

    // Tab header background
    D2D1_RECT_F headerBg = {rect.left, rect.top, rect.right, rect.top + tabHeight_};
    if (headerBgColor.a > 0) r.FillRect(headerBg, headerBgColor);

    // Header / content divider line
    if (divider.a > 0) {
        r.DrawLine(rect.left, rect.top + tabHeight_, rect.right, rect.top + tabHeight_, divider);
    }

    float x = rect.left;
    float tabW = 120.0f;

    for (int i = 0; i < (int)tabs_.size(); i++) {
        D2D1_RECT_F tabRect = {x, rect.top, x + tabW, rect.top + tabHeight_};

        // Per-tab base bg (if user set item-bg)
        if (itemBg.a > 0) r.FillRect(tabRect, itemBg);

        if (i == activeIndex_) {
            // Active tab background. Priority: user's selected-bg → fall back
            // to widget bgColor (lets simple flat tabs work) → none.
            if (css.hasSelectedBg) {
                if (css.selectedRadius > 0) {
                    // Inset the chip a few px so it sits inside the header bar.
                    D2D1_RECT_F chip = {tabRect.left + 4, tabRect.top + 4,
                                        tabRect.right - 4, tabRect.bottom - 4};
                    r.FillRoundedRect(chip, css.selectedRadius, css.selectedRadius, css.selectedBg);
                } else {
                    r.FillRect(tabRect, css.selectedBg);
                }
            } else if (bgColor.a > 0) {
                r.FillRect(tabRect, bgColor);
            }
            if (drawIndicator) {
                r.DrawLine(x, rect.top + tabHeight_ - 2, x + tabW, rect.top + tabHeight_ - 2,
                           accent, 2.0f);
            }
        } else if (i == hoveredTab_) {
            r.FillRect(tabRect, hoverBg);
        }

        D2D1_COLOR_F textColor = (i == activeIndex_) ? activeText : idleText;
        r.DrawText(tabs_[i].title, tabRect, textColor, fontSize,
                   DWRITE_TEXT_ALIGNMENT_CENTER);

        x += tabW;
    }

    (void)contentArea;
}

void TabControlWidget::DrawTree(Renderer& r) {
    if (!visible) return;
    OnDraw(r);
    paintedOnce_ = true;   // L45: mount-phase transition gate

    // Clip children to the content area (below tab headers)
    D2D1_RECT_F contentArea = {rect.left, rect.top + tabHeight_, rect.right, rect.bottom};
    r.PushClip(contentArea);
    for (auto& child : children_) {
        child->DrawTree(r);
    }
    r.PopClip();
}

int TabControlWidget::TabHitTest(float x, float y) const {
    if (y < rect.top || y >= rect.top + tabHeight_) return -1;
    float tabW = 120.0f;
    int idx = (int)((x - rect.left) / tabW);
    if (idx >= 0 && idx < (int)tabs_.size()) return idx;
    return -1;
}

bool TabControlWidget::OnMouseDown(const MouseEvent& e) {
    int idx = TabHitTest(e.x, e.y);
    if (idx >= 0 && idx != activeIndex_) {
        SetActiveIndex(idx);
        DoLayout();
        return true;
    }
    return false;
}

bool TabControlWidget::OnMouseMove(const MouseEvent& e) {
    bool inside = Contains(e.x, e.y);
    hoveredTab_ = inside ? TabHitTest(e.x, e.y) : -1;
    hovered = inside;
    return hovered;
}

void TabControlWidget::DoLayout() {
    D2D1_RECT_F contentArea = {
        rect.left, rect.top + tabHeight_,
        rect.right, rect.bottom
    };
    for (int i = 0; i < (int)tabs_.size(); i++) {
        if (tabs_[i].content) {
            tabs_[i].content->visible = (i == activeIndex_);
            tabs_[i].content->rect = contentArea;
            if (i == activeIndex_) {
                tabs_[i].content->DoLayout();
            }
        }
    }
}

D2D1_SIZE_F TabControlWidget::SizeHint() const {
    return {fixedW > 0 ? fixedW : 400.0f, fixedH > 0 ? fixedH : 300.0f};
}

// ---- ScrollView ----

void ScrollViewWidget::SetContent(WidgetPtr content) {
    children_.clear();
    content_ = content;
    if (content_) AddChild(content_);
}

float ScrollViewWidget::VisibleHeight() const {
    return rect.bottom - rect.top;
}

bool ScrollViewWidget::NeedsScrollbar() const {
    return contentHeight_ > VisibleHeight() + 1.0f;
}

void ScrollViewWidget::ClampScroll() {
    float maxScroll = std::max(0.0f, contentHeight_ - VisibleHeight());
    scrollY_ = std::clamp(scrollY_, 0.0f, maxScroll);
}

D2D1_RECT_F ScrollViewWidget::ThumbRect() const {
    if (!NeedsScrollbar()) return {};
    float visH = VisibleHeight();
    float ratio = visH / contentHeight_;
    float thumbH = std::max(20.0f, visH * ratio);
    float maxScroll = contentHeight_ - visH;
    float scrollRatio = maxScroll > 0 ? scrollY_ / maxScroll : 0;
    float thumbY = rect.top + scrollRatio * (visH - thumbH);
    float tw = ThumbWidth();
    float inset = 2.0f;  // avoid resize border
    return {rect.right - tw - inset, thumbY, rect.right - inset, thumbY + thumbH};
}

void ScrollViewWidget::DoLayout() {
    if (!content_) return;

    /* Build 110 (L29 follow-up): 应用 padding 给 content 区域. 之前
     * visW/visH/content.rect 直接用 sv.rect, padL/padT/padR/padB 字段被
     * 忽略, .uix 写 `padding: 24px 28px` 视觉上没起作用 — caller 的内容
     * 贴 ScrollView 边. 改成跟 VBox/HBox 同款行为: content 起点偏 padding,
     * 可用宽高扣 padding. */
    float visW = (rect.right - padR) - (rect.left + padL);
    float visH = (rect.bottom - padB) - (rect.top + padT);

    // First pass: layout content at full height to measure actual needed height
    // Give it a tall rect so VBox can place all children without compression
    float estimatedH = std::max(content_->SizeHint().height, visH);
    content_->rect = {rect.left + padL, rect.top + padT,
                        rect.left + padL + visW,
                        rect.top + padT + estimatedH};
    content_->DoLayout();

    // Measure actual content height from children bounds (EXCLUDING content_ itself)
    float maxBottom = rect.top + padT;
    std::function<void(Widget*)> measure = [&](Widget* w) {
        if (!w->visible) return;
        if (w->rect.bottom > maxBottom) maxBottom = w->rect.bottom;
        for (auto& c : w->Children()) measure(c.get());
    };
    // 只 measure 子元素的 bounds，不包含 content_ 自己的 rect.bottom（那是 estimatedH，非真实内容）
    for (auto& c : content_->Children()) measure(c.get());
    /* contentHeight_ 含 padT 但不含 padB — 内容 + 上 padding 是滚动可视范围,
     * 下 padding 通过 visH 已经扣过 (caller 想要的"末尾留白"自然出现). */
    contentHeight_ = std::max(maxBottom - rect.top, visH);


    // Scrollbar overlays content (absolute positioning, no space reserved)
    float cw = visW;
    ClampScroll();

    // Final layout with correct scroll offset and width
    content_->rect = {rect.left + padL, rect.top + padT - scrollY_,
                        rect.left + padL + cw,
                        rect.top + padT - scrollY_ + contentHeight_};
    content_->DoLayout();
}

void ScrollViewWidget::ApplyScrollDelta_(float oldScrollY) {
    if (!content_) return;
    const float dy = -(scrollY_ - oldScrollY);   /* 内容反向移动 */
    if (dy == 0.0f) return;
    ShiftSubtreeRects(content_.get(), 0.0f, dy);
}

void ScrollViewWidget::OnDraw(Renderer& r) {
    Widget::OnDraw(r);
    // Content is drawn in DrawTree with clipping — not here.

    // Scrollbar (overlay, no track background)
    if (NeedsScrollbar()) {
        auto thumb = ThumbRect();
        bool dark = theme::IsDark();
        D2D1_COLOR_F thumbColor;
        if (draggingThumb_)  thumbColor = theme::kAccent();
        else if (hoveringBar_) thumbColor = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.55f) : theme::Rgba(0x00,0x00,0x00,0.45f);
        else                   thumbColor = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.38f) : theme::Rgba(0x00,0x00,0x00,0.28f);
        float radius = ThumbWidth() / 2;
        r.FillRoundedRect(thumb, radius, radius, thumbColor);
    }
}

void ScrollViewWidget::DrawTree(Renderer& r) {
    if (!visible) return;
    // Draw background + scrollbar
    OnDraw(r);
    paintedOnce_ = true;   // L45: mount-phase transition gate
    /* Draw content inside clip region — only once.
     *
     * PushCull (build 285): clip 只让 D2D 丢掉画出界的像素, 遍历子树和录
     * display list 的成本一分没省。长列表里这笔钱是主要开销 —— 2000 行实测
     * 每帧 6.6ms 全花在录制看不见的行上, 拖动滚动条明显不跟手。PushCull 让
     * Widget::DrawTree 对完全落在视口外的子树直接返回。
     *
     * 剔除是 opt-in 的: 只有这里 push, 别处行为完全不变。 */
    r.PushClip(rect);
    r.PushCull(rect);
    if (content_) content_->DrawTree(r);
    r.PopCull();
    r.PopClip();
    // Skip Widget::DrawTree's default children iteration (content is already drawn)
}

bool ScrollViewWidget::OnMouseWheel(const MouseEvent& e) {
    if (!Contains(e.x, e.y)) return false;
    // 内容完全适配视口时不消耗滚轮事件，让父容器处理
    if (!NeedsScrollbar()) return false;
    const float oldScroll = scrollY_;
    scrollY_ -= e.delta * 0.3f;
    ClampScroll();
    ApplyScrollDelta_(oldScroll);   /* 纯偏移变化, 不重跑布局 */
    return true;
}

bool ScrollViewWidget::OnMouseDown(const MouseEvent& e) {
    if (NeedsScrollbar() && e.x >= rect.right - kBarSpace - 2 && e.x < rect.right - 2) {
        // Click anywhere in scrollbar track area
        draggingThumb_ = true;
        dragStartY_ = e.y;
        // Jump scroll to click position
        auto thumb = ThumbRect();
        float thumbH = thumb.bottom - thumb.top;
        float visH = VisibleHeight();
        float trackRange = visH - thumbH;
        if (trackRange > 0 && (e.y < thumb.top || e.y > thumb.bottom)) {
            const float oldScroll = scrollY_;
            float targetY = e.y - thumbH / 2 - rect.top;
            float maxScroll = contentHeight_ - visH;
            scrollY_ = targetY / (visH - thumbH) * maxScroll;
            ClampScroll();
            ApplyScrollDelta_(oldScroll);
        }
        dragStartScroll_ = scrollY_;
        return true;
    }
    return false;
}

bool ScrollViewWidget::OnMouseMove(const MouseEvent& e) {
    hovered = Contains(e.x, e.y);
    if (draggingThumb_) {
        float visH = VisibleHeight();
        float ratio = visH / contentHeight_;
        float thumbH = std::max(20.0f, visH * ratio);
        float trackRange = visH - thumbH;
        if (trackRange > 0) {
            const float oldScroll = scrollY_;
            float dy = e.y - dragStartY_;
            float maxScroll = contentHeight_ - visH;
            scrollY_ = dragStartScroll_ + dy * (maxScroll / trackRange);
            ClampScroll();
            ApplyScrollDelta_(oldScroll);
        }
        return true;
    }
    // Detect hover over scrollbar area for visual width change (no layout impact)
    hoveringBar_ = NeedsScrollbar() && hovered && e.x >= rect.right - kBarSpace - 2 && e.x < rect.right - 2;
    return hovered;
}

bool ScrollViewWidget::OnMouseUp(const MouseEvent& e) {
    if (draggingThumb_) {
        draggingThumb_ = false;
        return true;
    }
    return false;
}

// ---- RadioButton ----

void RadioButtonWidget::SetSelected(bool v) {
    if (selected_ == v) return;

    selected_ = v;
    if (selected_) {
        DeselectSiblings();
    }
    selectAnim_.SetTarget(selected_ ? 1.0f : 0.0f);
    animating_ = selectAnim_.Active();
    ui::GetContext().RegisterAnimatingWidget(this);
}

void RadioButtonWidget::UpdateAnimation() {
    selectAnim_.SetIdleTarget(selected_ ? 1.0f : 0.0f);
    selectAnim_.SampleFrame(GetTickCount64());
    animating_ = selectAnim_.Active();
}

void RadioButtonWidget::DeselectSiblings() {
    // 同 name="group" 的 radio 不一定是 DOM 直接 sibling —— HTML 里每个
    // radio 通常各自包在独立 <div class="row"> 里。遵循 DOM 语义：从 widget
    // 树的根开始遍历，所有 group 同名的 radio 都互斥。
    Widget* root = this;
    while (root->Parent()) root = root->Parent();

    std::function<void(Widget*)> walk = [&](Widget* w) {
        auto* rb = dynamic_cast<RadioButtonWidget*>(w);
        if (rb && rb != this && rb->group_ == group_) {
            rb->SetSelectedImmediate(false);
        }
        for (auto& c : w->Children()) walk(c.get());
    };
    walk(root);
}

void RadioButtonWidget::OnDraw(Renderer& r) {
    Widget::OnDraw(r);
    bool dark = theme::IsDark();

    float circR = 10.0f;
    float cy = (rect.top + rect.bottom) / 2;
    float cx = rect.left + circR + 2;

    D2D1_RECT_F outer = {cx - circR, cy - circR, cx + circR, cy + circR};

    D2D1_COLOR_F accent      = css.hasAccent ? css.accent : theme::kAccent();
    D2D1_COLOR_F accentHover = css.hasAccent ? css.accent : theme::kAccentHover();
    D2D1_COLOR_F fg = css.hasFg ? css.fg : theme::kBtnText();
    float fontSize = (css.fontSize > 0) ? css.fontSize : theme::kFontSizeNormal;

    selectAnim_.SetIdleTarget(selected_ ? 1.0f : 0.0f);
    float t = std::clamp(selectAnim_.SampleFrame(GetTickCount64()), 0.0f, 1.0f);
    animating_ = selectAnim_.Active();

    if (t > 0.01f) {
        D2D1_COLOR_F accentFill = hovered ? accentHover : accent;
        r.FillRoundedRect(outer, circR, circR, accentFill);

        float dotBaseR = 6.0f;
        if (pressed)      dotBaseR = 5.0f;
        else if (hovered) dotBaseR = 7.0f;
        float dotR = dotBaseR * ApplyEasing(EasingFunction::EaseOutCubic, t);

        if (dotR > 0.5f) {
            D2D1_RECT_F inner = {cx - dotR, cy - dotR, cx + dotR, cy + dotR};
            r.FillRoundedRect(inner, dotR, dotR, theme::white);
        }
    } else {
        D2D1_COLOR_F bg = (bgColor.a > 0) ? bgColor
                                          : (dark ? theme::Rgba(0xFF,0xFF,0xFF,0.05f) : theme::Rgba(0x00,0x00,0x00,0.02f));
        if (hovered && bgColor.a == 0)
            bg = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.08f) : theme::Rgba(0x00,0x00,0x00,0.05f);
        r.FillRoundedRect(outer, circR, circR, bg);

        float bw = (css.borderWidth >= 0) ? css.borderWidth : 1.0f;
        if (bw > 0) {
            D2D1_COLOR_F borderColor = css.hasBorderColor ? css.borderColor
                                                           : (dark ? theme::Rgba(0xFF,0xFF,0xFF,0.54f)
                                                                  : theme::Rgba(0x00,0x00,0x00,0.45f));
            r.DrawRoundedRect(outer, circR, circR, borderColor, bw);
        }
    }

    D2D1_RECT_F labelRect = {cx + circR + 8, rect.top, rect.right, rect.bottom};
    r.DrawText(text_, labelRect, fg, fontSize);
}

float RadioButtonWidget::ContentRight_() const {
    float dotSize = 20.0f, gap = 8.0f;
    float textW = theme::kFontSizeNormal * 0.65f * (float)text_.size();
    return rect.left + padL + dotSize + gap + textW + 8.0f;
}

bool RadioButtonWidget::OnMouseUp(const MouseEvent& e) {
    if (e.x >= rect.left && e.x < ContentRight_() && e.y >= rect.top && e.y < rect.bottom) {
        SetSelected(true);
        if (onValueChanged) onValueChanged(true);
        return true;
    }
    return false;
}

bool RadioButtonWidget::OnMouseMove(const MouseEvent& e) {
    hovered = e.x >= rect.left && e.x < ContentRight_() && e.y >= rect.top && e.y < rect.bottom;
    return hovered;
}

D2D1_SIZE_F RadioButtonWidget::SizeHint() const {
    float dotSize = 20.0f, gap = 8.0f;
    float textW = theme::kFontSizeNormal * 0.65f * (float)text_.size();
    float w = fixedW > 0 ? fixedW : (dotSize + gap + textW + 8.0f);
    return {w, fixedH > 0 ? fixedH : 32.0f};  // WinUI 3: MinHeight=32
}

// ---- Toggle ----

void ToggleWidget::UpdateCachedColors() {
    bool dark = theme::IsDark();
    // Off-state should feel soft, not high-contrast: track stays near the
    // surface color, thumb is a mid-gray (not near-black), border is faint.
    // Border alpha lives in OnDraw — keep both off colors here in sync.
    CachedTrackColorOff_ = dark ? theme::Rgb(0x3D, 0x3D, 0x3D) : theme::Rgb(0xF8, 0xF7, 0xF7);
    CachedTrackColorOn_ = theme::kAccent();
    CachedThumbColorOff_ = dark ? theme::Rgb(0xB8, 0xB8, 0xB8) : theme::Rgb(0x5B, 0x5B, 0x5B);
    /* On thumb: always white in both modes. Material / iOS / Tailwind / shadcn
     * 主流路线 — dark mode 下黑色 thumb 需要配 Fluent "暗模式 accent 提亮"
     * 整套 token, 单独黑手柄套饱和中蓝 accent 视觉过重 ("丑"). 跟其他主流
     * 设计系统对齐, ON 态 thumb 不区分主题. */
    CachedThumbColorOn_ = theme::Rgb(0xFF, 0xFF, 0xFF);
}

void ToggleWidget::UpdateAnimation() {
    anim_.SetIdleTarget(on_ ? 1.0f : 0.0f);
    anim_.SampleFrame(GetTickCount64());
    animating_ = anim_.Active();
}

void ToggleWidget::SetOn(bool v) {
    if (on_ == v) return;

    on_ = v;
    anim_.SetTarget(on_ ? 1.0f : 0.0f);
    animating_ = anim_.Active();
    ui::GetContext().RegisterAnimatingWidget(this);
}

void ToggleWidget::OnDraw(Renderer& r) {
    // Refresh the cached colors every frame: cheap (4 RGB literals) and the
    // only way the cache responds to a runtime dark-mode flip. Without this
    // the constructor's snapshot wins forever, which is why earlier color
    // tweaks in UpdateCachedColors didn't actually show up.
    UpdateCachedColors();
    Widget::OnDraw(r);
    bool dark = theme::IsDark();

    float trackW = 40.0f, trackH = 20.0f;
    float cy = (rect.top + rect.bottom) / 2;
    float tx = rect.left + 2;

    D2D1_RECT_F track = {tx, cy - trackH/2, tx + trackW, cy + trackH/2};
    float trackR = trackH / 2;

    // Resolve colors: accent → ON track, bgColor → OFF track, fg → label & ON thumb
    D2D1_COLOR_F onTrack  = css.hasAccent ? css.accent : CachedTrackColorOn_;
    D2D1_COLOR_F offTrack = (bgColor.a > 0) ? bgColor  : CachedTrackColorOff_;

    anim_.SetIdleTarget(on_ ? 1.0f : 0.0f);
    float t = std::clamp(anim_.SampleFrame(GetTickCount64()), 0.0f, 1.0f);
    animating_ = anim_.Active();
    D2D1_COLOR_F trackColor = {
        offTrack.r + t * (onTrack.r - offTrack.r),
        offTrack.g + t * (onTrack.g - offTrack.g),
        offTrack.b + t * (onTrack.b - offTrack.b),
        offTrack.a + t * (onTrack.a - offTrack.a)
    };
    r.FillRoundedRect(track, trackR, trackR, trackColor);

    if (t < 0.99f) {
        // Default border: same hue as off-track (#F8F7F7) shaded one step
        // darker so the outline reads as "extension" of the track surface
        // rather than a separate stroke. Dark mode mirrors with a faint
        // light tint over the dark surface.
        D2D1_COLOR_F borderColor = css.hasBorderColor ? css.borderColor
                                                       : (dark ? theme::Rgb(0x55, 0x55, 0x55)
                                                              : theme::Rgb(0xD8, 0xD7, 0xD7));
        borderColor.a *= (1.0f - t);
        float bw = (css.borderWidth >= 0) ? css.borderWidth : 1.0f;
        if (bw > 0) r.DrawRoundedRect(track, trackR, trackR, borderColor, bw);
    }

    float thumbSize = 12.0f;
    float thumbCR = 7.0f;

    float minX = tx + trackH / 2;
    float maxX = tx + trackW - trackH / 2;
    float thumbX = minX + t * (maxX - minX);

    float halfThumb = thumbSize / 2;
    D2D1_RECT_F thumb = {thumbX - halfThumb, cy - halfThumb, thumbX + halfThumb, cy + halfThumb};
    D2D1_COLOR_F thumbColor = {
        CachedThumbColorOff_.r + t * (CachedThumbColorOn_.r - CachedThumbColorOff_.r),
        CachedThumbColorOff_.g + t * (CachedThumbColorOn_.g - CachedThumbColorOff_.g),
        CachedThumbColorOff_.b + t * (CachedThumbColorOn_.b - CachedThumbColorOff_.b),
        CachedThumbColorOff_.a + t * (CachedThumbColorOn_.a - CachedThumbColorOff_.a)
    };
    r.FillRoundedRect(thumb, thumbCR, thumbCR, thumbColor);

    if (!text_.empty()) {
        D2D1_COLOR_F fg = css.hasFg ? css.fg : theme::kBtnText();
        float fontSize = (css.fontSize > 0) ? css.fontSize : theme::kFontSizeNormal;
        D2D1_RECT_F labelRect = {tx + trackW + 10, rect.top, rect.right, rect.bottom};
        r.DrawText(text_, labelRect, fg, fontSize);
    }
}

float ToggleWidget::ContentRight_() const {
    float trackW = 40.0f, gap = 8.0f;
    float textW = text_.empty() ? 0 : theme::kFontSizeNormal * 0.65f * (float)text_.size();
    return rect.left + padL + trackW + (text_.empty() ? 0 : gap + textW) + 4.0f;
}

bool ToggleWidget::OnMouseUp(const MouseEvent& e) {
    if (e.x >= rect.left && e.x < ContentRight_() && e.y >= rect.top && e.y < rect.bottom) {
        SetOn(!on_);
        if (onValueChanged) onValueChanged(on_);
        return true;
    }
    return false;
}

bool ToggleWidget::OnMouseMove(const MouseEvent& e) {
    hovered = e.x >= rect.left && e.x < ContentRight_() && e.y >= rect.top && e.y < rect.bottom;
    return hovered;
}

D2D1_SIZE_F ToggleWidget::SizeHint() const {
    float trackW = 40.0f, gap = 8.0f;
    float textW = text_.empty() ? 0 : theme::kFontSizeNormal * 0.65f * (float)text_.size();
    float w = fixedW > 0 ? fixedW : (trackW + (text_.empty() ? 0 : gap + textW) + 4.0f);
    return {w, fixedH > 0 ? fixedH : 26.0f};
}

// ---- ProgressBar ----

void ProgressBarWidget::SetValue(float v, bool animate) {
    targetValue_ = std::clamp(v, min_, max_);

    if (!animate) {
        value_ = targetValue_;
        valueAnim_.SetImmediate(targetValue_);
        animating_ = false;
        return;
    }

    if (value_ != targetValue_ || valueAnim_.Current() != targetValue_) {
        value_ = targetValue_;
        valueAnim_.SetTarget(targetValue_);
        animating_ = valueAnim_.Active();
        if (!animating_) return;
        ui::GetContext().RegisterAnimatingWidget(this);
    }
}

void ProgressBarWidget::UpdateAnimation() {
    valueAnim_.SetIdleTarget(targetValue_);
    valueAnim_.SampleFrame(GetTickCount64());
    animating_ = valueAnim_.Active();
}

void ProgressBarWidget::OnDraw(Renderer& r) {
    Widget::OnDraw(r);
    bool dark = theme::IsDark();

    // 6px bar over a same-height track. Earlier 3/1 split rendered as a
    // hairline that vanished against the page background.
    float barH = 6.0f;
    float trackH = 6.0f;
    float cy = (rect.top + rect.bottom) / 2;
    float cr = (css.borderRadius >= 0) ? css.borderRadius : 3.0f;
    D2D1_COLOR_F accent = css.hasAccent ? css.accent : theme::kAccent();

    D2D1_RECT_F trackRect = {rect.left, cy - trackH/2, rect.right, cy + trackH/2};
    D2D1_COLOR_F trackColor = (bgColor.a > 0) ? bgColor
                                              : (dark ? theme::Rgba(0xFF,0xFF,0xFF,0.12f)
                                                     : theme::Rgba(0x00,0x00,0x00,0.10f));
    r.FillRoundedRect(trackRect, cr, cr, trackColor);

    if (indeterminate_) {
        uint64_t now = GetTickCount64();
        float period = 888.0f;
        float phase = fmodf((float)now, period * 2.0f) / (period * 2.0f);
        float barW = (rect.right - rect.left) * 0.4f;
        float totalTravel = (rect.right - rect.left) + barW;
        float xOffset = -barW + phase * totalTravel;
        float fillL = std::max(rect.left, rect.left + xOffset);
        float fillR = std::min(rect.right, rect.left + xOffset + barW);
        if (fillR > fillL) {
            D2D1_RECT_F fillRect = {fillL, cy - barH/2, fillR, cy + barH/2};
            r.FillRoundedRect(fillRect, cr, cr, accent);
        }
    } else {
        valueAnim_.SetIdleTarget(targetValue_);
        float visualValue = valueAnim_.SampleFrame(GetTickCount64());
        animating_ = valueAnim_.Active();
        float pct = (max_ > min_) ? (visualValue - min_) / (max_ - min_) : 0;
        float fillW = (rect.right - rect.left) * pct;
        if (fillW > 0) {
            D2D1_RECT_F fillRect = {rect.left, cy - barH/2, rect.left + fillW, cy + barH/2};
            r.FillRoundedRect(fillRect, cr, cr, accent);
        }
    }
}

D2D1_SIZE_F ProgressBarWidget::SizeHint() const {
    return {fixedW > 0 ? fixedW : 200.0f, fixedH > 0 ? fixedH : 20.0f};
}

// ---- ToolTip ----

void ToolTipWidget::Show(const std::wstring& text, float x, float y) {
    text_ = text;
    showing_ = true;
    float w = theme::kFontSizeSmall * (float)text.length() * 0.6f + 16;
    float h = 26.0f;
    rect = {x, y - h - 4, x + w, y - 4};
}

void ToolTipWidget::Hide() {
    showing_ = false;
}

void ToolTipWidget::OnDraw(Renderer& r) {
    if (!showing_) return;

    r.FillRoundedRect(rect, 4, 4, theme::kToolbarBg());
    r.DrawRoundedRect(rect, 4, 4, theme::kDivider(), 0.5f);

    D2D1_RECT_F textR = {rect.left + 8, rect.top, rect.right - 8, rect.bottom};
    r.DrawText(text_, textR, theme::kBtnText(), theme::kFontSizeSmall, DWRITE_TEXT_ALIGNMENT_CENTER);
}

// ---- Overlay ----

void OverlayWidget::OnDraw(Renderer& r) {
    if (!active_) return;

    // Full-screen mask.
    D2D1_COLOR_F mask = {0.0f, 0.0f, 0.0f, 0.35f};
    r.FillRect(rect, mask);

    float cx = (rect.left + rect.right) * 0.5f;
    float cy = (rect.top + rect.bottom) * 0.5f;
    float panelW = 260.0f;
    float panelH = showSpinner_ ? 110.0f : 84.0f;
    D2D1_RECT_F panel = {cx - panelW * 0.5f, cy - panelH * 0.5f, cx + panelW * 0.5f, cy + panelH * 0.5f};

    D2D1_COLOR_F panelBg = theme::kToolbarBg();
    panelBg.a = 0.95f;
    r.FillRoundedRect(panel, 8.0f, 8.0f, panelBg);
    r.DrawRoundedRect(panel, 8.0f, 8.0f, theme::kDivider(), 0.8f);

    if (showSpinner_) {
        uint64_t now = GetTickCount64();
        if (spinnerTick_ == 0) spinnerTick_ = now;
        float t = (float)((now - spinnerTick_) % 1200ULL) / 1200.0f;
        float centerY = panel.top + 28.0f;
        float radius = 12.0f;
        const int segCount = 10;
        for (int i = 0; i < segCount; ++i) {
            float phase = std::fmod(t + (float)i / (float)segCount, 1.0f);
            float angle = phase * 6.2831853f;
            float sx = cx + std::cos(angle) * radius;
            float sy = centerY + std::sin(angle) * radius;
            float fade = (float)(i + 1) / (float)segCount;
            D2D1_COLOR_F c = theme::kAccent();
            c.a = 0.15f + fade * 0.85f;
            float dotR = 2.5f + fade * 1.8f;
            D2D1_RECT_F dot = {sx - dotR, sy - dotR, sx + dotR, sy + dotR};
            r.FillRoundedRect(dot, dotR, dotR, c);
        }
    }

    std::wstring text = text_.empty() ? L"Loading..." : text_;
    D2D1_RECT_F textRect = {
        panel.left + 16.0f,
        showSpinner_ ? panel.top + 52.0f : panel.top + 20.0f,
        panel.right - 16.0f,
        panel.bottom - 16.0f
    };
    r.DrawText(text, textRect, theme::kBtnText(), theme::kFontSizeNormal, DWRITE_TEXT_ALIGNMENT_CENTER);
}

bool OverlayWidget::OnMouseMove(const MouseEvent&) {
    return active_ && blockInput_;
}

bool OverlayWidget::OnMouseDown(const MouseEvent&) {
    if (!active_) return false;
    if (dismissOnClick_) {
        active_ = false;
        SyncInputCaptureState();
        return true;
    }
    return blockInput_;
}

bool OverlayWidget::OnMouseUp(const MouseEvent&) {
    return active_ && blockInput_;
}

bool OverlayWidget::OnMouseWheel(const MouseEvent&) {
    return active_ && blockInput_;
}

D2D1_SIZE_F OverlayWidget::SizeHint() const {
    return {fixedW > 0 ? fixedW : 0.0f, fixedH > 0 ? fixedH : 0.0f};
}

// ---- Dialog ----

// ---- ImageView ----

static std::unordered_map<UINT_PTR, ImageViewWidget*> g_animTimerMap;
static std::unordered_map<UINT_PTR, ImageViewWidget*> g_loadingTimerMap;

ImageViewWidget::ImageViewWidget() {
    expanding = true;
}

ImageViewWidget::~ImageViewWidget() {
    ClearBitmapResource();
    if (checkerTileResourceKey_.IsValid()) {
        GlobalResourceStore().Remove(checkerTileResourceKey_);
    }
    ClearTiles();
    if (loadingTimerId_) {
        KillTimer(NULL, loadingTimerId_);
        g_loadingTimerMap.erase(loadingTimerId_);
        loadingTimerId_ = 0;
    }
    if (animTimerId_) {
        KillTimer(NULL, animTimerId_);
        g_animTimerMap.erase(animTimerId_);
        animTimerId_ = 0;
    }
}

void ImageViewWidget::LoadFromFile(const std::wstring& path, Renderer& r) {
    /* 先尝试动画加载（排除 ICO — 多尺寸图标不是动画） */
    std::unique_ptr<Renderer::AnimatedPlayer> player;
    {
        auto dot = path.rfind(L'.');
        bool isIco = (dot != std::wstring::npos &&
                      (_wcsicmp(path.c_str() + dot, L".ico") == 0 ||
                       _wcsicmp(path.c_str() + dot, L".cur") == 0));
        if (!isIco) player = r.OpenAnimatedImage(path);
    }
    if (player && player->FrameCount() > 1) {
        StopAnimation();
        gif_ = std::move(player);
        currentFrame_ = 0;
        /* 合成帧 0 到 CPU 画布，创建 resource 主状态；GPU 位图只是有 RT 时的缓存。 */
        const uint8_t* px = gif_->ComposeTo(0);
        int w = gif_->CanvasWidth();
        int h = gif_->CanvasHeight();
        if (px) SetBitmapResourceFromPixels(px, w, h, w * 4, r, false);
        StartAnimation();
        return;
    }
    /* 静态图 */
    std::vector<uint8_t> pixels;
    int w = 0, h = 0, stride = 0;
    if (r.DecodeImageFileToBgraPremul(path, pixels, w, h, stride)) {
        SetBitmapResourceFromPixels(pixels.data(), w, h, stride, r, true);
    } else {
        ClearImage();
    }
}

void ImageViewWidget::ClearImage() {
    StopAnimation();
    gif_.reset();
    currentFrame_ = 0;
    ClearBitmapResource();
    bitmap_.Reset();
    imgW_ = imgH_ = 0;
    ++bitmapGeneration_;
    if (tiledMode_) ClearTiles();
}

bool ImageViewWidget::CopyPixels(void** outPixels, int* outW, int* outH) const {
    if (outPixels) *outPixels = nullptr;
    if (outW) *outW = 0;
    if (outH) *outH = 0;
    if (!outPixels || !outW || !outH || tiledMode_) return false;

    auto res = GlobalResourceStore().Acquire(bitmapResourceKey_);
    if (!res || !res->bytes || res->format != PixelFormat::BgraPremul ||
        res->width <= 0 || res->height <= 0 ||
        res->stride < res->width * 4) {
        return false;
    }

    const size_t rowBytes = static_cast<size_t>(res->width) * 4u;
    const size_t totalBytes = rowBytes * static_cast<size_t>(res->height);
    void* buf = std::malloc(totalBytes);
    if (!buf) return false;

    auto* dst = static_cast<uint8_t*>(buf);
    const auto* src = res->bytes->data();
    for (int y = 0; y < res->height; ++y) {
        std::memcpy(dst + static_cast<size_t>(y) * rowBytes,
                    src + static_cast<size_t>(y) * res->stride,
                    rowBytes);
    }

    *outPixels = buf;
    *outW = res->width;
    *outH = res->height;
    return true;
}

void ImageViewWidget::ClearBitmapResource() {
    if (!bitmapResourceKey_.IsValid()) return;
    GlobalResourceStore().Remove(bitmapResourceKey_);
    bitmapResourceKey_ = {};
}

bool ImageViewWidget::SetBitmapResourceFromPixels(const void* pixels, int w, int h, int stride,
                                                  Renderer& r, bool resetAnimation) {
    const uint64_t nextGeneration = bitmapGeneration_ + 1;
    ResourceKey resourceKey = GlobalResourceStore().AddImage(
        ResourceKind::Bitmap, nextGeneration, w, h, stride,
        PixelFormat::BgraPremul, pixels, true);
    auto res = GlobalResourceStore().Acquire(resourceKey);
    if (!res || !res->bytes) {
        if (resourceKey.IsValid()) GlobalResourceStore().Remove(resourceKey);
        ClearImage();
        return false;
    }

    auto bmp = r.CreateBitmapFromPixels(res->bytes->data(), res->width, res->height, res->stride);

    ClearBitmapResource();
    if (resetAnimation) {
        StopAnimation();
        gif_.reset();
        currentFrame_ = 0;
    }
    bitmapGeneration_ = nextGeneration;
    bitmap_ = std::move(bmp);
    bitmapResourceKey_ = resourceKey;
    imgW_ = res->width;
    imgH_ = res->height;
    return true;
}

void ImageViewWidget::SetBitmapFromPixels(const void* pixels, int w, int h, int stride, Renderer& r) {
    SetBitmapResourceFromPixels(pixels, w, h, stride, r, true);
}

void ImageViewWidget::CreateEmpty(int w, int h, Renderer& r) {
    if (w <= 0 || h <= 0) {
        ClearImage();
        return;
    }
    const size_t rowBytes = static_cast<size_t>(w) * 4;
    if (rowBytes > static_cast<size_t>(std::numeric_limits<int>::max())) {
        ClearImage();
        return;
    }
    if (static_cast<size_t>(h) > static_cast<size_t>(-1) / rowBytes) {
        ClearImage();
        return;
    }
    std::vector<uint8_t> pixels(rowBytes * static_cast<size_t>(h), 0);
    SetBitmapResourceFromPixels(pixels.data(), w, h, static_cast<int>(rowBytes), r, true);
}

void ImageViewWidget::UpdateRegion(int x, int y, const void* pixels, int w, int h, int stride) {
    if (!pixels || w <= 0 || h <= 0) return;
    const int pitch = (stride > 0) ? stride : (w * 4);
    const size_t rowBytes = static_cast<size_t>(w) * 4;
    if (pitch <= 0 || static_cast<size_t>(pitch) < rowBytes) return;

    auto res = GlobalResourceStore().Acquire(bitmapResourceKey_);
    if (!res || !res->bytes || res->format != PixelFormat::BgraPremul ||
        res->stride < res->width * 4) {
        return;
    }
    const int pxW = res->width;
    const int pxH = res->height;

    if (x < 0 || y < 0 || x + w > pxW || y + h > pxH) return;
    std::vector<uint8_t> updated = *res->bytes;
    const uint8_t* src = static_cast<const uint8_t*>(pixels);
    for (int row = 0; row < h; ++row) {
        uint8_t* dst = updated.data() + static_cast<size_t>(y + row) * res->stride + static_cast<size_t>(x) * 4;
        std::memcpy(dst, src + static_cast<size_t>(row) * pitch, rowBytes);
    }

    const uint64_t nextGeneration = bitmapGeneration_ + 1;
    ResourceKey newKey = GlobalResourceStore().AddImage(
        ResourceKind::Bitmap, nextGeneration, res->width, res->height, res->stride,
        res->format, updated.data(), true);
    if (!newKey.IsValid()) return;
    GlobalResourceStore().Remove(bitmapResourceKey_);
    bitmapResourceKey_ = newKey;
    bitmapGeneration_ = nextGeneration;
    bitmap_.Reset();
}

void ImageViewWidget::SetZoom(float z) {
    zoom_ = std::clamp(z, minZoom_, maxZoom_);
    NotifyViewport();
}

void ImageViewWidget::SetPan(float x, float y) {
    panX_ = x; panY_ = y;
    NotifyViewport();
}

void ImageViewWidget::FitToView() {
    int iw = ImageWidth(), ih = ImageHeight();
    if (iw <= 0 || ih <= 0) return;
    float areaW = rect.right - rect.left;
    float areaH = rect.bottom - rect.top;
    zoom_ = std::min(areaW / (float)iw, areaH / (float)ih);
    if (zoom_ > 1.0f) zoom_ = 1.0f;
    zoom_ = std::clamp(zoom_, minZoom_, maxZoom_);
    panX_ = panY_ = 0;
    NotifyViewport();
}

void ImageViewWidget::ResetView() {
    zoom_ = 1.0f;
    panX_ = panY_ = 0;
    NotifyViewport();
}

void ImageViewWidget::NotifyViewport() {
    if (onViewportChanged) onViewportChanged(zoom_, panX_, panY_);
}

void ImageViewWidget::EnsureCheckerboardTile(Renderer& r) {
    int curTheme = (int)theme::CurrentMode();
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

    /* 生成 16x16 棋盘格位图（2x2 个 8px 格子） */
    if (checkerTileResourceKey_.IsValid()) {
        GlobalResourceStore().Remove(checkerTileResourceKey_);
        checkerTileResourceKey_ = {};
    }
    checkerTile_.Reset();

    const int sz = 16;
    uint8_t pixels[sz * sz * 4];
    uint32_t c1, c2;
    if (theme::CurrentMode() == theme::Mode::Dark) {
        c1 = 0xFF472047;  /* BGRA: 0.25, 0.25, 0.28, 1 */
        c2 = 0xFF383338;  /* BGRA: 0.20, 0.20, 0.22, 1 */
        /* 精确值 */
        c1 = (255u << 24) | (71u << 16) | (64u << 8) | 64u;   /* #404047FF → BGRA */
        c2 = (255u << 24) | (56u << 16) | (51u << 8) | 51u;   /* #333338FF */
    } else {
        c1 = (255u << 24) | (204u << 16) | (204u << 8) | 204u; /* 0.8 */
        c2 = (255u << 24) | (153u << 16) | (153u << 8) | 153u; /* 0.6 */
    }
    for (int y = 0; y < sz; y++) {
        for (int x = 0; x < sz; x++) {
            int ix = x / 8, iy = y / 8;
            uint32_t c = ((ix + iy) % 2 == 0) ? c1 : c2;
            memcpy(pixels + (y * sz + x) * 4, &c, 4);
        }
    }

    const uint64_t nextGeneration = checkerTileGeneration_ + 1;
    ResourceKey resourceKey = GlobalResourceStore().AddImage(
        ResourceKind::Bitmap, nextGeneration, sz, sz, sz * 4,
        PixelFormat::BgraPremul, pixels, true);
    if (!resourceKey.IsValid()) return;

    checkerTileResourceKey_ = resourceKey;
    checkerTileGeneration_ = nextGeneration;
    auto res = GlobalResourceStore().Acquire(checkerTileResourceKey_);
    if (res && res->bytes) {
        checkerTile_ = r.CreateBitmapFromPixels(
            res->bytes->data(), res->width, res->height, res->stride);
    }
    checkerTheme_ = curTheme;
}

void ImageViewWidget::DrawCheckerboard(Renderer& r, const D2D1_RECT_F& area) {
    EnsureCheckerboardTile(r);
    if (!checkerTile_ && !checkerTileResourceKey_.IsValid()) return;
    r.FillRectWithImagePattern(checkerTileResourceKey_, checkerTile_.Get(), area);
}

/* ---- GIF 动画 timer ---- */
void CALLBACK ImageViewWidget::LoadingTimerProc(HWND, UINT, UINT_PTR id, DWORD) {
    auto it = g_loadingTimerMap.find(id);
    if (it == g_loadingTimerMap.end()) return;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM) -> BOOL {
        wchar_t cls[64];
        GetClassNameW(hwnd, cls, 64);
        if (wcscmp(cls, L"UiCore_Window") == 0)
            InvalidateRect(hwnd, nullptr, FALSE);
        return TRUE;
    }, 0);
}

void CALLBACK ImageViewWidget::AnimTimerProc(HWND, UINT, UINT_PTR id, DWORD) {
    auto it = g_animTimerMap.find(id);
    if (it != g_animTimerMap.end()) {
        it->second->AdvanceFrame();
    }
}

void ImageViewWidget::AdvanceFrame() {
    if (!gif_ || gif_->FrameCount() <= 1 ||
        (!bitmap_ && !bitmapResourceKey_.IsValid())) {
        return;
    }
    currentFrame_ = (currentFrame_ + 1) % gif_->FrameCount();

    /* 合成下一帧并同步到 CPU resource；GPU 位图有缓存时由 UpdateRegion 顺带更新。 */
    const uint8_t* px = gif_->ComposeTo(currentFrame_);
    if (px) {
        int w = gif_->CanvasWidth();
        int h = gif_->CanvasHeight();
        UpdateRegion(0, 0, px, w, h, w * 4);
    }

    /* 设下一帧的 delay */
    int delay = gif_->DelayMs(currentFrame_);
    if (animTimerId_) {
        KillTimer(NULL, animTimerId_);
        g_animTimerMap.erase(animTimerId_);
    }
    animTimerId_ = SetTimer(NULL, 0, (UINT)delay, AnimTimerProc);
    if (animTimerId_) g_animTimerMap[animTimerId_] = this;

    /* 请求重绘 — invalidate 当前线程所有 UiCore 窗口 */
    EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM) -> BOOL {
        wchar_t cls[64];
        GetClassNameW(hwnd, cls, 64);
        if (wcscmp(cls, L"UiCore_Window") == 0)
            InvalidateRect(hwnd, nullptr, FALSE);
        return TRUE;
    }, 0);
}

void ImageViewWidget::StartAnimation() {
    if (!gif_ || gif_->FrameCount() <= 1) return;
    StopAnimation();
    int delay = gif_->DelayMs(0);
    animTimerId_ = SetTimer(NULL, 0, (UINT)delay, AnimTimerProc);
    if (animTimerId_) g_animTimerMap[animTimerId_] = this;
}

bool ImageViewWidget::IsAnimated() const {
    return gif_ && gif_->FrameCount() > 1;
}

int ImageViewWidget::FrameCount() const {
    return gif_ ? gif_->FrameCount() : 1;
}

void ImageViewWidget::StopAnimation() {
    if (animTimerId_) {
        KillTimer(NULL, animTimerId_);
        g_animTimerMap.erase(animTimerId_);
        animTimerId_ = 0;
    }
}

void ImageViewWidget::SetLoading(bool on) {
    if (loading_ == on) return;
    loading_ = on;
    loadingAngle_ = 0.0f;
    if (on) {
        // ~60Hz threadless timer drives Invalidate so the spinner actually
        // animates without needing user input. OnDraw advances the angle.
        loadingTimerId_ = SetTimer(NULL, 0, 16, LoadingTimerProc);
        if (loadingTimerId_) g_loadingTimerMap[loadingTimerId_] = this;
    } else {
        if (loadingTimerId_) {
            KillTimer(NULL, loadingTimerId_);
            g_loadingTimerMap.erase(loadingTimerId_);
            loadingTimerId_ = 0;
        }
    }
    EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM) -> BOOL {
        wchar_t cls[64];
        GetClassNameW(hwnd, cls, 64);
        if (wcscmp(cls, L"UiCore_Window") == 0)
            InvalidateRect(hwnd, nullptr, FALSE);
        return TRUE;
    }, 0);
}

static void DrawSpinner(Renderer& r, float cx, float cy, float radius,
                        float angle, const D2D1_COLOR_F& color, float strokeW) {
    constexpr int kSegments = 18;
    constexpr float kSweep = 270.0f;
    constexpr float kPi = 3.14159265f;
    D2D1_POINT_2F prev{};
    for (int i = 0; i <= kSegments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSegments);
        const float a = (angle + kSweep * t) * kPi / 180.0f;
        D2D1_POINT_2F pt = {cx + radius * cosf(a), cy + radius * sinf(a)};
        if (i > 0) {
            D2D1_COLOR_F c = color;
            c.a *= 0.35f + 0.65f * t;
            r.DrawLine(prev.x, prev.y, pt.x, pt.y, c, strokeW);
        }
        prev = pt;
    }
}

void ImageViewWidget::SetTiled(int fullW, int fullH, int tileSize, Renderer& r) {
    ClearTiles();
    ClearBitmapResource();
    StopAnimation();
    gif_.reset();
    currentFrame_ = 0;
    ++tiledGeneration_;
    bitmap_.Reset();
    imgW_ = 0; imgH_ = 0;
    tiledMode_ = true;
    tiledFullW_ = fullW;
    tiledFullH_ = fullH;
    tiledTileSize_ = tileSize > 0 ? tileSize : 512;
}

void ImageViewWidget::SetTile(int tx, int ty, const void* pixels, int w, int h, int stride, Renderer& r) {
    if (!tiledMode_ || !pixels || w <= 0 || h <= 0) return;
    auto key = std::make_pair(tx, ty);
    ResourceKey resourceKey = GlobalResourceStore().AddImage(
        ResourceKind::Tile, tiledGeneration_, w, h, stride,
        PixelFormat::BgraPremul, pixels, true);
    auto res = GlobalResourceStore().Acquire(resourceKey);
    if (!res || !res->bytes) {
        if (resourceKey.IsValid()) GlobalResourceStore().Remove(resourceKey);
        return;
    }

    auto bmp = r.CreateBitmapFromPixels(res->bytes->data(), res->width, res->height, res->stride);

    auto old = tiles_.find(key);
    if (old != tiles_.end()) GlobalResourceStore().Remove(old->second.resourceKey);
    tiles_[key] = { bmp, resourceKey, w, h };
}

void ImageViewWidget::SetTilePreview(const void* pixels, int w, int h, int stride, Renderer& r) {
    if (!tiledMode_ || !pixels || w <= 0 || h <= 0) {
        if (tiledPreviewResourceKey_.IsValid()) {
            GlobalResourceStore().Remove(tiledPreviewResourceKey_);
            tiledPreviewResourceKey_ = {};
        }
        tiledPreview_.Reset();
        tiledPreviewW_ = 0;
        tiledPreviewH_ = 0;
        return;
    }

    ResourceKey resourceKey = GlobalResourceStore().AddImage(
        ResourceKind::Preview, tiledGeneration_, w, h, stride,
        PixelFormat::BgraPremul, pixels, true);
    auto res = GlobalResourceStore().Acquire(resourceKey);
    if (!res || !res->bytes) {
        if (resourceKey.IsValid()) GlobalResourceStore().Remove(resourceKey);
        return;
    }

    auto bmp = r.CreateBitmapFromPixels(res->bytes->data(), res->width, res->height, res->stride);

    if (tiledPreviewResourceKey_.IsValid()) {
        GlobalResourceStore().Remove(tiledPreviewResourceKey_);
    }
    tiledPreview_ = bmp;
    tiledPreviewResourceKey_ = resourceKey;
    tiledPreviewW_ = w;
    tiledPreviewH_ = h;
}

void ImageViewWidget::EvictTile(int tx, int ty) {
    auto it = tiles_.find(std::make_pair(tx, ty));
    if (it == tiles_.end()) return;
    GlobalResourceStore().Remove(it->second.resourceKey);
    tiles_.erase(it);
}

void ImageViewWidget::ClearTiles() {
    for (const auto& kv : tiles_) {
        GlobalResourceStore().Remove(kv.second.resourceKey);
    }
    tiles_.clear();
    if (tiledPreviewResourceKey_.IsValid()) {
        GlobalResourceStore().Remove(tiledPreviewResourceKey_);
        tiledPreviewResourceKey_ = {};
    }
    tiledPreview_.Reset();
    tiledPreviewW_ = 0;
    tiledPreviewH_ = 0;
    if (tiledMode_) {
        tiledMode_ = false;
        tiledFullW_ = 0;
        tiledFullH_ = 0;
    }
}

void ImageViewWidget::DrawTiled(Renderer& r) {
    int effW = ImageWidth(), effH = ImageHeight();
    float drawW = effW * zoom_;
    float drawH = effH * zoom_;
    float areaW = rect.right - rect.left;
    float areaH = rect.bottom - rect.top;
    float cx = rect.left + areaW / 2.0f + panX_;
    float cy = rect.top + areaH / 2.0f + panY_;
    float imgLeft = cx - drawW / 2.0f;
    float imgTop = cy - drawH / 2.0f;

    /* 如果有预览 resource，先画全图预览兜底 */
    if (tiledPreviewResourceKey_.IsValid() || tiledPreview_) {
        D2D1_RECT_F dest = { imgLeft, imgTop, imgLeft + drawW, imgTop + drawH };
        if (checkerboard_) DrawCheckerboard(r, dest);
        auto interp = (!antialias_ && zoom_ >= 4.0f)
            ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
            : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
        r.RecordImage(tiledPreviewResourceKey_, dest,
                      interp == D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                          ? ImageSampling::Nearest
                          : ImageSampling::Linear);
        if (tiledPreview_) r.DrawBitmap(tiledPreview_.Get(), dest, 1.0f, interp);
    }

    /* 计算可见瓦片范围 */
    float visL = (rect.left - imgLeft) / zoom_;
    float visT = (rect.top - imgTop) / zoom_;
    float visR = (rect.right - imgLeft) / zoom_;
    float visB = (rect.bottom - imgTop) / zoom_;

    int ts = tiledTileSize_;
    int txMin = std::max(0, (int)(visL / ts));
    int tyMin = std::max(0, (int)(visT / ts));
    int txMax = std::min((tiledFullW_ + ts - 1) / ts, (int)(visR / ts) + 1);
    int tyMax = std::min((tiledFullH_ + ts - 1) / ts, (int)(visB / ts) + 1);

    /* 诊断用：tile 强制 NEAREST 避免双线性采样在纹理边缘 clamp 产生接缝。
     * 如果缝消失 → 采样问题，下一步解码带 padding；如果仍有缝 → 解码端问题 */
    auto interp = D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR;

    for (int ty = tyMin; ty < tyMax; ty++) {
        for (int tx = txMin; tx < txMax; tx++) {
            auto it = tiles_.find({ tx, ty });
            if (it == tiles_.end()) continue;

            /* 每个 tile dest rect 外扩 1 像素，相邻必有 2 像素重叠。
             * 由于循环按 ty, tx 顺序绘制，后画的 tile 完全覆盖前一片的边缘，
             * 即使有 sub-pixel 对齐偏差或 preview 漏出也被盖住。*/
            int tw = it->second.w;
            int th = it->second.h;
            float x0 = std::floor(imgLeft + (tx * ts)      * zoom_) - 1.0f;
            float y0 = std::floor(imgTop  + (ty * ts)      * zoom_) - 1.0f;
            float x1 = std::ceil (imgLeft + (tx * ts + tw) * zoom_) + 1.0f;
            float y1 = std::ceil (imgTop  + (ty * ts + th) * zoom_) + 1.0f;

            D2D1_RECT_F dest = { x0, y0, x1, y1 };
            r.RecordImage(it->second.resourceKey, dest, ImageSampling::Nearest);
            if (it->second.bmp) r.DrawBitmap(it->second.bmp.Get(), dest, 1.0f, interp);
        }
    }
}

void ImageViewWidget::OnDraw(Renderer& r) {
    // Background
    r.FillRect(rect, theme::kContentBg());

    if (loading_) {
        float cx = (rect.left + rect.right) / 2;
        float cy = (rect.top + rect.bottom) / 2;
        D2D1_COLOR_F spinColor = theme::kAccent();
        spinColor.a = 0.8f;
        DrawSpinner(r, cx, cy, 20.0f, loadingAngle_, spinColor, 2.5f);
        loadingAngle_ += 8.0f;  /* ~45°/frame ≈ 1圈/秒 @60fps */
        if (loadingAngle_ >= 360.0f) loadingAngle_ -= 360.0f;
        return;
    }

    if (tiledMode_) {
        r.PushClip(rect);
        DrawTiled(r);
        r.PopClip();
        return;
    }

    if (!bitmap_ && !bitmapResourceKey_.IsValid()) {
        return;
    }

    r.PushClip(rect);

    float areaW = rect.right - rect.left;
    float areaH = rect.bottom - rect.top;

    /* 有效尺寸：90°/270° 时交换宽高 */
    int effW = ImageWidth();
    int effH = ImageHeight();
    float drawW = effW * zoom_;
    float drawH = effH * zoom_;
    float cx = rect.left + areaW / 2.0f + panX_;
    float cy = rect.top + areaH / 2.0f + panY_;

    D2D1_RECT_F dest = {cx - drawW / 2, cy - drawH / 2,
                         cx + drawW / 2, cy + drawH / 2};

    // Checkerboard behind image (for transparency)
    if (checkerboard_) {
        DrawCheckerboard(r, dest);
    }

    // 缩小 → HIGH_QUALITY_CUBIC（文字/线条清晰）
    // 放大 → NEAREST_NEIGHBOR（锐利像素，跟 Windows 照片查看器一致）
    // 抗锯齿开启：放大也走 HIGH_QUALITY_CUBIC，缩小保持不变
    bool useHQ = (zoom_ < 1.0f) || antialias_;
    auto interp = (!antialias_ && zoom_ >= 1.0f)
        ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
        : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
    const ImageSampling sampling = SamplingForBitmap(useHQ, interp);

    if (rotation_ == 0) {
        r.RecordImage(bitmapResourceKey_, dest, sampling);
        if (useHQ && bitmap_) {
            r.DrawBitmapHQ(bitmap_.Get(), dest, 1.0f,
                           D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        } else if (bitmap_) {
            r.DrawBitmap(bitmap_.Get(), dest, 1.0f, interp);
        }
    } else {
        /* 旋转绘制：先在原点绘制原始尺寸位图，通过变换矩阵旋转+缩放+平移到目标位置 */
        /* 目标中心 */
        float dcx = (dest.left + dest.right) / 2.0f;
        float dcy = (dest.top + dest.bottom) / 2.0f;

        /* 缩放因子：从原始位图尺寸到目标绘制尺寸 */
        float sx = drawW / (float)effW;
        float sy = drawH / (float)effH;

        /* 构建变换：先平移位图中心到原点 → 旋转 → 缩放 → 平移到目标中心 */
        auto xform =
            D2D1::Matrix3x2F::Translation(-(float)imgW_ / 2.0f, -(float)imgH_ / 2.0f) *
            D2D1::Matrix3x2F::Rotation((float)rotation_) *
            D2D1::Matrix3x2F::Scale(sx, sy) *
            D2D1::Matrix3x2F::Translation(dcx, dcy);
        r.PushTransform(xform);

        D2D1_RECT_F src = {0, 0, (float)imgW_, (float)imgH_};
        r.RecordImage(bitmapResourceKey_, src, sampling);
        if (useHQ && bitmap_) {
            r.DrawBitmapHQ(bitmap_.Get(), src, 1.0f,
                           D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        } else if (bitmap_) {
            r.DrawBitmap(bitmap_.Get(), src, 1.0f, interp);
        }
        r.PopTransform();
    }

    // Crop overlay
    if (cropMode_) DrawCropOverlay(r);

    r.PopClip();
}

bool ImageViewWidget::OnMouseDown(const MouseEvent& e) {
    if (!Contains(e.x, e.y) || !HasImage()) return false;

    /* 外部钩子：可在此拦截走拖出等特殊流程；返回 true 表示已处理不进默认 pan */
    if (onMouseDownHook && onMouseDownHook(e.x, e.y, e.leftBtn ? 1 : 0)) return true;

    // Crop mode: check handles first
    if (cropMode_) {
        auto handle = HitTestCropHandle(e.x, e.y);
        if (handle != None) {
            cropDragHandle_ = handle;
            cropDragStartX_ = e.x;
            cropDragStartY_ = e.y;
            cropDragOrigX_ = cropX_;
            cropDragOrigY_ = cropY_;
            cropDragOrigW_ = cropW_;
            cropDragOrigH_ = cropH_;
            return true;
        }
    }

    ConstrainPan();
    dragging_ = true;
    dragStartX_ = e.x;
    dragStartY_ = e.y;
    dragPanX_ = panX_;
    dragPanY_ = panY_;
    return true;
}

bool ImageViewWidget::OnMouseMove(const MouseEvent& e) {
    /* 外部钩子：返回 true 表示已处理（如进入拖出流程）。
     * 同时强制结束 pan 状态，避免事件回来后还认为在拖动。*/
    if (onMouseMoveHook && onMouseMoveHook(e.x, e.y)) {
        dragging_ = false;
        cropDragHandle_ = None;
        return true;
    }

    // Crop handle dragging
    if (cropMode_ && cropDragHandle_ != None) {
        float dix, diy;
        // Delta in image pixel coords
        float dx = (e.x - cropDragStartX_) / zoom_;
        float dy = (e.y - cropDragStartY_) / zoom_;

        float nx = cropDragOrigX_, ny = cropDragOrigY_;
        float nw = cropDragOrigW_, nh = cropDragOrigH_;

        switch (cropDragHandle_) {
            case Move:
                nx = cropDragOrigX_ + dx;
                ny = cropDragOrigY_ + dy;
                break;
            case TopLeft:
                nx = cropDragOrigX_ + dx; ny = cropDragOrigY_ + dy;
                nw = cropDragOrigW_ - dx; nh = cropDragOrigH_ - dy;
                break;
            case Top:
                ny = cropDragOrigY_ + dy; nh = cropDragOrigH_ - dy;
                break;
            case TopRight:
                ny = cropDragOrigY_ + dy;
                nw = cropDragOrigW_ + dx; nh = cropDragOrigH_ - dy;
                break;
            case Right:
                nw = cropDragOrigW_ + dx;
                break;
            case BottomRight:
                nw = cropDragOrigW_ + dx; nh = cropDragOrigH_ + dy;
                break;
            case Bottom:
                nh = cropDragOrigH_ + dy;
                break;
            case BottomLeft:
                nx = cropDragOrigX_ + dx;
                nw = cropDragOrigW_ - dx; nh = cropDragOrigH_ + dy;
                break;
            case Left:
                nx = cropDragOrigX_ + dx; nw = cropDragOrigW_ - dx;
                break;
            default: break;
        }

        // Enforce aspect ratio
        if (cropAspectRatio_ > 0 && cropDragHandle_ != Move) {
            nh = nw / cropAspectRatio_;
        }

        // Apply with minimum size
        if (nw >= 10 && nh >= 10) {
            cropX_ = nx; cropY_ = ny; cropW_ = nw; cropH_ = nh;
            ClampCrop();
            if (onCropChanged) onCropChanged(cropX_, cropY_, cropW_, cropH_);
        }
        return true;
    }

    if (dragging_) {
        panX_ = dragPanX_ + (e.x - dragStartX_);
        panY_ = dragPanY_ + (e.y - dragStartY_);
        ConstrainPan();
        NotifyViewport();
        return true;
    }
    hovered = Contains(e.x, e.y);
    return hovered;
}

bool ImageViewWidget::OnMouseUp(const MouseEvent& e) {
    if (cropDragHandle_ != None) {
        cropDragHandle_ = None;
        return true;
    }
    if (dragging_) {
        dragging_ = false;
        return true;
    }
    return false;
}

void ImageViewWidget::ConstrainPan() {
    float areaW = rect.right - rect.left;
    float areaH = rect.bottom - rect.top;
    float drawW = ImageWidth() * zoom_;
    float drawH = ImageHeight() * zoom_;

    /* 统一公式：maxPan = |drawSize - viewSize| / 2
       图片 > 视口：图片边缘不超出视口边界
       图片 ≤ 视口：图片可在视口内自由滑动，但不超出视口 */
    float maxPanX = std::abs(drawW - areaW) / 2.0f;
    panX_ = std::clamp(panX_, -maxPanX, maxPanX);

    float maxPanY = std::abs(drawH - areaH) / 2.0f;
    panY_ = std::clamp(panY_, -maxPanY, maxPanY);
}

bool ImageViewWidget::OnMouseWheel(const MouseEvent& e) {
    if (!Contains(e.x, e.y) || !HasImage()) return false;

    float factor = (e.delta > 0) ? 1.15f : (1.0f / 1.15f);
    float oldZoom = zoom_;
    float newZoom = std::clamp(zoom_ * factor, minZoom_, maxZoom_);
    /* 跨越 100% 时吸附到 1.0，避免因乘法步长永远错过 1:1 */
    if ((oldZoom < 1.0f && newZoom > 1.0f) || (oldZoom > 1.0f && newZoom < 1.0f)) {
        newZoom = 1.0f;
    }
    zoom_ = newZoom;

    /* 朝鼠标位置缩放：保持鼠标下的像素不动 */
    float areaW = rect.right - rect.left;
    float areaH = rect.bottom - rect.top;
    float relX = e.x - (rect.left + areaW / 2.0f) - panX_;
    float relY = e.y - (rect.top + areaH / 2.0f) - panY_;
    float scale = zoom_ / oldZoom;
    panX_ -= relX * (scale - 1.0f);
    panY_ -= relY * (scale - 1.0f);

    /* 不约束 pan：zoom-toward-cursor 在缩小时天然让 pan 指数衰减趋向 0，
       硬切 clamp 会干扰鸟瞰图定位。约束仅在拖拽开始时一次性应用。 */
    NotifyViewport();
    return true;
}

D2D1_SIZE_F ImageViewWidget::SizeHint() const {
    return {fixedW > 0 ? fixedW : 200.0f, fixedH > 0 ? fixedH : 200.0f};
}

// ---- ImageView Crop ----

void ImageViewWidget::SetCropMode(bool on) {
    cropMode_ = on;
    if (on && cropW_ <= 0 && HasImage()) ResetCrop();
}

void ImageViewWidget::SetCropRect(float x, float y, float w, float h) {
    cropX_ = x; cropY_ = y; cropW_ = w; cropH_ = h;
    ClampCrop();
}

void ImageViewWidget::GetCropRect(float& x, float& y, float& w, float& h) const {
    x = cropX_; y = cropY_; w = cropW_; h = cropH_;
}

void ImageViewWidget::ResetCrop() {
    float iw = (float)ImageWidth(), ih = (float)ImageHeight();
    cropX_ = 0; cropY_ = 0;
    cropW_ = iw; cropH_ = ih;
    if (cropAspectRatio_ > 0 && iw > 0 && ih > 0) {
        float curRatio = iw / ih;
        if (curRatio > cropAspectRatio_) {
            cropW_ = ih * cropAspectRatio_;
            cropH_ = ih;
        } else {
            cropW_ = iw;
            cropH_ = iw / cropAspectRatio_;
        }
        // Center the crop rect
        cropX_ = (iw - cropW_) / 2;
        cropY_ = (ih - cropH_) / 2;
    }
}

void ImageViewWidget::ClampCrop() {
    float iw = (float)ImageWidth(), ih = (float)ImageHeight();
    if (iw <= 0 || ih <= 0) return;
    cropW_ = std::max(10.0f, std::min(cropW_, iw));
    cropH_ = std::max(10.0f, std::min(cropH_, ih));
    cropX_ = std::clamp(cropX_, 0.0f, iw - cropW_);
    cropY_ = std::clamp(cropY_, 0.0f, ih - cropH_);
}

void ImageViewWidget::ScreenToImage(float sx, float sy, float& ix, float& iy) const {
    float areaW = rect.right - rect.left;
    float areaH = rect.bottom - rect.top;
    float cx = rect.left + areaW / 2 + panX_;
    float cy = rect.top + areaH / 2 + panY_;
    float effW = (float)ImageWidth(), effH = (float)ImageHeight();
    ix = (sx - cx) / zoom_ + effW / 2;
    iy = (sy - cy) / zoom_ + effH / 2;
}

void ImageViewWidget::ImageToScreen(float ix, float iy, float& sx, float& sy) const {
    float areaW = rect.right - rect.left;
    float areaH = rect.bottom - rect.top;
    float cx = rect.left + areaW / 2 + panX_;
    float cy = rect.top + areaH / 2 + panY_;
    float effW = (float)ImageWidth(), effH = (float)ImageHeight();
    sx = (ix - effW / 2) * zoom_ + cx;
    sy = (iy - effH / 2) * zoom_ + cy;
}

D2D1_RECT_F ImageViewWidget::CropScreenRect() const {
    float l, t, r, b;
    ImageToScreen(cropX_, cropY_, l, t);
    ImageToScreen(cropX_ + cropW_, cropY_ + cropH_, r, b);
    return {l, t, r, b};
}

ImageViewWidget::CropHandle ImageViewWidget::HitTestCropHandle(float sx, float sy) const {
    auto cr = CropScreenRect();
    float hw = 8.0f;  // handle hit size

    // Corners
    if (std::abs(sx - cr.left) < hw && std::abs(sy - cr.top) < hw) return TopLeft;
    if (std::abs(sx - cr.right) < hw && std::abs(sy - cr.top) < hw) return TopRight;
    if (std::abs(sx - cr.left) < hw && std::abs(sy - cr.bottom) < hw) return BottomLeft;
    if (std::abs(sx - cr.right) < hw && std::abs(sy - cr.bottom) < hw) return BottomRight;

    // Edges
    if (std::abs(sx - cr.left) < hw && sy > cr.top && sy < cr.bottom) return Left;
    if (std::abs(sx - cr.right) < hw && sy > cr.top && sy < cr.bottom) return Right;
    if (std::abs(sy - cr.top) < hw && sx > cr.left && sx < cr.right) return Top;
    if (std::abs(sy - cr.bottom) < hw && sx > cr.left && sx < cr.right) return Bottom;

    // Inside = move
    if (sx > cr.left && sx < cr.right && sy > cr.top && sy < cr.bottom) return Move;

    return None;
}

void ImageViewWidget::DrawCropOverlay(Renderer& r) {
    auto cr = CropScreenRect();
    D2D1_COLOR_F mask = {0, 0, 0, 0.5f};

    // Switch to aliased mode for mask rectangles to avoid antialiased seams
    auto* rt = r.RT();
    if (rt) rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);

    // Draw darkened regions outside crop rect (4 rectangles)
    r.FillRect({rect.left, rect.top, rect.right, cr.top}, mask);       // top
    r.FillRect({rect.left, cr.bottom, rect.right, rect.bottom}, mask); // bottom
    r.FillRect({rect.left, cr.top, cr.left, cr.bottom}, mask);         // left
    r.FillRect({cr.right, cr.top, rect.right, cr.bottom}, mask);       // right

    // Restore antialiased mode
    if (rt) rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // Crop border
    D2D1_COLOR_F borderColor = theme::white;
    r.DrawRect(cr, borderColor, 1.0f);

    // Rule of thirds lines
    D2D1_COLOR_F gridColor = {1, 1, 1, 0.3f};
    float w = cr.right - cr.left, h = cr.bottom - cr.top;
    r.DrawLine(cr.left + w/3, cr.top, cr.left + w/3, cr.bottom, gridColor, 0.5f);
    r.DrawLine(cr.left + 2*w/3, cr.top, cr.left + 2*w/3, cr.bottom, gridColor, 0.5f);
    r.DrawLine(cr.left, cr.top + h/3, cr.right, cr.top + h/3, gridColor, 0.5f);
    r.DrawLine(cr.left, cr.top + 2*h/3, cr.right, cr.top + 2*h/3, gridColor, 0.5f);

    // Corner handles (small squares)
    float hs = 5.0f;
    auto drawHandle = [&](float x, float y) {
        D2D1_RECT_F hr = {x - hs, y - hs, x + hs, y + hs};
        r.FillRect(hr, theme::white);
        r.DrawRect(hr, {0,0,0,0.3f}, 1.0f);
    };
    drawHandle(cr.left, cr.top);
    drawHandle(cr.right, cr.top);
    drawHandle(cr.left, cr.bottom);
    drawHandle(cr.right, cr.bottom);

    // Edge midpoint handles
    float mx = (cr.left + cr.right) / 2, my = (cr.top + cr.bottom) / 2;
    drawHandle(mx, cr.top);
    drawHandle(mx, cr.bottom);
    drawHandle(cr.left, my);
    drawHandle(cr.right, my);
}

// ---- IconButton ----

IconButtonWidget::IconButtonWidget(const std::string& svgContent, bool ghost)
    : svgContent_(svgContent), ghost_(ghost) {
    /* L148-4: 图标按钮默认手型 (跟 ButtonWidget 一致)。 */
    cursor = CursorKind::Pointer;}

static D2D1_COLOR_F ResolveIconButtonColorRole(int role) {
    switch (role) {
        case 1:  return theme::kTitleBarText();
        case 2:  return theme::kContentText();
        case 3:  return theme::kAccent();
        case 4:  return theme::status::danger;
        case 5:  return theme::kDivider();
        case 0:
        default: return theme::kBtnText();
    }
}

void IconButtonWidget::SetSvg(const std::string& svgContent) {
    svgContent_ = svgContent;
    iconParsed_ = false;
    icon_ = SvgIcon{};
}

void IconButtonWidget::OnDraw(Renderer& r) {
    // Lazy-parse SVG on first draw (needs renderer's factory)
    if (!iconParsed_) {
        icon_ = r.ParseSvgIcon(svgContent_);
        iconParsed_ = true;
    }

    // Background
    if (ghost_) {
        // Ghost mode: transparent by default, show bg on hover/press
        // (除非 hoverVisual_=false, 那就永远只画 icon —— titlebar 装饰按钮
        //  /状态指示器场景).
        if (hoverVisual_) {
            D2D1_COLOR_F bg = {0, 0, 0, 0};
            if (pressed)      bg = theme::kBtnPress();
            else if (hovered) bg = theme::kBtnHover();
            if (bg.a > 0) r.FillRoundedRect(rect, cornerRadius_, cornerRadius_, bg);
        }
    } else {
        // Normal mode: always show button background
        D2D1_COLOR_F bg = theme::kBtnNormal();
        if (pressed)      bg = theme::kBtnPress();
        else if (hovered) bg = theme::kBtnHover();
        r.FillRoundedRect(rect, cornerRadius_, cornerRadius_, bg);
        r.DrawRoundedRect(rect, cornerRadius_, cornerRadius_, theme::kDivider(), 0.5f);
    }

    // Icon
    D2D1_COLOR_F iconC = fixedColor_ ? iconColor_
                                      : ResolveIconButtonColorRole(iconColorRole_);
    D2D1_RECT_F iconRect = {
        rect.left + iconPad_, rect.top + iconPad_,
        rect.right - iconPad_, rect.bottom - iconPad_
    };
    r.DrawSvgIcon(icon_, iconRect, iconC);
}

bool IconButtonWidget::OnMouseMove(const MouseEvent& e) {
    hovered = Contains(e.x, e.y);
    return hovered;
}

bool IconButtonWidget::OnMouseDown(const MouseEvent& e) {
    if (Contains(e.x, e.y)) { pressed = true; return true; }
    return false;
}

bool IconButtonWidget::OnMouseUp(const MouseEvent& e) {
    pressed = false;
    return Contains(e.x, e.y);
}

D2D1_SIZE_F IconButtonWidget::SizeHint() const {
    float w = fixedW > 0 ? fixedW : 32.0f;
    float h = fixedH > 0 ? fixedH : 32.0f;
    return {w, h};
}

// ---- CaptionButton ----

void CaptionButtonWidget::OnDraw(Renderer& r) {
    D2D1_COLOR_F bg = {0, 0, 0, 0};
    if (isClose) {
        if (pressed) bg = theme::kCloseBtnPress();
        else if (hovered) bg = theme::kCloseBtnHover();
    } else {
        if (pressed) bg = theme::kBtnPress();
        else if (hovered) bg = theme::kBtnHover();
    }
    r.FillRect(rect, bg);
    D2D1_COLOR_F fg = (isClose && (hovered || pressed))
        ? D2D1_COLOR_F{1, 1, 1, 1} : theme::kBtnText();
    r.DrawIcon(icon, rect, fg, theme::kFontSizeIcon);
}

// ---- TitleBar ----

TitleBarWidget::TitleBarWidget(const std::wstring& title) : title_(title) {
    fixedH = theme::kTitleBarHeight;

    // Create built-in caption buttons
    auto minB = std::make_shared<CaptionButtonWidget>();
    minB->icon = L"\xE921";  // Minimize
    minB->fixedW = theme::kCaptionBtnWidth;
    minB->id = "_caption_min";
    minBtn_ = minB;
    AddChild(minBtn_);

    auto maxB = std::make_shared<CaptionButtonWidget>();
    maxB->icon = L"\xE922";  // Maximize
    maxB->fixedW = theme::kCaptionBtnWidth;
    maxB->id = "_caption_max";
    maxBtn_ = maxB;
    AddChild(maxBtn_);

    auto closeB = std::make_shared<CaptionButtonWidget>();
    closeB->icon = L"\xE8BB";  // Close
    closeB->isClose = true;
    closeB->fixedW = theme::kCaptionBtnWidth;
    closeB->id = "_caption_close";
    closeBtn_ = closeB;
    AddChild(closeBtn_);
}

TitleBarWidget::~TitleBarWidget() {
    if (userIconResourceKey_.IsValid()) {
        GlobalResourceStore().Remove(userIconResourceKey_);
    }
    if (exeIconResourceKey_.IsValid()) {
        GlobalResourceStore().Remove(exeIconResourceKey_);
    }
}

void TitleBarWidget::OnDraw(Renderer& r) {
    // Background
    D2D1_COLOR_F bg = hasCustomBg_
        ? customBg_
        : ((bgColor.a > 0.0f) ? bgColor : theme::kTitleBarBg());
    r.FillRect(rect, bg);

    // 三段图标查找：1) 用户显式设的 RGBA  2) EXE 嵌入资源 ID=1  3) 不画
    float titleLeft = rect.left + 12;
    if (showIcon_) {
        /* ResourceStore::Clear/PurgeGeneration can invalidate a ResourceKey
         * without notifying this widget. Drop stale keys before selecting an
         * icon; otherwise exeIconAttempted_ suppresses the only reload path
         * forever and RecordImage records a resource that no longer exists. */
        auto hasResource = [](const ResourceKey& key) {
            auto resource = GlobalResourceStore().Acquire(key);
            return resource && resource->bytes;
        };
        if (userIconResourceKey_.IsValid() && !hasResource(userIconResourceKey_)) {
            userIconResourceKey_ = {};
            userIconW_ = userIconH_ = 0;
            iconBitmap_.Reset();
        }
        if (exeIconResourceKey_.IsValid() && !hasResource(exeIconResourceKey_)) {
            exeIconResourceKey_ = {};
            exeIconAttempted_ = false;
            iconBitmap_.Reset();
        }

        // 懒加载 EXE 资源到 CPU store（只尝试一次，失败就不再 retry）
        if (!userIconResourceKey_.IsValid() && !exeIconResourceKey_.IsValid() && !exeIconAttempted_) {
            exeIconAttempted_ = true;
            HICON h = (HICON)LoadImageW(GetModuleHandleW(nullptr),
                                        MAKEINTRESOURCEW(1), IMAGE_ICON,
                                        kTitleBarExeIconSourcePx,
                                        kTitleBarExeIconSourcePx,
                                        LR_DEFAULTCOLOR);
            if (h) {
                std::vector<uint8_t> pixels;
                int w = 0, hgt = 0, stride = 0;
                if (r.DecodeHICONToBgraPremul(h, pixels, w, hgt, stride)) {
                    ResourceKey resourceKey = GlobalResourceStore().AddImage(
                        ResourceKind::Icon, kTitleBarIconGeneration, w, hgt, stride,
                        PixelFormat::BgraPremul, pixels.data(), true);
                    if (resourceKey.IsValid()) {
                        exeIconResourceKey_ = resourceKey;
                    }
                }
                DestroyIcon(h);
            }
        }

        const ResourceKey iconResourceKey = userIconResourceKey_.IsValid()
            ? userIconResourceKey_
            : exeIconResourceKey_;
        auto iconResource = GlobalResourceStore().Acquire(iconResourceKey);

        // 懒加载 icon resource → D2D 位图；纯录制阶段没有 RT 时只保留 ResourceKey。
        if (!iconBitmap_ && iconResource && iconResource->bytes) {
            iconBitmap_ = r.CreateBitmapFromPixels(
                iconResource->bytes->data(), iconResource->width,
                iconResource->height, iconResource->stride);
        }
        if (iconResource && iconResource->bytes) {
            /* 源位图 (EXE 图标 128px / 用户设的高分辨率图) 一律缩小到槽位,
             * 用 HighQualityCubic —— Linear 只采 2x2, 缩小比大于 2 会丢细节
             * 发虚 / 抖动。 */
            const float iconLeft = rect.left + kTitleBarIconLeft;
            const float iconTop  = rect.top  + kTitleBarIconTop;
            D2D1_RECT_F iconRect = {iconLeft, iconTop,
                                    iconLeft + kTitleBarIconSize,
                                    iconTop  + kTitleBarIconSize};
            r.RecordImage(iconResourceKey, iconRect,
                          ImageSampling::HighQualityCubic);
            if (iconBitmap_) r.DrawBitmapHQ(iconBitmap_.Get(), iconRect, 1.0f);
            titleLeft = iconRect.right + kTitleBarTitleGap;
        }
        // 没图就什么都不画，title 留在 left+12
    }

    // Title text
    float textRight = closeBtn_ ? closeBtn_->rect.left : rect.right;
    if (!customWidgets_.empty()) {
        textRight = customWidgets_.front()->rect.left - 8;
    }
    D2D1_RECT_F titleRect = {titleLeft, rect.top, textRight, rect.bottom};
    r.DrawText(title_, titleRect, theme::kTitleBarText(), theme::kFontSizeTitle,
               DWRITE_TEXT_ALIGNMENT_LEADING,
               (DWRITE_FONT_WEIGHT)titleWeight_);
}

void TitleBarWidget::SetIconFromPixels(const uint8_t* rgba, int w, int h) {
    iconBitmap_.Reset();          // 旧位图作废，下次 OnDraw 懒重建
    if (userIconResourceKey_.IsValid()) {
        GlobalResourceStore().Remove(userIconResourceKey_);
        userIconResourceKey_ = {};
    }
    if (!rgba || w <= 0 || h <= 0) {
        userIconW_ = userIconH_ = 0;
        exeIconAttempted_ = false; // 允许下次 OnDraw 再去试 EXE 图标
        return;
    }

    /* 入参字节序是 BGRA 预乘 (跟 32bpp DIB / DecodeHICONToBgraPremul 一致)。
     * 以前这里写 PixelFormat::Rgba —— 那个枚举名是错的, 它在 renderer 里跟
     * BgraPremul 走同一条 CreateBitmapFromPixels 分支, 从来没做过 R/B 交换。
     * 名字误导下游按字面 swap R/B, 橙色图标画成了蓝色 (GuoheView 踩过)。 */
    ResourceKey resourceKey = GlobalResourceStore().AddImage(
        ResourceKind::Icon, kTitleBarIconGeneration, w, h, w * 4,
        PixelFormat::BgraPremul, rgba, true);
    if (!resourceKey.IsValid()) {
        userIconW_ = userIconH_ = 0;
        return;
    }
    userIconResourceKey_ = resourceKey;
    userIconW_ = w;
    userIconH_ = h;
}

void TitleBarWidget::DoLayout() {
    float h = rect.bottom - rect.top;
    float btnW = theme::kCaptionBtnWidth;

    // Caption buttons at the right edge: [min][max][close]
    float bx = rect.right;

    bx -= btnW;
    closeBtn_->rect = {bx, rect.top, bx + btnW, rect.bottom};
    bx -= btnW;
    maxBtn_->rect = {bx, rect.top, bx + btnW, rect.bottom};
    bx -= btnW;
    minBtn_->rect = {bx, rect.top, bx + btnW, rect.bottom};

    // Custom widgets: laid out right-to-left before the caption buttons
    float cx = bx - 4;
    for (int i = (int)customWidgets_.size() - 1; i >= 0; --i) {
        auto& cw = customWidgets_[i];
        if (!cw->visible) continue;
        float ww = cw->fixedW > 0 ? cw->fixedW : 36.0f;
        float wh = cw->fixedH > 0 ? cw->fixedH : h;
        float wy = rect.top + (h - wh) / 2.0f;
        cx -= ww;
        cw->rect = {cx, wy, cx + ww, wy + wh};
        cx -= 2;
    }
}

D2D1_SIZE_F TitleBarWidget::SizeHint() const {
    return {0, theme::kTitleBarHeight};
}

// ---- CustomWidget ----

void CustomWidget::OnDraw(Renderer& r) {
    Widget::OnDraw(r);
    if (drawCb) {
        if (auto* recorder = r.DisplayListRecorderTarget()) {
            DrawApiContext drawCtx(nullptr, recorder);
            drawCb(apiHandle, &drawCtx, ToUiRect(), drawUd);
            return;
        }
        DrawApiContext drawCtx(r);
        drawCb(apiHandle, &drawCtx, ToUiRect(), drawUd);
    }
}

bool CustomWidget::OnMouseDown(const MouseEvent& e) {
    if (!Contains(e.x, e.y)) {
        focused = false;
        return false;
    }
    focused = true;
    if (mouseDownCb) {
        return mouseDownCb(apiHandle, e.x, e.y, e.leftBtn ? 1 : 0, mouseDownUd) != 0;
    }
    return true;
}

bool CustomWidget::OnMouseMove(const MouseEvent& e) {
    hovered = Contains(e.x, e.y);
    if (mouseMoveCb) {
        return mouseMoveCb(apiHandle, e.x, e.y, e.leftBtn ? 1 : 0, mouseMoveUd) != 0;
    }
    return hovered;
}

bool CustomWidget::OnMouseUp(const MouseEvent& e) {
    if (mouseUpCb) {
        return mouseUpCb(apiHandle, e.x, e.y, 0, mouseUpUd) != 0;
    }
    return false;
}

bool CustomWidget::OnMouseWheel(const MouseEvent& e) {
    if (!Contains(e.x, e.y)) return false;
    if (mouseWheelCb) {
        return mouseWheelCb(apiHandle, e.x, e.y, e.delta, mouseWheelUd) != 0;
    }
    return false;
}

bool CustomWidget::OnKeyDown(int vk) {
    if (!focused) return false;
    if (keyDownCb) {
        return keyDownCb(apiHandle, vk, keyDownUd) != 0;
    }
    return false;
}

bool CustomWidget::OnKeyChar(wchar_t ch) {
    if (!focused) return false;
    if (charCb) {
        return charCb(apiHandle, (int)ch, charUd) != 0;
    }
    return false;
}

void CustomWidget::DoLayout() {
    Widget::DoLayout();
    if (layoutCb) {
        layoutCb(apiHandle, ToUiRect(), layoutUd);
    }
}

D2D1_SIZE_F CustomWidget::SizeHint() const {
    return {fixedW > 0 ? fixedW : 100.0f, fixedH > 0 ? fixedH : 100.0f};
}

// ---- MenuBar ----

void MenuBarWidget::AddMenu(const std::wstring& text, ContextMenuPtr menu) {
    menus_.push_back({text, std::move(menu), 0, 0});
}

void MenuBarWidget::DoLayout() {
    float x = rect.left + 4.0f;
    float padH = 16.0f;
    for (auto& m : menus_) {
        float tw = theme::kFontSizeNormal * 0.65f * (float)m.text.size() + padH * 2;
        m.x = x;
        m.w = tw;
        x += tw;
    }
}

void MenuBarWidget::OnDraw(Renderer& r) {
    // Background
    r.FillRect(rect, theme::kToolbarBg());

    float y = rect.top;
    float h = rect.bottom - rect.top;

    for (int i = 0; i < (int)menus_.size(); i++) {
        auto& m = menus_[i];
        D2D1_RECT_F itemRect = {m.x, y, m.x + m.w, y + h};

        if (i == openIndex_) {
            r.FillRect(itemRect, theme::kAccent());
            r.DrawText(m.text, itemRect, D2D1_COLOR_F{1,1,1,1}, theme::kFontSizeNormal,
                       DWRITE_TEXT_ALIGNMENT_CENTER);
        } else if (i == hoveredIndex_) {
            r.FillRect(itemRect, theme::kBtnHover());
            r.DrawText(m.text, itemRect, theme::kBtnText(), theme::kFontSizeNormal,
                       DWRITE_TEXT_ALIGNMENT_CENTER);
        } else {
            r.DrawText(m.text, itemRect, theme::kBtnText(), theme::kFontSizeNormal,
                       DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }

    // Bottom border
    D2D1_RECT_F border = {rect.left, rect.bottom - 1, rect.right, rect.bottom};
    r.FillRect(border, theme::kDivider());
}

int MenuBarWidget::MenuHitTest(float x, float y) const {
    if (y < rect.top || y > rect.bottom) return -1;
    for (int i = 0; i < (int)menus_.size(); i++) {
        if (x >= menus_[i].x && x < menus_[i].x + menus_[i].w) return i;
    }
    return -1;
}

bool MenuBarWidget::OnMouseMove(const MouseEvent& e) {
    int prev = hoveredIndex_;
    hoveredIndex_ = MenuHitTest(e.x, e.y);

    // If a menu is open and user hovers to another menu item, switch
    if (openIndex_ >= 0 && hoveredIndex_ >= 0 && hoveredIndex_ != openIndex_) {
        CloseOpenMenu();
        openIndex_ = hoveredIndex_;
        auto& m = menus_[openIndex_];
        if (m.menu && hwnd_) {
            float scale = 1.0f;
            { HDC hdc = GetDC(hwnd_); if (hdc) { scale = (float)GetDeviceCaps(hdc, LOGPIXELSX) / 96.0f; ReleaseDC(hwnd_, hdc); } }
            POINT pt = {(LONG)(m.x * scale), (LONG)(rect.bottom * scale)};
            ClientToScreen(hwnd_, &pt);
            m.menu->ShowPopup(hwnd_, pt.x, pt.y);
        }
    }

    return hoveredIndex_ != prev;
}

bool MenuBarWidget::OnMouseDown(const MouseEvent& e) {
    int idx = MenuHitTest(e.x, e.y);
    if (idx < 0) return false;

    if (openIndex_ == idx) {
        CloseOpenMenu();
    } else {
        CloseOpenMenu();
        openIndex_ = idx;
        auto& m = menus_[openIndex_];
        if (m.menu && hwnd_) {
            float scale = 1.0f;
            { HDC hdc = GetDC(hwnd_); if (hdc) { scale = (float)GetDeviceCaps(hdc, LOGPIXELSX) / 96.0f; ReleaseDC(hwnd_, hdc); } }
            POINT pt = {(LONG)(m.x * scale), (LONG)(rect.bottom * scale)};
            ClientToScreen(hwnd_, &pt);
            m.menu->ShowPopup(hwnd_, pt.x, pt.y);
        }
    }
    return true;
}

bool MenuBarWidget::OnMouseUp(const MouseEvent& e) {
    return MenuHitTest(e.x, e.y) >= 0;
}

void MenuBarWidget::CloseOpenMenu() {
    if (openIndex_ >= 0 && openIndex_ < (int)menus_.size()) {
        if (menus_[openIndex_].menu) menus_[openIndex_].menu->Close();
    }
    openIndex_ = -1;
}

// ---- Splitter ----

void SplitterWidget::DoLayout() {
    if (children_.size() < 2) { Widget::DoLayout(); return; }

    auto& first  = children_[0];
    auto& second = children_[1];
    if (!first->visible && !second->visible) return;

    float cx = ContentLeft(), cy = ContentTop();
    float cw = ContentWidth(), ch = ContentHeight();

    if (!vertical_) {
        // Horizontal split: [left | bar | right]
        float avail = cw - barSize_;
        float leftW = avail * ratio_;
        leftW = std::clamp(leftW, first->minW, std::min(first->maxW, avail - second->minW));
        float rightW = avail - leftW;
        rightW = std::clamp(rightW, second->minW, second->maxW);

        first->rect  = {cx, cy, cx + leftW, cy + ch};
        second->rect = {cx + leftW + barSize_, cy, cx + cw, cy + ch};
    } else {
        // Vertical split: [top / bar / bottom]
        float avail = ch - barSize_;
        float topH = avail * ratio_;
        topH = std::clamp(topH, first->minH, std::min(first->maxH, avail - second->minH));
        float bottomH = avail - topH;
        bottomH = std::clamp(bottomH, second->minH, second->maxH);

        first->rect  = {cx, cy, cx + cw, cy + topH};
        second->rect = {cx, cy + topH + barSize_, cx + cw, cy + ch};
    }

    first->DoLayout();
    second->DoLayout();
}

void SplitterWidget::OnDraw(Renderer& r) {
    Widget::OnDraw(r);
    if (children_.size() < 2) return;

    // Draw the splitter bar
    float cx = ContentLeft(), cy = ContentTop();
    float cw = ContentWidth(), ch = ContentHeight();
    D2D1_RECT_F bar;

    if (!vertical_) {
        float leftW = (cw - barSize_) * ratio_;
        bar = {cx + leftW, cy, cx + leftW + barSize_, cy + ch};
    } else {
        float topH = (ch - barSize_) * ratio_;
        bar = {cx, cy + topH, cx + cw, cy + topH + barSize_};
    }

    D2D1_COLOR_F barColor = dragging_ ? theme::kAccent() : theme::kDivider();
    r.FillRect(bar, barColor);
}

bool SplitterWidget::OnMouseDown(const MouseEvent& e) {
    if (children_.size() < 2) return false;

    float cx = ContentLeft(), cy = ContentTop();
    float cw = ContentWidth(), ch = ContentHeight();

    if (!vertical_) {
        float leftW = (cw - barSize_) * ratio_;
        float barX = cx + leftW;
        if (e.x >= barX && e.x <= barX + barSize_) {
            dragging_ = true;
            dragOffset_ = e.x - barX;
            return true;
        }
    } else {
        float topH = (ch - barSize_) * ratio_;
        float barY = cy + topH;
        if (e.y >= barY && e.y <= barY + barSize_) {
            dragging_ = true;
            dragOffset_ = e.y - barY;
            return true;
        }
    }
    return false;
}

bool SplitterWidget::OnMouseMove(const MouseEvent& e) {
    if (!dragging_) {
        // Update cursor
        if (children_.size() >= 2) {
            float cx = ContentLeft(), cy = ContentTop();
            float cw = ContentWidth(), ch = ContentHeight();
            if (!vertical_) {
                float barX = cx + (cw - barSize_) * ratio_;
                if (e.x >= barX && e.x <= barX + barSize_ && e.y >= cy && e.y <= cy + ch) {
                    SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
                    return true;
                }
            } else {
                float barY = cy + (ch - barSize_) * ratio_;
                if (e.y >= barY && e.y <= barY + barSize_ && e.x >= cx && e.x <= cx + cw) {
                    SetCursor(LoadCursor(nullptr, IDC_SIZENS));
                    return true;
                }
            }
        }
        return false;
    }

    float cx = ContentLeft(), cy = ContentTop();
    float cw = ContentWidth(), ch = ContentHeight();

    float minR = 0.05f, maxR = 0.95f;
    if (children_.size() >= 2) {
        if (!vertical_) {
            float avail = cw - barSize_;
            if (avail > 0) {
                minR = std::max(0.05f, children_[0]->minW / avail);
                maxR = std::min(0.95f, (avail - children_[1]->minW) / avail);
            }
        } else {
            float avail = ch - barSize_;
            if (avail > 0) {
                minR = std::max(0.05f, children_[0]->minH / avail);
                maxR = std::min(0.95f, (avail - children_[1]->minH) / avail);
            }
        }
    }

    if (!vertical_) {
        float newLeft = e.x - dragOffset_ - cx;
        ratio_ = std::clamp(newLeft / (cw - barSize_), minR, maxR);
    } else {
        float newTop = e.y - dragOffset_ - cy;
        ratio_ = std::clamp(newTop / (ch - barSize_), minR, maxR);
    }
    DoLayout();
    return true;
}

bool SplitterWidget::OnMouseUp(const MouseEvent& e) {
    if (dragging_) {
        dragging_ = false;
        return true;
    }
    return false;
}

// ---- Expander ----

ExpanderWidget::ExpanderWidget(const std::wstring& header) : headerText_(header) {
    // Expander should NOT be expanding — its height is driven by SizeHint (header + animated content)
}

void ExpanderWidget::SetExpanded(bool v) {
    if (expanded_ == v) return;
    expanded_ = v;
    expandAnim_.Retarget(expanded_ ? 1.0f : 0.0f,
                         AnimationSpec{180.0f, EasingFunction::EaseOutCubic});
    animating_ = expandAnim_.Active();
    ui::GetContext().RegisterAnimatingWidget(
        this, AnimationInvalidation::Paint | AnimationInvalidation::Layout |
                  AnimationInvalidation::HitTest);
    if (onExpandedChanged) onExpandedChanged(expanded_);
}

void ExpanderWidget::UpdateAnimation() {
    expandAnim_.SetIdleTarget(expanded_ ? 1.0f : 0.0f);
    expandAnim_.SampleFrame(GetTickCount64());
    animating_ = expandAnim_.Active();
}

void ExpanderWidget::DoLayout() {
    // Measure content height: layout children in a temp area to get total height
    float cx = rect.left + padL;
    float cy = rect.top + headerHeight_ + padT;
    float cw = rect.right - rect.left - padL - padR;

    // Compute natural content height
    float contentH = 0;
    float gap = 4.0f;
    int visCount = 0;
    for (auto& child : children_) {
        if (!child->visible) continue;
        float h = child->fixedH > 0 ? child->fixedH : child->SizeHint().height;
        if (h <= 0) h = 24.0f;
        contentH += h;
        visCount++;
    }
    if (visCount > 1) contentH += gap * (visCount - 1);
    measuredContentH_ = contentH;

    // Animated visible content height
    float progress = expandAnim_.Current();
    float visibleH = measuredContentH_ * progress;

    // Layout children within visible area
    float y = cy;
    for (auto& child : children_) {
        if (!child->visible) continue;
        float h = child->fixedH > 0 ? child->fixedH : child->SizeHint().height;
        if (h <= 0) h = 24.0f;
        float w = child->fixedW > 0 ? child->fixedW : cw;
        if (child->percentW >= 0) w = cw * child->percentW / 100.0f;
        if (child->percentH >= 0) h = visibleH * child->percentH / 100.0f;
        if (child->expanding) w = cw;

        child->rect = {cx, y, cx + w, y + h};
        child->DoLayout();
        y += h + gap;
    }
}

void ExpanderWidget::OnDraw(Renderer& r) {
    // OnDraw only draws the header; children are handled by DrawTree
}

void ExpanderWidget::DrawTree(Renderer& r) {
    if (!visible) return;
    paintedOnce_ = true;   // L45: mount-phase transition gate; Expander DrawTree 自绘 header 不调 OnDraw, 这里手动标记

    bool dark = theme::IsDark();
    float fontSize = (css.fontSize > 0) ? css.fontSize : theme::kFontSizeNormal;
    D2D1_COLOR_F fg = css.hasFg ? css.fg : theme::kBtnText();

    // Header background: custom bgColor wins, otherwise the hover tint.
    D2D1_RECT_F headerRect = {rect.left, rect.top, rect.right, rect.top + headerHeight_};
    D2D1_COLOR_F headerBg;
    if (bgColor.a > 0) {
        headerBg = bgColor;
    } else {
        headerBg = headerHovered_
            ? (dark ? theme::Rgba(0xFF,0xFF,0xFF,0.05f) : theme::Rgba(0x00,0x00,0x00,0.03f))
            : theme::transparent;
    }
    r.FillRect(headerRect, headerBg);

    float chevronSize = 12.0f;
    float chevronX = rect.left + 12;
    float chevronY = rect.top + (headerHeight_ - chevronSize) / 2;
    D2D1_COLOR_F chevronColor = fg;
    float progress = expandAnim_.Current();
    float angle = progress * 90.0f;
    float ccx = chevronX + chevronSize / 2, ccy = chevronY + chevronSize / 2;
    float halfH = 4.0f, halfW = 2.5f;
    float rad = angle * 3.14159f / 180.0f;
    float cosA = std::cos(rad), sinA = std::sin(rad);
    auto rot = [&](float dx, float dy, float& ox, float& oy) {
        ox = ccx + dx * cosA - dy * sinA;
        oy = ccy + dx * sinA + dy * cosA;
    };
    float x1, y1, x2, y2, x3, y3;
    rot(-halfW, -halfH, x1, y1);
    rot(halfW, 0, x2, y2);
    rot(-halfW, halfH, x3, y3);
    r.DrawLine(x1, y1, x2, y2, chevronColor, 1.5f);
    r.DrawLine(x2, y2, x3, y3, chevronColor, 1.5f);

    D2D1_RECT_F textRect = {rect.left + 32, rect.top, rect.right - 8, rect.top + headerHeight_};
    r.DrawText(headerText_, textRect, fg, fontSize,
               DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_FONT_WEIGHT_SEMI_BOLD);

    D2D1_COLOR_F border = css.hasBorderColor
        ? css.borderColor
        : (dark ? theme::Rgba(0xFF,0xFF,0xFF,0.06f) : theme::Rgba(0x00,0x00,0x00,0.04f));
    r.DrawLine(rect.left, rect.top + headerHeight_, rect.right, rect.top + headerHeight_, border);

    // Children: clip to animated content area
    if (progress > 0.001f) {
        float visibleH = measuredContentH_ * progress;
        D2D1_RECT_F clipRect = {rect.left, rect.top + headerHeight_, rect.right,
                                 rect.top + headerHeight_ + visibleH + padT + padB};
        r.PushClip(clipRect);
        for (auto& child : children_) {
            if (child->visible) child->DrawTree(r);
        }
        r.PopClip();
    }
}

bool ExpanderWidget::OnMouseDown(const MouseEvent& e) {
    if (e.y >= rect.top && e.y < rect.top + headerHeight_ && e.x >= rect.left && e.x < rect.right) {
        return true;
    }
    // Forward to children if expanded
    if (expandAnim_.Current() > 0.5f) {
        for (auto& child : children_) {
            if (child->visible && child->Contains(e.x, e.y)) return child->OnMouseDown(e);
        }
    }
    return false;
}

bool ExpanderWidget::OnMouseMove(const MouseEvent& e) {
    headerHovered_ = (e.y >= rect.top && e.y < rect.top + headerHeight_ && e.x >= rect.left && e.x < rect.right);
    if (expandAnim_.Current() > 0.5f) {
        for (auto& child : children_) {
            if (child->visible) child->OnMouseMove(e);
        }
    }
    return headerHovered_ || Contains(e.x, e.y);
}

bool ExpanderWidget::OnMouseUp(const MouseEvent& e) {
    if (e.y >= rect.top && e.y < rect.top + headerHeight_ && e.x >= rect.left && e.x < rect.right) {
        Toggle();
        return true;
    }
    if (expandAnim_.Current() > 0.5f) {
        for (auto& child : children_) {
            if (child->visible && child->Contains(e.x, e.y)) return child->OnMouseUp(e);
        }
    }
    return false;
}

D2D1_SIZE_F ExpanderWidget::SizeHint() const {
    float contentH = 0;
    for (auto& child : children_) {
        if (child->visible) {
            float h = child->fixedH > 0 ? child->fixedH : child->SizeHint().height;
            contentH += (h > 0 ? h : 24.0f) + 4.0f;
        }
    }
    float totalH = headerHeight_ + contentH * expandAnim_.Current();
    return {fixedW > 0 ? fixedW : 300.0f, fixedH > 0 ? fixedH : totalH};
}

// ---- NumberBox ----

NumberBoxWidget::NumberBoxWidget(float min, float max, float value, float step)
    : min_(min), max_(max), value_(value), step_(step) {
    focusable = true;
    fixedH = 32.0f;
    Clamp();
}

void NumberBoxWidget::Clamp() {
    value_ = std::clamp(value_, min_, max_);
}

void NumberBoxWidget::SetValue(float v) {
    float old = value_;
    value_ = std::clamp(v, min_, max_);
    if (value_ != old && onFloatChanged) onFloatChanged(value_);
}

std::wstring NumberBoxWidget::FormatValue() const {
    wchar_t buf[64];
    if (decimals_ <= 0) swprintf(buf, 64, L"%d", (int)value_);
    else swprintf(buf, 64, L"%.*f", decimals_, (double)value_);
    return buf;
}

D2D1_RECT_F NumberBoxWidget::UpBtnRect() const {
    float btnW = 24.0f;
    float h = (rect.bottom - rect.top) / 2;
    return {rect.right - btnW, rect.top, rect.right, rect.top + h};
}

D2D1_RECT_F NumberBoxWidget::DownBtnRect() const {
    float btnW = 24.0f;
    float h = (rect.bottom - rect.top) / 2;
    return {rect.right - btnW, rect.top + h, rect.right, rect.bottom};
}

void NumberBoxWidget::CommitEdit() {
    if (!editing_) return;
    editing_ = false;
    try {
        float v = std::stof(std::string(editText_.begin(), editText_.end()));
        SetValue(v);
    } catch (...) {}
}

void NumberBoxWidget::OnDraw(Renderer& r) {
    bool dark = theme::IsDark();
    float cr = (css.borderRadius >= 0) ? css.borderRadius : theme::radius::medium;
    float fontSize = (css.fontSize > 0) ? css.fontSize : theme::kFontSizeNormal;
    D2D1_COLOR_F accent = css.hasAccent ? css.accent : theme::kAccent();
    D2D1_COLOR_F fg = css.hasFg ? css.fg : theme::kBtnText();

    D2D1_COLOR_F bg;
    if (bgColor.a > 0) {
        bg = bgColor;
    } else {
        if (focused)      bg = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.03f) : theme::Rgba(0xFF,0xFF,0xFF,1.0f);
        else if (hovered) bg = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.08f) : theme::Rgba(0xF9,0xF9,0xF9,0.97f);
        else              bg = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.06f) : theme::Rgba(0xFF,0xFF,0xFF,0.95f);
    }
    r.FillRoundedRect(rect, cr, cr, bg);

    bool customBorder = css.hasBorderColor || css.borderWidth >= 0;
    if (customBorder) {
        float bw = (css.borderWidth >= 0) ? css.borderWidth : 1.0f;
        if (bw > 0) {
            D2D1_COLOR_F bc = css.hasBorderColor ? css.borderColor
                                                  : (dark ? theme::Rgba(0xFF,0xFF,0xFF,0.20f)
                                                         : theme::Rgba(0x00,0x00,0x00,0.20f));
            r.DrawRoundedRect(rect, cr, cr, bc, bw);
        }
    } else {
        D2D1_COLOR_F border = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.08f) : theme::Rgba(0x00,0x00,0x00,0.06f);
        r.DrawRoundedRect(rect, cr, cr, border, 1.0f);
        if (focused) {
            D2D1_RECT_F botLine = {rect.left + 1, rect.bottom - 2, rect.right - 1, rect.bottom};
            r.FillRect(botLine, accent);
        }
    }

    float btnW = 24.0f;
    auto upR = UpBtnRect(), downR = DownBtnRect();

    // Clip the button column to the container's rounded silhouette so hover
    // fills don't square off the top-right / bottom-right corners.
    r.PushRoundedClip(rect, cr, cr);

    r.DrawLine(rect.right - btnW, rect.top + 2, rect.right - btnW, rect.bottom - 2,
               dark ? theme::Rgba(0xFF,0xFF,0xFF,0.06f) : theme::Rgba(0x00,0x00,0x00,0.04f));

    D2D1_COLOR_F btnColor = (hoveredBtn_ == 0)
        ? (dark ? theme::Rgba(0xFF,0xFF,0xFF,0.12f) : theme::Rgba(0x00,0x00,0x00,0.06f))
        : theme::transparent;
    r.FillRect(upR, btnColor);
    float ucx = (upR.left + upR.right) / 2, ucy = (upR.top + upR.bottom) / 2;
    r.DrawLine(ucx - 4, ucy + 2, ucx, ucy - 2, fg, 1.2f);
    r.DrawLine(ucx, ucy - 2, ucx + 4, ucy + 2, fg, 1.2f);

    btnColor = (hoveredBtn_ == 1)
        ? (dark ? theme::Rgba(0xFF,0xFF,0xFF,0.12f) : theme::Rgba(0x00,0x00,0x00,0.06f))
        : theme::transparent;
    r.FillRect(downR, btnColor);
    float dcx = (downR.left + downR.right) / 2, dcy = (downR.top + downR.bottom) / 2;
    r.DrawLine(dcx - 4, dcy - 2, dcx, dcy + 2, fg, 1.2f);
    r.DrawLine(dcx, dcy + 2, dcx + 4, dcy - 2, fg, 1.2f);

    r.PopRoundedClip();

    float btnW2 = 24.0f;
    D2D1_RECT_F textArea = {rect.left + 11, rect.top, rect.right - btnW2 - 4, rect.bottom};
    std::wstring display = editing_ ? editText_ : FormatValue();
    r.PushClip(textArea);
    r.DrawText(display, textArea, fg, fontSize);

    if (editing_ && focused) {
        uint64_t now = GetTickCount64();
        bool showCaret = ((now / 530) % 2) == 0;
        if (showCaret) {
            D2D1_COLOR_F caret = css.hasCaretColor ? css.caretColor : accent;
            std::wstring before = editText_.substr(0, cursorPos_);
            float textW = r.MeasureTextWidth(before, fontSize, nullptr);
            float caretX = rect.left + 11 + textW;
            float cy1 = rect.top + 6, cy2 = rect.bottom - 6;
            r.DrawLine(caretX, cy1, caretX, cy2, caret, 1.5f);
        }
    }
    r.PopClip();
}

bool NumberBoxWidget::OnMouseDown(const MouseEvent& e) {
    if (!enabled) return false;
    if (!Contains(e.x, e.y)) {
        if (editing_) CommitEdit();
        focused = false;
        return false;
    }
    focused = true;

    if (!readOnly) {
        auto upR = UpBtnRect(), downR = DownBtnRect();
        if (e.x >= upR.left && e.x <= upR.right && e.y >= upR.top && e.y <= upR.bottom) {
            if (editing_) CommitEdit();
            SetValue(value_ + step_);
            return true;
        }
        if (e.x >= downR.left && e.x <= downR.right && e.y >= downR.top && e.y <= downR.bottom) {
            if (editing_) CommitEdit();
            SetValue(value_ - step_);
            return true;
        }

        // Click on text area → enter edit mode
        if (!editing_) {
            editing_ = true;
            editText_ = FormatValue();
            cursorPos_ = (int)editText_.size();
        }
    }
    return true;
}

bool NumberBoxWidget::OnMouseMove(const MouseEvent& e) {
    hovered = Contains(e.x, e.y);
    hoveredBtn_ = -1;
    if (hovered) {
        auto upR = UpBtnRect(), downR = DownBtnRect();
        if (e.x >= upR.left && e.y < upR.bottom) hoveredBtn_ = 0;
        else if (e.x >= downR.left && e.y >= downR.top) hoveredBtn_ = 1;
    }
    return hovered;
}

bool NumberBoxWidget::OnMouseUp(const MouseEvent& e) {
    return Contains(e.x, e.y);
}

bool NumberBoxWidget::OnKeyChar(wchar_t ch) {
    if (!focused || !editing_ || !enabled || readOnly) return false;
    if (ch == '\r') { CommitEdit(); return true; }
    if (ch == '\b') {
        if (cursorPos_ > 0 && !editText_.empty()) {
            editText_.erase(cursorPos_ - 1, 1);
            cursorPos_--;
        }
        return true;
    }
    // Allow digits, minus, decimal point
    if ((ch >= '0' && ch <= '9') || ch == '-' || ch == '.') {
        editText_.insert(cursorPos_, 1, ch);
        cursorPos_++;
        return true;
    }
    return true;
}

bool NumberBoxWidget::OnKeyDown(int vk) {
    if (!focused || !enabled) return false;
    if (readOnly && (vk == VK_UP || vk == VK_DOWN || vk == VK_RETURN)) return true;
    if (vk == VK_UP) { SetValue(value_ + step_); return true; }
    if (vk == VK_DOWN) { SetValue(value_ - step_); return true; }
    if (vk == VK_ESCAPE) { editing_ = false; return true; }
    if (vk == VK_RETURN) { CommitEdit(); return true; }
    return false;
}

bool NumberBoxWidget::OnMouseWheel(const MouseEvent& e) {
    if (!enabled || readOnly) return false;
    if (!Contains(e.x, e.y)) return false;
    SetValue(value_ + (e.delta > 0 ? step_ : -step_));
    return true;
}

D2D1_SIZE_F NumberBoxWidget::SizeHint() const {
    return {fixedW > 0 ? fixedW : 120.0f, fixedH > 0 ? fixedH : 32.0f};
}

// ---- NavItem ----

NavItemWidget::NavItemWidget(const std::wstring& text, const std::string& svgIcon)
    : text_(text), svgContent_(svgIcon) {
    focusable = true;
    fixedH = 40.0f;  // WinUI 3: NavigationViewItem height = 40px
}

void NavItemWidget::SetSvgIcon(const std::string& svg) {
    svgContent_ = svg;
    svgParsed_ = false;
}

void NavItemWidget::SetSelected(bool sel) {
    selected_ = sel;
}

void NavItemWidget::OnDraw(Renderer& r) {
    bool dark = theme::IsDark();
    float w = rect.right - rect.left;
    float h = rect.bottom - rect.top;

    // Parse SVG icon lazily
    if (!svgParsed_ && !svgContent_.empty()) {
        svgIcon_ = r.ParseSvgIcon(svgContent_);
        svgParsed_ = true;
    }

    constexpr float cr = theme::radius::medium;  // 4px

    // Background: selected or hovered
    if (selected_) {
        D2D1_COLOR_F selBg = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.07f) : theme::Rgba(0x00,0x00,0x00,0.04f);
        r.FillRoundedRect(rect, cr, cr, selBg);
    } else if (hovered) {
        D2D1_COLOR_F hoverBg = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.05f) : theme::Rgba(0x00,0x00,0x00,0.03f);
        r.FillRoundedRect(rect, cr, cr, hoverBg);
    }

    // Left accent indicator (selected): 3px wide, 16px tall, rounded
    if (selected_) {
        float indicatorH = 16.0f;
        float cy = (rect.top + rect.bottom) / 2;
        D2D1_RECT_F indicator = {rect.left + 3, cy - indicatorH/2, rect.left + 6, cy + indicatorH/2};
        r.FillRoundedRect(indicator, 1.5f, 1.5f, theme::kAccent());
    }

    // Icon: always centered in the first 48px of the item's own width
    // (works correctly whether pane is expanded or compact)
    float iconSize = 16.0f;
    float iconAreaW = std::min(48.0f, rect.right - rect.left);  // clamp to item width
    float iconX = rect.left + (iconAreaW - iconSize) / 2;
    float iconY = (rect.top + rect.bottom) / 2 - iconSize / 2;
    D2D1_RECT_F iconRect = {iconX, iconY, iconX + iconSize, iconY + iconSize};

    D2D1_COLOR_F iconColor = selected_ ? theme::kAccent() : theme::kBtnText();

    if (svgIcon_.valid) {
        r.DrawSvgIcon(svgIcon_, iconRect, iconColor);
    } else if (!glyph_.empty()) {
        r.DrawIcon(glyph_, iconRect, iconColor, iconSize);
    }

    // Label text (clipped to available width, hidden when pane is narrow)
    float textLeft = rect.left + 48.0f;
    if (textLeft < rect.right - 8) {
        D2D1_RECT_F textRect = {textLeft, rect.top, rect.right - 8, rect.bottom};
        r.PushClip(textRect);
        D2D1_COLOR_F textColor = theme::kBtnText();
        r.DrawText(text_, textRect, textColor, theme::kFontSizeNormal);
        r.PopClip();
    }
}

bool NavItemWidget::OnMouseMove(const MouseEvent& e) {
    hovered = Contains(e.x, e.y);
    return hovered;
}

bool NavItemWidget::OnMouseDown(const MouseEvent& e) {
    if (Contains(e.x, e.y)) { pressed = true; return true; }
    return false;
}

bool NavItemWidget::OnMouseUp(const MouseEvent& e) {
    bool wasPressed = pressed;
    pressed = false;
    if (wasPressed && Contains(e.x, e.y) && onClick) {
        onClick();
        return true;
    }
    return false;
}

D2D1_SIZE_F NavItemWidget::SizeHint() const {
    return {fixedW > 0 ? fixedW : 200.0f, fixedH > 0 ? fixedH : 40.0f};
}

// ---- Flyout ----

FlyoutWidget::FlyoutWidget() {
    visible = false;  // hidden until Show()
    fixedW = 0; fixedH = 0;
}

D2D1_RECT_F FlyoutWidget::ComputeRect() const {
    if (!anchor_ || !content_) return {};

    D2D1_SIZE_F hint = content_->SizeHint();
    float w = std::max(hint.width, 100.0f);
    float h = std::max(hint.height, 40.0f);
    // Add padding
    w += 16; h += 16;

    auto& ar = anchor_->rect;
    float acx = (ar.left + ar.right) / 2;
    float acy = (ar.top + ar.bottom) / 2;
    float gap = 4.0f;

    // Use the root widget's rect as viewport (not ScrollView's clip)
    Widget* root = const_cast<FlyoutWidget*>(this)->Parent();
    while (root && root->Parent()) root = root->Parent();
    D2D1_RECT_F vp = root ? root->rect : Viewport();
    auto tryPlacement = [&](FlyoutPlacement p) -> D2D1_RECT_F {
        D2D1_RECT_F r = {};
        switch (p) {
            case FlyoutPlacement::Bottom:
                r = {acx - w/2, ar.bottom + gap, acx + w/2, ar.bottom + gap + h};
                break;
            case FlyoutPlacement::Top:
                r = {acx - w/2, ar.top - gap - h, acx + w/2, ar.top - gap};
                break;
            case FlyoutPlacement::Right:
                r = {ar.right + gap, acy - h/2, ar.right + gap + w, acy + h/2};
                break;
            case FlyoutPlacement::Left:
                r = {ar.left - gap - w, acy - h/2, ar.left - gap, acy + h/2};
                break;
            default: break;
        }
        return r;
    };

    auto fitsViewport = [&](const D2D1_RECT_F& r) {
        return r.top >= vp.top && r.bottom <= vp.bottom &&
               r.left >= vp.left && r.right <= vp.right;
    };
    auto clampToViewport = [&](D2D1_RECT_F r) -> D2D1_RECT_F {
        if (r.right > vp.right)  { float d = r.right - vp.right;   r.left -= d; r.right -= d; }
        if (r.left < vp.left)   { float d = vp.left - r.left;     r.left += d; r.right += d; }
        if (r.bottom > vp.bottom){ float d = r.bottom - vp.bottom; r.top -= d; r.bottom -= d; }
        if (r.top < vp.top)     { float d = vp.top - r.top;       r.top += d; r.bottom += d; }
        return r;
    };

    // For explicit placement: try preferred, then opposite, then clamp
    if (placement_ != FlyoutPlacement::Auto) {
        auto r = tryPlacement(placement_);
        if (fitsViewport(r)) return r;
        // Try opposite direction
        FlyoutPlacement opp = placement_;
        if (placement_ == FlyoutPlacement::Bottom) opp = FlyoutPlacement::Top;
        else if (placement_ == FlyoutPlacement::Top) opp = FlyoutPlacement::Bottom;
        else if (placement_ == FlyoutPlacement::Left) opp = FlyoutPlacement::Right;
        else if (placement_ == FlyoutPlacement::Right) opp = FlyoutPlacement::Left;
        auto r2 = tryPlacement(opp);
        if (fitsViewport(r2)) return r2;
        return clampToViewport(r);
    }

    // Auto: try Bottom → Top → Right → Left
    FlyoutPlacement order[] = {FlyoutPlacement::Bottom, FlyoutPlacement::Top,
                                FlyoutPlacement::Right, FlyoutPlacement::Left};
    for (auto p : order) {
        auto r = tryPlacement(p);
        if (fitsViewport(r)) return r;
    }
    return clampToViewport(tryPlacement(FlyoutPlacement::Bottom));  // fallback: clamp
}

void FlyoutWidget::Show(Widget* anchor) {
    anchor_ = anchor;
    open_ = true;
    visible = true;
    flyoutRect_ = ComputeRect();
    DoLayout();
}

void FlyoutWidget::Hide() {
    open_ = false;
    visible = false;
    anchor_ = nullptr;
    if (onDismissed) onDismissed();
}

void FlyoutWidget::DoLayout() {
    if (!open_ || !content_) return;
    flyoutRect_ = ComputeRect();
    // Layout content inside flyout with 8px padding
    content_->rect = {flyoutRect_.left + 8, flyoutRect_.top + 8,
                      flyoutRect_.right - 8, flyoutRect_.bottom - 8};
    content_->DoLayout();
}

void FlyoutWidget::OnDrawOverlay(Renderer& r) {
    if (!open_ || !content_) return;
    bool dark = theme::IsDark();

    // Shadow (simple offset rect)
    D2D1_COLOR_F shadowColor = dark ? theme::Rgba(0,0,0,0.40f) : theme::Rgba(0,0,0,0.15f);
    D2D1_RECT_F shadowRect = {flyoutRect_.left + 2, flyoutRect_.top + 2,
                                flyoutRect_.right + 2, flyoutRect_.bottom + 2};
    r.FillRoundedRect(shadowRect, 8, 8, shadowColor);

    // Background
    D2D1_COLOR_F bg = dark ? theme::Rgb(0x2B, 0x2B, 0x2B) : theme::white;
    r.FillRoundedRect(flyoutRect_, 8, 8, bg);

    // Border
    D2D1_COLOR_F border = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.08f) : theme::Rgba(0x00,0x00,0x00,0.06f);
    r.DrawRoundedRect(flyoutRect_, 8, 8, border, 0.5f);

    // Content
    r.PushClip(flyoutRect_);
    content_->DrawTree(r);
    r.PopClip();
}

bool FlyoutWidget::OnMouseDown(const MouseEvent& e) {
    if (!open_) return false;
    // Inside flyout → HitTest to find actual child widget
    if (e.x >= flyoutRect_.left && e.x <= flyoutRect_.right &&
        e.y >= flyoutRect_.top && e.y <= flyoutRect_.bottom) {
        if (content_) {
            Widget* hit = content_->HitTest(e.x, e.y);
            if (hit) {
                hit->pressed = true;
                pressedChild_ = hit;
                hit->OnMouseDown(e);
            }
        }
        return true;
    }
    // Outside → dismiss
    Hide();
    return true;
}

bool FlyoutWidget::OnMouseMove(const MouseEvent& e) {
    if (!open_) return false;
    if (e.x >= flyoutRect_.left && e.x <= flyoutRect_.right &&
        e.y >= flyoutRect_.top && e.y <= flyoutRect_.bottom) {
        if (content_) {
            Widget* hit = content_->HitTest(e.x, e.y);
            if (hit) hit->OnMouseMove(e);
        }
        return true;
    }
    return false;
}

bool FlyoutWidget::OnMouseUp(const MouseEvent& e) {
    if (!open_) return false;
    if (e.x >= flyoutRect_.left && e.x <= flyoutRect_.right &&
        e.y >= flyoutRect_.top && e.y <= flyoutRect_.bottom) {
        if (pressedChild_) {
            pressedChild_->OnMouseUp(e);
            pressedChild_->pressed = false;
            pressedChild_ = nullptr;
        } else if (content_) {
            Widget* hit = content_->HitTest(e.x, e.y);
            if (hit) hit->OnMouseUp(e);
        }
        return true;
    }
    return false;
}

// ---- SplitView ----

SplitViewWidget::SplitViewWidget() {
    expanding = true;
}

void SplitViewWidget::SetPaneOpen(bool open) {
    if (paneOpen_ == open) return;
    paneOpen_ = open;
    AnimationSpec spec{paneOpen_ ? 200.0f : 100.0f,
                       paneOpen_ ? EasingFunction::EaseOutCubic : EasingFunction::EaseInQuad};
    paneAnim_.Retarget(paneOpen_ ? 1.0f : 0.0f, spec);
    animating_ = paneAnim_.Active();
    ui::GetContext().RegisterAnimatingWidget(
        this, AnimationInvalidation::Paint | AnimationInvalidation::Layout |
                  AnimationInvalidation::HitTest);
    if (onPaneChanged) onPaneChanged(paneOpen_);
}

float SplitViewWidget::CurrentPaneWidth() const {
    float closedW = 0;
    if (mode_ == SplitViewMode::CompactOverlay || mode_ == SplitViewMode::CompactInline)
        closedW = compactPaneLength_;

    float openW = openPaneLength_;

    // For Inline/CompactInline: pane width affects layout
    // For Overlay/CompactOverlay: pane overlays, but we still need the visual width
    return closedW + paneAnim_.Current() * (openW - closedW);
}

void SplitViewWidget::UpdateAnimation() {
    paneAnim_.SetIdleTarget(paneOpen_ ? 1.0f : 0.0f);
    paneAnim_.SampleFrame(GetTickCount64());
    animating_ = paneAnim_.Active();
}

void SplitViewWidget::DoLayout() {
    float w = rect.right - rect.left;
    float h = rect.bottom - rect.top;
    float paneW = CurrentPaneWidth();

    bool isOverlay = (mode_ == SplitViewMode::Overlay || mode_ == SplitViewMode::CompactOverlay);

    float contentLeft;
    if (isOverlay) {
        // Overlay: content takes full width (or full minus compact strip)
        float compactW = (mode_ == SplitViewMode::CompactOverlay) ? compactPaneLength_ : 0;
        contentLeft = rect.left + compactW;
    } else {
        // Inline: content starts after pane
        contentLeft = rect.left + paneW;
    }

    // Layout content
    if (content_) {
        content_->rect = {contentLeft, rect.top, rect.right, rect.bottom};
        content_->DoLayout();
    }

    // Layout pane
    if (pane_) {
        pane_->rect = {rect.left, rect.top, rect.left + paneW, rect.bottom};
        pane_->DoLayout();
    }
}

void SplitViewWidget::OnDraw(Renderer& r) {
    bool dark = theme::IsDark();

    // Draw pane background
    float paneW = CurrentPaneWidth();
    if (paneW > 0 && pane_) {
        D2D1_RECT_F paneBg = {rect.left, rect.top, rect.left + paneW, rect.bottom};
        D2D1_COLOR_F paneBgColor = dark ? theme::Rgb(0x20, 0x20, 0x20) : theme::Rgb(0xF3, 0xF3, 0xF3);
        r.FillRect(paneBg, paneBgColor);

        // Right border on pane
        D2D1_COLOR_F borderColor = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.08f) : theme::Rgba(0x00,0x00,0x00,0.06f);
        r.DrawLine(rect.left + paneW, rect.top, rect.left + paneW, rect.bottom, borderColor, 1.0f);
    }
}

void SplitViewWidget::DrawTree(Renderer& r) {
    if (!visible) return;

    OnDraw(r);
    paintedOnce_ = true;   // L45: mount-phase transition gate

    bool isOverlay = (mode_ == SplitViewMode::Overlay || mode_ == SplitViewMode::CompactOverlay);

    // Draw content first (behind pane in overlay mode)
    if (content_) {
        content_->DrawTree(r);
        content_->DrawOverlays(r);
    }

    // In overlay mode, draw dimming overlay behind pane when open
    float progress = paneAnim_.Current();
    if (isOverlay && progress > 0.01f) {
        float compactW = (mode_ == SplitViewMode::CompactOverlay) ? compactPaneLength_ : 0;
        D2D1_RECT_F overlayRect = {rect.left + compactW, rect.top, rect.right, rect.bottom};
        D2D1_COLOR_F overlayColor = {0, 0, 0, 0.3f * progress};
        r.FillRect(overlayRect, overlayColor);
    }

    // Draw pane on top (always, in all modes)
    if (pane_) {
        float paneW = CurrentPaneWidth();
        if (paneW > 0) {
            r.PushClip({rect.left, rect.top, rect.left + paneW, rect.bottom});
            pane_->DrawTree(r);
            pane_->DrawOverlays(r);
            r.PopClip();
        }
    }
}

bool SplitViewWidget::OnMouseDown(const MouseEvent& e) {
    bool isOverlay = (mode_ == SplitViewMode::Overlay || mode_ == SplitViewMode::CompactOverlay);
    float paneW = CurrentPaneWidth();

    // Click on pane area
    if (e.x >= rect.left && e.x < rect.left + paneW && pane_) {
        return pane_->OnMouseDown({e.x, e.y, e.delta, e.leftBtn});
    }

    // Click on overlay (outside pane) → dismiss
    if (isOverlay && paneOpen_ && e.x >= rect.left + paneW) {
        SetPaneOpen(false);
        return true;
    }

    // Click on content
    if (content_) {
        return content_->OnMouseDown(e);
    }
    return false;
}

bool SplitViewWidget::OnMouseMove(const MouseEvent& e) {
    float paneW = CurrentPaneWidth();
    if (e.x >= rect.left && e.x < rect.left + paneW && pane_) {
        if (content_) {
            // Clear content hover
            MouseEvent leaveE{-1, -1};
            content_->OnMouseMove(leaveE);
        }
        return pane_->OnMouseMove({e.x, e.y, e.delta, e.leftBtn});
    }
    if (content_) {
        if (pane_) {
            MouseEvent leaveE{-1, -1};
            pane_->OnMouseMove(leaveE);
        }
        return content_->OnMouseMove(e);
    }
    return false;
}

bool SplitViewWidget::OnMouseUp(const MouseEvent& e) {
    float paneW = CurrentPaneWidth();
    if (e.x >= rect.left && e.x < rect.left + paneW && pane_) {
        return pane_->OnMouseUp({e.x, e.y, e.delta, e.leftBtn});
    }
    if (content_) return content_->OnMouseUp(e);
    return false;
}

D2D1_SIZE_F SplitViewWidget::SizeHint() const {
    return {fixedW > 0 ? fixedW : 800.0f, fixedH > 0 ? fixedH : 600.0f};
}

// ==================== ChipsInputWidget ====================
//
// Geometry model: the content is a run of elements (chip or single char)
// laid out left→right inside the text area (same insets as TextInput).
// Boundary i sits before element i; the caret / drop indicator live on
// boundaries, so chips are atomic by construction.

namespace {
constexpr float kChipsPadX     = 11.0f;  // text area inset (matches TextInput)
constexpr float kChipPadH      = 8.0f;   // horizontal padding inside a chip
constexpr float kChipMargin    = 3.0f;   // gap around a chip
constexpr float kChipCloseW    = 14.0f;  // × hit zone at a hovered chip's right
constexpr float kChipsDragThreshold = 4.0f;

std::wstring ChipsGetClipboard() {
    std::wstring out;
    if (!OpenClipboard(nullptr)) return out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* p = static_cast<wchar_t*>(GlobalLock(h))) {
            out = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

void ChipsSetClipboard(const std::wstring& s) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(h)) {
            memcpy(p, s.c_str(), bytes);
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
        } else {
            GlobalFree(h);
        }
    }
    CloseClipboard();
}
}  // namespace

ChipsInputWidget::ChipsInputWidget(const std::wstring& placeholder)
    : placeholder_(placeholder) {
    focusable = true;
    cssTag = "chips-input";
    // Native drop target for the window DnD framework: an external drag
    // (payload = chip key) shows an insertion caret while hovering and
    // drops in as a chip. Pages should NOT bind @drop/@dragover on the
    // widget — that would overwrite these hooks; listen to @change instead.
    dropTarget = true;
    onDragOverHook = [this](const MouseEvent& e) {
        extern MeasureContext* g_activeMeasureContext;
        if (g_activeMeasureContext)
            dropCaret_ = BoundaryFromX(*g_activeMeasureContext,
                                       e.x - rect.left - kChipsPadX + scrollX_);
    };
    onDragLeaveHook = [this]() { dropCaret_ = -1; };
    onDropHook = [this](const std::wstring& data, const MouseEvent& e) {
        extern MeasureContext* g_activeMeasureContext;
        int at = dropCaret_;
        if (at < 0 && g_activeMeasureContext)
            at = BoundaryFromX(*g_activeMeasureContext,
                               e.x - rect.left - kChipsPadX + scrollX_);
        if (at < 0 || at > (int)elems_.size()) at = (int)elems_.size();
        dropCaret_ = -1;
        if (readOnly || data.empty()) return;
        ClearSelection();
        elems_.insert(elems_.begin() + at, Elem{true, data});
        caret_ = at + 1;
        selectedChip_ = -1;
        Mutated();
    };
}

void ChipsInputWidget::ResetCaretBlink() {
    caretBlinkStartTick_ = static_cast<uint64_t>(GetTickCount64());
}

bool ChipsInputWidget::ShouldShowCaret() const {
    UINT blinkMs = TextInputWidget::EffectiveCaretBlinkMs();
    if (caretBlinkStartTick_ == 0) return true;
    uint64_t elapsed = static_cast<uint64_t>(GetTickCount64()) - caretBlinkStartTick_;
    return ((elapsed / blinkMs) % 2ULL) == 0ULL;
}

bool ChipsInputWidget::GetCaretRect(D2D1_RECT_F* out) const {
    extern MeasureContext* g_activeMeasureContext;
    if (!g_activeMeasureContext) return false;
    std::vector<float> xs;
    BoundaryXs(*g_activeMeasureContext, xs);
    int c = std::min(std::max(caret_, 0), (int)xs.size() - 1);
    float x = rect.left + kChipsPadX - scrollX_ + xs[c];
    *out = {x, rect.top + 6, x + 1.5f, rect.bottom - 7};
    return true;
}

bool ChipsInputWidget::IsOverChip(MeasureContext& m, float windowX) const {
    return ChipIndexAt(m, windowX - rect.left - kChipsPadX + scrollX_,
                       0, nullptr) >= 0;
}

bool ChipsInputWidget::IsOverChipClose(MeasureContext& m, float windowX) const {
    bool onClose = false;
    ChipIndexAt(m, windowX - rect.left - kChipsPadX + scrollX_, 0, &onClose);
    return onClose;
}

std::wstring ChipsInputWidget::Value() const {
    std::wstring out;
    for (const auto& el : elems_) {
        if (el.chip) { out += L'{'; out += el.s; out += L'}'; }
        else out += el.s;
    }
    return out;
}

void ChipsInputWidget::SetValue(const std::wstring& tpl) {
    elems_.clear();
    for (size_t i = 0; i < tpl.size();) {
        if (tpl[i] == L'{') {
            size_t close = tpl.find(L'}', i + 1);
            if (close != std::wstring::npos && close > i + 1 &&
                tpl.find(L'{', i + 1) > close) {
                elems_.push_back({true, tpl.substr(i + 1, close - i - 1)});
                i = close + 1;
                continue;
            }
        }
        elems_.push_back({false, std::wstring(1, tpl[i])});
        i++;
    }
    caret_ = (int)elems_.size();
    selectedChip_ = -1;
    ClearSelection();
    scrollX_ = 0;
}

void ChipsInputWidget::SetChipLabels(const std::wstring& spec) {
    chipLabels_.clear();
    size_t i = 0;
    while (i < spec.size()) {
        size_t sep = spec.find(L';', i);
        if (sep == std::wstring::npos) sep = spec.size();
        std::wstring pair = spec.substr(i, sep - i);
        size_t eq = pair.find(L'=');
        if (eq != std::wstring::npos && eq > 0)
            chipLabels_.emplace_back(pair.substr(0, eq), pair.substr(eq + 1));
        i = sep + 1;
    }
}

void ChipsInputWidget::InsertChip(const std::wstring& key) {
    if (readOnly || key.empty()) return;
    if (HasSelection()) DeleteSelection();
    if (caret_ < 0 || caret_ > (int)elems_.size()) caret_ = (int)elems_.size();
    elems_.insert(elems_.begin() + caret_, Elem{true, key});
    caret_++;
    selectedChip_ = -1;
    Mutated();
}

const std::wstring& ChipsInputWidget::ChipLabel(const std::wstring& key) const {
    for (const auto& kv : chipLabels_)
        if (kv.first == key) return kv.second;
    return key;
}

float ChipsInputWidget::ChipFontSize() const {
    float fontSize = (css.fontSize > 0) ? css.fontSize : theme::kFontSizeNormal;
    return fontSize - 1.0f;
}

float ChipsInputWidget::ElemWidth(MeasureContext& m, const Elem& el,
                                  float fontSize) const {
    if (el.chip)
        /* 常驻预留 × 区域 — 悬停显示 × 时不遮文字也不改变宽度 (布局不抖). */
        return m.MeasureTextWidth(ChipLabel(el.s), ChipFontSize()) +
               kChipPadH * 2 + kChipCloseW + kChipMargin * 2;
    return m.MeasureTextWidth(el.s, fontSize);
}

void ChipsInputWidget::BoundaryXs(MeasureContext& m,
                                  std::vector<float>& out) const {
    float fontSize = (css.fontSize > 0) ? css.fontSize : theme::kFontSizeNormal;
    out.clear();
    out.reserve(elems_.size() + 1);
    float x = 0;
    out.push_back(x);
    for (const auto& el : elems_) {
        x += ElemWidth(m, el, fontSize);
        out.push_back(x);
    }
}

int ChipsInputWidget::BoundaryFromX(MeasureContext& m, float localX) const {
    std::vector<float> xs;
    BoundaryXs(m, xs);
    int best = 0;
    float bestDist = 1e9f;
    for (int i = 0; i < (int)xs.size(); i++) {
        float d = std::fabs(xs[i] - localX);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

int ChipsInputWidget::ChipIndexAt(MeasureContext& m, float localX,
                                  float localY, bool* onClose) const {
    (void)localY;
    if (onClose) *onClose = false;
    std::vector<float> xs;
    BoundaryXs(m, xs);
    for (int i = 0; i < (int)elems_.size(); i++) {
        if (!elems_[i].chip) continue;
        float l = xs[i] + kChipMargin, r2 = xs[i + 1] - kChipMargin;
        if (localX >= l && localX < r2) {
            if (onClose && hoverChip_ == i && localX >= r2 - kChipCloseW)
                *onClose = true;
            return i;
        }
    }
    return -1;
}

void ChipsInputWidget::EnsureCaretVisible(MeasureContext& m) {
    std::vector<float> xs;
    BoundaryXs(m, xs);
    float viewW = std::max(0.0f, rect.right - rect.left - kChipsPadX * 2);
    float total = xs.back();
    if (total <= viewW) { scrollX_ = 0; return; }
    float cx = xs[std::min(std::max(caret_, 0), (int)xs.size() - 1)];
    if (cx - scrollX_ < 0) scrollX_ = cx;
    if (cx - scrollX_ > viewW) scrollX_ = cx - viewW;
    if (scrollX_ > total - viewW) scrollX_ = total - viewW;
    if (scrollX_ < 0) scrollX_ = 0;
}

void ChipsInputWidget::Mutated() {
    if (onTextChanged) onTextChanged(Value());
}

void ChipsInputWidget::DeleteSelection() {
    if (!HasSelection()) return;
    int a = std::min(selAnchor_, caret_);
    int b = std::max(selAnchor_, caret_);
    elems_.erase(elems_.begin() + a, elems_.begin() + b);
    caret_ = a;
    ClearSelection();
    selectedChip_ = -1;
    Mutated();
}

std::wstring ChipsInputWidget::SerializeRange(int a, int b) const {
    std::wstring out;
    for (int i = a; i < b && i < (int)elems_.size(); i++) {
        const Elem& el = elems_[i];
        if (el.chip) { out += L'{'; out += el.s; out += L'}'; }
        else out += el.s;
    }
    return out;
}

void ChipsInputWidget::RemoveChip(int idx) {
    if (idx < 0 || idx >= (int)elems_.size() || !elems_[idx].chip) return;
    elems_.erase(elems_.begin() + idx);
    if (caret_ > idx) caret_--;
    selectedChip_ = -1;
    hoverChip_ = -1;
    Mutated();
}

void ChipsInputWidget::OnDraw(Renderer& r) {
    bool dark = theme::IsDark();
    float cr = (css.borderRadius >= 0) ? css.borderRadius : theme::radius::medium;
    float fontSize = (css.fontSize > 0) ? css.fontSize : theme::kFontSizeNormal;
    D2D1_COLOR_F accent = css.hasAccent ? css.accent : theme::kAccent();
    D2D1_COLOR_F fg = css.hasFg ? css.fg : theme::kBtnText();
    bool focused = IsFocused();

    // Frame: same state machine as TextInput.
    bool customBg = bgColor.a > 0;
    D2D1_COLOR_F bg;
    if (customBg) {
        bg = bgColor;
    } else {
        if (!enabled)     bg = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.03f) : theme::Rgba(0xF0,0xF0,0xF0,0.90f);
        else if (focused) bg = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.03f) : theme::Rgba(0xFF,0xFF,0xFF,1.0f);
        else if (hovered) bg = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.08f) : theme::Rgba(0xF9,0xF9,0xF9,0.97f);
        else              bg = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.06f) : theme::Rgba(0xFF,0xFF,0xFF,0.95f);
    }
    r.FillRoundedRect(rect, cr, cr, bg);
    if (dragOver) {
        r.DrawRoundedRect(rect, cr, cr, accent, 1.5f);
    } else {
        D2D1_COLOR_F borderTop = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.08f) : theme::Rgba(0x00,0x00,0x00,0.06f);
        r.DrawRoundedRect(rect, cr, cr, borderTop, 1.0f);
        if (focused) {
            D2D1_RECT_F bottomLine = {rect.left + 1, rect.bottom - 2, rect.right - 1, rect.bottom};
            r.FillRect(bottomLine, accent);
        } else {
            D2D1_COLOR_F borderBot = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.54f) : theme::Rgba(0x00,0x00,0x00,0.45f);
            D2D1_RECT_F botLine = {rect.left + 2, rect.bottom - 1.0f, rect.right - 2, rect.bottom};
            r.FillRect(botLine, borderBot);
        }
    }

    /* 裁剪区只收水平 (滚动裁切用), 垂直几乎放满 — 高度紧张时 chip 下缘
     * 不再被裁掉几个像素. */
    D2D1_RECT_F area = {rect.left + kChipsPadX, rect.top + 1,
                        rect.right - kChipsPadX, rect.bottom - 2};
    r.PushClip(area);

    if (elems_.empty()) {
        D2D1_COLOR_F phColor = css.hasPlaceholderColor ? css.placeholderColor
                                                       : theme::kContentText();
        D2D1_RECT_F phArea = {area.left, rect.top + 5, area.right, rect.bottom - 6};
        r.DrawText(placeholder_, phArea, phColor, fontSize);
    }

    extern MeasureContext* g_activeMeasureContext;
    MeasureContext local;
    MeasureContext* m = g_activeMeasureContext;
    if (!m) { local.BindRenderer(&r); m = &local; }

    std::vector<float> xs;
    BoundaryXs(*m, xs);
    float baseX = area.left - scrollX_;
    float chipFont = ChipFontSize();
    float midY = (rect.top + rect.bottom) * 0.5f;

    // Selection band (behind elements) — accent when focused, gray otherwise.
    if (HasSelection()) {
        int a = std::min(selAnchor_, caret_);
        int b = std::max(selAnchor_, caret_);
        D2D1_COLOR_F selBg = focused
            ? D2D1_COLOR_F{accent.r, accent.g, accent.b, 0.30f}
            : (dark ? theme::Rgba(0xFF, 0xFF, 0xFF, 0.18f)
                    : theme::Rgba(0x00, 0x00, 0x00, 0.18f));
        r.FillRect({baseX + xs[a], rect.top + 4,
                    baseX + xs[b], rect.bottom - 5}, selBg);
    }

    for (int i = 0; i < (int)elems_.size(); i++) {
        const Elem& el = elems_[i];
        float x0 = baseX + xs[i], x1 = baseX + xs[i + 1];
        if (x1 < area.left || x0 > area.right) continue;
        if (el.chip) {
            D2D1_RECT_F pill = {x0 + kChipMargin, midY - (chipFont + 12.0f) * 0.5f,
                                x1 - kChipMargin, midY + (chipFont + 12.0f) * 0.5f};
            float pr = (pill.bottom - pill.top) * 0.5f;
            bool sel = (i == selectedChip_);
            D2D1_COLOR_F pillBg = sel ? accent
                : (dark ? theme::Rgba(0xFF,0xFF,0xFF,0.10f)
                        : theme::Rgba(0x00,0x00,0x00,0.06f));
            D2D1_COLOR_F pillFg = sel ? theme::white : fg;
            r.FillRoundedRect(pill, pr, pr, pillBg);
            if (!sel) {
                D2D1_COLOR_F pillBorder = dark ? theme::Rgba(0xFF,0xFF,0xFF,0.16f)
                                               : theme::Rgba(0x00,0x00,0x00,0.12f);
                r.DrawRoundedRect(pill, pr, pr, pillBorder, 1.0f);
            }
            D2D1_RECT_F tr = {pill.left + kChipPadH, midY - chipFont * 0.72f,
                              pill.right - kChipPadH - kChipCloseW,
                              midY + chipFont * 0.72f};
            r.DrawText(ChipLabel(el.s), tr, pillFg, chipFont);
            if (hovered && i == hoverChip_ && !readOnly) {
                // × affordance in the reserved right zone. Drawn as two
                // lines — text glyph metrics never center exactly.
                float cxm = pill.right - 2 - kChipCloseW * 0.5f;
                float arm = 3.2f;
                r.DrawLine(cxm - arm, midY - arm, cxm + arm, midY + arm,
                           pillFg, 1.2f);
                r.DrawLine(cxm - arm, midY + arm, cxm + arm, midY - arm,
                           pillFg, 1.2f);
            }
        } else {
            D2D1_RECT_F tr = {x0, rect.top + 5, x0 + 10000, rect.bottom - 6};
            r.DrawText(el.s, tr, fg, fontSize);
        }
    }

    // Caret (blinks like TextInput; hidden while a chip is armed for delete)
    if (focused && enabled && selectedChip_ < 0 && dragChip_ < 0 &&
        ShouldShowCaret()) {
        float cx = baseX + xs[std::min(std::max(caret_, 0), (int)xs.size() - 1)];
        D2D1_COLOR_F caretColor = css.hasCaretColor ? css.caretColor : accent;
        r.FillRect({cx, rect.top + 6, cx + 1.2f, rect.bottom - 7}, caretColor);
    }

    // Drop / reorder insertion indicator
    if (dropCaret_ >= 0 && dropCaret_ < (int)xs.size()) {
        float cx = baseX + xs[dropCaret_];
        r.FillRect({cx - 1, rect.top + 4, cx + 1.5f, rect.bottom - 5}, accent);
    }

    // Reorder ghost: the dragged chip follows the cursor (centered),
    // semi-transparent — without it the gesture reads as "nothing happens".
    if (IsDraggingChip() && dragChip_ < (int)elems_.size()) {
        const std::wstring& glabel = ChipLabel(elems_[dragChip_].s);
        float gw = m->MeasureTextWidth(glabel, chipFont) + kChipPadH * 2;
        float gh = chipFont + 12.0f;
        D2D1_RECT_F gp = {dragMx_ - gw * 0.5f, dragMy_ - gh * 0.5f,
                          dragMx_ + gw * 0.5f, dragMy_ + gh * 0.5f};
        float gpr = gh * 0.5f;
        r.FillRoundedRect(gp, gpr, gpr,
                          {accent.r, accent.g, accent.b, 0.75f});
        D2D1_RECT_F gt = {gp.left + kChipPadH, dragMy_ - chipFont * 0.72f,
                          gp.right - kChipPadH, dragMy_ + chipFont * 0.72f};
        r.DrawText(glabel, gt, theme::white, chipFont);
    }

    r.PopClip();
}

bool ChipsInputWidget::OnMouseDown(const MouseEvent& e) {
    if (!enabled) return false;
    if (!Contains(e.x, e.y)) return false;
    extern MeasureContext* g_activeMeasureContext;
    if (!g_activeMeasureContext) return true;
    MeasureContext& m = *g_activeMeasureContext;
    float localX = e.x - rect.left - kChipsPadX + scrollX_;

    bool onClose = false;
    int chip = ChipIndexAt(m, localX, e.y - rect.top, &onClose);
    pressX_ = e.x; pressY_ = e.y;
    dragMoved_ = false;
    if (chip >= 0 && !readOnly) {
        if (onClose) {
            RemoveChip(chip);
            return true;
        }
        dragChip_ = chip;
        selectedChip_ = chip;
    } else {
        dragChip_ = -1;
        selectedChip_ = -1;
        const int pos = BoundaryFromX(m, localX);
        if (e.shift && selAnchor_ >= 0) {
            /* Shift+click: extend from existing anchor */
        } else if (e.shift) {
            selAnchor_ = caret_;
        } else {
            selAnchor_ = pos;   /* drag-selection anchor; collapses on mouse-up */
        }
        selecting_ = true;
        caret_ = pos;
        EnsureCaretVisible(m);
        ResetCaretBlink();
    }
    return true;
}

bool ChipsInputWidget::OnMouseMove(const MouseEvent& e) {
    extern MeasureContext* g_activeMeasureContext;
    if (!g_activeMeasureContext) return false;
    MeasureContext& m = *g_activeMeasureContext;
    float localX = e.x - rect.left - kChipsPadX + scrollX_;

    if (dragChip_ >= 0) {
        float ddx = e.x - pressX_, ddy = e.y - pressY_;
        if (!dragMoved_ &&
            ddx * ddx + ddy * ddy >= kChipsDragThreshold * kChipsDragThreshold)
            dragMoved_ = true;
        if (dragMoved_) {
            dragMx_ = e.x; dragMy_ = e.y;
            dropCaret_ = BoundaryFromX(m, localX);
            return true;
        }
        return false;
    }
    if (selecting_) {
        int pos = BoundaryFromX(m, localX);
        if (pos != caret_) {
            caret_ = pos;
            EnsureCaretVisible(m);
            return true;
        }
        return false;
    }
    int prevHover = hoverChip_;
    hoverChip_ = Contains(e.x, e.y)
                     ? ChipIndexAt(m, localX, e.y - rect.top, nullptr)
                     : -1;
    return hoverChip_ != prevHover;
}

bool ChipsInputWidget::OnMouseUp(const MouseEvent& e) {
    (void)e;
    if (dragChip_ >= 0 && dragMoved_ && dropCaret_ >= 0) {
        // Reorder: remove the chip, re-insert at the drop boundary.
        int from = dragChip_;
        int to = dropCaret_;
        Elem el = elems_[from];
        elems_.erase(elems_.begin() + from);
        if (to > from) to--;
        if (to > (int)elems_.size()) to = (int)elems_.size();
        elems_.insert(elems_.begin() + to, el);
        caret_ = to + 1;
        selectedChip_ = -1;
        Mutated();
    }
    dragChip_ = -1;
    dragMoved_ = false;
    dropCaret_ = -1;
    if (selecting_) {
        selecting_ = false;
        if (selAnchor_ == caret_) selAnchor_ = -1;  /* plain click, no range */
    }
    return true;
}

bool ChipsInputWidget::OnKeyChar(wchar_t ch) {
    if (!IsFocused() || readOnly) return false;
    if (ch < 32) return false;
    if (HasSelection()) DeleteSelection();
    if (caret_ < 0 || caret_ > (int)elems_.size()) caret_ = (int)elems_.size();
    elems_.insert(elems_.begin() + caret_, Elem{false, std::wstring(1, ch)});
    caret_++;
    selectedChip_ = -1;
    ResetCaretBlink();
    extern MeasureContext* g_activeMeasureContext;
    if (g_activeMeasureContext) EnsureCaretVisible(*g_activeMeasureContext);
    Mutated();
    return true;
}

bool ChipsInputWidget::OnKeyDown(int vk) {
    if (!IsFocused()) return false;
    int n = (int)elems_.size();
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    if (ctrl && vk == 'A') {
        selAnchor_ = 0;
        caret_ = n;
        selectedChip_ = -1;
        return true;
    }
    if (ctrl && vk == 'C') {
        ChipsSetClipboard(HasSelection()
            ? SerializeRange(std::min(selAnchor_, caret_),
                             std::max(selAnchor_, caret_))
            : Value());
        return true;
    }
    if (ctrl && vk == 'X' && !readOnly) {
        if (HasSelection()) {
            ChipsSetClipboard(SerializeRange(std::min(selAnchor_, caret_),
                                             std::max(selAnchor_, caret_)));
            DeleteSelection();
        }
        return true;
    }
    if (ctrl && vk == 'V' && !readOnly) {
        // Paste parses `{key}` back into chips (same grammar as SetValue).
        std::wstring clip = ChipsGetClipboard();
        if (clip.empty()) return true;
        if (HasSelection()) DeleteSelection();
        ChipsInputWidget tmp;
        tmp.SetValue(clip);
        n = (int)elems_.size();
        if (caret_ < 0 || caret_ > n) caret_ = n;
        elems_.insert(elems_.begin() + caret_, tmp.elems_.begin(), tmp.elems_.end());
        caret_ += (int)tmp.elems_.size();
        selectedChip_ = -1;
        Mutated();
        return true;
    }

    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    auto beginShiftMove = [&]() {
        if (shift) { if (selAnchor_ < 0) selAnchor_ = caret_; }
        else ClearSelection();
    };

    switch (vk) {
    case VK_LEFT:
        selectedChip_ = -1;
        if (!shift && HasSelection()) {
            caret_ = std::min(selAnchor_, caret_);  /* collapse to range start */
            ClearSelection();
            break;
        }
        beginShiftMove();
        if (caret_ > 0) caret_--;
        break;
    case VK_RIGHT:
        selectedChip_ = -1;
        if (!shift && HasSelection()) {
            caret_ = std::max(selAnchor_, caret_);
            ClearSelection();
            break;
        }
        beginShiftMove();
        if (caret_ < n) caret_++;
        break;
    case VK_HOME: selectedChip_ = -1; beginShiftMove(); caret_ = 0; break;
    case VK_END:  selectedChip_ = -1; beginShiftMove(); caret_ = n; break;
    case VK_BACK:
        if (readOnly) return true;
        if (HasSelection()) { DeleteSelection(); break; }
        if (selectedChip_ >= 0) { RemoveChip(selectedChip_); break; }
        if (caret_ > 0) {
            if (elems_[caret_ - 1].chip) {
                selectedChip_ = caret_ - 1;  // stage 1: arm the chip
            } else {
                elems_.erase(elems_.begin() + caret_ - 1);
                caret_--;
                Mutated();
            }
        }
        break;
    case VK_DELETE:
        if (readOnly) return true;
        if (HasSelection()) { DeleteSelection(); break; }
        if (selectedChip_ >= 0) { RemoveChip(selectedChip_); break; }
        if (caret_ < n) {
            if (elems_[caret_].chip) {
                selectedChip_ = caret_;
            } else {
                elems_.erase(elems_.begin() + caret_);
                Mutated();
            }
        }
        break;
    case VK_ESCAPE:
        selectedChip_ = -1;
        ClearSelection();
        break;
    default:
        return false;
    }
    ResetCaretBlink();
    extern MeasureContext* g_activeMeasureContext;
    if (g_activeMeasureContext) EnsureCaretVisible(*g_activeMeasureContext);
    return true;
}

D2D1_SIZE_F ChipsInputWidget::SizeHint() const {
    /* 36: chip 胶囊 (chipFont+12 ≈ 25) 上下各留 ≥5px, 下缘不贴边. */
    return {fixedW > 0 ? fixedW : 240.0f, fixedH > 0 ? fixedH : 36.0f};
}

} // namespace ui
