export default {
  // Search
  "search.label": "搜索",
  "search.placeholder": "搜索控件、API、文档…",
  "search.noResults": "没有找到结果",
  "search.hint": "输入关键词搜索控件、API 函数或文档页面",

  // CodeBlock
  "code.copy": "复制",
  "code.copied": "已复制",

  // Header
  "header.lightMode": "浅色模式",
  "header.darkMode": "深色模式",
  "header.github": "GitHub",
  "header.switchLang": "Switch to English",

  // Sidebar
  "nav.home": "首页",
  "nav.docs": "文档",
  "nav.gettingStarted": "快速开始",
  "nav.controls": "控件",
  "nav.controlsOverview": "总览",
  "nav.designSystem": "设计系统",

  // Home - Hero
  "home.badge": "Windows 桌面 UI 框架",
  "home.title": "用 UI Core 构建精美的原生应用",
  "home.subtitle": "Direct2D 硬件加速渲染、Fluent 2 设计系统、29+ 内置控件、声明式 .ui 标记语言和纯 C API — 全部打包在一个 DLL 中。",
  "home.getStarted": "快速开始",
  "home.viewGithub": "在 GitHub 查看",

  // Home - Stats
  "home.stats.controls": "控件",
  "home.stats.apiFunctions": "C API 函数",
  "home.stats.gpuAccelerated": "GPU 加速",
  "home.stats.perMonitor": "逐显示器 V2",

  // Home - Features
  "home.whyTitle": "为什么选择 UI Core？",
  "home.whySubtitle": "现代化的 Windows 桌面开发方案 — 无臃肿框架，无托管运行时。",

  // Home - Code
  "home.codeTitle": "两种开发方式",
  "home.codeSubtitle": "使用 C API 获得完全控制，或用声明式 .ui 标记快速原型开发 — 也可以混合使用。",

  // Features
  "feature.gpu.title": "Direct2D 硬件加速",
  "feature.gpu.desc": "所有渲染基于 Direct2D + Direct3D 11。GPU 合成实现流畅 60fps 动画。",
  "feature.design.title": "Fluent 2 设计系统",
  "feature.design.desc": "对齐 Microsoft WinUI 3 规范。完整的颜色、字体、间距、阴影和动效 Token 集。",
  "feature.markup.title": "声明式 .ui 标记",
  "feature.markup.desc": "类似 XAML/QML 的 XML 声明语法。支持热重载、数据绑定和事件处理。",
  "feature.api.title": "纯 C API",
  "feature.api.desc": "187 个导出函数，基于不透明句柄设计。可从任何语言调用 — 无 C++ 暴露。",
  "feature.controls.title": "29+ 内置控件",
  "feature.controls.desc": "从 Button、CheckBox、Slider 到 ImageView、SplitView、ContextMenu。可直接用于生产应用。",
  "feature.dpi.title": "逐显示器 DPI V2",
  "feature.dpi.desc": "多显示器自动 DPI 缩放。在任何缩放比例下都能清晰渲染。",

  // Getting Started
  "gs.title": "快速开始",
  "gs.subtitle": "几分钟内构建并集成 UI Core 到你的 Windows 应用中。",
  "gs.requirements": "UI Core 需要 Windows 10 或更高版本、CMake 3.20+ 以及支持 C++17 的 MSVC 或 MinGW。",
  "gs.buildTitle": "从源码构建",
  "gs.cmakeTitle": "CMake 集成",
  "gs.cmakeDesc": "在你的 CMake 项目中链接 ui-core：",
  "gs.minimalTitle": "最小示例 — C API",
  "gs.minimalDesc": "使用纯 C API 创建一个包含标签和按钮的窗口。所有 187 个函数使用不透明的 uint64_t 句柄 — 不需要 C++ 头文件。",
  "gs.markupTitle": "声明式方式 — .ui 标记",
  "gs.markupDesc": "用基于 XML 的 .ui 文件构建相同的 UI。支持数据绑定、事件处理、热重载和样式类。",
  "gs.distTitle": "分发方式",
  "gs.distDynamic": "动态（默认）：",
  "gs.distDynamicDesc": "将 ui-core.dll 与 .exe 一起发布",
  "gs.distStatic": "静态：",
  "gs.distStaticDesc": "使用 -DUI_CORE_STATIC=ON 构建单个 .exe，无 DLL 依赖",
  "gs.depsTitle": "系统依赖",
  "gs.depsDesc": "UI Core 仅依赖 Windows 系统库 — 无需外部运行时：",

  // Controls
  "controls.title": "控件",
  "controls.subtitle": "29+ 内置控件覆盖容器、输入、显示和导航。所有控件遵循 Fluent 2 设计规范，主题感知渲染。",
  "controls.searchPlaceholder": "搜索控件…",
  "controls.count_one": "{{count}} 个控件",
  "controls.count_other": "{{count}} 个控件",
  "controls.category.all": "全部",
  "controls.category.container": "容器",
  "controls.category.input": "输入",
  "controls.category.display": "显示",
  "controls.category.navigation": "导航",

  // Sidebar extra
  "nav.markup": ".ui 标记语法",
  "nav.layout": "布局系统",
  "nav.cApi": "C API 参考",

  // API Reference
  "api.title": "C API 参考",
  "api.subtitle": "UI Core 导出 {{count}} 个纯 C 函数，按功能分组。所有函数使用不透明 uint64_t 句柄。",
  "api.searchPlaceholder": "搜索函数…",
  "api.functions": "个函数",

  // Control Detail
  "controlDetail.preview": "效果预览",
  "controlDetail.example": "代码示例",
  "controlDetail.relatedApi": "相关 API",

  // Markup
  "markup.title": ".ui 标记语法",
  "markup.subtitle": "使用类似 XAML/QML 的 XML 语法声明式构建 UI。支持样式类、数据绑定、事件处理和热重载。",
  "markup.basicTitle": "基本语法",
  "markup.basicDesc": "每个 XML 标签对应一个控件，属性映射到控件属性。标签名与控件类名一致。",
  "markup.styleTitle": "样式系统",
  "markup.styleDesc": "支持 class 选择器、标签选择器和伪状态（:hover, :pressed, :disabled）。样式可以内联在 <Style> 标签中。",
  "markup.bindingTitle": "数据绑定",
  "markup.bindingDesc": "使用 {variable} 语法绑定数据。通过 C++ 代码调用 SetText/SetFloat/SetBool 推送数据更新。",
  "markup.includeTitle": "组件引用",
  "markup.includeDesc": "使用 <Include> 标签引用其他 .ui 文件，支持传递属性作为参数。",
  "markup.repeaterTitle": "列表渲染",
  "markup.repeaterDesc": "使用 <Repeater> 根据数据模型动态生成子控件列表。",
  "markup.hotReloadTitle": "热重载",
  "markup.hotReloadDesc": "在调试构建中，修改 .ui 文件后保存即可立即看到变化，无需重新编译。",

  // Layout
  "layout.title": "布局系统",
  "layout.subtitle": "UI Core 提供灵活的布局容器：VBox/HBox（弹性盒）、Grid（网格）、Stack（层叠）、SplitView（导航分栏）、Splitter（可拖拽分割）和 ScrollView（滚动容器）。",
  "layout.flexDesc": "VBox 垂直排列子控件，HBox 水平排列。支持 padding、gap、align（start/center/end/stretch）和 justify 属性。",
  "layout.flexTitle": "弹性扩展",
  "layout.flexExpandDesc": "设置 expand=true 使控件填充父容器剩余空间。多个展开控件平分空间。",
  "layout.gridDesc": "Grid 按列数排列子控件，支持 colGap、rowGap、colspan 和 rowspan。",
  "layout.splitViewDesc": "SplitView 实现类似 WinUI NavigationView 的侧边栏导航，支持 4 种显示模式：overlay、inline、compactOverlay、compactInline。",
  "layout.splitterDesc": "Splitter 在两个面板之间放置可拖拽分割条，支持设置初始比例和方向。",
  "layout.vboxHboxTitle": "VBox / HBox（弹性盒）",
  "layout.gridTitle": "Grid（网格）",
  "layout.splitViewTitle": "SplitView（分栏导航）",
  "layout.splitterTitle": "Splitter（可拖拽分割）",
  "layout.scrollTitle": "ScrollView（滚动容器）",
  "layout.scrollDesc": "ScrollView 提供自动滚动条（4px 细条，WinUI 3 风格），支持鼠标滚轮。",

  // ControlPreview
  "controlDetail.noPreview": "此控件暂无交互预览，请参考下方代码示例。",

  // Search page names
  "search.page.gettingStarted": "快速开始",
  "search.page.markup": ".ui 标记语法",
  "search.page.layout": "布局系统",
  "search.page.controls": "控件",
  "search.page.cApi": "C API 参考",
  "search.page.designSystem": "设计系统",
  "search.page.ai": "AI 集成指南",

  // Sidebar
  "nav.aiGuide": "AI 集成指南",

  // AI Guide
  "ai.title": "AI 集成指南",
  "ai.subtitle": "本指南面向 AI 编程助手（Claude、GPT、Copilot 等），说明如何调用 UI Core 的 C API 构建界面、调试控件树和验证渲染结果。",
  "ai.tip": "AI 可以通过 C API 或 .ui 标记两种方式生成 UI。推荐先用 .ui 标记快速原型，再用 C API 精细控制。调试时使用 dump_tree + screenshot 闭环验证。",

  "ai.usageTitle": "如何使用",
  "ai.usageDesc": "下方是完整的 UI Core AI Skill 文件。将它添加到你的 AI 助手知识库中，AI 就能直接调用 UI Core 的 C API 或生成 .ui 标记文件来构建 Windows 桌面应用。",
  "ai.step1": "点击「复制」将全部内容复制到剪贴板，或点击「下载」保存为 .md 文件",
  "ai.step2": "将文件内容粘贴到 AI 助手的系统提示、CLAUDE.md、或项目知识库中",
  "ai.step3": "告诉 AI 你想构建什么界面，它会参考 Skill 文件生成代码",
  "ai.step4": "使用 ui_debug_screenshot + ui_debug_dump_tree 验证 AI 生成的结果",
  "ai.download": "下载 .md",
  "ai.lines": "行",

  "ai.handleTitle": "核心概念：不透明句柄",
  "ai.handleDesc": "UI Core 的所有对象（窗口、控件、菜单）都以 uint64_t 句柄表示。AI 不需要理解内部结构，只需要通过句柄调用 API 函数。句柄创建后通过 setter/getter 操作，通过 add_child 组装成树。",

  "ai.quickStartTitle": "方式一：C API 生成 UI",
  "ai.quickStartDesc": "AI 可以直接生成包含 ui_core.h 的 C 代码。基本模式：初始化 → 创建窗口 → 构建控件树 → 设置根控件 → 显示窗口 → 进入消息循环。",

  "ai.markupTitle": "方式二：生成 .ui 标记文件",
  "ai.markupDesc": "AI 也可以生成 XML 格式的 .ui 文件，无需编译即可热重载预览。标签名与控件类名一致，属性直接映射到控件属性。适合快速生成复杂布局。",

  "ai.callbackTitle": "事件回调模式",
  "ai.callbackDesc": "所有交互事件通过 C 回调函数处理。回调签名统一为 (控件句柄, userdata)。AI 生成代码时注意：userdata 常用于传递窗口句柄或其他控件句柄，需要 uintptr_t 转换。",

  "ai.layoutTitle": "布局速查表",
  "ai.layoutDesc": "AI 生成布局时最常用的函数。VBox/HBox 是主要容器，expand 控制弹性填充，find_by_id 用于后续查找控件。",

  "ai.themeTitle": "主题切换",

  "ai.debugTitle": "调试 API 总览",
  "ai.debugDesc": "UI Core 提供 4 个调试函数，AI 可以用它们检查控件树结构、定位布局问题和验证渲染结果。",
  "ai.debugTablePurpose": "功能",
  "ai.debugTableUse": "AI 用途",
  "ai.debugDumpTree": "导出完整控件树为 JSON",
  "ai.debugDumpTreeUse": "验证层级结构、id 是否存在、rect 是否合理",
  "ai.debugDumpWidget": "导出单个控件为 JSON",
  "ai.debugDumpWidgetUse": "检查特定控件的属性值",
  "ai.debugHighlight": "红框高亮指定控件",
  "ai.debugHighlightUse": "配合截图定位控件在窗口中的位置",
  "ai.debugScreenshot": "保存窗口截图为 PNG",
  "ai.debugScreenshotUse": "AI 读取截图文件，视觉验证渲染结果",

  "ai.debugVisualTitle": "高亮 + 截图 + 属性检查",

  "ai.verifyTitle": "AI 验证闭环流程",
  "ai.verifyDesc": "推荐 AI 按以下步骤验证生成的 UI：构建运行 → 截图验证 → 导出控件树 → 高亮可疑控件 → 检查交互。每步都可以程序化执行，形成自动化验证闭环。",

  "ai.pitfallTitle": "常见陷阱",
  "ai.pitfall1": "字符串必须使用 L\"...\" 宽字符字面量，不是 \"...\"",
  "ai.pitfall2": "ui_debug_dump_tree / dump_widget 返回的字符串必须调用 ui_debug_free() 释放",
  "ai.pitfall3": "ui_run() 是阻塞的消息循环，之后的代码不会执行",
  "ai.pitfall4": "句柄值 0 (UI_INVALID) 表示创建失败，使用前应检查",
  "ai.pitfall5": "回调中传递句柄需要 (void*)(uintptr_t) 双重转换",
  "ai.pitfall6": "ui_widget_set_padding() 是四参数（左上右下），单参数用 ui_widget_set_padding_uniform()",

  "ai.summary": "总结：AI 生成代码 → 编译运行 → ui_debug_screenshot 截图 → AI 视觉检查 → ui_debug_dump_tree 结构检查 → 修正并重复。这是 AI 使用 UI Core 的推荐工作流。",

  // Controls i18n - Containers
  "ctrl.VBox.name": "VBox（垂直布局）",
  "ctrl.VBox.desc": "垂直弹性盒布局容器，子控件从上到下排列",
  "ctrl.HBox.name": "HBox（水平布局）",
  "ctrl.HBox.desc": "水平弹性盒布局容器，子控件从左到右排列",
  "ctrl.Grid.name": "Grid（网格布局）",
  "ctrl.Grid.desc": "表格式网格布局，按列数自动排列子控件",
  "ctrl.Stack.name": "Stack（层叠容器）",
  "ctrl.Stack.desc": "类似选项卡的层叠容器，同一时间只显示一个子控件",
  "ctrl.ScrollView.name": "ScrollView（滚动容器）",
  "ctrl.ScrollView.desc": "可滚动容器，带 WinUI 3 风格细滚动条",
  "ctrl.SplitView.name": "SplitView（分栏导航）",
  "ctrl.SplitView.desc": "类似 WinUI NavigationView 的侧边栏导航，支持 4 种显示模式",
  "ctrl.Splitter.name": "Splitter（可拖拽分割）",
  "ctrl.Splitter.desc": "在两个面板之间放置可拖拽分割条",
  "ctrl.Panel.name": "Panel（面板）",
  "ctrl.Panel.desc": "带纯色背景的容器面板，支持主题色",
  "ctrl.Spacer.name": "Spacer（间距/填充）",
  "ctrl.Spacer.desc": "固定尺寸间距或弹性填充空间",
  "ctrl.Expander.name": "Expander（折叠面板）",
  "ctrl.Expander.desc": "可折叠展开的面板，带标题和内容区",

  // Controls i18n - Input
  "ctrl.Button.name": "Button（按钮）",
  "ctrl.Button.desc": "标准按钮和主要操作按钮，支持默认/强调两种样式",
  "ctrl.IconButton.name": "IconButton（图标按钮）",
  "ctrl.IconButton.desc": "基于 SVG 的图标按钮，支持幽灵（镂空）模式",
  "ctrl.CheckBox.name": "CheckBox（复选框）",
  "ctrl.CheckBox.desc": "多选复选框，带勾选动画和主题色填充",
  "ctrl.RadioButton.name": "RadioButton（单选按钮）",
  "ctrl.RadioButton.desc": "同组互斥的单选按钮，带缩放动画",
  "ctrl.Toggle.name": "Toggle（开关）",
  "ctrl.Toggle.desc": "开/关切换开关，带滑动和颜色过渡动画",
  "ctrl.Slider.name": "Slider（滑块）",
  "ctrl.Slider.desc": "数值范围滑块，支持指数缓动曲线",
  "ctrl.TextInput.name": "TextInput（单行输入框）",
  "ctrl.TextInput.desc": "单行文本输入框，支持占位文字、只读模式和文本选择",
  "ctrl.TextArea.name": "TextArea（多行文本框）",
  "ctrl.TextArea.desc": "多行文本输入区域，支持自动换行和文本选择",
  "ctrl.NumberBox.name": "NumberBox（数值输入框）",
  "ctrl.NumberBox.desc": "数值微调输入框，带增减按钮和小数位控制",
  "ctrl.ComboBox.name": "ComboBox（下拉选择）",
  "ctrl.ComboBox.desc": "下拉选择列表，支持单选和索引访问",

  // Controls i18n - Display
  "ctrl.Label.name": "Label（标签）",
  "ctrl.Label.desc": "文本显示控件，支持自动换行、最大行数、粗体、字号和对齐",
  "ctrl.ProgressBar.name": "ProgressBar（进度条）",
  "ctrl.ProgressBar.desc": "确定/不确定模式进度条，带动画过渡效果",
  "ctrl.ImageView.name": "ImageView（图片查看器）",
  "ctrl.ImageView.desc": "可缩放/平移/旋转的图片查看器，支持分块渲染和 GDI+ 流式加载",
  "ctrl.Separator.name": "Separator（分隔线）",
  "ctrl.Separator.desc": "水平或垂直分隔线，自动适配主题颜色",

  // Controls i18n - Navigation
  "ctrl.TitleBar.name": "TitleBar（标题栏）",
  "ctrl.TitleBar.desc": "自定义无边框窗口标题栏，可拖拽、支持自定义控件",
  "ctrl.NavItem.name": "NavItem（导航项）",
  "ctrl.NavItem.desc": "侧边栏导航项，带 SVG 图标和选中状态",
  "ctrl.TabControl.name": "TabControl（选项卡）",
  "ctrl.TabControl.desc": "选项卡式页面导航，支持多标签切换",
  "ctrl.Dialog.name": "Dialog（对话框）",
  "ctrl.Dialog.desc": "模态对话框，带确认/取消按钮和回调",
  "ctrl.Toast.name": "Toast（通知气泡）",
  "ctrl.Toast.desc": "弹出式通知消息，支持 3 个位置和 4 种图标",
  "ctrl.ContextMenu.name": "ContextMenu（右键菜单）",
  "ctrl.ContextMenu.desc": "右键上下文菜单，支持快捷键、子菜单和分隔符",
  "ctrl.Flyout.name": "Flyout（弹出层）",
  "ctrl.Flyout.desc": "附着在锚点控件上的弹出浮层，支持 4 个方向和自动翻转",

  // Design System
  "ds.title": "设计系统",
  "ds.subtitle": "UI Core 实现了 Microsoft 的 Fluent 2 设计 Token — 与 WinUI 3 使用相同的体系。颜色、字体、间距、圆角、阴影和动效均基于 Token。",
  "ds.brandColors": "品牌色阶",
  "ds.statusColors": "状态颜色",
  "ds.typography": "字体 — Segoe UI",
  "ds.spacing": "间距刻度",
  "ds.radius": "圆角",
  "ds.shadow": "阴影 / 层级",
  "ds.shadowDesc": "6 级阴影层级，使用环境光 + 主光源双层阴影。",
};
