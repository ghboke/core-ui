<p align="center">
  <img src="./logo.svg" alt="CORE UI" height="64">
</p>

<p align="center">
  <a href="./README-en.md">English</a> · <b>中文</b>
  &nbsp;·&nbsp;
  <a href="https://ghboke.github.io/core-ui/"><b>📖 在线文档</b></a>
</p>

**Core UI** 是一个现代化的 Windows 桌面 UI 框架，底层基于 **Direct2D / Direct3D 11** 硬件加速渲染，对齐 Microsoft **Fluent 2** 视觉语言，把从按钮、文本框到 Flyout、Dialog、TitleBar 的 25+ 个内置控件统一在一套**纯 C API**（250+ 个导出函数）之下——Rust、Go、Python、C#、Delphi 乃至 Lua 都能直接调用，不需要写 C++ 绑定层。界面推荐用 **`.uix` 单文件组件**（Vue 3 SFC 风格：`<window>` + `<script>` + `<style>` + `<template>`）描述：响应式数据绑定、`v-if` / `v-for` / `v-model` / `@click`、CSS 子集和 CSS 变量主题，脚本由内置的 **QuickJS-NG** 在原生进程内求值——没有 DOM、没有 Webview。

> **3.0 MB 一个 DLL，就能写出跟 Office / VSCode 同一设计语言的 Windows 桌面应用。**
> 不要 Chromium、不要 .NET、不要 Qt 几十兆的 moc/uic——一个 C 头文件，一份 `.uix` 单文件组件，搞定。

![version](https://img.shields.io/badge/version-1.6.0.170-blue)
![license](https://img.shields.io/badge/license-MIT-green)
![platform](https://img.shields.io/badge/platform-Windows%2010%2B-lightgrey)
![size](https://img.shields.io/badge/dll-3.0MB-brightgreen)
![api](https://img.shields.io/badge/C%20API-250%2B-orange)

## 🎯 为什么选 Core UI

| 对比维度 | Electron | WPF / WinUI 3 | Qt | **Core UI** |
|---------|----------|---------------|-----|-------------|
| **分发体积** | 100+ MB | 需要 .NET 运行时 | 40+ MB Qt DLLs | **3.0 MB 单 DLL** |
| **启动时间** | 1–3 秒 | 0.5–1 秒 | 0.5–1 秒 | **< 200 ms** |
| **内存占用** | 150+ MB | 80+ MB | 60+ MB | **< 30 MB** |
| **语言绑定** | 只能 JS | 只能 .NET | 只能 C++ | **C ABI，任意语言** |
| **设计规范** | 自己画 | Fluent（受限） | 类原生 | **Fluent 2 原生级** |
| **声明式 / 响应式 UI** | JSX + 虚拟 DOM | XAML + Binding | QML | **`.uix` Vue 3 SFC（QuickJS-NG）** |
| **学习曲线** | 大前端生态 | XAML + C# | C++ + Meta 对象 | **Vue 模板 + C 即上手** |

想要 Electron 的开发体验 + 原生的性能 + Fluent 2 的颜值 + Vue 的响应式心智模型——Core UI 是目前唯一同时打满几格的方案。

## 🤖 为 AI 而生

**Core UI 的架构天然让 LLM 做对**：`.uix` 就是 Vue 3 SFC（训练语料里 Vue / HTML 的占比远超任何桌面框架）、纯 C ABI 全 `uint64_t` 句柄（没有模板 / 继承 / 虚函数，极难幻觉出类型错误）、CSS + Flexbox 直接套用前端排版直觉、所有 API 遵循 `ui_<名词>_<动词>` 可被精确预测。Agent 只需 fetch 一个文件就能写出完整可运行的应用：

| 文档入口 | 说明 |
|------|------|
| **[`llms.txt`](./llms.txt)** | [llmstxt.org](https://llmstxt.org) 标准索引，Agent 第一步 fetch 这个 |
| **[`docs/uix-ai-guide.md`](./docs/uix-ai-guide.md)** | **自包含速查表**：`.uix` 结构 + 模板 + 脚本 + CSS 子集 + widget 列表 + 例子 |
| **[`docs/uix-guide.md`](./docs/uix-guide.md)** | Vue 3 SFC 完整指南、cookbook、限制 |
| **[`docs/debug-simulation.md`](./docs/debug-simulation.md)** | `ui_debug_*` 事件注入 + Named Pipe IPC，AI 自验证闭环 |
| **[`UI_CORE_API.md`](./UI_CORE_API.md)** | 250+ 导出函数完整清单，按模块分组 |

> Cursor / Claude Code / Cline / Continue 用户推荐把 `docs/uix-ai-guide.md` 加进项目规则（`.cursorrules` / `CLAUDE.md`），一次上下文全覆盖。

## 核心特性

### 🚀 轻到离谱，快到失真

- **3.0 MB 全量 DLL**，静态编译后 demo exe 仅 **~2 MB**，可装进 U 盘跑
- **Direct2D + Direct3D 11** 全硬件加速，Per-Monitor DPI V2 一次画对
- **冷启动 < 200ms**，空窗口内存 **< 30 MB**，60 fps 动画 CPU 占用 **< 3%**

### 🎨 颜值即正义

- 严格对齐 **Microsoft Fluent 2 Design Token**：色彩、圆角、阴影、动画无一例外
- 深色 / 浅色主题**一行切换**，所有内置控件自动响应；CSS 变量随之 cascade
- **自定义无边框窗口**自带 `<TitleBar>` 控件，系统级拖拽 / 贴靠 / 动画
- 25+ 控件颗粒度对标 WinUI 3：`button` / `input` / `toggle` / `combobox` / `progressbar` / `menu` / `Dialog` / `Toast`...

### 🧩 `.uix` 单文件组件 — 像写 Vue 一样写桌面 UI

```vue
<window title="Hello" width="400" height="300" centered="true" theme="light"/>

<script>
export default {
  data()    { return { count: 0 }; },
  computed: { doubled() { return this.count * 2; } },
  methods:  { inc() { this.count++; } }
}
</script>

<style>
  .root  { padding: 24px; gap: 12px; background: var(--bg); }
  .h1    { font-size: 22px; color: var(--fg); font-weight: 600; }
  button { background: var(--accent); color: #fff;
           padding: 6px 14px; border-radius: 4px; cursor: pointer; }
</style>

<template>
  <div class="root">
    <label class="h1">Hello, Core UI!</label>
    <label>count = {{ count }} · doubled = {{ doubled }}</label>
    <button @click="inc">+1</button>
  </div>
</template>
```

- **Vue 3 Options API**：`data()` / `computed` / `methods`，QuickJS-NG (ES2020+) 求值
- **响应式系统**：Proxy + WatchEffect，`{{ expr }}` / `:attr` / `v-if` / `v-for` / `v-model` / `@click` 自动收集依赖、增量重渲染
- **CSS 子集**：类 / 标签 / 后代选择器、伪类（`:hover` / `:disabled`）、Flexbox、`var(--*)` CSS 变量主题
- **i18n 内置**：`{{ $t('welcome') }}` 自动查 `.lang` 文件，运行时 `ui_page_set_locale` 切语言
- **声明式右键菜单**：`<menu trigger="#id" event="rclick">` + `<menuitem>` / `<separator>`

### 🌐 纯 C API，所有语言都能调

```c
#include <ui_core.h>

ui_init_with_theme(UI_THEME_LIGHT);

UiPage page = ui_page_load_file(L"app.uix");
ui_page_set_locale(page, "zh");
UiWindow win = ui_page_open_window(page, NULL);

/* 双向交换 reactive 状态 */
ui_page_set_int (page, "count", 42);
ui_page_set_json(page, "items", "[{\"id\":1,\"label\":\"a\"}]");
char* j = ui_page_get_json(page, "items");
ui_page_free(j);

ui_run();
ui_page_destroy(page);
```

- **250+ 导出函数**，句柄全部 `uint64_t`，没有一个 C++ 类型泄漏
- Rust / Go / Python / C# / Delphi / Pascal / Lua 全部能直接绑定
- 任意 widget（含 `<custom>` 自绘 widget）都能挂 `ui_widget_on_mouse_*` / `on_focus` / `on_wheel` 等全套事件回调，不再受"只有 button / input 能拿 onclick"的限制

### 🔍 自动化 / 调试：控件可编程驱动

```c
ui_debug_click(win, btn);                 // 完整 mouse down/up，触发 onClick
ui_debug_combo_select(win, combo, 2);     // 选中第 3 项 + 触发 onChanged
ui_debug_right_click_at(win, 300, 200);   // 右键弹出注册的 context menu
ui_debug_type_text(win, L"hello");        // 逐字符键盘输入
```

- **60+ 个 `ui_debug_*` 函数**（click / hover / drag / wheel / key / focus / 子菜单 path 点击 …）+ 内置 **Named Pipe IPC**（45+ 命令），PowerShell / Python 一行驱动，为端到端测试、AI Agent 操作 UI、脚本化回归设计
- 完整参考见 **[`docs/debug-simulation.md`](./docs/debug-simulation.md)**

## 🚀 快速开始

### 环境要求

- Windows 10 (1709+) · CMake 3.20+ · **MSVC 2019+ 或 clang-cl**（C++17）— 不支持 MinGW

### 构建

构建必须从 PowerShell 调用项目自带脚本（自动配置 vcvars64、用 llvm-rc 绕过 Windows SDK rc.exe 在 ninja 子进程里挂死的 bug）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File scripts/build-clang-cl.ps1 -Target core-ui
```

| Target | 产物 |
|---|---|
| `core-ui` | `core-ui.dll` + `core-ui.lib` 导入库（默认） |
| `core-ui-static` | `core-ui-static.lib` 自包含静态归档（含 QuickJS） |
| `ui-demo-uix` | `ui-demo-uix.exe` 单文件 demo（资源烤进 exe） |
| `golden_runner` | `golden_runner.exe` 黄金图回归测试 |

加 `-Clean` 强制重建 build 目录；省略 `-Target` 编全部；`-Static` 产单 exe（无 DLL 依赖）。

### Hello World

把上面 `.uix` 单文件组件那段存成 `hello.uix`，再写十行 C 胶水即可跑起来：

```cpp
#include <ui_core.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    ui_init_with_theme(UI_THEME_LIGHT);

    UiPage page = ui_page_load_file(L"hello.uix");
    if (!page) return 1;

    UiWindow win = ui_page_open_window(page, NULL);
    if (!win) { ui_page_destroy(page); return 2; }

    int ret = ui_run();
    ui_page_destroy(page);
    return ret;
}
```

就这么多。**没有 .vcxproj、没有 moc 预处理器、没有 XAML 编译器、没有 IDL。**

### 单 exe 打包（资源烤进可执行文件）

```cmake
include(cmake/UiCoreHelpers.cmake)
add_executable(my-app WIN32 main.cpp)
target_link_libraries(my-app PRIVATE core-ui)
ui_core_embed_text(my-app FILE app.uix      OUT app.embed.h     VAR k_app)
ui_core_embed_text(my-app FILE lang/zh.lang OUT lang_zh.embed.h VAR k_lang_zh)
```

```cpp
UiPage page = ui_page_load_string(k_app);
ui_page_load_language_string(page, "zh", k_lang_zh);
```

`demo/ui_demo_uix.cpp` 就是这种用法的最小完整例子（57 行胶水 + 单 `.uix` 文件 12 页 demo）。

## 🧩 内置控件 / 标签

`.uix` 模板里的标签直接映射到原生 widget：

| 类别 | 标签 |
|---|---|
| **容器** | `div`（Flexbox：`flex-direction` / `flex` / `gap` / `padding`）|
| **文本** | `label`（多行、自动换行）|
| **按钮** | `button`、`IconButton` |
| **输入** | `input`（type=`text` / `password` / `checkbox` / `radio` / `range` / `number`）、`textarea` |
| **选择** | `toggle`、`combobox` |
| **状态** | `progressbar`，`badge` 类（CSS）|
| **弹出** | `menu` / `menuitem` / `separator`、`Flyout`、`Dialog`、`Toast` |
| **图像** | `img`、`svg`（内联），底层 `ImageView` 支持缩放 / 平移 / 裁剪 |
| **窗口** | `TitleBar`（仅 `frameless="true"` 时使用） |

需要程序化构造时，C API 也提供 `ui_vbox` / `ui_hbox` / `ui_label` / `ui_button` / `ui_text_input` / `ui_combobox` / `ui_slider` / `ui_progress_bar` / `ui_image_view` / `ui_scroll_view` 等工厂函数。

## 🎨 主题

内置 Fluent 2 深色 / 浅色主题，运行时一行切换：

```c
ui_theme_set_mode(UI_THEME_DARK);
ui_theme_set_mode(UI_THEME_LIGHT);
```

`.uix` 的 `<style>` 用 CSS 变量引用主题色（`var(--bg)` / `var(--fg)` / `var(--accent)` / `var(--card-bg)` / `var(--border-subtle)` …），切主题时由库重新 cascade，所有控件自动响应。

## 版本号

版本号格式：`MAJOR.MINOR.PATCH.BUILD`，运行时可查：

```c
int major, minor, patch;
ui_core_version(&major, &minor, &patch);   // 1, 6, 0
const char* v = ui_core_version_string();   // "1.6.0.170"
```

## 适用场景

- ✅ **Windows 工具类桌面应用**（下载器、图片查看器、配置管理器、资料库工具）
- ✅ **需要 Fluent 外观但不愿被 .NET / WinUI 绑架**的原生项目
- ✅ **Rust / Go / Python 想要 Fluent 界面**但找不到合适绑定
- ✅ **嵌入到现有 C++ 项目**作为 UI 层（无第三方运行时）
- ✅ **离线分发**，体积敏感，不能带 Electron 全家桶
- ✅ **AI 驱动的 UI 生成**：把 `.uix` 当目标格式，闭环 emit → build → click → screenshot

## 📝 许可证

[MIT License](./LICENSE) © core-ui contributors
