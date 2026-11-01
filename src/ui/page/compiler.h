#pragma once
#include "compiled_page.h"
#include "../uix/template_ast.h"
#include "../css/css_ast.h"

namespace ui::page {

// Compile a parsed template tree + CSS stylesheet into a CompiledPage.
// Walks the tree, matches selectors, builds widgets, collects bindings /
// events / loops / conditionals / menus. The QuickJS runtime reads the
// resulting CompiledPage and wires it into JS effects.
CompiledPage Compile(const ui::uix::Node& root,
                     const ui::css::Stylesheet& sheet);

// Compile a v-for iteration template — same as Compile but does NOT treat
// the root node's v-for attribute as a loop trigger (it's already being
// iterated). `parentVars` is the host page's cssVars shared_ptr so the new
// subtree's lambdas point at the SAME var table — a runtime theme switch
// affects iteration and conditional widgets together with the host.
CompiledPage CompileIterationTemplate(const ui::uix::Node& root,
                                      const ui::css::Stylesheet& sheet,
                                      std::shared_ptr<std::vector<std::pair<std::string, std::string>>> parentVars);

// Compile a v-if conditional template — same as Compile but does NOT
// treat the root node's v-if as a trigger (the runtime decided to mount).
CompiledPage CompileConditionalTemplate(const ui::uix::Node& root,
                                        const ui::css::Stylesheet& sheet,
                                        std::shared_ptr<std::vector<std::pair<std::string, std::string>>> parentVars);

// Rewrite library-provided theme tokens (--bg / --fg / --accent / ...) in
// place using the current `theme::Current()` palette. User-pinned :root
// values for the same key are wiped — semantically the library owns these
// keys. Called from `ui_theme_set_mode` to flip Light/Dark without
// recompiling pages.
void RebuildThemeVars(std::vector<std::pair<std::string, std::string>>& vars);

// Walk every live page in the global registry and run RefreshThemeStyles.
// Defined in page_api.cpp where the registry lives. Called by
// `ui_theme_set_mode`.
void RefreshAllPageThemes();

// 销毁宿主没 destroy 的全部页面。ui_shutdown 在销毁窗口之前调用, 让这些控件树
// 在 Store / 设备 / COM 都还活着时按正常顺序析构 (等同宿主自己 ui_page_destroy)。
void DestroyAllPages();

}  // namespace ui::page
