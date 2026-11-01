#include "render_window.h"

#include "render_thread.h"
#include "renderer.h"

#include <algorithm>
#include <d2d1.h>
#include <windows.h>
#include <wincodec.h>

namespace ui {

namespace {

bool InitialClearColor(const DisplayList& list, D2D1_COLOR_F& out) {
    if (list.commands.empty()) return false;
    const auto& first = list.commands.front();
    if (first.type != DrawCommandType::Clear) return false;
    out = first.color;
    return true;
}

} // namespace

RenderWindow::RenderWindow(RenderWindowId id, HWND hwnd, RenderSurfaceKind surfaceKind,
                           std::shared_ptr<LayeredFrameBuffer> layered) {
    Register(id, hwnd, surfaceKind, std::move(layered));
}

RenderWindow::~RenderWindow() = default;
RenderWindow::RenderWindow(RenderWindow&&) noexcept = default;
RenderWindow& RenderWindow::operator=(RenderWindow&&) noexcept = default;

void RenderWindow::Register(RenderWindowId id, HWND hwnd, RenderSurfaceKind surfaceKind,
                            std::shared_ptr<LayeredFrameBuffer> layered) {
    id_ = id;
    hwnd_ = hwnd;
    surfaceKind_ = surfaceKind;
    layered_ = std::move(layered);
}

void RenderWindow::Unregister() {
    ReleaseResources();
    id_ = {};
    hwnd_ = nullptr;
}

void RenderWindow::ReleaseDeviceResources() {
    ReleaseResources();
}

RenderFrameResult RenderWindow::RenderFrame(const FrameJob& job) {
    if (!job.window.IsValid() ||
        !Matches(job.window) ||
        hwnd_ == nullptr ||
        job.hwnd != hwnd_ ||
        !IsWindow(hwnd_)) {
        return RenderFrameResult::InvalidWindow;
    }
    if (job.width_px <= 0 || job.height_px <= 0 || job.display_list.Empty()) {
        return RenderFrameResult::Skipped;
    }

    Renderer::SharedD2DGuard d2dGuard;

    if (!renderer_) {
        renderer_ = std::make_unique<Renderer>();
        rendererReady_ = false;
        targetReady_ = false;
        widthPx_ = 0;
        heightPx_ = 0;
    }
    if (!rendererReady_) {
        if (!renderer_->Init()) {
            ReleaseResources();
            return RenderFrameResult::DeviceFailure;
        }
        rendererReady_ = true;
    }
    if (!targetReady_ || !renderer_->RT()) {
        bool created = false;
        switch (surfaceKind_) {
        case RenderSurfaceKind::LayeredComposition:
            created = renderer_->CreateRenderTargetForLayered(hwnd_);
            break;
        case RenderSurfaceKind::HwndLayeredUlw:
            created = renderer_->CreateRenderTargetUlw(hwnd_, layered_);
            break;
        default:
            created = renderer_->CreateRenderTarget(
                hwnd_, surfaceKind_ == RenderSurfaceKind::HwndCopySwapChain);
            break;
        }
        if (!created) {
            ReleaseResources();
            return RenderFrameResult::DeviceFailure;
        }
        targetReady_ = true;
        widthPx_ = 0;
        heightPx_ = 0;
    }
    if (surfaceKind_ == RenderSurfaceKind::HwndSwapChain) {
        D2D1_COLOR_F clearColor{};
        if (InitialClearColor(job.display_list, clearColor)) {
            // DXGI_SCALING_NONE fills live-resize area outside the back buffer
            // with this color until the resized frame is presented.
            renderer_->SetSwapChainBackgroundColor(clearColor);
        }
    }
    const bool visible = IsWindowVisible(hwnd_) && !IsIconic(hwnd_);
    const bool presentNow = visible && !job.defer_present;
    const bool sizeChanged = widthPx_ != job.width_px || heightPx_ != job.height_px;
    if (sizeChanged) {
        renderer_->Resize(static_cast<UINT>(job.width_px), static_cast<UINT>(job.height_px));
        widthPx_ = job.width_px;
        heightPx_ = job.height_px;
    }

    renderer_->skipPresent = !presentNow;
    renderer_->skipVSync = visible &&
                           job.policy != PresentPolicy::Final &&
                           job.policy != PresentPolicy::Screenshot;
    renderer_->frameGeneration = job.generation;
    renderer_->BeginDraw();
    renderer_->ReplayDisplayList(job.display_list);
    HRESULT hr = renderer_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        ReleaseResources();
        return RenderFrameResult::RecreateTarget;
    }
    if (FAILED(hr)) {
        ReleaseResources();
        return RenderFrameResult::DeviceFailure;
    }
    if (surfaceKind_ == RenderSurfaceKind::HwndLayeredUlw && presentNow) {
        /* 帧已读回发布; 交给窗口线程 Commit (ULW 不能在这里调, 见 layered_frame.h) */
        PostMessageW(hwnd_, kUiCoreLayeredFrameReadyMessage, 0,
                     static_cast<LPARAM>(job.generation));
    }
    if (presentNow) return RenderFrameResult::Presented;
    return job.defer_present ? RenderFrameResult::Prepared : RenderFrameResult::Skipped;
}

bool RenderWindow::TryAcquireFrameSlot() {
    if (!renderer_ || !targetReady_) return true;
    return renderer_->TryAcquireFrameSlot();
}

HANDLE RenderWindow::FrameSlotWaitable() const {
    return (renderer_ && targetReady_) ? renderer_->FrameSlotWaitable() : nullptr;
}

void RenderWindow::MarkFrameSlotHeld() {
    if (renderer_) renderer_->MarkFrameSlotHeld();
}

RenderFrameResult RenderWindow::PresentPrepared(bool skipVSync) {
    if (!id_.IsValid() || hwnd_ == nullptr || !IsWindow(hwnd_)) {
        return RenderFrameResult::InvalidWindow;
    }
    if (!renderer_ || !targetReady_ || !renderer_->RT()) {
        return RenderFrameResult::Skipped;
    }
    if (!IsWindowVisible(hwnd_) || IsIconic(hwnd_)) {
        return RenderFrameResult::Skipped;
    }
    Renderer::SharedD2DGuard d2dGuard;
    HRESULT hr = renderer_->PresentPrepared(skipVSync);
    if (hr == D2DERR_RECREATE_TARGET) {
        ReleaseResources();
        return RenderFrameResult::RecreateTarget;
    }
    if (FAILED(hr)) {
        ReleaseResources();
        return RenderFrameResult::DeviceFailure;
    }
    return RenderFrameResult::Presented;
}

int RenderWindow::ScreenshotRegion(D2D1_RECT_F region, const std::wstring& outPath, float dpiScale) {
    if (!renderer_ || !renderer_->RT() || outPath.empty()) return -1;

    Renderer::SharedD2DGuard d2dGuard;

    auto* ctx = renderer_->RT();
    auto pixelSize = ctx->GetPixelSize();
    int fullW = static_cast<int>(pixelSize.width);
    int fullH = static_cast<int>(pixelSize.height);
    if (fullW <= 0 || fullH <= 0) return -2;

    int px0 = 0;
    int py0 = 0;
    int pxw = fullW;
    int pyh = fullH;
    if (region.right > region.left && region.bottom > region.top) {
        auto roundi = [](float f) { return static_cast<int>(f + 0.5f); };
        const float scale = dpiScale > 0.0f ? dpiScale : 1.0f;
        px0 = std::max(0, roundi(region.left * scale));
        py0 = std::max(0, roundi(region.top * scale));
        int px1 = std::min(fullW, roundi(region.right * scale));
        int py1 = std::min(fullH, roundi(region.bottom * scale));
        pxw = px1 - px0;
        pyh = py1 - py0;
        if (pxw <= 0 || pyh <= 0) return -18;
    }

    D2D1_BITMAP_PROPERTIES1 cpuProps = {};
    cpuProps.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                             D2D1_ALPHA_MODE_PREMULTIPLIED);
    cpuProps.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    ComPtr<ID2D1Bitmap1> cpuBmp;
    HRESULT hr = ctx->CreateBitmap(D2D1::SizeU(pxw, pyh), nullptr, 0, cpuProps, &cpuBmp);
    if (FAILED(hr)) return -3;

    ComPtr<ID2D1Bitmap1> target;
    ctx->GetTarget(reinterpret_cast<ID2D1Image**>(target.GetAddressOf()));
    if (!target) return -4;

    D2D1_POINT_2U dst = {0, 0};
    D2D1_RECT_U srcRc = {static_cast<UINT32>(px0), static_cast<UINT32>(py0),
                         static_cast<UINT32>(px0 + pxw), static_cast<UINT32>(py0 + pyh)};
    hr = cpuBmp->CopyFromBitmap(&dst, target.Get(), &srcRc);
    if (FAILED(hr)) return -5;

    D2D1_MAPPED_RECT mapped;
    hr = cpuBmp->Map(D2D1_MAP_OPTIONS_READ, &mapped);
    if (FAILED(hr)) return -6;

    auto unmap = [&]() { cpuBmp->Unmap(); };
    IWICImagingFactory* wic = renderer_->WIC();
    if (!wic) { unmap(); return -7; }

    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IWICStream> stream;

    hr = wic->CreateStream(&stream);
    if (FAILED(hr)) { unmap(); return -7; }
    hr = stream->InitializeFromFilename(outPath.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) { unmap(); return -8; }
    hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr)) { unmap(); return -9; }
    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) { unmap(); return -10; }
    hr = encoder->CreateNewFrame(&frame, nullptr);
    if (FAILED(hr)) { unmap(); return -11; }
    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) { unmap(); return -12; }
    hr = frame->SetSize(pxw, pyh);
    if (FAILED(hr)) { unmap(); return -13; }
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&fmt);
    if (FAILED(hr)) { unmap(); return -14; }
    hr = frame->WritePixels(pyh, mapped.pitch, mapped.pitch * pyh, mapped.bits);
    unmap();
    if (FAILED(hr)) return -15;
    hr = frame->Commit();
    if (FAILED(hr)) return -16;
    hr = encoder->Commit();
    if (FAILED(hr)) return -17;

    return 0;
}

void RenderWindow::ReleaseResources() {
    if (renderer_) {
        renderer_->ReleaseRenderTarget();
    }
    rendererReady_ = false;
    targetReady_ = false;
    widthPx_ = 0;
    heightPx_ = 0;
    renderer_.reset();
}

} // namespace ui
