#pragma once
#include "../widget.h"
#include "../uix/template_ast.h"
#include "../css/css_ast.h"
#include <memory>
#include <string>
#include <vector>

namespace ui::page {

// A reactive binding: raw JS expression source (rewritten + compiled to a
// closure at attach time, then re-evaluated on dep change with the result
// dispatched to a widget property).
//
//   text interpolation `Hello, {{ name }}!` → `"Hello, " + (name) + "!"`
//   :class="active ? 'on' : 'off'"          → `active ? 'on' : 'off'`
//   v-show="visible"                         → `visible`
//   v-model on `userName`                    → `userName`  (read side)
struct CompiledBinding {
    Widget* target = nullptr;
    std::string property;
    std::string sourceJs;
};

// An event handler: raw JS source, compiled to a closure at attach time.
//   `inc`                — bare ident (resolves to a method, called with $event)
//   `onDel(item.id)`     — call expression
//   `count = count + 1`  — assignment statement
struct CompiledEvent {
    Widget* target = nullptr;
    std::string event;
    std::string sourceJs;
};

// v-model target: on user-input change, write the widget's new value back
// into the named state property (via the JS proxy's set-trap).
struct CompiledModelWrite {
    Widget* target = nullptr;
    std::string propertyName;
};

// v-if: truthy → template subtree mounts as parentWidget's child at
// insertIndex; falsy → it's unmounted. Mount/unmount, not v-show toggle.
struct CompiledConditional {
    Widget* parentWidget = nullptr;
    const ui::uix::Node* templateNode = nullptr;
    std::string condSourceJs;
    size_t insertIndex = 0;
};

// v-for: runtime watches listSourceJs; on any list change, tear down old
// iterations and rebuild from the new array. Per-iteration bindings get
// `loopVar` and (optional) `indexVar` as closure parameters.
struct CompiledLoop {
    Widget* parentWidget = nullptr;
    const ui::uix::Node* templateNode = nullptr;
    std::string loopVar;
    std::string indexVar;
    std::string listName;
    std::string listSourceJs;
    std::string keySourceJs;
    size_t insertIndex = 0;
};

// Static window configuration extracted from a top-level <window .../> tag.
// All fields default to "unset"; consumer merges onto a UiWindowConfig.
struct WindowHints {
    bool        present     = false;  // set if <window> tag existed
    std::wstring title;                // empty → unset
    int         width       = 0;      // 0 → unset (use caller default)
    int         height      = 0;
    bool        autoHeight  = false;  // height="auto": 高度跟随内容 (build 328)
    int         minWidth    = 0;
    int         minHeight   = 0;
    int         resizable   = -1;     // -1=unset, 0=no, 1=yes
    int         frameless   = -1;     // -1=unset, 0=system frame, 1=frameless custom chrome
    int         centered    = -1;     // -1=unset, 0=use x,y, 1=center on screen
    int         theme       = -1;     // -1=unset, 0=dark, 1=light
};

// 编译期收集的 <menu>: 一棵 ContextMenu 结构 + 可选的"trigger 元素 @ event"
// 自动挂载. PageState::Attach 把它实例化成 ContextMenuPtr 并 wire callback.
struct CompiledMenu;
using CompiledMenuPtr = std::shared_ptr<CompiledMenu>;

struct CompiledMenuItem {
    /* BREAKING (build 75 / L17 follow-up): menuitem 重设计成 widget slot —
     * 老的 text / shortcut(文本字段) / iconSvg / imgSrc / hasColor / color_* /
     * boundText / boundIcon / boundStyle 全删. <menuitem> body 内任何 widget
     * 子节点 (svg / label / div / button / ...) 都按普通 widget 编译, 走
     * 完整 reactive binding 路径. 给视觉自由度 + 反应式正交性 + 砍掉所有
     * "static / bound 双路径" heuristic. */
    int          itemId = 0;            // 数字 id (内部 hit-test/debug/onClick-map); 非数字 id 时 autoId 兜底
    std::string  strId;                 // 原始 id 字符串 (key 如 "cmd_delete" 或数字串 "1000"), C callback 用
    std::vector<std::pair<std::string,std::string>> attrs;  // <menuitem> 全部静态属性 (含 id), callback 可读
    std::string  shortcut;              // 静态 shortcut 显示文本 (例 "Ctrl+S")
    std::string  onClick;               // 触发时调用的 JS method 名
    bool         separator = false;     // true → <separator/>
    CompiledMenuPtr submenu;            // 嵌套 submenu (parent item 旁边有 ▸ arrow)
    /* build 303: <template v-for / v-if> — 自身不产生行, 按条件 / 迭代把
     * groupItems 展开到所在菜单 (Vue 的 <template> 语义)。 */
    bool         group = false;
    std::vector<CompiledMenuItem> groupItems;
    /* Body 子节点 (AST) — 反应式 widget 模板. PopulateMenuItem 在每次 Show
     * 时用 CompileIterationTemplate 实例化一棵新 widget tree, 装到 MenuItem.
     * 多次 Show 之间 widget tree 全部重建 (跟 v-for 同款 pattern). 没有
     * children → contentRoot 为空 (空 menuitem, 只占位 + ID 派发). */
    ui::uix::NodePtr contentRoot;
    /* 反应式 attrs — 跟 widget 系 :x / v-if / v-for 同套 expr 求值. */
    std::string  boundShortcutExpr; // :shortcut="..." — 求 string
    std::string  boundEnabledExpr;  // :enabled="..." — 求 bool, false → disabled
    /* 其余 :attr (含 :id / :data-*) — 每次 populate 按当前迭代求值成字符串,
     * 覆盖进该项 attrs; :id 同时决定 strId (build 303)。 */
    std::vector<std::pair<std::string,std::string>> boundAttrs;
    std::string  vIfExpr;           // v-if / v-show
    std::string  vForArrayExpr;     // v-for="x in items"
    std::string  vForIterVar;
    std::string  vForIndexVar;
};

struct CompiledMenu {
    std::string  id;                    // <menu id="X"> ; 可空
    std::string  triggerSelector;       // "#btnId" → 该元素 click/rclick 自动 show menu
    std::string  triggerEvent;          // "click" (默认) | "rclick"
    std::string  rowClass;              // extra class appended to generated .menuitem-row
    bool         shareWidthWithSubmenus = true; // keep submenu tree at least parent width
    bool         hasBgColor = false;
    D2D1_COLOR_F bgColor = {};
    std::string  boundBgColorExpr;      // :background / :background-color / :bg-color / :bgColor
    bool         hasFrostedMaterial = false;
    bool         frostedMaterial = false;
    std::string  boundFrostedMaterialExpr; // :frosted-material / :material — 求 bool
    bool         hasBackdropBlur = false;
    float        backdropBlur = -1.0f;
    float        minWidth = 0.0f;       // build 307: min-width (DIP), 0 = 不限; 子菜单不继承
    std::string  boundBackdropBlurExpr; // :backdrop-blur / :backdrop-filter — 求 number 或 blur(...)
    std::vector<CompiledMenuItem> items;
    /* Phase E (L17 / build 73): 整个 menu / submenu 的 v-if / v-show. 求 false
     * 时 PopulateMenu 整体 skip 不 build items, ShowMenu 触发也是空菜单. */
    std::string  vIfExpr;
};

struct CompiledPage {
    WidgetPtr root;
    std::vector<CompiledBinding> bindings;
    std::vector<CompiledEvent> events;
    std::vector<CompiledModelWrite> modelWrites;
    std::vector<CompiledLoop> loops;
    std::vector<CompiledConditional> conditionals;
    std::vector<CompiledMenu> menus;
    // Raw <script> block content (Vue 3 SFC `export default {…}`); fed to
    // ScriptRuntime::EvalModule at attach time.
    std::string scriptSource;
    WindowHints windowHints;
    std::vector<std::string> errors;

    // The original HTML AST + CSS must live through the page's lifetime so v-for
    // iterations can re-compile their template subtree AND so widget recompute
    // lambdas hold stable pointers into the stylesheet (hence heap-allocated).
    std::unique_ptr<ui::uix::Node> ownedHtml;
    std::unique_ptr<ui::css::Stylesheet> ownedStylesheet;

    // CSS variables (`:root { --x: ... }` plus library-injected theme tokens
    // like `--bg / --fg / --accent`). Held via shared_ptr so all widget
    // recomputeStyle lambdas share ONE source of truth — `ui_theme_set_mode`
    // overwrites the contents in place and the next cascade pass on every
    // widget picks up new values without re-binding any lambda.
    std::shared_ptr<std::vector<std::pair<std::string, std::string>>> cssVars;
};

}  // namespace ui::page
