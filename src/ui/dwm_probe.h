#pragma once

/* 闪屏探针: 读 DWM 合成计数, 打进 trace.
 *
 * 用途: 判断"窗口矩形变化"和"对应尺寸的新画面 Present"是否落在同一合成帧。
 * flip model 交换链的翻转与窗口尺寸之间没有同步, 两者跨帧时 DWM 会合成一帧
 * "新尺寸 + 旧画面" (露底色 / 图像跳动)。宿主把 dwm_probe_rect 与随后尺寸
 * 匹配的 dwm_probe_present 按 refresh 计数配对, 差值 >= 1 即存在错帧机会。
 *
 * 只在 trace 开启时查询, 关闭时零开销。 */

#include <windows.h>
#include <dwmapi.h>

#include <cstdint>

#include "debug_trace.h"

namespace ui {

struct DwmFrameStamp {
    uint64_t refresh = 0;  /* DWM_TIMING_INFO::cRefresh: 显示器刷新计数 */
    uint64_t frame   = 0;  /* DWM_TIMING_INFO::cFrame: DWM 已合成帧计数 */
    bool     ok      = false;
};

inline DwmFrameStamp QueryDwmFrameStamp() {
    DwmFrameStamp s;
    DWM_TIMING_INFO ti{};
    ti.cbSize = sizeof(ti);
    /* Win8.1+ 要求 hwnd 传 nullptr, 取全局合成计时。 */
    if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &ti))) {
        s.refresh = ti.cRefresh;
        s.frame   = ti.cFrame;
        s.ok      = true;
    }
    return s;
}

}  // namespace ui
