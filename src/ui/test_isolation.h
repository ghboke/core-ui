#pragma once
// 测试隔离: 自动化测试和用户共用一个桌面时互不打扰。
//
// 环境变量 UI_CORE_TEST_ISOLATE=1 时, 窗口不抢前台 / 焦点 (不激活、不置前), 也不理真实
// 鼠标, 只认模拟输入 (ui_debug_*)。测试目标经 CMake 的 ui_core_mark_test 自动设置
// (test/src/test_isolation_env.cpp); 宿主做自动化测试时启动前设这个变量即可。
//
// 每次调用都读环境变量 (只在显示 / 激活这类少见路径上用), 测试可以中途改它对比行为。

#include <windows.h>

namespace ui {

inline bool TestIsolationEnabled() {
    wchar_t v[4] = {};
    return GetEnvironmentVariableW(L"UI_CORE_TEST_ISOLATE", v, 4) > 0 && v[0] == L'1';
}

/* 模拟输入标记: 调试接口 / 测试故意走消息队列投递鼠标消息 (测真实消息路径) 时, wParam 或上
 * 这一位, 隔离下窗口凭它跟用户的真实鼠标区分。系统不会设它: 鼠标消息的 wParam 低字是
 * MK_* (最大 0x40), 标题栏消息是命中码 (最大 HTHELP = 21)。窗口入口处去掉后照常处理。 */
constexpr WPARAM kSimInputMark = 0x4000;

inline bool IsMouseInputMessage(UINT msg) {
    switch (msg) {
    case WM_MOUSEMOVE: case WM_MOUSELEAVE: case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
    case WM_NCLBUTTONDOWN: case WM_NCLBUTTONUP: case WM_NCLBUTTONDBLCLK:
    case WM_NCRBUTTONDOWN: case WM_NCRBUTTONUP: case WM_NCRBUTTONDBLCLK:
    case WM_NCMBUTTONDOWN: case WM_NCMBUTTONUP: case WM_NCMBUTTONDBLCLK:
        return true;
    default:
        return false;
    }
}

}  // namespace ui
