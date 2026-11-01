#include "frame_ticker.h"

#include <dwmapi.h>

#include <algorithm>
#include <climits>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace ui {

namespace frameticker {

int64_t NextWakeQpc(int64_t now, int64_t lastVBlank, int64_t period, int64_t offset) {
    if (period <= 0) return now;
    // 只取 lastVBlank 的相位: 它可能落后, 也可能超前若干周期 (DWM 报的是排队中的帧)。
    const int64_t first = lastVBlank + offset;
    if (first > now) {
        const int64_t back = (first - now - 1) / period;   // 往回推到仍 > now 的最早一个
        return first - back * period;
    }
    const int64_t k = (now - first) / period + 1;
    return first + k * period;
}

InputPaintPlan PlanInputPaint(int64_t now, int64_t lastSubmit, int64_t lastVBlank,
                              int64_t period, int64_t lead) {
    InputPaintPlan plan;
    if (period <= 0 || lead <= 0 || lead >= period) return plan;
    if (lastSubmit == 0 || now - lastSubmit >= period) return plan;   // 空闲: 立即
    const int64_t deadline = NextWakeQpc(now, lastVBlank, period, period - lead);
    if (deadline - now > period - lead) {
        // 已在 vblank 前 lead 之内: 这次 vblank 前还没画过就立即画 (等不及了); 已画过
        // (本槽 vblank-lead 之后提交过) 就并到下一周期, 不然最后几毫秒每条移动都画一帧。
        const int64_t slot = deadline - period;
        if (lastSubmit < slot) return plan;
    }
    plan.now = false;
    plan.deadline = deadline;
    return plan;
}

}  // namespace frameticker

namespace {

struct Subscriber {
    UINT msg = 0;
    bool posted = false;
    uint32_t skipped = 0;   // 上一拍还没取走而跳过的拍数, 随下一条消息报给窗口 (trace 用)
};

int64_t QpcNow() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

int64_t QpcFreq() {
    static const int64_t f = [] {
        LARGE_INTEGER v;
        QueryPerformanceFrequency(&v);
        return v.QuadPart;
    }();
    return f;
}

struct Shot {
    HWND hwnd = nullptr;
    UINT msg = 0;
    int64_t at = 0;
};

bool QueryDwmTiming(int64_t& lastVBlank, int64_t& period) {
    DWM_TIMING_INFO ti{};
    ti.cbSize = sizeof(ti);
    if (FAILED(DwmGetCompositionTimingInfo(nullptr, &ti)) || ti.qpcRefreshPeriod == 0) return false;
    lastVBlank = static_cast<int64_t>(ti.qpcVBlank);
    period = static_cast<int64_t>(ti.qpcRefreshPeriod);
    return true;
}

}  // namespace

struct FrameTicker::Impl {
    std::mutex mu;
    std::condition_variable cv;
    std::unordered_map<HWND, Subscriber> subs;
    std::vector<Shot> shots;
    std::thread thread;
    bool stop = false;
    HANDLE timer = nullptr;
    HANDLE wakeEvent = nullptr;   // PostAt 打断线程的等待, 重新算醒来时刻

    std::mutex timingMu;
    int64_t cachedVBlank = 0, cachedPeriod = 0, cachedAt = 0;

    void EnsureThreadLocked();
    void Run();
    bool WaitUntil(int64_t wakeQpc);   // false = 被 wakeEvent 打断
    void StoreTiming(int64_t vblank, int64_t period);
};

FrameTicker& FrameTicker::Instance() {
    static FrameTicker* inst = new FrameTicker();   // 故意不析构, 见头文件
    return *inst;
}

FrameTicker::Impl& FrameTicker::State() {
    if (!impl_) impl_ = new Impl();
    return *impl_;
}

void FrameTicker::Impl::EnsureThreadLocked() {
    if (!wakeEvent) wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (thread.joinable()) return;
    stop = false;
    thread = std::thread([this] { Run(); });
}

void FrameTicker::Subscribe(HWND hwnd, UINT msg) {
    if (!hwnd) return;
    Impl& s = State();
    std::lock_guard<std::mutex> lock(s.mu);
    Subscriber& sub = s.subs[hwnd];
    sub.msg = msg;
    sub.posted = false;   // 消息万一丢了, 重新订阅能解开"已投递"
    s.EnsureThreadLocked();
    s.cv.notify_one();
}

void FrameTicker::PostAt(HWND hwnd, UINT msg, int64_t atQpc) {
    if (!hwnd) return;
    Impl& s = State();
    {
        std::lock_guard<std::mutex> lock(s.mu);
        bool replaced = false;
        for (Shot& shot : s.shots) {
            if (shot.hwnd == hwnd && shot.msg == msg) { shot.at = atQpc; replaced = true; break; }
        }
        if (!replaced) s.shots.push_back({hwnd, msg, atQpc});
        s.EnsureThreadLocked();
    }
    s.cv.notify_one();
    if (s.wakeEvent) SetEvent(s.wakeEvent);   // 线程可能正睡到更晚的心跳
}

void FrameTicker::Impl::StoreTiming(int64_t vblank, int64_t period) {
    std::lock_guard<std::mutex> lock(timingMu);
    cachedVBlank = vblank;
    cachedPeriod = period;
    cachedAt = QpcNow();
}

bool FrameTicker::VBlankTiming(int64_t& lastVBlank, int64_t& period) {
    Impl& s = State();
    {
        std::lock_guard<std::mutex> lock(s.timingMu);
        if (s.cachedPeriod > 0 && QpcNow() - s.cachedAt < QpcFreq() / 2) {
            lastVBlank = s.cachedVBlank;
            period = s.cachedPeriod;
            return true;
        }
    }
    if (!QueryDwmTiming(lastVBlank, period)) return false;
    s.StoreTiming(lastVBlank, period);
    return true;
}

void FrameTicker::Unsubscribe(HWND hwnd) {
    if (!impl_ || !hwnd) return;
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->subs.erase(hwnd);   // 只停心跳: 已约好的一次性投递窗口还在等, 不能丢
}

void FrameTicker::Forget(HWND hwnd) {
    if (!impl_ || !hwnd) return;
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->subs.erase(hwnd);
    /* 窗口销毁: 一并丢掉它还没到点的一次性投递, 免得 HWND 复用后收到。 */
    auto& shots = impl_->shots;
    for (size_t i = 0; i < shots.size();) {
        if (shots[i].hwnd == hwnd) { shots[i] = shots.back(); shots.pop_back(); }
        else ++i;
    }
}

void FrameTicker::Ack(HWND hwnd) {
    if (!impl_ || !hwnd) return;
    std::lock_guard<std::mutex> lock(impl_->mu);
    auto it = impl_->subs.find(hwnd);
    if (it != impl_->subs.end()) it->second.posted = false;
}

void FrameTicker::Stop() {
    if (!impl_) return;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        impl_->stop = true;
        impl_->subs.clear();
        impl_->shots.clear();
    }
    impl_->cv.notify_one();
    if (impl_->wakeEvent) SetEvent(impl_->wakeEvent);
    if (impl_->thread.joinable()) impl_->thread.join();
}

bool FrameTicker::Impl::WaitUntil(int64_t wakeQpc) {
    const int64_t waitQpc = wakeQpc - QpcNow();
    if (waitQpc <= 0) return true;
    // 可等待定时器的单位是 100ns, 负数表示相对时间。
    const int64_t hundredNs = waitQpc * 10000000 / QpcFreq();
    if (timer) {
        LARGE_INTEGER due;
        due.QuadPart = -hundredNs;
        if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
            if (!wakeEvent) {
                WaitForSingleObject(timer, INFINITE);
                return true;
            }
            const HANDLE handles[2] = {timer, wakeEvent};
            if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0) return true;
            CancelWaitableTimer(timer);
            return false;
        }
    }
    if (wakeEvent) {
        return WaitForSingleObject(wakeEvent, static_cast<DWORD>(hundredNs / 10000)) == WAIT_TIMEOUT;
    }
    Sleep(static_cast<DWORD>(hundredNs / 10000));
    return true;
}

void FrameTicker::Impl::Run() {
    // 高精度定时器 (Win10 1803+) 不改系统时钟精度, 不像 timeBeginPeriod 那样费电;
    // 拿不到就退回普通可等待定时器 / Sleep。
    timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                   TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);

    const int64_t freq = QpcFreq();
    const int64_t offset = freq / 1000;          // vblank 后 1ms 醒: DWM 已经拿走上一帧
    const int64_t fallbackPeriod = freq / 60;
    const int64_t slack = freq / 2000;           // 定时器早醒 0.5ms 内算到点
    int64_t lastWake = 0;                        // 上一次心跳

    for (;;) {
        bool haveSubs = false;
        int64_t shotAt = INT64_MAX;
        {
            std::unique_lock<std::mutex> lock(mu);
            cv.wait(lock, [this] { return stop || !subs.empty() || !shots.empty(); });
            if (stop) break;
            haveSubs = !subs.empty();
            for (const Shot& shot : shots) shotAt = (std::min)(shotAt, shot.at);
        }

        int64_t beatWake = INT64_MAX;
        if (haveSubs) {
            int64_t lastVBlank = 0, period = 0;
            if (QueryDwmTiming(lastVBlank, period)) {
                StoreTiming(lastVBlank, period);
            } else {
                lastVBlank = lastWake ? lastWake - offset : QpcNow();
                period = fallbackPeriod;
            }
            // 至少隔半个周期: DWM 时间戳偶尔没更新时, 不会在同一个 vblank 里连醒两次。
            int64_t now = QpcNow();
            if (lastWake && now - lastWake < period / 2) now = lastWake + period / 2;
            beatWake = frameticker::NextWakeQpc(now, lastVBlank, period, offset);
        }
        if (!WaitUntil((std::min)(beatWake, shotAt))) continue;   // 新的 PostAt: 重算
        const int64_t now = QpcNow();

        std::lock_guard<std::mutex> lock(mu);
        if (stop) break;
        if (haveSubs && now + slack >= beatWake) {
            lastWake = now;
            for (auto it = subs.begin(); it != subs.end();) {
                Subscriber& sub = it->second;
                if (sub.posted) { ++sub.skipped; ++it; continue; }
                if (!PostMessageW(it->first, sub.msg, static_cast<WPARAM>(sub.skipped),
                                  static_cast<LPARAM>(lastWake))) {
                    it = subs.erase(it);   // 窗口已经没了
                    continue;
                }
                sub.posted = true;
                sub.skipped = 0;
                ++it;
            }
        }
        for (size_t i = 0; i < shots.size();) {
            if (shots[i].at > now + slack) { ++i; continue; }
            PostMessageW(shots[i].hwnd, shots[i].msg, 0, 0);
            shots[i] = shots.back();
            shots.pop_back();
        }
    }

    if (timer) {
        CloseHandle(timer);
        timer = nullptr;
    }
}

}  // namespace ui
