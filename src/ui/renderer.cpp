#include "renderer.h"
#include "theme.h"
#include <vector>
#include <memory>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>
#include <windows.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace ui {
ComPtr<ID3D11Device> Renderer::s_d3dDevice;
ComPtr<ID2D1Device>  Renderer::s_d2dDevice;
int                  Renderer::s_deviceRefCount = 0;
} // namespace ui

#ifndef GetDpiForWindow
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
} // namespace

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

bool Renderer::EnsureSharedDevice() {
    if (s_d3dDevice && s_d2dDevice) return true;

    /* 全局只创建一次 D3D11 设备（最慢的步骤） */
    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
    };
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                    creationFlags, featureLevels, _countof(featureLevels),
                                    D3D11_SDK_VERSION, s_d3dDevice.GetAddressOf(),
                                    nullptr, nullptr);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                creationFlags, featureLevels, _countof(featureLevels),
                                D3D11_SDK_VERSION, s_d3dDevice.GetAddressOf(),
                                nullptr, nullptr);
        if (FAILED(hr)) return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = s_d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) return false;

    hr = factory_->CreateDevice(dxgiDevice.Get(), s_d2dDevice.GetAddressOf());
    if (FAILED(hr)) return false;

    /* 不要启用 ID3D11Multithread::SetMultithreadProtected(TRUE)：
     * 实测会让 ShowWindow 触发的首帧 DWM 合成等待 250-300ms（大约是 vsync 周期的整数倍），
     * 严重拖慢启动速度。若后台线程需要访问 D3D（如 GPU 回读），应改为线程间
     * 消息传递到 UI 线程执行，而不是全局加锁。*/
    return true;
}

bool Renderer::CreateRenderTarget(HWND hwnd) {
    hwnd_ = hwnd;
    targetBitmap_.Reset();
    swapChain_.Reset();
    ctx_.Reset();
    brushCache_.clear();

    /* 1. 确保共享 D3D11/D2D 设备已创建 */
    if (!EnsureSharedDevice()) return false;
    s_deviceRefCount++;

    /* 2. 为此窗口创建独立的 DeviceContext */
    HRESULT hr = s_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, ctx_.GetAddressOf());
    if (FAILED(hr)) return false;

    /* 3. 创建 SwapChain */
    ComPtr<IDXGIDevice> dxgiDevice;
    hr = s_d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
    if (FAILED(hr)) return false;

    ComPtr<IDXGIFactory2> dxgiFactory;
    hr = adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(dxgiFactory.GetAddressOf()));
    if (FAILED(hr)) return false;

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
    if (FAILED(hr)) return false;

    /* 4. 从 SwapChain 获取 back buffer，创建 target bitmap */
    ComPtr<IDXGISurface> surface;
    hr = swapChain_->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(surface.GetAddressOf()));
    if (FAILED(hr)) return false;

    UINT dpi = GetDpiForWindow(hwnd);
    D2D1_BITMAP_PROPERTIES1 bitmapProps = {};
    bitmapProps.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    bitmapProps.dpiX = (float)dpi;
    bitmapProps.dpiY = (float)dpi;
    bitmapProps.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

    hr = ctx_->CreateBitmapFromDxgiSurface(surface.Get(), bitmapProps, targetBitmap_.GetAddressOf());
    if (FAILED(hr)) return false;

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

void Renderer::Resize(UINT width, UINT height) {
    if (!ctx_ || !swapChain_) return;

    ctx_->SetTarget(nullptr);
    targetBitmap_.Reset();
    brushCache_.clear();

    HRESULT hr = swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) return;

    ComPtr<IDXGISurface> surface;
    hr = swapChain_->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(surface.GetAddressOf()));
    if (FAILED(hr)) return;

    UINT dpi = GetDpiForWindow(hwnd_);
    D2D1_BITMAP_PROPERTIES1 bitmapProps = {};
    bitmapProps.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    bitmapProps.dpiX = (float)dpi;
    bitmapProps.dpiY = (float)dpi;
    bitmapProps.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

    hr = ctx_->CreateBitmapFromDxgiSurface(surface.Get(), bitmapProps, targetBitmap_.GetAddressOf());
    if (FAILED(hr)) return;

    ctx_->SetTarget(targetBitmap_.Get());
    ctx_->SetDpi((float)dpi, (float)dpi);
}

void Renderer::BeginDraw() {
    ctx_->BeginDraw();
    ctx_->SetTransform(D2D1::Matrix3x2F::Identity());
}

HRESULT Renderer::EndDraw() {
    HRESULT hr = ctx_->EndDraw();
    if (swapChain_) {
        DXGI_PRESENT_PARAMETERS params = {};
        UINT syncInterval = skipVSync ? 0 : 1;
        swapChain_->Present1(syncInterval, 0, &params);
        skipVSync = false;
    }
    return hr;
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
    ctx_->Clear(color);
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
    auto brush = GetBrush(color);
    if (brush) ctx_->FillRectangle(rect, brush.Get());
}

void Renderer::FillRoundedRect(const D2D1_RECT_F& rect, float rx, float ry, const D2D1_COLOR_F& color) {
    auto brush = GetBrush(color);
    if (brush) {
        D2D1_ROUNDED_RECT rr = {rect, rx, ry};
        ctx_->FillRoundedRectangle(rr, brush.Get());
    }
}

void Renderer::DrawRect(const D2D1_RECT_F& rect, const D2D1_COLOR_F& color, float width) {
    auto brush = GetBrush(color);
    if (brush) ctx_->DrawRectangle(rect, brush.Get(), width);
}

void Renderer::DrawRoundedRect(const D2D1_RECT_F& rect, float rx, float ry, const D2D1_COLOR_F& color, float width) {
    auto brush = GetBrush(color);
    if (brush) {
        D2D1_ROUNDED_RECT rr = {rect, rx, ry};
        ctx_->DrawRoundedRectangle(rr, brush.Get(), width);
    }
}

void Renderer::DrawLine(float x1, float y1, float x2, float y2, const D2D1_COLOR_F& color, float width) {
    auto brush = GetBrush(color);
    if (brush) ctx_->DrawLine({x1, y1}, {x2, y2}, brush.Get(), width);
}

void Renderer::DrawText(const std::wstring& text, const D2D1_RECT_F& rect, const D2D1_COLOR_F& color,
                         float fontSize, DWRITE_TEXT_ALIGNMENT align, DWRITE_FONT_WEIGHT weight,
                         DWRITE_PARAGRAPH_ALIGNMENT vAlign, bool wordWrap) {
    auto brush = GetBrush(color);
    auto fmt = GetTextFormat(fontSize, theme::kFontFamily, weight);
    if (!brush || !fmt || !dwFactory_) return;

    float layoutW = rect.right - rect.left;
    float layoutH = rect.bottom - rect.top;
    if (layoutW <= 0 || layoutH <= 0) return;

    ComPtr<IDWriteTextLayout> layout;
    HRESULT hr = dwFactory_->CreateTextLayout(
        text.c_str(), (UINT32)text.length(), fmt.Get(), layoutW, layoutH, &layout);
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

    ctx_->DrawTextLayout({rect.left, rect.top}, layout.Get(), brush.Get());
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
    auto brush = GetBrush(color);
    auto fmt = GetTextFormat(fontSize, L"Segoe MDL2 Assets");
    if (brush && fmt) {
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        ctx_->DrawTextW(glyph.c_str(), (UINT32)glyph.length(), fmt.Get(), rect, brush.Get());
    }
}

ComPtr<ID2D1Bitmap> Renderer::LoadImageFromFile(const std::wstring& path) {
    if (!wicFactory_ || !ctx_) return nullptr;

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = wicFactory_->CreateDecoderFromFilename(
        path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
    if (FAILED(hr)) return nullptr;

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
    hr = decoder->GetFrame(bestFrame, frame.GetAddressOf());
    if (FAILED(hr)) return nullptr;

    ComPtr<IWICFormatConverter> converter;
    hr = wicFactory_->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr)) return nullptr;

    hr = converter->Initialize(
        frame.Get(), GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeMedianCut);
    if (FAILED(hr)) return nullptr;

    UINT imgW = 0, imgH = 0;
    hr = converter->GetSize(&imgW, &imgH);
    if (FAILED(hr) || imgW == 0 || imgH == 0) return nullptr;

    /* 创建空 D2D 位图，避免 CreateBitmapFromWicBitmap 导致的双倍内存 */
    auto bitmap = CreateEmptyBitmap(static_cast<int>(imgW), static_cast<int>(imgH));
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
    decoder.Reset();
    HeapCompact(GetProcessHeap(), 0);

    return bitmap;
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
    if (bitmap) {
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
    ctx_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

void Renderer::PopClip() {
    ctx_->PopAxisAlignedClip();
}

void Renderer::PushOpacity(float opacity, const D2D1_RECT_F& bounds) {
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
    ctx_->PopLayer();
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

    // Extract all paths with opacity
    auto paths = svg_detail::ExtractPaths(svgContent);
    if (paths.empty()) return icon;

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
            icon.valid = true;
        }
    }
    return icon;
}

void Renderer::DrawSvgIcon(const SvgIcon& icon, const D2D1_RECT_F& rect,
                            const D2D1_COLOR_F& color) {
    if (!icon.valid || !ctx_) return;

    float destW = rect.right - rect.left;
    float destH = rect.bottom - rect.top;
    float scale = std::min(destW / icon.viewBoxW, destH / icon.viewBoxH);
    float offX = rect.left + (destW - icon.viewBoxW * scale) / 2.0f;
    float offY = rect.top + (destH - icon.viewBoxH * scale) / 2.0f;

    auto iconTransform = D2D1::Matrix3x2F::Scale(scale, scale) *
                         D2D1::Matrix3x2F::Translation(offX, offY);
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

    ctx_->SetTransform(D2D1::Matrix3x2F::Identity());
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
