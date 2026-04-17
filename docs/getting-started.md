# 快速开始

## 文件结构

```
my_app/
  main.cpp          — 入口：加载 .ui 文件 + 注册事件
  app.ui            — 界面布局（声明式）
  ui_core.h         — 头文件
  ui-core.dll       — 运行时库
```

## CMake 集成

```cmake
add_executable(my_app WIN32 main.cpp)
target_include_directories(my_app PRIVATE third_party/ui-core/include)
target_link_libraries(my_app PRIVATE ${CMAKE_SOURCE_DIR}/third_party/ui-core/ui-core.lib)

# 复制 DLL
add_custom_command(TARGET my_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/third_party/ui-core/ui-core.dll
        $<TARGET_FILE_DIR:my_app>)
```

## .ui 文件方式（推荐）

**app.ui**
```xml
<ui version="1" width="640" height="480" resizable="true" title="My App">
  <VBox gap="0" expand="true">
    <TitleBar title="My App" />
    <VBox padding="24" gap="12" expand="true">
      <Label text="Hello, UI Core!" fontSize="20" bold="true" />
      <Button id="btn" text="Click Me" width="120" onClick="onBtnClick" />
      <Label id="status" text="Ready" />
    </VBox>
  </VBox>
</ui>
```

**main.cpp**
```cpp
#include <ui_core.h>
#include "../src/ui/markup/markup.h"
#include "../src/ui/controls.h"
#include "../src/ui/ui_context.h"
#include "../src/ui/ui_window.h"

static ui::UiMarkup g_layout;
static UiWindow g_win = UI_INVALID;

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    ui_init();

    // 1. 注册事件（必须在 LoadFile 之前）
    g_layout.SetHandler("onBtnClick", std::function<void()>([]() {
        g_layout.SetText("status", L"Button clicked!");
        ui_window_invalidate(g_win);
    }));

    // 2. 加载 .ui 文件
    if (!g_layout.LoadFile(L"app.ui")) {
        MessageBoxA(nullptr, g_layout.LastError().c_str(), "Error", MB_ICONERROR);
        return 1;
    }

    // 3. 创建窗口
    auto& wh = g_layout.Window();
    UiWindowConfig cfg = {0};
    cfg.title = L"My App";
    cfg.width = wh.width; cfg.height = wh.height;
    cfg.resizable = wh.resizable;
    g_win = ui_window_create(&cfg);

    auto& ctx = ui::GetContext();
    ui_window_set_root(g_win, ctx.handles.Insert(g_layout.Root()));
    ui_window_show(g_win);

    int ret = ui_run();
    ui_shutdown();
    return ret;
}
```

## C API 方式（简单场景 / 非 C++ 宿主）

```c
#include <ui_core.h>

void on_click(UiWidget w, void* ud) {
    ui_label_set_text(*(UiWidget*)ud, L"Clicked!");
}

int WINAPI wWinMain(HINSTANCE h, HINSTANCE, LPWSTR, int) {
    ui_init();
    UiWindowConfig cfg = {0};
    cfg.title = L"My App"; cfg.width = 640; cfg.height = 480; cfg.resizable = 1;
    UiWindow win = ui_window_create(&cfg);

    UiWidget root = ui_vbox();
    ui_widget_set_padding_uniform(root, 16);
    ui_widget_set_gap(root, 8);

    UiWidget label = ui_label(L"Hello!");
    ui_widget_add_child(root, label);

    UiWidget btn = ui_button(L"Click Me");
    ui_widget_set_width(btn, 120);
    ui_widget_on_click(btn, on_click, &label);
    ui_widget_add_child(root, btn);

    ui_window_set_root(win, root);
    ui_window_show(win);
    return ui_run();
}
```
