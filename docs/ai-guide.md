# UI Core — AI Agent Guide

> **这一个文件，就是 AI 生成 UI Core 应用需要的全部知识。**
> One file — everything an LLM needs to generate a complete UI Core app.

设计原则：纯 C ABI + 声明式 XML + 一份速查表。LLM 不需要先扫全仓库再动手。

---

## 1. 30 秒速览 / 30-second TL;DR

- **头文件**：`#include <ui_core.h>`（唯一公共头文件）
- **链接**：`ui-core.dll` / `ui-core.lib`
- **生命周期**：`ui_init()` → 建控件 → `ui_window_create()` → `ui_window_set_root()` → `ui_window_show()` → `ui_run()` → `ui_shutdown()`
- **所有句柄都是 `uint64_t`**，类型别名 `UiWidget`、`UiWindow`、`UiMenu`
- **UI 可以完全用 `.ui` XML 文件描述**，运行时加载 → 返回根 `UiWidget` → `ui_window_set_root()`
- **回调签名**：`typedef void (*Callback)(UiWidget w, void* userdata);`
- **字符串**：窗口/标签文本用 `const wchar_t*`（UTF-16），其他标识用 `const char*`

---

## 2. 最小可运行程序 / Minimum runnable program

```cpp
#include <ui_core.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    ui_init();

    UiWidget root = ui_vbox();
    ui_widget_set_padding(root, 32);
    ui_widget_set_gap(root, 16);
    ui_widget_add_child(root, ui_label(L"Hello UI Core"));
    ui_widget_add_child(root, ui_button(L"OK"));

    UiWindowConfig cfg = {0};
    cfg.title  = L"Demo";
    cfg.width  = 400;
    cfg.height = 300;
    UiWindow win = ui_window_create(&cfg);
    ui_window_set_root(win, root);
    ui_window_show(win);

    int code = ui_run();
    ui_shutdown();
    return code;
}
```

这段代码**开箱即跑**，不需要任何 `.ui` 文件。

---

## 3. 标记文件（推荐做法）

### 3.1 基本骨架

```xml
<ui version="1" width="800" height="600" title="My App">
  <VBox gap="16" padding="32" expand="true">
    <Label text="Title" fontSize="24" bold="true" />
    <Button text="@action" type="primary" onClick="onAction" />
  </VBox>
</ui>
```

C++ 加载：

```cpp
#include "src/ui/markup/markup.h"

static ui::UiMarkup layout;
layout.SetHandler("onAction", std::function<void()>([]() { /* … */ }));
layout.LoadFile(L"app.ui");

auto& ctx = ui::GetContext();
ui_window_set_root(win, ctx.handles.Insert(layout.Root()));
```

### 3.2 所有内置元素（标签名）

| 分类 | 标签 |
|------|------|
| 布局 | `VBox` `HBox` `Grid` `Stack` `ScrollView` `SplitView` `Panel` `Expander` |
| 输入 | `Button` `IconButton` `TextInput` `TextArea` `NumberBox` `CheckBox` `RadioButton` `Toggle` `Slider` `ComboBox` |
| 展示 | `Label` `ProgressBar` `ImageView` `Separator` |
| 导航 | `TitleBar` `NavItem` `TabControl` |
| 弹出 | `Flyout` `ContextMenu` `Dialog` `Toast` |
| 结构 | `Include` `Repeater` `style` |

### 3.3 通用属性（所有控件都能用）

| 属性 | 类型 | 示例 | 说明 |
|------|------|------|------|
| `width` / `height` | number / percent | `"120"` / `"50%"` | 尺寸；`0` = 自适应 |
| `expand` | bool | `"true"` | 占满剩余空间 |
| `gap` | number | `"16"` | 子元素间距（仅容器） |
| `padding` | number 或 四值 | `"32"` / `"16 24"` | 内边距 |
| `margin` | number 或 四值 | `"8"` | 外边距 |
| `bgColor` | color | `"#2a2a2a"` / `"theme.cardBg"` | 背景色 |
| `class` | string | `"title"` | 样式类（配合 `<style>`） |
| `id` | string | `"main-btn"` | 绑定 ID，供脚本回调查找 |
| `visible` | bool | `"true"` | 可见 |
| `position` | string | `"absolute"` | 配合 `left`/`top` 用绝对定位 |
| `text` | string | `"Save"` / `"@save_key"` / `"{count}"` | 文本，可 i18n / 数据绑定 |
| `onClick` | string | `"onSave"` | 回调名，配合 `SetHandler()` 注册 |

### 3.4 常用控件特有属性

- **Button**：`type="primary|default|ghost|danger"`、`icon="svg string"`
- **Label**：`fontSize="20"`、`bold="true"`、`color="#ffffff"`、`wrap="true"`
- **TextInput / TextArea**：`placeholder="…"`、`value="…"`、`onChange="…"`
- **Slider**：`min="0"` `max="100"` `value="50"` `onChange="…"`
- **ComboBox**：子元素 `<Item text="…" value="…" />`
- **NavItem**：`selected="true"`、`svg="…"`、`onClick="…"`
- **SplitView**：`mode="compactInline|compactOverlay"`、`openPaneLength="220"`、`open="true"`
- **ImageView**：`src="path.png"`、`fit="contain|cover|none"`
- **Include**：`src="header.ui" prop1="…" prop2="…"`
- **Repeater**：`model="{listName}"`，子元素是模板，`{field}` 引用列表项字段

### 3.5 样式块

```xml
<style>
  .title { fontSize: 20; bold: true; color: theme.contentText; }
  .sidebar { bgColor: theme.sidebarBg; padding: 4; }
  .sidebar Label { fontSize: 13; }
  Button.primary { bgColor: theme.accent; }
</style>
```

支持：类选择器、标签选择器、`A B`（后代）、`A.class`（标签+类）。不支持 ID 选择器（标签里直接写更清晰）。

### 3.6 数据绑定

```xml
<Label text="{count}" />
<Button text="Inc" onClick="onInc" />
```

```cpp
layout.SetInt("count", 0);
layout.SetHandler("onInc", std::function<void()>([&]() {
    layout.SetInt("count", layout.GetInt("count") + 1);  // 自动刷新 UI
}));
```

绑定方法：`SetText / SetInt / SetFloat / SetBool` + 对应 `Get*`。

### 3.7 国际化

```xml
<Label text="@welcome" />
<Button text="@action.save" />
```

`demo/lang/zh-CN.lang`：

```
welcome = 欢迎
action.save = 保存
```

运行时：`ui_i18n_set_language("zh-CN");` 所有 `@key` 自动刷新。

---

## 4. C API 速查 / C API cheatsheet

### 4.1 生命周期

```c
int  ui_init(void);                         // 返回 0 成功
int  ui_init_with_theme(UiThemeMode mode);  // UI_THEME_DARK | UI_THEME_LIGHT
void ui_shutdown(void);
int  ui_run(void);                          // 消息循环，返回 exit code
void ui_quit(int exit_code);                // 投递 WM_QUIT
```

### 4.2 版本号

```c
void        ui_core_version(int* major, int* minor, int* patch);
int         ui_core_version_build(void);
const char* ui_core_version_string(void);   // "1.0.0.1"
```

### 4.3 窗口

```c
UiWindow ui_window_create(const UiWindowConfig* config);
void     ui_window_destroy(UiWindow win);
void     ui_window_show(UiWindow win);
void     ui_window_hide(UiWindow win);
void     ui_window_set_root(UiWindow win, UiWidget root);
void     ui_window_set_title(UiWindow win, const wchar_t* title);
void     ui_window_invalidate(UiWindow win);   // 触发重绘
void     ui_window_relayout(UiWindow win);     // 强制重新布局
void*    ui_window_hwnd(UiWindow win);         // 拿到 HWND（互操作用）
```

`UiWindowConfig`：

```c
typedef struct UiWindowConfig {
    const wchar_t* title;
    int  width, height;
    int  system_frame;    // 0 = 无边框（默认），1 = 系统标题栏
    int  resizable;
    int  accept_files;    // 1 = 支持拖放文件
    int  x, y;            // 0 = 屏幕居中
    int  tool_window;     // 1 = 不在任务栏显示
    int  skip_animation;
    const void* icon_pixels;   // RGBA 32bpp
    int  icon_width, icon_height;
} UiWindowConfig;
```

### 4.4 控件工厂（返回 `UiWidget`）

```c
UiWidget ui_vbox(void);
UiWidget ui_hbox(void);
UiWidget ui_spacer(float size);                   // 0 = 自适应填充
UiWidget ui_panel(UiColor bg);
UiWidget ui_label(const wchar_t* text);
UiWidget ui_button(const wchar_t* text);
UiWidget ui_checkbox(const wchar_t* text);
UiWidget ui_radio_button(const wchar_t* text, const char* group);
UiWidget ui_toggle(const wchar_t* text);
UiWidget ui_slider(float min, float max, float value);
UiWidget ui_separator(void);
UiWidget ui_vseparator(void);
UiWidget ui_text_input(const wchar_t* placeholder);
UiWidget ui_text_area(const wchar_t* placeholder);
UiWidget ui_combobox(const wchar_t** items, int count);
UiWidget ui_progress_bar(float min, float max, float value);
UiWidget ui_tab_control(void);
UiWidget ui_scroll_view(void);
UiWidget ui_dialog(void);
UiWidget ui_image_view(void);
```

### 4.5 控件通用操作

```c
void  ui_widget_add_child(UiWidget parent, UiWidget child);
void  ui_widget_remove_child(UiWidget parent, UiWidget child);
void  ui_widget_destroy(UiWidget w);
void  ui_widget_set_visible(UiWidget w, int visible);
void  ui_widget_set_enabled(UiWidget w, int enabled);
void  ui_widget_set_padding(UiWidget w, float p);
void  ui_widget_set_gap(UiWidget w, float gap);
void  ui_widget_set_size(UiWidget w, float width, float height);
void  ui_widget_set_bg_color(UiWidget w, UiColor c);
void  ui_widget_on_click(UiWidget w, void (*cb)(UiWidget, void*), void* userdata);
```

### 4.6 弹出 / 反馈

```c
// 右键菜单
UiMenu ui_menu_create(void);
void   ui_menu_add_item(UiMenu menu, int id, const wchar_t* text);
void   ui_menu_add_separator(UiMenu menu);
void   ui_menu_show(UiWindow win, UiMenu menu, float x, float y);
// 通过 ui_window_on_menu 收 id → 分发

// Toast
void ui_toast(UiWindow win, const wchar_t* text, int duration_ms);
void ui_toast_ex(UiWindow win, const wchar_t* text, int duration_ms,
                 int position /*0=top 1=center 2=bottom*/,
                 int icon     /*0=none 1=success 2=error 3=warning*/);
```

### 4.7 主题

```c
void ui_theme_set_mode(UiThemeMode mode);  // UI_THEME_DARK / UI_THEME_LIGHT
```

切换后所有控件自动刷新。

完整函数清单见 [UI_CORE_API.md](../UI_CORE_API.md)。

---

## 5. 三个完整例子 / Three end-to-end examples

### 5.1 计数器（纯 C API）

```cpp
#include <ui_core.h>

static UiWidget g_label;
static int g_count = 0;

static void on_inc(UiWidget, void*) {
    wchar_t buf[32]; swprintf_s(buf, 32, L"%d", ++g_count);
    ui_label_set_text(g_label, buf);
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    ui_init();

    UiWidget root = ui_vbox();
    ui_widget_set_padding(root, 32);
    ui_widget_set_gap(root, 16);
    g_label = ui_label(L"0");
    ui_widget_add_child(root, g_label);
    UiWidget btn = ui_button(L"+1");
    ui_widget_on_click(btn, on_inc, nullptr);
    ui_widget_add_child(root, btn);

    UiWindowConfig cfg = {0};
    cfg.title = L"Counter"; cfg.width = 300; cfg.height = 200;
    UiWindow win = ui_window_create(&cfg);
    ui_window_set_root(win, root);
    ui_window_show(win);

    int code = ui_run();
    ui_shutdown();
    return code;
}
```

### 5.2 设置页面（.ui + 数据绑定）

**settings.ui**

```xml
<ui version="1" width="500" height="400" title="Settings">
  <VBox gap="20" padding="32" expand="true">
    <Label text="Settings" fontSize="24" bold="true" />
    <HBox gap="12">
      <Label text="Theme:" width="100" />
      <ComboBox id="theme" onChange="onTheme">
        <Item text="Dark" value="dark" />
        <Item text="Light" value="light" />
      </ComboBox>
    </HBox>
    <HBox gap="12">
      <Label text="Volume:" width="100" />
      <Slider min="0" max="100" value="{volume}" onChange="onVolume" expand="true" />
      <Label text="{volume}" width="40" />
    </HBox>
    <Toggle text="Notifications" value="{notify}" onChange="onNotify" />
    <Spacer />
    <Button text="Save" type="primary" onClick="onSave" />
  </VBox>
</ui>
```

**main.cpp**

```cpp
#include <ui_core.h>
#include "src/ui/markup/markup.h"
using namespace ui;

static UiMarkup layout;

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    ui_init();

    layout.SetInt("volume", 60);
    layout.SetBool("notify", true);

    layout.SetHandler("onTheme", std::function<void(const std::wstring&)>(
        [](const std::wstring& v) {
            ui_theme_set_mode(v == L"dark" ? UI_THEME_DARK : UI_THEME_LIGHT);
        }));
    layout.SetHandler("onVolume", std::function<void(float)>(
        [](float v) { layout.SetInt("volume", (int)v); }));
    layout.SetHandler("onSave", std::function<void()>(
        [&]() { /* persist somewhere */ }));

    layout.LoadFile(L"settings.ui");

    UiWindowConfig cfg = {0};
    cfg.title = L"Settings"; cfg.width = 500; cfg.height = 400;
    UiWindow win = ui_window_create(&cfg);
    ui_window_set_root(win, GetContext().handles.Insert(layout.Root()));
    ui_window_show(win);

    int code = ui_run();
    ui_shutdown();
    return code;
}
```

### 5.3 多页导航（SplitView + Repeater）

```xml
<ui version="1" width="1000" height="700" title="App">
  <style>
    .hero { fontSize: 28; bold: true; }
  </style>

  <VBox gap="0" expand="true">
    <TitleBar title="App" />
    <SplitView mode="compactInline" openPaneLength="220" open="true" expand="true">
      <VBox padding="4" gap="2">
        <NavItem text="Home"     svg="…" selected="true" onClick="onNav" id="home" />
        <NavItem text="Library"  svg="…" onClick="onNav" id="library" />
        <NavItem text="Settings" svg="…" onClick="onNav" id="settings" />
      </VBox>
      <Stack id="pages" expand="true">
        <VBox padding="32"><Label text="Home"     class="hero" /></VBox>
        <VBox padding="32"><Label text="Library"  class="hero" /></VBox>
        <VBox padding="32"><Label text="Settings" class="hero" /></VBox>
      </Stack>
    </SplitView>
  </VBox>
</ui>
```

AI 代理可把每个 `NavItem` 的 `id` 映射到 `Stack` 的索引，`onNav` 里做 `ui_stack_set_active_index(pages, i)`。

---

## 6. 常见坑 / Things NOT to hallucinate

1. **不要 `delete` 控件**：用 `ui_widget_destroy()`。控件由句柄表管理。
2. **不要跨线程调 UI API**：全部在消息循环线程。用 `ui_post_main_thread()` 投递。
3. **标记文件大小写敏感**：`Button` ✓、`button` ✗。
4. **`text` 属性可以是字面量 / `@key` / `{binding}`，但不能混用**：`text="Hello @name"` 无效。
5. **颜色写法**：`#RRGGBB`、`#RRGGBBAA`、`rgb(…)`、`theme.*`。**不支持命名色**（`"red"` 无效）。
6. **`onClick` 等属性的值是"回调名"，不是函数体**：`onClick="doSave"`，然后 `SetHandler("doSave", …)`。
7. **`UiColor` 的分量是 `float 0..1`**，不是 `0..255`。
8. **尺寸是 DIP**，框架自己处理 Per-Monitor DPI，**不要**手工乘 `dpiScale`。
9. **callback userdata 生命周期要自己管**：框架不会帮你 `free`。

---

## 7. 推荐的 AI 工作流

1. **读这份文件**（就是这个 `ai-guide.md`）— 拿到全部接口。
2. **从 `.ui` 开始写 UI**，而不是纯 C API — 声明式 XML 对 LLM 友好，出错率低。
3. **遇到不在本文档的需求**再去查 [UI_CORE_API.md](../UI_CORE_API.md) 或 [docs/c-api.md](c-api.md)。
4. **示例优先**：先参考 [`demo/app.ui`](../demo/app.ui) 和 [`demo/app.cpp`](../demo/app.cpp) 找相似模式。
5. **生成代码后跑 `cmake --build build`**，编译器比 LLM 自查更可靠。

---

## 8. 机器可读元数据 / Machine-readable metadata

```yaml
name: ui-core
version: 1.0.0.1
language: c++
c_abi: true
platform: windows
min_os: windows-10
build_system: cmake
header: include/ui_core.h
library: ui-core.dll
exports: 187
widgets: 25
markup_format: xml
hot_reload: true
themes: [dark, light]
design_system: fluent-2
license: MIT
single_file_agent_entry: docs/ai-guide.md
llms_txt: llms.txt
```
