#include "anim_clock.h"

#include <windows.h>

namespace ui {

uint64_t AnimNowMs() {
    static const int64_t freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    /* 先除后乘会丢精度, 先乘会在 freq=10MHz 时约 29 年后溢出 int64, 拆成整数 + 余数两段。 */
    const int64_t whole = now.QuadPart / freq;
    const int64_t rest = now.QuadPart % freq;
    const uint64_t ms = static_cast<uint64_t>(whole) * 1000ULL +
                        static_cast<uint64_t>(rest * 1000 / freq);
    return ms + 1;   // 开机后最初 1ms 内 QPC 可能算出 0, 加 1 保证非 0
}

}  // namespace ui
