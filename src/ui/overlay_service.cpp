#include "overlay_service.h"
#include "anim_clock.h"
#include "debug_trace.h"
#include "frame_ticker.h"
#include "theme.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ui {
namespace {

constexpr UINT kOverlayWakeMessage = WM_APP + 0x540;
/* 淡入 / 淡出跟动画心跳 (FrameTicker) 每次刷新一帧; 停留期间退订, 用一次性定时器
 * 在淡出前 kToastHoldWakeLeadMs 醒来重新订阅 (系统定时器 15.6ms 粒度, 提前醒免得淡出
 * 头一帧跳过)。以前整程 16ms 定时器 + timeBeginPeriod(1) 拉高全系统时钟精度。 */
constexpr UINT kToastFrameMessage = WM_APP + 0x541;
constexpr UINT_PTR kToastHoldTimerId = 1;
constexpr int kToastHoldWakeLeadMs = 20;
constexpr int kToastInMs = 200;
constexpr int kToastOutMs = 250;
/* 超宽文字折行 (build 331), 最多这么多行; 再多仍截断并报 text_clipped。 */
constexpr UINT32 kToastMaxLines = 4;

float EaseOutCubic(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

UINT OverlayDpiForWindow(HWND hwnd) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using Fn = UINT(WINAPI*)(HWND);
        auto fn = reinterpret_cast<Fn>(GetProcAddress(user32, "GetDpiForWindow"));
        if (fn) {
            UINT dpi = fn(hwnd);
            if (dpi) return dpi;
        }
    }
    HDC hdc = GetDC(hwnd);
    UINT dpi = hdc ? static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX)) : 96;
    if (hdc) ReleaseDC(hwnd, hdc);
    return dpi ? dpi : 96;
}

struct ToastBitmap {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    void* bits = nullptr;   /* DIB 像素 (预乘 BGRA, 自上而下), 调试截图读它 */
    int width = 0;
    int height = 0;

    ~ToastBitmap() {
        if (dc && oldBitmap) {
            SelectObject(dc, oldBitmap);
        }
        if (bitmap) {
            DeleteObject(bitmap);
        }
        if (dc) {
            DeleteDC(dc);
        }
    }
};

struct ToastOverlay {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    std::wstring text;
    int durationMs = 2000;
    int position = 0;
    int icon = 0;
    int anim = 0;
    UINT dpi = 96;
    float dpiScale = 1.0f;
    float widthDip = 0.0f;
    float heightDip = 0.0f;
    int targetX = 0;
    int targetY = 0;
    int slideRangePx = 0;
    uint64_t startTick = 0;     // AnimNowMs
    bool ticking = false;       // 已订阅动画心跳 (淡入 / 淡出中)
    bool textClipped = false;   /* 超过 kToastMaxLines 行 (或 fallback 下单词超宽), 被截断 */
    /* 测量时排好的版, 绘制直接用它 —— 测量和绘制各排一次可能折出不同行数。 */
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    float layoutMaxW = 0.0f;   /* layout 的排版宽度 (行在其中居中) */
    float textH = 0.0f;        /* 显示的文字高度 (超行数时截到 kToastMaxLines 行) */
    std::unique_ptr<ToastBitmap> bitmap;
};

} // namespace

struct OverlayService::Impl {
    enum class RequestType {
        CreateHostWindow,
        DestroyHostWindow,
        Invoke,
        ShowToast,
        DismissOwner,
        Stop
    };

    struct SyncResult {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        HWND hwnd = nullptr;
    };

    struct Request {
        RequestType type = RequestType::Stop;
        OverlayWindowHandler* handler = nullptr;
        OverlayWindowOptions options;
        SyncResult* sync = nullptr;
        std::function<void()> fn;
        HWND hwnd = nullptr;
        HWND owner = nullptr;
        std::wstring text;
        int durationMs = 0;
        int position = 0;
        int icon = 0;
        int anim = 0;
    };

    std::mutex mutex;
    std::condition_variable readyCv;
    std::queue<std::unique_ptr<Request>> requests;
    std::thread thread;
    DWORD threadId = 0;
    bool ready = false;
    bool stopping = false;

    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory;
    std::unordered_map<HWND, std::unique_ptr<ToastOverlay>> toasts;
    std::unordered_map<HWND, HWND> ownerToast;

    HWND CreateHostWindowSync(OverlayWindowHandler* handler,
                              const OverlayWindowOptions& options) {
        if (!handler) return nullptr;
        EnsureThread();
        if (GetCurrentThreadId() == threadId) {
            return CreateHostWindow(handler, options);
        }

        SyncResult sync;
        auto request = std::make_unique<Request>();
        request->type = RequestType::CreateHostWindow;
        request->handler = handler;
        request->options = options;
        request->sync = &sync;
        Post(std::move(request));

        std::unique_lock<std::mutex> lock(sync.mutex);
        sync.cv.wait(lock, [&sync]() { return sync.done; });
        return sync.hwnd;
    }

    void DestroyHostWindowSync(HWND hwnd) {
        if (!hwnd) return;
        EnsureThread();
        if (GetCurrentThreadId() == threadId) {
            DestroyHostWindow(hwnd);
            return;
        }

        SyncResult sync;
        auto request = std::make_unique<Request>();
        request->type = RequestType::DestroyHostWindow;
        request->hwnd = hwnd;
        request->sync = &sync;
        Post(std::move(request));

        std::unique_lock<std::mutex> lock(sync.mutex);
        sync.cv.wait(lock, [&sync]() { return sync.done; });
    }

    void InvokeSync(std::function<void()> fn) {
        if (!fn) return;
        EnsureThread();
        if (GetCurrentThreadId() == threadId) {
            fn();
            return;
        }

        SyncResult sync;
        auto request = std::make_unique<Request>();
        request->type = RequestType::Invoke;
        request->fn = std::move(fn);
        request->sync = &sync;
        Post(std::move(request));

        std::unique_lock<std::mutex> lock(sync.mutex);
        sync.cv.wait(lock, [&sync]() { return sync.done; });
    }

    /* 同 InvokeSync 走同一队列, 只是 sync 为空 (CompleteSync 对空指针直接返回)。 */
    void InvokeAsync(std::function<void()> fn) {
        if (!fn) return;
        EnsureThread();
        if (GetCurrentThreadId() == threadId) {
            fn();
            return;
        }
        auto request = std::make_unique<Request>();
        request->type = RequestType::Invoke;
        request->fn = std::move(fn);
        Post(std::move(request));
    }

    void EnsureThread() {
        std::unique_lock<std::mutex> lock(mutex);
        if (thread.joinable() && ready && !stopping) {
            return;
        }
        if (thread.joinable()) {
            lock.unlock();
            thread.join();
            lock.lock();
        }
        ready = false;
        stopping = false;
        thread = std::thread([this]() { ThreadMain(); });
        readyCv.wait(lock, [this]() { return ready; });
    }

    void Post(std::unique_ptr<Request> request) {
        EnsureThread();
        DWORD tid = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            tid = threadId;
            requests.push(std::move(request));
        }
        if (tid) {
            PostThreadMessageW(tid, kOverlayWakeMessage, 0, 0);
        }
    }

    void Stop() {
        std::thread oldThread;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!thread.joinable()) {
                return;
            }
        }

        auto request = std::make_unique<Request>();
        request->type = RequestType::Stop;
        Post(std::move(request));

        {
            std::lock_guard<std::mutex> lock(mutex);
            oldThread = std::move(thread);
        }
        if (oldThread.joinable()) {
            oldThread.join();
        }
    }

    void ThreadMain() {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        threadId = GetCurrentThreadId();
        MSG msg{};
        PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = ToastWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
        wc.lpszClassName = L"UiCore_OverlayToast";
        RegisterClassExW(&wc);

        WNDCLASSEXW hostClass{};
        hostClass.cbSize = sizeof(hostClass);
        hostClass.style = CS_HREDRAW | CS_VREDRAW;
        hostClass.lpfnWndProc = HostWndProc;
        hostClass.hInstance = GetModuleHandleW(nullptr);
        hostClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        hostClass.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
        hostClass.lpszClassName = L"UiCore_OverlayHost";
        RegisterClassExW(&hostClass);

        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory.GetAddressOf());
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(dwriteFactory.GetAddressOf()));

        {
            std::lock_guard<std::mutex> lock(mutex);
            ready = true;
        }
        readyCv.notify_all();

        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == kOverlayWakeMessage) {
                DrainRequests();
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        DestroyAllToasts();
        dwriteFactory.Reset();
        d2dFactory.Reset();
        {
            std::lock_guard<std::mutex> lock(mutex);
            ready = false;
            threadId = 0;
            stopping = false;
        }
        CoUninitialize();
    }

    void DrainRequests() {
        for (;;) {
            std::unique_ptr<Request> request;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (requests.empty()) break;
                request = std::move(requests.front());
                requests.pop();
            }
            if (!request) continue;
            switch (request->type) {
            case RequestType::CreateHostWindow: {
                HWND hwnd = CreateHostWindow(request->handler, request->options);
                CompleteSync(request->sync, hwnd);
                break;
            }
            case RequestType::DestroyHostWindow:
                DestroyHostWindow(request->hwnd);
                CompleteSync(request->sync, nullptr);
                break;
            case RequestType::Invoke:
                if (request->fn) {
                    request->fn();
                }
                CompleteSync(request->sync, nullptr);
                break;
            case RequestType::ShowToast:
                CreateToast(*request);
                break;
            case RequestType::DismissOwner:
                DestroyToastForOwner(request->owner);
                break;
            case RequestType::Stop:
                stopping = true;
                DestroyAllToasts();
                PostQuitMessage(0);
                break;
            }
        }
    }

    void CompleteSync(SyncResult* sync, HWND hwnd) {
        if (!sync) return;
        {
            std::lock_guard<std::mutex> lock(sync->mutex);
            sync->hwnd = hwnd;
            sync->done = true;
        }
        sync->cv.notify_one();
    }

    HWND CreateHostWindow(OverlayWindowHandler* handler,
                          const OverlayWindowOptions& options) {
        if (!handler) return nullptr;
        return CreateWindowExW(
            options.exStyle,
            L"UiCore_OverlayHost",
            options.title.empty() ? L"" : options.title.c_str(),
            options.style,
            options.x, options.y,
            std::max(1, options.width), std::max(1, options.height),
            options.owner, nullptr, GetModuleHandleW(nullptr), handler);
    }

    void DestroyHostWindow(HWND hwnd) {
        if (hwnd && IsWindow(hwnd)) {
            DestroyWindow(hwnd);
        }
    }

    void CreateToast(const Request& request) {
        if (!request.owner || !IsWindow(request.owner) || request.text.empty()) {
            return;
        }
        DestroyToastForOwner(request.owner);

        auto toast = std::make_unique<ToastOverlay>();
        toast->owner = request.owner;
        toast->text = request.text;
        toast->durationMs = std::max(1, request.durationMs);
        toast->position = request.position;
        toast->icon = request.icon;
        toast->anim = request.anim;
        toast->dpi = OverlayDpiForWindow(request.owner);
        toast->dpiScale = static_cast<float>(toast->dpi) / 96.0f;

        if (!MeasureToast(*toast) || !RenderToastBitmap(*toast)) {
            return;
        }

        const int widthPx = std::max(1, static_cast<int>(toast->widthDip * toast->dpiScale + 0.5f));
        const int heightPx = std::max(1, static_cast<int>(toast->heightDip * toast->dpiScale + 0.5f));
        const int startY = (toast->anim == 1)
                               ? toast->targetY
                               : toast->targetY + toast->slideRangePx;

        ToastOverlay* raw = toast.get();
        HWND hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
            L"UiCore_OverlayToast", L"", WS_POPUP,
            toast->targetX, startY, widthPx, heightPx,
            request.owner, nullptr, GetModuleHandleW(nullptr), raw);
        if (!hwnd) {
            return;
        }

        raw->hwnd = hwnd;
        raw->startTick = AnimNowMs();
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);

        ownerToast[request.owner] = hwnd;
        toasts[hwnd] = std::move(toast);
        UpdateToastFrame(*raw);
        ScheduleToastFrames(*raw);
    }

    /* 按所处阶段决定下一帧怎么来: 淡入 / 淡出订阅心跳, 停留期间退订 + 一次性定时器。 */
    static void ScheduleToastFrames(ToastOverlay& toast) {
        if (!toast.hwnd) return;
        const uint64_t elapsed = AnimNowMs() - toast.startTick;
        const uint64_t holdWake = static_cast<uint64_t>(kToastInMs + toast.durationMs) -
                                  std::min<uint64_t>(kToastHoldWakeLeadMs, toast.durationMs);
        const bool holding = elapsed >= static_cast<uint64_t>(kToastInMs) && elapsed < holdWake;
        if (holding) {
            if (toast.ticking) {
                FrameTicker::Instance().Unsubscribe(toast.hwnd);
                toast.ticking = false;
            }
            SetTimer(toast.hwnd, kToastHoldTimerId,
                     static_cast<UINT>(std::max<uint64_t>(holdWake - elapsed, USER_TIMER_MINIMUM)),
                     nullptr);
        } else if (!toast.ticking) {
            FrameTicker::Instance().Subscribe(toast.hwnd, kToastFrameMessage);
            toast.ticking = true;
        }
    }

    bool MeasureToast(ToastOverlay& toast) {
        RECT client{};
        if (!GetClientRect(toast.owner, &client)) {
            return false;
        }
        POINT origin{0, 0};
        ClientToScreen(toast.owner, &origin);
        const float clientW = std::max(1, static_cast<int>(client.right - client.left)) / toast.dpiScale;
        const float clientH = std::max(1, static_cast<int>(client.bottom - client.top)) / toast.dpiScale;

        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
        if (!dwriteFactory ||
            FAILED(dwriteFactory->CreateTextFormat(L"Microsoft YaHei UI", nullptr,
                                                   DWRITE_FONT_WEIGHT_NORMAL,
                                                   DWRITE_FONT_STYLE_NORMAL,
                                                   DWRITE_FONT_STRETCH_NORMAL,
                                                   theme::kFontSizeNormal,
                                                   L"zh-cn", format.GetAddressOf()))) {
            return false;
        }
        /* build 331: 超宽折行 (以前不换行, 长文案两头被裁)。EMERGENCY_BREAK 让超长的
         * 单词 / 路径也能断开 (Win 8.1+), 不支持时退回按词折行。 */
        if (FAILED(format->SetWordWrapping(DWRITE_WORD_WRAPPING_EMERGENCY_BREAK))) {
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        }
        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

        const float iconSize = theme::kFontSizeNormal + 2.0f;
        const float iconGap = (toast.icon > 0) ? 8.0f : 0.0f;
        const float iconSpace = (toast.icon > 0) ? (iconSize + iconGap) : 0.0f;
        const float padH = 20.0f;
        const float padV = 10.0f;
        const float maxTextW = std::max(120.0f, std::min(620.0f, clientW - padH * 2.0f - iconSpace - 40.0f));

        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        if (FAILED(dwriteFactory->CreateTextLayout(
                toast.text.c_str(), static_cast<UINT32>(toast.text.size()),
                format.Get(), maxTextW, 10000.0f, layout.GetAddressOf()))) {
            return false;
        }
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);

        /* 单行沿用旧口径 (含行尾空白); 折行后行尾空白不算宽度。 */
        const UINT32 lines = std::max<UINT32>(1, metrics.lineCount);
        const float measuredW = (lines == 1) ? metrics.widthIncludingTrailingWhitespace
                                             : metrics.width;
        const float lineH = metrics.height / static_cast<float>(lines);
        toast.textClipped = lines > kToastMaxLines || measuredW > maxTextW + 0.5f;
        const float textW = std::min(maxTextW, std::max(1.0f, measuredW));
        const float textH = std::max(theme::kFontSizeNormal,
                                     std::min(metrics.height, lineH * kToastMaxLines));
        toast.layout = layout;
        toast.layoutMaxW = maxTextW;
        toast.textH = textH;
        toast.widthDip = std::max(96.0f, textW + iconSpace + padH * 2.0f);
        toast.heightDip = std::max(iconSize, textH) + padV * 2.0f;

        const float xDip = (clientW - toast.widthDip) * 0.5f;
        float yDip = 50.0f;
        float hideOffsetDip = -(toast.heightDip + 60.0f);
        if (toast.position == 1) {
            yDip = (clientH - toast.heightDip) * 0.5f;
            hideOffsetDip = 0.0f;
        } else if (toast.position == 2) {
            yDip = clientH - toast.heightDip - 60.0f;
            hideOffsetDip = toast.heightDip + 60.0f;
        }

        toast.targetX = origin.x + static_cast<int>(xDip * toast.dpiScale);
        toast.targetY = origin.y + static_cast<int>(yDip * toast.dpiScale);
        toast.slideRangePx = static_cast<int>(hideOffsetDip * toast.dpiScale);
        return true;
    }

    bool RenderToastBitmap(ToastOverlay& toast) {
        const int widthPx = std::max(1, static_cast<int>(toast.widthDip * toast.dpiScale + 0.5f));
        const int heightPx = std::max(1, static_cast<int>(toast.heightDip * toast.dpiScale + 0.5f));

        auto bitmap = std::make_unique<ToastBitmap>();
        bitmap->width = widthPx;
        bitmap->height = heightPx;
        bitmap->dc = CreateCompatibleDC(nullptr);
        if (!bitmap->dc) {
            return false;
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = widthPx;
        bmi.bmiHeader.biHeight = -heightPx;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        bitmap->bitmap = CreateDIBSection(bitmap->dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap->bitmap || !bits) {
            return false;
        }
        bitmap->bits = bits;
        bitmap->oldBitmap = SelectObject(bitmap->dc, bitmap->bitmap);

        Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> target;
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            static_cast<FLOAT>(toast.dpi), static_cast<FLOAT>(toast.dpi));
        if (!d2dFactory || FAILED(d2dFactory->CreateDCRenderTarget(&props, target.GetAddressOf()))) {
            return false;
        }
        RECT rc{0, 0, widthPx, heightPx};
        if (FAILED(target->BindDC(bitmap->dc, &rc))) {
            return false;
        }

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush;
        target->CreateSolidColorBrush(D2D1::ColorF(0.15f, 0.15f, 0.18f, 0.92f), bgBrush.GetAddressOf());
        target->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.95f, 0.97f, 1.0f), textBrush.GetAddressOf());

        if (!toast.layout) {
            return false;
        }

        target->BeginDraw();
        target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

        const D2D1_RECT_F box = D2D1::RectF(0.0f, 0.0f, toast.widthDip, toast.heightDip);
        target->FillRoundedRectangle(D2D1::RoundedRect(box, 8.0f, 8.0f), bgBrush.Get());

        const float fontSize = theme::kFontSizeNormal;
        const float iconSize = fontSize + 2.0f;
        const float iconGap = (toast.icon > 0) ? 8.0f : 0.0f;
        const float iconSpace = (toast.icon > 0) ? (iconSize + iconGap) : 0.0f;
        const float padH = 20.0f;

        if (toast.icon > 0) {
            DrawIcon(*target.Get(), toast);
        }

        /* 用测量时那份排版画: 行在 layoutMaxW 宽的框里居中, 把这个框居中到文字区;
         * 竖直方向把 (最多 kToastMaxLines 行的) 文字块居中, 超出的行裁掉。 */
        const float areaL = padH + iconSpace;
        const float areaW = toast.widthDip - padH - areaL;
        const float textTop = (toast.heightDip - toast.textH) * 0.5f;
        if (toast.layout) {
            target->PushAxisAlignedClip(
                D2D1::RectF(areaL, textTop, toast.widthDip - padH, textTop + toast.textH),
                D2D1_ANTIALIAS_MODE_ALIASED);
            target->DrawTextLayout(
                D2D1::Point2F(areaL + (areaW - toast.layoutMaxW) * 0.5f, textTop),
                toast.layout.Get(), textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
            target->PopAxisAlignedClip();
        }

        HRESULT hr = target->EndDraw();
        if (FAILED(hr)) {
            return false;
        }

        toast.bitmap = std::move(bitmap);
        return true;
    }

    static void DrawIcon(ID2D1DCRenderTarget& target, const ToastOverlay& toast) {
        const float fontSize = theme::kFontSizeNormal;
        const float iconSize = fontSize + 2.0f;
        const float padH = 20.0f;
        const float ix = padH;
        const float iy = (toast.heightDip - iconSize) * 0.5f;
        const float cx = ix + iconSize * 0.5f;
        const float cy = iy + iconSize * 0.5f;
        const float r = iconSize * 0.5f;

        D2D1_COLOR_F color = D2D1::ColorF(0.3f, 0.85f, 0.4f, 1.0f);
        if (toast.icon == 2) {
            color = D2D1::ColorF(0.95f, 0.35f, 0.35f, 1.0f);
        } else if (toast.icon == 3) {
            color = D2D1::ColorF(1.0f, 0.82f, 0.2f, 1.0f);
        }

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
        target.CreateSolidColorBrush(color, brush.GetAddressOf());
        if (!brush) return;

        if (toast.icon == 1) {
            target.DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), brush.Get(), 1.5f);
            target.DrawLine(D2D1::Point2F(cx - r * 0.32f, cy + r * 0.05f),
                            D2D1::Point2F(cx - r * 0.02f, cy + r * 0.35f),
                            brush.Get(), 2.0f);
            target.DrawLine(D2D1::Point2F(cx - r * 0.02f, cy + r * 0.35f),
                            D2D1::Point2F(cx + r * 0.42f, cy - r * 0.26f),
                            brush.Get(), 2.0f);
        } else if (toast.icon == 2) {
            target.DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), brush.Get(), 1.5f);
            const float d = r * 0.34f;
            target.DrawLine(D2D1::Point2F(cx - d, cy - d), D2D1::Point2F(cx + d, cy + d),
                            brush.Get(), 2.0f);
            target.DrawLine(D2D1::Point2F(cx + d, cy - d), D2D1::Point2F(cx - d, cy + d),
                            brush.Get(), 2.0f);
        } else if (toast.icon == 3) {
            target.DrawLine(D2D1::Point2F(cx, cy - r * 0.55f),
                            D2D1::Point2F(cx - r * 0.55f, cy + r * 0.42f),
                            brush.Get(), 1.8f);
            target.DrawLine(D2D1::Point2F(cx - r * 0.55f, cy + r * 0.42f),
                            D2D1::Point2F(cx + r * 0.55f, cy + r * 0.42f),
                            brush.Get(), 1.8f);
            target.DrawLine(D2D1::Point2F(cx + r * 0.55f, cy + r * 0.42f),
                            D2D1::Point2F(cx, cy - r * 0.55f),
                            brush.Get(), 1.8f);
            target.DrawLine(D2D1::Point2F(cx, cy - r * 0.15f),
                            D2D1::Point2F(cx, cy + r * 0.16f),
                            brush.Get(), 2.0f);
            target.DrawLine(D2D1::Point2F(cx, cy + r * 0.30f),
                            D2D1::Point2F(cx + 0.1f, cy + r * 0.31f),
                            brush.Get(), 2.0f);
        }
    }

    /* 某一时刻的帧: 淡入 / 停留 / 淡出。UpdateToastFrame 与调试查询共用。 */
    struct ToastFrame {
        bool expired = false;
        uint64_t elapsed = 0;
        BYTE alpha = 255;
        int y = 0;
    };

    static ToastFrame ComputeToastFrame(const ToastOverlay& toast, uint64_t now) {
        ToastFrame f;
        f.elapsed = (toast.startTick != 0 && now >= toast.startTick)
                        ? (now - toast.startTick) : 0;
        const uint64_t inEnd = kToastInMs;
        const uint64_t holdEnd = inEnd + static_cast<uint64_t>(toast.durationMs);
        const uint64_t outEnd = holdEnd + kToastOutMs;

        float alpha = 1.0f;
        float slide = 1.0f;
        if (f.elapsed < inEnd) {
            const float t = EaseOutCubic(static_cast<float>(f.elapsed) / static_cast<float>(kToastInMs));
            slide = t;
            alpha = (toast.anim == 1) ? t : 1.0f;
        } else if (f.elapsed < holdEnd) {
            slide = 1.0f;
            alpha = 1.0f;
        } else if (f.elapsed < outEnd) {
            const float t = EaseOutCubic(static_cast<float>(f.elapsed - holdEnd) / static_cast<float>(kToastOutMs));
            slide = 1.0f - t;
            alpha = 1.0f - t;
        } else {
            f.expired = true;
            return f;
        }
        f.y = (toast.anim == 1)
                  ? toast.targetY
                  : toast.targetY + static_cast<int>(toast.slideRangePx * (1.0f - slide));
        f.alpha = static_cast<BYTE>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
        return f;
    }

    void UpdateToastFrame(ToastOverlay& toast) {
        if (!toast.hwnd || !toast.bitmap || !toast.bitmap->dc) {
            return;
        }

        const ToastFrame frame = ComputeToastFrame(toast, AnimNowMs());
        TraceEvent("overlay", "toast_frame",
                   {TraceI64("elapsed_ms", static_cast<int64_t>(frame.elapsed)),
                    TraceBool("expired", frame.expired)});
        if (frame.expired) {
            DestroyToastByHwnd(toast.hwnd);
            return;
        }
        const int y = frame.y;
        const BYTE alphaByte = frame.alpha;
        POINT dst{toast.targetX, y};
        POINT src{0, 0};
        SIZE size{toast.bitmap->width, toast.bitmap->height};
        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.SourceConstantAlpha = alphaByte;
        blend.AlphaFormat = AC_SRC_ALPHA;
        UpdateLayeredWindow(toast.hwnd, nullptr, &dst, &size, toast.bitmap->dc, &src,
                            0, &blend, ULW_ALPHA);
    }

    /* ---- 调试查询 (overlay 线程上调用) ---- */

    ToastOverlay* FindToast(HWND owner) {
        auto it = ownerToast.find(owner);
        if (it == ownerToast.end()) return nullptr;
        auto t = toasts.find(it->second);
        return (t == toasts.end()) ? nullptr : t->second.get();
    }

    static void AppendJsonString(std::string& out, const std::wstring& w) {
        std::string u8;
        if (!w.empty()) {
            const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                              nullptr, 0, nullptr, nullptr);
            u8.resize(static_cast<size_t>(n > 0 ? n : 0));
            if (n > 0) {
                WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                    u8.data(), n, nullptr, nullptr);
            }
        }
        out += '"';
        for (unsigned char c : u8) {
            switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
            }
        }
        out += '"';
    }

    std::string ToastInfoJson(HWND owner) {
        ToastOverlay* toast = FindToast(owner);
        if (!toast || !toast->bitmap) return "{\"visible\":false}";
        const ToastFrame f = ComputeToastFrame(*toast, AnimNowMs());
        if (f.expired) return "{\"visible\":false}";
        POINT origin{0, 0};
        ClientToScreen(owner, &origin);
        std::string o = "{\"visible\":true,\"text\":";
        AppendJsonString(o, toast->text);
        char buf[512];
        std::snprintf(buf, sizeof(buf),
                      ",\"icon\":%d,\"position\":%d,\"anim\":%d,\"duration_ms\":%d,"
                      "\"elapsed_ms\":%llu,\"alpha\":%d,\"x\":%d,\"y\":%d,"
                      "\"width\":%d,\"height\":%d,\"dpi\":%u,\"text_clipped\":%s}",
                      toast->icon, toast->position, toast->anim, toast->durationMs,
                      static_cast<unsigned long long>(f.elapsed), static_cast<int>(f.alpha),
                      toast->targetX - origin.x, f.y - origin.y,
                      toast->bitmap->width, toast->bitmap->height, toast->dpi,
                      toast->textClipped ? "true" : "false");
        o += buf;
        return o;
    }

    /* 预乘 BGRA → 直通 BGRA (PNG 存的是非预乘)。 */
    bool CopyToastPixels(HWND owner, int& w, int& h, std::vector<uint8_t>& px) {
        ToastOverlay* toast = FindToast(owner);
        if (!toast || !toast->bitmap || !toast->bitmap->bits) return false;
        if (ComputeToastFrame(*toast, AnimNowMs()).expired) return false;
        GdiFlush();
        w = toast->bitmap->width;
        h = toast->bitmap->height;
        const auto* src = static_cast<const uint8_t*>(toast->bitmap->bits);
        px.assign(src, src + static_cast<size_t>(w) * h * 4);
        for (size_t i = 0; i < px.size(); i += 4) {
            const uint8_t a = px[i + 3];
            if (a == 0 || a == 255) continue;
            for (int c = 0; c < 3; ++c) {
                px[i + c] = static_cast<uint8_t>(std::min(255, (px[i + c] * 255 + a / 2) / a));
            }
        }
        return true;
    }

    void DestroyToastForOwner(HWND owner) {
        auto it = ownerToast.find(owner);
        if (it == ownerToast.end()) {
            return;
        }
        DestroyToastByHwnd(it->second);
    }

    void DestroyToastByHwnd(HWND hwnd) {
        auto it = toasts.find(hwnd);
        if (it == toasts.end()) {
            return;
        }
        ToastOverlay* toast = it->second.get();
        if (toast->ticking) {
            /* 显式退订, 不等 PostMessage 失败自动清理 (HWND 可能被系统复用)。 */
            FrameTicker::Instance().Forget(toast->hwnd);
            toast->ticking = false;
        }
        if (toast->hwnd && IsWindow(toast->hwnd)) {
            KillTimer(toast->hwnd, kToastHoldTimerId);
            DestroyWindow(toast->hwnd);
        }
        ownerToast.erase(toast->owner);
        toasts.erase(it);
    }

    void DestroyAllToasts() {
        while (!toasts.empty()) {
            DestroyToastByHwnd(toasts.begin()->first);
        }
        ownerToast.clear();
    }

    static LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        OverlayWindowHandler* handler = nullptr;
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            handler = static_cast<OverlayWindowHandler*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(handler));
        } else {
            handler = reinterpret_cast<OverlayWindowHandler*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (handler) {
            bool handled = false;
            LRESULT result = handler->HandleOverlayMessage(hwnd, msg, wParam, lParam, handled);
            if (handled) {
                return result;
            }
        }

        if (msg == WM_NCDESTROY) {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    static LRESULT CALLBACK ToastWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        ToastOverlay* toast = nullptr;
        if (msg == WM_NCCREATE) {
            auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            toast = static_cast<ToastOverlay*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(toast));
        } else {
            toast = reinterpret_cast<ToastOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        switch (msg) {
        case kToastFrameMessage:
            FrameTicker::Instance().Ack(hwnd);
            if (toast) {
                /* 可能就此到期销毁 (toast 失效), 先记下 hwnd 再查是否还在。 */
                auto* impl = OverlayService::Instance().impl_;
                impl->UpdateToastFrame(*toast);
                auto it = impl->toasts.find(hwnd);
                if (it != impl->toasts.end()) ScheduleToastFrames(*it->second);
            }
            return 0;
        case WM_TIMER:
            if (wParam == kToastHoldTimerId && toast) {
                KillTimer(hwnd, kToastHoldTimerId);   // 一次性: 停留快结束, 接上心跳淡出
                ScheduleToastFrames(*toast);
                return 0;
            }
            break;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_NCCALCSIZE:
            if (wParam) return 0;
            break;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            break;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
};

OverlayService& OverlayService::Instance() {
    static OverlayService service;
    return service;
}

OverlayService::OverlayService()
    : impl_(new Impl()) {
}

OverlayService::~OverlayService() {
    Stop();
    delete impl_;
}

HWND OverlayService::CreateHostWindowSync(OverlayWindowHandler* handler,
                                          const OverlayWindowOptions& options) {
    return impl_->CreateHostWindowSync(handler, options);
}

void OverlayService::DestroyHostWindowSync(HWND hwnd) {
    impl_->DestroyHostWindowSync(hwnd);
}

void OverlayService::InvokeSync(std::function<void()> fn) {
    impl_->InvokeSync(std::move(fn));
}

void OverlayService::InvokeAsync(std::function<void()> fn) {
    impl_->InvokeAsync(std::move(fn));
}

void OverlayService::ShowToast(HWND owner, const std::wstring& text, int durationMs,
                               int position, int icon, int anim) {
    auto request = std::make_unique<Impl::Request>();
    request->type = Impl::RequestType::ShowToast;
    request->owner = owner;
    request->text = text;
    request->durationMs = durationMs;
    request->position = position;
    request->icon = icon;
    request->anim = anim;
    impl_->Post(std::move(request));
}

void OverlayService::DismissOwner(HWND owner) {
    auto request = std::make_unique<Impl::Request>();
    request->type = Impl::RequestType::DismissOwner;
    request->owner = owner;
    impl_->Post(std::move(request));
}

std::string OverlayService::DebugToastInfoJson(HWND owner) {
    std::string out = "{\"visible\":false}";
    if (!owner) return out;
    /* InvokeSync 与 ShowToast 同队列: 之前投的 ShowToast 此刻已经建好窗口。 */
    impl_->InvokeSync([&]() { out = impl_->ToastInfoJson(owner); });
    return out;
}

int OverlayService::DebugScreenshotToast(HWND owner, const wchar_t* outPath) {
    if (!owner || !outPath) return -1;
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    bool ok = false;
    impl_->InvokeSync([&]() { ok = impl_->CopyToastPixels(owner, w, h, px); });
    if (!ok || w <= 0 || h <= 0) return -1;

    /* 编码在调用线程做 (不占 overlay 线程)。调用线程可能已是 STA, 那就沿用。 */
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninit = SUCCEEDED(co);
    int rc = 0;
    {
        Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
        Microsoft::WRL::ComPtr<IWICStream> stream;
        Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
        Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        const UINT stride = static_cast<UINT>(w) * 4;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(wic.GetAddressOf())))) rc = -2;
        else if (FAILED(wic->CreateStream(stream.GetAddressOf()))) rc = -3;
        else if (FAILED(stream->InitializeFromFilename(outPath, GENERIC_WRITE))) rc = -4;
        else if (FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr,
                                           encoder.GetAddressOf()))) rc = -5;
        else if (FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) rc = -6;
        else if (FAILED(encoder->CreateNewFrame(frame.GetAddressOf(), nullptr))) rc = -7;
        else if (FAILED(frame->Initialize(nullptr))) rc = -8;
        else if (FAILED(frame->SetSize(static_cast<UINT>(w), static_cast<UINT>(h)))) rc = -9;
        else if (FAILED(frame->SetPixelFormat(&fmt))) rc = -10;
        else if (FAILED(frame->WritePixels(static_cast<UINT>(h), stride,
                                           stride * static_cast<UINT>(h), px.data()))) rc = -11;
        else if (FAILED(frame->Commit())) rc = -12;
        else if (FAILED(encoder->Commit())) rc = -13;
    }
    if (uninit) CoUninitialize();
    return rc;
}

void OverlayService::Stop() {
    impl_->Stop();
}

} // namespace ui
