/**
 * D2D 位图内存测试
 *
 * 对比三种位图创建方式的内存占用：
 *   A) D2D 1.0: ID2D1RenderTarget::CreateBitmap(D2D1_BITMAP_PROPERTIES)
 *   B) D2D 1.1: ID2D1DeviceContext::CreateBitmap(D2D1_BITMAP_PROPERTIES)  ← 旧 API 签名
 *   C) D2D 1.1: ID2D1DeviceContext::CreateBitmap(D2D1_BITMAP_PROPERTIES1) ← 新 API 签名
 *
 * 预期：A 和 B 都会有 CPU 副本（双倍内存），C 才是真正 GPU-only
 */

#include <windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <psapi.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

using Microsoft::WRL::ComPtr;

static const wchar_t* IMAGE_PATH = L"D:\\download\\\x6d4b\x8bd5\x56fe\x7247\\\x4e16\x754c\x5730\x56fe.JPG";

struct MemSnapshot {
    SIZE_T workingSet;
    SIZE_T privateBytes;
};

static MemSnapshot GetMem() {
    PROCESS_MEMORY_COUNTERS_EX pmc = {};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
    return { pmc.WorkingSetSize, pmc.PrivateUsage };
}

static void PrintMem(const char* label, MemSnapshot before, MemSnapshot after) {
    double wsDelta  = (double)(after.workingSet  - before.workingSet)  / (1024.0 * 1024.0);
    double pvDelta  = (double)(after.privateBytes - before.privateBytes) / (1024.0 * 1024.0);
    printf("  [%s] WorkingSet: +%.1f MB, PrivateBytes: +%.1f MB\n", label, wsDelta, pvDelta);
}

/* 用 WIC 解码图片到内存 buffer */
static bool DecodeImage(IWICImagingFactory* wic, const wchar_t* path,
                         std::vector<uint8_t>& pixels, UINT& w, UINT& h) {
    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = wic->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
                                                 WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
    if (FAILED(hr)) { printf("Failed to decode image: 0x%lx\n", hr); return false; }

    ComPtr<IWICBitmapFrameDecode> frame;
    decoder->GetFrame(0, frame.GetAddressOf());

    ComPtr<IWICFormatConverter> conv;
    wic->CreateFormatConverter(conv.GetAddressOf());
    conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                     WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeMedianCut);
    conv->GetSize(&w, &h);

    UINT stride = w * 4;
    pixels.resize((size_t)stride * h);
    WICRect rc = { 0, 0, (INT)w, (INT)h };
    conv->CopyPixels(&rc, stride, (UINT)pixels.size(), pixels.data());

    printf("Image: %u x %u (%.1f MB raw)\n\n", w, h, (double)(stride * h) / (1024.0 * 1024.0));
    return true;
}

/* ========== 测试 A: D2D 1.0 HwndRenderTarget ========== */
static void TestA_D2D10(HWND hwnd, IWICImagingFactory* wic,
                         const uint8_t* pixels, UINT w, UINT h) {
    printf("=== Test A: D2D 1.0 ID2D1HwndRenderTarget::CreateBitmap ===\n");

    ComPtr<ID2D1Factory> factory;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf());

    RECT rc;
    GetClientRect(hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right, rc.bottom);

    ComPtr<ID2D1HwndRenderTarget> rt;
    factory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_HARDWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
        D2D1::HwndRenderTargetProperties(hwnd, size),
        rt.GetAddressOf());
    if (!rt) {
        factory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(hwnd, size),
            rt.GetAddressOf());
    }
    if (!rt) { printf("  FAILED to create RT\n"); return; }

    auto before = GetMem();

    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ComPtr<ID2D1Bitmap> bitmap;
    HRESULT hr = rt->CreateBitmap(D2D1::SizeU(w, h), pixels, w * 4, props, bitmap.GetAddressOf());
    if (FAILED(hr)) { printf("  CreateBitmap FAILED: 0x%lx\n", hr); return; }

    auto after = GetMem();
    PrintMem("D2D 1.0 bitmap", before, after);

    bitmap.Reset();
    rt.Reset();
    factory.Reset();
    printf("\n");
}

/* ========== 测试 B: D2D 1.1 DeviceContext + 旧 API (D2D1_BITMAP_PROPERTIES) ========== */
static void TestB_D2D11_OldAPI(HWND hwnd, const uint8_t* pixels, UINT w, UINT h,
                                 ID2D1Factory1* factory1) {
    printf("=== Test B: D2D 1.1 DeviceContext + OLD CreateBitmap(D2D1_BITMAP_PROPERTIES) ===\n");

    /* 创建 D3D11 + DeviceContext */
    ComPtr<ID3D11Device> d3d;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                       levels, 2, D3D11_SDK_VERSION, d3d.GetAddressOf(), nullptr, nullptr);
    if (!d3d) {
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                           levels, 2, D3D11_SDK_VERSION, d3d.GetAddressOf(), nullptr, nullptr);
    }

    ComPtr<IDXGIDevice> dxgi;
    d3d.As(&dxgi);
    ComPtr<ID2D1Device> d2dDev;
    factory1->CreateDevice(dxgi.Get(), d2dDev.GetAddressOf());
    ComPtr<ID2D1DeviceContext> ctx;
    d2dDev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, ctx.GetAddressOf());

    /* 创建 swapchain 并绑定 target */
    ComPtr<IDXGIAdapter> adapter;
    dxgi->GetAdapter(adapter.GetAddressOf());
    ComPtr<IDXGIFactory2> dxgiFactory;
    adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(dxgiFactory.GetAddressOf()));

    RECT rc; GetClientRect(hwnd, &rc);
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = rc.right; desc.Height = rc.bottom;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

    ComPtr<IDXGISwapChain1> swap;
    dxgiFactory->CreateSwapChainForHwnd(d3d.Get(), hwnd, &desc, nullptr, nullptr, swap.GetAddressOf());

    ComPtr<IDXGISurface> surface;
    swap->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(surface.GetAddressOf()));
    D2D1_BITMAP_PROPERTIES1 tbp = {};
    tbp.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    tbp.dpiX = tbp.dpiY = 96.0f;
    tbp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    ComPtr<ID2D1Bitmap1> target;
    ctx->CreateBitmapFromDxgiSurface(surface.Get(), tbp, target.GetAddressOf());
    ctx->SetTarget(target.Get());

    /* 用 旧版 API 创建位图 (D2D1_BITMAP_PROPERTIES → ID2D1Bitmap) */
    auto before = GetMem();

    D2D1_BITMAP_PROPERTIES oldProps = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ComPtr<ID2D1Bitmap> bitmap;
    HRESULT hr = ctx->CreateBitmap(D2D1::SizeU(w, h), pixels, w * 4, oldProps, bitmap.GetAddressOf());
    if (FAILED(hr)) { printf("  CreateBitmap (old) FAILED: 0x%lx\n", hr); return; }

    auto after = GetMem();
    PrintMem("D2D 1.1 + old API", before, after);

    bitmap.Reset();
    printf("\n");
}

/* ========== 测试 C: D2D 1.1 DeviceContext + 新 API (D2D1_BITMAP_PROPERTIES1) ========== */
static void TestC_D2D11_NewAPI(HWND hwnd, const uint8_t* pixels, UINT w, UINT h,
                                 ID2D1Factory1* factory1) {
    printf("=== Test C: D2D 1.1 DeviceContext + NEW CreateBitmap(D2D1_BITMAP_PROPERTIES1) ===\n");

    ComPtr<ID3D11Device> d3d;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                       levels, 2, D3D11_SDK_VERSION, d3d.GetAddressOf(), nullptr, nullptr);
    if (!d3d) {
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                           levels, 2, D3D11_SDK_VERSION, d3d.GetAddressOf(), nullptr, nullptr);
    }

    ComPtr<IDXGIDevice> dxgi;
    d3d.As(&dxgi);
    ComPtr<ID2D1Device> d2dDev;
    factory1->CreateDevice(dxgi.Get(), d2dDev.GetAddressOf());
    ComPtr<ID2D1DeviceContext> ctx;
    d2dDev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, ctx.GetAddressOf());

    ComPtr<IDXGIAdapter> adapter;
    dxgi->GetAdapter(adapter.GetAddressOf());
    ComPtr<IDXGIFactory2> dxgiFactory;
    adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(dxgiFactory.GetAddressOf()));

    RECT rc; GetClientRect(hwnd, &rc);
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = rc.right; desc.Height = rc.bottom;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

    ComPtr<IDXGISwapChain1> swap;
    dxgiFactory->CreateSwapChainForHwnd(d3d.Get(), hwnd, &desc, nullptr, nullptr, swap.GetAddressOf());

    ComPtr<IDXGISurface> surface;
    swap->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(surface.GetAddressOf()));
    D2D1_BITMAP_PROPERTIES1 tbp = {};
    tbp.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    tbp.dpiX = tbp.dpiY = 96.0f;
    tbp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    ComPtr<ID2D1Bitmap1> target;
    ctx->CreateBitmapFromDxgiSurface(surface.Get(), tbp, target.GetAddressOf());
    ctx->SetTarget(target.Get());

    /* 用 新版 API 创建位图 (D2D1_BITMAP_PROPERTIES1 → ID2D1Bitmap1, GPU-only) */
    auto before = GetMem();

    D2D1_BITMAP_PROPERTIES1 newProps = {};
    newProps.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    newProps.dpiX = newProps.dpiY = 96.0f;
    newProps.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;  /* GPU-only, 不保留 CPU 副本 */

    ComPtr<ID2D1Bitmap1> bitmap1;
    HRESULT hr = ctx->CreateBitmap(D2D1::SizeU(w, h), pixels, w * 4, newProps, bitmap1.GetAddressOf());
    if (FAILED(hr)) { printf("  CreateBitmap (new) FAILED: 0x%lx\n", hr); return; }

    auto after = GetMem();
    PrintMem("D2D 1.1 + new API (GPU-only)", before, after);

    bitmap1.Reset();
    printf("\n");
}

/* ========== 测试 D: D2D 1.1 + 空位图 + CopyFromMemory (当前 renderer 实际路径) ========== */
static void TestD_D2D11_EmptyThenCopy(HWND hwnd, const uint8_t* pixels, UINT w, UINT h,
                                        ID2D1Factory1* factory1) {
    printf("=== Test D: D2D 1.1 CreateEmptyBitmap(old API) + CopyFromMemory ===\n");

    ComPtr<ID3D11Device> d3d;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                       levels, 2, D3D11_SDK_VERSION, d3d.GetAddressOf(), nullptr, nullptr);

    ComPtr<IDXGIDevice> dxgi;
    d3d.As(&dxgi);
    ComPtr<ID2D1Device> d2dDev;
    factory1->CreateDevice(dxgi.Get(), d2dDev.GetAddressOf());
    ComPtr<ID2D1DeviceContext> ctx;
    d2dDev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, ctx.GetAddressOf());

    ComPtr<IDXGIAdapter> adapter;
    dxgi->GetAdapter(adapter.GetAddressOf());
    ComPtr<IDXGIFactory2> dxgiFactory;
    adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(dxgiFactory.GetAddressOf()));

    RECT rc; GetClientRect(hwnd, &rc);
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = rc.right; desc.Height = rc.bottom;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

    ComPtr<IDXGISwapChain1> swap;
    dxgiFactory->CreateSwapChainForHwnd(d3d.Get(), hwnd, &desc, nullptr, nullptr, swap.GetAddressOf());

    ComPtr<IDXGISurface> surface;
    swap->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(surface.GetAddressOf()));
    D2D1_BITMAP_PROPERTIES1 tbp = {};
    tbp.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    tbp.dpiX = tbp.dpiY = 96.0f;
    tbp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    ComPtr<ID2D1Bitmap1> target;
    ctx->CreateBitmapFromDxgiSurface(surface.Get(), tbp, target.GetAddressOf());
    ctx->SetTarget(target.Get());

    /* 模拟当前 renderer.cpp 的 CreateEmptyBitmap: 用旧 API 创建空位图 */
    auto before = GetMem();

    D2D1_BITMAP_PROPERTIES oldProps = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ComPtr<ID2D1Bitmap> bitmap;
    ctx->CreateBitmap(D2D1::SizeU(w, h), nullptr, 0, oldProps, bitmap.GetAddressOf());

    /* CopyFromMemory 上传像素 */
    D2D1_RECT_U destRect = { 0, 0, w, h };
    bitmap->CopyFromMemory(&destRect, pixels, w * 4);

    auto after = GetMem();
    PrintMem("Empty(old) + CopyFromMemory", before, after);

    bitmap.Reset();
    printf("\n");
}

/* ========== 测试 E: D2D 1.1 + 空位图(新 API) + CopyFromMemory ========== */
static void TestE_D2D11_NewEmpty_ThenCopy(HWND hwnd, const uint8_t* pixels, UINT w, UINT h,
                                            ID2D1Factory1* factory1) {
    printf("=== Test E: D2D 1.1 CreateEmptyBitmap(NEW API, GPU-only) + CopyFromMemory ===\n");

    ComPtr<ID3D11Device> d3d;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                       levels, 2, D3D11_SDK_VERSION, d3d.GetAddressOf(), nullptr, nullptr);

    ComPtr<IDXGIDevice> dxgi;
    d3d.As(&dxgi);
    ComPtr<ID2D1Device> d2dDev;
    factory1->CreateDevice(dxgi.Get(), d2dDev.GetAddressOf());
    ComPtr<ID2D1DeviceContext> ctx;
    d2dDev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, ctx.GetAddressOf());

    ComPtr<IDXGIAdapter> adapter;
    dxgi->GetAdapter(adapter.GetAddressOf());
    ComPtr<IDXGIFactory2> dxgiFactory;
    adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(dxgiFactory.GetAddressOf()));

    RECT rc; GetClientRect(hwnd, &rc);
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = rc.right; desc.Height = rc.bottom;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

    ComPtr<IDXGISwapChain1> swap;
    dxgiFactory->CreateSwapChainForHwnd(d3d.Get(), hwnd, &desc, nullptr, nullptr, swap.GetAddressOf());

    ComPtr<IDXGISurface> surface;
    swap->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(surface.GetAddressOf()));
    D2D1_BITMAP_PROPERTIES1 tbp = {};
    tbp.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    tbp.dpiX = tbp.dpiY = 96.0f;
    tbp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    ComPtr<ID2D1Bitmap1> target;
    ctx->CreateBitmapFromDxgiSurface(surface.Get(), tbp, target.GetAddressOf());
    ctx->SetTarget(target.Get());

    /* 用 新版 API 创建空位图 */
    auto before = GetMem();

    D2D1_BITMAP_PROPERTIES1 newProps = {};
    newProps.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    newProps.dpiX = newProps.dpiY = 96.0f;
    newProps.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ;  /* 允许 CopyFromMemory */

    ComPtr<ID2D1Bitmap1> bitmap1;
    HRESULT hr = ctx->CreateBitmap(D2D1::SizeU(w, h), nullptr, 0, newProps, bitmap1.GetAddressOf());
    if (FAILED(hr)) {
        /* CPU_READ 可能需要特殊 usage，退回 NONE */
        newProps.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;
        hr = ctx->CreateBitmap(D2D1::SizeU(w, h), nullptr, 0, newProps, bitmap1.GetAddressOf());
        if (FAILED(hr)) { printf("  CreateBitmap1 FAILED: 0x%lx\n", hr); return; }
        printf("  (fallback to BITMAP_OPTIONS_NONE)\n");
    }

    D2D1_RECT_U destRect = { 0, 0, w, h };
    hr = bitmap1->CopyFromMemory(&destRect, pixels, w * 4);
    if (FAILED(hr)) { printf("  CopyFromMemory FAILED: 0x%lx\n", hr); return; }

    auto after = GetMem();
    PrintMem("Empty(new, GPU-only) + CopyFromMemory", before, after);

    bitmap1.Reset();
    printf("\n");
}

/* ========== Main ========== */

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    /* 输出到文件 */
    FILE* f;
    freopen_s(&f, "E:\\code\\c\\core-ui\\test\\build\\result.txt", "w", stdout);
    freopen_s(&f, "E:\\code\\c\\core-ui\\test\\build\\result.txt", "a", stderr);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    printf("====== D2D Bitmap Memory Test ======\n\n");

    /* 创建隐藏窗口 */
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"BitmapMemTest";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(L"BitmapMemTest", L"Test", WS_OVERLAPPEDWINDOW,
                               0, 0, 800, 600, nullptr, nullptr, hInst, nullptr);

    /* WIC + 解码图片 */
    ComPtr<IWICImagingFactory> wic;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                      IID_PPV_ARGS(wic.GetAddressOf()));

    std::vector<uint8_t> pixels;
    UINT imgW = 0, imgH = 0;
    if (!DecodeImage(wic.Get(), IMAGE_PATH, pixels, imgW, imgH)) {
        printf("FAILED to decode image!\n");
        printf("Path: D:\\download\\测试图片\\世界地图.JPG\n");
        fflush(stdout);
        return 1;
    }

    /* 创建 ID2D1Factory1 共享 */
    ComPtr<ID2D1Factory1> factory1;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                       __uuidof(ID2D1Factory1),
                       reinterpret_cast<void**>(factory1.GetAddressOf()));

    auto baseline = GetMem();
    printf("Baseline: WorkingSet=%.1f MB, PrivateBytes=%.1f MB\n\n",
           baseline.workingSet / (1024.0*1024.0), baseline.privateBytes / (1024.0*1024.0));

    /* 释放像素缓冲区前的快照 */
    printf("(pixel buffer in RAM = %.1f MB)\n\n", pixels.size() / (1024.0*1024.0));

    /* 运行测试 */
    TestA_D2D10(hwnd, wic.Get(), pixels.data(), imgW, imgH);

    Sleep(500); /* 让 GPU 稳定 */

    TestB_D2D11_OldAPI(hwnd, pixels.data(), imgW, imgH, factory1.Get());

    Sleep(500);

    TestC_D2D11_NewAPI(hwnd, pixels.data(), imgW, imgH, factory1.Get());

    Sleep(500);

    TestD_D2D11_EmptyThenCopy(hwnd, pixels.data(), imgW, imgH, factory1.Get());

    Sleep(500);

    TestE_D2D11_NewEmpty_ThenCopy(hwnd, pixels.data(), imgW, imgH, factory1.Get());

    printf("====== Test Complete ======\n");
    fflush(stdout);

    DestroyWindow(hwnd);
    CoUninitialize();
    return 0;
}
