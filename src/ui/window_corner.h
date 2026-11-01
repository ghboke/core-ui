#pragma once

/* Win11 窗口圆角策略 (build 312)。
 *
 * Win11 DWM 默认只给带 WS_THICKFRAME / WS_CAPTION 的窗口画圆角; 无边框且不可
 * 缩放的窗口是纯 WS_POPUP, 默认直角 —— 宿主弹窗 / ui_msgbox 因此跟主窗口
 * (可缩放, 自动圆角) 不一致。这类窗口显式请求 DWMWCP_ROUND; 其余保持
 * DWMWCP_DEFAULT, 由系统处理最大化 / 贴靠 / 全屏时的去圆角。
 *
 * Win10 不认识该属性, DwmSetWindowAttribute 返回 E_INVALIDARG, 窗口保持直角。
 * 常量本地定义: 老 Windows SDK 的 dwmapi.h 没有它们。 */
namespace ui {

constexpr unsigned long kDwmWindowCornerPreference = 33;  // DWMWA_WINDOW_CORNER_PREFERENCE
constexpr int kDwmCornerDefault = 0;                      // DWMWCP_DEFAULT
constexpr int kDwmCornerRound   = 2;                      // DWMWCP_ROUND

constexpr int CornerPreferenceFor(bool borderless, bool resizable) {
    return (borderless && !resizable) ? kDwmCornerRound : kDwmCornerDefault;
}

}  // namespace ui
