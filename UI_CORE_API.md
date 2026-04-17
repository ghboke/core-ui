# UI Core 使用文档

UI Core 是一个基于 Direct2D 的 Windows 原生 UI 框架，采用 Fluent 2 设计系统（对齐 WinUI 3 源码级规格），编译为 DLL。

**推荐使用 `.ui` 标记文件构建界面**（声明式，类似 XAML / QML），C++ 只写逻辑代码。也支持纯 C API 编程。

## 文档索引

| 文档 | 内容 |
|------|------|
| [快速开始](docs/getting-started.md) | 文件结构、最小示例（.ui 方式 + C API 方式） |
| [.ui 标记文件](docs/markup.md) | 文件结构、样式系统、数据绑定、事件处理、组件引入 |
| [布局容器](docs/layout.md) | VBox / HBox / Grid / Stack / ScrollView / Splitter / SplitView / Panel / Spacer |
| [控件参考](docs/controls.md) | Label / Button / CheckBox / RadioButton / Toggle / Slider / ProgressBar / TextInput / TextArea / ComboBox / NavItem / TitleBar / IconButton / TabControl / ImageView（含裁剪模式）/ Separator / Dialog / Toast / ContextMenu / CustomWidget |
| [C API 速查](docs/c-api.md) | 生命周期、窗口、控件创建/操作/回调、主题、调试 |
| [多语言 (i18n)](docs/i18n.md) | @key 引用、语言包文件、加载/切换语言、完整示例 |
| [设计系统](docs/design-system.md) | Fluent 2 色彩 / 字体 / 间距 / 圆角 / 阴影 / 动画 |

## 类型参考

```c
typedef uint64_t UiWidget;   // 控件句柄
typedef uint64_t UiWindow;   // 窗口句柄
typedef uint64_t UiMenu;     // 菜单句柄
#define UI_INVALID 0         // 无效句柄
typedef void* UiDrawCtx;     // 绘制上下文
typedef struct UiColor { float r, g, b, a; } UiColor;
typedef struct UiRect  { float left, top, right, bottom; } UiRect;
typedef enum UiThemeMode { UI_THEME_DARK = 0, UI_THEME_LIGHT = 1 } UiThemeMode;
```

## 注意事项

1. **线程安全**：所有 API 须在主线程调用
2. **字符串返回**：`get_text()` 返回内部指针，不要 free
3. **事件注册顺序**：`.ui` 方式必须在 `LoadFile()` 之前注册 handler
4. **多窗口**：`ui_run()` 服务所有窗口，最后一个关闭时返回
5. **DPI**：自动处理 Per-Monitor DPI V2
6. **推荐 .ui 文件**：UI 结构用 .ui 声明，C++ 只写逻辑
7. **Debug 内存**：`ui_debug_dump_*` 返回的字符串用 `ui_debug_free()` 释放
