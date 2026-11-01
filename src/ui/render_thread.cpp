#include "render_thread.h"

#include "debug_trace.h"
#include "renderer.h"

#include <chrono>
#include <iterator>
#include <objbase.h>

namespace ui {

namespace {

RenderDeviceStatus DeviceStatusFromResult(RenderFrameResult result) {
    switch (result) {
    case RenderFrameResult::RecreateTarget:
        return RenderDeviceStatus::RecreateTarget;
    case RenderFrameResult::DeviceFailure:
        return RenderDeviceStatus::UnknownFailure;
    default:
        return RenderDeviceStatus::Ok;
    }
}

bool IsCompletedRenderResult(RenderFrameResult result) {
    return result == RenderFrameResult::Presented ||
           result == RenderFrameResult::Prepared ||
           result == RenderFrameResult::Skipped;
}

void ResetDeviceResources(std::vector<std::shared_ptr<RenderWindow>>& windows) {
    for (auto& window : windows) {
        if (window) window->ReleaseDeviceResources();
    }
    Renderer::ResetSharedDeviceForDeviceLost();
}

} // namespace

RenderThread& RenderThread::Instance() {
    static RenderThread thread;
    return thread;
}

RenderThread::~RenderThread() {
    Stop();
    if (wakeEvent_) CloseHandle(wakeEvent_);
}

void RenderThread::Wake() {
    cv_.notify_one();
    if (wakeEvent_) SetEvent(wakeEvent_);
}

void RenderThread::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) return;
    if (!wakeEvent_) wakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    stopRequested_ = false;
    running_ = true;
    worker_ = std::thread(&RenderThread::ThreadMain, this);
}

void RenderThread::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;
        stopRequested_ = true;
    }
    cv_.notify_all();
    if (wakeEvent_) SetEvent(wakeEvent_);
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    latestJobs_.clear();
    presentJobs_.clear();
    windows_.clear();
    completedGenerations_.clear();
    running_ = false;
}

void RenderThread::RegisterWindow(RenderWindowId id, HWND hwnd, RenderSurfaceKind surfaceKind,
                                  std::shared_ptr<LayeredFrameBuffer> layered) {
    if (!id.IsValid() || !hwnd) return;
    bool notify = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = windows_.find(id.window_id);
        if (it == windows_.end()) {
            windows_.emplace(id.window_id,
                             std::make_shared<RenderWindow>(id, hwnd, surfaceKind, layered));
            return;
        }
        if (it->second && it->second->Matches(id) && it->second->Hwnd() == hwnd &&
            it->second->SurfaceKind() == surfaceKind && it->second->Layered() == layered) {
            it->second->Register(id, hwnd, surfaceKind, layered);
            return;
        }
        if (it->second) {
            resources_.ClearWindow(it->second->Id());
        }
        latestJobs_.erase(id.window_id);
        completedGenerations_.erase(id.window_id);
        retiredWindows_.push_back(std::move(it->second));
        windows_.erase(it);
        windows_.emplace(id.window_id,
                         std::make_shared<RenderWindow>(id, hwnd, surfaceKind, layered));
        notify = true;
    }
    if (notify) Wake();
}

void RenderThread::UnregisterWindow(RenderWindowId id) {
    if (!id.IsValid()) return;
    bool notify = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = windows_.find(id.window_id);
        if (it != windows_.end() && it->second && it->second->Matches(id)) {
            retiredWindows_.push_back(std::move(it->second));
            windows_.erase(it);
            latestJobs_.erase(id.window_id);
            completedGenerations_.erase(id.window_id);
            resources_.ClearWindow(id);
            notify = true;
        }
    }
    if (notify) Wake();
}

void RenderThread::Submit(FrameJob job) {
    if (!job.window.IsValid()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = windows_.find(job.window.window_id);
    if (it == windows_.end() || !it->second || !it->second->Matches(job.window)) return;
    latestJobs_.insert_or_assign(job.window.window_id, std::move(job));
    Wake();
}

bool RenderThread::PresentPrepared(RenderWindowId id, bool skipVSync, uint32_t timeoutMs) {
    if (!id.IsValid()) return false;

    PresentJob job;
    job.window = id;
    job.skipVSync = skipVSync;
    auto future = job.result.get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = windows_.find(id.window_id);
        if (!running_ || it == windows_.end() || !it->second || !it->second->Matches(id)) {
            return false;
        }
        presentJobs_.push_back(std::move(job));
    }
    Wake();
    if (future.wait_for(std::chrono::milliseconds(timeoutMs)) != std::future_status::ready) {
        return false;
    }
    return future.get();
}

int RenderThread::ScreenshotRegion(RenderWindowId id, D2D1_RECT_F region,
                                   std::wstring outPath, float dpiScale) {
    if (!id.IsValid() || outPath.empty()) return -1;

    ScreenshotJob job;
    job.window = id;
    job.region = region;
    job.outPath = std::move(outPath);
    job.dpiScale = dpiScale;
    auto future = job.result.get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = windows_.find(id.window_id);
        if (it == windows_.end() || !it->second || !it->second->Matches(id)) return -1;
        screenshotJobs_.push_back(std::move(job));
    }
    Wake();
    return future.get();
}

bool RenderThread::WaitForGeneration(uint64_t windowId, uint64_t generation, uint32_t timeoutMs) {
    if (windowId == 0 || generation == 0) return false;
    std::unique_lock<std::mutex> lock(mutex_);
    auto completed = [&]() {
        auto it = completedGenerations_.find(windowId);
        return it != completedGenerations_.end() && it->second >= generation;
    };
    if (completed()) return true;
    cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&]() {
        return completed() || resources_.DeviceLost();
    });
    return completed();
}

size_t RenderThread::RegisteredWindowCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return windows_.size();
}

size_t RenderThread::PendingJobCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latestJobs_.size();
}

uint64_t RenderThread::LastCompletedGeneration(uint64_t windowId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = completedGenerations_.find(windowId);
    return it != completedGenerations_.end() ? it->second : 0;
}

bool RenderThread::DeviceLostForTest() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return resources_.DeviceLost();
}

void RenderThread::ReportDeviceLostForTest(RenderWindowId failedWindow,
                                           RenderDeviceStatus status,
                                           uint64_t failedGeneration) {
    std::vector<std::shared_ptr<RenderWindow>> windowsToReset;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        HandleDeviceLostLocked(failedWindow, status, failedGeneration, windowsToReset);
    }
    ResetDeviceResources(windowsToReset);
}

std::vector<DeviceRecoveryRequest> RenderThread::ConsumeDeviceRecoveryRequestsForTest() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DeviceRecoveryRequest> out;
    out.swap(deviceRecoveryRequests_);
    return out;
}

void RenderThread::HandleDeviceLostLocked(
    RenderWindowId failedWindow,
    RenderDeviceStatus status,
    uint64_t failedGeneration,
    std::vector<std::shared_ptr<RenderWindow>>& windowsToReset) {
    resources_.MarkDeviceLost(status);
    latestJobs_.clear();

    const uint64_t deviceGeneration = resources_.DeviceGeneration();
    TraceEvent("render_thread", "device_lost",
               {TraceU64("failed_window_id", failedWindow.window_id),
                TraceU64("failed_hwnd_generation", failedWindow.hwnd_generation),
                TraceU64("failed_frame_generation", failedGeneration),
                TraceU64("device_generation", deviceGeneration),
                TraceI64("status", static_cast<int64_t>(status)),
                TraceI64("registered_windows", static_cast<int64_t>(windows_.size()))});

    windowsToReset.reserve(windowsToReset.size() + windows_.size());
    for (const auto& entry : windows_) {
        const auto& window = entry.second;
        if (!window || !window->IsRegistered()) continue;
        windowsToReset.push_back(window);

        DeviceRecoveryRequest request;
        request.window = window->Id();
        request.hwnd = window->Hwnd();
        request.status = status;
        request.failed_generation = failedGeneration;
        request.device_generation = deviceGeneration;
        deviceRecoveryRequests_.push_back(request);
        if (deviceRecoveryRequests_.size() > 128) {
            deviceRecoveryRequests_.erase(deviceRecoveryRequests_.begin());
        }
        if (request.hwnd) {
            PostMessageW(request.hwnd, kUiCoreRenderDeviceLostMessage,
                         static_cast<WPARAM>(request.status),
                         static_cast<LPARAM>(request.device_generation));
        }
    }
    cv_.notify_all();
}

void RenderThread::ThreadMain() {
    HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool coInitialized = SUCCEEDED(coHr);
    std::unique_lock<std::mutex> lock(mutex_);
    auto releaseWindows = [&](std::vector<std::shared_ptr<RenderWindow>>& windows) {
        if (windows.empty()) return;
        lock.unlock();
        for (auto& window : windows) {
            if (window) {
                window->Unregister();
            }
        }
        windows.clear();
        lock.lock();
    };
    for (;;) {
        cv_.wait(lock, [&]() {
            return stopRequested_ || !latestJobs_.empty() ||
                   !presentJobs_.empty() || !screenshotJobs_.empty() ||
                   !retiredWindows_.empty();
        });
        if (!retiredWindows_.empty()) {
            std::vector<std::shared_ptr<RenderWindow>> retired;
            retired.swap(retiredWindows_);
            releaseWindows(retired);
            continue;
        }
        if (stopRequested_) {
            while (!presentJobs_.empty()) {
                auto present = std::move(presentJobs_.front());
                presentJobs_.pop_front();
                present.result.set_value(false);
            }
            while (!screenshotJobs_.empty()) {
                auto screenshot = std::move(screenshotJobs_.front());
                screenshotJobs_.pop_front();
                screenshot.result.set_value(-1);
            }
            std::vector<std::shared_ptr<RenderWindow>> active;
            active.reserve(windows_.size());
            for (auto& entry : windows_) {
                active.push_back(std::move(entry.second));
            }
            windows_.clear();
            std::vector<std::shared_ptr<RenderWindow>> retired;
            retired.swap(retiredWindows_);
            releaseWindows(active);
            releaseWindows(retired);
            resources_.ClearAll();
            break;
        }

        if (!presentJobs_.empty()) {
            auto present = std::move(presentJobs_.front());
            presentJobs_.pop_front();

            std::shared_ptr<RenderWindow> window;
            auto it = windows_.find(present.window.window_id);
            if (it != windows_.end() && it->second && it->second->Matches(present.window)) {
                window = it->second;
            }

            lock.unlock();
            RenderFrameResult result = window
                ? window->PresentPrepared(present.skipVSync)
                : RenderFrameResult::InvalidWindow;
            const bool ok = result == RenderFrameResult::Presented ||
                            result == RenderFrameResult::Skipped;
            present.result.set_value(ok);
            lock.lock();

            if (result == RenderFrameResult::RecreateTarget ||
                result == RenderFrameResult::DeviceFailure) {
                std::vector<std::shared_ptr<RenderWindow>> windowsToReset;
                HandleDeviceLostLocked(present.window, DeviceStatusFromResult(result),
                                       0, windowsToReset);
                lock.unlock();
                ResetDeviceResources(windowsToReset);
                lock.lock();
            }
            continue;
        }

        auto job = TakeNextJobLocked();
        if (!job) {
            if (!screenshotJobs_.empty()) {
                auto screenshot = std::move(screenshotJobs_.front());
                screenshotJobs_.pop_front();

                std::shared_ptr<RenderWindow> window;
                auto it = windows_.find(screenshot.window.window_id);
                if (it != windows_.end() && it->second && it->second->Matches(screenshot.window)) {
                    window = it->second;
                }

                lock.unlock();
                int result = window
                    ? window->ScreenshotRegion(screenshot.region, screenshot.outPath, screenshot.dpiScale)
                    : -1;
                screenshot.result.set_value(result);
                lock.lock();
            } else if (!latestJobs_.empty()) {
                WaitForFrameSlotLocked(lock);
            }
            continue;
        }
        lock.unlock();

        RenderFrameResult renderResult = RenderFrameResult::Skipped;
        if (job->render_thread_present) {
            std::shared_ptr<RenderWindow> window;
            lock.lock();
            auto it = windows_.find(job->window.window_id);
            if (it != windows_.end() && it->second && it->second->Matches(job->window)) {
                window = it->second;
            }
            lock.unlock();

            renderResult = window
                ? window->RenderFrame(*job)
                : RenderFrameResult::InvalidWindow;

            lock.lock();
            if (renderResult == RenderFrameResult::RecreateTarget ||
                renderResult == RenderFrameResult::DeviceFailure) {
                std::vector<std::shared_ptr<RenderWindow>> windowsToReset;
                HandleDeviceLostLocked(job->window, DeviceStatusFromResult(renderResult),
                                       job->generation, windowsToReset);
                lock.unlock();
                ResetDeviceResources(windowsToReset);
                lock.lock();
            }
            lock.unlock();
        }

        lock.lock();
        auto it = windows_.find(job->window.window_id);
        if (IsCompletedRenderResult(renderResult) &&
            it != windows_.end() &&
            it->second &&
            it->second->Matches(job->window)) {
            completedGenerations_[job->window.window_id] = job->generation;
            if (renderResult == RenderFrameResult::Presented && resources_.DeviceLost()) {
                resources_.MarkDeviceRecovered();
                TraceEvent("render_thread", "device_recovered",
                           {TraceU64("window_id", job->window.window_id),
                            TraceU64("frame_generation", job->generation),
                            TraceU64("device_generation", resources_.DeviceGeneration())});
            }
            cv_.notify_all();
        }
    }
    lock.unlock();
    if (coInitialized) CoUninitialize();
}

bool RenderThread::JobNeedsFrameSlot(const FrameJob& job) {
    /* 改尺寸事务的预备帧 (defer_present) 和收尾 / 截图帧要立刻画, 不等名额: UI 线程
     * 在 WaitForGeneration 上等着它们。预备帧交的时候再补拿 (Renderer::PresentPrepared)。 */
    return job.render_thread_present && !job.defer_present &&
           job.policy != PresentPolicy::Final && job.policy != PresentPolicy::Screenshot;
}

std::optional<FrameJob> RenderThread::TakeNextJobLocked() {
    if (latestJobs_.empty()) return std::nullopt;

    auto best = latestJobs_.end();
    for (auto it = latestJobs_.begin(); it != latestJobs_.end(); ++it) {
        const auto& a = it->second;
        if (JobNeedsFrameSlot(a)) {
            auto w = windows_.find(it->first);
            // 窗口已不在: 照常取出, 渲染那边按无效窗口处理掉
            if (w != windows_.end() && w->second && !w->second->TryAcquireFrameSlot()) continue;
        }
        if (best == latestJobs_.end()) { best = it; continue; }
        const auto& b = best->second;
        if (a.priority > b.priority ||
            (a.priority == b.priority && a.generation > b.generation)) {
            best = it;
        }
    }
    if (best == latestJobs_.end()) return std::nullopt;   // 都在等名额

    FrameJob job = std::move(best->second);
    latestJobs_.erase(best);
    return job;
}

void RenderThread::WaitForFrameSlotLocked(std::unique_lock<std::mutex>& lock) {
    std::vector<HANDLE> handles{wakeEvent_};
    std::vector<std::shared_ptr<RenderWindow>> owners;
    for (const auto& entry : latestJobs_) {
        auto it = windows_.find(entry.first);
        if (it == windows_.end() || !it->second) continue;
        HANDLE h = it->second->FrameSlotWaitable();
        if (!h || handles.size() >= MAXIMUM_WAIT_OBJECTS) continue;
        handles.push_back(h);
        owners.push_back(it->second);
    }
    if (owners.empty() || !wakeEvent_) return;

    lock.unlock();
    const DWORD r = WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(),
                                           FALSE, Renderer::kFrameSlotWaitMs);
    lock.lock();
    if (r > WAIT_OBJECT_0 && r < WAIT_OBJECT_0 + handles.size()) {
        owners[r - WAIT_OBJECT_0 - 1]->MarkFrameSlotHeld();   // 等到 = 已占用这个名额
    } else if (r == WAIT_TIMEOUT) {
        /* 系统一直没取帧 (窗口刚被遮挡 / 驱动异常): 不等了, 照画照交。 */
        TraceEvent("render_thread", "frame_slot_timeout",
                   {TraceI64("windows", static_cast<int64_t>(owners.size()))});
        for (auto& w : owners) w->MarkFrameSlotHeld();
    }
}

} // namespace ui
