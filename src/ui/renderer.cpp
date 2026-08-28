#include "renderer.h"

#include "debug_trace.h"
#include "display_list.h"
#include "resource_store.h"

#include <algorithm>
#include <condition_variable>
#include <d2d1effects.h>
#include <mutex>
#include <thread>
#include "theme.h"
#include <dcomp.h>
#pragma comment(lib, "dcomp.lib")
#include <vector>
#include <map>
#include <unordered_set>
#include <string>
#include <memory>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cctype>
#include <limits>
#include <shlwapi.h>
#include <windows.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace ui {
ComPtr<ID3D11Device> Renderer::s_d3dDevice;
ComPtr<ID2D1Device>  Renderer::s_d2dDevice;
int                  Renderer::s_deviceRefCount = 0;
} // namespace ui

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

namespace ui {

namespace {
std::mutex g_sharedDeviceMu;
std::recursive_mutex g_sharedD2DUseMu;

static uint32_t FloatBits(float v) {
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(v), "float size mismatch");
    std::memcpy(&bits, &v, sizeof(bits));
    return bits;
}

static const wchar_t* ResolveLocaleName() {
    static wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    static bool initialized = false;
    if (!initialized) {
        int n = GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
        if (n <= 0) {
            std::wcsncpy(locale, L"en-US", _countof(locale) - 1);
            locale[_countof(locale) - 1] = L'\0';
        }
        initialized = true;
    }
    return locale;
}

ComPtr<ID2D1Brush> CreateDisplayListGradientBrush(ID2D1RenderTarget* rt,
                                                  const D2D1_RECT_F& rect,
                                                  const GradientRef& gradient) {
    if (!rt || gradient.stops.empty()) return nullptr;

    std::vector<D2D1_GRADIENT_STOP> d2dStops;
    d2dStops.reserve(gradient.stops.size());
    for (size_t i = 0; i < gradient.stops.size(); ++i) {
        D2D1_GRADIENT_STOP stop{};
        stop.position = gradient.stops[i].position >= 0.0f
            ? gradient.stops[i].position
            : (gradient.stops.size() > 1
                ? static_cast<float>(i) / static_cast<float>(gradient.stops.size() - 1)
                : 0.0f);
        stop.color = gradient.stops[i].color;
        d2dStops.push_back(stop);
    }

    ComPtr<ID2D1GradientStopCollection> stops;
    rt->CreateGradientStopCollection(d2dStops.data(), static_cast<UINT32>(d2dStops.size()),
                                     D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP,
                                     stops.GetAddressOf());
    if (!stops) return nullptr;

    if (!gradient.radial) {
        const float rad = gradient.angle_deg * 3.14159265f / 180.0f;
        const float dx = std::sin(rad);
        const float dy = -std::cos(rad);
        const float cx = (rect.left + rect.right) * 0.5f;
        const float cy = (rect.top + rect.bottom) * 0.5f;
        const float halfW = (rect.right - rect.left) * 0.5f;
        const float halfH = (rect.bottom - rect.top) * 0.5f;
        const float halfLen = std::abs(dx) * halfW + std::abs(dy) * halfH;
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props = {
            { cx - dx * halfLen, cy - dy * halfLen },
            { cx + dx * halfLen, cy + dy * halfLen }
        };
        ComPtr<ID2D1LinearGradientBrush> brush;
        rt->CreateLinearGradientBrush(props, stops.Get(), brush.GetAddressOf());
        return brush;
    }

    const float w = rect.right - rect.left;
    const float h = rect.bottom - rect.top;
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES props = {};
    props.center = {
        rect.left + w * gradient.cx_pct / 100.0f,
        rect.top + h * gradient.cy_pct / 100.0f
    };
    props.gradientOriginOffset = {0, 0};
    const float radius = std::max(w, h) * gradient.radius_pct / 100.0f;
    props.radiusX = radius;
    props.radiusY = radius;

    ComPtr<ID2D1RadialGradientBrush> brush;
    rt->CreateRadialGradientBrush(props, stops.Get(), brush.GetAddressOf());
    return brush;
}

void PaintDisplayListGradient(ID2D1RenderTarget* rt,
                              const D2D1_RECT_F& rect,
                              const GradientRef& gradient,
                              float radius) {
    if (!rt || gradient.stops.empty()) return;

    const bool tiled = gradient.tile_w > 0.0f && gradient.tile_h > 0.0f;
    if (!tiled) {
        auto brush = CreateDisplayListGradientBrush(rt, rect, gradient);
        if (!brush) return;
        if (radius > 0.0f) {
            rt->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush.Get());
        } else {
            rt->FillRectangle(rect, brush.Get());
        }
        return;
    }

    const D2D1_SIZE_F tileSize = D2D1::SizeF(gradient.tile_w, gradient.tile_h);
    ComPtr<ID2D1BitmapRenderTarget> tileRT;
    if (FAILED(rt->CreateCompatibleRenderTarget(tileSize, tileRT.GetAddressOf()))) return;

    const D2D1_RECT_F tileRect = {0.0f, 0.0f, gradient.tile_w, gradient.tile_h};
    auto tileBrush = CreateDisplayListGradientBrush(tileRT.Get(), tileRect, gradient);
    if (!tileBrush) return;

    tileRT->BeginDraw();
    tileRT->Clear(D2D1::ColorF(0, 0, 0, 0));
    tileRT->FillRectangle(tileRect, tileBrush.Get());
    if (FAILED(tileRT->EndDraw())) return;

    ComPtr<ID2D1Bitmap> tileBmp;
    if (FAILED(tileRT->GetBitmap(tileBmp.GetAddressOf())) || !tileBmp) return;

    D2D1_BITMAP_BRUSH_PROPERTIES bbp = {
        D2D1_EXTEND_MODE_WRAP,
        D2D1_EXTEND_MODE_WRAP,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
    };
    D2D1_BRUSH_PROPERTIES bp = {};
    bp.opacity = 1.0f;
    bp.transform = D2D1::Matrix3x2F::Translation(rect.left + gradient.pos_x,
                                                 rect.top + gradient.pos_y);

    ComPtr<ID2D1BitmapBrush> brush;
    if (FAILED(rt->CreateBitmapBrush(tileBmp.Get(), bbp, bp, brush.GetAddressOf())) || !brush) {
        return;
    }

    if (radius > 0.0f) {
        rt->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush.Get());
    } else {
        rt->FillRectangle(rect, brush.Get());
    }
}

void DebugBreakIfAttached() {
#if defined(_DEBUG)
    if (IsDebuggerPresent()) DebugBreak();
#else
    (void)0;
#endif
}
} // namespace

Renderer::SharedD2DGuard::SharedD2DGuard() {
    g_sharedD2DUseMu.lock();
}

Renderer::SharedD2DGuard::~SharedD2DGuard() {
    g_sharedD2DUseMu.unlock();
}

Renderer::~Renderer() {
    ReleaseRenderTarget();
}

bool Renderer::Init() {
    D2D1_FACTORY_OPTIONS options = {};
    options.debugLevel = D2D1_DEBUG_LEVEL_NONE;

    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                    __uuidof(ID2D1Factory1), &options,
                                    reinterpret_cast<void**>(ownedFactory_.GetAddressOf()));
    if (FAILED(hr)) return false;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                             __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(ownedDwFactory_.GetAddressOf()));
    if (FAILED(hr)) return false;

    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(ownedWicFactory_.GetAddressOf()));
    if (FAILED(hr)) return false;

    factory_    = ownedFactory_.Get();
    dwFactory_  = ownedDwFactory_.Get();
    wicFactory_ = ownedWicFactory_.Get();
    brushCache_.clear();
    textFormatCache_.clear();
    return true;
}

bool Renderer::Init(ID2D1Factory1* factory, IDWriteFactory* dwFactory, IWICImagingFactory* wicFactory) {
    if (!factory || !dwFactory || !wicFactory) return false;
    factory_    = factory;
    dwFactory_  = dwFactory;
    wicFactory_ = wicFactory;
    brushCache_.clear();
    textFormatCache_.clear();
    return true;
}

namespace {

/* 共享 D3D11 设备创建 (HW → WARP 回退)。free-threaded API, 可在任意线程跑。 */
Microsoft::WRL::ComPtr<ID3D11Device> CreateSharedD3DDevice() {
    Microsoft::WRL::ComPtr<ID3D11Device> dev;
    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
    };
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                    creationFlags, featureLevels, _countof(featureLevels),
                                    D3D11_SDK_VERSION, dev.GetAddressOf(),
                                    nullptr, nullptr);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                creationFlags, featureLevels, _countof(featureLevels),
                                D3D11_SDK_VERSION, dev.GetAddressOf(),
                                nullptr, nullptr);
        if (FAILED(hr)) dev.Reset();
    }
    return dev;
}

/* 预热状态 (跨线程移交用)。g_prewarmStarted 后 EnsureSharedDevice 必须等
 * 结果 (cv), 不能与预热线程并发重复创建。 */
std::mutex                            g_prewarmMu;
std::condition_variable               g_prewarmCv;
Microsoft::WRL::ComPtr<ID3D11Device>  g_prewarmDevice;
bool                                  g_prewarmStarted = false;
bool                                  g_prewarmDone    = false;

}  // namespace

void Renderer::PrewarmSharedDeviceAsync() {
    {
        std::lock_guard<std::mutex> lk(g_prewarmMu);
        if (g_prewarmStarted || s_d3dDevice) return;
        g_prewarmStarted = true;
    }
    std::thread([] {
        auto dev = CreateSharedD3DDevice();
        {
            std::lock_guard<std::mutex> lk(g_prewarmMu);
            g_prewarmDevice = std::move(dev);
            g_prewarmDone   = true;
        }
        g_prewarmCv.notify_all();
    }).detach();
}

bool Renderer::EnsureSharedDevice() {
    if (s_d3dDevice && s_d2dDevice) return true;

    if (!s_d3dDevice) {
        /* 收割预热结果; 未预热则原地创建 (老路径)。预热失败 (极端: 无 HW
         * 无 WARP) 时这里再试一次原地建, 行为与旧版一致。 */
        {
            std::unique_lock<std::mutex> lk(g_prewarmMu);
            if (g_prewarmStarted) {
                g_prewarmCv.wait(lk, [] { return g_prewarmDone; });
                s_d3dDevice = std::move(g_prewarmDevice);
            }
        }
        if (!s_d3dDevice) s_d3dDevice = CreateSharedD3DDevice();
        if (!s_d3dDevice) return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = s_d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) return false;

    hr = factory_->CreateDevice(dxgiDevice.Get(), s_d2dDevice.GetAddressOf());
    if (FAILED(hr)) return false;

    /* 不要启用 ID3D11Multithread::SetMultithreadProtected(TRUE)：
     * 实测会让 ShowWindow 触发的首帧 DWM 合成等待 250-300ms（大约是 vsync 周期的整数倍），
     * 严重拖慢启动速度。若后台线程需要访问 D3D（如 GPU 回读），应改为线程间
     * 消息传递到 UI 线程执行，而不是全局加锁。*/
    return true;
}

bool Renderer::AcquireSharedDeviceRef() {
    std::lock_guard<std::mutex> deviceLock(g_sharedDeviceMu);
    if (!EnsureSharedDevice()) return false;
    BindFactoryFromSharedDevice();
    if (!hasSharedDeviceRef_) {
        ++s_deviceRefCount;
        hasSharedDeviceRef_ = true;
    }
    return true;
}

void Renderer::BindFactoryFromSharedDevice() {
    if (!s_d2dDevice) return;
    ComPtr<ID2D1Factory> baseFactory;
    s_d2dDevice->GetFactory(baseFactory.GetAddressOf());
    ComPtr<ID2D1Factory1> factory1;
    if (!baseFactory || FAILED(baseFactory.As(&factory1)) || !factory1) return;
    if (factory_ == factory1.Get()) {
        sharedDeviceFactory_ = factory1;
        return;
    }
    sharedDeviceFactory_ = factory1;
    factory_ = sharedDeviceFactory_.Get();
    roundStrokeStyle_.Reset();
    brushCache_.clear();
}

void Renderer::ReleaseSharedDeviceRef() {
    std::lock_guard<std::mutex> deviceLock(g_sharedDeviceMu);
    if (!hasSharedDeviceRef_) return;
    if (s_deviceRefCount > 0) {
        --s_deviceRefCount;
    }
    hasSharedDeviceRef_ = false;
    if (s_deviceRefCount == 0) {
        s_d2dDevice.Reset();
    }
}

void Renderer::ResetSharedDeviceForDeviceLost() {
    std::lock_guard<std::mutex> deviceLock(g_sharedDeviceMu);
    s_d2dDevice.Reset();
    s_d3dDevice.Reset();
    s_deviceRefCount = 0;
}

bool Renderer::CreateRenderTarget(HWND hwnd) {
    ReleaseRenderTarget();
    hwnd_ = hwnd;
    targetThreadId_ = GetCurrentThreadId();
    auto fail = [&]() {
        ReleaseRenderTarget();
        return false;
    };

    /* 1. 确保共享 D3D11/D2D 设备已创建 */
    if (!AcquireSharedDeviceRef()) return false;

    /* 2. 为此窗口创建独立的 DeviceContext */
    HRESULT hr = s_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, ctx_.GetAddressOf());
    if (FAILED(hr)) return fail();

    /* 2.1. 尝试 QI 到 ID2D1DeviceContext5（支持 SVG、Color Font 等 1607+ 功能）。
     * 老系统失败则 ctx5_ 为空，SVG 等能力自动降级。*/
    ctx_.As(&ctx5_);

    /* 3. 创建 SwapChain */
    ComPtr<IDXGIDevice> dxgiDevice;
    hr = s_d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) return fail();

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
    if (FAILED(hr)) return fail();

    ComPtr<IDXGIFactory2> dxgiFactory;
    hr = adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(dxgiFactory.GetAddressOf()));
    if (FAILED(hr)) return fail();

    RECT rc;
    GetClientRect(hwnd, &rc);

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width  = rc.right - rc.left;
    desc.Height = rc.bottom - rc.top;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.Scaling     = DXGI_SCALING_NONE;

    hr = dxgiFactory->CreateSwapChainForHwnd(s_d3dDevice.Get(), hwnd, &desc,
                                              nullptr, nullptr, swapChain_.GetAddressOf());
    if (FAILED(hr)) return fail();
    SetSwapChainBackgroundColor(theme::kWindowBg());

    /* 4. 从 SwapChain 获取 back buffer，创建 target bitmap */
    ComPtr<IDXGISurface> surface;
    hr = swapChain_->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(surface.GetAddressOf()));
    if (FAILED(hr)) return fail();

    UINT dpi = GetDpiForWindow(hwnd);
    D2D1_BITMAP_PROPERTIES1 bitmapProps = {};
    bitmapProps.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    bitmapProps.dpiX = (float)dpi;
    bitmapProps.dpiY = (float)dpi;
    bitmapProps.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

    hr = ctx_->CreateBitmapFromDxgiSurface(surface.Get(), bitmapProps, targetBitmap_.GetAddressOf());
    if (FAILED(hr)) return fail();

    ctx_->SetTarget(targetBitmap_.Get());

    /* 5. 设置渲染参数 */
    ctx_->SetDpi((float)dpi, (float)dpi);
    ctx_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    /* 文字渲染参数：由 theme::TextRenderMode 或 per-window override 决定。
       ApplyTextRenderMode 会读 TextRenderMode() 并 SetTextAntialiasMode +
       SetTextRenderingParams。 */
    ApplyTextRenderMode();

    /* 如果已经有中英分离设置，重建一次 fallback（theme::SetCjkFonts 可能在
       窗口创建之前就设了） */
    RebuildFontFallback();

    return true;
}

bool Renderer::CreateRenderTargetForLayered(HWND hwnd) {
    ReleaseRenderTarget();
    hwnd_ = hwnd;
    targetThreadId_ = GetCurrentThreadId();
    auto fail = [&]() {
        ReleaseRenderTarget();
        return false;
    };

    if (!AcquireSharedDeviceRef()) return false;

    HRESULT hr = s_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                   ctx_.GetAddressOf());
    if (FAILED(hr)) return fail();
    ctx_.As(&ctx5_);

    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(s_d3dDevice.As(&dxgiDevice))) return fail();
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf()))) return fail();
    ComPtr<IDXGIFactory2> dxgiFactory;
    if (FAILED(adapter->GetParent(__uuidof(IDXGIFactory2),
                                   reinterpret_cast<void**>(dxgiFactory.GetAddressOf()))))
        return fail();

    RECT rc;
    GetClientRect(hwnd, &rc);
    UINT width  = std::max<int>(1, rc.right - rc.left);
    UINT height = std::max<int>(1, rc.bottom - rc.top);

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width  = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode   = DXGI_ALPHA_MODE_PREMULTIPLIED;  // transparent compositing

    hr = dxgiFactory->CreateSwapChainForComposition(s_d3dDevice.Get(), &desc,
                                                     nullptr, swapChain_.GetAddressOf());
    if (FAILED(hr)) return fail();

    // DirectComposition: bind the swap chain into the hwnd's visual tree so
    // pixels with alpha=0 punch through to whatever is behind the popup.
    ComPtr<IDCompositionDevice> dcomp;
    hr = DCompositionCreateDevice(dxgiDevice.Get(), __uuidof(IDCompositionDevice),
                                   reinterpret_cast<void**>(dcomp.GetAddressOf()));
    if (FAILED(hr)) return fail();

    ComPtr<IDCompositionTarget> target;
    hr = dcomp->CreateTargetForHwnd(hwnd, TRUE, target.GetAddressOf());
    if (FAILED(hr)) return fail();

    ComPtr<IDCompositionVisual> visual;
    hr = dcomp->CreateVisual(visual.GetAddressOf());
    if (FAILED(hr)) return fail();
    visual->SetContent(swapChain_.Get());
    target->SetRoot(visual.Get());
    dcomp->Commit();

    dcomp.As(&dcompDevice_);
    target.As(&dcompTarget_);
    visual.As(&dcompVisual_);

    ComPtr<IDXGISurface> surface;
    hr = swapChain_->GetBuffer(0, __uuidof(IDXGISurface),
                                reinterpret_cast<void**>(surface.GetAddressOf()));
    if (FAILED(hr)) return fail();

    UINT dpi = GetDpiForWindow(hwnd);
    D2D1_BITMAP_PROPERTIES1 bp = {};
    bp.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                       D2D1_ALPHA_MODE_PREMULTIPLIED);
    bp.dpiX = (float)dpi;
    bp.dpiY = (float)dpi;
    bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

    hr = ctx_->CreateBitmapFromDxgiSurface(surface.Get(), bp, targetBitmap_.GetAddressOf());
    if (FAILED(hr)) return fail();

    ctx_->SetTarget(targetBitmap_.Get());
    ctx_->SetDpi((float)dpi, (float)dpi);
    ctx_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ApplyTextRenderMode();
    RebuildFontFallback();

    return true;
}

void Renderer::ReleaseRenderTarget() {
    if (ctx_) {
        ctx_->SetTarget(nullptr);
    }
    transformStack_.clear();
    brushCache_.clear();
    imageBitmapCache_.clear();
    targetBitmap_.Reset();
    swapChain_.Reset();
    ctx5_.Reset();
    ctx_.Reset();
    dcompVisual_.Reset();
    dcompTarget_.Reset();
    dcompDevice_.Reset();
    hwnd_ = nullptr;
    targetThreadId_ = 0;
    ReleaseSharedDeviceRef();
}

void Renderer::Resize(UINT width, UINT height) {
    DebugAssertTargetThread("Resize");
    if (!ctx_ || !swapChain_) return;
    TraceScope resizeScope("core_renderer", "renderer_resize_duration");
    TraceEvent("core_renderer", "renderer_resize_begin",
               {TraceU64("w_px", width), TraceU64("h_px", height)});

    {
        TraceScope scope("core_renderer", "renderer_resize_detach_duration");
        ctx_->SetTarget(nullptr);
        targetBitmap_.Reset();
        brushCache_.clear();
    }

    HRESULT hr = S_OK;
    {
        TraceScope scope("core_renderer", "resize_buffers_duration");
        hr = swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    }
    if (FAILED(hr)) {
        TraceEvent("core_renderer", "resize_buffers_failed",
                   {TraceI64("hr", static_cast<int64_t>(hr))});
        return;
    }

    ComPtr<IDXGISurface> surface;
    {
        TraceScope scope("core_renderer", "resize_get_buffer_duration");
        hr = swapChain_->GetBuffer(0, __uuidof(IDXGISurface),
                                   reinterpret_cast<void**>(surface.GetAddressOf()));
    }
    if (FAILED(hr)) {
        TraceEvent("core_renderer", "resize_get_buffer_failed",
                   {TraceI64("hr", static_cast<int64_t>(hr))});
        return;
    }

    UINT dpi = GetDpiForWindow(hwnd_);
    D2D1_BITMAP_PROPERTIES1 bitmapProps = {};
    bitmapProps.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    bitmapProps.dpiX = (float)dpi;
    bitmapProps.dpiY = (float)dpi;
    bitmapProps.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

    {
        TraceScope scope("core_renderer", "resize_create_target_bitmap_duration");
        hr = ctx_->CreateBitmapFromDxgiSurface(surface.Get(), bitmapProps,
                                               targetBitmap_.GetAddressOf());
    }
    if (FAILED(hr)) {
        TraceEvent("core_renderer", "resize_create_target_bitmap_failed",
                   {TraceI64("hr", static_cast<int64_t>(hr))});
        return;
    }

    {
        TraceScope scope("core_renderer", "renderer_resize_bind_target_duration");
        ctx_->SetTarget(targetBitmap_.Get());
        ctx_->SetDpi((float)dpi, (float)dpi);
    }
}

void Renderer::BeginDraw() {
    DebugAssertTargetThread("BeginDraw");
    transformStack_.clear();
    ctx_->BeginDraw();
    ctx_->SetTransform(D2D1::Matrix3x2F::Identity());
}

HRESULT Renderer::EndDraw() {
    DebugAssertTargetThread("EndDraw");
    TraceScope scope("core_renderer", "renderer_end_draw_duration");
    HRESULT hr = S_OK;
    {
        TraceScope flushScope("core_renderer", "d2d_end_draw_duration");
        hr = ctx_->EndDraw();
    }
    /* L177: skipPresent → 只 flush D2D 绘制 (上面 ctx_->EndDraw 已做), 不 flip 到
     * DWM。给"绘制隐藏窗但不上屏"用 (避免 DWM 未合成窗的 Present 在 AMD 死锁)。 */
    if (swapChain_ && !skipPresent) {
        DXGI_PRESENT_PARAMETERS params = {};
        UINT syncInterval = skipVSync ? 0 : 1;
        TraceEvent("core_renderer", "present_begin",
                   {TraceU64("sync_interval", syncInterval),
                    TraceBool("skip_vsync", skipVSync)});
        {
            TraceScope presentScope("core_renderer", "present_duration");
            swapChain_->Present1(syncInterval, 0, &params);
        }
    }
    skipVSync = false;
    skipPresent = false;
    return hr;
}

HRESULT Renderer::PresentPrepared(bool skipVsync) {
    DebugAssertTargetThread("PresentPrepared");
    if (!swapChain_) return S_FALSE;
    DXGI_PRESENT_PARAMETERS params = {};
    const UINT syncInterval = skipVsync ? 0 : 1;
    TraceEvent("core_renderer", "present_prepared_begin",
               {TraceU64("sync_interval", syncInterval),
                TraceBool("skip_vsync", skipVsync)});
    TraceScope presentScope("core_renderer", "present_prepared_duration");
    return swapChain_->Present1(syncInterval, 0, &params);
}

HRESULT Renderer::SetSwapChainBackgroundColor(const D2D1_COLOR_F& color) {
    DebugAssertTargetThread("SetSwapChainBackgroundColor");
    if (!swapChain_) return S_FALSE;
    DXGI_RGBA bg = {color.r, color.g, color.b, color.a};
    return swapChain_->SetBackgroundColor(&bg);
}

void Renderer::DebugAssertTargetThread(const char* op) const {
#if defined(_DEBUG)
    if (targetThreadId_ != 0 && targetThreadId_ != GetCurrentThreadId()) {
        TraceEvent("core_renderer", "renderer_wrong_thread",
                   {TraceU64("owner_thread", targetThreadId_),
                    TraceU64("current_thread", GetCurrentThreadId())});
        DebugBreakIfAttached();
    }
#else
    (void)op;
#endif
}

void Renderer::FlushAndTrimGpu() {
    if (!ctx_) return;
    ctx_->Flush();

    ComPtr<ID2D1Device> d2dDevice;
    ctx_->GetDevice(d2dDevice.GetAddressOf());
    if (d2dDevice) {
        ComPtr<IDXGIDevice3> dxgi3;
        if (SUCCEEDED(d2dDevice.As(&dxgi3)) && dxgi3) {
            dxgi3->Trim();
        }
    }
}

void Renderer::Clear(const D2D1_COLOR_F& color) {
    if (auto* recorder = ActiveDisplayListRecorder()) {
        recorder->Clear(color);
    }
    if (!ctx_) return;
    ctx_->Clear(color);
}

void Renderer::SetDisplayListRecorder(DisplayListRecorder* recorder) {
    displayListRecorder_ = recorder;
}

bool Renderer::CreateSvgDocumentFromXml(const std::string& xml,
                                        float viewportW, float viewportH,
                                        ComPtr<ID2D1SvgDocument>* outDoc) {
    SharedD2DGuard d2dGuard;
    if (outDoc) outDoc->Reset();
    if (xml.empty() || viewportW <= 0.0f || viewportH <= 0.0f ||
        xml.size() > static_cast<size_t>(std::numeric_limits<UINT>::max())) {
        return false;
    }

    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(xml.data()),
                                    static_cast<UINT>(xml.size())));
    if (!stream) return false;

    ComPtr<ID2D1DeviceContext5> svgCtx5 = ctx5_;
    ComPtr<ID2D1DeviceContext> tempCtx;
    if (!svgCtx5) {
        std::lock_guard<std::mutex> deviceLock(g_sharedDeviceMu);
        if (!EnsureSharedDevice() || !s_d2dDevice) return false;
        HRESULT hr = s_d2dDevice->CreateDeviceContext(
            D2D1_DEVICE_CONTEXT_OPTIONS_NONE, tempCtx.GetAddressOf());
        if (FAILED(hr) || !tempCtx) return false;
        tempCtx.As(&svgCtx5);
    }
    if (!svgCtx5) return false;

    ComPtr<ID2D1SvgDocument> doc;
    HRESULT hr = svgCtx5->CreateSvgDocument(
        stream.Get(), D2D1::SizeF(viewportW, viewportH), doc.GetAddressOf());
    if (FAILED(hr) || !doc) return false;
    if (outDoc) *outDoc = doc;
    return true;
}

/* 三个绘制点共用的唯一一份"按文档顺序逐段画"实现。见 renderer.h 的声明注释:
 * steps 的顺序就是 z 序, 阴影必须紧贴它自己那一步。 */
void Renderer::DrawSvgStepsOrdered(
    ID2D1DeviceContext5* ctx5,
    const std::vector<SvgDocumentRef::Step>& steps,
    float viewportW, float viewportH,
    const D2D1_MATRIX_3X2_F& svgXform,
    std::vector<ComPtr<ID2D1SvgDocument>>* contentDocs,
    std::vector<ComPtr<ID2D1SvgDocument>>* shadowDocs) {
    if (!ctx5 || steps.empty() || viewportW <= 0.0f || viewportH <= 0.0f) return;
    if (contentDocs) contentDocs->resize(steps.size());
    if (shadowDocs) shadowDocs->resize(steps.size());

    auto makeDoc = [&](const std::string& data,
                       ComPtr<ID2D1SvgDocument>& outDoc) -> bool {
        if (outDoc) return true;                       // 缓存命中
        if (data.empty() ||
            data.size() > static_cast<size_t>((std::numeric_limits<UINT>::max)())) {
            return false;
        }
        ComPtr<IStream> stream;
        stream.Attach(SHCreateMemStream(
            reinterpret_cast<const BYTE*>(data.data()),
            static_cast<UINT>(data.size())));
        if (!stream) return false;
        return SUCCEEDED(ctx5->CreateSvgDocument(
                   stream.Get(), D2D1::SizeF(viewportW, viewportH),
                   outDoc.GetAddressOf())) && outDoc;
    };

    for (size_t i = 0; i < steps.size(); ++i) {
        const SvgDocumentRef::Step& step = steps[i];

        ComPtr<ID2D1SvgDocument> localContent;
        ComPtr<ID2D1SvgDocument>& content =
            contentDocs ? (*contentDocs)[i] : localContent;
        if (!makeDoc(step.xml, content)) continue;

        if (!step.shadow_xml.empty()) {
            ComPtr<ID2D1SvgDocument> localShadow;
            ComPtr<ID2D1SvgDocument>& shadow =
                shadowDocs ? (*shadowDocs)[i] : localShadow;
            ComPtr<ID2D1CommandList> shadowList;
            if (makeDoc(step.shadow_xml, shadow) &&
                SUCCEEDED(ctx5->CreateCommandList(shadowList.GetAddressOf())) &&
                shadowList) {
                ComPtr<ID2D1Image> oldTarget;
                ctx5->GetTarget(&oldTarget);
                ctx5->SetTarget(shadowList.Get());
                ctx5->SetTransform(D2D1::Matrix3x2F::Identity());
                ctx5->Clear(D2D1::ColorF(0, 0, 0, 0));
                shadow->SetViewportSize(D2D1::SizeF(viewportW, viewportH));
                ctx5->DrawSvgDocument(shadow.Get());
                ctx5->SetTarget(oldTarget.Get());

                ComPtr<ID2D1Effect> blur;
                if (SUCCEEDED(shadowList->Close()) &&
                    SUCCEEDED(ctx5->CreateEffect(CLSID_D2D1GaussianBlur,
                                                 blur.GetAddressOf())) && blur) {
                    blur->SetInput(0, shadowList.Get());
                    blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION,
                                   step.std_deviation);
                    blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE,
                                   D2D1_BORDER_MODE_SOFT);
                    ctx5->SetTransform(
                        D2D1::Matrix3x2F::Translation(step.dx, step.dy) * svgXform);
                    ctx5->DrawImage(blur.Get(), D2D1::Point2F(0, 0),
                                    D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
                }
            }
        }

        ctx5->SetTransform(svgXform);
        content->SetViewportSize(D2D1::SizeF(viewportW, viewportH));
        ctx5->DrawSvgDocument(content.Get());
    }
}

bool Renderer::RenderSvgDocumentToBgra(
    const std::vector<SvgDocumentRef::Step>& steps,
    float viewportW, float viewportH,
    uint32_t width, uint32_t height,
    uint8_t* outBgra) {
    SharedD2DGuard d2dGuard;
    if (steps.empty() || viewportW <= 0.0f || viewportH <= 0.0f ||
        width == 0 || height == 0 || !outBgra) {
        return false;
    }

    ComPtr<ID2D1Device> d2dDevice;
    {
        std::lock_guard<std::mutex> deviceLock(g_sharedDeviceMu);
        if (!EnsureSharedDevice() || !s_d2dDevice) return false;
        d2dDevice = s_d2dDevice;
    }

    ComPtr<ID2D1DeviceContext> ctx;
    HRESULT hr = d2dDevice->CreateDeviceContext(
        D2D1_DEVICE_CONTEXT_OPTIONS_NONE, ctx.GetAddressOf());
    if (FAILED(hr) || !ctx) return false;

    ComPtr<ID2D1DeviceContext5> ctx5;
    ctx.As(&ctx5);
    if (!ctx5) return false;

    constexpr float kDpi = 96.0f;
    ctx5->SetDpi(kDpi, kDpi);

    D2D1_BITMAP_PROPERTIES1 targetProps = {};
    targetProps.pixelFormat = D2D1::PixelFormat(
        DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    targetProps.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
    targetProps.dpiX = kDpi;
    targetProps.dpiY = kDpi;

    ComPtr<ID2D1Bitmap1> target;
    hr = ctx5->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0,
                            targetProps, target.GetAddressOf());
    if (FAILED(hr) || !target) return false;

    ctx5->SetTarget(target.Get());
    ctx5->BeginDraw();
    ctx5->Clear(D2D1::ColorF(0, 0, 0, 0));

    const float sx = static_cast<float>(width) / viewportW;
    const float sy = static_cast<float>(height) / viewportH;
    const D2D1_MATRIX_3X2_F svgXform = D2D1::Matrix3x2F::Scale(sx, sy);
    DrawSvgStepsOrdered(ctx5.Get(), steps, viewportW, viewportH, svgXform,
                        nullptr, nullptr);

    ctx5->SetTransform(D2D1::Matrix3x2F::Identity());
    hr = ctx5->EndDraw();
    ctx5->SetTarget(nullptr);
    if (FAILED(hr)) return false;

    D2D1_BITMAP_PROPERTIES1 cpuProps = {};
    cpuProps.pixelFormat = D2D1::PixelFormat(
        DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    cpuProps.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ |
                             D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    cpuProps.dpiX = kDpi;
    cpuProps.dpiY = kDpi;

    ComPtr<ID2D1Bitmap1> cpu;
    hr = ctx5->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0,
                            cpuProps, cpu.GetAddressOf());
    if (FAILED(hr) || !cpu) return false;

    D2D1_POINT_2U dst = {0, 0};
    D2D1_RECT_U src = {0, 0, width, height};
    hr = cpu->CopyFromBitmap(&dst, target.Get(), &src);
    if (FAILED(hr)) return false;

    D2D1_MAPPED_RECT mapped = {};
    hr = cpu->Map(D2D1_MAP_OPTIONS_READ, &mapped);
    if (FAILED(hr)) return false;

    const size_t dstRow = static_cast<size_t>(width) * 4u;
    const size_t srcRow = std::min(dstRow, static_cast<size_t>(mapped.pitch));
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* dstRowPtr = outBgra + static_cast<size_t>(y) * dstRow;
        const uint8_t* srcRowPtr = mapped.bits + static_cast<size_t>(y) * mapped.pitch;
        std::memcpy(dstRowPtr, srcRowPtr, srcRow);
        if (srcRow < dstRow) {
            std::memset(dstRowPtr + srcRow, 0, dstRow - srcRow);
        }
    }
    cpu->Unmap();
    return true;
}

ComPtr<ID2D1SolidColorBrush> Renderer::GetBrush(const D2D1_COLOR_F& color) {
    if (!ctx_) return nullptr;

    ColorKey key{FloatBits(color.r), FloatBits(color.g), FloatBits(color.b), FloatBits(color.a)};
    auto it = brushCache_.find(key);
    if (it != brushCache_.end()) {
        return it->second;
    }

    ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(ctx_->CreateSolidColorBrush(color, brush.GetAddressOf())) && brush) {
        brushCache_.emplace(std::move(key), brush);
    }
    return brush;
}

ComPtr<IDWriteTextFormat> Renderer::GetTextFormat(float fontSize, const wchar_t* family,
                                                   DWRITE_FONT_WEIGHT weight) {
    if (!dwFactory_) return nullptr;
    /* family == nullptr → 用本 Renderer 的 default font（per-window > theme > "Segoe UI"） */
    const wchar_t* resolvedFamily = family ? family : DefaultFontFamily();
    if (!resolvedFamily) resolvedFamily = L"Segoe UI";

    TextFormatKey key;
    key.sizeBits = FloatBits(fontSize);
    key.weight = static_cast<uint32_t>(weight);
    key.family = resolvedFamily;

    auto it = textFormatCache_.find(key);
    if (it != textFormatCache_.end()) {
        return it->second;
    }

    ComPtr<IDWriteTextFormat> fmt;
    HRESULT hr = dwFactory_->CreateTextFormat(
        resolvedFamily, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fontSize, ResolveLocaleName(), fmt.GetAddressOf());
    if (SUCCEEDED(hr) && fmt) {
        /* 如果有中英分离字体回退，attach 到 TextFormat3 上 */
        if (fontFallback_) {
            ComPtr<IDWriteTextFormat3> fmt3;
            if (SUCCEEDED(fmt.As(&fmt3)) && fmt3) {
                fmt3->SetFontFallback(fontFallback_.Get());
            }
        }
        textFormatCache_.emplace(std::move(key), fmt);
    }
    return fmt;
}

/* ---- Per-window font / render mode 状态 (since 1.3.0) ---- */

const wchar_t* Renderer::DefaultFontFamily() const {
    if (!defaultFontOverride_.empty()) return defaultFontOverride_.c_str();
    return theme::DefaultFontFamily();  /* 全局默认 "Segoe UI" */
}

const wchar_t* Renderer::LatinFontFamily() const {
    if (!latinFontOverride_.empty()) return latinFontOverride_.c_str();
    return theme::LatinFontFamily();  /* 可能返回 nullptr */
}

const wchar_t* Renderer::CjkFontFamily() const {
    if (!cjkFontOverride_.empty()) return cjkFontOverride_.c_str();
    return theme::CjkFontFamily();    /* 可能返回 nullptr */
}

theme::TextRenderMode Renderer::TextRenderMode() const {
    return hasRenderModeOverride_ ? renderModeOverride_ : theme::GetTextRenderMode();
}

void Renderer::SetDefaultFontFamily(const wchar_t* family) {
    defaultFontOverride_ = family ? family : L"";
    textFormatCache_.clear();  /* 缓存失效 */
    RebuildFontFallback();
}

void Renderer::SetCjkFonts(const wchar_t* latin, const wchar_t* cjk) {
    latinFontOverride_ = latin ? latin : L"";
    cjkFontOverride_   = cjk   ? cjk   : L"";
    textFormatCache_.clear();
    RebuildFontFallback();
}

void Renderer::SetTextRenderMode(theme::TextRenderMode mode) {
    hasRenderModeOverride_ = true;
    renderModeOverride_ = mode;
    ApplyTextRenderMode();  /* 立刻应用到 ctx_（如已创建） */
}

void Renderer::ApplyTextRenderMode() {
    if (!ctx_ || !dwFactory_) return;

    D2D1_TEXT_ANTIALIAS_MODE aa = D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE;
    DWRITE_RENDERING_MODE    rm = DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
    float gamma = 1.8f, enhancedContrast = 0.5f, clearTypeLevel = 0.0f;

    switch (TextRenderMode()) {
    case theme::TextRenderMode::Smooth:
        aa = D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE;
        rm = DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
        gamma = 1.8f; enhancedContrast = 0.5f; clearTypeLevel = 0.0f;
        break;
    case theme::TextRenderMode::ClearType:
        aa = D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE;
        rm = DWRITE_RENDERING_MODE_NATURAL;
        gamma = 1.8f; enhancedContrast = 1.0f; clearTypeLevel = 1.0f;
        break;
    case theme::TextRenderMode::Sharp:
        aa = D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE;
        rm = DWRITE_RENDERING_MODE_GDI_CLASSIC;
        gamma = 1.8f; enhancedContrast = 1.0f; clearTypeLevel = 1.0f;
        break;
    case theme::TextRenderMode::GraySharp:
        aa = D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE;
        rm = DWRITE_RENDERING_MODE_GDI_CLASSIC;
        gamma = 1.8f; enhancedContrast = 1.0f; clearTypeLevel = 0.0f;
        break;
    case theme::TextRenderMode::Aliased:
        aa = D2D1_TEXT_ANTIALIAS_MODE_ALIASED;
        rm = DWRITE_RENDERING_MODE_ALIASED;
        gamma = 1.8f; enhancedContrast = 0.0f; clearTypeLevel = 0.0f;
        break;
    }
    ctx_->SetTextAntialiasMode(aa);

    IDWriteRenderingParams* base = nullptr;
    IDWriteRenderingParams* custom = nullptr;
    if (SUCCEEDED(dwFactory_->CreateRenderingParams(&base))) {
        dwFactory_->CreateCustomRenderingParams(
            gamma, enhancedContrast, clearTypeLevel,
            base->GetPixelGeometry(), rm, &custom);
        if (custom) { ctx_->SetTextRenderingParams(custom); custom->Release(); }
        base->Release();
    }
}

/* 构造中英分离的 IDWriteFontFallback。LatinFamily/CjkFamily 任一非空即启用。
   CJK 覆盖的 Unicode 块：CJK Unified Ideographs + Extension A + 符号 / 标点 /
   全角形式 / 汉字偏旁 / 注音等常见范围。 */
void Renderer::RebuildFontFallback() {
    fontFallback_.Reset();
    const wchar_t* latin = LatinFontFamily();
    const wchar_t* cjk   = CjkFontFamily();
    if (!latin && !cjk) return;

    ComPtr<IDWriteFactory2> dwf2;
    if (FAILED(reinterpret_cast<IUnknown*>(dwFactory_)->QueryInterface(
            __uuidof(IDWriteFactory2), reinterpret_cast<void**>(dwf2.GetAddressOf())))) {
        return;  /* Fallback builder 需要 DWrite 1.2+ */
    }

    ComPtr<IDWriteFontFallbackBuilder> builder;
    if (FAILED(dwf2->CreateFontFallbackBuilder(builder.GetAddressOf()))) return;

    /* CJK Unicode 块 */
    if (cjk) {
        DWRITE_UNICODE_RANGE cjkRanges[] = {
            { 0x2E80, 0x2EFF },  /* CJK 部首补充 */
            { 0x3000, 0x303F },  /* CJK 符号和标点 */
            { 0x3040, 0x309F },  /* 平假名 */
            { 0x30A0, 0x30FF },  /* 片假名 */
            { 0x3100, 0x312F },  /* 注音字母 */
            { 0x3200, 0x33FF },  /* 带圈符号 / CJK 兼容 */
            { 0x3400, 0x4DBF },  /* CJK 扩展 A */
            { 0x4E00, 0x9FFF },  /* CJK 统一汉字 */
            { 0xF900, 0xFAFF },  /* CJK 兼容汉字 */
            { 0xFE30, 0xFE4F },  /* CJK 兼容形式 */
            { 0xFF00, 0xFFEF },  /* 半角及全角形式 */
        };
        const wchar_t* families[] = { cjk };
        builder->AddMapping(cjkRanges,
                            (UINT32)(sizeof(cjkRanges)/sizeof(cjkRanges[0])),
                            families, 1);
    }

    /* ASCII / 拉丁 / 西欧 */
    if (latin) {
        DWRITE_UNICODE_RANGE latinRanges[] = {
            { 0x0020, 0x007F },  /* ASCII */
            { 0x00A0, 0x024F },  /* Latin-1 补充 / Latin 扩展 A/B */
        };
        const wchar_t* families[] = { latin };
        builder->AddMapping(latinRanges,
                            (UINT32)(sizeof(latinRanges)/sizeof(latinRanges[0])),
                            families, 1);
    }

    /* 其余范围接系统默认 fallback */
    ComPtr<IDWriteFontFallback> sysFallback;
    if (SUCCEEDED(dwf2->GetSystemFontFallback(sysFallback.GetAddressOf())) && sysFallback) {
        builder->AddMappings(sysFallback.Get());
    }

    builder->CreateFontFallback(fontFallback_.GetAddressOf());
}

void Renderer::FillRect(const D2D1_RECT_F& rect, const D2D1_COLOR_F& color) {
    if (auto* recorder = ActiveDisplayListRecorder()) {
        recorder->FillRect(rect, color);
    }
    auto brush = GetBrush(color);
    if (brush) ctx_->FillRectangle(rect, brush.Get());
}

void Renderer::FillQuad(const D2D1_POINT_2F pts[4], const D2D1_COLOR_F& color) {
    if (auto* recorder = ActiveDisplayListRecorder()) {
        recorder->FillQuad(pts, color);
    }
    if (!ctx_ || !factory_) return;
    ComPtr<ID2D1PathGeometry> geom;
    if (FAILED(factory_->CreatePathGeometry(geom.GetAddressOf())) || !geom) return;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(geom->Open(sink.GetAddressOf())) || !sink) return;
    sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_FILLED);
    const D2D1_POINT_2F rest[3] = { pts[1], pts[2], pts[3] };
    sink->AddLines(rest, 3);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) return;
    auto brush = GetBrush(color);
    if (brush) ctx_->FillGeometry(geom.Get(), brush.Get());
}

void Renderer::FillRoundedRect(const D2D1_RECT_F& rect, float rx, float ry, const D2D1_COLOR_F& color) {
    if (auto* recorder = ActiveDisplayListRecorder()) {
        recorder->FillRoundedRect(rect, rx, ry, color);
    }
    auto brush = GetBrush(color);
    if (brush) {
        D2D1_ROUNDED_RECT rr = {rect, rx, ry};
        ctx_->FillRoundedRectangle(rr, brush.Get());
    }
}

bool Renderer::DrawBlurredRoundedRect(const D2D1_RECT_F& rect, float rx, float ry,
                                      float blurRadius, const D2D1_COLOR_F& color) {
    if (color.a <= 0.0f) return false;
    if (rect.right <= rect.left || rect.bottom <= rect.top) return false;
    if (blurRadius < 0.5f) {
        FillRoundedRect(rect, rx, ry, color);
        return true;
    }

    /* display list 必须先录, 再判 ctx_ —— 这是本文件所有图元的既定顺序
     * (对照 PushClip / DrawRect)。此处原本反过来: `if (!ctx_ || !ctx5_) return
     * false` 写在最前面, 于是渲染线程 present 接管、UI 线程 RT 释放之后, 这条
     * 命令**永远进不了 display list**, 调用方只看到 false。表现是 gh_img_view
     * 的图片阴影完全不画、widget box-shadow 静默退化到 fallback。
     * 返回值语义随之明确: 录进 display list 就算成功, 由渲染线程负责真正画。 */
    bool recorded = false;
    if (auto* recorder = ActiveDisplayListRecorder()) {
        recorder->DrawBlurredRoundedRect(rect, rx, ry, blurRadius, color);
        recorded = true;
    }
    if (!ctx_ || !ctx5_) return recorded;

    ComPtr<ID2D1CommandList> mask;
    if (FAILED(ctx5_->CreateCommandList(mask.GetAddressOf())) || !mask) {
        return false;
    }

    ComPtr<ID2D1Image> oldTarget;
    ctx5_->GetTarget(oldTarget.GetAddressOf());
    if (!oldTarget) return false;

    D2D1_MATRIX_3X2_F oldXform = D2D1::Matrix3x2F::Identity();
    ctx5_->GetTransform(&oldXform);

    ctx5_->SetTarget(mask.Get());
    ctx5_->SetTransform(D2D1::Matrix3x2F::Identity());
    ctx5_->Clear(D2D1::ColorF(0, 0, 0, 0));
    if (auto brush = GetBrush(color)) {
        ctx5_->FillRoundedRectangle(D2D1::RoundedRect(rect, rx, ry), brush.Get());
    }
    ctx5_->SetTarget(oldTarget.Get());
    ctx5_->SetTransform(oldXform);

    if (FAILED(mask->Close())) return false;

    ComPtr<ID2D1Effect> blur;
    if (FAILED(ctx5_->CreateEffect(CLSID_D2D1GaussianBlur, blur.GetAddressOf())) || !blur) {
        return false;
    }

    blur->SetInput(0, mask.Get());
    blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION,
                   std::max(0.5f, blurRadius * 0.5f));
    blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);

    ctx5_->DrawImage(blur.Get(), D2D1::Point2F(0, 0),
                     D2D1_INTERPOLATION_MODE_LINEAR,
                     D2D1_COMPOSITE_MODE_SOURCE_OVER);
    return true;
}

void Renderer::DrawRect(const D2D1_RECT_F& rect, const D2D1_COLOR_F& color, float width) {
    if (auto* recorder = ActiveDisplayListRecorder()) {
        recorder->DrawRect(rect, color, width);
    }
    auto brush = GetBrush(color);
    if (brush) ctx_->DrawRectangle(rect, brush.Get(), width);
}

void Renderer::DrawRoundedRect(const D2D1_RECT_F& rect, float rx, float ry, const D2D1_COLOR_F& color, float width) {
    if (auto* recorder = ActiveDisplayListRecorder()) {
        recorder->DrawRoundedRect(rect, rx, ry, color, width);
    }
    auto brush = GetBrush(color);
    if (brush) {
        D2D1_ROUNDED_RECT rr = {rect, rx, ry};
        ctx_->DrawRoundedRectangle(rr, brush.Get(), width);
    }
}

void Renderer::DrawLine(float x1, float y1, float x2, float y2, const D2D1_COLOR_F& color, float width) {
    if (auto* recorder = ActiveDisplayListRecorder()) {
        recorder->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), color, width);
    }
    auto brush = GetBrush(color);
    if (brush) ctx_->DrawLine({x1, y1}, {x2, y2}, brush.Get(), width);
}

void Renderer::DrawText(const std::wstring& text, const D2D1_RECT_F& rect, const D2D1_COLOR_F& color,
                         float fontSize, DWRITE_TEXT_ALIGNMENT align, DWRITE_FONT_WEIGHT weight,
                         DWRITE_PARAGRAPH_ALIGNMENT vAlign, bool wordWrap, const wchar_t* family) {
    if (auto* recorder = ActiveDisplayListRecorder()) {
        TextStyle style;
        style.color = color;
        style.font_size = fontSize;
        if (family && *family) style.font_family = family;
        style.alignment = static_cast<int>(align);
        style.paragraph_alignment = static_cast<int>(vAlign);
        style.weight = static_cast<int>(weight);
        style.word_wrap = wordWrap;
        recorder->DrawText(text, rect, style);
    }
    auto brush = GetBrush(color);
    auto fmt = GetTextFormat(fontSize, (family && *family) ? family : theme::kFontFamily, weight);
    if (!brush || !fmt || !dwFactory_) return;

    float layoutW = rect.right - rect.left;
    float layoutH = rect.bottom - rect.top;
    if (layoutW <= 0 || layoutH <= 0) return;

    float drawTop = rect.top;
    float drawLayoutH = layoutH;
    if (!wordWrap && vAlign == DWRITE_PARAGRAPH_ALIGNMENT_CENTER) {
        // Some CJK fallback glyphs overhang the nominal line box by a pixel or
        // two at fractional DPI scales. Expand the layout symmetrically so the
        // visual center stays unchanged while DirectWrite has room for ink.
        constexpr float kVerticalOverhangPad = 2.0f;
        drawTop -= kVerticalOverhangPad;
        drawLayoutH += kVerticalOverhangPad * 2.0f;
    }

    ComPtr<IDWriteTextLayout> layout;
    HRESULT hr = dwFactory_->CreateTextLayout(
        text.c_str(), (UINT32)text.length(), fmt.Get(), layoutW, drawLayoutH, &layout);
    if (FAILED(hr) || !layout) return;

    layout->SetTextAlignment(align);
    layout->SetParagraphAlignment(vAlign);

    if (wordWrap) {
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    } else {
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        // Enable character ellipsis trimming (text too long → "abc...")
        DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
        ComPtr<IDWriteInlineObject> ellipsis;
        dwFactory_->CreateEllipsisTrimmingSign(fmt.Get(), &ellipsis);
        if (ellipsis) layout->SetTrimming(&trimming, ellipsis.Get());
    }

    ctx_->DrawTextLayout({rect.left, drawTop}, layout.Get(), brush.Get());
}

ComPtr<IDWriteTextLayout> Renderer::CreateTextLayout(const std::wstring& text,
                                                       float maxWidth, float maxHeight,
                                                       float fontSize, bool wrap,
                                                       DWRITE_FONT_WEIGHT weight) {
    ComPtr<IDWriteTextLayout> layout;
    if (!dwFactory_) return layout;
    auto fmt = GetTextFormat(fontSize, theme::kFontFamily, weight);
    if (!fmt) return layout;
    HRESULT hr = dwFactory_->CreateTextLayout(
        text.c_str(), (UINT32)text.length(),
        fmt.Get(), maxWidth, maxHeight, layout.GetAddressOf());
    if (FAILED(hr) || !layout) { layout.Reset(); return layout; }
    layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP
                                 : DWRITE_WORD_WRAPPING_NO_WRAP);
    return layout;
}

float Renderer::MeasureTextWidth(const std::wstring& text, float fontSize,
                                 const wchar_t* family, DWRITE_FONT_WEIGHT weight) {
    if (text.empty() || !dwFactory_) return 0.0f;

    auto fmt = GetTextFormat(fontSize, family, weight);
    if (!fmt) return 0.0f;

    ComPtr<IDWriteTextLayout> layout;
    HRESULT hr = dwFactory_->CreateTextLayout(
        text.c_str(),
        static_cast<UINT32>(text.length()),
        fmt.Get(),
        std::numeric_limits<float>::max(),
        fontSize * 2.0f + 8.0f,
        layout.GetAddressOf());
    if (FAILED(hr) || !layout) return 0.0f;

    DWRITE_TEXT_METRICS metrics{};
    hr = layout->GetMetrics(&metrics);
    if (FAILED(hr)) return 0.0f;
    return metrics.widthIncludingTrailingWhitespace;
}

float Renderer::MeasureTextHeight(const std::wstring& text, float maxWidth, float fontSize,
                                   DWRITE_FONT_WEIGHT weight) {
    if (text.empty() || !dwFactory_) return fontSize + 4.0f;

    auto fmt = GetTextFormat(fontSize, theme::kFontFamily, weight);
    if (!fmt) return fontSize + 4.0f;

    ComPtr<IDWriteTextLayout> layout;
    HRESULT hr = dwFactory_->CreateTextLayout(
        text.c_str(),
        static_cast<UINT32>(text.length()),
        fmt.Get(),
        maxWidth,
        10000.0f,
        layout.GetAddressOf());
    if (FAILED(hr) || !layout) return fontSize + 4.0f;

    DWRITE_TEXT_METRICS metrics{};
    hr = layout->GetMetrics(&metrics);
    if (FAILED(hr)) return fontSize + 4.0f;
    return metrics.height;
}

void Renderer::DrawIcon(const std::wstring& glyph, const D2D1_RECT_F& rect, const D2D1_COLOR_F& color, float fontSize) {
    DrawText(glyph,
             rect,
             color,
             fontSize,
             DWRITE_TEXT_ALIGNMENT_CENTER,
             DWRITE_FONT_WEIGHT_NORMAL,
             DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
             false,
             L"Segoe MDL2 Assets");
}

/* 从已经创建好的 IWICBitmapDecoder 抽取最大帧 → CPU BGRA premul bytes。
   DisplayList 资源路径用这个，失败返回 false。 */
static bool DecodeDecoderToBgraPremul(IWICImagingFactory* wic,
                                      IWICBitmapDecoder* decoder,
                                      std::vector<uint8_t>& pixels,
                                      int& width,
                                      int& height,
                                      int& stride) {
    pixels.clear();
    width = height = stride = 0;
    if (!wic || !decoder) return false;

    UINT frameCount = 0;
    decoder->GetFrameCount(&frameCount);
    UINT bestFrame = 0;
    UINT bestArea = 0;
    if (frameCount > 1) {
        for (UINT i = 0; i < frameCount; i++) {
            ComPtr<IWICBitmapFrameDecode> f;
            if (SUCCEEDED(decoder->GetFrame(i, f.GetAddressOf()))) {
                UINT fw = 0, fh = 0;
                f->GetSize(&fw, &fh);
                UINT area = fw * fh;
                if (area > bestArea) { bestArea = area; bestFrame = i; }
            }
        }
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    HRESULT hr = decoder->GetFrame(bestFrame, frame.GetAddressOf());
    if (FAILED(hr)) return false;

    ComPtr<IWICFormatConverter> converter;
    hr = wic->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr)) return false;

    hr = converter->Initialize(
        frame.Get(), GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeMedianCut);
    if (FAILED(hr)) return false;

    UINT imgW = 0, imgH = 0;
    hr = converter->GetSize(&imgW, &imgH);
    if (FAILED(hr) || imgW == 0 || imgH == 0) return false;
    if (imgW > static_cast<UINT>(std::numeric_limits<int>::max() / 4) ||
        imgH > static_cast<UINT>(std::numeric_limits<int>::max())) {
        return false;
    }

    const UINT rowStride = imgW * 4;
    const uint64_t totalBytes = static_cast<uint64_t>(rowStride) * imgH;
    if (totalBytes > static_cast<uint64_t>(std::numeric_limits<UINT>::max()) ||
        totalBytes > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return false;
    }

    std::vector<uint8_t> out(static_cast<size_t>(totalBytes));
    hr = converter->CopyPixels(nullptr, rowStride, static_cast<UINT>(out.size()), out.data());
    if (FAILED(hr)) return false;

    pixels = std::move(out);
    width = static_cast<int>(imgW);
    height = static_cast<int>(imgH);
    stride = static_cast<int>(rowStride);
    return true;
}

/* 从已经创建好的 IWICBitmapDecoder 抽取最大帧 → strip 解码 → D2D 位图。
   File 路径和 Bytes 路径都走这个。失败返回 nullptr。 */
static ComPtr<ID2D1Bitmap> BitmapFromDecoder(IWICImagingFactory* wic,
                                              IWICBitmapDecoder* decoder,
                                              Renderer& r) {
    if (!wic || !decoder) return nullptr;

    /* ICO 包含多个尺寸，遍历找最大帧 */
    UINT frameCount = 0;
    decoder->GetFrameCount(&frameCount);
    UINT bestFrame = 0;
    UINT bestArea = 0;
    if (frameCount > 1) {
        for (UINT i = 0; i < frameCount; i++) {
            ComPtr<IWICBitmapFrameDecode> f;
            if (SUCCEEDED(decoder->GetFrame(i, f.GetAddressOf()))) {
                UINT fw = 0, fh = 0;
                f->GetSize(&fw, &fh);
                UINT area = fw * fh;
                if (area > bestArea) { bestArea = area; bestFrame = i; }
            }
        }
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    HRESULT hr = decoder->GetFrame(bestFrame, frame.GetAddressOf());
    if (FAILED(hr)) return nullptr;

    ComPtr<IWICFormatConverter> converter;
    hr = wic->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr)) return nullptr;

    hr = converter->Initialize(
        frame.Get(), GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeMedianCut);
    if (FAILED(hr)) return nullptr;

    UINT imgW = 0, imgH = 0;
    hr = converter->GetSize(&imgW, &imgH);
    if (FAILED(hr) || imgW == 0 || imgH == 0) return nullptr;

    /* 创建空 D2D 位图，避免 CreateBitmapFromWicBitmap 导致的双倍内存 */
    auto bitmap = r.CreateEmptyBitmap(static_cast<int>(imgW), static_cast<int>(imgH));
    if (!bitmap) return nullptr;

    /* 分条解码 + 上传：峰值内存 ≈ D2D 位图 + 一条带缓冲 */
    const UINT stripH = 512;
    const UINT stride = imgW * 4;
    const size_t stripBytes = static_cast<size_t>(stride) * stripH;
    auto stripBuf = std::make_unique<uint8_t[]>(stripBytes);

    for (UINT y = 0; y < imgH; y += stripH) {
        UINT rows = (y + stripH <= imgH) ? stripH : (imgH - y);
        WICRect rc = { 0, static_cast<INT>(y),
                       static_cast<INT>(imgW), static_cast<INT>(rows) };
        hr = converter->CopyPixels(&rc, stride, static_cast<UINT>(stride * rows),
                                   stripBuf.get());
        if (FAILED(hr)) return nullptr;

        D2D1_RECT_U dest = { 0, y, imgW, y + rows };
        hr = bitmap->CopyFromMemory(&dest, stripBuf.get(), stride);
        if (FAILED(hr)) return nullptr;
    }

    /* 立即释放 WIC 对象 + 条带缓冲，然后回收堆页 */
    stripBuf.reset();
    converter.Reset();
    frame.Reset();
    HeapCompact(GetProcessHeap(), 0);

    return bitmap;
}

ComPtr<ID2D1Bitmap> Renderer::LoadImageFromFile(const std::wstring& path) {
    if (!wicFactory_ || !ctx_) return nullptr;
    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = wicFactory_->CreateDecoderFromFilename(
        path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
    if (FAILED(hr)) return nullptr;
    return BitmapFromDecoder(wicFactory_, decoder.Get(), *this);
}

ComPtr<ID2D1Bitmap> Renderer::LoadImageFromBytes(const void* bytes, size_t size) {
    if (!wicFactory_ || !ctx_ || !bytes || size == 0) return nullptr;

    /* WIC 没有"从内存解码"直接 API，要走 IWICStream::InitializeFromMemory。
       SDK header 标了 InitializeFromMemory 的 buffer 形参为 BYTE*（非 const），
       但内部其实只读；强转 const_cast 是 WIC sample / 文档里推荐的标准用法。 */
    ComPtr<IWICStream> stream;
    HRESULT hr = wicFactory_->CreateStream(stream.GetAddressOf());
    if (FAILED(hr)) return nullptr;
    hr = stream->InitializeFromMemory(
        const_cast<BYTE*>(reinterpret_cast<const BYTE*>(bytes)),
        static_cast<DWORD>(size));
    if (FAILED(hr)) return nullptr;

    ComPtr<IWICBitmapDecoder> decoder;
    hr = wicFactory_->CreateDecoderFromStream(
        stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
    if (FAILED(hr)) return nullptr;
    return BitmapFromDecoder(wicFactory_, decoder.Get(), *this);
}

bool Renderer::DecodeImageFileToBgraPremul(const std::wstring& path,
                                           std::vector<uint8_t>& pixels,
                                           int& width, int& height, int& stride) {
    pixels.clear();
    width = height = stride = 0;
    if (!wicFactory_ || path.empty()) return false;

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = wicFactory_->CreateDecoderFromFilename(
        path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
    if (FAILED(hr)) return false;

    return DecodeDecoderToBgraPremul(wicFactory_, decoder.Get(), pixels, width, height, stride);
}

bool Renderer::DecodeImageBytesToBgraPremul(const void* bytes, size_t size,
                                            std::vector<uint8_t>& pixels,
                                            int& width, int& height, int& stride) {
    pixels.clear();
    width = height = stride = 0;
    if (!wicFactory_ || !bytes || size == 0) return false;

    ComPtr<IWICStream> stream;
    HRESULT hr = wicFactory_->CreateStream(stream.GetAddressOf());
    if (FAILED(hr)) return false;
    hr = stream->InitializeFromMemory(
        const_cast<BYTE*>(reinterpret_cast<const BYTE*>(bytes)),
        static_cast<DWORD>(size));
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapDecoder> decoder;
    hr = wicFactory_->CreateDecoderFromStream(
        stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
    if (FAILED(hr)) return false;

    return DecodeDecoderToBgraPremul(wicFactory_, decoder.Get(), pixels, width, height, stride);
}

/* ---- AnimatedPlayer（按需解码的 GIF 播放器） ---- */

Renderer::AnimatedPlayer::~AnimatedPlayer() = default;

int Renderer::AnimatedPlayer::DelayMs(int i) const {
    if (i < 0 || i >= (int)meta_.size()) return 100;
    return meta_[i].delayMs;
}

void Renderer::AnimatedPlayer::ResetCanvas() {
    std::fill(canvas_.begin(), canvas_.end(), 0);
    lastComposed_ = -1;
}

/* 合成第 index 帧的像素到画布（不处理 disposal，调用方控制）。 */
bool Renderer::AnimatedPlayer::DecodeOne(int index) {
    if (index < 0 || index >= (int)meta_.size()) return false;
    const FrameMeta& m = meta_[index];

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder_->GetFrame((UINT)index, frame.GetAddressOf()))) return false;

    ComPtr<IWICFormatConverter> conv;
    wic_->CreateFormatConverter(conv.GetAddressOf());
    if (!conv) return false;
    if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
                                 WICBitmapDitherTypeNone, nullptr, 0.0f,
                                 WICBitmapPaletteTypeMedianCut))) return false;

    const UINT fStride = m.w * 4;
    std::vector<uint8_t> framePx((size_t)fStride * m.h);
    if (FAILED(conv->CopyPixels(nullptr, fStride, (UINT)framePx.size(), framePx.data())))
        return false;

    const UINT stride = (UINT)canvasW_ * 4;
    for (UINT y = 0; y < m.h && (m.y + y) < (UINT)canvasH_; y++) {
        uint8_t* dst = canvas_.data() + ((size_t)m.y + y) * stride + (size_t)m.x * 4;
        uint8_t* src = framePx.data() + (size_t)y * fStride;
        for (UINT x = 0; x < m.w && (m.x + x) < (UINT)canvasW_; x++) {
            uint8_t sa = src[3];
            if (sa == 255) {
                dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = 255;
            } else if (sa > 0) {
                uint8_t da = dst[3];
                int oA = sa + (da * (255 - sa)) / 255;
                if (oA > 0) {
                    dst[0] = (uint8_t)((src[0]*sa + dst[0]*da*(255-sa)/255) / oA);
                    dst[1] = (uint8_t)((src[1]*sa + dst[1]*da*(255-sa)/255) / oA);
                    dst[2] = (uint8_t)((src[2]*sa + dst[2]*da*(255-sa)/255) / oA);
                    dst[3] = (uint8_t)oA;
                }
            }
            dst += 4; src += 4;
        }
    }
    return true;
}

const uint8_t* Renderer::AnimatedPlayer::ComposeTo(int frameIndex) {
    if (frameIndex < 0 || frameIndex >= (int)meta_.size() || canvas_.empty()) return nullptr;
    if (frameIndex == lastComposed_) return canvas_.data();

    /* 非顺序前进（跳帧或回绕）：从头重放 */
    if (frameIndex < lastComposed_) ResetCanvas();

    /* 推进：对每个 i in (lastComposed_, frameIndex]：
     *   1) 先把上一帧（lastComposed_）的 disposal 应用到画布，得到"合成 i 之前"的状态
     *   2) 若帧 i 的 disposal==3，此时备份画布（供 i 显示完后恢复到这里）
     *   3) 解码 + 合成帧 i
     * 这样返回时画布是"帧 frameIndex 显示时的状态"（该帧的 disposal 尚未应用）。 */
    const UINT stride = (UINT)canvasW_ * 4;
    for (int i = lastComposed_ + 1; i <= frameIndex; i++) {
        if (i > 0 && lastComposed_ == i - 1) {
            const FrameMeta& prev = meta_[i - 1];
            if (prev.disposal == 2) {
                UINT clearW = (UINT)canvasW_ > prev.x ? std::min<UINT>(prev.w, (UINT)canvasW_ - prev.x) : 0;
                for (UINT y = 0; y < prev.h && (prev.y + y) < (UINT)canvasH_; y++) {
                    uint8_t* dst = canvas_.data() + ((size_t)prev.y + y) * stride + (size_t)prev.x * 4;
                    memset(dst, 0, (size_t)clearW * 4);
                }
            } else if (prev.disposal == 3 && !prevCanvas_.empty()) {
                canvas_ = prevCanvas_;
            }
        }
        if (meta_[i].disposal == 3) prevCanvas_ = canvas_;
        if (!DecodeOne(i)) return canvas_.data();
        lastComposed_ = i;
    }
    return canvas_.data();
}

std::unique_ptr<Renderer::AnimatedPlayer> Renderer::OpenAnimatedImage(const std::wstring& path) {
    if (!wicFactory_) return nullptr;

    auto player = std::unique_ptr<AnimatedPlayer>(new AnimatedPlayer());
    player->wic_ = wicFactory_;

    HRESULT hr = wicFactory_->CreateDecoderFromFilename(
        path.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,  /* 对比 OnLoad：metadata 按需加载，open 更快 */
        player->decoder_.GetAddressOf());
    if (FAILED(hr)) return nullptr;

    UINT frameCount = 0;
    player->decoder_->GetFrameCount(&frameCount);
    if (frameCount <= 1) return nullptr;

    /* 全局画布尺寸 */
    UINT canvasW = 0, canvasH = 0;
    {
        ComPtr<IWICMetadataQueryReader> globalMeta;
        player->decoder_->GetMetadataQueryReader(globalMeta.GetAddressOf());
        if (globalMeta) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(globalMeta->GetMetadataByName(L"/logscrdesc/Width", &pv)))
                canvasW = pv.uiVal;
            PropVariantClear(&pv);
            PropVariantInit(&pv);
            if (SUCCEEDED(globalMeta->GetMetadataByName(L"/logscrdesc/Height", &pv)))
                canvasH = pv.uiVal;
            PropVariantClear(&pv);
        }
        if (canvasW == 0 || canvasH == 0) {
            ComPtr<IWICBitmapFrameDecode> f0;
            if (SUCCEEDED(player->decoder_->GetFrame(0, f0.GetAddressOf())) && f0)
                f0->GetSize(&canvasW, &canvasH);
        }
    }
    if (canvasW == 0 || canvasH == 0) return nullptr;
    player->canvasW_ = (int)canvasW;
    player->canvasH_ = (int)canvasH;
    player->canvas_.assign((size_t)canvasW * canvasH * 4, 0);

    /* 收集每帧元数据（不解码像素）。metadata 需要 GetFrame + QueryReader，
     * 但这一步很快 —— 119 帧 GIF 实测 <15ms。 */
    player->meta_.reserve(frameCount);
    for (UINT i = 0; i < frameCount; i++) {
        AnimatedPlayer::FrameMeta m{};
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(player->decoder_->GetFrame(i, frame.GetAddressOf())) || !frame) break;
        frame->GetSize(&m.w, &m.h);
        ComPtr<IWICMetadataQueryReader> meta;
        if (SUCCEEDED(frame->GetMetadataQueryReader(meta.GetAddressOf())) && meta) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(meta->GetMetadataByName(L"/grctlext/Delay", &pv))) {
                m.delayMs = pv.uiVal * 10;
                if (m.delayMs <= 0) m.delayMs = 100;
                if (m.delayMs < 20) m.delayMs = 20;
            }
            PropVariantClear(&pv);
            PropVariantInit(&pv);
            if (SUCCEEDED(meta->GetMetadataByName(L"/grctlext/Disposal", &pv)))
                m.disposal = pv.bVal;
            PropVariantClear(&pv);
            PropVariantInit(&pv);
            if (SUCCEEDED(meta->GetMetadataByName(L"/imgdesc/Left", &pv)))
                m.x = pv.uiVal;
            PropVariantClear(&pv);
            PropVariantInit(&pv);
            if (SUCCEEDED(meta->GetMetadataByName(L"/imgdesc/Top", &pv)))
                m.y = pv.uiVal;
            PropVariantClear(&pv);
        }
        player->meta_.push_back(m);
    }
    if (player->meta_.size() < 2) return nullptr;
    return player;
}

ComPtr<ID2D1Bitmap> Renderer::CreateBitmapFromPixels(const void* pixels, int width, int height, int stride) {
    if (!ctx_ || !pixels || width <= 0 || height <= 0) return nullptr;

    /* PREMULTIPLIED alpha → 正确显示带透明度的图片（PNG 等） */
    D2D1_BITMAP_PROPERTIES1 props = {};
    props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    ctx_->GetDpi(&props.dpiX, &props.dpiY);
    props.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;  /* GPU-only */

    ComPtr<ID2D1Bitmap1> bitmap1;
    HRESULT hr = ctx_->CreateBitmap(
        D2D1::SizeU(width, height),
        pixels, stride > 0 ? stride : width * 4,
        props, bitmap1.GetAddressOf());

    if (FAILED(hr)) return nullptr;
    ComPtr<ID2D1Bitmap> bitmap;
    bitmap1.As(&bitmap);
    return bitmap;
}

ComPtr<ID2D1Bitmap> Renderer::CreateBitmapFromPixelsStraight(
    const void* pixels, int width, int height, int stride) {
    if (!ctx_ || !pixels || width <= 0 || height <= 0) return nullptr;
    const int row = stride > 0 ? stride : width * 4;

    /* 把 straight BGRA 转成 premul 写到临时 buffer, 再走 PREMULTIPLIED 路径
       创建 D2D bitmap. round-to-nearest: (v*a + 127) / 255 ≈ round(v*a/255). */
    std::vector<uint8_t> premul(static_cast<size_t>(row) * height);
    const uint8_t* src = static_cast<const uint8_t*>(pixels);
    uint8_t*       dst = premul.data();
    for (int y = 0; y < height; ++y) {
        const uint8_t* sr = src + static_cast<size_t>(y) * row;
        uint8_t*       dr = dst + static_cast<size_t>(y) * row;
        for (int x = 0; x < width; ++x) {
            uint8_t a = sr[3];
            if (a == 255) {
                dr[0] = sr[0]; dr[1] = sr[1]; dr[2] = sr[2]; dr[3] = 255;
            } else if (a == 0) {
                dr[0] = 0; dr[1] = 0; dr[2] = 0; dr[3] = 0;
            } else {
                dr[0] = static_cast<uint8_t>((sr[0] * a + 127) / 255);
                dr[1] = static_cast<uint8_t>((sr[1] * a + 127) / 255);
                dr[2] = static_cast<uint8_t>((sr[2] * a + 127) / 255);
                dr[3] = a;
            }
            sr += 4; dr += 4;
        }
    }
    return CreateBitmapFromPixels(premul.data(), width, height, row);
}

bool Renderer::DecodeHICONToBgraPremul(HICON hicon,
                                       std::vector<uint8_t>& pixels,
                                       int& width, int& height, int& stride) {
    pixels.clear();
    width = height = stride = 0;
    if (!wicFactory_ || !hicon) return false;

    /* WIC 直接消化 HICON：解开 AND mask + color bits，输出 32bpp BGRA */
    ComPtr<IWICBitmap> wicBmp;
    HRESULT hr = wicFactory_->CreateBitmapFromHICON(hicon, wicBmp.GetAddressOf());
    if (FAILED(hr)) return false;

    /* 转成 DisplayList 资源和 D2D 都偏好的 premultiplied PBGRA。 */
    ComPtr<IWICFormatConverter> converter;
    hr = wicFactory_->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr)) return false;
    hr = converter->Initialize(
        wicBmp.Get(), GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeMedianCut);
    if (FAILED(hr)) return false;

    UINT imgW = 0, imgH = 0;
    hr = converter->GetSize(&imgW, &imgH);
    if (FAILED(hr) || imgW == 0 || imgH == 0) return false;
    if (imgW > static_cast<UINT>(std::numeric_limits<int>::max() / 4) ||
        imgH > static_cast<UINT>(std::numeric_limits<int>::max())) {
        return false;
    }

    const UINT rowStride = imgW * 4;
    const uint64_t totalBytes = static_cast<uint64_t>(rowStride) * imgH;
    if (totalBytes > static_cast<uint64_t>(std::numeric_limits<UINT>::max()) ||
        totalBytes > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return false;
    }

    std::vector<uint8_t> out(static_cast<size_t>(totalBytes));
    hr = converter->CopyPixels(nullptr, rowStride, static_cast<UINT>(out.size()), out.data());
    if (FAILED(hr)) return false;

    pixels = std::move(out);
    width = static_cast<int>(imgW);
    height = static_cast<int>(imgH);
    stride = static_cast<int>(rowStride);
    return true;
}

ComPtr<ID2D1Bitmap> Renderer::CreateBitmapFromHICON(HICON hicon) {
    std::vector<uint8_t> pixels;
    int width = 0, height = 0, stride = 0;
    if (!DecodeHICONToBgraPremul(hicon, pixels, width, height, stride)) return nullptr;

    return CreateBitmapFromPixels(pixels.data(), width, height, stride);
}

ComPtr<ID2D1Bitmap> Renderer::CreateEmptyBitmap(int width, int height) {
    if (!ctx_ || width <= 0 || height <= 0) return nullptr;

    D2D1_BITMAP_PROPERTIES1 props = {};
    props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    ctx_->GetDpi(&props.dpiX, &props.dpiY);
    props.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;  /* GPU-only */

    ComPtr<ID2D1Bitmap1> bitmap1;
    HRESULT hr = ctx_->CreateBitmap(
        D2D1::SizeU(width, height),
        nullptr, 0,
        props, bitmap1.GetAddressOf());

    if (FAILED(hr)) return nullptr;
    ComPtr<ID2D1Bitmap> bitmap;
    bitmap1.As(&bitmap);
    return bitmap;
}

void Renderer::DrawBitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F& destRect, float opacity,
                           D2D1_BITMAP_INTERPOLATION_MODE interp) {
    if (bitmap && ctx_) {
        /* 检查 bitmap 是否有 alpha 通道需要混合 */
        auto fmt = bitmap->GetPixelFormat();
        bool hasAlpha = (fmt.alphaMode == D2D1_ALPHA_MODE_PREMULTIPLIED ||
                         fmt.alphaMode == D2D1_ALPHA_MODE_STRAIGHT);
        if (!hasAlpha) {
            /* 不透明位图：用 COPY blend 跳过 sRGB gamma 转换，颜色准确 */
            ctx_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
        }
        ctx_->DrawBitmap(bitmap, destRect, opacity, interp);
        if (!hasAlpha) {
            ctx_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
        }
    }
}

void Renderer::DrawBitmapHQ(ID2D1Bitmap* bitmap, const D2D1_RECT_F& destRect,
                            float opacity, D2D1_INTERPOLATION_MODE interp) {
    if (!bitmap || !ctx_) return;
    auto fmt = bitmap->GetPixelFormat();
    bool hasAlpha = (fmt.alphaMode == D2D1_ALPHA_MODE_PREMULTIPLIED ||
                     fmt.alphaMode == D2D1_ALPHA_MODE_STRAIGHT);
    if (!hasAlpha) {
        ctx_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_COPY);
    }
    /* ID2D1DeviceContext::DrawBitmap 支持 D2D1_INTERPOLATION_MODE */
    ctx_->DrawBitmap(bitmap, destRect, opacity, interp, nullptr);
    if (!hasAlpha) {
        ctx_->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
    }
}

void Renderer::DrawBitmapClamped(ID2D1Bitmap* bitmap, const D2D1_RECT_F& destRect,
                                 float opacity, D2D1_INTERPOLATION_MODE interp) {
    if (!bitmap || !ctx_) return;
    /* 采样与 DrawBitmapHQ 完全一致 (含 HQ 缩小 prefilter; DrawBitmap 的源
     * 采样本身在位图边缘 clamp, 不渗透明) — 只关掉矩形边缘 AA:
     * PER_PRIMITIVE AA 在相邻瓦块共享边界给两边各画一条部分覆盖边, over
     * 合成后该行 alpha ≈ 0.75 < 1 → 拼缝一条半透明缝线 (放大时一目了然,
     * 透明图透出背景)。aliased 光栅化按同一舍入规则分配边界像素, 无缝。
     * 注: 曾试过 BitmapBrush1+EXTEND_CLAMP+FillRectangle — 画刷路径没有
     * DrawBitmap 的 HQ prefilter, 缩小/上采 gradient 处出复制行细线, 弃用。 */
    const D2D1_ANTIALIAS_MODE oldAA = ctx_->GetAntialiasMode();
    ctx_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    DrawBitmapHQ(bitmap, destRect, opacity, interp);
    ctx_->SetAntialiasMode(oldAA);
}

void Renderer::DrawBitmapSharpened(ID2D1Bitmap* bitmap, const D2D1_RECT_F& destRect,
                                    float sharpenAmount, D2D1_INTERPOLATION_MODE interp) {
    if (!bitmap || !ctx_) return;

    /* 3x3 Unsharp Mask 卷积核 */
    ComPtr<ID2D1Effect> sharpenEffect;
    HRESULT hr = ctx_->CreateEffect(CLSID_D2D1ConvolveMatrix, &sharpenEffect);
    if (FAILED(hr)) {
        /* fallback */
        ctx_->DrawBitmap(bitmap, destRect, 1.0f, interp);
        return;
    }

    float a = sharpenAmount;
    float kernel[] = {
         0,  -a,  0,
        -a, 1+4*a, -a,
         0,  -a,  0
    };
    sharpenEffect->SetInput(0, bitmap);
    sharpenEffect->SetValue(D2D1_CONVOLVEMATRIX_PROP_KERNEL_SIZE_X, (UINT32)3);
    sharpenEffect->SetValue(D2D1_CONVOLVEMATRIX_PROP_KERNEL_SIZE_Y, (UINT32)3);
    sharpenEffect->SetValue(D2D1_CONVOLVEMATRIX_PROP_KERNEL_MATRIX, kernel);

    /* 用变换矩阵实现缩放+平移 */
    auto bmpSize = bitmap->GetSize();
    float scaleX = (destRect.right - destRect.left) / bmpSize.width;
    float scaleY = (destRect.bottom - destRect.top) / bmpSize.height;

    D2D1_MATRIX_3X2_F oldXform;
    ctx_->GetTransform(&oldXform);
    ctx_->SetTransform(
        D2D1::Matrix3x2F::Scale(scaleX, scaleY) *
        D2D1::Matrix3x2F::Translation(destRect.left, destRect.top) *
        oldXform);

    ctx_->DrawImage(sharpenEffect.Get(), D2D1::Point2F(0, 0),
                     D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);

    ctx_->SetTransform(oldXform);
}

void Renderer::RecordImage(ResourceKey key, const D2D1_RECT_F& destRect,
                           ImageSampling sampling, float opacity) {
    auto* recorder = ActiveDisplayListRecorder();
    if (!recorder || !key.IsValid()) return;

    ImageRef image;
    image.key = key;
    auto res = GlobalResourceStore().Acquire(key);
    if (res) {
        image.width = res->width;
        image.height = res->height;
        image.stride = res->stride;
        image.format = res->format;
    }
    recorder->DrawImage(image, destRect, sampling, opacity);
}

ComPtr<ID2D1Bitmap> Renderer::GetCachedImageBitmap(ResourceKey key) {
    if (!ctx_ || !key.IsValid()) return {};
    auto cached = imageBitmapCache_.find(key);
    if (cached != imageBitmapCache_.end() && cached->second) {
        return cached->second;
    }

    auto res = GlobalResourceStore().Acquire(key);
    if (!res || !res->bytes) return {};

    ComPtr<ID2D1Bitmap> bitmap;
    switch (res->format) {
    case PixelFormat::BgraPremul:
    case PixelFormat::Rgba:
        bitmap = CreateBitmapFromPixels(res->bytes->data(), res->width, res->height, res->stride);
        break;
    case PixelFormat::BgraStraight:
        bitmap = CreateBitmapFromPixelsStraight(res->bytes->data(), res->width, res->height, res->stride);
        break;
    }
    if (!bitmap) return {};

    imageBitmapCache_[key] = bitmap;
    TraceEvent("core_renderer", "resource_bitmap_upload",
               {TraceU64("generation", key.image_generation),
                TraceU64("resource_id", key.resource_id),
                TraceU64("kind", static_cast<uint64_t>(key.kind)),
                TraceU64("bytes", static_cast<uint64_t>(res->byte_size)),
                TraceU64("cache_size", static_cast<uint64_t>(imageBitmapCache_.size()))});
    return bitmap;
}

void Renderer::DrawImageResource(ResourceKey key, const D2D1_RECT_F& destRect,
                                 ImageSampling sampling, float opacity) {
    RecordImage(key, destRect, sampling, opacity);
    if (!ctx_) return;

    auto bitmap = GetCachedImageBitmap(key);
    if (!bitmap) return;

    if (sampling == ImageSampling::HighQualityCubicClamp ||
        sampling == ImageSampling::LinearClamp) {
        DrawBitmapClamped(bitmap.Get(), destRect, opacity,
                          sampling == ImageSampling::HighQualityCubicClamp
                              ? D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC
                              : D2D1_INTERPOLATION_MODE_LINEAR);
    } else if (sampling == ImageSampling::HighQualityCubic) {
        DrawBitmapHQ(bitmap.Get(), destRect, opacity,
                     D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
    } else {
        auto interp = (sampling == ImageSampling::Nearest)
            ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
            : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
        DrawBitmap(bitmap.Get(), destRect, opacity, interp);
    }
}

void Renderer::PruneImageBitmapCacheTo(const std::vector<ImageRef>& imageRefs) {
    if (imageBitmapCache_.empty()) return;
    std::unordered_set<ResourceKey, ResourceKeyHash> live;
    live.reserve(imageRefs.size());
    for (const auto& ref : imageRefs) {
        if (ref.key.IsValid()) live.insert(ref.key);
    }

    size_t removed = 0;
    for (auto it = imageBitmapCache_.begin(); it != imageBitmapCache_.end(); ) {
        if (live.find(it->first) != live.end()) {
            ++it;
        } else {
            it = imageBitmapCache_.erase(it);
            ++removed;
        }
    }
    if (removed > 0) {
        TraceEvent("core_renderer", "resource_bitmap_cache_pruned",
                   {TraceU64("removed", static_cast<uint64_t>(removed)),
                    TraceU64("cache_size", static_cast<uint64_t>(imageBitmapCache_.size()))});
    }
}

void Renderer::FillRectWithImagePattern(ResourceKey key, ID2D1Bitmap* bitmap,
                                        const D2D1_RECT_F& rect) {
    auto* recorder = ActiveDisplayListRecorder();
    if (recorder && key.IsValid()) {
        ImageRef image;
        image.key = key;
        auto res = GlobalResourceStore().Acquire(key);
        if (res) {
            image.width = res->width;
            image.height = res->height;
            image.stride = res->stride;
            image.format = res->format;
        }
        recorder->FillImagePattern(image, rect);
    }

    if (!ctx_) return;

    ComPtr<ID2D1Bitmap> localBitmap;
    if (!bitmap && key.IsValid()) {
        localBitmap = GetCachedImageBitmap(key);
        bitmap = localBitmap.Get();
    }

    FillRectWithBitmap(bitmap, rect);
}

void Renderer::FillGradientRect(GradientRef gradient, const D2D1_RECT_F& rect, float radius) {
    if (auto* recorder = ActiveDisplayListRecorder()) {
        recorder->FillGradient(gradient, rect, radius);
    }
    if (!ctx_) return;
    PaintDisplayListGradient(ctx_.Get(), rect, gradient, radius);
}

void Renderer::RecordSvgDocument(std::vector<SvgDocumentRef::Step> steps,
                                 float viewportW, float viewportH,
                                 D2D1_MATRIX_3X2_F transform) {
    auto* recorder = ActiveDisplayListRecorder();
    if (!recorder || steps.empty() || viewportW <= 0.0f || viewportH <= 0.0f) return;

    SvgDocumentRef ref;
    ref.steps = std::move(steps);
    ref.viewport_w = viewportW;
    ref.viewport_h = viewportH;
    recorder->DrawSvgDocument(std::move(ref), transform);
}

void Renderer::RecordSvgDocument(std::string xml, float viewportW, float viewportH,
                                 D2D1_MATRIX_3X2_F transform) {
    if (xml.empty()) return;
    std::vector<SvgDocumentRef::Step> steps(1);
    steps[0].xml = std::move(xml);
    RecordSvgDocument(std::move(steps), viewportW, viewportH, transform);
}

bool Renderer::DrawBackdropBlur(const D2D1_RECT_F& rect, float radius, float blurRadius) {
    if (auto* recorder = ActiveDisplayListRecorder()) {
        recorder->DrawBackdropBlur(rect, radius, blurRadius);
    }
    if (!ctx_ || blurRadius <= 0.5f) return false;
    if (rect.right <= rect.left || rect.bottom <= rect.top) return false;

    ComPtr<ID2D1Image> targetImage;
    ctx_->GetTarget(targetImage.GetAddressOf());
    ComPtr<ID2D1Bitmap1> target;
    if (!targetImage || FAILED(targetImage.As(&target)) || !target) return false;

    D2D1_MATRIX_3X2_F oldXform = D2D1::Matrix3x2F::Identity();
    ctx_->GetTransform(&oldXform);
    const float det = oldXform._11 * oldXform._22 - oldXform._12 * oldXform._21;
    if (std::fabs(det) < 1e-6f) return false;
    D2D1_MATRIX_3X2_F inv = {};
    inv._11 =  oldXform._22 / det;
    inv._12 = -oldXform._12 / det;
    inv._21 = -oldXform._21 / det;
    inv._22 =  oldXform._11 / det;
    inv._31 = (oldXform._21 * oldXform._32 - oldXform._31 * oldXform._22) / det;
    inv._32 = (oldXform._12 * oldXform._31 - oldXform._11 * oldXform._32) / det;

    auto transformPoint = [](const D2D1_MATRIX_3X2_F& m, D2D1_POINT_2F p) {
        return D2D1::Point2F(
            p.x * m._11 + p.y * m._21 + m._31,
            p.x * m._12 + p.y * m._22 + m._32);
    };
    auto toDeviceRect = [&](const D2D1_RECT_F& r) {
        D2D1_POINT_2F p0 = transformPoint(oldXform, D2D1::Point2F(r.left,  r.top));
        D2D1_POINT_2F p1 = transformPoint(oldXform, D2D1::Point2F(r.right, r.top));
        D2D1_POINT_2F p2 = transformPoint(oldXform, D2D1::Point2F(r.right, r.bottom));
        D2D1_POINT_2F p3 = transformPoint(oldXform, D2D1::Point2F(r.left,  r.bottom));
        return D2D1::RectF(std::min({p0.x, p1.x, p2.x, p3.x}),
                           std::min({p0.y, p1.y, p2.y, p3.y}),
                           std::max({p0.x, p1.x, p2.x, p3.x}),
                           std::max({p0.y, p1.y, p2.y, p3.y}));
    };

    float dpiX = 96.0f, dpiY = 96.0f;
    ctx_->GetDpi(&dpiX, &dpiY);
    const float sx = dpiX / 96.0f;
    const float sy = dpiY / 96.0f;
    const float pad = std::ceil(blurRadius * 3.0f);
    D2D1_RECT_F captureDip = toDeviceRect({rect.left - pad, rect.top - pad,
                                           rect.right + pad, rect.bottom + pad});

    auto targetSize = target->GetPixelSize();
    auto floorClamp = [](float v, float scale, UINT maxv) -> UINT {
        int iv = static_cast<int>(std::floor(v * scale));
        iv = std::max(0, std::min(iv, static_cast<int>(maxv)));
        return static_cast<UINT>(iv);
    };
    auto ceilClamp = [](float v, float scale, UINT maxv) -> UINT {
        int iv = static_cast<int>(std::ceil(v * scale));
        iv = std::max(0, std::min(iv, static_cast<int>(maxv)));
        return static_cast<UINT>(iv);
    };

    D2D1_RECT_U srcPx{
        floorClamp(captureDip.left,   sx, targetSize.width),
        floorClamp(captureDip.top,    sy, targetSize.height),
        ceilClamp (captureDip.right,  sx, targetSize.width),
        ceilClamp (captureDip.bottom, sy, targetSize.height),
    };
    if (srcPx.right <= srcPx.left || srcPx.bottom <= srcPx.top) return false;

    UINT w = srcPx.right - srcPx.left;
    UINT h = srcPx.bottom - srcPx.top;
    D2D1_BITMAP_PROPERTIES1 props = {};
    props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                           D2D1_ALPHA_MODE_PREMULTIPLIED);
    props.dpiX = dpiX;
    props.dpiY = dpiY;
    props.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;

    ComPtr<ID2D1Bitmap1> backdrop;
    HRESULT hr = ctx_->CreateBitmap(D2D1::SizeU(w, h), nullptr, 0,
                                    props, backdrop.GetAddressOf());
    if (FAILED(hr) || !backdrop) return false;

    ctx_->Flush();
    hr = backdrop->CopyFromBitmap(nullptr, target.Get(), &srcPx);
    if (FAILED(hr)) return false;

    ComPtr<ID2D1Effect> blur;
    hr = ctx_->CreateEffect(CLSID_D2D1GaussianBlur, blur.GetAddressOf());
    if (FAILED(hr) || !blur) return false;
    blur->SetInput(0, backdrop.Get());
    blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, blurRadius);
    blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);

    D2D1_RECT_F captureLocal{
        static_cast<float>(srcPx.left) / sx,
        static_cast<float>(srcPx.top) / sy,
        static_cast<float>(srcPx.right) / sx,
        static_cast<float>(srcPx.bottom) / sy,
    };
    D2D1_POINT_2F origin = transformPoint(inv, D2D1::Point2F(captureLocal.left,
                                                             captureLocal.top));

    ++displayListRecorderPauseDepth_;
    if (radius > 0.0f) PushRoundedClip(rect, radius, radius);
    else PushClip(rect);
    ctx_->DrawImage(blur.Get(), origin,
                    D2D1_INTERPOLATION_MODE_LINEAR,
                    D2D1_COMPOSITE_MODE_SOURCE_OVER);
    if (radius > 0.0f) PopRoundedClip();
    else PopClip();
    --displayListRecorderPauseDepth_;
    return true;
}

void Renderer::FillRectWithBitmap(ID2D1Bitmap* bitmap, const D2D1_RECT_F& rect) {
    if (!ctx_ || !bitmap) return;

    D2D1_BITMAP_BRUSH_PROPERTIES bbProps = D2D1::BitmapBrushProperties(
        D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP,
        D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    D2D1_BRUSH_PROPERTIES bProps = D2D1::BrushProperties();

    ComPtr<ID2D1BitmapBrush> brush;
    HRESULT hr = ctx_->CreateBitmapBrush(bitmap, bbProps, bProps, brush.GetAddressOf());
    if (SUCCEEDED(hr) && brush) {
        ctx_->FillRectangle(rect, brush.Get());
    }
}

void Renderer::PushClip(const D2D1_RECT_F& rect) {
    if (auto* recorder = ActiveDisplayListRecorder()) {
        recorder->PushClip(rect);
    }
    if (!ctx_) return;
    ctx_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void Renderer::PushCull(const D2D1_RECT_F& rect) {
    /* 与外层求交: 嵌套滚动容器时内层可见范围不可能大于外层。 */
    if (cullStack_.empty()) {
        cullStack_.push_back(rect);
        return;
    }
    const D2D1_RECT_F& outer = cullStack_.back();
    cullStack_.push_back({
        (std::max)(rect.left,   outer.left),
        (std::max)(rect.top,    outer.top),
        (std::min)(rect.right,  outer.right),
        (std::min)(rect.bottom, outer.bottom)});
}

void Renderer::PopCull() {
    if (!cullStack_.empty()) cullStack_.pop_back();
}

bool Renderer::IsCulled(const D2D1_RECT_F& rect) const {
    if (cullStack_.empty()) return false;   /* opt-in: 没人 push 就不剔除 */
    const D2D1_RECT_F& c = cullStack_.back();
    /* 完全不相交才剔除。边界相接 (right == c.left) 视为不可见 —— 零宽重叠
     * 画不出任何像素。 */
    return rect.right  <= c.left  || rect.left >= c.right ||
           rect.bottom <= c.top   || rect.top  >= c.bottom;
}

void Renderer::PushClipAliased(const D2D1_RECT_F& rect) {
    if (auto* recorder = ActiveDisplayListRecorder()) {
        recorder->PushClipAliased(rect);
    }
    if (!ctx_) return;
    /* ALIASED: 相邻 clip 矩形拼接绘制同一内容时, PER_PRIMITIVE 会在共享
     * 边界两侧各画一条部分覆盖边 → 合成出一条半透明缝线。 */
    ctx_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);
}

void Renderer::PopClip() {
    if (auto* recorder = ActiveDisplayListRecorder()) {
        recorder->PopClip();
    }
    if (!ctx_) return;
    ctx_->PopAxisAlignedClip();
}

void Renderer::PushRoundedClip(const D2D1_RECT_F& rect, float rx, float ry) {
    if (auto* recorder = ActiveDisplayListRecorder()) {
        recorder->PushRoundedClip(rect, rx, ry);
    }
    if (!factory_ || !ctx_) return;
    ComPtr<ID2D1RoundedRectangleGeometry> geom;
    factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rect, rx, ry),
                                             geom.GetAddressOf());
    ComPtr<ID2D1Layer> layer;
    ctx_->CreateLayer(nullptr, layer.GetAddressOf());
    if (geom && layer) {
        /* D2D1_LAYER_OPTIONS_INITIALIZE_FOR_CLEARTYPE — 必须 (build 96+ L25):
         * layer 默认 OPTIONS_NONE 会把 ClearType sub-pixel 渲染关掉, layer 内
         * DrawText 在 CLEARTYPE 模式 (lib build 92+ 默认) 下文字几乎不可见.
         * 典型表现: 浅色模式 ComboBox 弹出 popup 里的 item 文字白底白字看不见
         * (popup 先 PushRoundedClip 再画 text). INITIALIZE_FOR_CLEARTYPE 告诉
         * D2D layer backing 已经初始化为不透明色, sub-pixel blend 可以正确合成. */
        ctx_->PushLayer(
            D2D1::LayerParameters(D2D1::InfiniteRect(), geom.Get(),
                                  D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                  D2D1::Matrix3x2F::Identity(), 1.0f,
                                  nullptr,
                                  D2D1_LAYER_OPTIONS_INITIALIZE_FOR_CLEARTYPE),
            layer.Get());
    }
}

void Renderer::PopRoundedClip() {
    if (auto* recorder = ActiveDisplayListRecorder()) {
        recorder->PopRoundedClip();
    }
    if (!ctx_) return;
    ctx_->PopLayer();
}

void Renderer::PushOpacity(float opacity, const D2D1_RECT_F& bounds) {
    if (auto* recorder = ActiveDisplayListRecorder()) {
        recorder->PushOpacity(opacity, bounds);
    }
    if (!ctx_) return;
    ComPtr<ID2D1Layer> layer;
    ctx_->CreateLayer(nullptr, layer.GetAddressOf());
    if (layer) {
        ctx_->PushLayer(
            D2D1::LayerParameters(bounds, nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                  D2D1::Matrix3x2F::Identity(), opacity),
            layer.Get());
    }
}

void Renderer::PopOpacity() {
    if (auto* recorder = ActiveDisplayListRecorder()) {
        recorder->PopOpacity();
    }
    if (!ctx_) return;
    ctx_->PopLayer();
}

void Renderer::PushTransform(const D2D1_MATRIX_3X2_F& transform) {
    if (auto* recorder = ActiveDisplayListRecorder()) {
        recorder->PushTransform(transform);
    }
    if (!ctx_) return;
    D2D1_MATRIX_3X2_F saved = D2D1::Matrix3x2F::Identity();
    ctx_->GetTransform(&saved);
    transformStack_.push_back(saved);
    ctx_->SetTransform(transform * saved);
}

void Renderer::PopTransform() {
    if (auto* recorder = ActiveDisplayListRecorder()) {
        recorder->PopTransform();
    }
    if (!ctx_ || transformStack_.empty()) return;
    ctx_->SetTransform(transformStack_.back());
    transformStack_.pop_back();
}

namespace svg_detail {
static bool BuildGeometry(ID2D1Factory* factory, const std::vector<std::string>& pathDatas,
                          ID2D1PathGeometry** outGeometry);
}

void Renderer::ReplayDisplayList(const DisplayList& list) {
    if (!ctx_) return;
    std::vector<D2D1_MATRIX_3X2_F> transformStack;
    for (const auto& cmd : list.commands) {
        switch (cmd.type) {
        case DrawCommandType::Clear:
            Clear(cmd.color);
            break;
        case DrawCommandType::PushClip:
            PushClip(cmd.rect);
            break;
        case DrawCommandType::PushClipAliased:
            PushClipAliased(cmd.rect);
            break;
        case DrawCommandType::PopClip:
            PopClip();
            break;
        case DrawCommandType::PushRoundedClip:
            PushRoundedClip(cmd.rect, cmd.radius_x, cmd.radius_y);
            break;
        case DrawCommandType::PopRoundedClip:
            PopRoundedClip();
            break;
        case DrawCommandType::PushOpacity:
            PushOpacity(cmd.opacity, cmd.rect);
            break;
        case DrawCommandType::PopOpacity:
            PopOpacity();
            break;
        case DrawCommandType::PushTransform: {
            D2D1_MATRIX_3X2_F saved = D2D1::Matrix3x2F::Identity();
            ctx_->GetTransform(&saved);
            transformStack.push_back(saved);
            ctx_->SetTransform(cmd.transform * saved);
            break;
        }
        case DrawCommandType::PopTransform:
            if (!transformStack.empty()) {
                ctx_->SetTransform(transformStack.back());
                transformStack.pop_back();
            }
            break;
        case DrawCommandType::FillRect:
            FillRect(cmd.rect, cmd.color);
            break;
        case DrawCommandType::FillQuad:
            if (cmd.quad_index < list.quad_refs.size())
                FillQuad(list.quad_refs[cmd.quad_index].pts, cmd.color);
            break;
        case DrawCommandType::DrawRect:
            DrawRect(cmd.rect, cmd.color, cmd.stroke_width);
            break;
        case DrawCommandType::FillRoundedRect:
            FillRoundedRect(cmd.rect, cmd.radius_x, cmd.radius_y, cmd.color);
            break;
        case DrawCommandType::DrawRoundedRect:
            DrawRoundedRect(cmd.rect, cmd.radius_x, cmd.radius_y, cmd.color, cmd.stroke_width);
            break;
        case DrawCommandType::DrawBlurredRoundedRect:
            DrawBlurredRoundedRect(cmd.rect, cmd.radius_x, cmd.radius_y,
                                   cmd.blur_radius, cmd.color);
            break;
        case DrawCommandType::DrawLine:
            DrawLine(cmd.p0.x, cmd.p0.y, cmd.p1.x, cmd.p1.y, cmd.color, cmd.stroke_width);
            break;
        case DrawCommandType::DrawText: {
            if (cmd.text_index >= list.text_pool.size()) break;
            DrawText(list.text_pool[cmd.text_index],
                     cmd.rect,
                     cmd.text_style.color,
                     cmd.text_style.font_size,
                     static_cast<DWRITE_TEXT_ALIGNMENT>(cmd.text_style.alignment),
                     static_cast<DWRITE_FONT_WEIGHT>(cmd.text_style.weight),
                     static_cast<DWRITE_PARAGRAPH_ALIGNMENT>(cmd.text_style.paragraph_alignment),
                     cmd.text_style.word_wrap,
                     cmd.text_style.font_family.empty() ? nullptr : cmd.text_style.font_family.c_str());
            break;
        }
        case DrawCommandType::DrawImage: {
            if (cmd.image_ref_index >= list.image_refs.size()) break;
            const auto& ref = list.image_refs[cmd.image_ref_index];
            DrawImageResource(ref.key, cmd.rect, cmd.sampling, cmd.opacity);
            break;
        }
        case DrawCommandType::FillImagePattern: {
            if (cmd.image_ref_index >= list.image_refs.size()) break;
            const auto& ref = list.image_refs[cmd.image_ref_index];
            FillRectWithImagePattern(ref.key, nullptr, cmd.rect);
            break;
        }
        case DrawCommandType::FillGradient: {
            if (cmd.gradient_ref_index >= list.gradient_refs.size()) break;
            FillGradientRect(list.gradient_refs[cmd.gradient_ref_index], cmd.rect, cmd.radius_x);
            break;
        }
        case DrawCommandType::DrawSvgIcon: {
            if (cmd.svg_ref_index >= list.svg_icon_refs.size()) break;
            const auto& icon = list.svg_icon_refs[cmd.svg_ref_index];
            if (!factory_ || icon.view_box_w <= 0.0f || icon.view_box_h <= 0.0f) break;

            float destW = cmd.rect.right - cmd.rect.left;
            float destH = cmd.rect.bottom - cmd.rect.top;
            if (destW <= 0.0f || destH <= 0.0f) break;
            float scale = std::min(destW / icon.view_box_w, destH / icon.view_box_h);
            float offX = cmd.rect.left + (destW - icon.view_box_w * scale) / 2.0f;
            float offY = cmd.rect.top + (destH - icon.view_box_h * scale) / 2.0f;

            D2D1_MATRIX_3X2_F oldXform = D2D1::Matrix3x2F::Identity();
            ctx_->GetTransform(&oldXform);
            auto iconTransform = D2D1::Matrix3x2F::Scale(scale, scale) *
                                 D2D1::Matrix3x2F::Translation(offX, offY) *
                                 oldXform;

            if (!icon.layers.empty()) {
                for (const auto& layer : icon.layers) {
                    if (layer.path_data.empty()) continue;
                    ID2D1PathGeometry* raw = nullptr;
                    if (!svg_detail::BuildGeometry(factory_, layer.path_data, &raw)) continue;
                    ComPtr<ID2D1PathGeometry> geom;
                    geom.Attach(raw);
                    D2D1_COLOR_F layerColor = cmd.color;
                    layerColor.a *= layer.opacity;
                    auto brush = GetBrush(layerColor);
                    if (!brush) continue;
                    ctx_->SetTransform(layer.transform * iconTransform);
                    if (layer.stroke_width > 0.0f) {
                        ctx_->DrawGeometry(geom.Get(), brush.Get(),
                                           layer.stroke_width, GetRoundStrokeStyle());
                    } else {
                        ctx_->FillGeometry(geom.Get(), brush.Get());
                    }
                }
            } else if (!icon.path_data.empty()) {
                ID2D1PathGeometry* raw = nullptr;
                if (svg_detail::BuildGeometry(factory_, icon.path_data, &raw)) {
                    ComPtr<ID2D1PathGeometry> geom;
                    geom.Attach(raw);
                    auto brush = GetBrush(cmd.color);
                    if (brush) {
                        ctx_->SetTransform(iconTransform);
                        ctx_->FillGeometry(geom.Get(), brush.Get());
                    }
                }
            }
            ctx_->SetTransform(oldXform);
            break;
        }
        case DrawCommandType::DrawSvgDocument: {
            if (cmd.svg_document_ref_index >= list.svg_document_refs.size()) break;
            const auto& ref = list.svg_document_refs[cmd.svg_document_ref_index];
            if (!ctx5_ || ref.steps.empty() || ref.viewport_w <= 0.0f ||
                ref.viewport_h <= 0.0f) {
                break;
            }

            D2D1_MATRIX_3X2_F oldXform = D2D1::Matrix3x2F::Identity();
            ctx5_->GetTransform(&oldXform);
            DrawSvgStepsOrdered(ctx5_.Get(), ref.steps, ref.viewport_w,
                                ref.viewport_h, cmd.transform * oldXform,
                                nullptr, nullptr);
            ctx5_->SetTransform(oldXform);
            break;
        }
        case DrawCommandType::DrawSvgTextRuns: {
            if (cmd.svg_text_ref_index >= list.svg_text_refs.size()) break;
            const auto& ref = list.svg_text_refs[cmd.svg_text_ref_index];
            DrawSvgTextRuns(ref.runs, ref.base_transform);
            break;
        }
        case DrawCommandType::DrawBackdropBlur:
            DrawBackdropBlur(cmd.rect, cmd.radius_x, cmd.blur_radius);
            break;
        }
    }
    PruneImageBitmapCacheTo(list.image_refs);
}

// ---- SVG icon parsing and rendering ----

// Lightweight SVG path parser: extracts viewBox + path d, builds ID2D1PathGeometry
namespace svg_detail {

static void SkipWS(const char*& p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')) ++p;
}

static float ParseFloat(const char*& p) {
    SkipWS(p);
    char* end = nullptr;
    float v = strtof(p, &end);
    if (end == p) return 0;
    p = end;
    SkipWS(p);
    return v;
}

static bool ParseFlag(const char*& p) {
    SkipWS(p);
    bool v = (*p == '1');
    if (*p == '0' || *p == '1') ++p;
    SkipWS(p);
    return v;
}

// Extract attribute value from SVG XML by name
static std::string ExtractAttr(const std::string& svg, const std::string& tag,
                                const std::string& attr) {
    auto tagPos = svg.find("<" + tag);
    if (tagPos == std::string::npos) {
        // Try self-closing or any element
        tagPos = svg.find(tag);
        if (tagPos == std::string::npos) return "";
    }
    auto attrPos = svg.find(attr + "=\"", tagPos);
    if (attrPos == std::string::npos) {
        attrPos = svg.find(attr + "='", tagPos);
        if (attrPos == std::string::npos) return "";
        auto start = attrPos + attr.length() + 2;
        auto end = svg.find("'", start);
        return (end != std::string::npos) ? svg.substr(start, end - start) : "";
    }
    auto start = attrPos + attr.length() + 2;
    auto end = svg.find("\"", start);
    return (end != std::string::npos) ? svg.substr(start, end - start) : "";
}

// Extract all path d attributes (supports multiple <path> elements)
struct PathInfo {
    std::string d;
    float opacity = 1.0f;
    float strokeWidth = 0.0f; // >0 means stroke (fill="none")
    D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Identity();
};

/* 解析 SVG transform 属性串：matrix(a,b,c,d,e,f) / translate(x[,y]) / scale(s[,sy]) /
 * rotate(deg[,cx,cy])。多个变换连写按 SVG 规则左乘累积（先列出的最后应用）。
 * 行向量语义：transformed = original * result。 */
static D2D1_MATRIX_3X2_F ParseSvgTransform(const std::string& s) {
    D2D1_MATRIX_3X2_F result = D2D1::Matrix3x2F::Identity();
    const char* p = s.c_str();
    auto skipSep = [&]() {
        while (*p && (*p==' '||*p==','||*p=='\t'||*p=='\n'||*p=='\r')) ++p;
    };
    while (*p) {
        skipSep();
        if (!*p) break;
        const char* nameStart = p;
        while (*p && (((*p|0x20) >= 'a' && (*p|0x20) <= 'z'))) ++p;
        std::string name(nameStart, p - nameStart);
        skipSep();
        if (*p != '(') break;
        ++p;
        float args[6] = {0,0,0,0,0,0};
        int n = 0;
        while (*p && *p != ')' && n < 6) {
            skipSep();
            if (*p == ')') break;
            char* end = nullptr;
            args[n] = strtof(p, &end);
            if (end == p) break;
            p = end;
            ++n;
        }
        if (*p == ')') ++p;
        D2D1_MATRIX_3X2_F m = D2D1::Matrix3x2F::Identity();
        if (name == "matrix" && n == 6) {
            m = D2D1::Matrix3x2F(args[0], args[1], args[2], args[3], args[4], args[5]);
        } else if (name == "translate") {
            m = D2D1::Matrix3x2F::Translation(args[0], n >= 2 ? args[1] : 0.0f);
        } else if (name == "scale") {
            m = D2D1::Matrix3x2F::Scale(args[0], n >= 2 ? args[1] : args[0]);
        } else if (name == "rotate") {
            if (n >= 3)
                m = D2D1::Matrix3x2F::Rotation(args[0], D2D1::Point2F(args[1], args[2]));
            else
                m = D2D1::Matrix3x2F::Rotation(args[0]);
        } else {
            continue;
        }
        result = m * result;
    }
    return result;
}

static bool IsIdentity(const D2D1_MATRIX_3X2_F& m) {
    return m._11 == 1.0f && m._12 == 0.0f &&
           m._21 == 0.0f && m._22 == 1.0f &&
           m._31 == 0.0f && m._32 == 0.0f;
}

static std::string extractAttrFromTag(const std::string& tag, const char* attr) {
    std::string needle1 = std::string(" ") + attr + "=\"";
    std::string needle2 = std::string(" ") + attr + "='";
    auto p = tag.find(needle1);
    if (p != std::string::npos) {
        auto s = p + needle1.size();
        auto e = tag.find("\"", s);
        if (e != std::string::npos) return tag.substr(s, e - s);
    }
    p = tag.find(needle2);
    if (p != std::string::npos) {
        auto s = p + needle2.size();
        auto e = tag.find("'", s);
        if (e != std::string::npos) return tag.substr(s, e - s);
    }
    return "";
}

static std::vector<PathInfo> ExtractPaths(const std::string& svg) {
    std::vector<PathInfo> paths;

    /* 线性扫标签，维护一个 <g> 嵌套栈，累积 transform / opacity / stroke-width */
    struct GroupCtx {
        D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Identity();
        float opacity = 1.0f;
        float strokeWidth = 0.0f;
    };
    std::vector<GroupCtx> stack;
    GroupCtx current{};

    size_t pos = 0;
    while (pos < svg.size()) {
        size_t lt = svg.find('<', pos);
        if (lt == std::string::npos) break;

        /* </g> 出栈 */
        if (svg.compare(lt, 4, "</g>") == 0) {
            if (!stack.empty()) { current = stack.back(); stack.pop_back(); }
            pos = lt + 4;
            continue;
        }

        /* <g ...> 入栈，应用 group 的 transform / opacity / stroke-width */
        if (svg.compare(lt, 3, "<g ") == 0 || svg.compare(lt, 3, "<g>") == 0 ||
            svg.compare(lt, 3, "<g\t") == 0 || svg.compare(lt, 3, "<g\n") == 0) {
            auto tagEnd = svg.find('>', lt);
            if (tagEnd == std::string::npos) break;
            std::string tag = svg.substr(lt, tagEnd - lt + 1);
            stack.push_back(current);

            auto tStr = extractAttrFromTag(tag, "transform");
            if (!tStr.empty()) {
                auto m = ParseSvgTransform(tStr);
                current.transform = m * current.transform;
            }
            auto opStr = extractAttrFromTag(tag, "opacity");
            if (!opStr.empty()) current.opacity *= (float)atof(opStr.c_str());

            auto swAttr = extractAttrFromTag(tag, "stroke-width");
            auto fillAttr = extractAttrFromTag(tag, "fill");
            auto strokeAttr = extractAttrFromTag(tag, "stroke");
            if (!swAttr.empty() && (fillAttr == "none" || strokeAttr == "currentColor"))
                current.strokeWidth = (float)atof(swAttr.c_str());

            /* 自闭合 <g .../> 立即出栈 */
            if (tagEnd > 0 && svg[tagEnd - 1] == '/') {
                if (!stack.empty()) { current = stack.back(); stack.pop_back(); }
            }
            pos = tagEnd + 1;
            continue;
        }

        /* <path / <circle 提取 */
        bool isPath = svg.compare(lt, 5, "<path") == 0;
        bool isCircle = svg.compare(lt, 7, "<circle") == 0;
        if (!isPath && !isCircle) {
            pos = lt + 1;
            continue;
        }

        auto tagEnd = svg.find('>', lt);
        if (tagEnd == std::string::npos) break;
        std::string tag = svg.substr(lt, tagEnd - lt + 1);

        PathInfo pi;
        pi.opacity = current.opacity;
        pi.transform = current.transform;

        if (isCircle) {
            auto cxs = extractAttrFromTag(tag, "cx");
            auto cys = extractAttrFromTag(tag, "cy");
            auto rs  = extractAttrFromTag(tag, "r");
            if (!cxs.empty() && !cys.empty() && !rs.empty()) {
                float cx = (float)atof(cxs.c_str());
                float cy = (float)atof(cys.c_str());
                float r  = (float)atof(rs.c_str());
                char buf[256];
                snprintf(buf, sizeof(buf),
                    "M%.3f,%.3f A%.3f,%.3f 0 1,0 %.3f,%.3f A%.3f,%.3f 0 1,0 %.3f,%.3f Z",
                    cx - r, cy, r, r, cx + r, cy, r, r, cx - r, cy);
                pi.d = buf;
            }
        } else {
            pi.d = extractAttrFromTag(tag, "d");
        }

        auto opStr = extractAttrFromTag(tag, "opacity");
        if (!opStr.empty()) pi.opacity *= (float)atof(opStr.c_str());

        auto tStr = extractAttrFromTag(tag, "transform");
        if (!tStr.empty()) {
            auto m = ParseSvgTransform(tStr);
            pi.transform = m * pi.transform;
        }

        auto fillAttr = extractAttrFromTag(tag, "fill");
        auto strokeAttr = extractAttrFromTag(tag, "stroke");
        auto swAttr = extractAttrFromTag(tag, "stroke-width");
        float sw = !swAttr.empty() ? (float)atof(swAttr.c_str()) : current.strokeWidth;
        bool hasFill = !fillAttr.empty() && fillAttr != "none";
        bool hasStroke = !strokeAttr.empty() && strokeAttr != "none";
        /* 走 stroke：路径有 stroke-width(>0) 且没有显式 fill（或 fill=none）*/
        if (sw > 0 && !hasFill && (hasStroke || current.strokeWidth > 0 ||
                                   fillAttr == "none")) {
            pi.strokeWidth = sw;
        }

        if (!pi.d.empty()) paths.push_back(std::move(pi));
        pos = tagEnd + 1;
    }
    return paths;
}

// ===================== L75: SVG 文字 (<text> / <foreignObject>) =====================

// UTF-8 → wstring (SVG/HTML 文字是 UTF-8).
static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring ws((size_t)len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &ws[0], len);
    return ws;
}

// 从 CSS style 串取某属性值 ("…;font-size:16px;…" → "16px"). prop 用小写.
static std::string CssProp(const std::string& style, const char* prop) {
    std::string needle = prop;
    size_t p = 0;
    while ((p = style.find(needle, p)) != std::string::npos) {
        bool leftOk = (p == 0 || style[p-1]==';' || style[p-1]==' ' ||
                       style[p-1]=='\t' || style[p-1]=='\n' || style[p-1]=='{');
        size_t c = p + needle.size();
        while (c < style.size() && (style[c]==' '||style[c]=='\t')) ++c;
        if (leftOk && c < style.size() && style[c]==':') {
            size_t s = c + 1;
            size_t e = style.find(';', s);
            std::string v = style.substr(s, e==std::string::npos ? std::string::npos : e - s);
            size_t a = v.find_first_not_of(" \t\n\r");
            if (a == std::string::npos) return "";
            size_t b = v.find_last_not_of(" \t\n\r");
            return v.substr(a, b - a + 1);
        }
        p = c;
    }
    return "";
}

// 解 SVG/CSS 颜色 rgb()/rgba()/#rgb/#rrggbb/基本命名 → D2D1_COLOR_F. false = none/无效.
static bool ParseSvgColor(const std::string& in, D2D1_COLOR_F& out) {
    size_t a = in.find_first_not_of(" \t\n\r");
    if (a == std::string::npos) return false;
    size_t b = in.find_last_not_of(" \t\n\r");
    std::string c = in.substr(a, b - a + 1);
    if (c.empty() || c == "none" || c == "transparent" || c == "currentColor") return false;
    std::string lc = c; for (auto& ch : lc) ch = (char)tolower((unsigned char)ch);
    if (c[0] == '#') {
        unsigned r=0,g=0,bl=0;
        if (c.size() >= 7) { if (sscanf(c.c_str()+1, "%2x%2x%2x", &r,&g,&bl)!=3) return false; }
        else if (c.size() >= 4) { unsigned R=0,G=0,B=0;
            if (sscanf(c.c_str()+1,"%1x%1x%1x",&R,&G,&B)!=3) return false;
            r=R*17; g=G*17; bl=B*17; }
        else return false;
        out = D2D1::ColorF(r/255.0f, g/255.0f, bl/255.0f, 1.0f); return true;
    }
    if (lc.compare(0,4,"rgb(")==0 || lc.compare(0,5,"rgba(")==0) {
        int r=0,g=0,bl=0; float al=1.0f;
        const char* p = c.c_str() + c.find('(') + 1;
        int got = sscanf(p, " %d , %d , %d , %f", &r,&g,&bl,&al);
        if (got < 3) return false;
        out = D2D1::ColorF(r/255.0f, g/255.0f, bl/255.0f, got>=4 ? al : 1.0f); return true;
    }
    if (lc=="black") { out=D2D1::ColorF(0,0,0,1); return true; }
    if (lc=="white") { out=D2D1::ColorF(1,1,1,1); return true; }
    if (lc=="red")   { out=D2D1::ColorF(1,0,0,1); return true; }
    if (lc=="green") { out=D2D1::ColorF(0,0.5f,0,1); return true; }
    if (lc=="blue")  { out=D2D1::ColorF(0,0,1,1); return true; }
    if (lc=="gray"||lc=="grey") { out=D2D1::ColorF(0.5f,0.5f,0.5f,1); return true; }
    return false;
}

// HTML 实体解码 (常见几个 + 数字实体), 追加到 utf8 串.
static void AppendDecoded(std::string& out, const std::string& src) {
    for (size_t i = 0; i < src.size(); ) {
        if (src[i] == '&') {
            size_t semi = src.find(';', i);
            if (semi != std::string::npos && semi - i <= 10) {
                std::string ent = src.substr(i+1, semi - i - 1);
                if (ent == "amp") out += '&';
                else if (ent == "lt") out += '<';
                else if (ent == "gt") out += '>';
                else if (ent == "quot") out += '"';
                else if (ent == "apos") out += '\'';
                else if (ent == "nbsp") out += ' ';
                else if (!ent.empty() && ent[0]=='#') {
                    int code = (ent.size()>1 && (ent[1]=='x'||ent[1]=='X'))
                             ? (int)strtol(ent.c_str()+2, nullptr, 16)
                             : atoi(ent.c_str()+1);
                    if (code > 0 && code < 0x110000) {
                        if (code < 0x80) out += (char)code;
                        else if (code < 0x800) { out += (char)(0xC0|(code>>6)); out += (char)(0x80|(code&0x3F)); }
                        else { out += (char)(0xE0|(code>>12)); out += (char)(0x80|((code>>6)&0x3F)); out += (char)(0x80|(code&0x3F)); }
                    }
                } else { out += '&'; out += ent; out += ';'; }  // 未知实体 → 原样
                i = semi + 1; continue;
            }
        }
        out += src[i++];
    }
}

// 抽 [start,end) 内纯文字: 剥 <...> 标签 (<br>/</p>/</div> → 换行), 解实体,
// 逐行合并空白 + trim + 去空行, → wstring.
static std::wstring ExtractInnerText(const std::string& svg, size_t start, size_t end) {
    if (end > svg.size()) end = svg.size();
    std::string raw;
    size_t i = start;
    while (i < end) {
        if (svg[i] == '<') {
            size_t te = svg.find('>', i);
            if (te == std::string::npos || te >= end) break;
            if (svg.compare(i, 3, "<br") == 0) raw += '\n';
            else if (svg.compare(i, 4, "</p>") == 0 || svg.compare(i, 6, "</div>") == 0) raw += '\n';
            i = te + 1;
        } else {
            size_t lt = svg.find('<', i);
            if (lt == std::string::npos || lt > end) lt = end;
            AppendDecoded(raw, svg.substr(i, lt - i));
            i = lt;
        }
    }
    // 逐行: 折叠内部空白 + trim, 丢空行, '\n' 连接
    std::string norm;
    std::string line;
    auto flush = [&](){
        std::string t; bool sp=false, started=false;
        for (char ch : line) {
            if (ch==' '||ch=='\t'||ch=='\r') { if (started) sp = true; }
            else { if (sp) { t+=' '; sp=false; } t += ch; started = true; }
        }
        if (!t.empty()) { if (!norm.empty()) norm += '\n'; norm += t; }
        line.clear();
    };
    for (char ch : raw) { if (ch=='\n') flush(); else line += ch; }
    flush();
    return Utf8ToWide(norm);
}

// 通用名 → 主题默认字体 (DirectWrite 不认 CSS generic). 取字体列表首项, 剥引号.
static std::wstring ResolveFontFamily(const std::string& css) {
    std::string f = css;
    size_t comma = f.find(',');
    if (comma != std::string::npos) f = f.substr(0, comma);
    // 剥引号 + trim
    std::string t;
    for (char ch : f) if (ch!='"' && ch!='\'') t += ch;
    size_t a = t.find_first_not_of(" \t"); if (a==std::string::npos) return L"";
    size_t b = t.find_last_not_of(" \t");
    t = t.substr(a, b - a + 1);
    std::string lc = t; for (auto& ch : lc) ch = (char)tolower((unsigned char)ch);
    if (lc.empty() || lc=="sans-serif" || lc=="serif" || lc=="system-ui" ||
        lc=="ui-sans-serif" || lc=="inherit") return L"";   // → 主题默认
    return Utf8ToWide(t);
}

static std::vector<std::wstring> ResolveFontFamilyList(const std::string& css) {
    std::vector<std::wstring> out;
    size_t pos = 0;
    while (pos < css.size()) {
        size_t start = pos;
        char quote = 0;
        while (pos < css.size()) {
            char c = css[pos];
            if ((c == '"' || c == '\'') && (pos == start || css[pos - 1] != '\\')) {
                quote = quote == c ? 0 : (quote ? quote : c);
            } else if (c == ',' && !quote) {
                break;
            }
            ++pos;
        }
        std::wstring fam = ResolveFontFamily(css.substr(start, pos - start));
        if (!fam.empty()) out.push_back(std::move(fam));
        if (pos < css.size() && css[pos] == ',') ++pos;
    }
    return out;
}

// L87: font-weight 解析. "bold"/"normal"/"bolder"/"lighter" + 数字 100..900.
static DWRITE_FONT_WEIGHT ParseFontWeight(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\n\r");
    if (a == std::string::npos) return DWRITE_FONT_WEIGHT_NORMAL;
    size_t b = s.find_last_not_of(" \t\n\r");
    std::string lc = s.substr(a, b - a + 1);
    for (auto& ch : lc) ch = (char)tolower((unsigned char)ch);
    if (lc == "normal")  return DWRITE_FONT_WEIGHT_NORMAL;
    if (lc == "bold")    return DWRITE_FONT_WEIGHT_BOLD;
    if (lc == "bolder")  return DWRITE_FONT_WEIGHT_BOLD;
    if (lc == "lighter") return DWRITE_FONT_WEIGHT_LIGHT;
    int n = atoi(lc.c_str());
    if (n < 1) return DWRITE_FONT_WEIGHT_NORMAL;
    if (n > 999) n = 999;
    return (DWRITE_FONT_WEIGHT)n;
}

// L87: 解析 SVG 所有 <linearGradient>/<radialGradient> → id→渐变. 含 href/xlink:href
// stop + 几何属性继承 (resolve pass). vbW/vbH = viewBox 尺寸 (userSpaceOnUse 的 % 用).
static std::map<std::string, SvgTextGradient>
ParseGradients(const std::string& svg, float vbW, float vbH) {
    struct Raw {
        SvgTextGradient g;
        std::string href;
        bool hX1=false,hY1=false,hX2=false,hY2=false,hCx=false,hCy=false,hR=false;
        bool hUnits=false, hXf=false, hStops=false;
    };
    std::map<std::string, Raw> raw;

    auto coord = [&](const std::string& s, bool isX, bool userSpace) -> float {
        bool pct = s.find('%') != std::string::npos;
        float v = (float)atof(s.c_str());
        if (userSpace) return pct ? v / 100.0f * (isX ? vbW : vbH) : v;
        return pct ? v / 100.0f : v;   // objectBoundingBox: 无单位即 0..1 分数
    };

    size_t pos = 0;
    while (pos < svg.size()) {
        size_t lp = svg.find("<linearGradient", pos);
        size_t rp = svg.find("<radialGradient", pos);
        size_t lt = (lp < rp) ? lp : rp;
        if (lt == std::string::npos) break;
        bool radial = (lt == rp);
        size_t tagEnd = svg.find('>', lt);
        if (tagEnd == std::string::npos) break;
        std::string tag = svg.substr(lt, tagEnd - lt + 1);
        bool selfClose = (tagEnd > 0 && svg[tagEnd-1] == '/');

        std::string id = extractAttrFromTag(tag, "id");
        if (id.empty()) { pos = tagEnd + 1; continue; }

        Raw r;
        r.g.radial = radial;
        std::string units = extractAttrFromTag(tag, "gradientUnits");
        if (!units.empty()) { r.hUnits = true; r.g.userSpace = (units == "userSpaceOnUse"); }
        std::string xf = extractAttrFromTag(tag, "gradientTransform");
        if (!xf.empty()) { r.hXf = true; r.g.transform = ParseSvgTransform(xf); }
        r.href = extractAttrFromTag(tag, "href");
        if (r.href.empty()) r.href = extractAttrFromTag(tag, "xlink:href");

        bool us = r.g.userSpace;
        if (!radial) {
            std::string s;
            // 默认: objectBB 0,0→1,0; userSpace 0,0→100%,0
            r.g.x1 = 0; r.g.y1 = 0; r.g.x2 = us ? vbW : 1.0f; r.g.y2 = 0;
            if (!(s = extractAttrFromTag(tag, "x1")).empty()) { r.hX1=true; r.g.x1 = coord(s,true,us); }
            if (!(s = extractAttrFromTag(tag, "y1")).empty()) { r.hY1=true; r.g.y1 = coord(s,false,us); }
            if (!(s = extractAttrFromTag(tag, "x2")).empty()) { r.hX2=true; r.g.x2 = coord(s,true,us); }
            if (!(s = extractAttrFromTag(tag, "y2")).empty()) { r.hY2=true; r.g.y2 = coord(s,false,us); }
        } else {
            std::string s;
            r.g.cx = us ? vbW*0.5f : 0.5f; r.g.cy = us ? vbH*0.5f : 0.5f;
            r.g.r  = us ? 0.5f*((vbW+vbH)*0.5f) : 0.5f;
            if (!(s = extractAttrFromTag(tag, "cx")).empty()) { r.hCx=true; r.g.cx = coord(s,true,us); }
            if (!(s = extractAttrFromTag(tag, "cy")).empty()) { r.hCy=true; r.g.cy = coord(s,false,us); }
            if (!(s = extractAttrFromTag(tag, "r")).empty())  { r.hR=true;  r.g.r  = coord(s,true,us); }
        }

        // 子 <stop>
        if (!selfClose) {
            const char* closeTag = radial ? "</radialGradient>" : "</linearGradient>";
            size_t close = svg.find(closeTag, tagEnd);
            size_t scanEnd = (close == std::string::npos) ? svg.size() : close;
            size_t sp = tagEnd + 1;
            while (sp < scanEnd) {
                size_t st = svg.find("<stop", sp);
                if (st == std::string::npos || st >= scanEnd) break;
                size_t se = svg.find('>', st);
                if (se == std::string::npos || se > scanEnd) break;
                std::string stag = svg.substr(st, se - st + 1);
                std::string style = extractAttrFromTag(stag, "style");
                SvgGradientStop gs;
                std::string off = extractAttrFromTag(stag, "offset");
                gs.offset = (float)atof(off.c_str());
                if (off.find('%') != std::string::npos) gs.offset /= 100.0f;
                if (gs.offset < 0) gs.offset = 0; if (gs.offset > 1) gs.offset = 1;
                std::string sc = extractAttrFromTag(stag, "stop-color");
                if (sc.empty()) sc = CssProp(style, "stop-color");
                D2D1_COLOR_F col = D2D1::ColorF(0,0,0,1);
                ParseSvgColor(sc, col);
                std::string so = extractAttrFromTag(stag, "stop-opacity");
                if (so.empty()) so = CssProp(style, "stop-opacity");
                if (!so.empty()) { float a=(float)atof(so.c_str()); col.a *= (a<0?0:(a>1?1:a)); }
                gs.color = col;
                r.g.stops.push_back(gs);
                sp = se + 1;
            }
        }
        r.hStops = !r.g.stops.empty();
        raw[id] = std::move(r);
        pos = tagEnd + 1;
    }

    // href 继承: 自身没设的 stops / 几何 / units / transform 从被引用者拷.
    // 迭代不动点 (短链), 父未 resolve 完(href 非空)就等下一轮; 环则停留空 stops.
    for (size_t iter = 0; iter <= raw.size(); ++iter) {
        bool changed = false;
        for (auto& kv : raw) {
            Raw& r = kv.second;
            if (r.href.empty()) continue;
            std::string pid = r.href;
            if (!pid.empty() && pid[0] == '#') pid = pid.substr(1);
            auto it = raw.find(pid);
            if (it == raw.end()) { r.href.clear(); changed = true; continue; }
            Raw& par = it->second;
            if (!par.href.empty()) continue;  // 父还没 resolve 完, 等
            if (!r.hStops && !par.g.stops.empty()) { r.g.stops = par.g.stops; r.hStops = true; }
            if (!r.hUnits) r.g.userSpace = par.g.userSpace;
            if (!r.hXf)    r.g.transform = par.g.transform;
            if (!r.hX1) r.g.x1 = par.g.x1;
            if (!r.hY1) r.g.y1 = par.g.y1;
            if (!r.hX2) r.g.x2 = par.g.x2;
            if (!r.hY2) r.g.y2 = par.g.y2;
            if (!r.hCx) r.g.cx = par.g.cx;
            if (!r.hCy) r.g.cy = par.g.cy;
            if (!r.hR)  r.g.r  = par.g.r;
            r.href.clear();
            changed = true;
        }
        if (!changed) break;
    }

    std::map<std::string, SvgTextGradient> out;
    for (auto& kv : raw) out[kv.first] = std::move(kv.second.g);
    return out;
}

// 扫 <text> / <foreignObject>, 维护 <g> 继承栈 (transform + opacity + fill + font-*,
// 同 ExtractPaths 但带文字属性级联), 产出文字 run. L87: 字重/渐变/继承/透明.
static std::vector<SvgTextRun> ExtractTextRuns(const std::string& svg) {
    std::vector<SvgTextRun> runs;

    // viewBox 尺寸 (userSpaceOnUse 渐变的 % 用), 缺则用 width/height, 再缺给 0.
    float vbW = 0, vbH = 0;
    {
        std::string vb = ExtractAttr(svg, "svg", "viewBox");
        float vx, vy;
        if (vb.empty() || sscanf(vb.c_str(), "%f %f %f %f", &vx, &vy, &vbW, &vbH) != 4) {
            vbW = (float)atof(ExtractAttr(svg, "svg", "width").c_str());
            vbH = (float)atof(ExtractAttr(svg, "svg", "height").c_str());
        }
    }
    std::map<std::string, SvgTextGradient> grads = ParseGradients(svg, vbW, vbH);

    // <g> 继承状态: 文字属性沿 g 链级联 ("" / 0 / *Set=false 表示未设).
    struct GState {
        D2D1_MATRIX_3X2_F  xf = D2D1::Matrix3x2F::Identity();
        float              opacity = 1.0f;
        std::string        fill;                 // 继承 fill (含 url()), "" = 未设
        bool               fillSet = false;
        float              fontSize = 0.0f;       // 0 = 未设
        std::wstring       fontFamily;
        bool               fontFamilySet = false;
        DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
        bool               weightSet = false;
    };
    std::vector<GState> stack;
    GState cur;

    // 把一个 <g>/<text> 标签的文字呈现属性灌进 st (own=true 时 fill 也认 color/fill).
    auto applyTextAttrs = [&](GState& st, const std::string& tag, const std::string& style) {
        std::string s;
        if (!(s = extractAttrFromTag(tag, "opacity")).empty() || !(s = CssProp(style,"opacity")).empty()) {
            float v = (float)atof(s.c_str()); st.opacity *= (v<0?0:(v>1?1:v));
        }
        if (!(s = extractAttrFromTag(tag, "fill-opacity")).empty() || !(s = CssProp(style,"fill-opacity")).empty()) {
            float v = (float)atof(s.c_str()); st.opacity *= (v<0?0:(v>1?1:v));
        }
        if ((s = extractAttrFromTag(tag, "fill")).empty()) s = CssProp(style, "fill");
        if (s.empty()) s = CssProp(style, "color");
        if (!s.empty()) { st.fill = s; st.fillSet = true; }
        if ((s = extractAttrFromTag(tag, "font-size")).empty()) s = CssProp(style, "font-size");
        if (!s.empty()) { float v=(float)atof(s.c_str()); if (v>0) st.fontSize = v; }
        if ((s = extractAttrFromTag(tag, "font-family")).empty()) s = CssProp(style, "font-family");
        if (!s.empty()) { st.fontFamily = ResolveFontFamily(s); st.fontFamilySet = true; }
        if ((s = extractAttrFromTag(tag, "font-weight")).empty()) s = CssProp(style, "font-weight");
        if (!s.empty()) { st.weight = ParseFontWeight(s); st.weightSet = true; }
    };

    size_t pos = 0;
    while (pos < svg.size()) {
        size_t lt = svg.find('<', pos);
        if (lt == std::string::npos) break;

        if (svg.compare(lt, 4, "</g>") == 0) {
            if (!stack.empty()) { cur = stack.back(); stack.pop_back(); }
            pos = lt + 4; continue;
        }
        if (svg.compare(lt,3,"<g ")==0 || svg.compare(lt,3,"<g>")==0 ||
            svg.compare(lt,3,"<g\t")==0 || svg.compare(lt,3,"<g\n")==0) {
            size_t tagEnd = svg.find('>', lt);
            if (tagEnd == std::string::npos) break;
            std::string tag = svg.substr(lt, tagEnd - lt + 1);
            stack.push_back(cur);
            auto tStr = extractAttrFromTag(tag, "transform");
            if (!tStr.empty()) cur.xf = ParseSvgTransform(tStr) * cur.xf;
            std::string gStyle = extractAttrFromTag(tag, "style");
            applyTextAttrs(cur, tag, gStyle);
            if (tagEnd > 0 && svg[tagEnd-1]=='/') {
                if (!stack.empty()) { cur = stack.back(); stack.pop_back(); }
            }
            pos = tagEnd + 1; continue;
        }

        bool isText = svg.compare(lt,5,"<text")==0 &&
                      (svg[lt+5]==' '||svg[lt+5]=='>'||svg[lt+5]=='\t'||svg[lt+5]=='\n');
        bool isFO   = svg.compare(lt,14,"<foreignObject")==0;
        if (!isText && !isFO) { pos = lt + 1; continue; }

        size_t tagEnd = svg.find('>', lt);
        if (tagEnd == std::string::npos) break;
        std::string tag = svg.substr(lt, tagEnd - lt + 1);
        bool selfClose = (tagEnd > 0 && svg[tagEnd-1] == '/');
        const char* closeTag = isText ? "</text>" : "</foreignObject>";
        size_t closeLen = isText ? 7 : 16;
        size_t close = selfClose ? tagEnd : svg.find(closeTag, tagEnd);
        size_t contentEnd = (close==std::string::npos) ? svg.size() : close;

        SvgTextRun run;
        // 先继承 <g> 链上的呈现属性, 再用 <text> 自身覆盖.
        GState ts = cur;
        std::string style = extractAttrFromTag(tag, "style");
        applyTextAttrs(ts, tag, style);

        run.transform = cur.xf;
        auto self = extractAttrFromTag(tag, "transform");
        if (!self.empty()) run.transform = ParseSvgTransform(self) * run.transform;
        run.x = (float)atof(extractAttrFromTag(tag, "x").c_str());
        run.y = (float)atof(extractAttrFromTag(tag, "y").c_str());

        if (ts.fontSize > 0) run.fontSize = ts.fontSize;
        if (ts.fontFamilySet) run.fontFamily = ts.fontFamily;
        if (ts.weightSet) run.fontWeight = ts.weight;
        run.opacity = ts.opacity;

        // fill: url(#id) → 渐变; 否则纯色; 都没有 → 默认黑.
        if (ts.fillSet && !ts.fill.empty()) {
            const std::string& fill = ts.fill;
            if (fill.compare(0, 4, "url(") == 0) {
                size_t h = fill.find('#');
                size_t e = fill.find(')', h);
                if (h != std::string::npos && e != std::string::npos) {
                    std::string id = fill.substr(h + 1, e - h - 1);
                    auto it = grads.find(id);
                    if (it != grads.end() && !it->second.stops.empty()) {
                        run.gradient = it->second;
                        run.hasGradient = true;
                    }
                }
            } else {
                D2D1_COLOR_F col;
                if (ParseSvgColor(fill, col)) run.color = col;
            }
        }

        if (isFO) {
            run.block = true;
            run.anchor = 1;
            /* foreignObject 的 (x,y) 是框左上角 (常为 0, 真实位置在父 <g transform>),
             * 文字要在框内居中 → 锚点设框中心 (x+width/2, y+height/2). 累积 transform
             * 后即落在 label/node 中心. */
            float w = (float)atof(extractAttrFromTag(tag, "width").c_str());
            float h = (float)atof(extractAttrFromTag(tag, "height").c_str());
            run.x += w / 2.0f;
            run.y += h / 2.0f;
            if (w > 1.0f) run.maxWidth = w;
        } else {
            std::string anchor = extractAttrFromTag(tag, "text-anchor");
            if (anchor.empty()) anchor = CssProp(style, "text-anchor");
            if (anchor == "middle") run.anchor = 1;
            else if (anchor == "end") run.anchor = 2;
        }

        if (!selfClose) run.text = ExtractInnerText(svg, tagEnd + 1, contentEnd);
        if (!run.text.empty()) runs.push_back(std::move(run));
        pos = (close==std::string::npos) ? svg.size() : close + closeLen;
    }
    return runs;
}

static bool BuildGeometry(ID2D1Factory* factory, const std::vector<std::string>& pathDatas,
                           ID2D1PathGeometry** outGeometry) {
    ComPtr<ID2D1PathGeometry> geom;
    HRESULT hr = factory->CreatePathGeometry(geom.GetAddressOf());
    if (FAILED(hr)) return false;

    ComPtr<ID2D1GeometrySink> sink;
    hr = geom->Open(sink.GetAddressOf());
    if (FAILED(hr)) return false;

    sink->SetFillMode(D2D1_FILL_MODE_WINDING);

    for (auto& pathData : pathDatas) {
        const char* p = pathData.c_str();
        float cx = 0, cy = 0;       // current point
        float sx = 0, sy = 0;       // subpath start
        float lcx = 0, lcy = 0;     // last control point (for S/T)
        char lastCmd = 0;
        bool figureOpen = false;

        auto EnsureFigure = [&]() {
            if (!figureOpen) {
                sink->BeginFigure({cx, cy}, D2D1_FIGURE_BEGIN_FILLED);
                figureOpen = true;
            }
        };

        while (*p) {
            SkipWS(p);
            if (!*p) break;

            char cmd = 0;
            if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
                cmd = *p++;
            } else {
                cmd = lastCmd;
            }
            bool rel = (cmd >= 'a' && cmd <= 'z');
            char CMD = rel ? (cmd - 32) : cmd;

            switch (CMD) {
            case 'M': {
                float x = ParseFloat(p), y = ParseFloat(p);
                if (rel) { x += cx; y += cy; }
                if (figureOpen) { sink->EndFigure(D2D1_FIGURE_END_OPEN); figureOpen = false; }
                cx = sx = x; cy = sy = y;
                EnsureFigure();
                // Subsequent coordinates after M are treated as L
                lastCmd = rel ? 'l' : 'L';
                continue;
            }
            case 'L': {
                float x = ParseFloat(p), y = ParseFloat(p);
                if (rel) { x += cx; y += cy; }
                EnsureFigure();
                sink->AddLine({x, y});
                cx = x; cy = y;
                break;
            }
            case 'H': {
                float x = ParseFloat(p);
                if (rel) x += cx;
                EnsureFigure();
                sink->AddLine({x, cy});
                cx = x;
                break;
            }
            case 'V': {
                float y = ParseFloat(p);
                if (rel) y += cy;
                EnsureFigure();
                sink->AddLine({cx, y});
                cy = y;
                break;
            }
            case 'C': {
                float x1 = ParseFloat(p), y1 = ParseFloat(p);
                float x2 = ParseFloat(p), y2 = ParseFloat(p);
                float x = ParseFloat(p), y = ParseFloat(p);
                if (rel) { x1+=cx; y1+=cy; x2+=cx; y2+=cy; x+=cx; y+=cy; }
                EnsureFigure();
                sink->AddBezier({{x1,y1},{x2,y2},{x,y}});
                lcx = x2; lcy = y2;
                cx = x; cy = y;
                break;
            }
            case 'S': {
                float x2 = ParseFloat(p), y2 = ParseFloat(p);
                float x = ParseFloat(p), y = ParseFloat(p);
                if (rel) { x2+=cx; y2+=cy; x+=cx; y+=cy; }
                float x1 = 2*cx - lcx, y1 = 2*cy - lcy;
                EnsureFigure();
                sink->AddBezier({{x1,y1},{x2,y2},{x,y}});
                lcx = x2; lcy = y2;
                cx = x; cy = y;
                break;
            }
            case 'Q': {
                float x1 = ParseFloat(p), y1 = ParseFloat(p);
                float x = ParseFloat(p), y = ParseFloat(p);
                if (rel) { x1+=cx; y1+=cy; x+=cx; y+=cy; }
                EnsureFigure();
                sink->AddQuadraticBezier({{x1,y1},{x,y}});
                lcx = x1; lcy = y1;
                cx = x; cy = y;
                break;
            }
            case 'T': {
                float x = ParseFloat(p), y = ParseFloat(p);
                if (rel) { x+=cx; y+=cy; }
                float x1 = 2*cx - lcx, y1 = 2*cy - lcy;
                EnsureFigure();
                sink->AddQuadraticBezier({{x1,y1},{x,y}});
                lcx = x1; lcy = y1;
                cx = x; cy = y;
                break;
            }
            case 'A': {
                float rx = ParseFloat(p), ry = ParseFloat(p);
                float angle = ParseFloat(p);
                bool largeArc = ParseFlag(p);
                bool sweep = ParseFlag(p);
                float x = ParseFloat(p), y = ParseFloat(p);
                if (rel) { x+=cx; y+=cy; }
                EnsureFigure();
                sink->AddArc({
                    {x, y}, {rx, ry}, angle,
                    sweep ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                    largeArc ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL
                });
                cx = x; cy = y;
                break;
            }
            case 'Z': {
                if (figureOpen) {
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    figureOpen = false;
                }
                cx = sx; cy = sy;
                break;
            }
            default:
                ++p; // skip unknown
                continue;
            }
            lastCmd = cmd;
        }
        if (figureOpen) sink->EndFigure(D2D1_FIGURE_END_OPEN);
    }

    hr = sink->Close();
    if (FAILED(hr)) return false;

    *outGeometry = geom.Detach();
    return true;
}

} // namespace svg_detail

SvgIcon Renderer::ParseSvgIcon(const std::string& svgContent) {
    SvgIcon icon;
    if (!factory_ || svgContent.empty()) return icon;

    // Extract viewBox
    auto vb = svg_detail::ExtractAttr(svgContent, "svg", "viewBox");
    if (!vb.empty()) {
        float vx, vy, vw, vh;
        if (sscanf(vb.c_str(), "%f %f %f %f", &vx, &vy, &vw, &vh) == 4) {
            icon.viewBoxW = vw;
            icon.viewBoxH = vh;
        }
    } else {
        // Try width/height
        auto w = svg_detail::ExtractAttr(svgContent, "svg", "width");
        auto h = svg_detail::ExtractAttr(svgContent, "svg", "height");
        if (!w.empty()) icon.viewBoxW = (float)atof(w.c_str());
        if (!h.empty()) icon.viewBoxH = (float)atof(h.c_str());
    }

    // L75: 文字 run (<text> / <foreignObject>) —— 跟 path 独立提取, 即使没有
    // path 也要 (纯文字 / 只有 rect 的图表). 给 fallback 路径用; 原生 D2D 路径
    // 走 ParseSvgTextRuns 单独拿.
    icon.textRuns = svg_detail::ExtractTextRuns(svgContent);

    // Extract all paths with opacity
    auto paths = svg_detail::ExtractPaths(svgContent);
    if (paths.empty()) {
        icon.valid = !icon.textRuns.empty();   // 纯文字 SVG 也算有效
        return icon;
    }

    // Check if any path has non-default opacity, stroke or transform
    bool needLayers = false;
    for (auto& pi : paths) {
        if (pi.opacity < 0.99f || pi.strokeWidth > 0 ||
            !svg_detail::IsIdentity(pi.transform)) { needLayers = true; break; }
    }

    if (needLayers) {
        // Build per-path layers for opacity / stroke / transform support
        for (auto& pi : paths) {
            std::vector<std::string> single = {pi.d};
            ID2D1PathGeometry* geom = nullptr;
            if (svg_detail::BuildGeometry(factory_, single, &geom)) {
                SvgPathLayer layer;
                layer.geometry.Attach(geom);
                layer.pathData = std::move(single);
                layer.opacity = pi.opacity;
                layer.strokeWidth = pi.strokeWidth;
                layer.transform = pi.transform;
                icon.layers.push_back(std::move(layer));
            }
        }
        icon.valid = !icon.layers.empty();
    } else {
        // All opaque fills — use single combined geometry (faster)
        std::vector<std::string> datas;
        for (auto& pi : paths) datas.push_back(pi.d);
        ID2D1PathGeometry* geom = nullptr;
        if (svg_detail::BuildGeometry(factory_, datas, &geom)) {
            icon.geometry.Attach(geom);
            icon.pathData = std::move(datas);
            icon.valid = true;
        }
    }
    return icon;
}

void Renderer::DrawSvgIcon(const SvgIcon& icon, const D2D1_RECT_F& rect,
                            const D2D1_COLOR_F& color) {
    if (!icon.valid) return;
    if (auto* recorder = ActiveDisplayListRecorder()) {
        SvgIconRef ref;
        ref.view_box_w = icon.viewBoxW;
        ref.view_box_h = icon.viewBoxH;
        ref.path_data = icon.pathData;
        ref.layers.reserve(icon.layers.size());
        for (const auto& layer : icon.layers) {
            SvgPathLayerRef layerRef;
            layerRef.path_data = layer.pathData;
            layerRef.opacity = layer.opacity;
            layerRef.stroke_width = layer.strokeWidth;
            layerRef.transform = layer.transform;
            ref.layers.push_back(std::move(layerRef));
        }
        recorder->DrawSvgIcon(std::move(ref), rect, color);
    }
    if (!ctx_) return;

    float destW = rect.right - rect.left;
    float destH = rect.bottom - rect.top;
    float scale = std::min(destW / icon.viewBoxW, destH / icon.viewBoxH);
    float offX = rect.left + (destW - icon.viewBoxW * scale) / 2.0f;
    float offY = rect.top + (destH - icon.viewBoxH * scale) / 2.0f;

    D2D1_MATRIX_3X2_F oldXform = D2D1::Matrix3x2F::Identity();
    ctx_->GetTransform(&oldXform);
    D2D1_MATRIX_3X2_F iconTransform =
        D2D1::Matrix3x2F::Scale(scale, scale) *
        D2D1::Matrix3x2F::Translation(offX, offY) *
        oldXform;
    ctx_->SetTransform(iconTransform);

    if (!icon.layers.empty()) {
        // Multi-layer with per-path opacity, stroke and SVG transform
        for (auto& layer : icon.layers) {
            if (!layer.geometry) continue;
            D2D1_COLOR_F layerColor = color;
            layerColor.a *= layer.opacity;
            auto brush = GetBrush(layerColor);
            if (!brush) continue;
            /* 行向量乘法：路径局部点 * pathTransform * iconScaleOffset → 屏幕点 */
            ctx_->SetTransform(layer.transform * iconTransform);
            if (layer.strokeWidth > 0) {
                ctx_->DrawGeometry(layer.geometry.Get(), brush.Get(),
                                   layer.strokeWidth, GetRoundStrokeStyle());
            } else {
                ctx_->FillGeometry(layer.geometry.Get(), brush.Get());
            }
        }
    } else if (icon.geometry) {
        // Single combined geometry (all opaque)
        auto brush = GetBrush(color);
        if (brush) ctx_->FillGeometry(icon.geometry.Get(), brush.Get());
    }

    ctx_->SetTransform(oldXform);
}

// L75: 只解析文字 run (D2D 原生 SVG 路径用 —— D2D 画形状, 这里补文字).
std::vector<SvgTextRun> Renderer::ParseSvgTextRuns(const std::string& svgContent) {
    if (svgContent.empty()) return {};
    return svg_detail::ExtractTextRuns(svgContent);
}

// ===== L121: SVG <text> → <path> 字形轮廓内联 (修复文字 z 序) =====
namespace {

/* GetGlyphRunOutline 把字形轮廓写进这个 sink, 序列化成 SVG path-data 串.
 * 字形轮廓坐标系与 D2D 一致 (y 向下, baseline 为 0); 每段加上 (ox_,oy_) =
 * 该 glyph run 的 baseline 原点, 得到 layout 局部空间坐标. 栈对象, 不真正
 * 管理 COM 生命周期. */
class SvgGlyphPathSink final : public ID2D1SimplifiedGeometrySink {
public:
    std::string& data() { return d_; }
    void setOffset(float x, float y) { ox_ = x; oy_ = y; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(ID2D1SimplifiedGeometrySink)) {
            *ppv = static_cast<ID2D1SimplifiedGeometrySink*>(this);
            return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE) override {}
    void STDMETHODCALLTYPE SetSegmentFlags(D2D1_PATH_SEGMENT) override {}
    void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F p, D2D1_FIGURE_BEGIN) override {
        d_ += 'M'; num(p.x + ox_); d_ += ' '; num(p.y + oy_);
    }
    void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F* pts, UINT32 n) override {
        for (UINT32 i = 0; i < n; ++i) {
            d_ += 'L'; num(pts[i].x + ox_); d_ += ' '; num(pts[i].y + oy_);
        }
    }
    void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT* s, UINT32 n) override {
        for (UINT32 i = 0; i < n; ++i) {
            d_ += 'C';
            num(s[i].point1.x + ox_); d_ += ' '; num(s[i].point1.y + oy_); d_ += ' ';
            num(s[i].point2.x + ox_); d_ += ' '; num(s[i].point2.y + oy_); d_ += ' ';
            num(s[i].point3.x + ox_); d_ += ' '; num(s[i].point3.y + oy_);
        }
    }
    void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END) override { d_ += 'Z'; }
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }

private:
    void num(float v) {
        char buf[40];
        int n = snprintf(buf, sizeof(buf), "%.2f", (double)v);
        if (n <= 0) return;
        int e = n;                       // 去末尾 0 / 小数点, 压缩体积
        bool dot = false;
        for (int i = 0; i < n; ++i) if (buf[i] == '.') { dot = true; break; }
        if (dot) {
            while (e > 0 && buf[e-1] == '0') --e;
            if (e > 0 && buf[e-1] == '.') --e;
        }
        d_.append(buf, (size_t)e);
    }
    std::string d_;
    float ox_ = 0.0f, oy_ = 0.0f;
};

/* 自定义 text renderer: 把 layout 的每个 glyph run 通过 GetGlyphRunOutline
 * 灌进 SvgGlyphPathSink (复用 IDWriteTextLayout 的 shaping + 字体回退, 数学
 * 符号 / 中英混排都靠 DWrite 自动处理). */
class SvgGlyphOutlineRenderer final : public IDWriteTextRenderer {
public:
    explicit SvgGlyphOutlineRenderer(SvgGlyphPathSink* sink) : sink_(sink) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IDWritePixelSnapping) ||
            iid == __uuidof(IDWriteTextRenderer)) {
            *ppv = static_cast<IDWriteTextRenderer*>(this);
            return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* o) override {
        *o = TRUE; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* m) override {
        *m = DWRITE_MATRIX{1, 0, 0, 1, 0, 0}; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* p) override {
        *p = 1.0f; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(
            void*, FLOAT bx, FLOAT by, DWRITE_MEASURING_MODE,
            const DWRITE_GLYPH_RUN* run, const DWRITE_GLYPH_RUN_DESCRIPTION*,
            IUnknown*) override {
        if (!run || !run->fontFace || run->glyphCount == 0) return S_OK;
        sink_->setOffset(bx, by);
        run->fontFace->GetGlyphRunOutline(
            run->fontEmSize, run->glyphIndices, run->glyphAdvances,
            run->glyphOffsets, run->glyphCount, run->isSideways,
            (run->bidiLevel & 1), sink_);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT,
            const DWRITE_UNDERLINE*, IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT,
            const DWRITE_STRIKETHROUGH*, IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT,
            IDWriteInlineObject*, BOOL, BOOL, IUnknown*) override { return S_OK; }

private:
    SvgGlyphPathSink* sink_;
};

// 把已布局好的 layout 渲染成 SVG path-data; 原点 (ox,oy) 同 DrawSvgTextRuns.
static std::string RenderLayoutToSvgPath(IDWriteTextLayout* layout, float ox, float oy) {
    if (!layout) return {};
    SvgGlyphPathSink sink;
    SvgGlyphOutlineRenderer renderer(&sink);
    if (FAILED(layout->Draw(nullptr, &renderer, ox, oy))) return {};
    return std::move(sink.data());
}

// 把属性值里的 " 和 & 转义, 安全写进生成的 <path ...> 属性 (fill/transform 串).
static std::string XmlAttrEscape(const std::string& s) {
    std::string o; o.reserve(s.size());
    for (char c : s) {
        if (c == '"') o += "&quot;";
        else if (c == '&') o += "&amp;";
        else if (c == '<') o += "&lt;";
        else o += c;
    }
    return o;
}

static std::string TrimAscii(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n\"'");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n\"'");
    return s.substr(a, b - a + 1);
}

static std::string LowerAscii(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

static float ParseSvgCssLength(const std::string& value, float fontSize, float fallback = 0.0f) {
    std::string s = TrimAscii(value);
    if (s.empty()) return fallback;

    const char* p = s.c_str();
    char* end = nullptr;
    float v = strtof(p, &end);
    if (end == p) return fallback;

    while (*end && isspace((unsigned char)*end)) ++end;
    std::string unit = LowerAscii(TrimAscii(end));
    if (unit.empty() || unit == "px") return v;
    if (unit == "pt") return v * (96.0f / 72.0f);
    if (unit == "pc") return v * 16.0f;
    if (unit == "in") return v * 96.0f;
    if (unit == "cm") return v * (96.0f / 2.54f);
    if (unit == "mm") return v * (96.0f / 25.4f);
    if (unit == "q")  return v * (96.0f / 101.6f);
    if (unit == "em") return v * (fontSize > 0.0f ? fontSize : 16.0f);
    if (unit == "ex") return v * (fontSize > 0.0f ? fontSize * 0.5f : 8.0f);
    return v;
}

static std::string CssDeclValue(const std::string& block, const char* prop) {
    std::string lower = LowerAscii(block);
    std::string key = LowerAscii(prop);
    size_t p = lower.find(key);
    while (p != std::string::npos) {
        bool leftOk = (p == 0) || !(isalnum((unsigned char)lower[p - 1]) || lower[p - 1] == '-');
        size_t q = p + key.size();
        while (q < lower.size() && isspace((unsigned char)lower[q])) ++q;
        if (leftOk && q < lower.size() && lower[q] == ':') {
            size_t v0 = q + 1;
            size_t v1 = block.size();
            int paren = 0;
            char quote = 0;
            for (size_t i = v0; i < block.size(); ++i) {
                char c = block[i];
                if (quote) {
                    if (c == quote) quote = 0;
                    continue;
                }
                if (c == '"' || c == '\'') { quote = c; continue; }
                if (c == '(') { ++paren; continue; }
                if (c == ')' && paren > 0) { --paren; continue; }
                if (c == ';' && paren == 0) { v1 = i; break; }
            }
            return TrimAscii(block.substr(v0, v1 - v0));
        }
        p = lower.find(key, p + key.size());
    }
    return {};
}

static std::vector<uint8_t> DecodeBase64Ascii(const std::string& text) {
    int map[256];
    for (int& v : map) v = -1;
    const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; alphabet[i]; ++i) map[(unsigned char)alphabet[i]] = i;
    std::vector<uint8_t> out;
    int val = 0, bits = -8;
    for (unsigned char c : text) {
        if (isspace(c)) continue;
        if (c == '=') break;
        int m = map[c];
        if (m < 0) continue;
        val = (val << 6) | m;
        bits += 6;
        if (bits >= 0) {
            out.push_back((uint8_t)((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

static std::vector<uint8_t> ExtractWoff2DataUrl(const std::string& src) {
    std::string lower = LowerAscii(src);
    size_t u = lower.find("url(");
    if (u == std::string::npos) return {};
    u += 4;
    size_t e = src.find(')', u);
    if (e == std::string::npos || e <= u) return {};
    std::string url = TrimAscii(src.substr(u, e - u));
    std::string lurl = LowerAscii(url);
    size_t b64 = lurl.find("base64,");
    if (b64 == std::string::npos) return {};
    if (lurl.find("woff2") == std::string::npos && lurl.find("font/woff2") == std::string::npos)
        return {};
    return DecodeBase64Ascii(url.substr(b64 + 7));
}

struct EmbeddedSvgFonts {
    struct Face {
        std::wstring cssFamily;
        std::wstring alias;
        ComPtr<IDWriteFontFace3> face;
        DWRITE_FONT_METRICS metrics{};
    };

    ComPtr<IDWriteFactory5> factory5;
    ComPtr<IDWriteInMemoryFontFileLoader> loader;
    ComPtr<IDWriteFontCollection1> collection;
    std::vector<std::vector<uint8_t>> fontBuffers;
    std::vector<Face> faces;
    std::map<std::wstring, std::vector<size_t>> familyFaces;
    std::map<std::wstring, std::vector<std::wstring>> aliases;

    ~EmbeddedSvgFonts() {
        if (factory5 && loader) factory5->UnregisterFontFileLoader(loader.Get());
    }

    bool HasFamily(const std::wstring& family) const {
        if (!collection || family.empty()) return false;
        UINT32 idx = 0; BOOL exists = FALSE;
        return SUCCEEDED(collection->FindFamilyName(family.c_str(), &idx, &exists)) && exists;
    }

    const Face* FindFaceForChar(const std::vector<std::wstring>& families,
                                wchar_t ch, UINT16* outGlyph) const {
        if (outGlyph) *outGlyph = 0;
        if (faces.empty() || ch == 0) return nullptr;
        for (const auto& fam : families) {
            auto it = familyFaces.find(fam);
            if (it == familyFaces.end()) continue;
            for (size_t idx : it->second) {
                if (idx >= faces.size() || !faces[idx].face) continue;
                UINT16 glyph = 0;
                UINT32 codePoint = (UINT32)ch;
                if (SUCCEEDED(faces[idx].face->GetGlyphIndicesW(&codePoint, 1, &glyph)) && glyph != 0) {
                    if (outGlyph) *outGlyph = glyph;
                    return &faces[idx];
                }
            }
        }
        return nullptr;
    }

    std::vector<std::wstring> ExpandFamilies(const std::vector<std::wstring>& families) const {
        std::vector<std::wstring> expanded;
        expanded.reserve(families.size());
        for (const auto& fam : families) {
            auto it = aliases.find(fam);
            if (it != aliases.end()) {
                for (const auto& alias : it->second) {
                    if (HasFamily(alias)) expanded.push_back(alias);
                }
            } else if (HasFamily(fam)) {
                expanded.push_back(fam);
            }
        }
        return expanded;
    }

    ComPtr<IDWriteFontFallback> MakeFallback(IDWriteFactory* dwFactory,
                                             const std::vector<std::wstring>& families,
                                             IDWriteFontFallback* tail,
                                             const wchar_t* locale,
                                             const wchar_t* skipFirst) const {
        if (!collection || families.empty() || !dwFactory) return nullptr;
        std::vector<std::wstring> expanded = ExpandFamilies(families);
        if (skipFirst && !expanded.empty() && expanded.front() == skipFirst) {
            expanded.erase(expanded.begin());
        }
        std::vector<const WCHAR*> names;
        names.reserve(expanded.size());
        for (const auto& fam : expanded) {
            names.push_back(fam.c_str());
        }
        if (names.empty()) return nullptr;

        ComPtr<IDWriteFactory2> factory2;
        if (FAILED(dwFactory->QueryInterface(__uuidof(IDWriteFactory2),
                reinterpret_cast<void**>(factory2.GetAddressOf()))) || !factory2)
            return nullptr;

        ComPtr<IDWriteFontFallbackBuilder> builder;
        if (FAILED(factory2->CreateFontFallbackBuilder(builder.GetAddressOf())) || !builder)
            return nullptr;
        DWRITE_UNICODE_RANGE range{0x0000, 0x10FFFF};
        if (FAILED(builder->AddMapping(&range, 1, names.data(), (UINT32)names.size(),
                collection.Get(), locale, nullptr, 1.0f)))
            return nullptr;
        if (tail) builder->AddMappings(tail);
        ComPtr<IDWriteFontFallback> fallback;
        if (FAILED(builder->CreateFontFallback(fallback.GetAddressOf()))) return nullptr;
        return fallback;
    }
};

static bool CopyFontFileStream(IDWriteFontFileStream* stream, std::vector<uint8_t>& out) {
    if (!stream) return false;
    UINT64 size64 = 0;
    if (FAILED(stream->GetFileSize(&size64)) || size64 == 0 || size64 > UINT32_MAX)
        return false;
    const void* fragment = nullptr;
    void* ctx = nullptr;
    if (FAILED(stream->ReadFileFragment(&fragment, 0, size64, &ctx)) || !fragment)
        return false;
    out.resize((size_t)size64);
    std::memcpy(out.data(), fragment, (size_t)size64);
    stream->ReleaseFileFragment(ctx);
    return true;
}

static std::vector<uint8_t> UnpackFontContainer(IDWriteFactory5* factory5,
                                                const std::vector<uint8_t>& data) {
    if (!factory5 || data.empty()) return data;
    DWRITE_CONTAINER_TYPE type = factory5->AnalyzeContainerType(data.data(), (UINT32)data.size());
    if (type == DWRITE_CONTAINER_TYPE_UNKNOWN) return data;
    ComPtr<IDWriteFontFileStream> stream;
    if (FAILED(factory5->UnpackFontFile(type, data.data(), (UINT32)data.size(),
            stream.GetAddressOf())) || !stream)
        return {};
    std::vector<uint8_t> unpacked;
    return CopyFontFileStream(stream.Get(), unpacked) ? unpacked : std::vector<uint8_t>{};
}

static const std::wstring* FirstEmbeddedAlias(const EmbeddedSvgFonts* fonts,
                                              const std::vector<std::wstring>& families) {
    if (!fonts || families.empty()) return nullptr;
    auto it = fonts->aliases.find(families.front());
    if (it != fonts->aliases.end() && !it->second.empty())
        return &it->second.front();
    return fonts->HasFamily(families.front()) ? &families.front() : nullptr;
}

static std::unique_ptr<EmbeddedSvgFonts> BuildEmbeddedSvgFonts(IDWriteFactory* dwFactory,
                                                               const std::string& svg) {
    if (!dwFactory || svg.find("@font-face") == std::string::npos) return nullptr;
    ComPtr<IDWriteFactory5> factory5;
    if (FAILED(dwFactory->QueryInterface(__uuidof(IDWriteFactory5),
            reinterpret_cast<void**>(factory5.GetAddressOf()))) || !factory5)
        return nullptr;

    auto ctx = std::make_unique<EmbeddedSvgFonts>();
    ctx->factory5 = factory5;
    if (FAILED(factory5->CreateInMemoryFontFileLoader(ctx->loader.GetAddressOf())) || !ctx->loader)
        return nullptr;
    if (FAILED(factory5->RegisterFontFileLoader(ctx->loader.Get())))
        return nullptr;

    ComPtr<IDWriteFontSetBuilder1> builder;
    if (FAILED(factory5->CreateFontSetBuilder(builder.GetAddressOf())) || !builder)
        return nullptr;

    bool added = false;
    uint32_t fontIndex = 0;
    size_t pos = 0;
    while ((pos = svg.find("@font-face", pos)) != std::string::npos) {
        size_t lb = svg.find('{', pos);
        size_t rb = lb == std::string::npos ? std::string::npos : svg.find('}', lb + 1);
        if (lb == std::string::npos || rb == std::string::npos) break;
        std::string block = svg.substr(lb + 1, rb - lb - 1);
        pos = rb + 1;

        std::string family8 = CssDeclValue(block, "font-family");
        std::string src = CssDeclValue(block, "src");
        if (family8.empty() || src.empty()) continue;
        std::vector<uint8_t> fontData = ExtractWoff2DataUrl(src);
        if (fontData.empty()) continue;
        fontData = UnpackFontContainer(factory5.Get(), fontData);
        if (fontData.empty()) continue;

        std::wstring family = svg_detail::Utf8ToWide(TrimAscii(family8));
        if (family.empty()) continue;
        std::wstring alias = family + L"__svgfont_" + std::to_wstring(fontIndex++);

        ctx->fontBuffers.push_back(std::move(fontData));
        const auto& storedFont = ctx->fontBuffers.back();
        ComPtr<IDWriteFontFile> fontFile;
        if (FAILED(ctx->loader->CreateInMemoryFontFileReference(
                dwFactory, storedFont.data(), (UINT32)storedFont.size(), nullptr,
                fontFile.GetAddressOf())) || !fontFile)
            continue;

        ComPtr<IDWriteFontFaceReference> ref;
        if (FAILED(factory5->CreateFontFaceReference(
                fontFile.Get(), 0, DWRITE_FONT_SIMULATIONS_NONE, ref.GetAddressOf())) || !ref)
            continue;

        ComPtr<IDWriteFontFace3> face;
        if (FAILED(ref->CreateFontFace(face.GetAddressOf())) || !face)
            continue;

        DWRITE_FONT_PROPERTY prop{DWRITE_FONT_PROPERTY_ID_FAMILY_NAME, alias.c_str(), L"en-us"};
        if (SUCCEEDED(builder->AddFontFaceReference(ref.Get(), &prop, 1))) {
            EmbeddedSvgFonts::Face f;
            f.cssFamily = family;
            f.alias = alias;
            f.face = face;
            f.face->GetMetrics(&f.metrics);
            ctx->familyFaces[family].push_back(ctx->faces.size());
            ctx->faces.push_back(std::move(f));
            ctx->aliases[family].push_back(std::move(alias));
            added = true;
        }
    }

    if (!added) return nullptr;
    ComPtr<IDWriteFontSet> fontSet;
    if (FAILED(builder->CreateFontSet(fontSet.GetAddressOf())) || !fontSet)
        return nullptr;
    if (FAILED(factory5->CreateFontCollectionFromFontSet(fontSet.Get(), ctx->collection.GetAddressOf())) ||
        !ctx->collection)
        return nullptr;
    return ctx;
}

static bool RenderEmbeddedGlyphRun(const EmbeddedSvgFonts* fonts,
                                   const std::wstring& txt,
                                   float fontSize,
                                   const std::vector<std::wstring>& families,
                                   DWRITE_FONT_WEIGHT weight,
                                   DWRITE_FONT_STYLE fstyle,
                                   float svgX, float svgY,
                                   int anchor,
                                   bool block,
                                   std::string* outPath,
                                   float* outAdv) {
    if (outPath) outPath->clear();
    if (outAdv) *outAdv = 0.0f;
    if (!fonts || txt.empty() || fontSize <= 0.0f || families.empty())
        return false;
    if (weight != DWRITE_FONT_WEIGHT_NORMAL || fstyle != DWRITE_FONT_STYLE_NORMAL)
        return false;

    struct Glyph {
        const EmbeddedSvgFonts::Face* face = nullptr;
        UINT16 glyph = 0;
        float advance = 0.0f;
    };
    std::vector<Glyph> glyphs;
    glyphs.reserve(txt.size());

    float totalAdvance = 0.0f;
    float maxAscent = 0.0f;
    float maxLineHeight = 0.0f;
    for (size_t i = 0; i < txt.size(); ++i) {
        wchar_t ch = txt[i];
        if (ch == L'\r') continue;
        if (ch == L'\n') return false;  // keep complex multiline layout on the DWrite path
        if (ch >= 0xD800 && ch <= 0xDFFF) return false; // keep surrogate pairs on DWrite shaping

        UINT16 glyph = 0;
        const auto* face = fonts->FindFaceForChar(families, ch, &glyph);
        if (!face || !face->face) {
            if (ch == L' ' || ch == L'\t') {
                float adv = (ch == L'\t') ? fontSize : fontSize * 0.33f;
                glyphs.push_back(Glyph{nullptr, 0, adv});
                totalAdvance += adv;
                continue;
            }
            return false;
        }

        const float upm = face->metrics.designUnitsPerEm > 0
            ? (float)face->metrics.designUnitsPerEm : 1000.0f;
        const float scale = fontSize / upm;
        DWRITE_GLYPH_METRICS gm{};
        if (FAILED(face->face->GetDesignGlyphMetrics(&glyph, 1, &gm, FALSE)))
            return false;
        float adv = (float)gm.advanceWidth * scale;
        if (adv <= 0.0f) adv = fontSize * 0.5f;
        totalAdvance += adv;
        maxAscent = std::max(maxAscent, (float)face->metrics.ascent * scale);
        maxLineHeight = std::max(maxLineHeight,
            (float)(face->metrics.ascent + face->metrics.descent + face->metrics.lineGap) * scale);
        glyphs.push_back(Glyph{face, glyph, adv});
    }

    if (glyphs.empty()) return false;
    if (outAdv) *outAdv = totalAdvance;

    float x = svgX;
    if (anchor == 1)      x -= totalAdvance * 0.5f;
    else if (anchor == 2) x -= totalAdvance;
    float baseline = svgY;
    if (block) {
        const float h = maxLineHeight > 0.0f ? maxLineHeight : fontSize;
        const float a = maxAscent > 0.0f ? maxAscent : fontSize * 0.8f;
        x = svgX - totalAdvance * 0.5f;
        baseline = svgY - h * 0.5f + a;
    }

    SvgGlyphPathSink sink;
    float pen = 0.0f;
    for (const auto& g : glyphs) {
        if (!g.face || !g.face->face || g.glyph == 0) {
            pen += g.advance;
            continue;
        }
        DWRITE_GLYPH_OFFSET off{0.0f, 0.0f};
        FLOAT adv = g.advance;
        sink.setOffset(x + pen, baseline);
        if (FAILED(g.face->face->GetGlyphRunOutline(
                fontSize, &g.glyph, &adv, &off, 1, FALSE, FALSE, &sink)))
            return false;
        pen += g.advance;
    }

    if (outPath) *outPath = std::move(sink.data());
    return outPath && !outPath->empty();
}

} // namespace

std::string Renderer::SvgInlineTextAsPaths(const std::string& svg) {
    if (svg.empty() || !dwFactory_) return svg;
    using svg_detail::extractAttrFromTag;
    using svg_detail::CssProp;
    using svg_detail::ResolveFontFamily;
    using svg_detail::ResolveFontFamilyList;
    using svg_detail::ParseFontWeight;
    using svg_detail::ExtractInnerText;
    using svg_detail::AppendDecoded;
    using svg_detail::Utf8ToWide;

    auto embeddedFonts = BuildEmbeddedSvgFonts(dwFactory_, svg);
    auto shouldSkipForeignObject = [&](const std::string& tag, size_t foStart, size_t foEnd) {
        auto isPercentLength = [](const std::string& v) {
            return v.find('%') != std::string::npos;
        };
        if (isPercentLength(extractAttrFromTag(tag, "width")) ||
            isPercentLength(extractAttrFromTag(tag, "height")))
            return true;

        size_t swOpen = svg.rfind("<switch", foStart);
        if (swOpen == std::string::npos) return false;
        size_t prevSwClose = svg.rfind("</switch>", foStart);
        if (prevSwClose != std::string::npos && prevSwClose > swOpen) return false;
        size_t swClose = svg.find("</switch>", foEnd);
        if (swClose == std::string::npos) return false;
        size_t img = svg.find("<image", foEnd);
        return img != std::string::npos && img < swClose;
    };
    auto cleanCssColor = [](std::string v) -> std::string {
        std::string lc = LowerAscii(v);
        size_t bang = lc.find("!important");
        if (bang != std::string::npos) v = v.substr(0, bang);
        v = TrimAscii(v);
        lc = LowerAscii(v);
        if (lc.empty() || lc == "inherit" || lc == "initial" ||
            lc == "unset" || lc == "currentcolor") {
            return {};
        }
        return v;
    };
    auto foreignObjectTextColor = [&](size_t start, size_t end) -> std::string {
        std::string color;
        size_t p = start;
        while (p < end) {
            size_t lt2 = svg.find('<', p);
            if (lt2 == std::string::npos || lt2 >= end) break;
            if (lt2 + 1 < end && svg[lt2 + 1] == '/') {
                p = lt2 + 2;
                continue;
            }
            size_t te2 = svg.find('>', lt2);
            if (te2 == std::string::npos || te2 >= end) break;
            std::string childTag = svg.substr(lt2, te2 - lt2 + 1);
            std::string v = extractAttrFromTag(childTag, "color");
            if (v.empty()) v = CssProp(extractAttrFromTag(childTag, "style"), "color");
            v = cleanCssColor(v);
            if (!v.empty()) color = std::move(v);
            p = te2 + 1;
        }
        return color;
    };
    struct SvgFoBackground {
        bool valid = false;
        std::string fill;
        std::string opacity;
    };
    auto formatFloatAttr = [](float v) -> std::string {
        char buf[48];
        int n = snprintf(buf, sizeof(buf), "%.3f", (double)v);
        if (n <= 0) return "0";
        int e = n;
        while (e > 0 && buf[e - 1] == '0') --e;
        if (e > 0 && buf[e - 1] == '.') --e;
        if (e <= 0) return "0";
        return std::string(buf, static_cast<size_t>(e));
    };
    auto colorToSvgAttrs = [&](const std::string& cssColor, SvgFoBackground& out) {
        D2D1_COLOR_F c{};
        if (!svg_detail::ParseSvgColor(cleanCssColor(cssColor), c) || c.a <= 0.0f) return;
        auto byte = [](float x) -> int {
            int v = static_cast<int>(std::round(x * 255.0f));
            return v < 0 ? 0 : (v > 255 ? 255 : v);
        };
        char fill[16];
        snprintf(fill, sizeof(fill), "#%02x%02x%02x", byte(c.r), byte(c.g), byte(c.b));
        out.valid = true;
        out.fill = fill;
        out.opacity = c.a < 0.999f ? formatFloatAttr(c.a) : std::string();
    };
    auto foreignObjectBackground = [&](size_t start, size_t end) -> SvgFoBackground {
        SvgFoBackground bg;
        size_t p = start;
        while (p < end) {
            size_t lt2 = svg.find('<', p);
            if (lt2 == std::string::npos || lt2 >= end) break;
            if (lt2 + 1 < end && svg[lt2 + 1] == '/') {
                p = lt2 + 2;
                continue;
            }
            size_t te2 = svg.find('>', lt2);
            if (te2 == std::string::npos || te2 >= end) break;
            std::string childTag = svg.substr(lt2, te2 - lt2 + 1);
            std::string v = extractAttrFromTag(childTag, "background-color");
            if (v.empty()) {
                v = CssProp(extractAttrFromTag(childTag, "style"), "background-color");
            }
            if (!v.empty()) colorToSvgAttrs(v, bg);
            p = te2 + 1;
        }
        return bg;
    };

    /* 沿 <g> 链继承的"文字呈现属性"(只继承 font/fill, 不碰 transform/opacity ——
     * 那两样留给 DOM 作用在生成的 <path> 祖先上, 避免双重应用). */
    struct GFont {
        float fontSize = 0.0f;                                   // 0 = 未设
        std::vector<std::wstring> families; bool familySet = false;
        DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL; bool weightSet = false;
        DWRITE_FONT_STYLE  fstyle = DWRITE_FONT_STYLE_NORMAL;  bool styleSet  = false;  // L122: italic
        std::string fill; bool fillSet = false;                  // url()/#hex/named, "" = 未设
    };
    auto applyFont = [&](GFont& st, const std::string& tag, const std::string& style) {
        std::string s;
        if ((s = extractAttrFromTag(tag, "fill")).empty()) s = CssProp(style, "fill");
        if (s.empty()) s = CssProp(style, "color");
        if (!s.empty()) { st.fill = s; st.fillSet = true; }
        if ((s = extractAttrFromTag(tag, "font-size")).empty()) s = CssProp(style, "font-size");
        if (!s.empty()) {
            float base = st.fontSize > 0.0f ? st.fontSize : 16.0f;
            float v = ParseSvgCssLength(s, base, 0.0f);
            if (v > 0) st.fontSize = v;
        }
        if ((s = extractAttrFromTag(tag, "font-family")).empty()) s = CssProp(style, "font-family");
        if (!s.empty()) { st.families = ResolveFontFamilyList(s); st.familySet = true; }
        if ((s = extractAttrFromTag(tag, "font-weight")).empty()) s = CssProp(style, "font-weight");
        if (!s.empty()) { st.weight = ParseFontWeight(s); st.weightSet = true; }
        if ((s = extractAttrFromTag(tag, "font-style")).empty()) s = CssProp(style, "font-style");
        if (!s.empty()) {
            st.fstyle = (s.find("italic")  != std::string::npos) ? DWRITE_FONT_STYLE_ITALIC
                      : (s.find("oblique") != std::string::npos) ? DWRITE_FONT_STYLE_OBLIQUE
                      :                                            DWRITE_FONT_STYLE_NORMAL;
            st.styleSet = true;
        }
    };

    /* 把一段文本渲染成 SVG path-data。svgX/svgY = SVG 用户坐标 (svgY 为 baseline),
     * anchor: 0=start 1=middle 2=end; block=围绕(svgX,svgY)居中(foreignObject)。
     * *outAdv 回填布局自然宽 (供 tspan 无显式 x 时接续)。每次新建 format (转换在
     * load 时一次性跑, 不必缓存), 带 font-style + 字体回退。 */
    auto renderRun = [&](const std::wstring& txt, float fontSize, const std::vector<std::wstring>& families,
                         DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE fstyle,
                         float svgX, float svgY, int anchor, bool block, float maxW,
                         float* outAdv) -> std::string {
        if (outAdv) *outAdv = 0.0f;
        if (txt.empty()) return {};
        std::string embeddedPath;
        if (RenderEmbeddedGlyphRun(embeddedFonts.get(), txt, fontSize, families,
                                   weight, fstyle, svgX, svgY, anchor, block,
                                   &embeddedPath, outAdv)) {
            return embeddedPath;
        }
        const std::wstring* embeddedPrimary = FirstEmbeddedAlias(embeddedFonts.get(), families);
        const wchar_t* fam = embeddedPrimary ? embeddedPrimary->c_str()
                            : (!families.empty() ? families.front().c_str() : DefaultFontFamily());
        if (!fam || !fam[0]) fam = L"Segoe UI";
        IDWriteFontCollection* collection =
            embeddedPrimary
                ? embeddedFonts->collection.Get() : nullptr;
        ComPtr<IDWriteTextFormat> fmt;
        if (FAILED(dwFactory_->CreateTextFormat(fam, collection, weight, fstyle,
                DWRITE_FONT_STRETCH_NORMAL, fontSize, ResolveLocaleName(),
                fmt.GetAddressOf())) || !fmt)
            return {};
        ComPtr<IDWriteFontFallback> embeddedFallback =
            embeddedFonts ? embeddedFonts->MakeFallback(dwFactory_, families, fontFallback_.Get(),
                                                        ResolveLocaleName(), embeddedPrimary ? embeddedPrimary->c_str() : nullptr)
                          : nullptr;
        if (embeddedFallback || fontFallback_) {
            ComPtr<IDWriteTextFormat3> f3;
            if (SUCCEEDED(fmt.As(&f3)) && f3)
                f3->SetFontFallback(embeddedFallback ? embeddedFallback.Get() : fontFallback_.Get());
        }
        float layoutMaxW = maxW > 1.0f ? maxW : 100000.0f;
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(dwFactory_->CreateTextLayout(txt.c_str(), (UINT32)txt.size(),
                fmt.Get(), layoutMaxW, 100000.0f, &layout)) || !layout)
            return {};
        layout->SetWordWrapping(maxW > 1.0f ? DWRITE_WORD_WRAPPING_WRAP
                                            : DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TEXT_METRICS tm{};
        layout->GetMetrics(&tm);
        if (block) {
            layout->SetMaxWidth(tm.width + 1.0f);
            layout->SetTextAlignment(anchor == 2 ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                 : DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        float ox = svgX, oy = svgY;
        if (block) {
            ox = svgX - tm.width  / 2.0f;
            oy = svgY - tm.height / 2.0f;
        } else {
            if (anchor == 1)      ox = svgX - tm.width / 2.0f;
            else if (anchor == 2) ox = svgX - tm.width;
            DWRITE_LINE_METRICS lm{}; UINT32 lc = 0;
            if (SUCCEEDED(layout->GetLineMetrics(&lm, 1, &lc)) && lc >= 1)
                oy = svgY - lm.baseline;
        }
        if (outAdv) *outAdv = tm.width;
        return RenderLayoutToSvgPath(layout.Get(), ox, oy);
    };

    auto buildPath = [&](const std::string& d, const std::string& fillStr,
                         const std::string& opacityStr, const std::string& fillOpacityStr,
                         const std::string& xfStr) -> std::string {
        std::string p = "<path d=\""; p += d;
        p += "\" fill=\"" + XmlAttrEscape(fillStr) + "\" fill-rule=\"nonzero\"";
        p += " stroke=\"none\" stroke-width=\"0\"";
        if (!opacityStr.empty())     p += " opacity=\"" + XmlAttrEscape(opacityStr) + "\"";
        if (!fillOpacityStr.empty()) p += " fill-opacity=\"" + XmlAttrEscape(fillOpacityStr) + "\"";
        if (!xfStr.empty())          p += " transform=\"" + XmlAttrEscape(xfStr) + "\"";
        p += "/>";
        return p;
    };
    auto buildRect = [&](float x, float y, float w, float h,
                         const SvgFoBackground& bg, const std::string& xfStr) -> std::string {
        if (!bg.valid || w <= 0.0f || h <= 0.0f) return {};
        std::string r = "<rect x=\"";
        r += formatFloatAttr(x);
        r += "\" y=\"";
        r += formatFloatAttr(y);
        r += "\" width=\"";
        r += formatFloatAttr(w);
        r += "\" height=\"";
        r += formatFloatAttr(h);
        r += "\" fill=\"";
        r += XmlAttrEscape(bg.fill);
        r += "\" stroke=\"none\" stroke-width=\"0\"";
        if (!bg.opacity.empty()) {
            r += " fill-opacity=\"";
            r += XmlAttrEscape(bg.opacity);
            r += "\"";
        }
        if (!xfStr.empty()) {
            r += " transform=\"";
            r += XmlAttrEscape(xfStr);
            r += "\"";
        }
        r += "/>";
        return r;
    };

    struct Repl { size_t start, end; std::string text; };

    struct DrawIoHtmlStyle {
        float fontSize = 16.0f;
        std::vector<std::wstring> families;
        DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
        DWRITE_FONT_STYLE fstyle = DWRITE_FONT_STYLE_NORMAL;
        std::string fill = "#000000";
        float lineHeight = 0.0f;
        int baseline = 0;  // negative=sup, positive=sub
    };
    struct DrawIoTextRun {
        std::wstring text;
        DrawIoHtmlStyle style;
        float width = 0.0f;
        float height = 0.0f;
        float baseline = 0.0f;
    };
    struct DrawIoLine {
        std::vector<DrawIoTextRun> runs;
        float width = 0.0f;
        float height = 0.0f;
        float baselineFromTop = 0.0f;
        float maxFontSize = 0.0f;
    };
    struct DrawIoLabelBox {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        int anchor = 1;
        bool valid = false;
    };

    auto drawIoTagName = [](const std::string& tag) -> std::string {
        size_t i = 1;
        while (i < tag.size() && (tag[i] == '/' || tag[i] == '!' || tag[i] == '?')) ++i;
        while (i < tag.size() && std::isspace((unsigned char)tag[i])) ++i;
        size_t s = i;
        while (i < tag.size()) {
            unsigned char c = (unsigned char)tag[i];
            if (!(std::isalnum(c) || c == ':' || c == '-' || c == '_')) break;
            ++i;
        }
        std::string name = LowerAscii(tag.substr(s, i - s));
        size_t colon = name.rfind(':');
        return colon == std::string::npos ? name : name.substr(colon + 1);
    };
    auto drawIoSelfClosing = [](const std::string& tag) -> bool {
        size_t i = tag.size();
        while (i > 0 && std::isspace((unsigned char)tag[i - 1])) --i;
        return i >= 2 && tag[i - 2] == '/' && tag[i - 1] == '>';
    };
    auto drawIoVoidTag = [](const std::string& name) -> bool {
        return name == "br" || name == "hr" || name == "img" ||
               name == "input" || name == "meta" || name == "link";
    };
    auto drawIoLineHeight = [&](const std::string& value, float fontSize) -> float {
        std::string s = LowerAscii(TrimAscii(value));
        if (s.empty() || s == "normal") return fontSize * 1.2f;
        char* end = nullptr;
        float v = std::strtof(s.c_str(), &end);
        if (end == s.c_str() || v <= 0.0f) return fontSize * 1.2f;
        while (*end && std::isspace((unsigned char)*end)) ++end;
        if (!*end) return v * fontSize;
        return ParseSvgCssLength(s, fontSize, fontSize * 1.2f);
    };
    auto drawIoSameStyle = [](const DrawIoHtmlStyle& a, const DrawIoHtmlStyle& b) {
        return a.fontSize == b.fontSize &&
               a.families == b.families &&
               a.weight == b.weight &&
               a.fstyle == b.fstyle &&
               a.fill == b.fill &&
               a.lineHeight == b.lineHeight &&
               a.baseline == b.baseline;
    };
    auto drawIoBaselineShift = [](const DrawIoTextRun& run, float maxFontSize) -> float {
        if (run.style.baseline > 0)
            return maxFontSize * 0.22f * (float)run.style.baseline;
        if (run.style.baseline < 0)
            return -maxFontSize * 0.38f * (float)(-run.style.baseline);
        return 0.0f;
    };
    auto drawIoApplyStyle = [&](DrawIoHtmlStyle& st,
                                const std::string& name,
                                const std::string& tag) {
        std::string style = extractAttrFromTag(tag, "style");
        if (name == "b" || name == "strong") st.weight = DWRITE_FONT_WEIGHT_BOLD;
        if (name == "i" || name == "em")     st.fstyle = DWRITE_FONT_STYLE_ITALIC;
        if (name == "sub") {
            st.baseline += 1;
            st.fontSize *= 0.65f;
        } else if (name == "sup") {
            st.baseline -= 1;
            st.fontSize *= 0.65f;
        }

        std::string s;
        if (name == "font") {
            s = extractAttrFromTag(tag, "face");
            if (!s.empty()) {
                auto families = ResolveFontFamilyList(s);
                if (!families.empty()) st.families = std::move(families);
            }
            s = extractAttrFromTag(tag, "color");
            s = cleanCssColor(s);
            if (!s.empty()) st.fill = std::move(s);
        }

        s = CssProp(style, "font-size");
        if (!s.empty()) {
            float v = ParseSvgCssLength(s, st.fontSize > 0.0f ? st.fontSize : 16.0f, 0.0f);
            if (v > 0.0f) st.fontSize = v;
        }
        s = CssProp(style, "font-family");
        if (!s.empty()) {
            auto families = ResolveFontFamilyList(s);
            if (!families.empty()) st.families = std::move(families);
        }
        s = CssProp(style, "font-weight");
        if (!s.empty()) st.weight = ParseFontWeight(s);
        s = CssProp(style, "font-style");
        if (!s.empty()) {
            std::string lc = LowerAscii(s);
            st.fstyle = lc.find("italic") != std::string::npos ? DWRITE_FONT_STYLE_ITALIC
                      : lc.find("oblique") != std::string::npos ? DWRITE_FONT_STYLE_OBLIQUE
                      : DWRITE_FONT_STYLE_NORMAL;
        }
        s = CssProp(style, "color");
        if (s.empty()) s = extractAttrFromTag(tag, "color");
        s = cleanCssColor(s);
        if (!s.empty()) st.fill = std::move(s);
        s = CssProp(style, "line-height");
        if (!s.empty()) st.lineHeight = drawIoLineHeight(s, st.fontSize);
        s = CssProp(style, "vertical-align");
        if (!s.empty()) {
            std::string lc = LowerAscii(s);
            if (lc.find("sub") != std::string::npos) st.baseline += 1;
            else if (lc.find("super") != std::string::npos ||
                     lc.find("sup") != std::string::npos) st.baseline -= 1;
        }
    };
    auto drawIoParseBox = [&](const std::string& tag,
                              size_t contentStart,
                              size_t contentEnd,
                              float baseFontSize,
                              DrawIoLabelBox& box) -> bool {
        std::string fw = extractAttrFromTag(tag, "width");
        std::string fh = extractAttrFromTag(tag, "height");
        if (fw.find('%') == std::string::npos && fh.find('%') == std::string::npos) {
            box.x = ParseSvgCssLength(extractAttrFromTag(tag, "x"), baseFontSize, 0.0f);
            box.y = ParseSvgCssLength(extractAttrFromTag(tag, "y"), baseFontSize, 0.0f);
            box.w = ParseSvgCssLength(fw, baseFontSize, 0.0f);
            box.h = ParseSvgCssLength(fh, baseFontSize, 0.0f);
            box.valid = box.w > 0.0f && box.h >= 0.0f;
        }

        size_t p = contentStart;
        while (p < contentEnd) {
            size_t lt2 = svg.find('<', p);
            if (lt2 == std::string::npos || lt2 >= contentEnd) break;
            size_t te2 = svg.find('>', lt2);
            if (te2 == std::string::npos || te2 >= contentEnd) break;
            std::string childTag = svg.substr(lt2, te2 - lt2 + 1);
            p = te2 + 1;
            if (lt2 + 1 < svg.size() && svg[lt2 + 1] == '/') continue;
            std::string name = drawIoTagName(childTag);
            if (name != "div" && name != "span") continue;
            std::string style = extractAttrFromTag(childTag, "style");
            if (style.empty()) continue;

            std::string align = CssProp(style, "text-align");
            if (!align.empty()) {
                align = LowerAscii(align);
                if (align.find("right") != std::string::npos) box.anchor = 2;
                else if (align.find("center") != std::string::npos) box.anchor = 1;
                else if (align.find("left") != std::string::npos) box.anchor = 0;
            }
            std::string justify = CssProp(style, "justify-content");
            if (!justify.empty()) {
                justify = LowerAscii(justify);
                if (justify.find("flex-end") != std::string::npos ||
                    justify.find("end") != std::string::npos) {
                    box.anchor = 2;
                } else if (justify.find("center") != std::string::npos) {
                    box.anchor = 1;
                } else if (justify.find("flex-start") != std::string::npos ||
                           justify.find("start") != std::string::npos) {
                    box.anchor = 0;
                }
            }

            std::string w = CssProp(style, "width");
            std::string h = CssProp(style, "height");
            std::string ml = CssProp(style, "margin-left");
            std::string mt = CssProp(style, "margin-top");
            std::string pt = CssProp(style, "padding-top");
            if (!w.empty() && (!ml.empty() || !pt.empty() || !mt.empty())) {
                box.x = ParseSvgCssLength(ml, baseFontSize, box.x);
                box.y = ParseSvgCssLength(!pt.empty() ? pt : mt, baseFontSize, box.y);
                box.w = ParseSvgCssLength(w, baseFontSize, box.w);
                box.h = ParseSvgCssLength(h, baseFontSize, box.h);
                box.valid = box.w > 0.0f && box.h >= 0.0f;
                return box.valid;
            }
        }
        return box.valid;
    };
    auto drawIoMeasureRun = [&](const DrawIoTextRun& run,
                                float* outWidth,
                                float* outHeight,
                                float* outBaseline) -> bool {
        if (outWidth) *outWidth = 0.0f;
        if (outHeight) *outHeight = 0.0f;
        if (outBaseline) *outBaseline = 0.0f;
        if (run.text.empty()) return false;
        const std::wstring* embeddedPrimary = FirstEmbeddedAlias(embeddedFonts.get(), run.style.families);
        const wchar_t* fam = embeddedPrimary ? embeddedPrimary->c_str()
                            : (!run.style.families.empty() ? run.style.families.front().c_str()
                                                           : DefaultFontFamily());
        if (!fam || !fam[0]) fam = L"Segoe UI";
        IDWriteFontCollection* collection = embeddedPrimary ? embeddedFonts->collection.Get() : nullptr;
        ComPtr<IDWriteTextFormat> fmt;
        if (FAILED(dwFactory_->CreateTextFormat(fam, collection, run.style.weight,
                run.style.fstyle, DWRITE_FONT_STRETCH_NORMAL, run.style.fontSize,
                ResolveLocaleName(), fmt.GetAddressOf())) || !fmt)
            return false;
        ComPtr<IDWriteFontFallback> embeddedFallback =
            embeddedFonts ? embeddedFonts->MakeFallback(dwFactory_, run.style.families,
                                                        fontFallback_.Get(), ResolveLocaleName(),
                                                        embeddedPrimary ? embeddedPrimary->c_str() : nullptr)
                          : nullptr;
        if (embeddedFallback || fontFallback_) {
            ComPtr<IDWriteTextFormat3> f3;
            if (SUCCEEDED(fmt.As(&f3)) && f3)
                f3->SetFontFallback(embeddedFallback ? embeddedFallback.Get() : fontFallback_.Get());
        }
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(dwFactory_->CreateTextLayout(run.text.c_str(), (UINT32)run.text.size(),
                fmt.Get(), 100000.0f, 100000.0f, layout.GetAddressOf())) || !layout)
            return false;
        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TEXT_METRICS tm{};
        layout->GetMetrics(&tm);
        DWRITE_LINE_METRICS lm{};
        UINT32 lineCount = 0;
        layout->GetLineMetrics(&lm, 1, &lineCount);
        float width = std::max(tm.width, tm.widthIncludingTrailingWhitespace);
        float height = tm.height > 0.0f ? tm.height : run.style.fontSize * 1.2f;
        float baseline = lineCount >= 1 && lm.baseline > 0.0f
            ? lm.baseline : run.style.fontSize * 0.8f;
        if (outWidth) *outWidth = width;
        if (outHeight) *outHeight = height;
        if (outBaseline) *outBaseline = baseline;
        return width > 0.0f || height > 0.0f;
    };
    auto tryRenderDrawIoForeignObject = [&](const std::string& tag,
                                            size_t elemStart,
                                            size_t elemEnd,
                                            size_t contentStart,
                                            size_t contentEnd,
                                            float baseFontSize,
                                            const std::vector<std::wstring>& baseFamilies,
                                            DWRITE_FONT_WEIGHT baseWeight,
                                            DWRITE_FONT_STYLE baseStyle,
                                            const std::string& baseFill,
                                            const std::string& opacityStr,
                                            const std::string& fillOpacityStr,
                                            const std::string& xfStr,
                                            Repl& out) -> bool {
        if (contentStart >= contentEnd || svg.find("http://www.w3.org/1999/xhtml", contentStart) >= contentEnd)
            return false;
        DrawIoLabelBox box;
        if (!drawIoParseBox(tag, contentStart, contentEnd, baseFontSize, box) || !box.valid)
            return false;

        DrawIoHtmlStyle base;
        base.fontSize = baseFontSize > 0.0f ? baseFontSize : 16.0f;
        base.families = baseFamilies;
        base.weight = baseWeight;
        base.fstyle = baseStyle;
        base.fill = baseFill.empty() ? "#000000" : baseFill;
        base.lineHeight = base.fontSize * 1.2f;

        std::vector<DrawIoLine> lines(1);
        std::vector<DrawIoHtmlStyle> htmlStack;
        htmlStack.push_back(base);

        auto trimLineEnd = [&]() {
            if (lines.empty() || lines.back().runs.empty()) return;
            auto& t = lines.back().runs.back().text;
            while (!t.empty() && t.back() == L' ') t.pop_back();
            if (t.empty()) lines.back().runs.pop_back();
        };
        auto lineBreak = [&]() {
            trimLineEnd();
            if (!lines.back().runs.empty()) lines.push_back(DrawIoLine{});
        };
        auto appendText = [&](const std::string& raw) {
            std::string decoded;
            AppendDecoded(decoded, raw);
            if (decoded.empty()) return;
            std::string norm;
            bool haveText = !lines.back().runs.empty() && !lines.back().runs.back().text.empty();
            bool lastSpace = haveText && lines.back().runs.back().text.back() == L' ';
            for (unsigned char c : decoded) {
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                    if ((haveText || !norm.empty()) && !lastSpace) {
                        norm.push_back(' ');
                        lastSpace = true;
                    }
                } else {
                    norm.push_back((char)c);
                    haveText = true;
                    lastSpace = false;
                }
            }
            if (norm.empty()) return;
            std::wstring text = Utf8ToWide(norm);
            if (text.empty()) return;
            const DrawIoHtmlStyle& st = htmlStack.back();
            if (!lines.back().runs.empty() &&
                drawIoSameStyle(lines.back().runs.back().style, st)) {
                lines.back().runs.back().text += text;
            } else {
                DrawIoTextRun run;
                run.text = std::move(text);
                run.style = st;
                lines.back().runs.push_back(std::move(run));
            }
        };

        size_t p = contentStart;
        while (p < contentEnd) {
            size_t lt2 = svg.find('<', p);
            if (lt2 == std::string::npos || lt2 >= contentEnd) {
                appendText(svg.substr(p, contentEnd - p));
                break;
            }
            if (lt2 > p) appendText(svg.substr(p, lt2 - p));
            size_t te2 = svg.find('>', lt2);
            if (te2 == std::string::npos || te2 >= contentEnd) break;
            std::string childTag = svg.substr(lt2, te2 - lt2 + 1);
            bool closing = lt2 + 1 < svg.size() && svg[lt2 + 1] == '/';
            std::string name = drawIoTagName(childTag);
            if (closing) {
                if (htmlStack.size() > 1) htmlStack.pop_back();
            } else if (name == "br") {
                lineBreak();
            } else if (!name.empty() && !drawIoVoidTag(name)) {
                DrawIoHtmlStyle st = htmlStack.back();
                drawIoApplyStyle(st, name, childTag);
                if (!drawIoSelfClosing(childTag)) htmlStack.push_back(std::move(st));
            }
            p = te2 + 1;
        }
        trimLineEnd();
        while (!lines.empty() && lines.back().runs.empty()) lines.pop_back();
        if (lines.empty()) return false;

        float totalHeight = 0.0f;
        for (auto& line : lines) {
            float topRel = std::numeric_limits<float>::max();
            float bottomRel = -std::numeric_limits<float>::max();
            float cssLineHeight = 0.0f;
            for (auto& run : line.runs) {
                if (!drawIoMeasureRun(run, &run.width, &run.height, &run.baseline))
                    continue;
                line.width += run.width;
                line.maxFontSize = std::max(line.maxFontSize, run.style.fontSize);
                cssLineHeight = std::max(cssLineHeight, run.style.lineHeight);
            }
            if (line.maxFontSize <= 0.0f) line.maxFontSize = base.fontSize;
            for (const auto& run : line.runs) {
                float shift = drawIoBaselineShift(run, line.maxFontSize);
                topRel = std::min(topRel, -run.baseline + shift);
                bottomRel = std::max(bottomRel, run.height - run.baseline + shift);
            }
            if (topRel == std::numeric_limits<float>::max()) {
                topRel = -line.maxFontSize * 0.8f;
                bottomRel = line.maxFontSize * 0.2f;
            }
            line.height = std::max(bottomRel - topRel, cssLineHeight);
            line.baselineFromTop = -topRel + std::max(0.0f, line.height - (bottomRel - topRel)) * 0.5f;
            totalHeight += line.height;
        }

        float anchorX = box.x;
        if (box.anchor == 1) anchorX = box.x + box.w * 0.5f;
        else if (box.anchor == 2) anchorX = box.x + box.w;
        float top = box.y + box.h * 0.5f - totalHeight * 0.5f;

        std::string paths;
        SvgFoBackground bg = foreignObjectBackground(contentStart, contentEnd);
        paths += buildRect(box.x, box.y, box.w, box.h, bg, xfStr);
        for (const auto& line : lines) {
            float x = anchorX;
            if (box.anchor == 1) x -= line.width * 0.5f;
            else if (box.anchor == 2) x -= line.width;
            float pen = 0.0f;
            for (const auto& run : line.runs) {
                if (run.text.empty() || run.width <= 0.0f) continue;
                float baseline = top + line.baselineFromTop +
                                 drawIoBaselineShift(run, line.maxFontSize);
                float adv = 0.0f;
                std::string d = renderRun(run.text, run.style.fontSize, run.style.families,
                                          run.style.weight, run.style.fstyle,
                                          x + pen, baseline, 0, false, 0.0f, &adv);
                if (!d.empty())
                    paths += buildPath(d, run.style.fill, opacityStr, fillOpacityStr, xfStr);
                pen += run.width > 0.0f ? run.width : adv;
            }
            top += line.height;
        }
        if (paths.empty()) return false;

        out.start = elemStart;
        out.end = elemEnd;
        size_t swOpen = svg.rfind("<switch", elemStart);
        if (swOpen != std::string::npos) {
            size_t prevClose = svg.rfind("</switch>", elemStart);
            if (prevClose == std::string::npos || prevClose < swOpen) {
                size_t swClose = svg.find("</switch>", elemEnd);
                if (swClose != std::string::npos) {
                    out.start = swOpen;
                    out.end = swClose + 9;
                }
            }
        }
        out.text = std::move(paths);
        return true;
    };

    std::vector<GFont> stack;
    GFont cur;
    std::vector<Repl> repls;

    size_t pos = 0;
    while (pos < svg.size()) {
        size_t lt = svg.find('<', pos);
        if (lt == std::string::npos) break;

        if (svg.compare(lt, 4, "</g>") == 0) {
            if (!stack.empty()) { cur = stack.back(); stack.pop_back(); }
            pos = lt + 4; continue;
        }
        if (svg.compare(lt, 3, "<g ") == 0 || svg.compare(lt, 3, "<g>") == 0 ||
            svg.compare(lt, 3, "<g\t") == 0 || svg.compare(lt, 3, "<g\n") == 0) {
            size_t te = svg.find('>', lt);
            if (te == std::string::npos) break;
            std::string tag = svg.substr(lt, te - lt + 1);
            stack.push_back(cur);
            applyFont(cur, tag, extractAttrFromTag(tag, "style"));
            if (te > 0 && svg[te-1] == '/') {            // 自闭合 <g/> 立即出栈
                if (!stack.empty()) { cur = stack.back(); stack.pop_back(); }
            }
            pos = te + 1; continue;
        }

        bool isText = svg.compare(lt, 5, "<text") == 0 &&
                      (svg[lt+5]==' '||svg[lt+5]=='>'||svg[lt+5]=='\t'||svg[lt+5]=='\n');
        bool isFO   = svg.compare(lt, 14, "<foreignObject") == 0;
        if (!isText && !isFO) { pos = lt + 1; continue; }

        size_t te = svg.find('>', lt);
        if (te == std::string::npos) break;
        std::string tag = svg.substr(lt, te - lt + 1);
        bool selfClose = (te > 0 && svg[te-1] == '/');
        const char* closeTag = isText ? "</text>" : "</foreignObject>";
        size_t closeLen = isText ? 7 : 16;
        size_t close = selfClose ? te : svg.find(closeTag, te);
        size_t contentEnd = (close == std::string::npos) ? svg.size() : close;
        size_t elemEnd    = (close == std::string::npos) ? svg.size() : close + closeLen;

        // ---- text 级属性 (tspan 缺省时继承) ----
        GFont ts = cur;
        std::string style = extractAttrFromTag(tag, "style");
        applyFont(ts, tag, style);
        float tFontSize = ts.fontSize > 0 ? ts.fontSize : 16.0f;
        const std::vector<std::wstring>& tFam =
            (ts.familySet && !ts.families.empty()) ? ts.families : cur.families;
        DWRITE_FONT_WEIGHT tWeight = ts.weightSet ? ts.weight : DWRITE_FONT_WEIGHT_NORMAL;
        DWRITE_FONT_STYLE  tStyle  = ts.styleSet  ? ts.fstyle : DWRITE_FONT_STYLE_NORMAL;
        std::string tFill = (ts.fillSet && !ts.fill.empty()) ? ts.fill : std::string("#000000");
        if (isFO) {
            std::string htmlColor = foreignObjectTextColor(te + 1, contentEnd);
            if (!htmlColor.empty()) tFill = std::move(htmlColor);
        }

        float tx = ParseSvgCssLength(extractAttrFromTag(tag, "x"), tFontSize, 0.0f);
        float ty = ParseSvgCssLength(extractAttrFromTag(tag, "y"), tFontSize, 0.0f);
        float foX = tx, foY = ty, foW = 0.0f, foH = 0.0f;
        SvgFoBackground foBg = isFO ? foreignObjectBackground(te + 1, contentEnd)
                                    : SvgFoBackground();
        std::string xfStr          = extractAttrFromTag(tag, "transform");
        std::string opacityStr     = extractAttrFromTag(tag, "opacity");
        std::string fillOpacityStr = extractAttrFromTag(tag, "fill-opacity");

        if (isFO) {
            Repl drawIoRepl{};
            if (tryRenderDrawIoForeignObject(tag, lt, elemEnd, te + 1, contentEnd,
                                             tFontSize, tFam, tWeight, tStyle, tFill,
                                             opacityStr, fillOpacityStr, xfStr,
                                             drawIoRepl)) {
                pos = drawIoRepl.end;
                repls.push_back(std::move(drawIoRepl));
                continue;
            }

            // Non-text draw.io fallbacks may intentionally rely on an image branch.
            if (shouldSkipForeignObject(tag, lt, elemEnd)) {
                pos = elemEnd;
                continue;
            }
        }
        pos = elemEnd;

        int  anchor = 0;
        bool block  = false;
        float maxW  = 0.0f;
        if (isFO) {
            block = true; anchor = 1;
            foW = ParseSvgCssLength(extractAttrFromTag(tag, "width"), tFontSize, 0.0f);
            foH = ParseSvgCssLength(extractAttrFromTag(tag, "height"), tFontSize, 0.0f);
            tx += foW / 2.0f; ty += foH / 2.0f;
            if (foW > 1.0f) maxW = foW;
        } else {
            std::string a = extractAttrFromTag(tag, "text-anchor");
            if (a.empty()) a = CssProp(style, "text-anchor");
            if (a == "middle") anchor = 1; else if (a == "end") anchor = 2;
        }

        // ---- 是否含 <tspan> 子元素 ----
        size_t firstTspan = selfClose ? std::string::npos : svg.find("<tspan", te);
        bool hasTspan = (firstTspan != std::string::npos && firstTspan < contentEnd);

        if (!hasTspan) {
            // 纯文本: 整段一次渲染
            std::wstring text = selfClose ? std::wstring()
                                          : ExtractInnerText(svg, te + 1, contentEnd);
            if (text.empty()) continue;
            std::string d = renderRun(text, tFontSize, tFam, tWeight, tStyle,
                                      tx, ty, anchor, block, maxW, nullptr);
            if (d.empty()) continue;
            std::string replacement;
            if (isFO) replacement += buildRect(foX, foY, foW, foH, foBg, xfStr);
            replacement += buildPath(d, tFill, opacityStr, fillOpacityStr, xfStr);
            repls.push_back({lt, elemEnd, std::move(replacement)});
            continue;
        }

        /* L122: 含 <tspan> —— 每个 tspan 当"带定位的子 run"。逐 tspan 用自带
         * x/y(绝对) + dx/dy(相对) 定位, 字体/style/fill 自带优先否则继承 text 级;
         * 文本按 fill 分组累积成 <path>(保留首见顺序), 原地内联保 z 序。
         * text-anchor 在多 tspan 场景按 start 处理(matplotlib 用法)。
         *
         * L292: tspan 可以嵌套 —— ProcessOn 思维导图导出的是
         *   <tspan x y><tspan dx="0">文字</tspan></tspan>
         * 旧实现找 </tspan> 不做深度配对, 外层拿到的"文本"把内层标记一起吃进去,
         * 于是 `<tspan dx="0">` 被当正文画出来, 每行凭空变宽、压住图框。现在改成
         * 递归遍历: 遇到子 tspan 就带着继承下来的字体/位置往下走。 */
        std::vector<std::pair<std::string,std::string>> byFill;   // fill -> 累积 d
        auto addD = [&](const std::string& fill, const std::string& d) {
            for (auto& kv : byFill) if (kv.first == fill) { kv.second += d; return; }
            byFill.push_back({fill, d});
        };
        float penX = tx, penY = ty;
        /* 锚点是 chunk 级的: 某个 tspan 写了绝对 x 就开一个新 chunk, 该 chunk 的
         * 第一段文字按 text-anchor 对齐, 之后的接续段一律 start。挂起状态要能跨
         * 嵌套传下去 —— x 在外层、文字在内层时不传就会漏掉 middle/end 对齐。 */
        bool anchorPending = false;
        int  pendingAnchor = 0;
        auto xmlSpacePreserve = [&](const std::string& elementTag, bool inherited) {
            std::string space = extractAttrFromTag(elementTag, "xml:space");
            if (space.empty()) space = extractAttrFromTag(elementTag, "space");
            if (space == "preserve") return true;
            if (space == "default") return false;
            return inherited;
        };
        const bool textPreserveSpace = xmlSpacePreserve(tag, false);

        // 一个文本节点 → 一段 run。空白节点(tspan 间的换行缩进)折叠后为空, 自动丢弃。
        auto emitRun = [&](const std::string& rawSlice, const GFont& st, bool preserve) {
            std::string decoded; AppendDecoded(decoded, rawSlice);
            std::wstring txt;
            if (preserve) {
                txt = Utf8ToWide(decoded);
            } else {
                std::string norm; bool sp = false, started = false;   // collapse whitespace + trim
                for (char c : decoded) {
                    if (c==' '||c=='\t'||c=='\n'||c=='\r') { if (started) sp = true; }
                    else { if (sp) { norm += ' '; sp = false; } norm += c; started = true; }
                }
                txt = Utf8ToWide(norm);
            }
            if (txt.empty()) return;
            float fs = st.fontSize > 0 ? st.fontSize : tFontSize;
            const std::vector<std::wstring>& fam =
                (st.familySet && !st.families.empty()) ? st.families : tFam;
            DWRITE_FONT_WEIGHT wt = st.weightSet ? st.weight : tWeight;
            DWRITE_FONT_STYLE  st2 = st.styleSet  ? st.fstyle : tStyle;
            std::string fill = (st.fillSet && !st.fill.empty()) ? st.fill : tFill;
            const int runAnchor = anchorPending ? pendingAnchor : 0;
            anchorPending = false;
            float adv = 0.0f;
            std::string d = renderRun(txt, fs, fam, wt, st2, penX, penY,
                                      runAnchor, false, 0, &adv);
            if (!d.empty()) addD(fill, d);
            penX += adv;                                // 无下个显式 x 时接续
        };

        // 深度配对的 </tspan>: 跳过嵌套子 tspan, 返回自身闭合标签的 '<' 位置。
        auto findTspanClose = [&](size_t from, size_t to) -> size_t {
            int depth = 1;
            size_t p = from;
            while (p < to) {
                size_t lt2 = svg.find('<', p);
                if (lt2 == std::string::npos || lt2 >= to) break;
                if (svg.compare(lt2, 8, "</tspan>") == 0) {
                    if (--depth == 0) return lt2;
                    p = lt2 + 8; continue;
                }
                if (svg.compare(lt2, 6, "<tspan") == 0) {
                    size_t g = svg.find('>', lt2);
                    if (g == std::string::npos || g >= to) break;
                    if (!(g > 0 && svg[g-1] == '/')) ++depth;
                    p = g + 1; continue;
                }
                p = lt2 + 1;
            }
            return std::string::npos;
        };

        auto walkTspans = [&](auto&& self, size_t from, size_t to,
                              const GFont& parentFont, bool parentPreserve) -> void {
            size_t p = from;
            while (p < to) {
                size_t lt2 = svg.find('<', p);
                if (lt2 == std::string::npos || lt2 >= to) {
                    emitRun(svg.substr(p, to - p), parentFont, parentPreserve);
                    return;
                }
                if (lt2 > p) emitRun(svg.substr(p, lt2 - p), parentFont, parentPreserve);
                size_t tgEnd = svg.find('>', lt2);
                if (tgEnd == std::string::npos || tgEnd >= to) return;
                const bool isTspan =
                    lt2 + 6 < svg.size() && svg.compare(lt2, 6, "<tspan") == 0 &&
                    (svg[lt2+6]==' ' || svg[lt2+6]=='>' || svg[lt2+6]=='/' ||
                     svg[lt2+6]=='\t' || svg[lt2+6]=='\n' || svg[lt2+6]=='\r');
                if (!isTspan) { p = tgEnd + 1; continue; }   // <title> 等: 跳过标签本身

                std::string ttag = svg.substr(lt2, tgEnd - lt2 + 1);
                const bool tSelf = (tgEnd > 0 && svg[tgEnd-1] == '/');

                GFont es = parentFont;                  // 继承外层 + 本 tspan 覆盖
                std::string tstyle = extractAttrFromTag(ttag, "style");
                applyFont(es, ttag, tstyle);
                const float fs = es.fontSize > 0 ? es.fontSize : tFontSize;

                std::string sx = extractAttrFromTag(ttag, "x"),  sy = extractAttrFromTag(ttag, "y");
                std::string sdx = extractAttrFromTag(ttag, "dx"), sdy = extractAttrFromTag(ttag, "dy");
                if (!sx.empty() || !sy.empty()) { anchorPending = true; pendingAnchor = anchor; }
                std::string ta = extractAttrFromTag(ttag, "text-anchor");
                if (ta.empty()) ta = CssProp(tstyle, "text-anchor");
                if (ta == "middle")     { anchorPending = true; pendingAnchor = 1; }
                else if (ta == "end")   { anchorPending = true; pendingAnchor = 2; }
                else if (ta == "start") { anchorPending = true; pendingAnchor = 0; }
                if (!sx.empty())  penX = ParseSvgCssLength(sx, fs, penX);
                if (!sy.empty())  penY = ParseSvgCssLength(sy, fs, penY);
                if (!sdx.empty()) penX += ParseSvgCssLength(sdx, fs, 0.0f);
                if (!sdy.empty()) penY += ParseSvgCssLength(sdy, fs, 0.0f);

                if (tSelf) { p = tgEnd + 1; continue; }
                const size_t close = findTspanClose(tgEnd + 1, to);
                self(self, tgEnd + 1, close == std::string::npos ? to : close, es,
                     xmlSpacePreserve(ttag, parentPreserve));
                p = (close == std::string::npos) ? to : close + 8;
            }
        };
        walkTspans(walkTspans, te + 1, contentEnd, ts, textPreserveSpace);
        if (byFill.empty()) continue;
        std::string allPaths;
        if (isFO) allPaths += buildRect(foX, foY, foW, foH, foBg, xfStr);
        for (auto& kv : byFill)
            allPaths += buildPath(kv.second, kv.first, opacityStr, fillOpacityStr, xfStr);
        repls.push_back({lt, elemEnd, std::move(allPaths)});
    }

    if (repls.empty()) return svg;
    std::string out = svg;
    for (auto it = repls.rbegin(); it != repls.rend(); ++it)   // 从后往前替换, 保持偏移有效
        out.replace(it->start, it->end - it->start, it->text);
    return out;
}

// L75: DirectWrite 渲染 SVG 文字 run. baseXf = SVG user-space → 屏幕 (跟形状同一个).
// L87: 按文字 bbox 建 SVG 渐变 brush. bx/by/bw/bh = 局部绘制空间的文字框,
// opacity 乘进每个 stop alpha. nullptr = 无 stop / 建失败 (调用方回退纯色).
static ComPtr<ID2D1Brush> MakeSvgTextGradientBrush(
        ID2D1RenderTarget* rt, const SvgTextGradient& g,
        float bx, float by, float bw, float bh, float opacity) {
    if (!rt || g.stops.empty()) return nullptr;

    std::vector<D2D1_GRADIENT_STOP> ds;
    ds.reserve(g.stops.size());
    for (const auto& s : g.stops) {
        D2D1_GRADIENT_STOP gs;
        gs.position = s.offset;
        gs.color = s.color;
        gs.color.a *= opacity;
        ds.push_back(gs);
    }
    ComPtr<ID2D1GradientStopCollection> coll;
    rt->CreateGradientStopCollection(ds.data(), (UINT32)ds.size(),
                                     D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP,
                                     coll.GetAddressOf());
    if (!coll) return nullptr;

    /* 渐变坐标 → 文字局部空间:
     *   objectBoundingBox(默认): 先过 gradientTransform, 再 scale(bw,bh)·translate(bx,by)
     *   userSpaceOnUse: 坐标已是用户空间(=文字局部), 仅过 gradientTransform */
    D2D1_MATRIX_3X2_F M = g.transform;
    if (!g.userSpace) {
        M = g.transform * D2D1::Matrix3x2F::Scale(bw, bh)
                        * D2D1::Matrix3x2F::Translation(bx, by);
    }
    auto P = [&](float x, float y) -> D2D1_POINT_2F {
        return { x * M._11 + y * M._21 + M._31,
                 x * M._12 + y * M._22 + M._32 };
    };

    if (!g.radial) {
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props = { P(g.x1, g.y1), P(g.x2, g.y2) };
        ComPtr<ID2D1LinearGradientBrush> b;
        rt->CreateLinearGradientBrush(props, coll.Get(), b.GetAddressOf());
        return b;
    }
    D2D1_POINT_2F c  = P(g.cx, g.cy);
    D2D1_POINT_2F ex = P(g.cx + g.r, g.cy);
    D2D1_POINT_2F ey = P(g.cx, g.cy + g.r);
    float rx = std::sqrt((ex.x-c.x)*(ex.x-c.x) + (ex.y-c.y)*(ex.y-c.y));
    float ry = std::sqrt((ey.x-c.x)*(ey.x-c.x) + (ey.y-c.y)*(ey.y-c.y));
    D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES props = { c, {0,0}, rx, ry };
    ComPtr<ID2D1RadialGradientBrush> b;
    rt->CreateRadialGradientBrush(props, coll.Get(), b.GetAddressOf());
    return b;
}

void Renderer::DrawSvgTextRuns(const std::vector<SvgTextRun>& runs,
                                const D2D1_MATRIX_3X2_F& baseXf) {
    if (auto* recorder = ActiveDisplayListRecorder()) {
        SvgTextRunListRef ref;
        ref.runs = runs;
        ref.base_transform = baseXf;
        recorder->DrawSvgTextRuns(std::move(ref));
    }
    if (runs.empty() || !ctx_ || !dwFactory_) return;

    D2D1_MATRIX_3X2_F saved;
    ctx_->GetTransform(&saved);

    for (const auto& run : runs) {
        if (run.text.empty()) continue;
        const wchar_t* fam = run.fontFamily.empty() ? theme::kFontFamily
                                                     : run.fontFamily.c_str();
        auto fmt = GetTextFormat(run.fontSize, fam, run.fontWeight);
        if (!fmt) continue;

        float maxW = run.maxWidth > 1.0f ? run.maxWidth : 100000.0f;
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(dwFactory_->CreateTextLayout(
                run.text.c_str(), (UINT32)run.text.size(),
                fmt.Get(), maxW, 100000.0f, &layout)) || !layout)
            continue;
        layout->SetWordWrapping(run.maxWidth > 1.0f ? DWRITE_WORD_WRAPPING_WRAP
                                                    : DWRITE_WORD_WRAPPING_NO_WRAP);

        DWRITE_TEXT_METRICS tm{};
        layout->GetMetrics(&tm);

        /* 多行 / 居中 / 右对齐: 把 layout 收窄到自然宽 + 设对齐, 让多行在块内
         * 各自对齐 (否则 100000 宽框里 center 会跑偏). */
        if (run.block || run.anchor != 0) {
            layout->SetMaxWidth(tm.width + 1.0f);
            layout->SetTextAlignment(run.anchor == 2 ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                     : DWRITE_TEXT_ALIGNMENT_CENTER);
        }

        float ox = run.x, oy = run.y;
        if (run.block) {
            // foreignObject: 文字块围绕 (x,y)+transform 原点 水平 + 垂直居中.
            ox = run.x - tm.width  / 2.0f;
            oy = run.y - tm.height / 2.0f;
        } else {
            // <text>: SVG 的 y 是 baseline, DirectWrite 原点是 top → 上移一个 baseline.
            if (run.anchor == 1)      ox = run.x - tm.width / 2.0f;
            else if (run.anchor == 2) ox = run.x - tm.width;
            DWRITE_LINE_METRICS lm{}; UINT32 lineCnt = 0;
            if (SUCCEEDED(layout->GetLineMetrics(&lm, 1, &lineCnt)) && lineCnt >= 1)
                oy = run.y - lm.baseline;
        }

        ctx_->SetTransform(run.transform * baseXf);
        ComPtr<ID2D1Brush> brush;
        if (run.hasGradient) {
            brush = MakeSvgTextGradientBrush(ctx_.Get(), run.gradient,
                                             ox, oy, tm.width, tm.height, run.opacity);
        }
        if (!brush) {
            D2D1_COLOR_F c = run.color;
            c.a *= run.opacity;           // L87: 继承 opacity 乘进纯色 alpha
            brush = GetBrush(c);
        }
        if (brush) ctx_->DrawTextLayout(D2D1::Point2F(ox, oy), layout.Get(), brush.Get());
    }

    ctx_->SetTransform(saved);
}

ID2D1StrokeStyle* Renderer::GetRoundStrokeStyle() {
    if (roundStrokeStyle_) return roundStrokeStyle_.Get();
    if (!factory_) return nullptr;
    D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
        D2D1_LINE_JOIN_ROUND, 10.0f, D2D1_DASH_STYLE_SOLID, 0.0f);
    factory_->CreateStrokeStyle(props, nullptr, 0, roundStrokeStyle_.GetAddressOf());
    return roundStrokeStyle_.Get();
}

} // namespace ui
