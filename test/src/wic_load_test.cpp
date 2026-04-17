/**
 * WIC 加载内存测试
 *
 * 模拟 guohe-view 的实际加载路径：
 *   1. D2D 1.1 DeviceContext + SwapChain
 *   2. CreateEmptyBitmap (D2D1_BITMAP_PROPERTIES1, GPU-only)
 *   3. WIC 条带解码 + CopyFromMemory 上传
 *
 * 预期峰值：D2D bitmap (~270MB) + 1 strip buffer (~20MB) ≈ 290MB
 */

#include <windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <psapi.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <memory>

using Microsoft::WRL::ComPtr;

static const wchar_t* IMAGE_PATH = L"D:\\download\\\x6d4b\x8bd5\x56fe\x7247\\\x4e16\x754c\x5730\x56fe.JPG";
static const char* LOG_PATH = "E:\\code\\c\\core-ui\\test\\build\\wic_result.txt";

static FILE* g_log = nullptr;

static void logMem(const char* label) {
    PROCESS_MEMORY_COUNTERS_EX pmc = {};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
    fprintf(g_log, "  %-45s WS=%4zuMB  Private=%4zuMB\n", label,
            pmc.WorkingSetSize / (1024*1024), pmc.PrivateUsage / (1024*1024));
    fflush(g_log);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    g_log = fopen(LOG_PATH, "w");
    if (!g_log) return 1;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    fprintf(g_log, "====== WIC Load Memory Test ======\n\n");

    /* 创建窗口 */
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"WicLoadTest";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(L"WicLoadTest", L"Test", WS_OVERLAPPEDWINDOW,
                               0, 0, 800, 600, nullptr, nullptr, hInst, nullptr);

    logMem("0. process start");

    /* ====== 创建 D2D 1.1 DeviceContext ====== */
    ComPtr<ID2D1Factory1> factory1;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                       __uuidof(ID2D1Factory1),
                       reinterpret_cast<void**>(factory1.GetAddressOf()));

    ComPtr<ID3D11Device> d3d;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                       levels, 2, D3D11_SDK_VERSION, d3d.GetAddressOf(), nullptr, nullptr);

    ComPtr<IDXGIDevice> dxgiDev;
    d3d.As(&dxgiDev);
    ComPtr<ID2D1Device> d2dDev;
    factory1->CreateDevice(dxgiDev.Get(), d2dDev.GetAddressOf());
    ComPtr<ID2D1DeviceContext> ctx;
    d2dDev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, ctx.GetAddressOf());

    /* SwapChain */
    ComPtr<IDXGIAdapter> adapter;
    dxgiDev->GetAdapter(adapter.GetAddressOf());
    ComPtr<IDXGIFactory2> dxgiFactory;
    adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(dxgiFactory.GetAddressOf()));

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = 800; desc.Height = 600;
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

    logMem("1. D2D 1.1 context ready");

    /* ====== WIC 打开图片 ====== */
    ComPtr<IWICImagingFactory> wic;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                      IID_PPV_ARGS(wic.GetAddressOf()));

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = wic->CreateDecoderFromFilename(IMAGE_PATH, nullptr, GENERIC_READ,
                                                 WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
    if (FAILED(hr)) {
        fprintf(g_log, "FAILED to open image: 0x%lx\n", hr);
        fclose(g_log);
        return 1;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    decoder->GetFrame(0, frame.GetAddressOf());

    ComPtr<IWICFormatConverter> converter;
    wic->CreateFormatConverter(converter.GetAddressOf());
    converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                          WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeMedianCut);

    UINT imgW = 0, imgH = 0;
    converter->GetSize(&imgW, &imgH);
    fprintf(g_log, "Image: %u x %u (%.1f MB raw BGRA)\n\n",
            imgW, imgH, (double)(imgW * imgH * 4) / (1024.0*1024.0));

    logMem("2. WIC decoder opened");

    /* ====== 创建空 D2D 位图 (D2D1_BITMAP_PROPERTIES1, GPU-only) ====== */
    D2D1_BITMAP_PROPERTIES1 bmpProps = {};
    bmpProps.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    bmpProps.dpiX = bmpProps.dpiY = 96.0f;
    bmpProps.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;

    ComPtr<ID2D1Bitmap1> bitmap;
    hr = ctx->CreateBitmap(D2D1::SizeU(imgW, imgH), nullptr, 0, bmpProps, bitmap.GetAddressOf());
    if (FAILED(hr)) {
        fprintf(g_log, "CreateBitmap FAILED: 0x%lx\n", hr);
        fclose(g_log);
        return 1;
    }

    logMem("3. empty D2D bitmap created");

    /* ====== 条带解码 + 上传 ====== */
    const UINT stripH = 512;
    const UINT stride = imgW * 4;
    auto stripBuf = std::make_unique<uint8_t[]>((size_t)stride * stripH);

    logMem("4. strip buffer allocated (20MB)");

    int lastPct = 0;
    for (UINT y = 0; y < imgH; y += stripH) {
        UINT rows = (y + stripH <= imgH) ? stripH : (imgH - y);
        WICRect rc = { 0, (INT)y, (INT)imgW, (INT)rows };
        converter->CopyPixels(&rc, stride, stride * rows, stripBuf.get());

        D2D1_RECT_U dest = { 0, y, imgW, y + rows };
        bitmap->CopyFromMemory(&dest, stripBuf.get(), stride);

        int pct = ((y + rows) * 100) / imgH;
        int bucket = (pct / 20) * 20;
        if (bucket > lastPct && bucket <= 80) {
            lastPct = bucket;
            char buf[64];
            snprintf(buf, sizeof(buf), "5. decode+upload %d%%", bucket);
            logMem(buf);
        }
    }

    logMem("6. decode+upload 100% complete");

    /* ====== 释放 WIC 资源 ====== */
    stripBuf.reset();
    converter.Reset();
    frame.Reset();
    decoder.Reset();

    logMem("7. WIC resources released");

    HeapCompact(GetProcessHeap(), 0);

    logMem("8. HeapCompact done (final)");

    fprintf(g_log, "\n====== Test Complete ======\n");
    fclose(g_log);

    DestroyWindow(hwnd);
    CoUninitialize();
    return 0;
}
