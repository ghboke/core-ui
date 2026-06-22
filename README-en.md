<p align="center">
  <img src="./logo.svg" alt="CORE UI" height="64">
</p>

<p align="center">
  <b>English</b> · <a href="./README.md">中文</a>
  &nbsp;·&nbsp;
  <a href="https://ghboke.github.io/core-ui/"><b>📖 Online Docs</b></a>
</p>

**Core UI** is a modern Windows desktop UI framework. Rendering runs on **Direct2D / Direct3D 11** hardware acceleration, aligned with Microsoft's **Fluent 2** visual language, and every widget — from buttons and text fields to Flyout, Dialog, and TitleBar (25+ built-ins) — is exposed through a single **pure C API (250+ functions)**, so Rust, Go, Python, C#, Delphi, and even Lua can bind it directly without a C++ shim. UIs are best described in **`.uix` single-file components** — Vue 3 SFC style (`<window>` + `<script>` + `<style>` + `<template>`) with reactive bindings, `v-if` / `v-for` / `v-model` / `@click`, a CSS subset, and CSS-variable theming. Scripts are evaluated in-process by an embedded **QuickJS-NG** runtime — no DOM, no Webview.

> **A 3.0 MB single DLL that ships Office / VS Code-grade UI on Windows.**
> No Chromium. No .NET. No 40 MB of Qt DLLs and moc/uic preprocessors. One C header, one `.uix` single-file component — done.

![version](https://img.shields.io/badge/version-1.6.0.170-blue)
![license](https://img.shields.io/badge/license-MIT-green)
![platform](https://img.shields.io/badge/platform-Windows%2010%2B-lightgrey)
![size](https://img.shields.io/badge/dll-3.0MB-brightgreen)
![api](https://img.shields.io/badge/C%20API-250%2B-orange)

## 🎯 Why Core UI

| Dimension | Electron | WPF / WinUI 3 | Qt | **Core UI** |
|-----------|----------|---------------|-----|-------------|
| **Distribution size** | 100+ MB | needs .NET runtime | 40+ MB Qt DLLs | **3.0 MB single DLL** |
| **Startup time** | 1–3 s | 0.5–1 s | 0.5–1 s | **< 200 ms** |
| **Memory footprint** | 150+ MB | 80+ MB | 60+ MB | **< 30 MB** |
| **Language bindings** | JS only | .NET only | C++ only | **C ABI, any language** |
| **Design language** | DIY | Fluent (limited) | Platform-native | **Fluent 2, native-grade** |
| **Declarative / reactive UI** | JSX + virtual DOM | XAML + Binding | QML | **`.uix` Vue 3 SFC (QuickJS-NG)** |
| **Learning curve** | full-stack JS | XAML + C# | C++ + meta object | **Vue templates + C, instant** |

You want Electron's DX + native-level performance + Fluent 2 looks + Vue's reactivity model — Core UI is currently the only option that checks all those boxes.

## 🤖 Built for the AI Era

**Core UI's architecture makes LLMs get it right**: `.uix` is just Vue 3 SFC (training data is dominated by Vue / HTML far more than any desktop UI), the pure C ABI uses `uint64_t` handles everywhere (no templates / inheritance / virtuals — LLMs almost never hallucinate a type error), CSS + Flexbox transfers frontend layout intuition directly, and every API follows `ui_<noun>_<verb>` so it's highly predictable. An agent only needs to fetch one file to emit a complete, runnable app:

| Documentation entry | Description |
|------|-------------|
| **[`llms.txt`](./llms.txt)** | [llmstxt.org](https://llmstxt.org) standard index — the first file an agent should fetch |
| **[`docs/uix-ai-guide.md`](./docs/uix-ai-guide.md)** | **Self-contained cheatsheet**: `.uix` structure + template + script + CSS subset + widget list + examples |
| **[`docs/uix-guide.md`](./docs/uix-guide.md)** | Vue 3 SFC complete guide, cookbook, limitations |
| **[`docs/debug-simulation.md`](./docs/debug-simulation.md)** | `ui_debug_*` event injection + Named Pipe IPC, AI self-verification loop |
| **[`UI_CORE_API.md`](./UI_CORE_API.md)** | 250+ exported functions, grouped by module |

> **Cursor / Claude Code / Cline / Continue users:** add `docs/uix-ai-guide.md` to your project rules (`.cursorrules` / `CLAUDE.md`) for full coverage in a single context. Docs are currently authored in Chinese; English translations are planned.

## Core Features

### 🚀 Ridiculously small, absurdly fast

- **3.0 MB full DLL**, or a **~2 MB statically-linked exe** — it fits on a USB stick
- **Direct2D + Direct3D 11** full hardware acceleration, Per-Monitor DPI V2 out of the box
- **Cold start < 200 ms**, empty-window memory **< 30 MB**, 60 fps animation CPU usage **< 3%**

### 🎨 Looks that sell

- Strictly aligned with **Microsoft Fluent 2 design tokens**: colors, radii, shadows, motion — no shortcuts
- Dark / light theme switches with **one line** of C; CSS variables re-cascade automatically
- **Custom borderless window** ships with a `<TitleBar>` control, native drag / snap / animation
- 25+ controls match WinUI 3's granularity: `button` / `input` / `toggle` / `combobox` / `progressbar` / `menu` / `Dialog` / `Toast` ...

### 🧩 `.uix` single-file components — write desktop UI like Vue

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

- **Vue 3 Options API**: `data()` / `computed` / `methods`, evaluated by QuickJS-NG (ES2020+)
- **Reactivity**: Proxy + WatchEffect; templates auto-track dependencies via `{{ expr }}` / `:attr` / `v-if` / `v-for` / `v-model` / `@click` and re-render incrementally
- **CSS subset**: class / element / descendant selectors, pseudo-classes (`:hover`, `:disabled`), Flexbox, `var(--*)` CSS-variable theming
- **Built-in i18n**: `{{ $t('welcome') }}` resolves from `.lang` files; switch at runtime via `ui_page_set_locale`
- **Declarative right-click menus**: `<menu trigger="#id" event="rclick">` + `<menuitem>` / `<separator>`

### 🌐 Pure C API — every language is welcome

```c
#include <ui_core.h>

ui_init_with_theme(UI_THEME_LIGHT);

UiPage page = ui_page_load_file(L"app.uix");
ui_page_set_locale(page, "zh");
UiWindow win = ui_page_open_window(page, NULL);

/* Two-way exchange of reactive state */
ui_page_set_int (page, "count", 42);
ui_page_set_json(page, "items", "[{\"id\":1,\"label\":\"a\"}]");
char* j = ui_page_get_json(page, "items");
ui_page_free(j);

ui_run();
ui_page_destroy(page);
```

- **250+ exported functions**, all handles are plain `uint64_t` — zero C++ types leak through
- Rust / Go / Python / C# / Delphi / Pascal / Lua can all bind directly
- Any widget (including custom-drawn `<custom>` widgets) can receive the full set of event callbacks — `ui_widget_on_mouse_*` / `on_focus` / `on_wheel` — not just `button` / `input`

### 🔍 Automation / debugging: controls are programmable

```c
ui_debug_click(win, btn);                    // full mouse down/up, fires onClick
ui_debug_combo_select(win, combo, 2);        // select item 2 + fire onChanged
ui_debug_right_click_at(win, 300, 200);      // pop up the registered context menu
ui_debug_type_text(win, L"hello");           // per-character keyboard input
```

- **60+ `ui_debug_*` functions** (click / hover / drag / wheel / key / focus / submenu path click …) plus a built-in **Named Pipe IPC** (45+ commands) — drive from PowerShell / Python in one line. Designed for end-to-end tests, AI agents operating UIs, and scripted regressions.
- Full reference in **[`docs/debug-simulation.md`](./docs/debug-simulation.md)**

## 🚀 Getting Started

### Requirements

- Windows 10 (1709+) · CMake 3.20+ · **MSVC 2019+ or clang-cl** (C++17) — MinGW is not supported

### Build

Builds must be invoked from PowerShell using the bundled script (it sets up vcvars64 and routes around a Windows SDK rc.exe hang in ninja subprocesses by swapping in llvm-rc):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File scripts/build-clang-cl.ps1 -Target core-ui
```

| Target | Artifact |
|---|---|
| `core-ui` | `core-ui.dll` + `core-ui.lib` import library (default) |
| `core-ui-static` | `core-ui-static.lib` self-contained static archive (bundles QuickJS) |
| `ui-demo-uix` | `ui-demo-uix.exe` single-file demo (resources baked in) |
| `golden_runner` | `golden_runner.exe` golden-image regression runner |

Pass `-Clean` to rebuild from scratch; omit `-Target` to build everything; `-Static` produces a single exe (no DLL dependency).

### Hello World

Save the `.uix` component above as `hello.uix`, then write ten lines of C glue to run it:

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

That's it. **No `.vcxproj`, no `moc` preprocessor, no XAML compiler, no IDL.**

### Single-exe packaging (resources baked into the executable)

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

`demo/ui_demo_uix.cpp` is the smallest complete example of this pattern (57 lines of glue + a single `.uix` file with 12 demo pages).

## 🧩 Built-in Tags / Controls

Tags inside `.uix` templates map directly to native widgets:

| Category | Tags |
|---|---|
| **Containers** | `div` (Flexbox: `flex-direction` / `flex` / `gap` / `padding`) |
| **Text** | `label` (multi-line, auto-wrap) |
| **Buttons** | `button`, `IconButton` |
| **Input** | `input` (type=`text` / `password` / `checkbox` / `radio` / `range` / `number`), `textarea` |
| **Selection** | `toggle`, `combobox` |
| **Status** | `progressbar`, `badge` (CSS class) |
| **Popups** | `menu` / `menuitem` / `separator`, `Flyout`, `Dialog`, `Toast` |
| **Image** | `img`, `svg` (inline); the underlying `ImageView` supports zoom / pan / crop |
| **Window** | `TitleBar` (only when `frameless="true"`) |

For procedural construction, the C API also offers factories: `ui_vbox` / `ui_hbox` / `ui_label` / `ui_button` / `ui_text_input` / `ui_combobox` / `ui_slider` / `ui_progress_bar` / `ui_image_view` / `ui_scroll_view`, etc.

## 🎨 Theming

Built-in Fluent 2 dark / light themes, one-line runtime switch:

```c
ui_theme_set_mode(UI_THEME_DARK);
ui_theme_set_mode(UI_THEME_LIGHT);
```

`.uix` `<style>` blocks reference theme colors via CSS variables (`var(--bg)` / `var(--fg)` / `var(--accent)` / `var(--card-bg)` / `var(--border-subtle)` …) — the library re-cascades on theme change so all controls follow.

## Versioning

Version format: `MAJOR.MINOR.PATCH.BUILD`, queryable at runtime:

```c
int major, minor, patch;
ui_core_version(&major, &minor, &patch);   // 1, 6, 0
const char* v = ui_core_version_string();   // "1.6.0.170"
```

## Where It Fits

- ✅ **Windows utility apps** (downloaders, image viewers, config managers, data tools)
- ✅ **Native projects that want Fluent looks** without getting locked into .NET / WinUI
- ✅ **Rust / Go / Python projects** that need a Fluent UI but can't find a solid binding
- ✅ **Embedding into existing C++ projects** as the UI layer — no third-party runtime
- ✅ **Size-sensitive offline distribution** where shipping Electron is not an option
- ✅ **AI-driven UI generation**: target `.uix` as the output format and close the emit → build → click → screenshot loop

## 📝 License

[MIT License](./LICENSE) © core-ui contributors
