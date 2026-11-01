#pragma once

#include <windows.h>
#include <cstdint>
#include <mutex>

namespace ui {

/* 分层窗口 (WS_EX_LAYERED + UpdateLayeredWindow) 的帧双缓冲。
 *
 * 为什么用 ULW: 程序频繁改窗口位置/尺寸 (看图器无边框随图缩放) 时, flip / 复制模式
 * 交换链的呈现与窗口矩形变化都没有同步, 屏幕有其他动画时会合成"新矩形 + 旧画面"。
 * ULW 一次调用同时提交位置 + 尺寸 + 画面, frame-probe 实测 0 坏帧。
 * 画面必须先读回到 CPU 内存 (DIB) 再交给 ULW —— 直接给 GDI 兼容纹理的 DC 不原子
 * (GuoheView tools/ulw-proto 实测)。
 *
 * 线程分工:
 *   渲染线程: BeginWrite 拿后台缓冲写像素 → Publish 换到前台。
 *   UI 线程:  Commit 把前台交给 ULW (ULW 改位置/尺寸会同步发 WM_WINDOWPOSCHANGED,
 *             只能在窗口所属线程调, 否则与等渲染完成的 UI 线程互等)。
 * 只有一个写线程; Publish 与 Commit 互斥, 所以 Commit 读到的前台缓冲不会被改写。
 *
 * 像素: BGRA premultiplied, 自上而下, stride = w * 4。 */
class LayeredFrameBuffer {
public:
    LayeredFrameBuffer() = default;
    ~LayeredFrameBuffer();
    LayeredFrameBuffer(const LayeredFrameBuffer&) = delete;
    LayeredFrameBuffer& operator=(const LayeredFrameBuffer&) = delete;

    /* 渲染线程: 后台缓冲按 w×h 准备好 (尺寸变了才重建 DIB), 返回像素指针;
     * 失败返回 nullptr。 */
    uint8_t* BeginWrite(int w, int h);
    /* 渲染线程: 后台缓冲写完, 记上帧号换到前台。 */
    void Publish(uint64_t generation);

    /* UI 线程: 前台帧交给 ULW。screenPos 为空 = 位置不变; 尺寸总是前台帧的尺寸。
     * minGeneration: 前台帧号小于它就不提交 (防旧帧覆盖新窗口)。
     * 已提交过同一帧且 screenPos 为空时跳过。返回是否调用了 ULW 且成功。 */
    bool Commit(HWND hwnd, const POINT* screenPos, uint64_t minGeneration = 0);

    uint64_t FrontGeneration() const;
    uint64_t CommittedGeneration() const;
    SIZE FrontSize() const;
    /* 测试用: 读前台帧一个像素 (BGRA premul 打包成 0xAARRGGBB)。越界或无帧返回 false。 */
    bool ReadFrontPixel(int x, int y, uint32_t* out) const;

private:
    struct Slot {
        HBITMAP dib = nullptr;
        HDC dc = nullptr;
        HGDIOBJ oldBitmap = nullptr;
        uint8_t* bits = nullptr;
        int w = 0;
        int h = 0;
        uint64_t generation = 0;
    };
    static bool Ensure(Slot& s, int w, int h);
    static void Free(Slot& s);

    mutable std::mutex mutex_;
    Slot slots_[2];
    int front_ = 0;            /* slots_[front_] = 前台; 另一块是后台 */
    bool hasFront_ = false;
    uint64_t committed_ = 0;
};

} // namespace ui
