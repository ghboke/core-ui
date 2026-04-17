/**
 * 测试 HeapOptimizeResources 能否回收 WIC 解码残留内存
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
#include <memory>

using Microsoft::WRL::ComPtr;

static const wchar_t* IMAGE_PATH = L"D:\\download\\\x6d4b\x8bd5\x56fe\x7247\\\x4e16\x754c\x5730\x56fe.JPG";
static const char* LOG_PATH = "E:\\code\\c\\core-ui\\test\\build\\heap_opt_result.txt";
static FILE* g_log = nullptr;

/* HeapOptimizeResources: Windows 8.1+ (value 3) - 强制堆归还空闲页 */
#ifndef HeapOptimizeResources
#define HeapOptimizeResources ((HEAP_INFORMATION_CLASS)3)
#endif

typedef struct MY_HEAP_OPTIMIZE_RESOURCES_INFORMATION {
    DWORD Version;
    DWORD Flags;
} MY_HEAP_OPTIMIZE_RESOURCES_INFORMATION;

static void logMem(const char* label) {
    PROCESS_MEMORY_COUNTERS_EX pmc = {};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
    fprintf(g_log, "  %-50s WS=%4zuMB  Private=%4zuMB\n", label,
            pmc.WorkingSetSize / (1024*1024), pmc.PrivateUsage / (1024*1024));
    fflush(g_log);
}

static void tryHeapOptimize() {
    /* 尝试所有进程堆 */
    HANDLE heaps[64];
    DWORD count = GetProcessHeaps(64, heaps);
    for (DWORD i = 0; i < count; i++) {
        HeapCompact(heaps[i], 0);
    }

    MY_HEAP_OPTIMIZE_RESOURCES_INFORMATION info = {};
    info.Version = 1;
    info.Flags = 0;

    /* 对默认进程堆调用 */
    BOOL ok = HeapSetInformation(GetProcessHeap(), HeapOptimizeResources, &info, sizeof(info));
    fprintf(g_log, "  HeapOptimizeResources(processHeap): %s\n", ok ? "OK" : "FAILED");

    /* 对所有堆调用 */
    for (DWORD i = 0; i < count; i++) {
        HeapSetInformation(heaps[i], HeapOptimizeResources, &info, sizeof(info));
    }

    /* NULL = 所有堆 */
    ok = HeapSetInformation(NULL, HeapOptimizeResources, &info, sizeof(info));
    fprintf(g_log, "  HeapOptimizeResources(NULL): %s\n", ok ? "OK" : "FAILED");
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

    WNDCLASSW wc = {}; wc.lpfnWndProc = WndProc; wc.hInstance = hInst; wc.lpszClassName = L"HOT";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(L"HOT", L"T", WS_OVERLAPPEDWINDOW, 0,0,800,600, nullptr,nullptr,hInst,nullptr);

    fprintf(g_log, "====== Heap Optimize Test ======\n\n");
    logMem("0. start");

    /* D2D 1.1 setup */
    ComPtr<ID2D1Factory1> factory;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1),
                       reinterpret_cast<void**>(factory.GetAddressOf()));
    ComPtr<ID3D11Device> d3d;
    D3D_FEATURE_LEVEL lvl[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                       D3D11_CREATE_DEVICE_BGRA_SUPPORT, lvl, 1, D3D11_SDK_VERSION,
                       d3d.GetAddressOf(), nullptr, nullptr);
    ComPtr<IDXGIDevice> dxgi; d3d.As(&dxgi);
    ComPtr<ID2D1Device> d2dDev; factory->CreateDevice(dxgi.Get(), d2dDev.GetAddressOf());
    ComPtr<ID2D1DeviceContext> ctx;
    d2dDev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, ctx.GetAddressOf());

    ComPtr<IDXGIAdapter> adapter; dxgi->GetAdapter(adapter.GetAddressOf());
    ComPtr<IDXGIFactory2> dxgiF;
    adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(dxgiF.GetAddressOf()));
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width=800; desc.Height=600; desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count=1; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount=2; desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    ComPtr<IDXGISwapChain1> swap;
    dxgiF->CreateSwapChainForHwnd(d3d.Get(), hwnd, &desc, nullptr, nullptr, swap.GetAddressOf());
    ComPtr<IDXGISurface> surface;
    swap->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(surface.GetAddressOf()));
    D2D1_BITMAP_PROPERTIES1 tbp = {};
    tbp.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    tbp.dpiX = tbp.dpiY = 96.0f;
    tbp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    ComPtr<ID2D1Bitmap1> target;
    ctx->CreateBitmapFromDxgiSurface(surface.Get(), tbp, target.GetAddressOf());
    ctx->SetTarget(target.Get());

    logMem("1. D2D ready");

    /* WIC decode + strip upload */
    ComPtr<IWICImagingFactory> wic;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()));

    ComPtr<IWICBitmapDecoder> dec;
    wic->CreateDecoderFromFilename(IMAGE_PATH, nullptr, GENERIC_READ,
                                    WICDecodeMetadataCacheOnLoad, dec.GetAddressOf());
    ComPtr<IWICBitmapFrameDecode> frame; dec->GetFrame(0, frame.GetAddressOf());
    ComPtr<IWICFormatConverter> conv; wic->CreateFormatConverter(conv.GetAddressOf());
    conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                     WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeMedianCut);
    UINT w, h; conv->GetSize(&w, &h);
    fprintf(g_log, "  Image: %u x %u\n", w, h);

    /* GPU-only bitmap */
    D2D1_BITMAP_PROPERTIES1 bp = {};
    bp.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    bp.dpiX = bp.dpiY = 96.0f; bp.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;
    ComPtr<ID2D1Bitmap1> bmp;
    ctx->CreateBitmap(D2D1::SizeU(w, h), nullptr, 0, bp, bmp.GetAddressOf());

    logMem("2. bitmap created");

    /* strip decode */
    const UINT stripH = 512, stride = w * 4;
    size_t stripBytes = (size_t)stride * stripH;
    auto stripBuf = std::make_unique<uint8_t[]>(stripBytes);
    for (UINT y = 0; y < h; y += stripH) {
        UINT rows = (y + stripH <= h) ? stripH : (h - y);
        WICRect rc = { 0, (INT)y, (INT)w, (INT)rows };
        conv->CopyPixels(&rc, stride, stride * rows, stripBuf.get());
        D2D1_RECT_U dst = { 0, y, w, y + rows };
        bmp->CopyFromMemory(&dst, stripBuf.get(), stride);
    }

    logMem("3. decode+upload done");

    /* 释放 WIC */
    stripBuf.reset();
    conv.Reset(); frame.Reset(); dec.Reset();

    logMem("4. WIC released");

    /* 尝试各种回收手段 */
    HeapCompact(GetProcessHeap(), 0);
    logMem("5. HeapCompact");

    tryHeapOptimize();
    logMem("6. HeapOptimizeResources");

    /* EmptyWorkingSet */
    EmptyWorkingSet(GetCurrentProcess());
    logMem("7. EmptyWorkingSet");

    /* OfferVirtualMemory 需要知道地址... 跳过 */

    /* 等待一下让系统稳定 */
    Sleep(2000);
    logMem("8. after 2s sleep");

    fprintf(g_log, "\n  Bitmap alive, PrivateBytes = bitmap GPU mapping + heap residual\n");
    fprintf(g_log, "  Only bitmap destruction + D3D cleanup can fully reclaim.\n");

    fprintf(g_log, "\n====== Done ======\n");
    fclose(g_log);
    DestroyWindow(hwnd);
    CoUninitialize();
    return 0;
}
