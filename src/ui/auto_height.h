#pragma once

#include <cmath>

namespace ui {

/* <window height="auto"> 的目标客户区高度 (DIP, build 328)。
 * content_h: 根节点 HeightForWidth(客户区宽度);
 * min_h:     ui_window_set_min_size / min-height 给的下限, <=0 不限;
 * work_h:    窗口所在屏工作区高度 (DIP), <=0 不限 —— 内容再高也不能让窗口
 *            超出屏幕, 那样底部按钮一样点不到。
 * 向上取整前减一点余量: 浮点累加出的 490.004 不该变成 491。 */
inline int AutoHeightTargetDip(float content_h, int min_h, int work_h) {
    int h = static_cast<int>(std::ceil(content_h - 0.01f));
    if (min_h > 0 && h < min_h) h = min_h;
    if (work_h > 0 && h > work_h) h = work_h;
    return h < 1 ? 1 : h;
}

}  // namespace ui
