#pragma once
// BoxBlurBgra: 右键菜单磨砂背景用的横 + 竖两遍 box blur (BGRA, 输出 alpha 恒 255)。
// 边缘按 clamp 取样 (越界取最近的边缘像素), 每个通道 = 窗口内 2r+1 个值的整数平均。
// 纯 header, 无 Win32 依赖, 供 context_menu.cpp 和 box_blur_test 共用。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ui {

// 滑动窗口累加: 窗口右移一格 = 减去移出的、加上移入的, 耗时与半径无关。
// 结果与"每像素重新累加 2r+1 个邻居"的朴素写法逐字节一致 (box_blur_test 对照);
// 朴素写法在菜单尺寸 (444x1294, r=64) 两遍约 185ms, 卡 UI 线程。
inline void BoxBlurBgra(std::vector<uint8_t>& pixels, int width, int height, int radius) {
    if (width <= 0 || height <= 0 || radius <= 0 || pixels.empty()) return;
    std::vector<uint8_t> tmp(pixels.size());
    auto idx = [width](int x, int y) { return ((size_t)y * (size_t)width + (size_t)x) * 4; };
    const int diameter = radius * 2 + 1;

    // 横向: 每行一个窗口和, x 从 0 往右滑。
    for (int y = 0; y < height; ++y) {
        int sb = 0, sg = 0, sr = 0;
        for (int k = -radius; k <= radius; ++k) {
            size_t p = idx(std::clamp(k, 0, width - 1), y);
            sb += pixels[p + 0];
            sg += pixels[p + 1];
            sr += pixels[p + 2];
        }
        for (int x = 0; x < width; ++x) {
            size_t d = idx(x, y);
            tmp[d + 0] = (uint8_t)(sb / diameter);
            tmp[d + 1] = (uint8_t)(sg / diameter);
            tmp[d + 2] = (uint8_t)(sr / diameter);
            tmp[d + 3] = 255;
            size_t out = idx(std::clamp(x - radius, 0, width - 1), y);
            size_t in  = idx(std::clamp(x + radius + 1, 0, width - 1), y);
            sb += pixels[in + 0] - pixels[out + 0];
            sg += pixels[in + 1] - pixels[out + 1];
            sr += pixels[in + 2] - pixels[out + 2];
        }
    }

    // 纵向: 每列一个窗口和, 按行推进 (逐行访问, 不按列跳着读)。
    std::vector<int> sums((size_t)width * 3, 0);
    for (int k = -radius; k <= radius; ++k) {
        const int sy = std::clamp(k, 0, height - 1);
        for (int x = 0; x < width; ++x) {
            size_t p = idx(x, sy);
            sums[(size_t)x * 3 + 0] += tmp[p + 0];
            sums[(size_t)x * 3 + 1] += tmp[p + 1];
            sums[(size_t)x * 3 + 2] += tmp[p + 2];
        }
    }
    for (int y = 0; y < height; ++y) {
        const int outY = std::clamp(y - radius, 0, height - 1);
        const int inY  = std::clamp(y + radius + 1, 0, height - 1);
        for (int x = 0; x < width; ++x) {
            int* s = &sums[(size_t)x * 3];
            size_t d = idx(x, y);
            pixels[d + 0] = (uint8_t)(s[0] / diameter);
            pixels[d + 1] = (uint8_t)(s[1] / diameter);
            pixels[d + 2] = (uint8_t)(s[2] / diameter);
            pixels[d + 3] = 255;
            size_t out = idx(x, outY);
            size_t in  = idx(x, inY);
            s[0] += tmp[in + 0] - tmp[out + 0];
            s[1] += tmp[in + 1] - tmp[out + 1];
            s[2] += tmp[in + 2] - tmp[out + 2];
        }
    }
}

}  // namespace ui
