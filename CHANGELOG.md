# Changelog

版本号规则：`MAJOR.MINOR.PATCH.BUILD`

- `MAJOR/MINOR/PATCH`：语义化版本，对应 `CMakeLists.txt` 的 `UI_CORE_VERSION`
- `BUILD`：构建编号，对应 `CMakeLists.txt` 的 `UI_CORE_INTERNAL_VERSION`
  - 每次发布构建 +1；同一 MAJOR.MINOR.PATCH 下可累加
- DLL 属性里看到的 `FileVersion` 即该四段串，由 `src/version.rc.in` 注入

## 1.1.0 — build 2

### 新增：调试与事件模拟 API (`ui_debug_*`)

为自动化测试 / AI 代理操作 UI / 脚本化回归而设计的一整套事件注入接口。详见
[`docs/debug-simulation.md`](./docs/debug-simulation.md)。

- **鼠标 / 键盘模拟（内部通路）**：`ui_debug_click` / `right_click` / `hover` /
  `drag` / `wheel` / `focus` / `blur` / `key` / `type_text` 等，走真实 widget 事件
  路径，触发 onClick / onValueChanged 等回调。
- **高层控件操作**：`ui_debug_checkbox_set` / `toggle_set` / `radio_select` /
  `combo_select` / `slider_set` / `number_set` / `tab_set` / `expander_set` /
  `splitview_set` / `flyout_show` / `text_set` / `scroll_set` 等。
- **Context Menu 支持（含子菜单）**：`ui_debug_menu_is_open` / `menu_item_count_at` /
  `menu_click_path`（按整数路径 `[i0,i1,...]` 点击任意深度的菜单项）。
- **HWND 通路**：`ui_debug_post_click` / `post_right_click` / `post_key` /
  `post_char` / `pump`，通过 Win32 `PostMessage` 走真实消息循环。
- **线程安全工具**：`ui_window_invoke_sync(win, fn, ud)` 把跨线程调用 marshal 到 UI
  线程，避免工作线程直接 mutate widget 造成数据竞争。

### 新增：Widget 事件派发重构

- 抽出 `UiWindowImpl::DispatchKeyDown(vk)`，WM_KEYDOWN 和 `SimKeyDown` 共用。
  先前 Sim 版本漏掉 Tab 焦点 / Enter-Space 激活 / 方向键 Slider 等逻辑，现已补齐。
- `ContextMenu::SimulateClickPath([i0, i1, ...])` 递归点击任意深度子菜单，并把
  item id 回传给 root 菜单的父窗口。
- 新增 `ContextMenu::g_debugSuppressAutoClose` / `ui_debug_set_menu_autoclose(0)`：
  自动化脚本持有前台窗口时关掉菜单的 50ms 前台轮询自动关闭行为。

### 修复

- **`ui_widget_find_by_id`**：markup 构建的子 widget 从未登记到 handle table，
  旧实现会返回 `UI_INVALID`，实际上应当把找到的 widget 插入 handle table 再返回。
- `ui_debug_dump_widget` 现在能识别 `NavItem` / `Flyout` / `Expander` / `SplitView` /
  `NumberBox` / `TextInput` / `TextArea` 的类型名和状态字段。

### 新增：demo + pipe 调试协议

- `demo/app.cpp` 的 `\\.\pipe\ui_core_debug` 从 6 条命令扩展到 45+ 条，覆盖所有
  可交互控件。pipe 命令处理器现在通过 `ui_window_invoke_sync` 在 UI 线程上执行。
- demo 注册了一个带 "Paste Special" 子菜单的右键菜单以便测试。
- 新增 `scripts/debug-smoke.ps1` 回归脚本：60 步端到端覆盖 9 个 demo 页面，
  每步截图，生成 `smoke-report.json`。

### 文档

- 新增 [`docs/debug-simulation.md`](./docs/debug-simulation.md) —— 控件 × 操作矩阵、
  C API 参考、pipe 命令表、PowerShell / Python / C++ 示例。
- website 新增 Debug 页（`/docs/debug`）。

---

## 1.0.0 — build 1

- 首个带版本号记录的版本
- `core-ui.dll` / `ui-demo.exe` 嵌入 Windows VERSIONINFO 资源
- 新增 `ui_core_version()` / `ui_core_version_build()` / `ui_core_version_string()` 运行时 API
- 头文件暴露 `UI_CORE_VERSION_MAJOR/MINOR/PATCH/BUILD` 宏与 `UI_CORE_VERSION_STRING`
