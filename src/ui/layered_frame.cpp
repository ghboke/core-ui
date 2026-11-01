#include "layered_frame.h"

#include "debug_trace.h"

namespace ui {

LayeredFrameBuffer::~LayeredFrameBuffer() {
    std::lock_guard<std::mutex> lock(mutex_);
    Free(slots_[0]);
    Free(slots_[1]);
}

bool LayeredFrameBuffer::Ensure(Slot& s, int w, int h) {
    if (s.dib && s.w == w && s.h == h) return true;
    Free(s);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   /* 自上而下 */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
        if (dib) DeleteObject(dib);
        return false;
    }
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) {
        DeleteObject(dib);
        return false;
    }
    s.oldBitmap = SelectObject(dc, dib);
    s.dib = dib;
    s.dc = dc;
    s.bits = static_cast<uint8_t*>(bits);
    s.w = w;
    s.h = h;
    return true;
}

void LayeredFrameBuffer::Free(Slot& s) {
    if (s.dc) {
        if (s.oldBitmap) SelectObject(s.dc, s.oldBitmap);
        DeleteDC(s.dc);
    }
    if (s.dib) DeleteObject(s.dib);
    s = Slot{};
}

uint8_t* LayeredFrameBuffer::BeginWrite(int w, int h) {
    if (w <= 0 || h <= 0) return nullptr;
    /* 后台缓冲只有渲染线程碰; front_ 只在 Publish (同线程) 里改, 读它不用锁。 */
    Slot& back = slots_[1 - front_];
    if (!Ensure(back, w, h)) {
        TraceEvent("core_layered", "layered_dib_create_failed",
                   {TraceI64("w_px", w), TraceI64("h_px", h)});
        return nullptr;
    }
    return back.bits;
}

void LayeredFrameBuffer::Publish(uint64_t generation) {
    std::lock_guard<std::mutex> lock(mutex_);
    Slot& back = slots_[1 - front_];
    if (!back.dib) return;
    back.generation = generation;
    front_ = 1 - front_;
    hasFront_ = true;
}

bool LayeredFrameBuffer::Commit(HWND hwnd, const POINT* screenPos, uint64_t minGeneration) {
    if (!hwnd) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasFront_) return false;
    const Slot& f = slots_[front_];
    if (f.generation < minGeneration) return false;
    if (!screenPos && f.generation == committed_) return false;

    POINT src{0, 0};
    SIZE size{f.w, f.h};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    const BOOL ok = UpdateLayeredWindow(hwnd, nullptr, const_cast<POINT*>(screenPos), &size,
                                        f.dc, &src, 0, &blend, ULW_ALPHA);
    if (ok) committed_ = f.generation;
    TraceEvent("core_layered", ok ? "layered_commit" : "layered_commit_failed",
               {TraceU64("frame_generation", f.generation),
                TraceI64("w_px", f.w),
                TraceI64("h_px", f.h),
                TraceBool("move", screenPos != nullptr),
                TraceI64("x", screenPos ? screenPos->x : 0),
                TraceI64("y", screenPos ? screenPos->y : 0),
                TraceU64("err", ok ? 0 : GetLastError())});
    return ok != FALSE;
}

uint64_t LayeredFrameBuffer::FrontGeneration() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return hasFront_ ? slots_[front_].generation : 0;
}

uint64_t LayeredFrameBuffer::CommittedGeneration() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return committed_;
}

SIZE LayeredFrameBuffer::FrontSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasFront_) return SIZE{0, 0};
    return SIZE{slots_[front_].w, slots_[front_].h};
}

bool LayeredFrameBuffer::ReadFrontPixel(int x, int y, uint32_t* out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasFront_ || !out) return false;
    const Slot& f = slots_[front_];
    if (x < 0 || y < 0 || x >= f.w || y >= f.h) return false;
    const uint8_t* p = f.bits + (static_cast<size_t>(y) * f.w + x) * 4;
    *out = (uint32_t(p[3]) << 24) | (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | p[0];
    return true;
}

} // namespace ui
