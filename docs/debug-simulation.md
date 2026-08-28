# Debug Simulation / 调试与事件模拟

本文档记录 core-ui 的 **自动化调试 / 交互模拟 API**。通过这些接口，你可以在不移动鼠标、
不敲键盘的情况下，对窗口里的任意控件触发点击、选择、右键菜单、键盘输入等行为，用于：

- 端到端 UI 自动化测试
- 脚本驱动演示 / 录屏
- AI 代理操作 UI（例如对话式助手）
- 回归验证（内部发布前通过 smoke 脚本覆盖核心流程）

## 架构

两层通道，自上而下：

```
                ┌──────────────────────────────────────┐
                │  Named pipe  \\.\pipe\ui_core_debug  │  ← 文本命令（PowerShell / Python）
                └──────────────┬───────────────────────┘
                               │
                ┌──────────────▼─────────────────┐
                │   C API:  ui_debug_* (ui_core.h) │  ← 从 C/C++ 代码直接调用
                └──────┬─────────────────┬─────────┘
                       │                 │
            (内部通路) ▼                 ▼ (HWND 通路)
       UiWindowImpl::Sim*          PostMessage(hwnd, WM_*)
       （同步触发 widget 事件）      （走真实 Win32 消息循环）
                       │                 │
                       └────────┬────────┘
                                ▼
                         Widget OnMouseDown / OnKeyChar ...
                         Callbacks (onClick / onValueChanged / ...)
```

- **内部通路 `ui_debug_*`**：同步、立即生效、可在任意线程调用（但需保证 UI 线程未阻塞）。
  精确命中 widget 的 `OnMouseDown` / `OnKeyChar`，触发回调。推荐用于自动化测试。
- **HWND 通路 `ui_debug_post_*`**：异步，通过 `PostMessage` 入队到 Win32 消息队列。
  走和真实用户一模一样的路径（含 `WM_SETCURSOR`、capture、tooltip 计时器等）。
  最保真，但需要 `ui_run()` 或 `ui_debug_pump()` 处理队列后才生效。

> **库级 pipe server（v1.4.0 build 8 起可用）**：`\\.\pipe\ui_core_debug` 协议
> 实现在 core-ui 库内（`src/ui/ui_debug_server.cpp`），任何应用一行启用。详见
> 本文末"Server API"一节。`.ui` demo（`demo/app.cpp`）和 `.uix` demo
> （`demo/ui_demo_html.cpp`，对应 `ui-demo-uix.exe`）都已切换到这套 API。

## 控件 × 可模拟操作矩阵

| 控件 | C API | Pipe 命令 | 说明 |
|---|---|---|---|
| Button | `ui_debug_click` | `click <id>` | 触发 onClick |
| IconButton | 同上 | 同上 | |
| CaptionButton | 同上 | 同上 | 标题栏最小/最大/关闭按钮 |
| CheckBox | `ui_debug_checkbox_toggle/set` | `check <id> [0\|1\|toggle]` | 改 checked + 触发 onValueChanged |
| RadioButton | `ui_debug_radio_select` | `radio <id>` | 选中该项（自动取消同组） |
| Toggle (Switch) | `ui_debug_toggle_set` | `toggle <id> [0\|1\|toggle]` | 改 on + 触发 onValueChanged |
| Slider | `ui_debug_slider_set` / `ui_debug_drag` | `slider <id> <v>` / `drag <id> <dx> <dy>` | 设 value + 触发 onFloatChanged |
| ProgressBar | `ui_progress_set_value` | — | 只读展示 |
| NumberBox | `ui_debug_number_set` | `number <id> <v>` | 设 value |
| TextInput | `ui_debug_text_set` / `ui_debug_type_text` | `input <id> <text>` / `type <text>` (需先 focus) | 直接赋值 或 逐字符输入 |
| TextArea | 同上 | `textarea <id> <text>` / `type` | |
| ComboBox | `ui_debug_combo_select` | `combo <id> <index>` | 改 selectedIndex + 触发 onSelectionChanged |
| ComboBox | `ui_debug_combo_open/close` | `combo_open <id>` | 打开下拉 |
| TabControl | `ui_debug_tab_set` | `tab <id> <idx>` | 切换 tab |
| ScrollView | `ui_debug_scroll_set` / `ui_debug_wheel` | `scroll <id> <y>` / `wheel <id> <delta>` | 设滚动位置 或 滚轮 |
| Expander | `ui_debug_expander_set` | `expander <id> [0\|1\|toggle]` | 展开/折叠 |
| SplitView | `ui_debug_splitview_set` | `splitview <id> [0\|1\|toggle]` | 开/关侧栏 |
| Flyout | `ui_debug_flyout_show/hide` | `flyout [show\|hide\|toggle]` | demo 里绑定到固定的 `demoFlyout` |
| Dialog | `ui_debug_dialog_confirm/cancel` | `dialog_confirm` / `dialog_cancel` | 等同按 Enter / Esc |
| Context Menu | `ui_debug_menu_click_index/id` | `menu_click <idx>` / `menu_click_id <id>` | 对当前已打开菜单 |
| Context Menu | `ui_debug_menu_close` | `menu_close` | |
| Context Menu | `ui_debug_screenshot_menu` | `screenshot_menu <path>` | 截 popup RT, 验证 icon / submenu 实际渲染 |
| ImageView | `ui_image_set_zoom/pan/rotation` | `zoom/pan/rotate <id> ...` | |
| OcrImgView | `ui_debug_drag_to` / `ui_debug_double_click_at` / `ui_debug_key_mod` | `drag_to` / `dbl_click_at` / `key ctrl+c` | 划词=在文本 quad 上拖；**单击是清选区**（PDF 惯例），要选中某块用 `dbl_click_at`，三击选整行=`dbl_click_at` 后紧跟一次 `click_at`。坐标换算用 `ui_ocr_img_view_image_to_screen`。回归见 `test/scripts/ocr_demo_test.ps1`（62 项，五场景 + 四个旋转角），demo 为 `ocr-demo` target。**选择粒度是字**，所以拖动端点要用 `word_edge <i> <0\|1>`（块的左/右缘）而非 `word_center`，后者只落在块中间某个字上 |
| NavItem | `ui_debug_click` | `click nav_home` | 点击即切换 |
| MenuBar | `ui_debug_click` | `click <menubar_id>` | 点击按钮区触发菜单 |
| Splitter | `ui_debug_drag` | `drag <id> <dx> <dy>` | 拖动分隔条改比例 |
| Custom | `ui_debug_click` / `ui_debug_wheel` | 普通鼠标模拟 | 走用户回调 |
| Label / Separator / Spacer / Panel / VBox / HBox / Grid / Stack / Overlay / ToolTip | — | — | 非交互 |

## C API 参考

所有函数返回 `0` 成功、非 `0` 失败；坐标参数均为 **DIP（逻辑像素）**。

### 鼠标

```c
int  ui_debug_click(UiWindow win, UiWidget w);
int  ui_debug_click_at(UiWindow win, float x, float y);
int  ui_debug_double_click(UiWindow win, UiWidget w);
int  ui_debug_double_click_at(UiWindow win, float x, float y);
int  ui_debug_right_click(UiWindow win, UiWidget w);
int  ui_debug_right_click_at(UiWindow win, float x, float y);
int  ui_debug_hover(UiWindow win, UiWidget w);
int  ui_debug_mouse_move(UiWindow win, float x, float y);
int  ui_debug_mouse_down(UiWindow win, float x, float y);
int  ui_debug_mouse_up(UiWindow win, float x, float y);
int  ui_debug_drag(UiWindow win, UiWidget w, float dx, float dy);
int  ui_debug_drag_to(UiWindow win, float x1, float y1, float x2, float y2);
int  ui_debug_wheel(UiWindow win, UiWidget w, float delta);
int  ui_debug_wheel_at(UiWindow win, float x, float y, float delta);
```

双击走真实的 `WM_LBUTTONDBLCLK` 路径（`@dblclick` hook + `Widget::OnMouseDoubleClick`
冒泡）。**连点两次 `ui_debug_click` 不等价** —— 那条路只触发 `onClick`，永远进
不了双击分支。位置敏感的双击（图片划词选整行、时间轴打点之类）必须用
`_at` 变体，`ui_debug_double_click` 只能点 widget 中心。

滚轮由 `UiWindowImpl::OnMouseWheel` 里一条硬编码的 `dynamic_cast` 链分发到
已知 widget 类型。**新写的可缩放/可滚动 widget 必须把自己加进那条链**，否则
`ui_debug_wheel*` 和真实滚轮都不会调到它的 `OnMouseWheel`（表现为"滚轮没反应"
但事件确实发出去了）。

### 焦点 / 键盘

```c
int  ui_debug_focus(UiWindow win, UiWidget w);
int  ui_debug_blur(UiWindow win);
int  ui_debug_key(UiWindow win, int vk);           /* 发送 WM_KEYDOWN 到焦点控件 */
int  ui_debug_key_mod(UiWindow win, int vk, int ctrl, int shift, int alt);
int  ui_debug_type_char(UiWindow win, unsigned int ch);
int  ui_debug_type_text(UiWindow win, const wchar_t* text);
```

**组合键必须用 `ui_debug_key_mod`。** widget 的 `OnKeyDown` 普遍用
`GetKeyState(VK_CONTROL)` 判修饰键，而 `ui_debug_key` 只送 vk —— 修饰键读出来
永远是"没按下"，`Ctrl+C` / `Ctrl+A` 这类分支在自动化里等同死代码。
`ui_debug_key_mod` 在注入期间用 `SetKeyboardState` 临时置位**线程**键盘状态表
（debug 命令本就跑在 UI 线程，和 `GetKeyState` 同线程），调用后还原。

### 高层控件

```c
int  ui_debug_checkbox_toggle(UiWindow win, UiWidget w);
int  ui_debug_checkbox_set(UiWindow win, UiWidget w, int checked);
int  ui_debug_toggle_set(UiWindow win, UiWidget w, int on);
int  ui_debug_radio_select(UiWindow win, UiWidget w);
int  ui_debug_combo_select(UiWindow win, UiWidget w, int index);
int  ui_debug_combo_open(UiWidget w);
int  ui_debug_combo_close(UiWidget w);
int  ui_debug_slider_set(UiWindow win, UiWidget w, float value);
int  ui_debug_number_set(UiWindow win, UiWidget w, float value);
int  ui_debug_tab_set(UiWidget w, int index);
int  ui_debug_expander_set(UiWidget w, int expanded);
int  ui_debug_splitview_set(UiWidget w, int open);
int  ui_debug_flyout_show(UiWidget flyout, UiWidget anchor);
int  ui_debug_flyout_hide(UiWidget flyout);
int  ui_debug_text_set(UiWidget w, const wchar_t* text);
int  ui_debug_scroll_set(UiWidget scrollview, float y);
```

### Context Menu

需要菜单已被打开（通过 `ui_menu_show` 或真实右键）。这些 API 操作的是 **当前 active 菜单**：

```c
int  ui_debug_menu_is_open(UiWindow win);      /* 1 = 当前有菜单 */
int  ui_debug_menu_item_count(UiWindow win);
int  ui_debug_menu_click_index(UiWindow win, int index);
int  ui_debug_menu_click_id(UiWindow win, int item_id);
int  ui_debug_menu_close(UiWindow win);

/* 子菜单：用整数 path 定位任意深度的 item。
   例如 path={2,0}, depth=2 表示"顶层第 2 项（必须是 submenu）里的第 0 项"。*/
int  ui_debug_menu_item_count_at(UiWindow win, const int* path, int depth);
int  ui_debug_menu_item_id_at(UiWindow win, const int* path, int depth);
int  ui_debug_menu_has_submenu_at(UiWindow win, const int* path, int depth);
int  ui_debug_menu_click_path(UiWindow win, const int* path, int depth);
```

Pipe 协议里用 `/` 分隔 path 分量，例如 `menu_click_path 2/0`。

### Dialog

Dialog 是窗口级 modal overlay（详见 `controls.md`），脱离 widget 树。
模拟操作不需要 widget handle，直接对 window 触发：

```c
int  ui_debug_dialog_confirm(UiWindow win);    /* 等同按 Enter，触发 OK 回调 */
int  ui_debug_dialog_cancel(UiWindow win);     /* 等同按 Esc，触发 Cancel 回调 */
```

Dialog 弹出期间，所有 `ui_debug_*` 鼠标 / 滚轮模拟会被窗口入口拦截直接派给
dialog（modal 行为），下层 widget 不会响应——这是预期的。要测下层逻辑就先
`dialog_cancel`。

### HWND 通道（PostMessage）

```c
int  ui_debug_post_click(UiWindow win, float x, float y);
int  ui_debug_post_right_click(UiWindow win, float x, float y);
int  ui_debug_post_mouse_move(UiWindow win, float x, float y);
int  ui_debug_post_key(UiWindow win, int vk);
int  ui_debug_post_char(UiWindow win, unsigned int ch);
int  ui_debug_pump(void);                      /* 处理消息队列，返回已处理条数 */
```

### 查询

```c
int  ui_debug_widget_center(UiWidget w, float* outX, float* outY);   /* DIP 中心点 */
int  ui_debug_widget_is_visible(UiWidget w);
```

## Pipe 命令参考

连接 `\\.\pipe\ui_core_debug`，写入单行命令文本，读取一个 JSON 响应。

### 响应格式

- 成功：`{"ok":true, ...}` 或一个数据 JSON（如 `tree`、`widget` 返回完整结构）
- 失败：`{"error":"..."}`

### 命令总表

| 命令 | 示例 | 说明 |
|---|---|---|
| `tree` | `tree` | 整棵 widget 树 JSON |
| `widget <id>` | `widget nav_home` | 单个 widget 详情 |
| `highlight <id>` | `highlight flyoutBtn` | 红框高亮；`highlight` 清除 |
| `screenshot <path>` | `screenshot out.png` | 保存 PNG（UTF-8 路径） |
| `screenshot_widget <id> <path>` | `screenshot_widget nav_home nav.png` | 单 widget 截图，按 widget rect 裁剪 |
| `screenshot_menu <path>` | `screenshot_menu menu.png` | 截当前打开的 popup 菜单 RT（自 build 27）；用来验证 menu icon / submenu / 颜色实际渲染 |
| `invalidate` | — | 重绘窗口 |
| `pump` | — | 处理 PostMessage 队列 |
| `nav <0-8>` | `nav 3` | 切到某页（0=home, 1=button, 2=check, 3=input, 4=range, 5=status, 6=layout, 7=crop, 8=settings） |
| `scroll [id] [y]` | `scroll input_page 200` | 设置 ScrollView.scrollY |
| `flyout [show\|hide\|toggle]` | `flyout toggle` | 对 demo 里 `demoFlyout` 操作 |
| `click <id>` | `click flyoutBtn` | 点击（触发 onClick） |
| `click_at <x> <y>` | `click_at 300 400` | 窗口坐标点击 |
| `dbl_click <id>` | | 双击控件中心；走真实 WM_LBUTTONDBLCLK 路径 |
| `dbl_click_at <x> <y>` | `dbl_click_at 150 178` | 按坐标双击；位置敏感的双击用这个 |
| `rclick <id>` / `rclick_at <x> <y>` | | 右键；等同 WM_RBUTTONUP |
| `hover <id>` | | 鼠标移到控件中心 |
| `move <x> <y>` | | |
| `drag <id> <dx> <dy>` | `drag hSlider 80 0` | 从控件中心拖 (dx, dy) |
| `drag_to <x1> <y1> <x2> <y2>` | | |
| `wheel <id> <delta>` | `wheel pages -120` | 滚轮 |
| `wheel_at <x> <y> <delta>` | | |
| `focus <id>` / `blur` | | |
| `key <vk\|name>` | `key enter` | 发虚拟键；名称：enter/esc/tab/space/back/del/left/right/up/down/home/end；单个字母按字面解（`key a` = VK 0x41），纯数字仍按 VK 码解（`key 65` = 'A'） |
| `key <mod>+<key>` | `key ctrl+a` · `key ctrl+shift+c` | 组合键；修饰键 ctrl/shift/alt 可叠加。走 `ui_debug_key_mod`，`GetKeyState` 能读到修饰键状态 |
| `type <text>` | `type Hello` | 逐字符发送 WM_CHAR 等效；需先 focus |
| `check <id> [0\|1\|toggle]` | `check chkA toggle` | |
| `toggle <id> [0\|1\|toggle]` | | |
| `radio <id>` | `radio radioB` | 选中该 radio |
| `combo <id> <idx>` | `combo langCombo 1` | |
| `combo_open <id>` | | |
| `slider <id> <v>` | `slider volSlider 0.75` | |
| `number <id> <v>` | | |
| `tab <id> <idx>` | | |
| `expander <id> [0\|1\|toggle]` | | |
| `splitview <id> [0\|1\|toggle]` | `splitview mainSplit toggle` | |
| `input <id> <text>` / `textarea <id> <text>` / `set_text <id> <text>` | | 直接赋值 |
| `zoom <id> <v>` / `pan <id> <x> <y>` / `rotate <id> <deg>` | | ImageView |
| `menu_is_open` / `menu_count` | | |
| `menu_click <idx>` / `menu_click_id <id>` / `menu_close` | | |
| `menu_count_at <path>` / `menu_id_at <path>` / `menu_has_sub <path>` / `menu_click_path <path>` | `menu_click_path 2/0` | path 用 `/` 分隔，支持任意层级 |
| `dialog_confirm` / `dialog_cancel` | | |
| `post_click <x> <y>` / `post_rclick <x> <y>` / `post_key <vk>` / `post_char <cp>` | | Win32 通道；需后续 `pump` 或等待 ui_run |
| `help` / `?` | | 返回命令列表 |

## 使用示例

### PowerShell（一次性命令）

```powershell
# 从命令行发送一条命令，读一次响应
function Send-UiCmd($cmd) {
    $pipe = New-Object System.IO.Pipes.NamedPipeClientStream '.', 'ui_core_debug', 'InOut'
    $pipe.Connect(2000)
    $w = New-Object System.IO.StreamWriter($pipe)
    $w.Write($cmd); $w.Flush()
    $r = New-Object System.IO.StreamReader($pipe)
    $resp = $r.ReadToEnd()
    $pipe.Dispose()
    return $resp
}

Send-UiCmd 'nav 2'                      # 切到 check 页
Send-UiCmd 'check chk_sample toggle'    # 勾选 / 取消
Send-UiCmd 'screenshot check-page.png'
```

### Python

```python
import win32file, pywintypes

def send(cmd):
    h = win32file.CreateFile(r'\\.\pipe\ui_core_debug',
        win32file.GENERIC_READ | win32file.GENERIC_WRITE,
        0, None, win32file.OPEN_EXISTING, 0, None)
    win32file.SetNamedPipeHandleState(h, 2, None, None)  # PIPE_READMODE_MESSAGE
    win32file.WriteFile(h, cmd.encode('utf-8'))
    _, data = win32file.ReadFile(h, 65536)
    win32file.CloseHandle(h)
    return data.decode('utf-8')

print(send('nav 1'))              # 切到 Button 页
print(send('click btn_primary'))  # 点击主按钮
```

### C++（内嵌 C API）

```cpp
#include <ui_core.h>

void demoAutomation(UiWindow win) {
    UiWidget root = ui_window_get_root(win);

    // 1. 切到 check 页
    UiWidget navCheck = ui_widget_find_by_id(root, "nav_check");
    ui_debug_click(win, navCheck);

    // 2. 勾选 checkbox
    UiWidget cb = ui_widget_find_by_id(root, "chk_sample");
    ui_debug_checkbox_set(win, cb, 1);

    // 3. 打开下拉，选第 2 项
    UiWidget combo = ui_widget_find_by_id(root, "lang_combo");
    ui_debug_combo_select(win, combo, 1);

    // 4. 右键出菜单 → 选第一项（假设当前窗口注册了 onRightClick 弹菜单）
    ui_debug_right_click_at(win, 400, 300);
    // 等一帧让 menu popup 上屏
    ui_debug_pump();
    ui_debug_menu_click_index(win, 0);
}
```

## 已知限制与下一步

- **线程安全**：demo 的 pipe 命令处理器会通过 `ui_window_invoke_sync` 把实际执行
  marshal 到 UI 线程，所以内置 pipe 协议是安全的。如果你在应用里自己从工作线程
  调 `ui_debug_*` 系列 API，仍需用 `ui_window_invoke_sync` 或其它手段把调用
  送到 UI 线程。
- **键盘模拟**：`ui_debug_key(vk)` 和真实 WM_KEYDOWN 走同一条 `DispatchKeyDown`
  分发，因此 Tab 焦点轮换、Enter/Space 激活 Button/CheckBox/Toggle、方向键控制
  Slider/Radio、Esc 关 ComboBox 都会被模拟触发。**组合键用 `ui_debug_key_mod`
  / `key ctrl+a`** —— 它在注入期间用 `SetKeyboardState` 临时置位
  线程键盘状态表，让 widget 里的 `GetKeyState(VK_CONTROL)` 读到修饰键，调用后
  还原。此前只能靠用户手动按住修饰键，或 `ui_window_register_shortcut` 绕过。
- **滚轮分发**：`UiWindowImpl::OnMouseWheel` 用一条硬编码 `dynamic_cast` 链找
  接收者。**新增可滚动/可缩放 widget 必须把自己加进那条链**，否则真实滚轮和
  `ui_debug_wheel*` 都不会调到它的 `OnMouseWheel`。症状是"滚轮完全没反应"而事件
  确实发出去了——排查时先确认自己在不在链里，别去怀疑事件注入。
- **子菜单**：`menu_click_path` 支持任意深度，但目前不真正"展开"子菜单的 HWND
  弹窗（模拟点击不会有过场动画 / 可见的子菜单窗口）。如果需要像真实用户那样逐层
  hover 打开，用 `ui_debug_hover` + 坐标去触发 `ContextMenu::HandleMouseMove`。
- **拖拽（DnD）**：未覆盖系统级文件拖入（WM_DROPFILES）— 需用其他方案模拟。
- **动画时序**：像 Expander / Toggle 的展开动画是由 timer 推进的；模拟操作会立刻改变
  逻辑状态，但视觉上可能要等下一次 paint 才生效。自动化脚本里通常在 `pump` 之后再
  `screenshot`。

## Server API

库级 named pipe debug server。任何应用一行启用：

```c
ui_init();
UiWindow win = ui_window_create(...);
ui_debug_server_start(win, NULL);   // 默认 pipe \\.\pipe\ui_core_debug
ui_run();
ui_debug_server_stop();   // 程序退出前清理
ui_shutdown();
```

`pipe_name=NULL` 用默认 `ui_core_debug`。**自 1.6.0 build 63 开始**支持多 server
并存，每窗口一个独立 pipe——同进程主窗 + 设置窗 / 子窗都能各自挂 server，互不
抢占：

```c
ui_debug_server_start(main_win,     L"my_app_main_debug");
ui_debug_server_start(settings_win, L"my_app_settings_debug");
```

之前同进程只能一个 server，重复 `_start` 返回 `-2`；1.6.0+ 不再有这个限制。
每条命令通过 `ui_window_invoke_sync` marshal 回对应窗口的 UI 线程执行。

### 自定义命令处理器

应用可注册 handler 添加私有命令或覆盖 builtin：

```c
static int myHandler(const char* cmd, const char* args,
                     char* out, int cap, void* /*ud*/) {
    if (strcmp(cmd, "open_settings") == 0) {
        OpenSettingsDialog();
        const char* s = "{\"ok\":true}";
        int n = (int)strlen(s); memcpy(out, s, n); return n;
    }
    return 0;   // 不处理，回退到 builtin
}

ui_debug_server_set_handler(myHandler, NULL);
ui_debug_server_start(win, NULL);
```

派发顺序：用户 handler 优先 → builtin → unknown 错误。返回值约定：
`>0` = 写入 `out_buf` 的字节数（作为 JSON 响应）；`0` = 不处理；`<0` = 错误（也回退）。

### 单 widget 截图

```c
UI_API int ui_debug_screenshot_widget(UiWindow win, UiWidget w, const wchar_t* outPath);
```

按 `widget->rect` 裁剪窗口截图，DIP→pixel 内部完成。pipe 命令对应
`screenshot_widget <id> <path>`。Widget 必须已布局（rect 非空）。

### Demo 接入示例

- `demo/app.cpp`（`.ui` demo）—— 自定义 handler 处理 `nav` / `flyout` /
  `scroll`（active-page fallback），其余命令走 builtin
- `demo/ui_demo_html.cpp`（`.uix` demo，对应 `ui-demo-uix.exe`）—— 一行
  `ui_debug_server_start`，无私有命令
- PowerShell / Python 脚本可通过 pipe 端到端驱动 tree dump、hover、click、
  screenshot、screenshot_widget、wheel、dialog/menu 状态查询

## 相关文件

- `include/ui_core.h` — 公开 C API 声明
- `src/ui/ui_debug_server.h/cpp` — 库级 pipe server + builtin dispatcher
- `src/ui/ui_window.h/cpp` — `UiWindowImpl::Sim*` 内部事件派发 + `ScreenshotRegion`
- `src/ui/context_menu.h/cpp` — `ContextMenu::SimulateClickIndex`
- `src/ui/ui_api.cpp` — C API 实现
- `demo/app.cpp` — `.ui` demo + 自定义 handler 示例
- `demo/ui_demo_html.cpp` — `.uix` demo (`ui-demo-uix.exe`) 接入示例
- 内部 smoke 脚本 — 覆盖端到端和跨页面回归，不随公开仓发布
