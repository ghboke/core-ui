<p align="center">
  <img src="./logo.svg" alt="UI Core" height="64">
</p>

<p align="center">
  <a href="./README-en.md">English</a> · <b>中文</b>
</p>

**UI Core** 是一个现代化的 Windows 桌面 UI 框架，从零重新设计以匹配 Microsoft **Fluent 2** 视觉语言，同时保持**原生级的性能**与**极小的分发体积**。底层基于 **Direct2D / Direct3D 11** 硬件加速渲染，把从按钮、文本框到 SplitView、Flyout、Expander 的 25+ 个内置控件统一在一套**纯 C API（187+ 函数）**之下——Rust、Go、Python、C#、Delphi 乃至 Lua 都能直接调用，不需要写 C++ 绑定层。

推荐用类 XAML 的 **`.ui` 标记文件**描述界面，支持 CSS 风格的样式选择器、数据绑定、国际化、**热重载**——写 UI 不再是堆代码，而是像写 HTML 一样声明一棵节点树。项目还专门为 **AI 协作**而设计：一份自包含的速查文档（[`docs/ai-guide.md`](./docs/ai-guide.md)）让 LLM 一次读完就能生成完整的可运行应用，真正做到"描述需求 → 得到界面"的极速原型闭环。

> **8.4 MB 一个 DLL，就能写出跟 Office / VSCode 同一设计语言的 Windows 桌面应用。**
> 不要 Chromium、不要 .NET、不要 Qt 几十兆的 moc/uic——一个 C 头文件，一份 `.ui` 标记文件，搞定。

![version](https://img.shields.io/badge/version-1.0.0.1-blue)
![license](https://img.shields.io/badge/license-MIT-green)
![platform](https://img.shields.io/badge/platform-Windows%2010%2B-lightgrey)
![size](https://img.shields.io/badge/dll-8.4MB-brightgreen)
![api](https://img.shields.io/badge/C%20API-187%2B-orange)

## 🎯 为什么选 UI Core

| 对比维度 | Electron | WPF / WinUI 3 | Qt | **UI Core** |
|---------|----------|---------------|-----|-------------|
| **分发体积** | 100+ MB | 需要 .NET 运行时 | 40+ MB Qt DLLs | **8.4 MB 单 DLL** |
| **启动时间** | 1–3 秒 | 0.5–1 秒 | 0.5–1 秒 | **< 200 ms** |
| **内存占用** | 150+ MB | 80+ MB | 60+ MB | **< 30 MB** |
| **语言绑定** | 只能 JS | 只能 .NET | 只能 C++ | **C ABI，任意语言** |
| **设计规范** | 自己画 | Fluent（受限） | 类原生 | **Fluent 2 原生级** |
| **热重载** | ❌ | ❌ | ❌ | **✅ `.ui` 文件** |
| **学习曲线** | 大前端生态 | XAML + C# | C++ + Meta 对象 | **C + XML 即上手** |

**一句话总结**：想要 Electron 的开发体验 + 原生的性能 + Fluent 2 的颜值 + Qt 的控件齐全——UI Core 是目前唯一同时打满四格的方案。

## 🤖 AI 时代的快速 UI 开发方案

**UI Core 是为 LLM 和 AI Agent 从零设计的 UI 框架。**

现在大家都让 AI 写代码。你试过让 Claude / GPT / Cursor 写一个 Qt 应用吗？它会告诉你去看 `moc`、帮你 include 一堆用不上的头、还经常搞错信号槽语法。写 WPF？它会把 XAML、C# code-behind、x:Bind 表达式混着编。原因很简单：**这些框架的心智负担不是为 token 预算设计的**。

UI Core 的架构天然让 AI 做对：

| 设计决策 | AI 收益 |
|---------|---------|
| **纯 C ABI，全部 `uint64_t` 句柄** | 没有 C++ 模板、没有继承、没有虚函数——LLM 极难幻觉出类型错误 |
| **声明式 `.ui` XML** | UI 就是一棵标签树，LLM 最擅长的事；比 imperative 代码短 3~5 倍 |
| **全部 API 遵循 `ui_<名词>_<动词>`** | 可被精确预测：想禁用按钮？`ui_widget_set_enabled` 几乎一次猜对 |
| **一份 `docs/ai-guide.md` 自包含速查** | Agent 只需 fetch 这一个文件，就能写出完整应用，不必遍历仓库 |
| **热重载 `.ui` 文件** | Agent 改完无需重编译即可看效果，闭环更快 |
| **187+ 导出函数全部进 DLL 符号表** | Agent 可以动态检查 `objdump -p ui-core.dll` 知道当前版本真实 API |
| **所有文本走 `@key` 或 `{bind}`** | Agent 生成的 UI 天然多语言 / 数据驱动，不写死 |

典型工作流：

```
用户："帮我做一个 Windows 桌面下载器 UI，要 Fluent 外观"
  ↓
AI fetch  docs/ai-guide.md   （一个文件，全部知识）
  ↓
AI 生成   app.ui + main.cpp  （声明式 XML + 10 行 C）
  ↓
cmake --build build         （编译器兜底，LLM 不用自己验证）
  ↓
右键改 .ui 文件热重载调样式   （不必重新生成代码）
```

### 📖 AI 文档入口

| 入口 | 给谁用 | 说明 |
|------|-------|------|
| **[`llms.txt`](./llms.txt)** | AI Agent 爬虫 | [llmstxt.org](https://llmstxt.org) 标准索引，Agent 第一步 fetch 这个 |
| **[`docs/ai-guide.md`](./docs/ai-guide.md)** | LLM 编程 | **自包含速查表**：API + 标签 + 属性 + 3 个完整例子 + 常见坑 |
| **[`UI_CORE_API.md`](./UI_CORE_API.md)** | 深入场景 | 187+ 导出函数完整清单，按模块分组 |

**Cursor / Claude Code / Cline / Continue 等用户推荐把 `docs/ai-guide.md` 加到项目规则里**（Cursor 的 `.cursorrules`、Claude Code 的 `CLAUDE.md`），一次上下文全覆盖。

提示词模板（复制即用）：

```
本项目使用 UI Core 框架。调用前先阅读 docs/ai-guide.md。
约束：
  1. 优先使用 .ui 标记文件描述 UI，C++ 只写事件处理和数据绑定。
  2. 所有颜色用 theme.* 引用，不要硬编码。
  3. 所有文本用 @key 引用 .lang 文件，不要写死字符串。
  4. 控件工厂函数：ui_vbox / ui_hbox / ui_button / ui_label …（见 ai-guide 第 4.4 节）
```

## ✨ 核心特性

### 🚀 轻到离谱，快到失真

- **8.4 MB 全量 DLL**，静态编译后 demo exe 仅 **1 MB**，可装进 U 盘跑
- **Direct2D + Direct3D 11** 全硬件加速，Per-Monitor DPI V2 一次画对
- **冷启动 < 200ms**，对标记文件路径点一下就能看到窗口

### 🎨 颜值即正义

- 严格对齐 **Microsoft Fluent 2 Design Token**：色彩、圆角、阴影、动画无一例外
- 深色 / 浅色主题**一行切换**，所有内置控件自动响应
- **自定义无边框窗口**自带 TitleBar 控件，系统级拖拽 / 贴靠 / 动画
- 25+ 控件颗粒度对标 WinUI 3：Expander、Flyout、SplitView、NavItem、NumberBox…

### 🧩 声明式 UI，像写 HTML 一样

```xml
<ui version="1" width="400" height="300">
  <VBox gap="16" padding="32" expand="true">
    <Label text="Hello, UI Core!" fontSize="24" bold="true" />
    <Button text="Click Me" type="primary" onClick="onBtn" />
  </VBox>
</ui>
```

- **类 XAML / QML** 的 `.ui` 标记文件，**支持热重载**
- **CSS-like 样式系统**：类选择器、标签选择器、后代选择器、`theme.*` 主题色
- **数据绑定**：`text="{variable}"` + `SetText/SetFloat/SetBool`
- **列表渲染**：`<Repeater model="{list}">` 模板
- **组件化**：`<Include src="header.ui" />` 复用
- **i18n 内置**：`text="@welcome"` 自动查 `.lang` 文件

### 🌐 纯 C API，所有语言都能调

```c
#include <ui_core.h>

ui_init();
UiWidget btn = ui_button(L"确定");
ui_widget_on_click(btn, my_cb, NULL);
// … 从任何能调 DLL 的语言用
```

- **187+ 导出函数**，句柄全部 `uint64_t`，没有一个 C++ 类型泄漏
- Rust / Go / Python / C# / Delphi / Pascal / Lua 全部能直接绑定
- 所有控件、窗口、动画、主题、IME、拖放——C API 全覆盖

### 🔍 实时调试直接外挂

- 内置 **Named Pipe IPC**，外部工具可查询控件树、高亮控件、切换页面
- `ui-debug` 协议文档化，配合自制工具或 VSCode 扩展做可视化调试

## 📊 真实数据

| 指标 | 数值 |
|------|------|
| `ui-core.dll` 体积（全功能） | **8.4 MB** |
| `ui-demo.exe` 体积（静态链接） | **1.0 MB** |
| 空窗口内存占用 | **< 30 MB** |
| 冷启动到首帧 | **< 200 ms** |
| 60 fps 动画 CPU 占用 | **< 3%** |
| 导出 C 函数数量 | **187+** |
| 内置控件 | **25+** |
| 支持的文件格式（ImageView） | PNG / JPG / BMP / GIF / WebP |

## 🚀 快速开始

### 环境要求

- Windows 10+
- CMake 3.20+
- MSVC 2019+ 或 MinGW-w64（C++17）

### 构建

```bash
git clone https://github.com/<your-account>/ui-core.git
cd ui-core
cmake -B build -G Ninja
cmake --build build
```

产物：

| 文件 | 说明 |
|------|------|
| `build/ui-core.dll` | 动态库（带版本号 `1.0.0.1`） |
| `build/ui-demo.exe` | 演示程序 |

单 exe 分发（无依赖）：

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
        // 按钮点击
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

就这么多。**没有 Proj 文件、没有 moc 预处理器、没有 XAML 编译器、没有 IDL**。

## 🧩 控件列表

### 容器
| 控件 | 说明 |
|------|------|
| `VBox` / `HBox` | 垂直 / 水平布局 |
| `Grid` | 网格布局 |
| `Stack` | 堆叠（显示一个子项） |
| `ScrollView` | 滚动容器 |
| `SplitView` | 可折叠侧边栏导航 |
| `Panel` | 自由定位容器 |
| `Expander` | 折叠 / 展开面板 |

### 输入
| 控件 | 说明 |
|------|------|
| `Button` / `IconButton` | 按钮 / 图标按钮 |
| `TextInput` / `TextArea` | 单行 / 多行文本输入 |
| `NumberBox` | 数字输入（带步进按钮） |
| `CheckBox` / `RadioButton` | 复选 / 单选 |
| `Toggle` | 开关 |
| `Slider` | 滑块 |
| `ComboBox` | 下拉选择 |

### 展示
| 控件 | 说明 |
|------|------|
| `Label` | 文本（多行、自动换行） |
| `ProgressBar` | 进度条（确定 / 不确定） |
| `ImageView` | 图片（缩放、平移、裁剪、GIF） |
| `Separator` | 分隔线 |

### 导航 / 弹出
| 控件 | 说明 |
|------|------|
| `TitleBar` | 无边框窗口标题栏 |
| `NavItem` | 侧边导航项（SVG 图标） |
| `TabControl` | 选项卡 |
| `Flyout` | 弹出浮层 |
| `ContextMenu` | 右键菜单 |
| `Dialog` | 模态对话框 |
| `Toast` | 轻提示 |

## 🎨 主题

内置 Fluent 2 深色 / 浅色主题，运行时一行切换：

```c
ui_theme_set_mode(UI_THEME_DARK);
ui_theme_set_mode(UI_THEME_LIGHT);
```

所有控件自动响应。标记文件里引用主题色：

```
theme.accent / theme.windowBg / theme.sidebarBg / theme.cardBg
theme.contentText / theme.divider / theme.inputBg / theme.inputBorder
```

## 🔢 版本号

版本号格式：`MAJOR.MINOR.PATCH.BUILD`（当前 `1.0.0.1`）

- 编译期宏：`UI_CORE_VERSION_MAJOR/MINOR/PATCH/BUILD` + `UI_CORE_VERSION_STRING`
- 运行时 API：

```c
int major, minor, patch;
ui_core_version(&major, &minor, &patch);   // 1, 0, 0
int build = ui_core_version_build();        // 1
const char* v = ui_core_version_string();   // "1.0.0.1"
```

- Windows DLL 属性页（右键 `ui-core.dll` → 属性 → 详细信息）会显示 `FileVersion 1.0.0.1`
- 完整发布历史见 [CHANGELOG.md](./CHANGELOG.md)

## 📁 项目结构

```
ui-core/
├── include/           # 公共头文件 (ui_core.h, plugin_api.h)
├── src/
│   ├── version.rc.in  # Windows 版本资源模板
│   └── ui/            # 框架实现
│       ├── renderer.*     # Direct2D 渲染引擎
│       ├── widget.*       # 基础控件 + 布局
│       ├── controls.*     # 所有内置控件
│       ├── ui_window.*    # 窗口 + 事件分发
│       ├── ui_api.cpp     # C API 实现
│       ├── animation.*    # 动画系统
│       ├── context_menu.* # 右键菜单
│       └── markup/        # .ui 标记文件解析器
├── demo/              # 演示程序
├── docs/              # 使用文档
├── test/              # 单元测试
├── CMakeLists.txt
├── VERSION
└── CHANGELOG.md
```

## 📚 文档

| 文档 | 内容 |
|------|------|
| [快速上手](docs/getting-started.md) | 集成指南 |
| [标记文件](docs/markup.md) | .ui 文件语法 |
| [C API](docs/c-api.md) | 纯 C 接口参考 |
| [控件](docs/controls.md) | 控件详细说明 |
| [布局](docs/layout.md) | 布局系统 |
| [设计系统](docs/design-system.md) | Fluent 2 设计规范 |
| [国际化](docs/i18n.md) | 多语言支持 |
| [API 总览](UI_CORE_API.md) | 全部导出函数清单 |

## 🤝 适用场景

- ✅ **Windows 工具类桌面应用**（下载器、图片查看器、配置管理器、资料库工具）
- ✅ **需要 Fluent 外观但不愿被 .NET/WinUI 绑架**的原生项目
- ✅ **Rust / Go / Python 想要 Fluent 界面**但找不到合适绑定
- ✅ **嵌入到现有 C++ 项目**作为 UI 层（无第三方运行时）
- ✅ **离线分发**，体积敏感，不能带 Electron 全家桶

## 📝 许可证

[MIT License](./LICENSE) © ui-core contributors

— 如果这个项目让你少写了一个 Electron 应用，**请点一个 Star ⭐**。
