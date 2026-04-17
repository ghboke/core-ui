<p align="center">
  <img src="./logo.svg" alt="UI Core" height="64">
</p>

<p align="center">
  <b>English</b> · <a href="./README.md">中文</a>
</p>

**UI Core** is a modern Windows desktop UI framework, rebuilt from the ground up to match Microsoft's **Fluent 2** visual language while keeping **native-level performance** and a **tiny distribution footprint**. Rendering runs on **Direct2D / Direct3D 11** hardware acceleration, and every widget — from buttons and text fields to SplitView, Flyout, and Expander — is exposed through a single **pure C API (187+ functions)**, so Rust, Go, Python, C#, Delphi, and even Lua can bind it directly without writing a C++ shim.

UIs are best described in **`.ui` markup files** — a XAML-like declarative format with CSS-style selectors, data binding, internationalization, and **hot reload** — so building a UI feels more like writing HTML than wiring up widgets. The project is also purpose-built for **AI-driven development**: a single self-contained cheatsheet ([`docs/ai-guide.md`](./docs/ai-guide.md)) lets an LLM read it once and emit a complete, runnable app, turning "describe what you want" into "see it running" in a single loop.

> **An 8.4 MB single DLL that ships Office / VS Code-grade UI on Windows.**
> No Chromium. No .NET. No 40 MB of Qt DLLs and moc/uic preprocessors. One C header, one `.ui` markup file — done.

![version](https://img.shields.io/badge/version-1.0.0.1-blue)
![license](https://img.shields.io/badge/license-MIT-green)
![platform](https://img.shields.io/badge/platform-Windows%2010%2B-lightgrey)
![size](https://img.shields.io/badge/dll-8.4MB-brightgreen)
![api](https://img.shields.io/badge/C%20API-187%2B-orange)

## 🎯 Why UI Core

| Dimension | Electron | WPF / WinUI 3 | Qt | **UI Core** |
|-----------|----------|---------------|-----|-------------|
| **Distribution size** | 100+ MB | needs .NET runtime | 40+ MB Qt DLLs | **8.4 MB single DLL** |
| **Startup time** | 1–3 s | 0.5–1 s | 0.5–1 s | **< 200 ms** |
| **Memory footprint** | 150+ MB | 80+ MB | 60+ MB | **< 30 MB** |
| **Language bindings** | JS only | .NET only | C++ only | **C ABI, any language** |
| **Design language** | DIY | Fluent (limited) | Platform-native | **Fluent 2, native-grade** |
| **Hot reload** | ❌ | ❌ | ❌ | **✅ `.ui` files** |
| **Learning curve** | full-stack JS | XAML + C# | C++ + meta object | **C + XML, instant** |

**In one line:** You want Electron's DX + native-level performance + Fluent 2 looks + Qt-level control coverage — UI Core is currently the only option that checks all four boxes.

## 🤖 Fast UI Development for the AI Era

**UI Core is designed from the ground up to be driven by LLMs and AI agents.**

Everybody is having AI write code now. Try asking Claude / GPT / Cursor to build a Qt app — it'll point you at `moc`, include a pile of unnecessary headers, and misremember signal/slot syntax. Try WPF — it will blend XAML, C# code-behind, and `x:Bind` expressions. The reason is simple: **the cognitive surface of these frameworks was never designed with a token budget in mind.**

UI Core is architected so agents get it right:

| Design decision | Win for AI |
|-----------------|-----------|
| **Pure C ABI, every handle is `uint64_t`** | No C++ templates, no inheritance, no virtuals — LLMs almost never hallucinate a type error |
| **Declarative `.ui` XML** | UI is a tree of tagged elements — the thing LLMs are best at; 3–5× shorter than imperative code |
| **Every API follows `ui_<noun>_<verb>`** | Highly predictable: "disable a button" → `ui_widget_set_enabled`, guessed right on the first try |
| **A single self-contained cheatsheet** | Agents only need to fetch `docs/ai-guide.md`; no repo-wide crawl required |
| **Hot-reload for `.ui` files** | Agents can iterate on the UI without a rebuild — faster feedback loop |
| **187+ exports in the DLL symbol table** | Agents can `objdump -p ui-core.dll` to ground-truth the current API surface |
| **All text uses `@key` or `{bind}`** | AI-generated UI is i18n-ready and data-driven by default, not hard-coded |

Typical workflow:

```
User: "Build me a Windows desktop download-manager UI with a Fluent look"
  ↓
Agent fetches docs/ai-guide.md   (one file — all the knowledge)
  ↓
Agent emits   app.ui + main.cpp  (declarative XML + 10 lines of C)
  ↓
cmake --build build              (compiler is the ground truth, no LLM self-check)
  ↓
Tweak .ui, hot-reload to polish  (no re-codegen)
```

### 📖 AI Documentation Entry Points

| Entry | Audience | Description |
|-------|----------|-------------|
| **[`llms.txt`](./llms.txt)** | AI agent crawlers | [llmstxt.org](https://llmstxt.org) standard index — the first file an agent should fetch |
| **[`docs/ai-guide.md`](./docs/ai-guide.md)** | LLMs writing code | **Self-contained cheatsheet**: API + tags + attributes + 3 runnable examples + gotchas |
| **[`UI_CORE_API.md`](./UI_CORE_API.md)** | Deep dives | Full list of 187+ exported functions, grouped by module |

**Cursor / Claude Code / Cline / Continue users:** add `docs/ai-guide.md` to your project rules (Cursor's `.cursorrules`, Claude Code's `CLAUDE.md`) for full coverage in a single context.

Copy-paste prompt template:

```
This project uses the UI Core framework. Read docs/ai-guide.md before generating code.
Constraints:
  1. Prefer .ui markup to describe the UI; use C++ only for event handlers and data binding.
  2. Reference colors via theme.*; never hard-code hex values.
  3. Reference text via @key into .lang files; never hard-code user-facing strings.
  4. Control factories: ui_vbox / ui_hbox / ui_button / ui_label … (see ai-guide §4.4).
```

## ✨ Core Features

### 🚀 Ridiculously small, absurdly fast

- **8.4 MB full DLL**, or a **1 MB statically-linked exe** — it fits on a USB stick
- **Direct2D + Direct3D 11** full hardware acceleration, Per-Monitor DPI V2 out of the box
- **Cold start < 200 ms** — click and the window is already there

### 🎨 Looks that sell

- Strictly aligned with **Microsoft Fluent 2 design tokens**: colors, radii, shadows, motion — no shortcuts
- Dark / light theme switches with **one line** of C, every control follows automatically
- **Custom borderless window** ships with a TitleBar control, native drag / snap / animation
- 25+ controls match WinUI 3's granularity: Expander, Flyout, SplitView, NavItem, NumberBox, …

### 🧩 Declarative UI, write it like HTML

```xml
<ui version="1" width="400" height="300">
  <VBox gap="16" padding="32" expand="true">
    <Label text="Hello, UI Core!" fontSize="24" bold="true" />
    <Button text="Click Me" type="primary" onClick="onBtn" />
  </VBox>
</ui>
```

- **XAML / QML-like** `.ui` markup with **hot reload**
- **CSS-like style system**: class / element / descendant selectors, `theme.*` color refs
- **Data binding**: `text="{variable}"` + `SetText/SetFloat/SetBool`
- **List rendering**: `<Repeater model="{list}">` templates
- **Composition**: `<Include src="header.ui" />` for reuse
- **Built-in i18n**: `text="@welcome"` automatically resolves from `.lang` files

### 🌐 Pure C API — every language is welcome

```c
#include <ui_core.h>

ui_init();
UiWidget btn = ui_button(L"OK");
ui_widget_on_click(btn, my_cb, NULL);
// … call from any language that can load a DLL
```

- **187+ exported functions**, all handles are plain `uint64_t` — zero C++ types leak through
- Rust / Go / Python / C# / Delphi / Pascal / Lua can all bind directly
- Windows, widgets, animations, theming, IME, drag-and-drop — everything is in the C API

### 🔍 Live debugging, out-of-process

- Built-in **Named Pipe IPC**: external tools can query the widget tree, highlight widgets, switch pages
- The `ui-debug` protocol is documented — pair it with a custom tool or a VS Code extension for visual debugging

## 📊 Real Numbers

| Metric | Value |
|--------|-------|
| `ui-core.dll` size (full feature) | **8.4 MB** |
| `ui-demo.exe` size (static link) | **1.0 MB** |
| Empty window memory | **< 30 MB** |
| Cold start to first frame | **< 200 ms** |
| 60 fps animation CPU usage | **< 3%** |
| Exported C functions | **187+** |
| Built-in controls | **25+** |
| Image formats (ImageView) | PNG / JPG / BMP / GIF / WebP |

## 🚀 Getting Started

### Requirements

- Windows 10+
- CMake 3.20+
- MSVC 2019+ or MinGW-w64 (C++17)

### Build

```bash
git clone https://github.com/<your-account>/ui-core.git
cd ui-core
cmake -B build -G Ninja
cmake --build build
```

Artifacts:

| File | Description |
|------|-------------|
| `build/ui-core.dll` | Shared library (version `1.0.0.1` embedded) |
| `build/ui-demo.exe` | Demo application |

Single-exe distribution (zero dependencies):

```bash
cmake -B build -G Ninja -DUI_CORE_STATIC=ON
cmake --build build
```

### Hello World

**app.ui**

```xml
<ui version="1" width="400" height="300" title="Hello">
  <VBox gap="16" padding="32" expand="true">
    <Label text="Hello, UI Core!" fontSize="24" bold="true" />
    <Button text="Click Me" type="primary" onClick="onBtn" />
  </VBox>
</ui>
```

**main.cpp**

```cpp
#include <ui_core.h>
#include "src/ui/markup/markup.h"

static ui::UiMarkup layout;

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    ui_init();

    layout.SetHandler("onBtn", std::function<void()>([]() {
        // button clicked
    }));
    layout.LoadFile(L"app.ui");

    UiWindowConfig cfg = {0};
    cfg.title  = L"Hello";
    cfg.width  = 400;
    cfg.height = 300;
    UiWindow win = ui_window_create(&cfg);

    auto& ctx = ui::GetContext();
    ui_window_set_root(win, ctx.handles.Insert(layout.Root()));
    ui_window_show(win);

    int ret = ui_run();
    ui_shutdown();
    return ret;
}
```

That's it. **No `.vcxproj`, no `moc` preprocessor, no XAML compiler, no IDL.**

## 🧩 Control Reference

### Containers
| Control | Description |
|---------|-------------|
| `VBox` / `HBox` | Vertical / horizontal layout |
| `Grid` | Grid layout |
| `Stack` | Stack (shows one child at a time) |
| `ScrollView` | Scrollable container |
| `SplitView` | Collapsible sidebar navigation |
| `Panel` | Free-positioning container |
| `Expander` | Collapsible / expandable panel |

### Input
| Control | Description |
|---------|-------------|
| `Button` / `IconButton` | Button / icon button |
| `TextInput` / `TextArea` | Single / multi-line text input |
| `NumberBox` | Number input with stepper |
| `CheckBox` / `RadioButton` | Checkbox / radio |
| `Toggle` | Toggle switch |
| `Slider` | Slider |
| `ComboBox` | Dropdown select |

### Display
| Control | Description |
|---------|-------------|
| `Label` | Text (multi-line, auto wrap) |
| `ProgressBar` | Determinate / indeterminate progress |
| `ImageView` | Image (zoom, pan, crop, GIF) |
| `Separator` | Separator line |

### Navigation / Popup
| Control | Description |
|---------|-------------|
| `TitleBar` | Borderless window title bar |
| `NavItem` | Sidebar nav item (SVG icon) |
| `TabControl` | Tabs |
| `Flyout` | Popup flyout |
| `ContextMenu` | Right-click menu |
| `Dialog` | Modal dialog |
| `Toast` | Transient toast |

## 🎨 Theming

Built-in Fluent 2 dark/light themes, one-line runtime switch:

```c
ui_theme_set_mode(UI_THEME_DARK);
ui_theme_set_mode(UI_THEME_LIGHT);
```

All controls respond automatically. Reference theme colors from markup via `theme.*`:

```
theme.accent / theme.windowBg / theme.sidebarBg / theme.cardBg
theme.contentText / theme.divider / theme.inputBg / theme.inputBorder
```

## 🔢 Versioning

Version format: `MAJOR.MINOR.PATCH.BUILD` (currently `1.0.0.1`)

- Compile-time macros: `UI_CORE_VERSION_MAJOR/MINOR/PATCH/BUILD` + `UI_CORE_VERSION_STRING`
- Runtime API:

```c
int major, minor, patch;
ui_core_version(&major, &minor, &patch);   // 1, 0, 0
int build = ui_core_version_build();        // 1
const char* v = ui_core_version_string();   // "1.0.0.1"
```

- Windows DLL properties (right-click `ui-core.dll` → Properties → Details) show `FileVersion 1.0.0.1`
- Full release history in [CHANGELOG.md](./CHANGELOG.md)

## 📁 Project Layout

```
ui-core/
├── include/           # Public headers (ui_core.h, plugin_api.h)
├── src/
│   ├── version.rc.in  # Windows version resource template
│   └── ui/            # Framework implementation
│       ├── renderer.*     # Direct2D renderer
│       ├── widget.*       # Base widget + layout
│       ├── controls.*     # Built-in controls
│       ├── ui_window.*    # Window + event dispatch
│       ├── ui_api.cpp     # C API implementation
│       ├── animation.*    # Animation system
│       ├── context_menu.* # Right-click menu
│       └── markup/        # .ui parser
├── demo/              # Demo app
├── docs/              # Usage docs
├── test/              # Unit tests
├── CMakeLists.txt
├── VERSION
└── CHANGELOG.md
```

## 📚 Documentation

| Doc | Content |
|-----|---------|
| [Getting Started](docs/getting-started.md) | Integration guide |
| [Markup](docs/markup.md) | `.ui` syntax |
| [C API](docs/c-api.md) | Pure C reference |
| [Controls](docs/controls.md) | Control details |
| [Layout](docs/layout.md) | Layout system |
| [Design System](docs/design-system.md) | Fluent 2 tokens |
| [i18n](docs/i18n.md) | Multi-language support |
| [API Index](UI_CORE_API.md) | Full exported function list |

> Note: docs are currently authored in Chinese. English translations are planned.

## 🤝 Where It Fits

- ✅ **Windows utility apps** (downloaders, image viewers, config managers, data tools)
- ✅ **Native projects that want Fluent looks** without getting locked into .NET / WinUI
- ✅ **Rust / Go / Python projects** that need a Fluent UI but can't find a solid binding
- ✅ **Embedding into existing C++ projects** as the UI layer — no third-party runtime
- ✅ **Size-sensitive offline distribution** where shipping Electron is not an option

## 📝 License

[MIT License](./LICENSE) © ui-core contributors

— If this project saved you from shipping another Electron app, **please drop a Star ⭐**.
