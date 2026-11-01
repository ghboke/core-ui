#pragma once

#include <cstdint>

namespace ui {
class Widget;
namespace page {

/* build 316: 普通文字设置接口 (ui_label_set_text / ui_widget_set_tooltip /
 * ui_titlebar_set_title / ui_window_set_title) 调用时解除该控件 / 窗口在所有
 * 页面上的 key 绑定 —— 宿主改用字面文字后, 换语言不能再把它盖回去。 */
enum class HostTextSlot { Text = 0, Tooltip = 1, TitlebarTitle = 2 };
void UnbindHostText(Widget* w, HostTextSlot slot);
void UnbindHostWindowTitle(uint64_t win);

}  // namespace page
}  // namespace ui
