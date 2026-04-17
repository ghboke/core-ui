/**
 * 对比三种加载方式的内存：
 *   A) 手动条带: CreateEmptyBitmap + WIC CopyPixels 条带 + CopyFromMemory
 *   B) D2D 直传: CreateBitmapFromWicBitmap (让 D2D 内部处理)
 *   C) 手动条带 + 独立堆: 在 HeapCreate 的独立堆上分配条带缓冲
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
static const char* LOG_PATH = "E:\\code\\c\\core-ui\\test\\build\\direct_result.txt";
static FILE* g_log = nullptr;

static void logMem(const char* label) {
    PROCESS_MEMORY_COUNTERS_EX pmc = {};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
    fprintf(g_log, "  %-50s WS=%4zuMB  Private=%4zuMB\n", label,
            pmc.WorkingSetSize / (1024*1024), pmc.PrivateUsage / (1024*1024));
    fflush(g_log);
}

struct D2DEnv {
    ComPtr<ID2D1Factory1> factory;
    ComPtr<ID3D11Device> d3d;
    ComPtr<ID2D1DeviceContext> ctx;
    ComPtr<IDXGISwapChain1> swap;
    ComPtr<ID2D1Bitmap1> target;

    bool init(HWND hwnd) {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                           __uuidof(ID2D1Factory1),
                           reinterpret_cast<void**>(factory.GetAddressOf()));
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                           levels, 1, D3D11_SDK_VERSION, d3d.GetAddressOf(), nullptr, nullptr);
        ComPtr<IDXGIDevice> dxgi; d3d.As(&dxgi);
        ComPtr<ID2D1Device> d2dDev;
        factory->CreateDevice(dxgi.Get(), d2dDev.GetAddressOf());
        d2dDev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, ctx.GetAddressOf());

        ComPtr<IDXGIAdapter> adapter; dxgi->GetAdapter(adapter.GetAddressOf());
        ComPtr<IDXGIFactory2> dxgiF;
        adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(dxgiF.GetAddressOf()));
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width = 800; desc.Height = 600;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        dxgiF->CreateSwapChainForHwnd(d3d.Get(), hwnd, &desc, nullptr, nullptr, swap.GetAddressOf());

        ComPtr<IDXGISurface> surface;
        swap->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(surface.GetAddressOf()));
        D2D1_BITMAP_PROPERTIES1 tbp = {};
        tbp.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        tbp.dpiX = tbp.dpiY = 96.0f;
        tbp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        ctx->CreateBitmapFromDxgiSurface(surface.Get(), tbp, target.GetAddressOf());
        ctx->SetTarget(target.Get());
        return true;
    }
};

static ComPtr<IWICFormatConverter> openWic(IWICImagingFactory* wic, UINT& w, UINT& h) {
    ComPtr<IWICBitmapDecoder> dec;
    wic->CreateDecoderFromFilename(IMAGE_PATH, nullptr, GENERIC_READ,
                                    WICDecodeMetadataCacheOnLoad, dec.GetAddressOf());
    ComPtr<IWICBitmapFrameDecode> frame;
    dec->GetFrame(0, frame.GetAddressOf());
    ComPtr<IWICFormatConverter> conv;
    wic->CreateFormatConverter(conv.GetAddressOf());
    conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                     WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeMedianCut);
    conv->GetSize(&w, &h);
    return conv;
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    g_log = fopen(LOG_PATH, "w");
    if (!g_log) return 1;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"DTest";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(L"DTest", L"T", WS_OVERLAPPEDWINDOW, 0, 0, 800, 600, nullptr, nullptr, hInst, nullptr);

    ComPtr<IWICImagingFactory> wic;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()));

    fprintf(g_log, "====== D2D+WIC Memory Comparison ======\n\n");

    /* ====== Test B: CreateBitmapFromWicBitmap (D2D 直传) ====== */
    {
        fprintf(g_log, "--- Test B: CreateBitmapFromWicBitmap ---\n");
        D2DEnv env; env.init(hwnd);
        logMem("B0. before WIC open");

        UINT w, h;
        auto conv = openWic(wic.Get(), w, h);
        fprintf(g_log, "  Image: %u x %u\n", w, h);
        logMem("B1. WIC opened");

        D2D1_BITMAP_PROPERTIES1 props = {};
        props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        props.dpiX = props.dpiY = 96.0f;
        props.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;

        ComPtr<ID2D1Bitmap1> bmp;
        HRESULT hr = env.ctx->CreateBitmapFromWicBitmap(conv.Get(), props, bmp.GetAddressOf());
        if (FAILED(hr)) {
            fprintf(g_log, "  FAILED: 0x%lx\n", hr);
        } else {
            logMem("B2. CreateBitmapFromWicBitmap done");
        }

        conv.Reset();
        logMem("B3. WIC released");
        bmp.Reset();
        logMem("B4. bitmap released");
    }
    /* env 销毁 */
    logMem("B5. all D2D released");

    fprintf(g_log, "\n");

    /* ====== Test C: 条带 + VirtualAlloc 手动管理 ====== */
    {
        fprintf(g_log, "--- Test C: strip decode + VirtualAlloc ---\n");
        D2DEnv env; env.init(hwnd);
        logMem("C0. before");

        UINT w, h;
        auto conv = openWic(wic.Get(), w, h);
        logMem("C1. WIC opened");

        /* 创建 GPU-only 位图 */
        D2D1_BITMAP_PROPERTIES1 props = {};
        props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        props.dpiX = props.dpiY = 96.0f;
        props.bitmapOptions = D2D1_BITMAP_OPTIONS_NONE;

        ComPtr<ID2D1Bitmap1> bmp;
        env.ctx->CreateBitmap(D2D1::SizeU(w, h), nullptr, 0, props, bmp.GetAddressOf());
        logMem("C2. empty bitmap created");

        /* 用 VirtualAlloc 分配条带缓冲（可以精确归还） */
        const UINT stripH = 512;
        const UINT stride = w * 4;
        size_t stripBytes = (size_t)stride * stripH;
        uint8_t* stripBuf = (uint8_t*)VirtualAlloc(nullptr, stripBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

        for (UINT y = 0; y < h; y += stripH) {
            UINT rows = (y + stripH <= h) ? stripH : (h - y);
            WICRect rc = { 0, (INT)y, (INT)w, (INT)rows };
            conv->CopyPixels(&rc, stride, stride * rows, stripBuf);
            D2D1_RECT_U dest = { 0, y, w, y + rows };
            bmp->CopyFromMemory(&dest, stripBuf, stride);
        }

        logMem("C3. decode+upload done");

        VirtualFree(stripBuf, 0, MEM_RELEASE);
        logMem("C4. strip VirtualFree");

        conv.Reset();
        logMem("C5. WIC released");

        HeapCompact(GetProcessHeap(), 0);
        logMem("C6. HeapCompact");

        bmp.Reset();
    }
    logMem("C7. all released");

    fprintf(g_log, "\n====== Done ======\n");
    fclose(g_log);
    DestroyWindow(hwnd);
    CoUninitialize();
    return 0;
}
