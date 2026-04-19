# Changelog

版本号规则：`MAJOR.MINOR.PATCH.BUILD`

- `MAJOR/MINOR/PATCH`：语义化版本，对应 `CMakeLists.txt` 的 `UI_CORE_VERSION`
- `BUILD`：构建编号，对应 `CMakeLists.txt` 的 `UI_CORE_INTERNAL_VERSION`
  - 每次发布构建 +1；同一 MAJOR.MINOR.PATCH 下可累加
- DLL 属性里看到的 `FileVersion` 即该四段串，由 `src/version.rc.in` 注入

## 1.3.0 — build 4

### 新增：字体 / 文字渲染 C API

三层控制粒度：全局默认、单窗口覆盖、渲染模式预设。纯 C API，零 DirectWrite 样板代码。
支持中英字体分离（Latin / CJK 各走各的家族），通过 `IDWriteFontFallbackBuilder`
在 11 个 CJK Unicode 区段上做映射。

**全局默认（进程级）**
- `ui_theme_set_default_font(family)` / `ui_theme_get_default_font()`
- `ui_theme_set_cjk_font(latin, cjk)` / `ui_theme_get_cjk_latin_font()` / `ui_theme_get_cjk_cjk_font()`
- `ui_theme_set_text_render_mode(mode)` / `ui_theme_get_text_render_mode()`

**单窗口覆盖**
- `ui_window_set_default_font(win, family)`
- `ui_window_set_cjk_font(win, latin, cjk)`
- `ui_window_set_text_render_mode(win, mode)`
- `ui_window_clear_font_override(win)` —— 一次清除该窗口所有覆盖

**渲染模式枚举 `UiTextRenderMode`** ——  5 个预设覆盖 95% 场景，不需要手写 DWrite
gamma / contrast / clearTypeLevel：

| 预设                          | 风格                  | DWrite 映射                    |
|-------------------------------|-----------------------|--------------------------------|
| `UI_TEXT_RENDER_SMOOTH`       | 默认 / WinUI 风        | GRAYSCALE + NATURAL_SYMMETRIC |
| `UI_TEXT_RENDER_CLEARTYPE`    | Office / Chrome 风     | CLEARTYPE + NATURAL           |
| `UI_TEXT_RENDER_SHARP`        | 记事本最锐             | CLEARTYPE + GDI_CLASSIC       |
| `UI_TEXT_RENDER_GRAY_SHARP`   | 灰度 + 像素对齐        | GRAYSCALE + GDI_CLASSIC       |
| `UI_TEXT_RENDER_ALIASED`      | 无抗锯齿 / 像素块      | ALIASED + ALIASED             |

### 修复

- **嵌套 ContextMenu**：子菜单叶子项被点击后不再残留 root popup；父菜单内移动/点击不再
  让子菜单闪烁 关-开-关。

### Demo

- `demo/font_api_demo.cpp` —— 公开 API 版字体 / 渲染模式演示
- `demo/font_demo.cpp` —— 7 模式 × 3 字体的低层 A/B 对比，开发调参用

---

### New: Font / text-rendering C API

Three layers of control: process-wide defaults, per-window overrides, and render-mode
presets. Pure C API, zero DirectWrite boilerplate. Supports Latin / CJK font split
(each script uses its own family) via `IDWriteFontFallbackBuilder` mapping across
11 CJK Unicode blocks.

**Global defaults (process-wide)**
- `ui_theme_set_default_font(family)` / `ui_theme_get_default_font()`
- `ui_theme_set_cjk_font(latin, cjk)` / `ui_theme_get_cjk_latin_font()` / `ui_theme_get_cjk_cjk_font()`
- `ui_theme_set_text_render_mode(mode)` / `ui_theme_get_text_render_mode()`

**Per-window overrides**
- `ui_window_set_default_font(win, family)`
- `ui_window_set_cjk_font(win, latin, cjk)`
- `ui_window_set_text_render_mode(win, mode)`
- `ui_window_clear_font_override(win)` — reset all overrides on this window at once

**Render-mode enum `UiTextRenderMode`** — five presets cover 95% of cases; no need
to hand-tune DWrite gamma / contrast / clearTypeLevel:

| Preset                        | Style                         | DWrite mapping                 |
|-------------------------------|-------------------------------|--------------------------------|
| `UI_TEXT_RENDER_SMOOTH`       | default, WinUI-like           | GRAYSCALE + NATURAL_SYMMETRIC  |
| `UI_TEXT_RENDER_CLEARTYPE`    | Office / Chrome-like          | CLEARTYPE + NATURAL            |
| `UI_TEXT_RENDER_SHARP`        | Notepad-sharp                 | CLEARTYPE + GDI_CLASSIC        |
| `UI_TEXT_RENDER_GRAY_SHARP`   | grayscale, pixel-aligned      | GRAYSCALE + GDI_CLASSIC        |
| `UI_TEXT_RENDER_ALIASED`      | aliased / pixel block         | ALIASED + ALIASED              |

### Fixes

- **Nested ContextMenu**: a leaf-item click no longer leaves the root popup on screen;
  movement/clicks within a parent menu no longer cause the submenu to flicker
  closed-open-closed.

### Demos

- `demo/font_api_demo.cpp` — public-API showcase for the new font / render-mode knobs
- `demo/font_demo.cpp` — low-level 7-modes × 3-fonts A/B matrix, internal tuning tool

---

## 1.2.0 — build 3

### 新增：无边框画布模式（Frameless Canvas Mode）

面向"整个窗口就是一张画布 + 滚轮缩放 + 按住拖动窗口"类应用（如图片查看器 /
色卡面板 / 桌面挂件），补齐原先需要绕过 ui-core 直接调 Win32 的盲区。

#### 画布三件套
- `ui_window_set_min_size(win, w, h)` —— 覆盖主题默认最小尺寸（480×360），
  画布模式下窗口可以缩到图片实际大小以下
- `ui_window_set_background_mode(win, mode)` —— `mode=1` 时 OnPaint 用透明 Clear
  代替主题色填充，SetWindowPos 扩大窗口时不再闪主题色
- `ui_widget_set_drag_window(w, enable)` —— 命中该 widget 时 WM_NCHITTEST 返回
  HTCAPTION，由系统接管窗口拖动。HitTest 的深度优先机制让内部交互控件天然
  不受影响（只有命中 Panel 本身的空白处才触发拖窗）

#### 一键切换
- `ui_window_enable_canvas_mode(win, 1)` —— 一次打开 min_size=32×32 +
  bg_mode=1 + 根 dragWindow=true + 隐藏 TitleBar（如有）；=0 恢复默认

#### 窗口几何（DIP-native，原先缺失）
- `ui_window_set_rect(win, x, y, w, h)` —— 原子 SetWindowPos + 同步重绘
- `ui_window_set_size(win, w, h)` —— 保持位置改尺寸
- `ui_window_set_position(win, x, y)` —— 保持尺寸改位置
- `ui_window_get_rect_screen(win, &x, &y, &w, &h)` —— 读当前几何
- `ui_window_resize_with_anchor(win, w, h, acx, acy, sx, sy)` —— 滚轮缩放
  "光标不动"原语：resize 到 (w, h) 的同时把客户区 (acx, acy) 对齐到屏幕 (sx, sy)

#### Widget 新字段
- `Widget.dragWindow`（markup 属性 `dragWindow` / `drag-window`）

---

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
