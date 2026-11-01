#pragma once

/* MsgBox — 独立窗口模态询问框 (build 158, GuoheView L148)。
 *
 * 像系统 MessageBox 的使用手感: 同步阻塞、返回被点按钮索引、支持 1..4
 * 个自定义按钮、Enter=默认按钮、Esc/关闭=取消按钮。
 *
 * 实现走 lib 自己的窗口 + widget 体系 (dogfood 内部 C API):
 * ui_window_create (frameless, 系统 DWM 管窗口形状 — Win11 圆角 / Win10
 * 直角) + TitleBar (标题 + 拖动 + 关闭按钮) + label/button widget 树,
 * 主题/hover/focus/Tab 导航全部自动获得。参考 GuoheView 图片信息窗的
 * 窗口形态。
 *
 * 取代旧的 in-window DialogWidget (ui_dialog_*, build 158 BREAKING 移除)。
 */

#include <string>
#include <vector>

#include "../../include/ui_core.h"

namespace ui {

class MsgBox {
public:
    /* 同步模态。parent = 宿主 UiWindow (被禁用至返回)。
     * check_text 空 = 无勾选框。返回 {按钮索引, 勾选终态}。 */
    static UiMsgBoxResult Show(UiWindow parent,
                               const std::wstring& title,
                               const std::wstring& message,
                               const std::vector<std::wstring>& buttons,
                               int default_idx, int cancel_idx, int icon,
                               const std::wstring& check_text,
                               int check_initial,
                               const UiColor* btn_colors,        /* 可 NULL */
                               const std::vector<int>& button_keys = {});  /* 每按钮 VK 绑定; 空=无 */
};

/* ---- IPC 自动化钩子 (build 172) — 让 ui_debug_server 驱动当前活动 msgbox。
 * 全部必须在 UI 线程调 (debug server 经 ui_window_invoke_sync marshal)。模态
 * 唯一, 故"当前活动"就是栈顶那个 (支持嵌套)。 */
struct MsgBoxDebugInfo {
    bool active = false;
    int  default_idx = 0;
    int  cancel_idx  = -1;
    int  button_count = 0;
    int  focused_idx = -1;        /* build 174: 当前键盘焦点按钮 (-1=无) */
    std::wstring title;
    std::vector<std::wstring> buttons;
};
MsgBoxDebugInfo MsgBoxDebugSnapshot();   /* 当前活动 msgbox 快照 (active=false=无)。 */
bool MsgBoxDebugFinish(int idx);          /* 触发 idx 按钮 (等价点击); 无活动返 false。 */
bool MsgBoxDebugCancel();                 /* build 339: 等价 Esc (cancelled=1); 不可取消 / 无活动返 false。 */

}  // namespace ui
