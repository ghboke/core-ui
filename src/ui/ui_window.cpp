#include "ui_window.h"
#include "ui_context.h"
#include "controls.h"
#include "image_view_plus.h"
#include "gh_img_view.h"
#include "ocr_img_view.h"
#include "theme.h"
#include "animation.h"
#include "debug_trace.h"
#include "overlay_service.h"
#include "render_thread.h"
#include <windowsx.h>
#include <imm.h>       /* IME composition/candidate window positioning */
#include <dwmapi.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <wincodec.h>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <unordered_set>
#include <utility>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shlwapi.lib")

#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0A00
static inline UINT GetDpiForWindow(HWND hwnd) {
    HMODULE hModule = LoadLibraryW(L"user32.dll");
    if (hModule) {
        typedef UINT(WINAPI* PFN_GetDpiForWindow)(HWND);
        PFN_GetDpiForWindow pfn = (PFN_GetDpiForWindow)GetProcAddress(hModule, "GetDpiForWindow");
        if (pfn) {
            UINT dpi = pfn(hwnd);
            FreeLibrary(hModule);
            return dpi;
        }
        FreeLibrary(hModule);
    }
    HDC hdc = GetDC(hwnd);
    UINT dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(hwnd, hdc);
    return dpi ? dpi : 96;
}
#endif

#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0A00
static inline int GetSystemMetricsForDpi(int index, UINT dpi) {
    HMODULE hModule = LoadLibraryW(L"user32.dll");
    if (hModule) {
        typedef int(WINAPI* PFN_GetSystemMetricsForDpi)(int, UINT);
        PFN_GetSystemMetricsForDpi pfn = (PFN_GetSystemMetricsForDpi)GetProcAddress(hModule, "GetSystemMetricsForDpi");
        if (pfn) {
            int value = pfn(index, dpi);
            FreeLibrary(hModule);
            return value;
        }
        FreeLibrary(hModule);
    }
    return GetSystemMetrics(index);
}
#endif

namespace ui {

// 放在最上方供 WM_APP+120 case 引用（InvokeSync 的 SendMessage 携带的载荷）
struct UiInvokeReq {
    void (*fn)(void*);
    void* ud;
};

bool UiWindowImpl::classRegistered_ = false;

// Defined above UiWindowImpl::OnMouseMove; declared here because WM_RBUTTONUP
// (@contextmenu) dispatches earlier in the file.
static void StampDomMouseState(MouseEvent& e, int button = -1);

#ifndef DWMWA_TRANSITIONS_FORCEDISABLED
#define DWMWA_TRANSITIONS_FORCEDISABLED 3
#endif

namespace {
constexpr UINT kMsgStartupReveal = WM_APP + 101;
constexpr UINT kMsgRenderFrame = WM_APP + 130;
constexpr UINT kMsgRenderDeviceLost = kUiCoreRenderDeviceLostMessage;
constexpr UINT_PTR kCaretBlinkTimerId = 0xCA11;

class ActiveMeasureContextScope {
public:
    ActiveMeasureContextScope(MeasureContext& measure, Renderer& renderer)
        : previous_(g_activeMeasureContext) {
        measure.BindRenderer(&renderer);
        g_activeMeasureContext = &measure;
    }

    ~ActiveMeasureContextScope() {
        g_activeMeasureContext = previous_;
    }

private:
    MeasureContext* previous_ = nullptr;
};

constexpr UINT_PTR kToggleAnimTimerId = 0xCA12;
constexpr UINT_PTR kWindowOpenAnimTimerId = 0xCA13;
constexpr UINT_PTR kWindowCloseAnimTimerId = 0xCA14;
constexpr UINT kToggleAnimIntervalMs = 16;
constexpr UINT kWindowAnimIntervalMs = 16;

constexpr float kPopPeakScale = 1.02f;       // 关闭动画微弹峰值
constexpr float kClosePeakPhase = 0.18f;
constexpr float kCloseEndScale = 0.88f;
constexpr float kCloseFadeStart = 0.40f;
constexpr int   kOpenSlideOffset = 12;       // 开场上滑偏移（像素）
constexpr LONG  kCanvasMaxTrackPx = 14000;

static void EnsureLayeredForAlpha(HWND hwnd) {
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_LAYERED) == 0) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
    }
}

static void ClearLayeredAfterAlpha(HWND hwnd) {
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_LAYERED) == 0) return;

    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                 SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static float Clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static float EaseOutCubic(float t) {
    float p = Clamp01(t);
    return 1.0f - std::pow(1.0f - p, 3.0f);
}

static float EaseInCubic(float t) {
    float p = Clamp01(t);
    return p * p * p;
}

static float PopBounceScale(float t, float startScale, float peakScale, float endScale, float peakPhase) {
    float p = Clamp01(t);
    float phase = Clamp01(peakPhase);
    if (phase <= 0.0f) return endScale;
    if (phase >= 1.0f) return startScale;

    if (p < phase) {
        float local = EaseOutCubic(p / phase);
        return startScale + (peakScale - startScale) * local;
    }

    float local = EaseOutCubic((p - phase) / (1.0f - phase));
    return peakScale + (endScale - peakScale) * local;
}

static void SetWindowScaleAtCenter(HWND hwnd, float centerX, float centerY,
                                   float targetWidth, float targetHeight, float scale) {
    float currentWidth = targetWidth * scale;
    float currentHeight = targetHeight * scale;
    int currentX = (int)(centerX - currentWidth / 2.0f);
    int currentY = (int)(centerY - currentHeight / 2.0f);
    SetWindowPos(hwnd, nullptr, currentX, currentY, (int)currentWidth, (int)currentHeight,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}
}

// ---- Helper: tree traversal ----
static void ForEachWidget(Widget* w, const std::function<void(Widget*)>& fn) {
    fn(w);
    for (auto& c : w->Children()) ForEachWidget(c.get(), fn);
}

UiWindowImpl::UiWindowImpl() = default;
UiWindowImpl::~UiWindowImpl() {
    /* toast 叠加窗 owner = hwnd_, DestroyWindow(hwnd_) 会连带销毁它, 但仍要
     * 显式 KillTimer + timeEndPeriod 配对 (DestroyToast 负责), 防泄漏时钟精度. */
    DestroyToast();
    UnregisterRenderWindow();
    if (hwnd_) DestroyWindow(hwnd_);
    if (hIcon_) DestroyIcon(hIcon_);
}

bool UiWindowImpl::RegisterWindowClass() {
    if (classRegistered_) return true;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"UiCore_Window";
    if (!RegisterClassExW(&wc)) return false;

    classRegistered_ = true;
    return true;
}

bool UiWindowImpl::Create(const wchar_t* title, int width, int height,
                           bool borderless, bool resizable, bool acceptFiles,
                           int x, int y, bool toolWindow, HWND ownerHwnd) {
    if (!RegisterWindowClass()) return false;

    borderless_ = borderless;
    resizable_ = resizable;
    toolWindow_ = toolWindow;
    title_ = title ? title : L"";
    configWidth_ = width;
    configHeight_ = height;

    /* L174: 去掉 WS_EX_COMPOSITED —— core-ui 是单 HWND + D2D 自绘, 无子窗口,
     * 该 flag (给子窗自下而上双缓冲 alpha 用) 在此零收益, 却让 DWM 每次合成多走
     * 一层重定向双缓冲, 拖动/缩放时平添开销。WS_EX_LAYERED 仍单独保留给开场动画/
     * 透明路径 (走 SetLayeredWindowAttributes), 不受影响。 */
    DWORD exStyle = 0;
    if (toolWindow) exStyle |= WS_EX_TOOLWINDOW;
    /* Build 65+ (L14): owner 窗不上 Alt+Tab / 不单独 taskbar 项. owned 顶级窗
     * 用 WS_EX_APPWINDOW 跟 owner 语义冲突 (强制出现在 taskbar), 撤掉. */
    else if (!ownerHwnd) exStyle |= WS_EX_APPWINDOW;
    if (acceptFiles) exStyle |= WS_EX_ACCEPTFILES;

    DWORD style;
    if (borderless) {
        style = WS_POPUP | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;
        if (resizable) style |= WS_THICKFRAME;
    } else {
        style = WS_OVERLAPPEDWINDOW;
        if (!resizable) style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }

    // Width / height 是 DIP, 按系统 DPI scale 到 physical pixels.
    // x / y 是 screen px (Win32 惯例, 跟 SetWindowRect / SetWindowPosition
    // 一致, build 94+ L23). 持久化 DIP-stable 位置的应用用 ui_window_dpi()
    // 拿 DPI 自己 MulDiv (老 build 把 DIP 隐藏在 getter / create 里, 跟
    // setter 不自洽, L23 修).
    UINT sysDpi = 96;
    {
        HDC hdc = GetDC(nullptr);
        if (hdc) { sysDpi = (UINT)GetDeviceCaps(hdc, LOGPIXELSX); ReleaseDC(nullptr, hdc); }
    }
    int physW = MulDiv(width,  (int)sysDpi, 96);
    int physH = MulDiv(height, (int)sysDpi, 96);

    int winX, winY;
    if (x != CW_USEDEFAULT && y != CW_USEDEFAULT) {
        winX = x;
        winY = y;
    } else {
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        winX = (screenW - physW) / 2;
        winY = (screenH - physH) / 2;
    }

    windowTargetWidth_ = (float)physW;
    windowTargetHeight_ = (float)physH;
    windowTargetX_ = winX;
    windowTargetY_ = winY;

    /* 不需要开场动画时不加 WS_EX_LAYERED，避免首次 ShowWindow 500ms+ 延迟 */
    if (!skipOpenAnimation_)
        exStyle |= WS_EX_LAYERED;

    hwnd_ = CreateWindowExW(exStyle, L"UiCore_Window", title_.c_str(), style,
                            winX, winY, physW, physH,
                            ownerHwnd, nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd_) return false;
    ++hwndGeneration_;

    if (!skipOpenAnimation_) {
        SetLayeredWindowAttributes(hwnd_, 0, 0, LWA_ALPHA);
    }
    ShowWindow(hwnd_, SW_HIDE);

    BOOL disableTransitions = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_TRANSITIONS_FORCEDISABLED,
                          &disableTransitions, sizeof(disableTransitions));

    dpi_ = GetDpiForWindow(hwnd_);
    dpiScale_ = (float)dpi_ / 96.0f;
    RefreshClientSizeCache();

    if (borderless) {
        MARGINS margins = {0, 0, 1, 0};
        DwmExtendFrameIntoClientArea(hwnd_, &margins);
    }

    auto& ctx = GetContext();
    if (!renderer_.Init(ctx.D2DFactory(), ctx.DWFactory(), ctx.WICFactory())) return false;
    // CreateRenderTarget deferred to Show() — ensures client rect is final

    return true;
}

void UiWindowImpl::RegisterRenderWindow(uint64_t id) {
    windowId = id;
    if (!hwnd_ || id == 0 || hwndGeneration_ == 0) return;
    renderWindowId_ = RenderWindowId{id, hwndGeneration_};
    renderThreadPresentActive_ = false;
    RenderThread::Instance().Start();
    RenderThread::Instance().RegisterWindow(renderWindowId_, hwnd_);
}

void UiWindowImpl::UnregisterRenderWindow() {
    if (!renderWindowId_.IsValid()) return;
    RenderThread::Instance().UnregisterWindow(renderWindowId_);
    renderWindowId_ = {};
    renderThreadPresentActive_ = false;
}

void UiWindowImpl::SetIconFromPixels(const uint8_t* rgba, int w, int h) {
    if (!hwnd_ || !rgba || w <= 0 || h <= 0) return;

    // Create DIB section with BGRA pixel data (Windows expects BGRA)
    BITMAPV5HEADER bi = {};
    bi.bV5Size        = sizeof(bi);
    bi.bV5Width       = w;
    bi.bV5Height      = -h;  // top-down
    bi.bV5Planes      = 1;
    bi.bV5BitCount    = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask     = 0x00FF0000;
    bi.bV5GreenMask   = 0x0000FF00;
    bi.bV5BlueMask    = 0x000000FF;
    bi.bV5AlphaMask   = 0xFF000000;

    HDC dc = GetDC(nullptr);
    uint8_t* bits = nullptr;
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS,
                                     (void**)&bits, nullptr, 0);
    if (!color || !bits) { ReleaseDC(nullptr, dc); return; }

    // Convert RGBA → BGRA with premultiplied alpha
    for (int i = 0; i < w * h; i++) {
        uint8_t r = rgba[i*4+0], g = rgba[i*4+1], b = rgba[i*4+2], a = rgba[i*4+3];
        bits[i*4+0] = (uint8_t)(b * a / 255);  // B
        bits[i*4+1] = (uint8_t)(g * a / 255);  // G
        bits[i*4+2] = (uint8_t)(r * a / 255);  // R
        bits[i*4+3] = a;                        // A
    }

    HBITMAP mask = CreateBitmap(w, h, 1, 1, nullptr);
    ICONINFO ii = {};
    ii.fIcon    = TRUE;
    ii.hbmMask  = mask;
    ii.hbmColor = color;
    HICON icon = CreateIconIndirect(&ii);

    DeleteObject(color);
    DeleteObject(mask);
    ReleaseDC(nullptr, dc);

    if (icon) {
        if (hIcon_) DestroyIcon(hIcon_);
        hIcon_ = icon;
        SendMessage(hwnd_, WM_SETICON, ICON_BIG,   (LPARAM)hIcon_);
        SendMessage(hwnd_, WM_SETICON, ICON_SMALL,  (LPARAM)hIcon_);
    }
}

/* L101: 最大化态客户区 = 显示器工作区 (与 OnGetMinMaxInfo 把 ptMaxSize 限到
 * rcWork 一致)。Show / PrepareRT 在 start_maximized hint 下用它把隐藏窗口预置到
 * 最终最大化尺寸, 避免首帧按常规尺寸布局/fit 再被 SW_SHOWMAXIMIZED resize (内容
 * "先常规一帧再放大")。 */
static bool MaximizedWorkRect(HWND hwnd, RECT& out) {
    MONITORINFO mi{ sizeof(mi) };
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
        return false;
    out = mi.rcWork;
    return true;
}

void UiWindowImpl::Show() {
    if (!hwnd_) return;

    /* 外部可能先 ShowWindow(SW_MAXIMIZE) 了 —— 这里检测一次，
     * 如果已经是最大化，跳过 SetWindowPos 写死尺寸的路径 +
     * 跳过 slide 动画（动画用 SW_SHOWNOACTIVATE 会把最大化态覆盖掉）。
     * Build 105+ (L25): UiWindowConfig.start_maximized 走同一分支, 让
     * caller 持久化"最大化关→最大化开"不用自己 ShowWindow(SW_MAXIMIZE)
     * 撞 lib 的 layered fade-in 时序. */
    bool alreadyZoomed = IsZoomed(hwnd_) != 0;
    bool preMaximized  = alreadyZoomed || startMaximizedPending_;
    /* L101: hint 触发(尚未 zoom) → 下面要先把窗口预置到最大化尺寸再 OnPaint;
     * 已 IsZoomed(外部 SW_MAXIMIZE) → 保持原 SWP_NOSIZE 不动尺寸。 */
    bool maxFromHint   = startMaximizedPending_ && !alreadyZoomed;
    startMaximizedPending_ = false;

    /* 从当前窗口位置同步 target（外部可能已通过 SetWindowPos 改了位置） */
    {
        RECT wr; GetWindowRect(hwnd_, &wr);
        windowTargetX_ = wr.left;
        windowTargetY_ = wr.top;
        windowTargetWidth_ = (float)(wr.right - wr.left);
        windowTargetHeight_ = (float)(wr.bottom - wr.top);
    }

    BOOL disableTransitions = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_TRANSITIONS_FORCEDISABLED,
                          &disableTransitions, sizeof(disableTransitions));

    EnsureLayeredForAlpha(hwnd_);

    startupRevealPending_ = false;
    startupRevealPosted_ = false;

    if (preMaximized) {
        /* 最大化路径：不走 slide 动画。
         * - 已 IsZoomed(外部已 SW_MAXIMIZE): 只触发 NCCALCSIZE 让 borderless 客户区
         *   生效, 不改位置/尺寸。
         * - L101 start_maximized hint(尚未 zoom): 先把窗口预置到最大化客户区(=工作区),
         *   让下面 OnPaint 的首帧即最大化尺寸, 不留常规尺寸帧。 */
        RECT wr;
        if (maxFromHint && MaximizedWorkRect(hwnd_, wr)) {
            SetWindowPos(hwnd_, nullptr, wr.left, wr.top,
                         wr.right - wr.left, wr.bottom - wr.top,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        } else {
            SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        RefreshClientSizeCache();
        LayoutRoot();
        SubmitRenderThreadFrameNow(FrameReason::Startup, PresentPolicy::Immediate);
        ValidateRect(hwnd_, nullptr);
        /* 直接以最大化态显示，不走 slide 动画 */
        SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
        ShowWindow(hwnd_, SW_SHOWMAXIMIZED);
        ClearLayeredAfterAlpha(hwnd_);
        if (!toolWindow_) SetForegroundWindow(hwnd_);
        return;
    }

    // Force WM_NCCALCSIZE so client rect reflects our borderless override
    SetWindowPos(hwnd_, nullptr, windowTargetX_, windowTargetY_,
                 (int)windowTargetWidth_, (int)windowTargetHeight_,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    RefreshClientSizeCache();

    LayoutRoot();
    SubmitRenderThreadFrameNow(FrameReason::Startup, PresentPolicy::Immediate);
    ValidateRect(hwnd_, nullptr);

    StartWindowOpenAnimation();
}

void UiWindowImpl::PrepareRT() {
    if (!hwnd_) return;

    BOOL disableTransitions = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_TRANSITIONS_FORCEDISABLED,
                          &disableTransitions, sizeof(disableTransitions));

    LONG_PTR exStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    if (exStyle & WS_EX_LAYERED)
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);

    /* L101: start_maximized hint 命中时, prepare 阶段就把隐藏窗口预置到最大化
     * 客户区(=工作区, 同 OnGetMinMaxInfo)。这样 caller 在 show 前做的布局/图片
     * fit 就按最大化尺寸算; 随后 ShowImmediate 的 SW_SHOWMAXIMIZED 是同尺寸,
     * 无 reflow、无"先常规 fit 一帧再放大"。未命中走原常规尺寸路径。 */
    RECT target;
    if (startMaximizedPending_ && MaximizedWorkRect(hwnd_, target)) {
        /* L101/L102/L103 + 首帧全屏 + 无闪: 真最大化, 但全程**屏幕上不可见**。
         * 窗口此刻在创建时的常规尺寸, 直接 SW_SHOWMAXIMIZED 会把还没建 RT/绘内容的
         * 窗口短暂合成一帧出来(黑底 + 顶部最大化窗自带的 1px 白边线)= 用户看到的白条闪。
         * 故先 WS_EX_LAYERED + alpha 0 让窗口透明, 再 SW_SHOWMAXIMIZED:
         *   - maximize 把 rcNormalPosition 记成常规创建尺寸 → 拖标题/双击还原到常规窗口(L102);
         *   - client → 工作区 → caller 图片 fit 按最大化算(L101 无"先小后大");
         *   - 全程 alpha 0, 屏幕上完全不可见 → 无黑底/白边闪。
         * 窗口保持 shown+透明, ShowImmediate 绘完内容后一次 alpha→255 揭示 = 首帧即全屏
         * +有内容, 无任何闪/白边/先小后大。不再 SetWindowPos(已最大化在工作区)。 */
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE,
                          GetWindowLongPtrW(hwnd_, GWL_EXSTYLE) | WS_EX_LAYERED);
        SetLayeredWindowAttributes(hwnd_, 0, 0, LWA_ALPHA);   /* alpha 0 = 不可见 */
        maximized_ = true;
        if (borderless_) {
            MARGINS mz{0, 0, 0, 0};
            DwmExtendFrameIntoClientArea(hwnd_, &mz);
        }
        ShowWindow(hwnd_, SW_SHOWMAXIMIZED);   /* 透明状态下最大化, 屏上不可见 */
        framePrepared_ = true;
        GetWindowRect(hwnd_, &target);
        windowTargetX_      = target.left;
        windowTargetY_      = target.top;
        windowTargetWidth_  = (float)(target.right - target.left);
        windowTargetHeight_ = (float)(target.bottom - target.top);
    } else {
        GetWindowRect(hwnd_, &target);
        windowTargetX_      = target.left;
        windowTargetY_      = target.top;
        windowTargetWidth_  = (float)(target.right - target.left);
        windowTargetHeight_ = (float)(target.bottom - target.top);
        SetWindowPos(hwnd_, nullptr, windowTargetX_, windowTargetY_,
                     (int)windowTargetWidth_, (int)windowTargetHeight_,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        framePrepared_ = true;
    }

    RefreshClientSizeCache();
    LayoutRoot();
}

void UiWindowImpl::ShowImmediate(bool activate) {
    if (!hwnd_) return;

    /* Build 105+ (L25): start_maximized hint 走 SW_SHOWMAXIMIZED, 首帧
     * 即最大化态. argv 启动 (文件关联) + 上次最大化关闭场景下用. */
    bool startMax = startMaximizedPending_;
    startMaximizedPending_ = false;

    if (!framePrepared_) {
        /* 未经 PrepareRT 的直接 show: 仍需 FRAMECHANGED 触发 NCCALCSIZE
         * (borderless 客户区)。prepared 路径已做过, 跳过省一笔 DWM 事务。 */
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    RefreshClientSizeCache();

    LayoutRoot();
    SubmitRenderThreadFrameNow(FrameReason::Startup, PresentPolicy::Immediate);
    ValidateRect(hwnd_, nullptr);

    if (startMax) {
        /* L102/L103: 窗口已在 PrepareRT 真最大化 + WS_EX_LAYERED alpha 0(shown 但透明,
         * client=工作区, rcNormalPosition=常规)。OnPaint 已把内容绘进 RT, 这里一次
         * alpha→255 揭示 → 首帧即全屏 + 有内容, 无黑底/白边/先小后大闪。 */
        SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
        ClearLayeredAfterAlpha(hwnd_);
    } else {
        /* SW_SHOWNA 显示但不激活 → 不触发 WM_ACTIVATE 的 DWM 首帧同步(部分机器 200-300ms)。 */
        ShowWindow(hwnd_, SW_SHOWNA);
    }
    if (activate && !toolWindow_) {
        BringWindowToTop(hwnd_);
        SetForegroundWindow(hwnd_);
    }
}

void UiWindowImpl::Hide() {
    ShowWindow(hwnd_, SW_HIDE);
}

void UiWindowImpl::NotifyWidgetDestroyed(Widget* w) {
    if (!w) return;
    if (hoveredWidget_ == w) hoveredWidget_ = nullptr;
    if (pressedWidget_ == w) pressedWidget_ = nullptr;
    if (focusedWidget_ == w) focusedWidget_ = nullptr;
    if (tooltipWidget_ == w) tooltipWidget_ = nullptr;
    if (dragSource_ == w) { dragSource_ = nullptr; dragActive_ = false; }
    if (dragOverTarget_ == w) dragOverTarget_ = nullptr;
    animationHost_.RemoveWidget(w);
    propertyAnimations_.Cancel(w);
}

void UiWindowImpl::ActivateForMouseInput() {
    if (!hwnd_ || toolWindow_) return;

    BringWindowToTop(hwnd_);
    SetForegroundWindow(hwnd_);
    SetActiveWindow(hwnd_);
    ::SetFocus(hwnd_);
}

void UiWindowImpl::Invalidate() {
    if (!hwnd_) return;
    if (visualUpdateDepth_ > 0) {
        visualPaintDirty_ = true;
        return;
    }
    if (!IsWindowVisible(hwnd_) || IsIconic(hwnd_)) {
        if (IsTraceEnabled()) {
            TraceEvent("core_window", "invalidate_skipped_hidden",
                       {TraceU64("hwnd", static_cast<uint64_t>(
                                      reinterpret_cast<uintptr_t>(hwnd_))),
                        TraceBool("iconic", IsIconic(hwnd_) != FALSE)});
        }
        return;
    }
    RequestRenderFrame(FrameReason::Paint, PresentPolicy::Deferred);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void UiWindowImpl::InvalidateNow() {
    if (!hwnd_) return;
    Invalidate();
    if (visualUpdateDepth_ <= 0) {
        const bool visibleForPaint = IsWindowVisible(hwnd_) && !IsIconic(hwnd_);
        if (visibleForPaint) {
            RequestRenderFrame(FrameReason::Paint, PresentPolicy::Immediate);
        }
        UpdateWindow(hwnd_);
        if (visibleForPaint) {
            FlushRenderFrameNow(FrameReason::Paint, PresentPolicy::Immediate);
        }
    }
}

bool UiWindowImpl::HasPendingPaint() const {
    if (!hwnd_) return false;
    return GetUpdateRect(hwnd_, nullptr, FALSE) != FALSE;
}

void UiWindowImpl::RequestRenderFrame(FrameReason reason, PresentPolicy policy) {
    if (!hwnd_) return;
    frameScheduler_.Request(reason, policy);
    if (renderFramePosted_) return;
    renderFramePosted_ = true;
    PostMessageW(hwnd_, kMsgRenderFrame, 0, 0);
}

void UiWindowImpl::FlushRenderFrameNow(FrameReason reason, PresentPolicy policy) {
    if (!hwnd_) return;
    if (reason != FrameReason::None) {
        RequestRenderFrame(reason, policy);
    }
    renderFramePosted_ = false;
    if (!frameScheduler_.HasPending()) return;
    PaintAndValidate();
}

void UiWindowImpl::SubmitFrameJob(const FrameRequest& frame, DisplayList displayList) {
    if (!hwnd_ || !renderWindowId_.IsValid() || frame.generation == 0) return;
    FrameJob job;
    job.window = renderWindowId_;
    job.hwnd = hwnd_;
    job.generation = frame.generation;
    job.width_px = static_cast<int>(clientWidthPx_);
    job.height_px = static_cast<int>(clientHeightPx_);
    job.dpi_scale = dpiScale_;
    job.policy = frame.policy;
    job.priority = static_cast<int>(frame.policy);
    job.render_thread_present = renderThreadPresentActive_;
    job.defer_present = deferNextFramePresent_ && FrameRequiresPresentBarrier(frame);
    job.display_list = std::move(displayList);
    TraceEvent("core_window", "frame_job_submitted",
               {TraceU64("frame_generation", frame.generation),
                TraceI64("policy", static_cast<int64_t>(frame.policy)),
                TraceI64("width_px", job.width_px),
                TraceI64("height_px", job.height_px),
                TraceBool("render_thread_present", job.render_thread_present),
                TraceBool("defer_present", job.defer_present),
                TraceI64("display_list_commands", static_cast<int64_t>(job.display_list.commands.size()))});
    RenderThread::Instance().Submit(std::move(job));
}

void UiWindowImpl::ActivateRenderThreadPresent() {
    if (renderThreadPresentActive_ || !renderWindowId_.IsValid()) return;
    renderer_.ReleaseRenderTarget();
    renderThreadPresentActive_ = true;
    TraceEvent("core_window", "render_thread_present_activated",
               {TraceU64("window_id", renderWindowId_.window_id),
                TraceU64("hwnd_generation", renderWindowId_.hwnd_generation)});
}

bool UiWindowImpl::WaitForRenderGeneration(const FrameRequest& frame, bool force, bool prepared) {
    if (!hwnd_ || !renderWindowId_.IsValid() || frame.generation == 0) return false;
    if (!renderThreadPresentActive_) return false;
    if (!force && !FrameRequiresPresentBarrier(frame)) return false;
    const bool completed = RenderThread::Instance().WaitForGeneration(
        renderWindowId_.window_id, frame.generation, 1000);
    TraceEvent("core_window",
               completed ? (prepared ? "render_generation_prepared"
                                     : "render_generation_presented")
                         : "render_generation_wait_incomplete",
               {TraceU64("frame_generation", frame.generation),
                TraceBool("forced", force),
                TraceBool("prepared", prepared),
                TraceBool("completed", completed)});
    return completed;
}

void UiWindowImpl::SubmitRenderThreadFrameNow(FrameReason reason, PresentPolicy policy) {
    if (!hwnd_ || !renderWindowId_.IsValid()) return;
    frameScheduler_.Request(reason, policy);
    auto frame = frameScheduler_.BeginFrame();
    if (frame.generation == 0) return;
    ActivateRenderThreadPresent();
    DisplayList displayList = OnPaint(frame.generation);
    SubmitFrameJob(frame, std::move(displayList));
    frameScheduler_.CompleteFrame(frame.generation);
    TraceEvent("core_window", "frame_scheduler_complete",
               {TraceU64("frame_generation", frame.generation)});
    WaitForRenderGeneration(frame, true);
}

bool UiWindowImpl::FlushVisualTransactionFrame(bool resizeDirty,
                                               bool paintDirty,
                                               bool final,
                                               bool deferPresent) {
    if (!hwnd_) return false;
    if (!resizeDirty && !paintDirty && !final) return false;
    if (resizeDirty) renderer_.skipVSync = true;
    frameScheduler_.RequestVisualTransaction(resizeDirty, paintDirty, final);
    deferredPresentGeneration_ = 0;
    deferredPresentPrepared_ = false;
    deferNextFramePresent_ = deferPresent;
    FlushRenderFrameNow(FrameReason::None, PresentPolicy::Deferred);
    deferNextFramePresent_ = false;
    return deferPresent && deferredPresentPrepared_;
}

void UiWindowImpl::PrepareDeferredVisualWindowRect(int xScreen, int yScreen,
                                                   int wDip, int hDip,
                                                   int wPx, int hPx) {
    if (!hwnd_) return;
    deferredVisualWindowRectPending_ = true;
    deferredVisualWindowXScreen_ = xScreen;
    deferredVisualWindowYScreen_ = yScreen;
    deferredVisualWindowWDip_ = wDip;
    deferredVisualWindowHDip_ = hDip;
    deferredVisualWindowWPx_ = wPx;
    deferredVisualWindowHPx_ = hPx;

    RECT wr{};
    RECT cr{};
    int nonClientW = 0;
    int nonClientH = 0;
    if (GetWindowRect(hwnd_, &wr) && GetClientRect(hwnd_, &cr)) {
        nonClientW = (wr.right - wr.left) - (cr.right - cr.left);
        nonClientH = (wr.bottom - wr.top) - (cr.bottom - cr.top);
    }
    const UINT oldClientW = clientWidthPx_;
    const UINT oldClientH = clientHeightPx_;
    const UINT targetClientW = static_cast<UINT>((std::max)(1, wPx - nonClientW));
    const UINT targetClientH = static_cast<UINT>((std::max)(1, hPx - nonClientH));
    UpdateClientSizeCache(targetClientW, targetClientH);
    LayoutRoot();
    const bool sizeChanged = oldClientW != targetClientW || oldClientH != targetClientH;
    const bool notifyPreparedResize = sizeChanged && onResize != nullptr;
    if (notifyPreparedResize) {
        preparedVisualResizeNotified_ = true;
        preparedVisualResizeWPx_ = targetClientW;
        preparedVisualResizeHPx_ = targetClientH;
        TraceEvent("core_window", "prepared_visual_resize_callback",
                   {TraceU64("client_w_px", targetClientW),
                    TraceU64("client_h_px", targetClientH)});
        NotifyResizeCallback(targetClientW, targetClientH);
    }
    TraceEvent("core_window", "set_window_rect_deferred",
               {TraceI64("x", xScreen),
                TraceI64("y", yScreen),
                TraceI64("w_dip", wDip),
                TraceI64("h_dip", hDip),
                TraceI64("w_px", wPx),
                TraceI64("h_px", hPx),
                TraceU64("client_w_px", targetClientW),
                TraceU64("client_h_px", targetClientH),
                TraceBool("resize_callback", notifyPreparedResize),
                TraceI64("depth", visualUpdateDepth_)});
}

bool UiWindowImpl::WaitForVisualTransactionDwmBarrier(uint64_t generation) {
    if (!hwnd_ || !IsWindowVisible(hwnd_) || IsIconic(hwnd_)) return false;
    HRESULT hr = S_OK;
    {
        TraceScope scope("core_window", "visual_transaction_dwm_flush_duration");
        hr = DwmFlush();
    }
    TraceEvent("core_window", "visual_transaction_dwm_flush",
               {TraceU64("frame_generation", generation),
                TraceI64("hr", static_cast<int64_t>(hr)),
                TraceBool("ok", SUCCEEDED(hr))});
    return SUCCEEDED(hr);
}

void UiWindowImpl::CommitDeferredVisualWindowRect() {
    if (!hwnd_ || !deferredVisualWindowRectPending_) return;
    const int x = deferredVisualWindowXScreen_;
    const int y = deferredVisualWindowYScreen_;
    const int wDip = deferredVisualWindowWDip_;
    const int hDip = deferredVisualWindowHDip_;
    const int wPx = deferredVisualWindowWPx_;
    const int hPx = deferredVisualWindowHPx_;
    deferredVisualWindowRectPending_ = false;

    TraceEvent("core_window", "set_window_rect_commit_after_prepare",
               {TraceI64("x", x),
                TraceI64("y", y),
                TraceI64("w_dip", wDip),
                TraceI64("h_dip", hDip),
                TraceI64("w_px", wPx),
                TraceI64("h_px", hPx)});
    {
        TraceScope scope("core_window", "set_window_rect_commit_duration");
        SetWindowPos(hwnd_, nullptr, x, y, wPx, hPx,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    }
}

bool UiWindowImpl::PresentPreparedVisualWindowFrame(uint64_t generation) {
    if (!hwnd_ || !renderWindowId_.IsValid() || !renderThreadPresentActive_) return false;
    const bool presented = RenderThread::Instance().PresentPrepared(renderWindowId_, true, 1000);
    TraceEvent("core_window",
               presented ? "visual_transaction_prepared_presented"
                         : "visual_transaction_prepared_present_failed",
               {TraceU64("frame_generation", generation),
                TraceBool("presented", presented)});
    return presented;
}

void UiWindowImpl::BeginCanvasVisualTransaction() {
    ++visualUpdateDepth_;
    TraceEvent("core_window", "begin_canvas_visual_transaction");
}

void UiWindowImpl::EndCanvasVisualTransaction() {
    if (visualUpdateDepth_ <= 0) return;
    --visualUpdateDepth_;
    if (visualUpdateDepth_ != 0) return;

    const bool needPaint = visualPaintDirty_ || visualResizeDirty_;
    const bool paintDirty = visualPaintDirty_;
    const bool resizeDirty = visualResizeDirty_;
    TraceEvent("core_window", "end_canvas_visual_transaction",
               {TraceBool("need_paint", needPaint),
                TraceBool("resize_dirty", resizeDirty)});
    visualPaintDirty_ = false;
    visualResizeDirty_ = false;
    if (!needPaint || !hwnd_) return;

    const bool deferPresent = deferredVisualWindowRectPending_ && resizeDirty;
    const bool prepared = FlushVisualTransactionFrame(resizeDirty, paintDirty, false,
                                                     deferPresent);
    const uint64_t preparedGeneration = deferredPresentGeneration_;
    if (deferPresent && prepared) {
        WaitForVisualTransactionDwmBarrier(preparedGeneration);
    }
    CommitDeferredVisualWindowRect();
    if (deferPresent && prepared) {
        if (!PresentPreparedVisualWindowFrame(preparedGeneration)) {
            SubmitRenderThreadFrameNow(FrameReason::Resize | FrameReason::Paint,
                                       PresentPolicy::Immediate);
        }
    } else if (deferPresent) {
        TraceEvent("core_window", "visual_transaction_prepare_fallback",
                   {TraceU64("frame_generation", preparedGeneration),
                    TraceBool("prepared", prepared)});
        SubmitRenderThreadFrameNow(FrameReason::Resize | FrameReason::Paint,
                                   PresentPolicy::Immediate);
    }
}

// ---- 窗口几何（DIP-native） ----

void UiWindowImpl::SetWindowRect(int xScreen, int yScreen, int wDip, int hDip) {
    if (!hwnd_) return;
    int pw = MulDiv(wDip, dpi_, 96);
    int ph = MulDiv(hDip, dpi_, 96);
    {
        TraceEvent("core_window", "set_window_rect_before",
                   {TraceI64("x", xScreen),
                    TraceI64("y", yScreen),
                    TraceI64("w_dip", wDip),
                    TraceI64("h_dip", hDip),
                    TraceI64("w_px", pw),
                    TraceI64("h_px", ph),
                    TraceI64("depth", visualUpdateDepth_)});
    }
    if (visualUpdateDepth_ > 0) {
        visualResizeDirty_ = true;
        visualPaintDirty_ = true;
        PrepareDeferredVisualWindowRect(xScreen, yScreen, wDip, hDip, pw, ph);
        ValidateRect(hwnd_, nullptr);
        return;
    }
    /* SWP_NOCOPYBITS 防止 Windows 把旧客户区内容整块位移。 */
    SetWindowPos(hwnd_, nullptr, xScreen, yScreen, pw, ph,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    InvalidateRect(hwnd_, nullptr, FALSE);
    UpdateWindow(hwnd_);
}

void UiWindowImpl::SetWindowSize(int wDip, int hDip) {
    if (!hwnd_) return;
    RECT r; GetWindowRect(hwnd_, &r);
    SetWindowRect(r.left, r.top, wDip, hDip);
}

void UiWindowImpl::SetWindowPosition(int xScreen, int yScreen) {
    if (!hwnd_) return;
    /* 只移动不改尺寸 */
    SetWindowPos(hwnd_, nullptr, xScreen, yScreen, 0, 0,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
}

void UiWindowImpl::GetWindowRectScreen(int* x, int* y, int* wDip, int* hDip) const {
    if (!hwnd_) return;
    RECT r; GetWindowRect(hwnd_, &r);
    /* x/y 是 screen px (Win32 GetWindowRect 原值), w/h 是 DIP. build 94+ L23:
     * 之前 x/y 也除 dpiScale_ 返 DIP 防 "save→restore round-trip 漂移", 但 sibling
     * API set_rect / set_position / Create 输入 x/y 一直是 screen px, 两边
     * 不自洽 — 用 get→set 复制窗口几何到 sub-window 必错位. 改回 screen px,
     * "DPI-stable 持久化" 需求改由 caller 用 ui_window_dpi(win) 自己 MulDiv. */
    if (x)    *x    = r.left;
    if (y)    *y    = r.top;
    if (wDip) *wDip = (int)((r.right  - r.left) / dpiScale_);
    if (hDip) *hDip = (int)((r.bottom - r.top ) / dpiScale_);
}

int UiWindowImpl::Dpi() const {
    return hwnd_ ? (int)dpi_ : 96;
}

void UiWindowImpl::ResizeWithAnchor(int wDip, int hDip,
                                     float anchorClientXDip, float anchorClientYDip,
                                     int anchorScreenX, int anchorScreenY) {
    /* 要让 client(acx, acy) 落在屏幕 (sx, sy)：
     *   new_window_left = sx - acx * dpiScale
     *   new_window_top  = sy - acy * dpiScale
     * 注意：这里假设无边框或 client-area 覆盖整个窗口（画布模式成立）。
     * 有系统边框的窗口 client 区相对窗口偏移一个标题栏 + 边框，不适用此 API。 */
    int newLeft = anchorScreenX - (int)(anchorClientXDip * dpiScale_);
    int newTop  = anchorScreenY - (int)(anchorClientYDip * dpiScale_);
    SetWindowRect(newLeft, newTop, wDip, hDip);
}

void UiWindowImpl::SetFrameless(bool frameless) {
    if (!hwnd_) return;
    if (frameless == borderless_) return;  // no-op

    // Recompute GWL_STYLE based on new mode + current resizable_.
    DWORD style;
    if (frameless) {
        style = WS_POPUP | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;
        if (resizable_) style |= WS_THICKFRAME;
    } else {
        style = WS_OVERLAPPEDWINDOW;
        if (!resizable_) style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }
    // Preserve WS_VISIBLE
    DWORD old = (DWORD)GetWindowLongPtrW(hwnd_, GWL_STYLE);
    if (old & WS_VISIBLE) style |= WS_VISIBLE;

    SetWindowLongPtrW(hwnd_, GWL_STYLE, style);
    borderless_ = frameless;

    // Force the non-client area to be recalculated; same flags used by Microsoft
    // docs for dynamic style changes.
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                 SWP_NOACTIVATE | SWP_FRAMECHANGED);

    // Re-layout root because the client area size may have changed.
    LayoutRoot();
    Invalidate();
}

void UiWindowImpl::SetResizable(bool resizable) {
    if (resizable_ == resizable) return;
    resizable_ = resizable;
    if (!hwnd_) return;

    DWORD style = (DWORD)GetWindowLongPtrW(hwnd_, GWL_STYLE);
    if (resizable_) {
        style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    } else {
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }

    SetWindowLongPtrW(hwnd_, GWL_STYLE, style);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                 SWP_NOACTIVATE | SWP_FRAMECHANGED);
    Invalidate();
}

void UiWindowImpl::EnableCanvasMode(bool enable) {
    canvasMode_ = enable;
    if (enable) {
        SetMinSize(32, 32);
        SetBackgroundMode(1);
        if (root_) root_->dragWindow = true;
        /* 隐藏根下第一个 TitleBar（如果有） */
        std::function<bool(Widget*)> hideTitleBar = [&](Widget* w) -> bool {
            if (!w) return false;
            if (dynamic_cast<TitleBarWidget*>(w)) { w->visible = false; return true; }
            for (auto& c : w->Children()) if (hideTitleBar(c.get())) return true;
            return false;
        };
        if (root_) hideTitleBar(root_.get());
    } else {
        SetMinSize(0, 0);
        SetBackgroundMode(0);
        if (root_) root_->dragWindow = false;
        /* 恢复 TitleBar visible */
        std::function<void(Widget*)> showTitleBar = [&](Widget* w) {
            if (!w) return;
            if (dynamic_cast<TitleBarWidget*>(w)) { w->visible = true; return; }
            for (auto& c : w->Children()) showTitleBar(c.get());
        };
        if (root_) showTitleBar(root_.get());
    }
    if (root_) LayoutRoot();
    Invalidate();
}

static void UpdateMaxButtonIcon(Widget* w, bool maximized);

void UiWindowImpl::UpdateDwmFrameMargins() {
    if (!borderless_) return;
    MARGINS margins = maximized_ ? MARGINS{0, 0, 0, 0} : MARGINS{0, 0, 1, 0};
    DwmExtendFrameIntoClientArea(hwnd_, &margins);
}

static void UpdateMaxButtonIcon(Widget* w, bool maximized) {
    if (!w) return;
    auto* tb = dynamic_cast<TitleBarWidget*>(w);
    if (tb) {
        auto* btn = dynamic_cast<CaptionButtonWidget*>(tb->MaxBtn());
        if (btn) btn->icon = maximized ? L"\xE923" : L"\xE922";
        return;
    }
    for (auto& c : w->Children()) UpdateMaxButtonIcon(c.get(), maximized);
}

static void WireTitleBar(Widget* w, HWND hwnd) {
    auto* tb = dynamic_cast<TitleBarWidget*>(w);
    if (tb) {
        tb->windowHandle = (void*)hwnd;
        if (tb->CloseBtn()) {
            tb->CloseBtn()->onClick = [hwnd]() { PostMessage(hwnd, WM_CLOSE, 0, 0); };
        }
        if (tb->MaxBtn()) {
            tb->MaxBtn()->onClick = [hwnd]() {
                ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
            };
        }
        if (tb->MinBtn()) {
            tb->MinBtn()->onClick = [hwnd]() { ShowWindow(hwnd, SW_MINIMIZE); };
        }
    }
    // Wire MenuBar to window HWND for popup menus
    auto* mb = dynamic_cast<MenuBarWidget*>(w);
    if (mb) {
        mb->SetHwnd(hwnd);
    }
    if (tb) return;
    for (auto& c : w->Children()) WireTitleBar(c.get(), hwnd);
}

void UiWindowImpl::SetRoot(WidgetPtr root) {
    root_ = std::move(root);
    // Auto-wire any TitleBarWidget to this window's HWND
    if (root_ && hwnd_) WireTitleBar(root_.get(), hwnd_);
    if (root_ && canvasMode_) {
        root_->dragWindow = true;
        std::function<bool(Widget*)> hideTitleBar = [&](Widget* w) -> bool {
            if (!w) return false;
            if (dynamic_cast<TitleBarWidget*>(w)) { w->visible = false; return true; }
            for (auto& c : w->Children()) if (hideTitleBar(c.get())) return true;
            return false;
        };
        hideTitleBar(root_.get());
    }
    LayoutRoot();
    UpdateCaretBlinkTimer();
    UpdateToggleAnimTimer();
    Invalidate();
}

void UiWindowImpl::StartWindowOpenAnimation() {
    if (!hwnd_) return;

    windowAnimating_ = true;
    windowClosing_ = false;
    windowAnimProgress_ = 0.0f;

    // 窗口直接放在目标尺寸，不缩放，仅用透明度 + Y 偏移做动画
    EnsureLayeredForAlpha(hwnd_);
    SetWindowPos(hwnd_, nullptr, windowTargetX_, windowTargetY_ + kOpenSlideOffset,
                 (int)windowTargetWidth_, (int)windowTargetHeight_,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetLayeredWindowAttributes(hwnd_, 0, 0, LWA_ALPHA);

    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    QueryPerformanceCounter(&windowAnimStartTick_);
    KillTimer(hwnd_, kWindowOpenAnimTimerId);
    if (!SetTimer(hwnd_, kWindowOpenAnimTimerId, kWindowAnimIntervalMs, nullptr)) {
        windowAnimating_ = false;
        SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
        SetWindowPos(hwnd_, nullptr, windowTargetX_, windowTargetY_,
                     (int)windowTargetWidth_, (int)windowTargetHeight_,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        ClearLayeredAfterAlpha(hwnd_);
        ShowOwnedPopups(hwnd_, TRUE);
        if (!toolWindow_) SetForegroundWindow(hwnd_);
        return;
    }

    PostMessageW(hwnd_, WM_TIMER, kWindowOpenAnimTimerId, 0);
}

void UiWindowImpl::StartWindowCloseAnimation() {
    if (!hwnd_) return;

    windowClosing_ = true;
    windowAnimating_ = true;
    windowAnimProgress_ = 0.0f;
    EnsureLayeredForAlpha(hwnd_);
    SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);

    RECT rect;
    GetWindowRect(hwnd_, &rect);
    windowTargetWidth_ = (float)(rect.right - rect.left);
    windowTargetHeight_ = (float)(rect.bottom - rect.top);
    windowCloseStartX_ = rect.left;
    windowCloseStartY_ = rect.top;

    QueryPerformanceCounter(&windowAnimStartTick_);
    KillTimer(hwnd_, kWindowCloseAnimTimerId);
    if (!SetTimer(hwnd_, kWindowCloseAnimTimerId, kWindowAnimIntervalMs, nullptr)) {
        windowClosing_ = false;
        windowAnimating_ = false;
        DestroyWindow(hwnd_);
    }
}

bool UiWindowImpl::HasAnimationFrameWork() const {
    return !animationHost_.Empty() || propertyAnimations_.HasActive();
}

void UiWindowImpl::EnsureAnimationTimer() {
    if (!hwnd_ || toggleAnimTimerRunning_ || !HasAnimationFrameWork()) return;
    if (SetTimer(hwnd_, kToggleAnimTimerId, kToggleAnimIntervalMs, nullptr) != 0) {
        toggleAnimTimerRunning_ = true;
    }
}

void UiWindowImpl::StopAnimationTimer() {
    if (!hwnd_ || !toggleAnimTimerRunning_) return;
    KillTimer(hwnd_, kToggleAnimTimerId);
    toggleAnimTimerRunning_ = false;
}

void UiWindowImpl::UpdateToggleAnimTimer() {
    if (!hwnd_) return;

    animationHost_.Clear();
    if (root_) {
        ForEachWidget(root_.get(), [&](Widget* w) {
            bool needsWidgetTimer = false;
            AnimationInvalidation invalidation = AnimationInvalidation::Paint;

            auto* tw = dynamic_cast<ToggleWidget*>(w);
            if (tw && tw->animating_) {
                needsWidgetTimer = true;
            }
            auto* cb = dynamic_cast<CheckBoxWidget*>(w);
            if (cb && cb->animating_) {
                needsWidgetTimer = true;
            }
            auto* rb = dynamic_cast<RadioButtonWidget*>(w);
            if (rb && rb->animating_) {
                needsWidgetTimer = true;
            }
            auto* pb = dynamic_cast<ProgressBarWidget*>(w);
            if (pb && (pb->animating_ || pb->IsIndeterminate())) {
                needsWidgetTimer = true;
            }
            auto* sl = dynamic_cast<SliderWidget*>(w);
            if (sl && sl->ThumbAnimating()) {
                needsWidgetTimer = true;
            }
            auto* sv = dynamic_cast<SplitViewWidget*>(w);
            if (sv && sv->animating_) {
                needsWidgetTimer = true;
                invalidation |= AnimationInvalidation::Layout;
                invalidation |= AnimationInvalidation::HitTest;
            }
            auto* ex = dynamic_cast<ExpanderWidget*>(w);
            if (ex && ex->animating_) {
                needsWidgetTimer = true;
                invalidation |= AnimationInvalidation::Layout;
                invalidation |= AnimationInvalidation::HitTest;
            }
            auto* iv = dynamic_cast<ImageViewWidget*>(w);
            if (iv && iv->IsLoading()) {
                needsWidgetTimer = true;
            }
            auto* ov = dynamic_cast<OverlayWidget*>(w);
            if (ov && ov->IsActive() && ov->ShowSpinner()) {
                needsWidgetTimer = true;
            }
            if (needsWidgetTimer) animationHost_.RegisterWidget(w, invalidation);
        });
    }

    bool needsTimer = HasAnimationFrameWork();
    if (needsTimer) {
        EnsureAnimationTimer();
        Invalidate();
    } else {
        StopAnimationTimer();
    }
}

void UiWindowImpl::RegisterAnimatingWidget(Widget* w, AnimationInvalidation invalidation) {
    if (!hwnd_ || !w) return;

    animationHost_.RegisterWidget(w, invalidation);
    EnsureAnimationTimer();
    Invalidate();
}

void UiWindowImpl::AnimateProperty(Widget* target, AnimProperty prop, float from, float to,
                                   float durationMs, EasingFunction easing,
                                   std::function<void()> onComplete) {
    propertyAnimations_.Animate(target, prop, from, to, durationMs, easing, std::move(onComplete));
    EnsureAnimationTimer();
    Invalidate();
}

void UiWindowImpl::CancelPropertyAnimation(Widget* target, AnimProperty prop) {
    propertyAnimations_.Cancel(target, prop);
}

void UiWindowImpl::SetTitle(const std::wstring& title) {
    title_ = title;
    if (hwnd_) SetWindowTextW(hwnd_, title_.c_str());
}

void UiWindowImpl::UpdateClientSizeCache(UINT widthPx, UINT heightPx) {
    clientWidthPx_ = widthPx;
    clientHeightPx_ = heightPx;
    const float s = dpiScale_ > 0.0f ? dpiScale_ : 1.0f;
    clientWidthDip_ = static_cast<float>(widthPx) / s;
    clientHeightDip_ = static_cast<float>(heightPx) / s;
}

void UiWindowImpl::RefreshClientSizeCache() {
    if (!hwnd_) {
        UpdateClientSizeCache(0, 0);
        return;
    }
    RECT rc{};
    if (!GetClientRect(hwnd_, &rc)) return;
    const int w = std::max<int>(0, static_cast<int>(rc.right - rc.left));
    const int h = std::max<int>(0, static_cast<int>(rc.bottom - rc.top));
    UpdateClientSizeCache(static_cast<UINT>(w), static_cast<UINT>(h));
}

D2D1_SIZE_F UiWindowImpl::CachedClientSizeDip() const {
    return D2D1::SizeF(clientWidthDip_, clientHeightDip_);
}

MeasureContext* g_activeMeasureContext = nullptr;

void UiWindowImpl::LayoutRoot() {
    if (!root_) return;
    if ((clientWidthPx_ == 0 || clientHeightPx_ == 0) && hwnd_) {
        RefreshClientSizeCache();
    }
    TraceScope scope("core_window", "layout_root_duration");
    D2D1_SIZE_F size = CachedClientSizeDip();
    if (size.width <= 0.0f || size.height <= 0.0f) return;
    TraceEvent("core_window", "layout_root_begin",
               {TraceF64("client_w", size.width),
                TraceF64("client_h", size.height)});
    root_->rect = {0, 0, size.width, size.height};
    Viewport() = root_->rect;
    ActiveMeasureContextScope measureScope(measureContext_, renderer_);
    root_->DoLayout();
}

// ---- WndProc ----

LRESULT CALLBACK UiWindowImpl::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    UiWindowImpl* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<UiWindowImpl*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<UiWindowImpl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT UiWindowImpl::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT:
    case WM_DISPLAYCHANGE:
        if (visualUpdateDepth_ > 0) {
            visualPaintDirty_ = true;
            if (msg == WM_PAINT) {
                PAINTSTRUCT ps{};
                BeginPaint(hwnd_, &ps);
                EndPaint(hwnd_, &ps);
            } else {
                ValidateRect(hwnd_, nullptr);
            }
            return 0;
        }
        if (!IsWindowVisible(hwnd_) || IsIconic(hwnd_)) {
            if (msg == WM_PAINT) {
                PAINTSTRUCT ps{};
                BeginPaint(hwnd_, &ps);
                EndPaint(hwnd_, &ps);
            } else {
                ValidateRect(hwnd_, nullptr);
            }
            return 0;
        }
        if (msg == WM_PAINT) {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd_, &ps);
            EndPaint(hwnd_, &ps);
        } else {
            ValidateRect(hwnd_, nullptr);
        }
        RequestRenderFrame(FrameReason::Paint, PresentPolicy::Deferred);
        return 0;

    case WM_ENTERSIZEMOVE:
        isMoving_ = true;
        isResizing_ = false;  // 先假设是移动，WM_SIZING 会修正
        CloseMenu();
        break;

    case WM_SIZING: {
        // 收到 WM_SIZING 说明是 resize，不是纯移动
        if (!isResizing_) {
            isResizing_ = true;
        }
        /* L57: aspect ratio lock — borderless 看图器 enter 时 SetAspectLock(image_w, image_h),
         * 用户拖窗任意边/角时这里按比例修正 RECT, Win32 把修正后的 RECT 当 user
         * 实际拖的 size, image 永远严格填满 widget = window. */
        if (aspectLockW_ > 0 && aspectLockH_ > 0) {
            RECT* r = reinterpret_cast<RECT*>(lParam);
            int w = r->right - r->left;
            int h = r->bottom - r->top;
            if (w <= 0 || h <= 0) return TRUE;
            const double aspect = (double)aspectLockW_ / (double)aspectLockH_;
            switch (wParam) {
                case WMSZ_LEFT:
                case WMSZ_RIGHT:
                    /* 拖横向 → 按 w 算 h, 调整 bottom (保留 top, 朝下扩) */
                    r->bottom = r->top + (int)((double)w / aspect + 0.5);
                    break;
                case WMSZ_TOP:
                case WMSZ_BOTTOM:
                    /* 拖纵向 → 按 h 算 w, 调整 right (保留 left, 朝右扩) */
                    r->right = r->left + (int)((double)h * aspect + 0.5);
                    break;
                case WMSZ_TOPLEFT:
                case WMSZ_TOPRIGHT:
                case WMSZ_BOTTOMLEFT:
                case WMSZ_BOTTOMRIGHT: {
                    /* 拖角 → 按拖动幅度大的那边算另一边. user 拖角时直觉是
                     * "整个矩形跟着鼠标走", 哪边离 aspect 更远就以那边为主. */
                    const double cur_aspect = (double)w / (double)h;
                    if (cur_aspect > aspect) {
                        int newH = (int)((double)w / aspect + 0.5);
                        if (wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOPRIGHT)
                            r->top = r->bottom - newH;
                        else
                            r->bottom = r->top + newH;
                    } else {
                        int newW = (int)((double)h * aspect + 0.5);
                        if (wParam == WMSZ_TOPLEFT || wParam == WMSZ_BOTTOMLEFT)
                            r->left = r->right - newW;
                        else
                            r->right = r->left + newW;
                    }
                    break;
                }
                default:
                    break;
            }
            return TRUE;
        }
        break;
    }

    case WM_EXITSIZEMOVE:
    {
        const bool wasResizing = isResizing_;
        isMoving_ = false;
        isResizing_ = false;
        if (wasResizing && renderThreadPresentActive_) {
            TraceEvent("core_window", "wm_exitsizemove_final_resize_frame");
            RequestRenderFrame(FrameReason::Resize | FrameReason::Final, PresentPolicy::Final);
            ValidateRect(hwnd_, nullptr);
            return 0;
        }
        Invalidate();
        break;
    }

    case WM_SIZE: {
        TraceScope wmSizeScope("core_window", "wm_size_duration");
        {
            TraceEvent("core_window", "wm_size",
                       {TraceU64("w_px", (unsigned)LOWORD(lParam)),
                        TraceU64("h_px", (unsigned)HIWORD(lParam)),
                        TraceI64("depth", visualUpdateDepth_),
                        TraceU64("wparam", (unsigned)wParam)});
        }
        // 动画期间跳过 resize/layout/repaint，DWM 会自动缩放已有内容
        if (windowAnimating_) {
            maximized_ = (wParam == SIZE_MAXIMIZED);
            return 0;
        }
        const UINT sizeW = LOWORD(lParam);
        const UINT sizeH = HIWORD(lParam);
        UpdateClientSizeCache(sizeW, sizeH);
        OnResize(sizeW, sizeH);
        maximized_ = (wParam == SIZE_MAXIMIZED);
        // 更新最大化按钮图标：最大化 → 还原图标，非最大化 → 最大化图标
        UpdateMaxButtonIcon(root_.get(), maximized_);
        {
            TraceScope frameScope("core_window", "update_dwm_frame_margins_duration");
            UpdateDwmFrameMargins();
        }
        if (visualUpdateDepth_ > 0) {
            visualResizeDirty_ = true;
            visualPaintDirty_ = true;
            ValidateRect(hwnd_, nullptr);
            return 0;
        }
        const bool interactiveResize = isMoving_ && isResizing_;
        const PresentPolicy resizePolicy = interactiveResize
            ? PresentPolicy::Immediate
            : PresentPolicy::Final;
        FrameReason resizeReason = FrameReason::Resize;
        if (resizePolicy == PresentPolicy::Final) resizeReason |= FrameReason::Final;
        TraceEvent("core_window", "wm_size_request_resize_frame",
                   {TraceBool("interactive", interactiveResize),
                    TraceI64("policy", static_cast<int64_t>(resizePolicy))});
        RequestRenderFrame(resizeReason, resizePolicy);
        ValidateRect(hwnd_, nullptr);
        return 0;
    }

    case WM_DPICHANGED:
        OnDpiChanged(HIWORD(wParam), reinterpret_cast<const RECT*>(lParam)); return 0;

    case WM_GETMINMAXINFO:
        OnGetMinMaxInfo(reinterpret_cast<MINMAXINFO*>(lParam)); return 0;

    case WM_MOUSEACTIVATE:
        if (!toolWindow_) {
            ActivateForMouseInput();
            return MA_ACTIVATE;
        }
        break;

    case WM_MOUSEMOVE:
        if (canvasDragTracking_) {
            POINT cur;
            GetCursorPos(&cur);
            int newLeft = canvasDragStartRect_.left + (cur.x - canvasDragStartScreen_.x);
            int newTop = canvasDragStartRect_.top + (cur.y - canvasDragStartScreen_.y);
            SetWindowPos(hwnd_, nullptr, newLeft, newTop, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        if (tbDragTracking_) {
            /* L219: 标题栏拦截拖动 —— 按下后跟踪位移, 超过系统拖动阈值视为"拖动"。
             * 没超阈值的纯点击在 WM_LBUTTONUP 复位、不触发。超阈值后模仿 Windows
             * "拖最大化窗口标题栏 → 还原并跟随光标": 记抓取点在(全屏)标题栏的水平比例
             * + 垂直偏移 → fire onTitleBarDrag(宿主退出全屏/还原窗口大小) → 把还原后的
             * 窗口摆到光标下(光标落回标题栏同一相对位置) → 重发系统移动循环跟随光标。 */
            POINT cur; GetCursorPos(&cur);
            if (std::abs(cur.x - tbDragStartScreen_.x) > GetSystemMetrics(SM_CXDRAG) ||
                std::abs(cur.y - tbDragStartScreen_.y) > GetSystemMetrics(SM_CYDRAG)) {
                tbDragTracking_ = false;
                RECT fr; GetWindowRect(hwnd_, &fr);
                float fx = (fr.right > fr.left)
                    ? float(tbDragStartScreen_.x - fr.left) / float(fr.right - fr.left)
                    : 0.5f;
                int grabY = tbDragStartScreen_.y - fr.top;
                if (onTitleBarDrag) onTitleBarDrag();   // 宿主退出全屏 → 窗口还原成窗口态
                RECT wr; GetWindowRect(hwnd_, &wr);
                int nw = wr.right - wr.left;
                int newLeft = cur.x - int(fx * nw);
                int newTop  = cur.y - grabY;
                SetWindowPos(hwnd_, nullptr, newLeft, newTop, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                ReleaseCapture();
                /* intercept 此刻已被宿主在 exit_fullscreen 里关掉 → 这次 NCLBUTTONDOWN
                 * 不再被拦截, 进系统移动循环, 窗口跟随光标直到松手。 */
                SendMessageW(hwnd_, WM_NCLBUTTONDOWN, HTCAPTION,
                             MAKELPARAM(cur.x, cur.y));
            }
            return 0;
        }
        OnMouseMove((float)GET_X_LPARAM(lParam), (float)GET_Y_LPARAM(lParam)); return 0;
    case WM_MOUSELEAVE:
        if (hoveredWidget_) {
            for (Widget* w = hoveredWidget_; w; w = w->Parent()) {
                w->hovered = false;
                w->RefreshCssState();
                if (w->onMouseLeaveHook) w->onMouseLeaveHook();
            }
            hoveredWidget_ = nullptr;
        }
        tooltipVisible_ = false; tooltipWidget_ = nullptr;
        InvalidateNow(); return 0;
    case WM_LBUTTONDOWN:
        OnMouseDown((float)GET_X_LPARAM(lParam), (float)GET_Y_LPARAM(lParam)); return 0;
    case WM_LBUTTONUP:
        if (canvasDragTracking_) {
            canvasDragTracking_ = false;
            ReleaseCapture();
            return 0;
        }
        if (tbDragTracking_) {   /* L219: 标题栏只是点击(没拖过阈值), 复位、不退全屏 */
            tbDragTracking_ = false;
            ReleaseCapture();
            return 0;
        }
        OnMouseUp((float)GET_X_LPARAM(lParam), (float)GET_Y_LPARAM(lParam)); return 0;
    case WM_CAPTURECHANGED:
        canvasDragTracking_ = false;
        tbDragTracking_ = false;   /* L219: capture 被夺走时复位标题栏拖动跟踪 */
        // 鼠标 capture 被夺走 (DoDragDrop 起拖 / 系统). press 中的 widget 收不到
        // WM_LBUTTONUP, 复位它避免卡在 drag 态. CancelMouseCapture 自守 pressedWidget_
        // 为空时 no-op (正常 ReleaseCapture 流程已先清空, 不会重复触发).
        CancelMouseCapture();
        return 0;
    case WM_LBUTTONDBLCLK:
        // Win32 with CS_DBLCLKS replaces the second WM_LBUTTONDOWN of a
        // rapid double-click with WM_LBUTTONDBLCLK. Without this branch
        // every other quick click would be dropped (no down → no click).
        // Run the normal mouse-down flow first (sets pressedWidget_ so
        // the upcoming WM_LBUTTONUP can fire onClick exactly like a
        // single click) then the double-click hooks for any widget that
        // wants both. Mirrors browser semantics where a dblclick is two
        // click events plus a dblclick.
        OnMouseDown       ((float)GET_X_LPARAM(lParam), (float)GET_Y_LPARAM(lParam));
        OnMouseDoubleClick((float)GET_X_LPARAM(lParam), (float)GET_Y_LPARAM(lParam));
        return 0;
    case WM_TIMER:
        if (wParam == kTooltipTimerId) {
            KillTimer(hwnd_, kTooltipTimerId);
            tooltipTimerId_ = 0;
            // 沿父链上溯取 tooltip owner (同 OnMouseMove 调度处, L72).
            Widget* tipOwner = hoveredWidget_;
            while (tipOwner && tipOwner->tooltip.empty()) tipOwner = tipOwner->Parent();
            if (tipOwner && !tooltipVisible_) {
                tooltipVisible_ = true;
                tooltipWidget_ = tipOwner;
                tooltipX_ = mouseX_;
                tooltipY_ = mouseY_;
                Invalidate();
            }
            return 0;
        }
        if (wParam == kCaretBlinkTimerId) {
            if (HasFocusedTextInput()) {
                Invalidate();
            } else {
                UpdateCaretBlinkTimer();
            }
            return 0;
        }
        if (wParam == kToggleAnimTimerId) {
            if (HasAnimationFrameWork()) {
                Invalidate();
            } else {
                StopAnimationTimer();
            }
            return 0;
        }
        if (wParam == kWindowOpenAnimTimerId && windowAnimating_ && !windowClosing_) {
            LARGE_INTEGER now, freq;
            QueryPerformanceCounter(&now);
            QueryPerformanceFrequency(&freq);
            float elapsedMs = static_cast<float>(now.QuadPart - windowAnimStartTick_.QuadPart)
                              * 1000.0f / static_cast<float>(freq.QuadPart);
            windowAnimProgress_ = elapsedMs / kWindowOpenAnimDurationMs;
            if (windowAnimProgress_ >= 1.0f) {
                windowAnimProgress_ = 1.0f;
                windowAnimating_ = false;
                KillTimer(hwnd_, kWindowOpenAnimTimerId);
            }

            float t = EaseOutCubic(windowAnimProgress_);

            // 透明度：0 → 255
            BYTE alpha = static_cast<BYTE>(255.0f * t);
            SetLayeredWindowAttributes(hwnd_, 0, alpha, LWA_ALPHA);

            // Y 偏移：kOpenSlideOffset → 0（上滑入位），窗口尺寸不变
            int offsetY = static_cast<int>(kOpenSlideOffset * (1.0f - t));
            SetWindowPos(hwnd_, nullptr,
                         windowTargetX_, windowTargetY_ + offsetY,
                         (int)windowTargetWidth_, (int)windowTargetHeight_,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);

            if (!windowAnimating_) {
                ClearLayeredAfterAlpha(hwnd_);
                ShowOwnedPopups(hwnd_, TRUE);
                if (!toolWindow_) SetForegroundWindow(hwnd_);
                Invalidate();
            }
            return 0;
        }
        if (wParam == kWindowCloseAnimTimerId && windowAnimating_ && windowClosing_) {
            LARGE_INTEGER now, freq;
            QueryPerformanceCounter(&now);
            QueryPerformanceFrequency(&freq);
            float elapsedMs = static_cast<float>(now.QuadPart - windowAnimStartTick_.QuadPart)
                              * 1000.0f / static_cast<float>(freq.QuadPart);
            windowAnimProgress_ = elapsedMs / kWindowCloseAnimDurationMs;
            if (windowAnimProgress_ >= 1.0f) {
                KillTimer(hwnd_, kWindowCloseAnimTimerId);
                windowAnimating_ = false;
                windowClosing_ = false;
                DestroyWindow(hwnd_);
                return 0;
            }

            float t = windowAnimProgress_;
            float scale = 1.0f;
            if (t < kClosePeakPhase) {
                float local = EaseOutCubic(t / kClosePeakPhase);
                scale = 1.0f + (kPopPeakScale - 1.0f) * local;
            } else {
                float local = EaseInCubic((t - kClosePeakPhase) / (1.0f - kClosePeakPhase));
                scale = kPopPeakScale + (kCloseEndScale - kPopPeakScale) * local;
            }

            float fadeP = Clamp01((t - kCloseFadeStart) / (1.0f - kCloseFadeStart));
            BYTE alpha = (BYTE)(255.0f * (1.0f - fadeP));
            SetLayeredWindowAttributes(hwnd_, 0, alpha, LWA_ALPHA);

            float centerX = windowCloseStartX_ + windowTargetWidth_ / 2.0f;
            float centerY = windowCloseStartY_ + windowTargetHeight_ / 2.0f;
            SetWindowScaleAtCenter(hwnd_, centerX, centerY, windowTargetWidth_, windowTargetHeight_, scale);
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        if (root_) {
            ForEachWidget(root_.get(), [&](Widget* w) {
                auto* ti = dynamic_cast<TextInputWidget*>(w);
                if (ti) ti->focused = false;
                auto* ta = dynamic_cast<TextAreaWidget*>(w);
                if (ta) ta->focused = false;
                auto* cw = dynamic_cast<CustomWidget*>(w);
                if (cw) cw->focused = false;
            });
        }
        UpdateCaretBlinkTimer();
        InvalidateNow();
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd_, &pt);
        OnMouseWheel((float)pt.x, (float)pt.y, GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;
    }
    case WM_MOUSEHWHEEL: {
        // Horizontal wheel → @wheel with deltaX (DOM parity). No built-in
        // control scrolls horizontally today, so only the hook path exists.
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd_, &pt);
        float hdx = (float)pt.x / dpiScale_, hdy = (float)pt.y / dpiScale_;
        if (root_) {
            Widget* hit = root_->HitTest(hdx, hdy);
            if (hit && hit->onMouseWheelHook) {
                MouseEvent e{hdx, hdy};
                StampDomMouseState(e, -1);
                e.deltaX = (float)GET_WHEEL_DELTA_WPARAM(wParam);
                hit->onMouseWheelHook(e);
                InvalidateNow();
            }
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        float dx = (float)GET_X_LPARAM(lParam) / dpiScale_;
        float dy = (float)GET_Y_LPARAM(lParam) / dpiScale_;
        DispatchContextMenu(dx, dy);
        if (onRightClick) onRightClick(dx, dy);
        InvalidateNow();
        return 0;
    }

    case WM_IME_STARTCOMPOSITION:
        imeComposing_ = true;
        UpdateImeCompositionPos();
        break;
    case WM_IME_COMPOSITION:
        // Pin the IME composition/candidate window at the focused widget's
        // caret, then let DefWindowProc run the default composition handling
        // (result chars still arrive via WM_CHAR).
        UpdateImeCompositionPos();
        break;
    case WM_IME_ENDCOMPOSITION:
        imeComposing_ = false;
        break;

    case WM_CHAR:
        // Don't forward Tab char (handled in WM_KEYDOWN)
        if (wParam == '\t') return 0;
        // Forward to focused widget
        if (focusedWidget_) {
            ActiveMeasureContextScope measureScope(measureContext_, renderer_);
            if (focusedWidget_->OnKeyChar((wchar_t)wParam)) {
                InvalidateNow();
                return 0;
            }
        }
        break;

    case WM_KEYDOWN: {
        int vk = (int)wParam;
        /* L96: IME(中文等)激活、正在处理该键时, Windows 把 wParam 设成
         * VK_PROCESSKEY(0xE5), 真实键拿不到 → key 消费者(如快捷键捕获)记成
         * 0xE5 显示"?"。从 lParam 的扫描码反查真实 VK(MapVirtualKey 走扫描码、
         * IME 无关、user32 已链接, 不引 imm32)。IME 文字输入走 WM_CHAR/
         * WM_IME_CHAR 另一条路, 不受此影响。 */
        if (vk == VK_PROCESSKEY) {
            /* 组字进行中: 按键 (退格删组字串 / 翻页 / 数字选字) 完全属于
             * IME, 绝不转发给控件 — 否则退格会把已落定文本也删掉。 */
            if (imeComposing_) break;
            UINT sc = (UINT)((lParam >> 16) & 0xFF);
            UINT real = MapVirtualKeyW(sc, MAPVK_VSC_TO_VK);
            if (real) vk = (int)real;
        }
        if (DispatchKeyDown(vk)) return 0;
        if (onKey) {
            HWND h = hwnd_;
            onKey(vk);
            // onKey may destroy this window (for example Esc/close). Do not
            // touch `this` after the callback; repaint the HWND directly if it
            // still exists so host-level commands (image navigation, zoom, ...)
            // are visible immediately.
            if (h && IsWindow(h)) {
                InvalidateRect(h, nullptr, FALSE);
                UpdateWindow(h);
            }
        }
        /* return 0 (而非 break) — onKey 回调里可能销毁本窗口 (典型: 自定义
         * 快捷键 close-on-key). break 会 fall-through 到末尾的
         * "return DefWindowProcW(hwnd_, ...)", 解引用 this->hwnd_, 撞 UAF.
         * 既然 onKey 装了 callback 就当 caller 已完全负责派发, 不再下传
         * DefWindowProc — 实践中 lib 应用无 system menu, 不依赖默认处理
         * (Alt+F4 / F10 走 WM_SYSKEYDOWN, 跟此分支无关). build 95+ L24 修. */
        return 0;
    }

    case WM_DROPFILES: OnDropFiles(reinterpret_cast<HDROP>(wParam)); return 0;

    case WM_SETCURSOR:
        switch (LOWORD(lParam)) {
        case HTCLIENT: {
            // Open ComboBox dropdown is an overlay, not a widget subtree —
            // hoveredWidget_ / CSS cursor resolution below would fall through
            // to whatever sits underneath it (e.g. a draggable chip showing
            // the move cursor). Inside the dropdown rect always show the
            // plain arrow and stop here.
            if (root_) {
                bool inDropdown = false;
                ForEachWidget(root_.get(), [&](Widget* w) {
                    if (inDropdown) return;
                    auto* cb = dynamic_cast<ComboBoxWidget*>(w);
                    if (!cb || !cb->IsOpen()) return;
                    POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd_, &pt);
                    const float mx = (float)pt.x / dpiScale_;
                    const float my = (float)pt.y / dpiScale_;
                    auto dr = cb->DropdownRect();
                    if (mx >= dr.left && mx < dr.right &&
                        my >= dr.top && my < dr.bottom) {
                        inDropdown = true;
                    }
                });
                if (inDropdown) {
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                    return TRUE;
                }
            }
            // Splitter: resize cursor during drag or hover over bar
            if (root_) {
                bool splitterCursor = false;
                std::function<bool(Widget*)> checkSplitter = [&](Widget* w) -> bool {
                    auto* sp = dynamic_cast<SplitterWidget*>(w);
                    if (sp) {
                        LPCTSTR cursor = sp->IsVertical() ? IDC_SIZENS : IDC_SIZEWE;

                        // Always show resize cursor while dragging
                        if (sp->IsDragging()) { SetCursor(LoadCursor(nullptr, cursor)); return true; }

                        // Check hover over bar
                        POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd_, &pt);
                        float mx = (float)pt.x / dpiScale_, my = (float)pt.y / dpiScale_;
                        if (!sp->IsVertical()) {
                            float barX = sp->ContentLeft() + (sp->ContentWidth() - 5.0f) * sp->Ratio();
                            if (mx >= barX && mx <= barX + 5.0f) { SetCursor(LoadCursor(nullptr, cursor)); return true; }
                        } else {
                            float barY = sp->ContentTop() + (sp->ContentHeight() - 5.0f) * sp->Ratio();
                            if (my >= barY && my <= barY + 5.0f) { SetCursor(LoadCursor(nullptr, cursor)); return true; }
                        }
                    }
                    for (auto& c : w->Children())
                        if (checkSplitter(c.get())) return true;
                    return false;
                };
                splitterCursor = checkSplitter(root_.get());
                if (splitterCursor) return TRUE;
            }
            // CSS cursor: walk parent chain to find the first widget with an
            // explicit cursor (mirrors Web inheritance) before falling back
            // to the built-in I-beam / arrow defaults.
            ui::Widget* cursorOwner = hoveredWidget_;
            while (cursorOwner && cursorOwner->cursor == ui::CursorKind::Default)
                cursorOwner = cursorOwner->Parent();
            if (cursorOwner && cursorOwner->cursor != ui::CursorKind::Default) {
                LPCWSTR sys = IDC_ARROW;
                switch (cursorOwner->cursor) {
                    case ui::CursorKind::Pointer:     sys = IDC_HAND;       break;
                    case ui::CursorKind::Text:        sys = IDC_IBEAM;      break;
                    case ui::CursorKind::Crosshair:   sys = IDC_CROSS;      break;
                    case ui::CursorKind::Wait:        sys = IDC_WAIT;       break;
                    case ui::CursorKind::Move:        sys = IDC_SIZEALL;    break;
                    case ui::CursorKind::NotAllowed:  sys = IDC_NO;         break;
                    case ui::CursorKind::EwResize:    sys = IDC_SIZEWE;     break;
                    case ui::CursorKind::NsResize:    sys = IDC_SIZENS;     break;
                    case ui::CursorKind::NeswResize:  sys = IDC_SIZENESW;   break;
                    case ui::CursorKind::NwseResize:  sys = IDC_SIZENWSE;   break;
                    case ui::CursorKind::Help:        sys = IDC_HELP;       break;
                    case ui::CursorKind::None:        SetCursor(nullptr);   return TRUE;
                    default: break;
                }
                SetCursor(LoadCursor(nullptr, sys));
                return TRUE;
            }
            // Draggable elements (DnD sources, e.g. variable chips feeding a
            // chips-input) show the move cursor — framework-wide, mirrors the
            // affordance of HTML draggable items.
            for (ui::Widget* w = hoveredWidget_; w; w = w->Parent()) {
                if (w->draggable) {
                    SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
                    return TRUE;
                }
            }
            if (hoveredWidget_) {
                if (dynamic_cast<TextInputWidget*>(hoveredWidget_)) {
                    SetCursor(LoadCursor(nullptr, IDC_IBEAM)); return TRUE;
                }
                if (auto* ci = dynamic_cast<ChipsInputWidget*>(hoveredWidget_)) {
                    // Zoned cursor: chip body → move (drag-reorderable),
                    // chip × close zone → hand (click target),
                    // text / free area → I-beam. Active reorder keeps move.
                    if (ci->IsDraggingChip()) {
                        SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
                        return TRUE;
                    }
                    POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd_, &pt);
                    ActiveMeasureContextScope measureScope(measureContext_, renderer_);
                    const float mx = (float)pt.x / dpiScale_;
                    LPCWSTR shape = IDC_IBEAM;
                    if (ci->IsOverChipClose(measureContext_, mx)) shape = IDC_HAND;
                    else if (ci->IsOverChip(measureContext_, mx)) shape = IDC_SIZEALL;
                    SetCursor(LoadCursor(nullptr, shape));
                    return TRUE;
                }
                auto* ta = dynamic_cast<TextAreaWidget*>(hoveredWidget_);
                if (ta) {
                    // Arrow cursor on scrollbar area, I-beam on text area
                    POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd_, &pt);
                    float mx = (float)pt.x / dpiScale_;
                    if (ta->NeedsScrollbar() && mx >= ta->rect.right - 8) {
                        SetCursor(LoadCursor(nullptr, IDC_ARROW)); return TRUE;
                    }
                    SetCursor(LoadCursor(nullptr, IDC_IBEAM)); return TRUE;
                }
            }
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            return TRUE;
        }
        case HTLEFT:
        case HTRIGHT:
            SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
            return TRUE;
        case HTTOP:
        case HTBOTTOM:
            SetCursor(LoadCursor(nullptr, IDC_SIZENS));
            return TRUE;
        case HTTOPLEFT:
        case HTBOTTOMRIGHT:
            SetCursor(LoadCursor(nullptr, IDC_SIZENWSE));
            return TRUE;
        case HTTOPRIGHT:
        case HTBOTTOMLEFT:
            SetCursor(LoadCursor(nullptr, IDC_SIZENESW));
            return TRUE;
        default:
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            return TRUE;
        }

    case WM_ERASEBKGND: return 1;

    case WM_APP + 100: {
        // Menu item clicked. LPARAM = heap MenuClickInfo* (id + 全部属性), 读完 delete.
        auto* info = reinterpret_cast<MenuClickInfo*>(lParam);
        activeMenu_ = nullptr;  // menu already closed itself
        if (onMenuItemClick && info) onMenuItemClick(info);
        delete info;
        Invalidate();
        return 0;
    }
    case WM_APP + 120: {
        // InvokeSync: 跨线程 SendMessage 来的 "在 UI 线程上执行 fn(ud)" 请求
        auto* req = reinterpret_cast<UiInvokeReq*>(lParam);
        if (req && req->fn) req->fn(req->ud);
        return 0;
    }
    case kMsgRenderFrame: {
        renderFramePosted_ = false;
        if (!frameScheduler_.HasPending()) {
            return 0;
        }
        if (visualUpdateDepth_ > 0) {
            visualPaintDirty_ = true;
            return 0;
        }
        if (!IsWindowVisible(hwnd_) || IsIconic(hwnd_)) {
            return 0;
        }
        PaintAndValidate();
        return 0;
    }
    case kMsgRenderDeviceLost: {
        const uint64_t deviceGeneration = static_cast<uint64_t>(lParam);
        if (deviceGeneration != 0 &&
            deviceGeneration == lastDeviceRecoveryGeneration_) {
            return 0;
        }
        lastDeviceRecoveryGeneration_ = deviceGeneration;
        TraceEvent("core_window", "render_device_lost_recovery_requested",
                   {TraceU64("window_id", renderWindowId_.window_id),
                    TraceU64("hwnd_generation", renderWindowId_.hwnd_generation),
                    TraceU64("device_generation", deviceGeneration),
                    TraceI64("status", static_cast<int64_t>(wParam))});
        if (!hwnd_ || !renderWindowId_.IsValid() ||
            !IsWindowVisible(hwnd_) || IsIconic(hwnd_)) {
            return 0;
        }
        if (visualUpdateDepth_ > 0) {
            visualPaintDirty_ = true;
            visualResizeDirty_ = true;
            return 0;
        }
        RequestRenderFrame(FrameReason::Paint | FrameReason::Final,
                           PresentPolicy::Final);
        return 0;
    }
    case kMsgStartupReveal: {
        if (startupRevealPending_) {
            SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);

            if (borderless_) {
                MARGINS margins = maximized_ ? MARGINS{0, 0, 0, 0} : MARGINS{0, 0, 1, 0};
                DwmExtendFrameIntoClientArea(hwnd_, &margins);
            }

            SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            InvalidateRect(hwnd_, nullptr, FALSE);
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            startupRevealPending_ = false;
        }
        startupRevealPosted_ = false;
        return 0;
    }

    case WM_CLOSE:
        if (onCloseRequest && !onCloseRequest()) return 0;
        if (onClose) onClose();
        DestroyWindow(hwnd_);
        return 0;

    case WM_DESTROY: {
        if (caretBlinkTimerRunning_) {
            KillTimer(hwnd_, kCaretBlinkTimerId);
            caretBlinkTimerRunning_ = false;
        }
        /* 主窗销毁前先收掉 toast 叠加窗 + 其 timer + timeEndPeriod 配对
         * (this 在 RemoveWindow 后失效, 不能拖到那之后). */
        DestroyToast();
        UnregisterRenderWindow();
        HWND oldHwnd = hwnd_;
        hwnd_ = nullptr;
        auto& ctx = GetContext();
        // 先保存 id 并重置成员，再 RemoveWindow（RemoveWindow 会析构 this）。
        // 不能在 RemoveWindow 之后访问任何成员，否则是 use-after-free。
        uint64_t id = windowId;
        windowId = 0;
        // 清掉 HWND 的 userdata，防止 WM_NCDESTROY 再拿到已释放的 self
        if (oldHwnd) SetWindowLongPtrW(oldHwnd, GWLP_USERDATA, 0);
        if (!ctx.IsShuttingDown()) {
            if (id) ctx.RemoveWindow(id);          // this 在此之后失效
            if (!ctx.HasWindows()) PostQuitMessage(0);
        }
        return 0;
    }
    }

    // Borderless NC handling
    if (borderless_) {
        switch (msg) {
        case WM_NCCALCSIZE: return OnNcCalcSize(wParam, lParam);
        case WM_NCACTIVATE: return DefWindowProcW(hwnd_, WM_NCACTIVATE, wParam, -1);
        case WM_NCPAINT: return 0;
        case WM_NCHITTEST: return OnNcHitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        case WM_NCLBUTTONDOWN:
            /* L90: 菜单开着时, 点窗口非客户区 (含 canvas mode 下 OnNcHitTest 把
             * 整窗/画布判成 HTCAPTION 的可拖区) 先关菜单并消掉本次点击 —— 对齐
             * HTCLIENT 路径 OnMouseDown→CloseMenu 的行为. 否则 borderless 看图
             * 左键点画布走 WM_NCLBUTTONDOWN, 完全不经 OnMouseDown, 菜单关不掉.
             * 无菜单时 canvas mode 走库内自由拖动, 其它标题栏命中仍按系统行为. */
            if (activeMenu_) { ActivateForMouseInput(); CloseMenu(); return 0; }
            /* L219: 标题栏拖动拦截(宿主开启, 如全屏态)。命中点是 TitleBar 背景
             * (非按钮: 按钮命中是其自身 HTCLIENT, 不是 HTCAPTION)且 onTitleBarDrag
             * 已注册时, 不进系统移动循环, 改为自己 SetCapture + 跟踪位移(见 WM_MOUSEMOVE),
             * 超阈值才退出全屏。intercept=false(普通态)则 fall-through 走系统拖窗。 */
            if (wParam == HTCAPTION && titlebarDragIntercept_ && onTitleBarDrag && root_) {
                POINT spt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };  // screen
                POINT cpt = spt; ScreenToClient(hwnd_, &cpt);
                Widget* hit = root_->HitTest((float)cpt.x / dpiScale_,
                                             (float)cpt.y / dpiScale_);
                if (hit && dynamic_cast<TitleBarWidget*>(hit)) {
                    ActivateForMouseInput();
                    tbDragTracking_ = true;
                    tbDragStartScreen_ = spt;
                    SetCapture(hwnd_);
                    return 0;
                }
            }
            if (wParam == HTCAPTION && IsCanvasDragHit(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam))) {
                ActivateForMouseInput();
                canvasDragTracking_ = true;
                canvasDragStartScreen_ = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                GetWindowRect(hwnd_, &canvasDragStartRect_);
                SetCapture(hwnd_);
                return 0;
            }
            break;
        case WM_NCLBUTTONDBLCLK:
            if (wParam == HTCAPTION) {
                ShowWindow(hwnd_, maximized_ ? SW_RESTORE : SW_MAXIMIZE);
                return 0;
            }
            break;
        case WM_NCRBUTTONUP:
            /* L53: HTCAPTION 区域右键 → 走 onRightClick (widget tree
             * 的右键派发, 例如 WireSubtreeMenus 弹 app context menu),
             * 不走 DefWindowProc 的系统菜单 (Move/Size/Minimize/Close).
             *
             * 配合 dragWindow ancestor walk (上面 OnNcHitTest 改动 1) —
             * 画布可拖窗 + 画布右键弹 app 菜单 这对组合拳是 EnableCanvasMode
             * 的核心 UX, 缺一边 (右键卡死 / 拖不动) 都没法用.
             *
             * 后向兼容: onRightClick 未设 (典型 lib demo / 老应用) 时
             * fall-through DefWindowProc, titlebar 右键仍弹系统菜单, 老
             * 行为不变. */
            if (wParam == HTCAPTION && onRightClick) {
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ScreenToClient(hwnd_, &pt);
                float dx = (float)pt.x / dpiScale_;
                float dy = (float)pt.y / dpiScale_;
                onRightClick(dx, dy);
                Invalidate();
                return 0;
            }
            break;
        }
    }

    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

// ---- Paint ----

void UiWindowImpl::BeginAnimationFrame() {
    if (animationHost_.Empty()) return;

    uint64_t frameTick = GetTickCount64();
    double frameIntervalMs = lastAnimationFrameTick_ != 0
        ? static_cast<double>(frameTick - lastAnimationFrameTick_)
        : 0.0;
    lastAnimationFrameTick_ = frameTick;

    TraceScope frameScope("core_window", "animation_frame_duration");
    bool anyAnimating = false;
    bool needsLayout = false;
    int inputWidgets = (int)animationHost_.Size();
    int tickedWidgets = 0;
    std::vector<AnimationTarget> stillAnimating;
    const auto activeTargets = animationHost_.Targets();
    stillAnimating.reserve(activeTargets.size());

    for (const AnimationTarget& target : activeTargets) {
        Widget* w = target.widget;
        if (!w) continue;
        bool keep = false;
        bool ticked = false;

        auto* tw = dynamic_cast<ToggleWidget*>(w);
        if (tw && tw->animating_) {
            tw->UpdateAnimation();
            ticked = true;
        }
        if (tw && tw->animating_) keep = true;

        auto* cb = dynamic_cast<CheckBoxWidget*>(w);
        if (cb && cb->animating_) {
            cb->UpdateAnimation();
            ticked = true;
            if (cb->animating_) keep = true;
        }
        auto* rb = dynamic_cast<RadioButtonWidget*>(w);
        if (rb && rb->animating_) {
            rb->UpdateAnimation();
            ticked = true;
            if (rb->animating_) keep = true;
        }
        auto* pb = dynamic_cast<ProgressBarWidget*>(w);
        if (pb && pb->animating_) {
            pb->UpdateAnimation();
            ticked = true;
            if (pb->animating_) keep = true;
        }
        if (pb && pb->IsIndeterminate()) {
            keep = true;
        }
        auto* sl = dynamic_cast<SliderWidget*>(w);
        if (sl) {
            sl->UpdateThumbAnimation();
            ticked = true;
            if (sl->ThumbAnimating()) keep = true;
        }
        auto* sv = dynamic_cast<SplitViewWidget*>(w);
        if (sv && sv->animating_) {
            sv->UpdateAnimation();
            ticked = true;
            if (sv->animating_) keep = true;
        }
        auto* ex = dynamic_cast<ExpanderWidget*>(w);
        if (ex && ex->animating_) {
            ex->UpdateAnimation();
            ticked = true;
            if (ex->animating_) keep = true;
        }
        auto* iv = dynamic_cast<ImageViewWidget*>(w);
        if (iv && iv->IsLoading()) {
            keep = true;
        }
        auto* ov = dynamic_cast<OverlayWidget*>(w);
        if (ov && ov->IsActive() && ov->ShowSpinner()) {
            keep = true;
        }

        if (keep) {
            anyAnimating = true;
            stillAnimating.push_back(target);
            if (HasAnimationInvalidation(target.invalidation, AnimationInvalidation::Layout)) {
                needsLayout = true;
            }
        }
        if (ticked) ++tickedWidgets;
    }

    animationHost_.ReplaceTargets(std::move(stillAnimating));

    if (needsLayout) {
        LayoutRoot();
    }

    if (IsTraceEnabled()) {
        TraceEvent("core_window", "animation_frame",
                   {TraceI64("input_count", inputWidgets),
                    TraceI64("ticked_count", tickedWidgets),
                    TraceI64("remaining_count", (int64_t)animationHost_.Size()),
                    TraceBool("any_animating", anyAnimating),
                    TraceBool("needs_layout", needsLayout),
                    TraceF64("frame_interval_ms", frameIntervalMs),
                    TraceBool("dropped_frame_estimate", frameIntervalMs > 25.0)});
    }

    if (!anyAnimating) {
        animationHost_.Clear();
        lastAnimationFrameTick_ = 0;
        StopAnimationTimer();
    }
}

void UiWindowImpl::PaintAndValidate() {
    if (!hwnd_) return;
    auto frame = frameScheduler_.BeginFrame();
    TraceEvent("core_window", "paint_and_validate",
               {TraceI64("frame_reasons", static_cast<int64_t>(frame.reasons)),
                TraceI64("present_policy", static_cast<int64_t>(frame.policy)),
                TraceU64("frame_generation", frame.generation)});
    auto completeFrame = [&]() {
        if (frame.generation == 0) return;
        frameScheduler_.CompleteFrame(frame.generation);
        TraceEvent("core_window", "frame_scheduler_complete",
                   {TraceU64("frame_generation", frame.generation)});
    };
    if (frame.generation == 0) {
        ValidateRect(hwnd_, nullptr);
        return;
    }
    ActivateRenderThreadPresent();
    const bool prepared = deferNextFramePresent_ && FrameRequiresPresentBarrier(frame);
    DisplayList displayList = OnPaint(frame.generation);
    SubmitFrameJob(frame, std::move(displayList));
    ValidateRect(hwnd_, nullptr);
    completeFrame();
    const bool completed = WaitForRenderGeneration(frame, false, prepared);
    if (prepared) {
        deferredPresentGeneration_ = frame.generation;
        deferredPresentPrepared_ = completed;
    }
}

DisplayList UiWindowImpl::OnPaint(uint64_t frameGeneration) {
    const bool canRecordDisplayList = frameGeneration != 0 && renderWindowId_.IsValid();
    if (!canRecordDisplayList) return {};
    TraceScope paintScope("core_window", "on_paint_duration");
    if (IsTraceEnabled()) {
        D2D1_SIZE_F size = CachedClientSizeDip();
        TraceEvent("core_window", "on_paint_begin",
                   {TraceF64("client_w", size.width),
                    TraceF64("client_h", size.height),
                    TraceI64("depth", visualUpdateDepth_),
                    TraceBool("skip_vsync", renderer_.skipVSync)});
    }

    // 窗口开/关动画期间不重绘，DWM 负责缩放已有内容
    if (windowAnimating_) {
        ValidateRect(hwnd_, nullptr);
        return {};
    }

    // Consume any pending layout request from dynamic widget mutations
    // (v-if mount, v-for re-key, etc.) before drawing the new state.
    if (ui::LayoutDirtyFlag()) {
        TraceScope scope("core_window", "paint_dirty_layout_duration");
        ui::LayoutDirtyFlag() = false;
        LayoutRoot();
    }

    BeginAnimationFrame();

    // 移动期间正常渲染（不跳过）

    // Tick property animations
    size_t propertyAnimInput = propertyAnimations_.Count();
    bool animsRunning = propertyAnimations_.Tick();
    if (IsTraceEnabled() && propertyAnimInput > 0) {
        TraceEvent("core_window", "property_animation_frame",
                   {TraceI64("input_count", (int64_t)propertyAnimInput),
                    TraceI64("remaining_count", (int64_t)propertyAnimations_.Count()),
                    TraceBool("any_animating", animsRunning)});
    }
    if (ui::LayoutDirtyFlag()) {
        TraceScope scope("core_window", "paint_post_animation_layout_duration");
        ui::LayoutDirtyFlag() = false;
        LayoutRoot();
    }
    if (animsRunning) EnsureAnimationTimer();

    const bool recordDisplayList = canRecordDisplayList;
    DisplayListRecorder recorder;
    DisplayListRecorder* previousRecorder = nullptr;
    if (recordDisplayList) {
        DisplayList base;
        base.window_id = renderWindowId_.window_id;
        base.generation = frameGeneration;
        base.width_px = static_cast<int>(clientWidthPx_);
        base.height_px = static_cast<int>(clientHeightPx_);
        base.dpi_scale = dpiScale_;
        recorder.Reset(std::move(base));
        previousRecorder = renderer_.DisplayListRecorderTarget();
        renderer_.SetDisplayListRecorder(&recorder);
    }

    if (bgMode_ == 0) {
        renderer_.Clear(theme::kWindowBg());
    } else {
        renderer_.Clear({0, 0, 0, 0});
    }

    if (root_) {
        Viewport() = root_->rect;
        ActiveMeasureContextScope measureScope(measureContext_, renderer_);
        {
            TraceScope scope("core_window", "draw_tree_duration");
            root_->DrawTree(renderer_);
        }
        {
            TraceScope scope("core_window", "draw_overlays_duration");
            root_->DrawOverlays(renderer_);
        }
    }

    // Debug highlight: draw red border around widget with matching ID
    if (!debugHighlightId_.empty() && root_) {
        Widget* target = root_->FindById(debugHighlightId_);
        if (target) {
            D2D1_COLOR_F red = {1.0f, 0.0f, 0.0f, 0.9f};
            D2D1_RECT_F rc = target->rect;
            renderer_.DrawRect(rc, red, 2.0f);
            /* 内边距 1px 再画一圈，确保显眼 */
            D2D1_RECT_F inner = {rc.left + 2, rc.top + 2, rc.right - 2, rc.bottom - 2};
            D2D1_COLOR_F yellow = {1.0f, 1.0f, 0.0f, 0.5f};
            renderer_.DrawRect(inner, yellow, 1.0f);
        }
    }

    // Drag ghost: a small pill following the cursor showing the grabbed
    // item's display text (source label text — localized; falls back to the
    // raw payload, then "…"). Drawn above everything but the tooltip; kept
    // deliberately simple — targets signal acceptance via the :drag-over
    // pseudo-class instead of ghost styling.
    if (dragActive_) {
        std::wstring ghost = dragGhostText_.empty()
                                 ? (dragPayload_.empty() ? L"…" : dragPayload_)
                                 : dragGhostText_;
        if (ghost.size() > 32) ghost = ghost.substr(0, 31) + L"…";
        float fontSize = theme::kFontSizeSmall;
        float padH = 8.0f, padV = 4.0f;
        float textW = fontSize * 0.65f * static_cast<float>(ghost.size());
        {
            IDWriteFactory* dwf = renderer_.DWFactory();
            IDWriteTextFormat* fmt = nullptr;
            if (dwf) {
                dwf->CreateTextFormat(theme::kFontFamily, nullptr,
                    DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL, fontSize, L"", &fmt);
                if (fmt) {
                    IDWriteTextLayout* layout = nullptr;
                    dwf->CreateTextLayout(ghost.c_str(), (UINT32)ghost.size(),
                        fmt, 1000.0f, 100.0f, &layout);
                    if (layout) {
                        DWRITE_TEXT_METRICS metrics = {};
                        layout->GetMetrics(&metrics);
                        textW = metrics.width;
                        layout->Release();
                    }
                    fmt->Release();
                }
            }
        }
        float gw = textW + padH * 2, gh = fontSize + padV * 2 + 4.0f;
        // Centered on the cursor — matches how the user "holds" the item.
        float gx = dragX_ - gw * 0.5f, gy = dragY_ - gh * 0.5f;
        D2D1_SIZE_F winSize = CachedClientSizeDip();
        if (gx + gw > winSize.width - 2)  gx = winSize.width - 2 - gw;
        if (gy + gh > winSize.height - 2) gy = winSize.height - 2 - gh;
        if (gx < 2) gx = 2;
        if (gy < 2) gy = 2;
        D2D1_RECT_F bg = {gx, gy, gx + gw, gy + gh};
        D2D1_COLOR_F bgColor, borderColor;
        if (theme::IsDark()) {
            bgColor     = {0.20f, 0.22f, 0.30f, 0.92f};
            borderColor = {0.35f, 0.45f, 0.75f, 0.90f};
        } else {
            bgColor     = {0.93f, 0.95f, 1.00f, 0.95f};
            borderColor = {0.45f, 0.55f, 0.85f, 0.90f};
        }
        renderer_.FillRoundedRect(bg, gh * 0.5f, gh * 0.5f, bgColor);
        renderer_.DrawRoundedRect(bg, gh * 0.5f, gh * 0.5f, borderColor, 1.0f);
        D2D1_RECT_F textRect = {gx, gy + padV, gx + gw, gy + gh - padV};
        renderer_.DrawText(ghost, textRect, theme::kBtnText(), fontSize,
                           DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // Tooltip rendering
    if (tooltipVisible_ && tooltipWidget_ && !tooltipWidget_->tooltip.empty()) {
        const auto& text = tooltipWidget_->tooltip;
        float fontSize = theme::kFontSizeSmall;
        float padH = 10.0f, padV = 5.0f;

        /* 在 BeginDraw 之外测量文字宽度（避免 RT 状态影响） */
        float textW = fontSize * 0.65f * static_cast<float>(text.size());  /* 估算 */
        /* 用 DWrite 精确测量 */
        {
            IDWriteFactory* dwf = renderer_.DWFactory();
            IDWriteTextFormat* fmt = nullptr;
            if (dwf) {
                dwf->CreateTextFormat(theme::kFontFamily, nullptr,
                    DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL, fontSize, L"", &fmt);
                if (fmt) {
                    IDWriteTextLayout* layout = nullptr;
                    dwf->CreateTextLayout(text.c_str(), (UINT32)text.size(),
                        fmt, 1000.0f, 100.0f, &layout);
                    if (layout) {
                        DWRITE_TEXT_METRICS metrics = {};
                        layout->GetMetrics(&metrics);
                        textW = metrics.width;
                        layout->Release();
                    }
                    fmt->Release();
                }
            }
        }

        float tipW = textW + padH * 2;
        float tipH = fontSize + padV * 2 + 4.0f;

        /* 在 widget 下方居中显示 */
        float cx = (tooltipWidget_->rect.left + tooltipWidget_->rect.right) * 0.5f;
        float tipX = cx - tipW * 0.5f;
        float tipY = tooltipWidget_->rect.bottom + 4.0f;

        /* 确保不超出窗口 */
        D2D1_SIZE_F winSize = CachedClientSizeDip();
        if (tipX + tipW > winSize.width - 4) tipX = winSize.width - 4 - tipW;
        if (tipX < 4) tipX = 4;
        if (tipY + tipH > winSize.height - 4) {
            tipY = tooltipWidget_->rect.top - tipH - 4.0f;
        }

        D2D1_RECT_F bg = {tipX, tipY, tipX + tipW, tipY + tipH};
        /* 跟随主题：深色模式深底，浅色模式浅底 */
        D2D1_COLOR_F bgColor, borderColor;
        if (theme::IsDark()) {
            bgColor     = {0.15f, 0.15f, 0.18f, 0.95f};
            borderColor = {0.30f, 0.30f, 0.35f, 0.80f};
        } else {
            bgColor     = {0.98f, 0.98f, 0.98f, 0.97f};
            borderColor = {0.70f, 0.70f, 0.72f, 0.80f};
        }
        renderer_.FillRoundedRect(bg, 4.0f, 4.0f, bgColor);
        renderer_.DrawRoundedRect(bg, 4.0f, 4.0f, borderColor, 0.5f);

        D2D1_RECT_F textRect = {tipX, tipY + padV, tipX + tipW, tipY + tipH - padV};
        renderer_.DrawText(text, textRect, theme::kBtnText(), fontSize,
                           DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    if (borderless_ && !maximized_) {
        D2D1_SIZE_F size = CachedClientSizeDip();
        D2D1_RECT_F border = {0, 0, size.width, size.height};
        renderer_.DrawRect(border, theme::kWindowBorder(), theme::kBorderWidth);
    }

    // Toast notification is owned by OverlayService and is not recorded into the main window.

    if (recordDisplayList) {
        renderer_.SetDisplayListRecorder(previousRecorder);
    }

    /* loading spinner 持续动画：检查是否有 ImageView 处于 loading 状态 */
    if (root_) {
        bool anyLoading = false;
        ForEachWidget(root_.get(), [&](Widget* w) {
            auto* iv = dynamic_cast<ImageViewWidget*>(w);
            if (iv && iv->IsLoading()) anyLoading = true;
        });
        if (anyLoading) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    if (startupRevealPending_ && !startupRevealPosted_) {
        startupRevealPosted_ = true;
        PostMessageW(hwnd_, kMsgStartupReveal, 0, 0);
    }
    TraceEvent("core_window", "on_paint_end");
    if (recordDisplayList) {
        return recorder.Finish();
    }
    return {};
}

void UiWindowImpl::OnResize(UINT width, UINT height) {
    TraceScope resizeScope("core_window", "on_resize_duration");
    const bool skipPreparedCallback =
        preparedVisualResizeNotified_ &&
        preparedVisualResizeWPx_ == width &&
        preparedVisualResizeHPx_ == height;
    if (preparedVisualResizeNotified_) {
        preparedVisualResizeNotified_ = false;
        preparedVisualResizeWPx_ = 0;
        preparedVisualResizeHPx_ = 0;
    }
    UpdateClientSizeCache(width, height);
    {
        TraceEvent("core_window", "on_resize",
                   {TraceU64("w_px", width),
                    TraceU64("h_px", height),
                    TraceBool("skip_prepared_callback", skipPreparedCallback),
                    TraceI64("depth", visualUpdateDepth_)});
    }
    {
        TraceScope scope("core_window", "on_resize_layout_duration");
        LayoutRoot();
    }
    if (!skipPreparedCallback) {
        TraceScope scope("core_window", "on_resize_callback_duration");
        NotifyResizeCallback(width, height);
    } else {
        TraceEvent("core_window", "on_resize_callback_skipped_prepared",
                   {TraceU64("w_px", width),
                    TraceU64("h_px", height)});
    }
}

void UiWindowImpl::NotifyResizeCallback(UINT width, UINT height) {
    /* L91: 回调单位补统一为 DIP. width/height 来自 WM_SIZE = 物理像素, 但
     * ui_window_set_size / ui_widget_get_rect / 窗口几何 API 都已是 DIP
     * (L6/L23 v1.2.0 统一过, 当时漏了本回调). 转成 DIP 跟它们一致, 消费者
     * (如无边框看图记忆窗口长边) 拿到的尺寸跟 set_size 同单位, 高 DPI 屏不再
     * 错乘 dpiScale_. */
    if (onResize) {
        TraceScope scope("core_window", "notify_resize_callback_duration");
        const float s = dpiScale_ > 0.0f ? dpiScale_ : 1.0f;
        onResize((int)(width / s + 0.5f), (int)(height / s + 0.5f));
    }
    if (onPageResize) {
        TraceScope scope("core_window", "notify_page_resize_duration");
        const float s = dpiScale_ > 0.0f ? dpiScale_ : 1.0f;
        onPageResize((int)(width / s + 0.5f), (int)(height / s + 0.5f));
    }
}

void UiWindowImpl::OnDpiChanged(UINT dpi, const RECT* suggested) {
    dpi_ = dpi;
    dpiScale_ = (float)dpi / 96.0f;
    SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                 suggested->right - suggested->left, suggested->bottom - suggested->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    RefreshClientSizeCache();
}

// ---- Mouse events ----

void UiWindowImpl::ShowMenu(ContextMenuPtr menu, float x, float y) {
    if (activeMenu_) activeMenu_->Close();
    activeMenu_ = std::move(menu);
    if (activeMenu_ && hwnd_) {
        // Convert widget coords to screen coords
        POINT pt = {(LONG)(x * dpiScale_), (LONG)(y * dpiScale_)};
        ClientToScreen(hwnd_, &pt);
        activeMenu_->ShowPopup(hwnd_, pt.x, pt.y);
    }
}

void UiWindowImpl::CloseMenu() {
    if (activeMenu_) { activeMenu_->Close(); activeMenu_ = nullptr; }
    Invalidate();
}

// Fill DOM MouseEvent-style state (buttons bitmask + modifier keys) on an
// event about to be dispatched to @mouse* hooks. `button` is the DOM button
// index that caused the event (0 left / 1 middle / 2 right, -1 for move/wheel).
static void StampDomMouseState(MouseEvent& e, int button) {
    e.button  = button;
    e.buttons = ((GetKeyState(VK_LBUTTON) & 0x8000) ? 1 : 0) |
                ((GetKeyState(VK_RBUTTON) & 0x8000) ? 2 : 0) |
                ((GetKeyState(VK_MBUTTON) & 0x8000) ? 4 : 0);
    e.ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    e.shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
    e.alt   = (GetKeyState(VK_MENU)    & 0x8000) != 0;
}

void UiWindowImpl::OnMouseMove(float x, float y) {
    ActiveMeasureContextScope measureScope(measureContext_, renderer_);
    float dx = x / dpiScale_, dy = y / dpiScale_;
    bool repaintNow = false;

    // If a popup menu is open, clicking in the main window should close it
    // (the popup's WM_ACTIVATE handler closes it when focus shifts)

    if (pressedWidget_) {
        MouseEvent e{dx, dy, 0, true};
        StampDomMouseState(e);

        // Drag & drop state machine (armed in OnMouseDown).
        if (dragSource_) {
            if (!dragActive_) {
                float mdx = dx - dragStartX_, mdy = dy - dragStartY_;
                if (mdx * mdx + mdy * mdy >=
                    kDragThresholdDip * kDragThresholdDip) {
                    dragActive_ = true;
                    SetCapture(hwnd_);
                    dragPayload_ = dragSource_->dragData;
                    // Ghost pill shows what the user grabbed: the source
                    // label's visible (possibly localized) text. The raw
                    // drag-data key is a machine payload — only fall back to
                    // it when the source has no display text.
                    dragGhostText_.clear();
                    if (auto* lb = dynamic_cast<LabelWidget*>(dragSource_))
                        dragGhostText_ = lb->Text();
                    if (dragGhostText_.empty()) dragGhostText_ = dragPayload_;
                    if (dragSource_->onDragStartHook)
                        dragSource_->onDragStartHook(e, dragPayload_);
                }
            }
            if (dragActive_) {
                dragX_ = dx; dragY_ = dy;
                UpdateDragOverTarget(e);
                InvalidateNow();  // ghost follows the cursor
            }
        }

        // Pointer-capture semantics: during a press the captured widget keeps
        // receiving @mousemove even when the cursor is over other widgets —
        // required for script-driven drags to work like DOM setPointerCapture.
        if (pressedWidget_->onMouseMoveHook) pressedWidget_->onMouseMoveHook(e);
        // hook 可能跑模态循环 (宿主在 @mousemove 里起 OLE DoDragDrop 拖出):
        // DoDragDrop 内部 SetCapture 夺走 capture → WM_CAPTURECHANGED 同步派发
        // → CancelMouseCapture() 清空 pressedWidget_. 返回后必须重查, 否则
        // 下一行空指针崩溃 (GuoheView 拖出图片到其他窗口即触发).
        if (!pressedWidget_) { Invalidate(); return; }
        const bool handled = pressedWidget_->OnMouseMove(e);
        // Captured drags can produce a continuous stream of WM_MOUSEMOVE. If
        // we only invalidate, WM_PAINT may not run until the mouse stops, so
        // the visual state jumps to the final position. Keep this generic for
        // all pressed widgets that explicitly handle drag moves.
        if (handled) InvalidateNow();
        else Invalidate();
        return;
    }

    if (root_) {
        auto* hit = root_->HitTest(dx, dy);
        if (hoveredWidget_ != hit) {
            repaintNow = true;
            // Hover propagation: every widget in the new hit's ancestor chain
            // is "hovered", every widget in the old chain that's no longer in
            // the new chain leaves hover. Mirrors CSS :hover semantics so a
            // div with `@click` / `:hover` styles fires regardless of which
            // descendant the mouse actually landed on.
            std::unordered_set<Widget*> oldChain, newChain;
            for (Widget* w = hoveredWidget_; w; w = w->Parent()) oldChain.insert(w);
            for (Widget* w = hit; w; w = w->Parent()) newChain.insert(w);
            for (Widget* w : oldChain) {
                if (newChain.count(w)) continue;
                w->hovered = false;
                w->RefreshCssState();
                if (w->onMouseLeaveHook) w->onMouseLeaveHook();
            }
            for (Widget* w : newChain) {
                if (oldChain.count(w)) continue;
                w->hovered = true;
                w->RefreshCssState();
                if (w->onMouseEnterHook) {
                    MouseEvent enterE{dx, dy};
                    StampDomMouseState(enterE);
                    w->onMouseEnterHook(enterE);
                }
            }
            if (hoveredWidget_) {
                MouseEvent leaveE{dx, dy};
                if (hoveredWidget_->OnMouseMove(leaveE)) repaintNow = true;
            }
            hoveredWidget_ = hit;

            // Tooltip: reset on widget change, schedule timer for delayed show.
            // 沿父链上溯找最近一个有 tooltip 的祖先 (跟 hover 状态传播 + cursor
            // 继承一致) —— 容器 widget set_tooltip 后, hover 其内部子 widget
            // (如图标按钮的 svg 子级) 也能弹 (L72).
            tooltipVisible_ = false;
            tooltipWidget_ = nullptr;
            if (tooltipTimerId_) { KillTimer(hwnd_, tooltipTimerId_); tooltipTimerId_ = 0; }
            Widget* tipOwner = hit;
            while (tipOwner && tipOwner->tooltip.empty()) tipOwner = tipOwner->Parent();
            if (tipOwner) {
                hoverStartTick_ = GetTickCount();
                tooltipTimerId_ = SetTimer(hwnd_, kTooltipTimerId, kTooltipDelayMs, nullptr);
            }
        }

        mouseX_ = dx; mouseY_ = dy;

        // Forward move to hovered widget (for internal tracking like tab hover).
        // Fire the user-bound @mousemove hook first — many widget subclasses
        // override OnMouseMove without forwarding to Widget::OnMouseMove, so
        // we can't rely on the base class to fire onMouseMoveHook. Doing it
        // here means @mousemove on any widget works regardless of subclass.
        if (hit) {
            MouseEvent e{dx, dy};
            StampDomMouseState(e);
            if (hit->onMouseMoveHook) hit->onMouseMoveHook(e);
            if (hit->OnMouseMove(e)) repaintNow = true;
        }

        ForEachWidget(root_.get(), [&](Widget* w) {
            auto* cb = dynamic_cast<ComboBoxWidget*>(w);
            if (cb && cb->IsOpen()) {
                MouseEvent e{dx, dy};
                if (cb->OnMouseMove(e)) repaintNow = true;
            }
        });

        // Update ScrollView scrollbar hover state (overlay scrollbar)
        ForEachWidget(root_.get(), [&](Widget* w) {
            auto* sv = dynamic_cast<ScrollViewWidget*>(w);
            if (sv && sv->visible && sv->NeedsScrollbar()) {
                MouseEvent e{dx, dy};
                if (sv->OnMouseMove(e)) repaintNow = true;
            }
        });
    }
    if (repaintNow || HasPendingPaint()) InvalidateNow();
    else Invalidate();
    UpdateToggleAnimTimer();  // start timer if Slider thumb needs animation

    TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
    TrackMouseEvent(&tme);
}

void UiWindowImpl::OnMouseDown(float x, float y) {
    ActiveMeasureContextScope measureScope(measureContext_, renderer_);
    float dx = x / dpiScale_, dy = y / dpiScale_;

    // Hide tooltip on click
    tooltipVisible_ = false; tooltipWidget_ = nullptr;
    if (tooltipTimerId_) { KillTimer(hwnd_, tooltipTimerId_); tooltipTimerId_ = 0; }

    // Close any open context menu on left click
    if (activeMenu_) CloseMenu();

    if (!root_) return;

    // Set focus to clicked focusable widget, or clear focus
    // Mouse click hides focus ring (only keyboard Tab shows it)
    {
        showFocusRing_ = false;
        ShowFocusRing() = false;  // mouse click hides focus ring
        Widget* hit = root_->HitTest(dx, dy);
        Widget* target = hit;
        while (target && !target->focusable) target = target->Parent();
        SetFocus(target);

        // Clear NumberBox focus when clicking elsewhere
        ForEachWidget(root_.get(), [&](Widget* w) {
            auto* nb = dynamic_cast<NumberBoxWidget*>(w);
            if (nb && nb != target && nb != hit) {
                if (nb->focused) { nb->focused = false; }
            }
        });
    }

    // Check open ComboBox dropdowns
    {
        bool handledByDropdown = false;
        ForEachWidget(root_.get(), [&](Widget* w) {
            if (handledByDropdown) return;
            auto* cb = dynamic_cast<ComboBoxWidget*>(w);
            if (!cb || !cb->IsOpen()) return;

            const int dropdownIndex = cb->DropdownItemAt(dx, dy);
            bool inDropdown = dropdownIndex >= 0;
            bool inCombo = cb->Contains(dx, dy);

            if (inDropdown) {
                cb->SetSelectedIndex(dropdownIndex);
                if (cb->onSelectionChanged) cb->onSelectionChanged(dropdownIndex);
                cb->Close();
                handledByDropdown = true;
            } else if (inCombo) {
                cb->Close();
                handledByDropdown = true;
            } else {
                cb->Close();
            }
        });

        if (handledByDropdown) {
            UpdateCaretBlinkTimer();
            InvalidateNow();
            return;
        }
    }

    // Check open Flyouts — dismiss on outside click, forward on inside click
    {
        bool handledByFlyout = false;
        ForEachWidget(root_.get(), [&](Widget* w) {
            if (handledByFlyout) return;
            auto* fw = dynamic_cast<FlyoutWidget*>(w);
            if (!fw || !fw->IsOpen()) return;

            MouseEvent e{dx, dy, 0, true};
            if (fw->OnMouseDown(e)) {
                handledByFlyout = true;
                pressedWidget_ = fw;  // so OnMouseUp reaches the flyout
            }
        });

        if (handledByFlyout) {
            UpdateCaretBlinkTimer();
            InvalidateNow();
            return;
        }
    }

    // Check ScrollView scrollbar clicks (overlay scrollbar area)
    {
        bool handledByScrollbar = false;
        ForEachWidget(root_.get(), [&](Widget* w) {
            if (handledByScrollbar) return;
            auto* sv = dynamic_cast<ScrollViewWidget*>(w);
            if (!sv || !sv->visible || !sv->NeedsScrollbar()) return;
            if (!sv->Contains(dx, dy)) return;
            // Only intercept clicks in the scrollbar track region
            if (dx < sv->rect.right - 10) return;

            MouseEvent e{dx, dy, 0, true};
            if (sv->OnMouseDown(e)) {
                handledByScrollbar = true;
                pressedWidget_ = sv;
                SetCapture(hwnd_);
            }
        });

        if (handledByScrollbar) {
            UpdateCaretBlinkTimer();
            InvalidateNow();
            return;
        }
    }

    // Hit test and dispatch
    auto* hit = root_->HitTest(dx, dy);

    // Check if a Splitter bar was clicked (Splitter intercepts before children)
    {
        Widget* w = hit;
        while (w) {
            auto* sp = dynamic_cast<SplitterWidget*>(w);
            if (sp) {
                MouseEvent e{dx, dy, 0, true};
                if (sp->OnMouseDown(e)) {
                    pressedWidget_ = sp;
                    SetCapture(hwnd_);
                    InvalidateNow();
                    return;
                }
            }
            w = w->Parent();
        }
    }

    if (hit) {
        hit->pressed = true;
        hit->RefreshCssState();
        pressedWidget_ = hit;

        // Drag & drop: arm the nearest draggable ancestor. The drag only
        // starts once the move exceeds kDragThresholdDip (see OnMouseMove),
        // so plain clicks on draggable widgets keep working.
        dragSource_ = nullptr;
        dragActive_ = false;
        for (Widget* w = hit; w; w = w->Parent()) {
            if (w->draggable) {
                dragSource_ = w;
                dragStartX_ = dx; dragStartY_ = dy;
                break;
            }
        }

        if (dynamic_cast<SliderWidget*>(hit) ||
            dynamic_cast<ScrollViewWidget*>(hit) ||
            dynamic_cast<ImageViewWidget*>(hit) ||
            dynamic_cast<ImageViewPlusWidget*>(hit) ||
            dynamic_cast<GhImgViewWidget*>(hit)) {
            SetCapture(hwnd_);
        }

        MouseEvent e{dx, dy, 0, true};
        StampDomMouseState(e, 0);
        // Fire @mousedown hook before the widget's own logic — see
        // OnMouseMove for the rationale (subclasses don't all forward).
        if (hit->onMouseDownHook) hit->onMouseDownHook(e);
        hit->OnMouseDown(e);
    }
    UpdateCaretBlinkTimer();
    InvalidateNow();
}

void UiWindowImpl::CancelMouseCapture() {
    // capture 被外部夺走 → press 中的 widget 不会再收到 WM_LBUTTONUP. 等价一次
    // "取消": 复位 pressedWidget_ + widget 内部 drag 状态, 但不 fire onClick
    // (不是真正的点击释放). 不调 ReleaseCapture — capture 已易主.
    if (!pressedWidget_) return;
    // An in-flight drag ends without a drop when capture is stolen.
    if (dragActive_) {
        MouseEvent cancelE{dragX_, dragY_};
        FinishDrag(false, cancelE);
    }
    dragSource_ = nullptr;
    WidgetPtr keepAlive = pressedWidget_->shared_from_this();
    Widget* w = pressedWidget_;
    pressedWidget_ = nullptr;
    w->pressed = false;
    w->RefreshCssState();
    // 让 widget 复位自身 drag 态 (gh_img_view dragging_ / slider / scrollview 等).
    // 走到这里的 pressedWidget_ 必是 SetCapture 过的拖拽类 widget, 其 OnMouseUp
    // 无 onClick 自触发语义, 安全. 坐标无意义 (拖拽类 OnMouseUp 不读坐标).
    MouseEvent e{0.0f, 0.0f, 0, false};
    if (w->onMouseUpHook) w->onMouseUpHook(e);
    w->OnMouseUp(e);
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    InvalidateNow();
}

void UiWindowImpl::OnMouseDoubleClick(float x, float y) {
    if (!root_) return;
    // The system already delivers WM_LBUTTONDOWN before WM_LBUTTONDBLCLK,
    // so press/release/focus are handled there — here we only need the
    // hit-test → widget dispatch for @dblclick listeners. Modal dialogs
    // and active menus still swallow input.
    if (activeMenu_) return;

    float dx = x / dpiScale_, dy = y / dpiScale_;
    Widget* hit = root_->HitTest(dx, dy);
    if (!hit) return;
    MouseEvent e{dx, dy, 0, true};
    StampDomMouseState(e, 0);
    // Fire @dblclick hook on the hit widget before bubbling — see
    // OnMouseMove for the rationale (subclasses don't all forward).
    if (hit->onMouseDblClickHook) hit->onMouseDblClickHook(e);
    // Walk up to the first widget that consumes the dblclick, mirroring
    // how onClick bubbles through wrapper layouts.
    for (Widget* w = hit; w; w = w->Parent()) {
        if (w->OnMouseDoubleClick(e)) break;
    }
    InvalidateNow();
}

void UiWindowImpl::OnMouseUp(float x, float y) {
    float dx = x / dpiScale_, dy = y / dpiScale_;

    if (pressedWidget_) {
        // Lifetime guard: an @click handler may end up unmounting THIS
        // widget (e.g. v-for row's "delete" button calls remove(item),
        // which rebuilds the loop and frees the iteration's widget tree).
        // The shared_ptr below keeps the widget alive through the
        // onClick chain so subsequent dynamic_cast / member access don't
        // hit a freed object. Once the local goes out of scope the
        // widget actually destructs.
        WidgetPtr keepAlive = pressedWidget_->shared_from_this();
        Widget* w = pressedWidget_;
        pressedWidget_ = nullptr;

        w->pressed = false;
        w->RefreshCssState();

        // Snapshot hit-test BEFORE OnMouseUp — OnMouseUp may trigger layout
        // changes (e.g. Expander expanding) that move w out from under the
        // cursor.
        bool hitWidget = w->Contains(dx, dy);

        // Run widget's own OnMouseUp FIRST so stateful widgets (Toggle,
        // CheckBox, RadioButton) flip their internal state before the
        // @click handler fires. This way handler can read the post-click
        // value via On()/IsChecked() — matches HTML form input semantics
        // (DOM `click` fires after the checkbox state has flipped).
        // Note: ButtonWidget::OnMouseUp also has a self-fire path, but it's
        // gated on `pressed` which we cleared above, so no double-fire.
        MouseEvent e{dx, dy, 0, false};
        StampDomMouseState(e, 0);

        // Finish an active drag before the widget's own OnMouseUp. A release
        // that ends a drag never counts as a click (DOM: dragend suppresses
        // click on the source).
        const bool wasDrag = dragActive_;
        if (dragActive_) {
            FinishDrag(true, e);
            ReleaseCapture();  // drag start SetCapture'd for any source type
        }
        dragSource_ = nullptr;

        // Fire @mouseup hook before subclass dispatch (subclasses don't all
        // forward to Widget::OnMouseUp).
        if (w->onMouseUpHook) w->onMouseUpHook(e);
        w->OnMouseUp(e);

        // Event bubbling: walk up the parent chain to find the first widget
        // with an onClick handler. This mirrors Web semantics so clicking on
        // any descendant of a div with @click="..." fires the div's handler.
        if (hitWidget && !wasDrag) {
            Widget* target = w;
            while (target && !target->onClick) target = target->Parent();
            if (target && target->onClick) target->onClick();
        }

        if (dynamic_cast<SliderWidget*>(w) ||
            dynamic_cast<ScrollViewWidget*>(w) ||
            dynamic_cast<ImageViewWidget*>(w) ||
            dynamic_cast<ImageViewPlusWidget*>(w) ||
            dynamic_cast<GhImgViewWidget*>(w) ||
            dynamic_cast<SplitterWidget*>(w)) {
            ReleaseCapture();
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
        }
        // keepAlive falls out of scope here; if the handler removed w from
        // its parent, this is where the actual destruction happens.
    }

    UpdateToggleAnimTimer();
    InvalidateNow();
}

void UiWindowImpl::OnMouseWheel(float x, float y, int delta) {
    float dx = x / dpiScale_, dy = y / dpiScale_;

    if (root_) {
        MouseEvent e{dx, dy, (float)delta};
        StampDomMouseState(e);

        // An open ComboBox owns the wheel while the pointer is inside its
        // overlay. The overlay is not part of the widget tree, so normal
        // HitTest would otherwise deliver this wheel event to the ScrollView
        // or other widget behind it.
        bool handledByDropdown = false;
        ForEachWidget(root_.get(), [&](Widget* w) {
            if (handledByDropdown) return;
            auto* cb = dynamic_cast<ComboBoxWidget*>(w);
            if (cb && cb->IsOpen() && cb->OnMouseWheel(e)) {
                handledByDropdown = true;
            }
        });
        if (handledByDropdown) {
            InvalidateNow();
            return;
        }

        // HitTest to find deepest widget, then bubble up looking for scroll handler
        Widget* hit = root_->HitTest(dx, dy);
        bool handled = false;
        bool hookFired = false;

        // Fire @wheel hook on the hit widget (subclass overrides may not
        // forward to Widget::OnMouseWheel).
        if (hit && hit->onMouseWheelHook) {
            hookFired = true;
            hit->onMouseWheelHook(e);
        }

        for (Widget* w = hit; w && !handled; w = w->Parent()) {
            if (auto* ta = dynamic_cast<TextAreaWidget*>(w)) {
                if (ta->visible && ta->NeedsScrollbar()) {
                    handled = ta->OnMouseWheel(e);
                }
            }
            else if (auto* iv = dynamic_cast<ImageViewWidget*>(w)) {
                if (iv->visible) {
                    handled = iv->OnMouseWheel(e);
                }
            }
            else if (auto* ivp = dynamic_cast<ImageViewPlusWidget*>(w)) {
                if (ivp->visible) {
                    handled = ivp->OnMouseWheel(e);
                }
            }
            else if (auto* gv = dynamic_cast<GhImgViewWidget*>(w)) {
                if (gv->visible) {
                    handled = gv->OnMouseWheel(e);
                }
            }
            else if (auto* ov = dynamic_cast<OcrImgViewWidget*>(w)) {
                if (ov->visible) {
                    handled = ov->OnMouseWheel(e);
                }
            }
            else if (auto* sv = dynamic_cast<ScrollViewWidget*>(w)) {
                if (sv->visible) {
                    handled = sv->OnMouseWheel(e);
                    if (handled) sv->DoLayout();
                }
            }
        }
        if (handled || hookFired) {
            // Wheel handlers can produce a continuous stream of state changes
            // just like captured drag moves. Force the current window to paint
            // the latest zoom/scroll state instead of waiting for a later click
            // or low-priority WM_PAINT dispatch.
            InvalidateNow();
        }
    }
}

// ---- Debug simulation (DIP coords → pixel → existing private handlers) ----

void UiWindowImpl::SimMouseMove(float dipX, float dipY) {
    OnMouseMove(dipX * dpiScale_, dipY * dpiScale_);
}
void UiWindowImpl::SimMouseDown(float dipX, float dipY) {
    OnMouseDown(dipX * dpiScale_, dipY * dpiScale_);
}
void UiWindowImpl::SimMouseUp(float dipX, float dipY) {
    OnMouseUp(dipX * dpiScale_, dipY * dpiScale_);
}
void UiWindowImpl::SimMouseWheel(float dipX, float dipY, float delta) {
    OnMouseWheel(dipX * dpiScale_, dipY * dpiScale_, (int)delta);
}
void UiWindowImpl::SimMouseDoubleClick(float dipX, float dipY) {
    /* 复刻真实 Win32 序列: 第一次完整 click, 然后系统用 WM_LBUTTONDBLCLK
     * 顶替第二次 WM_LBUTTONDOWN, 最后一个 WM_LBUTTONUP 收尾。 */
    const float px = dipX * dpiScale_, py = dipY * dpiScale_;
    OnMouseMove(px, py);
    OnMouseDown(px, py);
    OnMouseUp(px, py);
    OnMouseDoubleClick(px, py);
    OnMouseUp(px, py);
}
// @contextmenu — DOM semantics: fires on right-button release, bubbles up to
// the nearest ancestor with a listener. A page-level listener does NOT
// suppress the window's onRightClick host callback (hosts that own a native
// context menu keep working unchanged). Shared by WM_RBUTTONUP and
// SimRightClick so simulated right clicks exercise the same path.
void UiWindowImpl::DispatchContextMenu(float dx, float dy) {
    if (!root_) return;
    Widget* hit = root_->HitTest(dx, dy);
    for (Widget* w = hit; w; w = w->Parent()) {
        if (w->onContextMenuHook) {
            MouseEvent e{dx, dy};
            StampDomMouseState(e, 2);
            w->onContextMenuHook(e);
            break;
        }
    }
}

// While a drag is active: hit-test under the cursor, resolve the nearest
// ancestor that accepts drops, and maintain enter/over/leave + the widget
// `dragOver` flag (CSS :drag-over). The drag source's own subtree is skipped
// as a target — dropping something onto itself is a no-op.
void UiWindowImpl::UpdateDragOverTarget(const MouseEvent& e) {
    Widget* target = nullptr;
    if (root_) {
        Widget* hit = root_->HitTest(e.x, e.y);
        for (Widget* w = hit; w; w = w->Parent()) {
            if (w == dragSource_) { target = nullptr; break; }
            if (!target && w->CanAcceptDrop()) target = w;
        }
    }
    if (target != dragOverTarget_) {
        if (dragOverTarget_) {
            dragOverTarget_->dragOver = false;
            dragOverTarget_->RefreshCssState();
            if (dragOverTarget_->onDragLeaveHook)
                dragOverTarget_->onDragLeaveHook();
        }
        dragOverTarget_ = target;
        if (dragOverTarget_) {
            dragOverTarget_->dragOver = true;
            dragOverTarget_->RefreshCssState();
            if (dragOverTarget_->onDragEnterHook)
                dragOverTarget_->onDragEnterHook(e);
        }
    }
    if (dragOverTarget_ && dragOverTarget_->onDragOverHook)
        dragOverTarget_->onDragOverHook(e);
}

// Ends the active drag: fires drop on the hovered target (if accepting and
// `dropped`), dragend on the source, and clears all drag state. Also used by
// CancelMouseCapture with dropped=false.
void UiWindowImpl::FinishDrag(bool dropped, const MouseEvent& e) {
    Widget* target = dragOverTarget_;
    Widget* source = dragSource_;
    dragOverTarget_ = nullptr;
    dragSource_ = nullptr;
    dragActive_ = false;
    bool accepted = false;
    if (target) {
        target->dragOver = false;
        target->RefreshCssState();
        if (dropped && target->onDropHook) {
            accepted = true;
            target->onDropHook(dragPayload_, e);
        }
    }
    if (source && source->onDragEndHook) source->onDragEndHook(accepted);
    dragPayload_.clear();
    dragGhostText_.clear();
    InvalidateNow();
}

void UiWindowImpl::UpdateImeCompositionPos() {
    if (!hwnd_ || !focusedWidget_) return;
    ActiveMeasureContextScope measureScope(measureContext_, renderer_);
    D2D1_RECT_F cr{};
    if (!focusedWidget_->GetCaretRect(&cr)) return;
    HIMC imc = ImmGetContext(hwnd_);
    if (!imc) return;
    POINT pt = {static_cast<LONG>(cr.left * dpiScale_),
                static_cast<LONG>(cr.bottom * dpiScale_) + 2};
    COMPOSITIONFORM cf{};
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos = pt;
    ImmSetCompositionWindow(imc, &cf);
    CANDIDATEFORM cd{};
    cd.dwIndex = 0;
    cd.dwStyle = CFS_CANDIDATEPOS;
    cd.ptCurrentPos = pt;
    ImmSetCandidateWindow(imc, &cd);
    ImmReleaseContext(hwnd_, imc);
}

void UiWindowImpl::SimRightClick(float dipX, float dipY) {
    // Matches WM_RBUTTONUP: contextmenu hook dispatch + onRightClick callback.
    DispatchContextMenu(dipX, dipY);
    if (onRightClick) onRightClick(dipX, dipY);
    InvalidateNow();
}
void UiWindowImpl::SimKeyDown(int vk) {
    /* 必须跟真实 WM_KEYDOWN 一样, 控件不消费时下传窗口级 onKey —— 应用级
     * 快捷键 (ui_window_on_key: 翻页 / 缩放 / 切图…) 全挂在那上面。只调
     * DispatchKeyDown 的话这些快捷键在自动化里完全不可达。
     * onKey 可能销毁本窗口 (典型 Esc 关窗), 回调后不得再碰 this。 */
    if (DispatchKeyDown(vk)) return;
    if (onKey) {
        HWND h = hwnd_;
        onKey(vk);
        if (h && IsWindow(h)) {
            InvalidateRect(h, nullptr, FALSE);
            UpdateWindow(h);
        }
    }
}

// 共用的 WM_KEYDOWN 分发逻辑。返回 true 表示事件被消费。
bool UiWindowImpl::DispatchKeyDown(int vk) {
    ActiveMeasureContextScope measureScope(measureContext_, renderer_);

    // 1. Tab / Shift+Tab → focus traversal (only when enabled)
    if (vk == VK_TAB && tabNavigationEnabled_) {
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        showFocusRing_ = true;
        ShowFocusRing() = true;
        FocusNext(shift);
        InvalidateNow();
        return true;
    }

    // 2. Shortcuts (Ctrl+Key, Alt+Key) —— GetKeyState 读真实键盘状态，
    //    sim 注入时若没有同时按 Ctrl/Alt 这条分支会跳过，这是期望行为。
    if (!shortcuts_.empty()) {
        int mods = 0;
        if (GetKeyState(VK_CONTROL) & 0x8000) mods |= 1;  // MOD_CTRL
        if (GetKeyState(VK_SHIFT)   & 0x8000) mods |= 2;  // MOD_SHIFT
        if (GetKeyState(VK_MENU)    & 0x8000) mods |= 4;  // MOD_ALT
        for (auto& sc : shortcuts_) {
            if (sc.vk == vk && sc.modifiers == mods) {
                sc.callback();
                InvalidateNow();
                return true;
            }
        }
    }

    // 3. Enter / Space → activate focused widget
    if (focusedWidget_ && (vk == VK_RETURN || vk == VK_SPACE)) {
        if (auto* cb = dynamic_cast<CheckBoxWidget*>(focusedWidget_)) {
            if (vk == VK_SPACE) {
                cb->SetChecked(!cb->Checked());
                if (cb->onValueChanged) cb->onValueChanged(cb->Checked());
                UpdateToggleAnimTimer();
                InvalidateNow();
                return true;
            }
        }
        if (auto* tg = dynamic_cast<ToggleWidget*>(focusedWidget_)) {
            if (vk == VK_SPACE) {
                tg->SetOn(!tg->On());
                if (tg->onValueChanged) tg->onValueChanged(tg->On());
                UpdateToggleAnimTimer();
                InvalidateNow();
                return true;
            }
        }
        if (focusedWidget_->onClick) {
            focusedWidget_->onClick();
            InvalidateNow();
            return true;
        }
    }

    // 4. Arrow keys for Slider (left/right)
    if (focusedWidget_ && (vk == VK_LEFT || vk == VK_RIGHT)) {
        if (auto* sl = dynamic_cast<SliderWidget*>(focusedWidget_)) {
            float step = (vk == VK_RIGHT) ? 1.0f : -1.0f;
            sl->SetValue(sl->Value() + step);
            if (sl->onFloatChanged) sl->onFloatChanged(sl->Value());
            InvalidateNow();
            return true;
        }
    }

    // 5. Arrow keys for RadioButton group (up/down)
    if (focusedWidget_ && (vk == VK_UP || vk == VK_DOWN)) {
        if (auto* rb = dynamic_cast<RadioButtonWidget*>(focusedWidget_)) {
            Widget* parent = rb->Parent();
            if (parent) {
                std::vector<RadioButtonWidget*> group;
                for (auto& c : parent->Children()) {
                    auto* r = dynamic_cast<RadioButtonWidget*>(c.get());
                    if (r && r->Group() == rb->Group()) group.push_back(r);
                }
                int cur = 0;
                for (int i = 0; i < (int)group.size(); i++)
                    if (group[i] == rb) { cur = i; break; }
                int next = vk == VK_DOWN ? (cur + 1) % (int)group.size()
                                         : (cur - 1 + (int)group.size()) % (int)group.size();
                group[next]->SetSelected(true);
                if (group[next]->onValueChanged) group[next]->onValueChanged(true);
                SetFocus(group[next]);
                UpdateToggleAnimTimer();
                InvalidateNow();
                return true;
            }
        }
    }

    // 6. Escape → close ComboBox dropdown
    if (focusedWidget_ && vk == VK_ESCAPE) {
        if (auto* combo = dynamic_cast<ComboBoxWidget*>(focusedWidget_)) {
            if (combo->IsOpen()) { combo->Close(); InvalidateNow(); return true; }
        }
    }

    // 7. Forward to focused widget's generic OnKeyDown
    if (focusedWidget_ && focusedWidget_->OnKeyDown(vk)) {
        InvalidateNow();
        return true;
    }

    return false;
}

// ---- UI thread marshaling ----
void UiWindowImpl::InvokeSync(void (*fn)(void*), void* ud) {
    if (!fn) return;
    if (!hwnd_) { fn(ud); return; }
    // 若已在 UI 线程（拥有该 HWND 的线程），直接调用——避免 SendMessage 死锁。
    DWORD hwndTid = GetWindowThreadProcessId(hwnd_, nullptr);
    if (hwndTid == GetCurrentThreadId()) {
        fn(ud);
        return;
    }
    UiInvokeReq req{fn, ud};
    SendMessageW(hwnd_, WM_APP + 120, 0, reinterpret_cast<LPARAM>(&req));
}
void UiWindowImpl::SimKeyChar(wchar_t ch) {
    if (focusedWidget_) {
        ActiveMeasureContextScope measureScope(measureContext_, renderer_);
        if (focusedWidget_->OnKeyChar(ch)) {
            InvalidateNow();
        }
    }
}

void UiWindowImpl::OnDropFiles(HDROP hDrop) {
    wchar_t path[MAX_PATH]{};
    UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < count; i++) {
        if (DragQueryFileW(hDrop, i, path, MAX_PATH)) {
            if (onDrop) onDrop(path);
        }
    }
    DragFinish(hDrop);
}

// ---- NC handling (borderless) ----

LRESULT UiWindowImpl::OnNcCalcSize(WPARAM wParam, LPARAM lParam) {
    if (!wParam) return DefWindowProcW(hwnd_, WM_NCCALCSIZE, wParam, lParam);
    /* 无边框：客户区 = 整个窗口（不减非客户区）。
     * 最大化尺寸由 WM_GETMINMAXINFO 限制到工作区。 */
    return 0;
}

LRESULT UiWindowImpl::OnNcHitTest(int sx, int sy) {
    POINT pt = {sx, sy};
    ScreenToClient(hwnd_, &pt);
    float x = (float)pt.x / dpiScale_;
    float y = (float)pt.y / dpiScale_;
    if ((clientWidthPx_ == 0 || clientHeightPx_ == 0) && hwnd_) {
        RefreshClientSizeCache();
    }
    D2D1_SIZE_F size = CachedClientSizeDip();
    float w = size.width, h = size.height;
    float border = theme::kResizeBorder;

    if (!maximized_ && resizable_ && w > 0.0f && h > 0.0f) {
        bool l = x < border, r = x >= w - border, t = y < border, b = y >= h - border;

        // 四角和顶部/左右边缘始终优先 resize
        if (t && l) return HTTOPLEFT;    if (t && r) return HTTOPRIGHT;
        if (b && l) return HTBOTTOMLEFT; if (b && r) return HTBOTTOMRIGHT;
        if (t) return HTTOP;
        if (l) return HTLEFT;

        if (r) return HTRIGHT;

        /* 底部边缘 — Build 111 (L29 follow-up): 跟 left/right/top 一致, 始终
         * 优先 resize, 不让给 widget. 之前先做 HitTest 命中 widget 就让 HTCLIENT,
         * 主窗 toolbar / settings 窗 ScrollView 底部都被 widget 接走, 用户拖
         * 不到 frame resize. kResizeBorder 是 ~5-8px 极小一块, 让给 resize
         * 不会显著影响 widget 主体交互区 (button 通常 36px+, 边缘 5px 让出去
         * 视觉无感). */
        if (b) return HTBOTTOM;
    }

    // Check if hitting a widget
    if (root_) {
        auto* hit = root_->HitTest(x, y);
        if (hit && (hit != root_.get() || canvasMode_)) {
            if (dynamic_cast<TitleBarWidget*>(hit)) return HTCAPTION;
            /* dragWindow 属性沿 parent chain 向上查 — L53 修复:
             *
             * 1.2.0 引入 EnableCanvasMode 把 dragWindow=true 设在 root_ 上,
             * 期望 "整个画布可拖". 但 HitTest 走深度优先返叶子, 只检查
             * hit 自己的 dragWindow → 当 root 有交互子控件 (gh_img_view /
             * image_view / 普通 Panel) 时永远不会命中 root_, "整个画布可拖"
             * 等于零. 图片查看器等典型场景 (lib 自己 changelog 里点名的)
             * 用不了.
             *
             * 改成 ancestor walk: chain 上任一节点 dragWindow=true 即返
             * HTCAPTION. EnableCanvasMode 的 root_.dragWindow=true 现在
             * 真正等价于 "整个画布可拖", 符合 API 命名直觉.
             *
             * 安全: 1.2.0 至今 dragWindow 的唯一真实 setter 是 EnableCanvasMode
             * (作用 root), 没有外部 caller 自己设过中间节点, 不存在 "中间节点
             * dragWindow=true + 叶子是交互控件" 的现存用法被破坏. */
            for (Widget* w = hit; w != nullptr; w = w->Parent()) {
                /* hitClient 优先于 dragWindow: canvas mode 里叠在可拖画布上的
                 * 交互 overlay (如 borderless 关闭热区) 标记后拿回 HTCLIENT,
                 * 否则整棵树被 root 的 dragWindow 判成 HTCAPTION, widget 层
                 * 永远收不到 WM_MOUSEMOVE → :hover / @click 全部失效。 */
                if (w->hitClient) return HTCLIENT;
                if (w->dragWindow) return HTCAPTION;
            }
            return HTCLIENT;
        }
    }

    // Title bar region fallback (first 36px)
    if (y < theme::kTitleBarHeight) return HTCAPTION;
    return HTCLIENT;
}

bool UiWindowImpl::IsCanvasDragHit(int sx, int sy) const {
    if (!canvasMode_ || !root_ || !hwnd_) return false;

    POINT pt = {sx, sy};
    ScreenToClient(hwnd_, &pt);
    float x = (float)pt.x / dpiScale_;
    float y = (float)pt.y / dpiScale_;

    Widget* hit = root_->HitTest(x, y);
    if (!hit) {
        RECT client{};
        GetClientRect(hwnd_, &client);
        if (pt.x < client.left || pt.x >= client.right ||
            pt.y < client.top || pt.y >= client.bottom) {
            return false;
        }
        hit = root_.get();
    }

    for (Widget* w = hit; w != nullptr; w = w->Parent()) {
        if (dynamic_cast<TitleBarWidget*>(w)) return false;
        if (w->dragWindow) return true;
    }
    return false;
}

void UiWindowImpl::OnGetMinMaxInfo(MINMAXINFO* mmi) {
    int minW, minH;
    if (minWOverride_ > 0) {
        /* 用户显式覆盖（无边框画布等场景，可以小于主题默认） */
        minW = minWOverride_;
    } else {
        minW = (configWidth_ > 0 && configWidth_ < theme::kMinWidth) ? configWidth_ : theme::kMinWidth;
    }
    if (minHOverride_ > 0) {
        minH = minHOverride_;
    } else {
        minH = (configHeight_ > 0 && configHeight_ < theme::kMinHeight) ? configHeight_ : theme::kMinHeight;
    }
    mmi->ptMinTrackSize.x = (LONG)(minW * dpiScale_);
    mmi->ptMinTrackSize.y = (LONG)(minH * dpiScale_);

    /* 无边框窗口：限制最大化尺寸到工作区（扣除任务栏）。
     * 这样 WM_NCCALCSIZE 的 orig rect 就已经是工作区大小。 */
    if (borderless_ && hwnd_) {
        HMONITOR mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfoW(mon, &mi)) {
            mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
            mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
            mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
            mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
            if (canvasMode_) {
                LONG virtualW = (LONG)GetSystemMetrics(SM_CXVIRTUALSCREEN);
                LONG virtualH = (LONG)GetSystemMetrics(SM_CYVIRTUALSCREEN);
                mmi->ptMaxTrackSize.x = std::max({mmi->ptMaxTrackSize.x,
                                                  mmi->ptMaxSize.x,
                                                  virtualW,
                                                  kCanvasMaxTrackPx});
                mmi->ptMaxTrackSize.y = std::max({mmi->ptMaxTrackSize.y,
                                                  mmi->ptMaxSize.y,
                                                  virtualH,
                                                  kCanvasMaxTrackPx});
            }
        }
    }
}

bool UiWindowImpl::HasFocusedTextInput() const {
    if (!root_) return false;

    bool focused = false;
    ForEachWidget(root_.get(), [&](Widget* w) {
        if (focused) return;
        auto* ti = dynamic_cast<TextInputWidget*>(w);
        if (ti && ti->focused) { focused = true; return; }
        auto* ta = dynamic_cast<TextAreaWidget*>(w);
        if (ta && ta->focused) { focused = true; return; }
        auto* cw = dynamic_cast<CustomWidget*>(w);
        if (cw && cw->focused) { focused = true; return; }
        auto* ci = dynamic_cast<ChipsInputWidget*>(w);
        if (ci && ci->IsFocused()) { focused = true; return; }
    });
    return focused;
}

void UiWindowImpl::UpdateCaretBlinkTimer() {
    if (!hwnd_) return;

    bool needTimer = HasFocusedTextInput();
    if (needTimer && !caretBlinkTimerRunning_) {
        UINT blinkMs = TextInputWidget::EffectiveCaretBlinkMs();
        if (SetTimer(hwnd_, kCaretBlinkTimerId, blinkMs, nullptr) != 0) {
            caretBlinkTimerRunning_ = true;
        }
    } else if (!needTimer && caretBlinkTimerRunning_) {
        KillTimer(hwnd_, kCaretBlinkTimerId);
        caretBlinkTimerRunning_ = false;
    }
}

// ---- Focus management ----

void UiWindowImpl::SetFocus(Widget* w) {
    if (focusedWidget_ == w) return;
    if (focusedWidget_) {
        focusedWidget_->SetFocused(false);
        focusedWidget_->RefreshCssState();
        if (auto* ti = dynamic_cast<TextInputWidget*>(focusedWidget_)) ti->focused = false;
        if (auto* ta = dynamic_cast<TextAreaWidget*>(focusedWidget_)) ta->focused = false;
    }
    focusedWidget_ = w;
    if (w) {
        w->SetFocused(true);
        w->RefreshCssState();
        if (auto* ti = dynamic_cast<TextInputWidget*>(w)) { ti->focused = true; ti->ResetCaretBlink(); }
        if (auto* ta = dynamic_cast<TextAreaWidget*>(w)) { ta->focused = true; ta->ResetCaretBlink(); }
        if (auto* ci = dynamic_cast<ChipsInputWidget*>(w)) ci->ResetCaretBlink();
    }
    UpdateCaretBlinkTimer();
}

void UiWindowImpl::ClearFocus() {
    SetFocus(nullptr);
}

void UiWindowImpl::FocusWidget(Widget* w) {
    SetFocus(w);
    /* 亮焦点环 — 编程式设焦点视同键盘导航 (让用户看见焦点落在哪个按钮)。 */
    showFocusRing_ = true;
    ShowFocusRing() = true;
    Invalidate();
}

void UiWindowImpl::FocusNext(bool reverse) {
    if (!root_ || !tabNavigationEnabled_) return;
    std::vector<Widget*> chain;
    root_->CollectFocusable(chain);
    if (chain.empty()) return;

    // Sort by tabIndex if specified (stable sort preserves tree order for equal/unset)
    std::stable_sort(chain.begin(), chain.end(), [](Widget* a, Widget* b) {
        int ta = a->tabIndex < 0 ? 10000 : a->tabIndex;
        int tb = b->tabIndex < 0 ? 10000 : b->tabIndex;
        return ta < tb;
    });

    if (!focusedWidget_) {
        SetFocus(reverse ? chain.back() : chain.front());
        return;
    }

    // Find current index
    int cur = -1;
    for (int i = 0; i < (int)chain.size(); i++) {
        if (chain[i] == focusedWidget_) { cur = i; break; }
    }

    int next;
    if (reverse) {
        next = (cur <= 0) ? (int)chain.size() - 1 : cur - 1;
    } else {
        next = (cur < 0 || cur >= (int)chain.size() - 1) ? 0 : cur + 1;
    }
    SetFocus(chain[next]);
}

// ---- Shortcut registration ----

void UiWindowImpl::RegisterShortcut(int modifiers, int vk, std::function<void()> cb) {
    shortcuts_.push_back({modifiers, vk, std::move(cb)});
}

// ---- Toast ----

void UiWindowImpl::DestroyToast() {
    OverlayService::Instance().DismissOwner(hwnd_);
}

void UiWindowImpl::ShowToast(const std::wstring& text, int durationMs, int position, int icon, int anim) {
    if (!hwnd_) return;
    OverlayService::Instance().ShowToast(hwnd_, text, durationMs, position, icon, anim);
}

// ---- Debug: Highlight ----
void UiWindowImpl::SetDebugHighlight(const char* widgetId) {
    debugHighlightId_ = widgetId ? widgetId : "";
    Invalidate();
}

// ---- Debug: Screenshot ----
int UiWindowImpl::Screenshot(const wchar_t* outPath) {
    return ScreenshotRegion({0, 0, 0, 0}, outPath);  // empty region → full window
}

int UiWindowImpl::ScreenshotRegion(D2D1_RECT_F region, const wchar_t* outPath) {
    if (!root_ || !outPath) return -1;

    FlushRenderFrameNow(FrameReason::Screenshot, PresentPolicy::Screenshot);
    if (!renderThreadPresentActive_ || !renderWindowId_.IsValid()) return -1;
    return RenderThread::Instance().ScreenshotRegion(renderWindowId_, region, outPath, dpiScale_);
}

} // namespace ui
