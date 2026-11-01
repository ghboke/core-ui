#include "msgbox.h"

#include "renderer.h"
#include "test_isolation.h"
#include "theme.h"
#include "ui_context.h"

#include <windows.h>
#include <algorithm>

namespace ui {

namespace {

/* 布局常量 (DIP)。 */
constexpr float kTitleBarH  = 36.0f;   /* lib TitleBar 默认高度量级 */
constexpr float kPadX       = 20.0f;
constexpr float kPadTop     = 14.0f;
constexpr float kPadBottom  = 16.0f;
constexpr float kIconGap    = 12.0f;
constexpr float kIconSize   = 22.0f;
constexpr float kMsgSize    = 13.0f;
constexpr float kBtnAreaGap = 18.0f;
constexpr float kBtnH       = 30.0f;
constexpr float kBtnMinW    = 78.0f;
constexpr float kBtnPadX    = 16.0f;
constexpr float kBtnGap     = 8.0f;
constexpr float kCardMinW   = 320.0f;
constexpr float kCardMaxW   = 540.0f;

struct BoxState {
    bool done   = false;
    int  result = -1;
    bool cancelled = false;   /* build 339: Esc / 关闭收场 (UiMsgBoxResult.cancelled) */
    int  defaultIdx = 0;
    int  cancelIdx  = -1;
    UiWindow win = 0;
    std::vector<int>          buttonKeys;   /* 每按钮 VK 绑定 (build 172); 空/0=无 */
    std::wstring              title;        /* IPC dialog_state 快照用 */
    std::vector<std::wstring> buttons;
    std::vector<UiWidget>     btnWidgets;   /* build 174: 按钮 widget, 方向键移焦点用 */
    int                       focusedBtnIdx = -1;

    void Finish(int r) {
        result = r;
        done   = true;
        /* 泵可能阻塞在 GetMessage — 投空消息唤醒。 */
        if (win) {
            if (HWND h = (HWND)ui_window_hwnd(win)) PostMessageW(h, WM_NULL, 0, 0);
        }
    }

    void Cancel() {
        if (cancelIdx < 0) return;
        cancelled = true;
        Finish(cancelIdx);
    }
};

/* 当前活动 msgbox (模态唯一; 嵌套时 Show 内 save/restore 成栈)。仅 UI 线程访问
 * (Show / OnKey / IPC invoke_sync 全在 UI 线程) → 无需加锁。build 172 IPC 自动化。 */
BoxState* g_activeBox = nullptr;

struct BtnCtx {
    BoxState* state;
    int       idx;
};

void OnBtnClick(UiWidget /*w*/, void* ud) {
    auto* c = static_cast<BtnCtx*>(ud);
    if (c && c->state) c->state->Finish(c->idx);
}

/* build 174: 按钮获得焦点 → 同步 focusedBtnIdx (覆盖 Tab / 方向键 / 鼠标点击各路径,
 * 让方向键导航从实际焦点处继续)。 */
void OnBtnFocus(UiWidget /*w*/, void* ud) {
    auto* c = static_cast<BtnCtx*>(ud);
    if (c && c->state) c->state->focusedBtnIdx = c->idx;
}

void OnKey(UiWindow win, int vk, void* ud) {
    auto* s = static_cast<BoxState*>(ud);
    if (!s) return;
    /* build 172: 每按钮快捷键优先 — 命中 button_keys 即触发该按钮 (含覆盖 Enter)。 */
    for (size_t i = 0; i < s->buttonKeys.size(); ++i) {
        if (s->buttonKeys[i] != 0 && s->buttonKeys[i] == vk) { s->Finish((int)i); return; }
    }
    /* build 174: ←/→ 在按钮间移焦点 (循环)。注: 此回调仅在 DispatchKeyDown 未消费时
     * 触发 —— 聚焦按钮的 Enter/Space 已被内置激活 (onClick→Finish), Left/Right 对
     * 按钮内置不处理故落到这里。布局 btnWidgets[0..n-1] 左→右。 */
    if ((vk == VK_LEFT || vk == VK_RIGHT) && !s->btnWidgets.empty()) {
        const int n = (int)s->btnWidgets.size();
        int target;
        if (s->focusedBtnIdx < 0 || s->focusedBtnIdx >= n) {
            /* 首次方向键: 焦点现身于 default_idx (安全默认), 不移动。 */
            target = (s->defaultIdx >= 0 && s->defaultIdx < n) ? s->defaultIdx : 0;
        } else {
            target = (vk == VK_RIGHT) ? (s->focusedBtnIdx + 1) % n
                                      : (s->focusedBtnIdx - 1 + n) % n;
        }
        ui_window_focus_widget(win, s->btnWidgets[target]);  /* on_focus 同步 focusedBtnIdx */
        return;
    }
    if (vk == VK_ESCAPE && s->cancelIdx >= 0) s->Cancel();
    else if (vk == VK_RETURN) s->Finish(s->focusedBtnIdx >= 0 ? s->focusedBtnIdx : s->defaultIdx);
}

void OnClose(uint64_t /*win*/, void* ud) {
    auto* s = static_cast<BoxState*>(ud);
    if (s && s->cancelIdx >= 0) s->Cancel();
    /* cancel_idx < 0 (不可取消): 吞掉关闭 — lib on_close 后仍会销毁?
     * lib 语义: on_close 回调后窗口销毁。不可取消的 box 不提供关闭按钮
     * (show_buttons 已隐藏), 此回调仅防御性兜底。 */
}

/* 图标: 语义色符号字符 (Segoe UI 自带, 矢量随 DPI)。 */
const wchar_t* IconGlyph(int icon) {
    switch (icon) {
        case UI_MSGBOX_ICON_INFO:     return L"ℹ";  /* ℹ */
        case UI_MSGBOX_ICON_WARNING:  return L"⚠";  /* ⚠ */
        case UI_MSGBOX_ICON_ERROR:    return L"✖";  /* ✖ */
        case UI_MSGBOX_ICON_QUESTION: return L"?";
        default:                      return nullptr;
    }
}

UiColor IconColor(int icon) {
    switch (icon) {
        case UI_MSGBOX_ICON_INFO:    return {0.22f, 0.44f, 0.93f, 1.0f};
        case UI_MSGBOX_ICON_WARNING: return {0.91f, 0.64f, 0.07f, 1.0f};
        case UI_MSGBOX_ICON_ERROR:   return {0.85f, 0.23f, 0.23f, 1.0f};
        default:                     return ui_theme_accent();
    }
}

}  // namespace

UiMsgBoxResult MsgBox::Show(UiWindow parent,
                            const std::wstring& title,
                            const std::wstring& message,
                            const std::vector<std::wstring>& buttons,
                            int default_idx, int cancel_idx, int icon,
                            const std::wstring& check_text,
                            int check_initial,
                            const UiColor* btn_colors,
                            const std::vector<int>& button_keys) {
    UiMsgBoxResult res{};
    res.button  = cancel_idx;
    res.checked = check_initial ? 1 : 0;
    res.cancelled = 1;   /* 提前返回 (建窗失败等) = 没有按钮被点 */
    if (buttons.empty()) { res.button = -1; return res; }
    const int n = (int)buttons.size();
    if (default_idx < 0 || default_idx >= n) default_idx = 0;
    if (cancel_idx >= n) cancel_idx = -1;

    /* ---- 测量 (DWrite 工厂即可, 不需要 RT) ---- */
    Renderer meas;
    auto& ctx = GetContext();
    meas.Init(ctx.D2DFactory(), ctx.DWFactory(), ctx.WICFactory());

    const bool hasIcon  = IconGlyph(icon) != nullptr;
    const float iconCol = hasIcon ? (kIconSize + kIconGap) : 0.0f;
    const float titleW  = meas.MeasureTextWidth(
        title, 13.0f, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD) + 120.0f;
    const float msgNatW = meas.MeasureTextWidth(message, kMsgSize);

    float btnRowW = 0;
    std::vector<float> btnW(buttons.size());
    for (size_t i = 0; i < buttons.size(); ++i) {
        btnW[i] = (std::max)(kBtnMinW,
                             meas.MeasureTextWidth(buttons[i], kMsgSize) +
                                 2 * kBtnPadX);
        btnRowW += btnW[i];
    }
    btnRowW += kBtnGap * (float)(buttons.size() - 1);

    const float msgCapW = (std::min)(msgNatW, 440.0f) + iconCol;
    const bool hasCheck = !check_text.empty();
    const float checkNatW =
        hasCheck ? meas.MeasureTextWidth(check_text, kMsgSize) + 48.0f + iconCol
                 : 0.0f;
    float cardW = (std::max)((std::max)((std::max)(titleW, msgCapW), btnRowW),
                             checkNatW);
    cardW = (std::min)((std::max)(cardW + 2 * kPadX, kCardMinW), kCardMaxW);

    const float msgW = cardW - 2 * kPadX - iconCol;
    const float msgH =
        message.empty() ? 0.0f
                        : meas.MeasureTextHeight(message, msgW, kMsgSize);
    constexpr float kCheckH = 24.0f;
    const float bodyH = (std::max)(hasIcon ? kIconSize : 0.0f, msgH);
    /* body 子块统一 gap=kBtnAreaGap (content / [checkbox] / 按钮行)。 */
    const float cardH = kTitleBarH + kPadTop + bodyH +
                        (hasCheck ? kBtnAreaGap + kCheckH : 0.0f) +
                        kBtnAreaGap + kBtnH + kPadBottom;

    /* ---- 窗口 (frameless + owner; DWM 管形状 — Win11 圆角/Win10 直角) ---- */
    HWND parentHwnd = (HWND)ui_window_hwnd(parent);
    UiWindowConfig cfg{};
    cfg.title       = title.c_str();
    cfg.width       = (int)cardW;
    cfg.height      = (int)cardH;
    cfg.system_frame = 0;
    cfg.resizable   = 0;
    cfg.tool_window = 1;
    cfg.skip_animation = 1;
    cfg.owner       = parent;
    /* 居中于 parent (物理像素)。 */
    if (parentHwnd) {
        RECT pr{};
        GetWindowRect(parentHwnd, &pr);
        const UINT dpi = GetDpiForWindow(parentHwnd);
        const float s  = dpi ? (float)dpi / 96.0f : 1.0f;
        const int wpx = (int)(cardW * s), hpx = (int)(cardH * s);
        cfg.x = pr.left + ((pr.right - pr.left) - wpx) / 2;
        cfg.y = pr.top + ((pr.bottom - pr.top) - hpx) / 2;
    }

    UiWindow win = ui_window_create(&cfg);
    if (!win) return res;

    BoxState state;
    state.defaultIdx = default_idx;
    state.cancelIdx  = cancel_idx;
    state.win        = win;
    state.buttonKeys = button_keys;        /* build 172: 每按钮快捷键 */
    state.title      = title;              /* IPC dialog_state 快照 */
    state.buttons    = buttons;

    /* ---- widget 树 ---- */
    UiWidget root = ui_vbox();
    ui_widget_set_gap(root, 0);

    UiWidget tb = ui_titlebar(title.c_str());
    /* 只留关闭按钮 (cancel 可用时); 不可取消的 box 连关闭都不给。 */
    ui_titlebar_show_buttons(tb, 0, 0, cancel_idx >= 0 ? 1 : 0);
    ui_titlebar_show_icon(tb, 0);
    ui_widget_add_child(root, tb);

    UiWidget body = ui_vbox();
    ui_widget_set_padding(body, kPadX, kPadTop, kPadX, kPadBottom);
    ui_widget_set_gap(body, kBtnAreaGap);
    ui_widget_add_child(root, body);

    UiWidget content = ui_hbox();
    ui_widget_set_gap(content, kIconGap);
    if (hasIcon) {
        UiWidget ic = ui_label(IconGlyph(icon));
        ui_label_set_font_size(ic, kIconSize - 2.0f);
        ui_label_set_text_color(ic, IconColor(icon));
        ui_widget_set_size(ic, kIconSize, kIconSize);
        ui_widget_add_child(content, ic);
    }
    UiWidget msg = ui_label(message.c_str());
    ui_label_set_font_size(msg, kMsgSize);
    ui_label_set_wrap(msg, 1);
    ui_widget_set_size(msg, msgW, msgH);
    ui_widget_add_child(content, msg);
    ui_widget_add_child(body, content);

    /* 勾选框 — 系统 TaskDialog verification checkbox 同位: 按钮行上方,
     * 与消息文本左对齐 (有图标时随文字列缩进)。 */
    UiWidget check = 0;
    if (hasCheck) {
        UiWidget checkRow = ui_hbox();
        ui_widget_set_padding(checkRow, hasIcon ? iconCol : 0.0f, 0, 0, 0);
        check = ui_checkbox(check_text.c_str());
        ui_checkbox_set_checked(check, check_initial ? 1 : 0);
        /* checkbox 默认 SizeHint 偏窄会截断文本 — 按测量显式给宽。 */
        ui_widget_set_size(check,
                           meas.MeasureTextWidth(check_text, kMsgSize) + 48.0f,
                           24.0f);
        ui_widget_add_child(checkRow, check);
        ui_widget_add_child(body, checkRow);
    }

    /* 按钮行 — 右对齐: 用 padding-left 把整行推到右缘 (HBox 无 justify)。 */
    UiWidget btnRow = ui_hbox();
    ui_widget_set_gap(btnRow, kBtnGap);
    const float push = (std::max)(0.0f, cardW - 2 * kPadX - btnRowW);
    ui_widget_set_padding(btnRow, push, 0, 0, 0);
    std::vector<BtnCtx> btnCtx(buttons.size());
    for (size_t i = 0; i < buttons.size(); ++i) {
        UiWidget b = ui_button(buttons[i].c_str());
        ui_widget_set_size(b, btnW[i], kBtnH);
        if (btn_colors && btn_colors[i].a > 0) {
            /* 自定义色: 实底 + 自动黑白字 (ButtonWidget cssOwnsBg 路径) */
            ui_widget_set_bg_color(b, btn_colors[i]);
        } else if ((int)i == default_idx) {
            ui_button_set_type(b, 1);  /* primary */
        }
        btnCtx[i] = {&state, (int)i};
        ui_widget_on_click(b, &OnBtnClick, &btnCtx[i]);
        ui_widget_on_focus(b, &OnBtnFocus, &btnCtx[i]);  /* build 174: 同步 focusedBtnIdx */
        ui_widget_add_child(btnRow, b);
        state.btnWidgets.push_back(b);   /* build 174: 方向键移焦点用 */
    }
    ui_widget_add_child(body, btnRow);

    ui_window_set_root(win, root);
    ui_window_on_key(win, &OnKey, &state);
    ui_window_on_close(win, &OnClose, &state);

    /* ---- 模态: 禁宿主 → 显示 → 泵 → 恢复 ---- */
    const bool parentWasEnabled = parentHwnd && IsWindowEnabled(parentHwnd);
    if (parentHwnd) EnableWindow(parentHwnd, FALSE);

    ui_window_show_immediate(win);
    HWND boxHwnd = (HWND)ui_window_hwnd(win);
    if (boxHwnd && !TestIsolationEnabled()) {   // 测试隔离: 不抢用户前台
        SetForegroundWindow(boxHwnd);
        SetFocus(boxHwnd);
    }

    /* build 172: 注册为当前活动 msgbox (IPC 自动化驱动)。嵌套时 save/restore 成栈。
     * 同线程访问, 无需锁。 */
    BoxState* prevActive = g_activeBox;
    g_activeBox = &state;

    /* build 174: 开框默认不显焦点 (focusedBtnIdx=-1, 无焦点环) —— 鼠标用户不该看到
     * 焦点环。回车在无焦点时走 OnKey 兜底=default_idx (调用方设成"取消"=安全)。首次
     * 方向键 / Tab 才让焦点现身 (方向键见 OnKey: 现身于 default_idx; Tab 走内置
     * FocusNext + 亮环)。default_idx 按钮的 accent 主样式仍在, 标示默认动作。 */

    MSG m;
    while (!state.done && GetMessageW(&m, nullptr, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    g_activeBox = prevActive;
    if (!state.done) {
        /* WM_QUIT 流到这 — 转回外层泵, 按取消收场。 */
        PostQuitMessage((int)m.wParam);
        state.result = cancel_idx;
        state.cancelled = true;
    }
    /* 勾选终态在窗口销毁前读取。 */
    if (check) res.checked = ui_checkbox_get_checked(check) ? 1 : 0;

    /* 先恢复宿主再销毁自己 — 反序 Windows 会把激活权丢给其它应用
     * (MessageBox 实现的经典细节)。 */
    if (parentHwnd && parentWasEnabled) EnableWindow(parentHwnd, TRUE);
    if (parentHwnd && !TestIsolationEnabled()) SetForegroundWindow(parentHwnd);
    ui_window_destroy(win);
    res.button = state.result;
    res.cancelled = state.cancelled ? 1 : 0;
    return res;
}

// ---- IPC 自动化钩子 (build 172) — 仅 UI 线程调 (debug server invoke_sync) ----

MsgBoxDebugInfo MsgBoxDebugSnapshot() {
    MsgBoxDebugInfo info;
    if (BoxState* s = g_activeBox) {
        info.active       = true;
        info.default_idx  = s->defaultIdx;
        info.cancel_idx   = s->cancelIdx;
        info.button_count = (int)s->buttons.size();
        info.focused_idx  = s->focusedBtnIdx;
        info.title        = s->title;
        info.buttons      = s->buttons;
    }
    return info;
}

bool MsgBoxDebugFinish(int idx) {
    if (BoxState* s = g_activeBox) {
        s->Finish(idx);   /* idx 越界(如 cancel=-1)= 关闭按取消收场, 等价 OnClose */
        return true;
    }
    return false;
}

bool MsgBoxDebugCancel() {
    BoxState* s = g_activeBox;
    if (!s || s->cancelIdx < 0) return false;
    s->Cancel();
    return true;
}

}  // namespace ui
