#pragma once
//
// frame_ticker.h — 动画心跳: 按显示器刷新 (vblank) 给正在动画的窗口发消息。
//
// 取代以前每个窗口自己的 SetTimer(16ms) —— 系统定时器 15.6ms 一跳, 帧间隔会在
// 16 / 31ms 间跳, 也对不上显示器刷新。这里一个进程共用一个后台线程:
//   - 用 DwmGetCompositionTimingInfo 拿上次 vblank 时刻与刷新周期 (DWM 全局只有一个
//     合成时钟, 多显示器 / 双显卡都适用), 用高精度可等待定时器睡到下一个 vblank 之后
//     一点点再醒, 醒来给每个订阅窗口 PostMessage。窗口立刻推进动画并画一帧, 交换链
//     Present(0) 后正好赶上下一次 DWM 合成。
//   - 每个窗口最多一条消息在队列: 窗口处理时调用 Ack 之前不会再发, UI 线程忙时
//     不会堆积一串过期帧。
//   - 没有订阅者时线程挂起, 不占 CPU。
//   - 消息参数: wParam = 上一拍没取走而跳过的拍数, lParam = 本拍醒来的 QPC 时刻
//     (窗口拿来量排队延迟, 只给 trace 用)。
//
// 线程: Subscribe / Unsubscribe / Ack 可在任意线程调用 (内部加锁, PostMessage 在锁内,
// 窗口在 WM_DESTROY 里同步退订后就不会再收到)。Stop 在 ui_shutdown 里调用, 不放进
// 静态析构 (DLL 卸载持加载器锁时 join 会死锁; 单例故意不析构)。
//
#include <windows.h>
#include <cstdint>

namespace ui {

class FrameTicker {
public:
    static FrameTicker& Instance();

    void Subscribe(HWND hwnd, UINT msg);   // 重复订阅只会复位"已投递"标记
    void Unsubscribe(HWND hwnd);            // 只停心跳, 不动已约好的 PostAt
    void Forget(HWND hwnd);                 // 窗口销毁时: 停心跳并丢掉未到点的 PostAt
    void Ack(HWND hwnd);                    // 窗口取走了这一拍, 允许投下一拍
    // 一次性: 在 QPC 时刻 atQpc 给 hwnd 投一条 msg (wParam/lParam 为 0), 误差约 1ms。
    // 同一 hwnd + msg 重复约定时以最后一次为准。跟心跳订阅互不影响。
    void PostAt(HWND hwnd, UINT msg, int64_t atQpc);
    // 刷新时序 (QPC): 上一次 vblank 的相位参考与周期; 拿不到时返回 false。
    // 内部按 0.5s 缓存 DwmGetCompositionTimingInfo, 可在任意线程高频调用。
    bool VBlankTiming(int64_t& lastVBlank, int64_t& period);
    void Stop();

    FrameTicker(const FrameTicker&) = delete;
    FrameTicker& operator=(const FrameTicker&) = delete;

private:
    FrameTicker() = default;
    struct Impl;
    Impl* impl_ = nullptr;
    Impl& State();
};

namespace frameticker {

// 下一次唤醒的 QPC 时刻: 晚于 now 的第一个 "vblank + offset"。
// lastVBlank 是 DWM 报的上一次 vblank (可能落后若干周期, 也可能略新于 now)。
// period <= 0 时返回 now (调用方自己退回固定间隔)。
int64_t NextWakeQpc(int64_t now, int64_t lastVBlank, int64_t period, int64_t offset);

// 输入 (鼠标移动) 引起的重绘: 立即画, 还是并到下一次 vblank 前 lead 处一起画。
//   - 上一帧距今已超过一个周期 (空闲后第一次移动) → 立即画, 延迟最低;
//   - 离下一次 vblank 已不到 lead → 等不及, 立即画;
//   - 否则延到 deadline = 下一次 vblank - lead: 同一周期里的移动只画最后状态那一帧,
//     又赶在 DWM 合成之前, 不比立即画多一帧延迟。
// lastSubmit 为 0 表示从没画过。全部单位 QPC。
struct InputPaintPlan {
    bool now = true;
    int64_t deadline = 0;
};
InputPaintPlan PlanInputPaint(int64_t now, int64_t lastSubmit, int64_t lastVBlank,
                              int64_t period, int64_t lead);

}  // namespace frameticker
}  // namespace ui
