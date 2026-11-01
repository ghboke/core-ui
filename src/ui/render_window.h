#pragma once

#include <d2d1.h>
#include <windows.h>
#include <cstdint>
#include <memory>
#include <string>

namespace ui {

struct FrameJob;
class Renderer;
class LayeredFrameBuffer;

/* ULW 窗口: 渲染线程把一帧读回进 LayeredFrameBuffer 后发给窗口, 由 UI 线程
 * Commit (wParam 0, lParam = 帧号)。延后呈现的帧 (canvas 视觉事务) 不发, 由事务
 * 自己带位置提交。 */
constexpr UINT kUiCoreLayeredFrameReadyMessage = WM_APP + 132;

struct RenderWindowId {
    uint64_t window_id = 0;
    uint64_t hwnd_generation = 0;

    bool IsValid() const { return window_id != 0 && hwnd_generation != 0; }
    bool operator==(const RenderWindowId& other) const noexcept {
        return window_id == other.window_id &&
               hwnd_generation == other.hwnd_generation;
    }
};

enum class RenderSurfaceKind : uint8_t {
    HwndSwapChain,
    LayeredComposition,
    /* canvas 模式: 复制模式交换链, 程序频繁 SetWindowPos 时尺寸与内容同帧到达。 */
    HwndCopySwapChain,
    /* 分层窗口 (WS_EX_LAYERED): 离屏绘制 → 读回 → UI 线程 UpdateLayeredWindow,
     * 位置 + 尺寸 + 画面原子提交。需要 LayeredFrameBuffer。 */
    HwndLayeredUlw,
};

enum class RenderFrameResult : uint8_t {
    Skipped,
    Prepared,
    Presented,
    InvalidWindow,
    RecreateTarget,
    DeviceFailure,
};

class RenderWindow {
public:
    RenderWindow() = default;
    RenderWindow(RenderWindowId id, HWND hwnd,
                 RenderSurfaceKind surfaceKind = RenderSurfaceKind::HwndSwapChain,
                 std::shared_ptr<LayeredFrameBuffer> layered = nullptr);
    ~RenderWindow();

    RenderWindow(const RenderWindow&) = delete;
    RenderWindow& operator=(const RenderWindow&) = delete;
    RenderWindow(RenderWindow&&) noexcept;
    RenderWindow& operator=(RenderWindow&&) noexcept;

    RenderWindowId Id() const { return id_; }
    HWND Hwnd() const { return hwnd_; }
    RenderSurfaceKind SurfaceKind() const { return surfaceKind_; }
    bool IsRegistered() const { return id_.IsValid() && hwnd_ != nullptr; }
    bool Matches(RenderWindowId id) const { return id_ == id; }

    void Register(RenderWindowId id, HWND hwnd,
                  RenderSurfaceKind surfaceKind = RenderSurfaceKind::HwndSwapChain,
                  std::shared_ptr<LayeredFrameBuffer> layered = nullptr);
    const std::shared_ptr<LayeredFrameBuffer>& Layered() const { return layered_; }
    void Unregister();
    void ReleaseDeviceResources();
    RenderFrameResult RenderFrame(const FrameJob& job);
    RenderFrameResult PresentPrepared(bool skipVSync);
    /* 交帧名额 (见 Renderer::TryAcquireFrameSlot)。还没建交换链时恒有。 */
    bool TryAcquireFrameSlot();
    HANDLE FrameSlotWaitable() const;
    void MarkFrameSlotHeld();
    int ScreenshotRegion(D2D1_RECT_F region, const std::wstring& outPath, float dpiScale);

private:
    void ReleaseResources();

    RenderWindowId id_{};
    HWND hwnd_ = nullptr;
    RenderSurfaceKind surfaceKind_ = RenderSurfaceKind::HwndSwapChain;
    std::shared_ptr<LayeredFrameBuffer> layered_;
    std::unique_ptr<Renderer> renderer_;
    bool rendererReady_ = false;
    bool targetReady_ = false;
    int widthPx_ = 0;
    int heightPx_ = 0;
};

} // namespace ui
