#pragma once
#include "compiled_page.h"
#include "../context_menu.h"
#include "../expression/value.h"     // expr::Value still used by ApplyBindingToWidget
#include "../uix/script_runtime.h"   // QuickJS ScriptRuntime + JSValue

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ui::page {

// Runtime host for a compiled .uix page. Owns the QuickJS runtime, the
// reactive proxy on the data object, all binding/event/loop/conditional
// runtimes, plus the menu / window plumbing.
class PageState {
public:
    PageState();
    ~PageState();

    PageState(const PageState&) = delete;
    PageState& operator=(const PageState&) = delete;

    // Install a compiled page. Spins up a fresh ScriptRuntime + reactive
    // proxy + binding effects + v-if/v-for runtimes. Existing variable
    // values set via Set* are preserved across Attach (hot reload).
    void Attach(CompiledPage page);

    // Process-wide override for the QuickJS path. The flag is now mostly
    // historical (every page goes through QuickJS); kept for env-var
    // diff-bisecting and source compatibility.
    static void SetGlobalUseQuickJS(bool v);
    static bool GetGlobalUseQuickJS();

    // ui_page_watch (build 304): 状态 name 的值变化时回调 (name, 新值 JSON)。
    // 同一 name 只留一个; cb 为空即取消。热重载后自动重新挂上。
    // 不要在回调里取消同一个监听 (回调运行在该监听的 effect 里)。
    using WatchCallback = std::function<void(const std::string& name, const std::string& json)>;
    void Watch(const std::string& name, WatchCallback cb);

    // Called by ui_page_open_window after window creation; wires <menu>
    // triggers + <menuitem onclick> dispatchers that need the HWND.
    void AttachWindow(uint64_t winHandle);

    // v-if/v-for newly-mounted subtrees may carry <menu> children — give
    // them the same trigger/dispatch wiring as compile-time menus.
    void WireSubtreeMenus(const std::vector<CompiledMenu>& subMenus,
                          std::vector<std::shared_ptr<ContextMenu>>& outMenus);

    // Access root widget.
    WidgetPtr Root() const { return page_.root; }

    // Access compiled page data (window hints, etc.)
    const CompiledPage& PageData() const { return page_; }

    // Set / get reactive variables (by name). Writes go through the proxy
    // set-trap, triggering every effect that reads the same key.
    void SetBool(const std::string& name, bool v);
    void SetNumber(const std::string& name, double v);
    void SetString(const std::string& name, const std::string& v);
    void SetValue(const std::string& name, ui::expr::Value v);

    bool GetValue(const std::string& name, ui::expr::Value& out) const;

    // ---- Internationalization ----
    void LoadTranslations(const std::string& locale,
                          const std::unordered_map<std::string, std::string>& pairs);
    void SetLocale(const std::string& locale);
    const std::string& CurrentLocale() const { return currentLocale_; }
    std::string Translate(const std::string& key) const;

    // ---- build 316: 宿主按 key 设的文字 (ui_page_bind_* / ui_page_set_text_key) ----
    // 绑定时立即按当前 locale 翻译; SetLocale 时统一重译。参数替换 {0} {1} …,
    // 以 "@" 开头的参数本身也按 key 翻译 ("@@" 开头 = 字面的 "@")。
    enum class TextSlot { Text, Tooltip, TitlebarTitle };
    struct KeyText {
        std::string               key;
        std::vector<std::wstring> args;
    };
    std::wstring FormatKeyText(const KeyText& t) const;
    void BindWidgetText(const WidgetPtr& w, TextSlot slot, KeyText t);
    bool UnbindWidgetText(Widget* w, TextSlot slot);
    void BindComboItems(const WidgetPtr& w, std::vector<std::string> keys);
    void BindWindowTitle(uint64_t win, KeyText t);
    bool UnbindWindowTitle(uint64_t win);
    void SetTextKey(const std::string& name, KeyText t);
    bool UnbindTextVar(const std::string& name);

    // Errors (compile + runtime).
    const std::vector<std::string>& Errors() const { return errors_; }

    // ---- Lifecycle hooks (Vue parity for v-if / v-for remount) ----
    // Register a callback that fires every time a widget with the given
    // `id` is mounted (initial render, v-if truthy, v-for iteration build).
    // Multiple registrations on the same id replace the previous one.
    // Callback receives the freshly-mounted widget — DO NOT cache the
    // pointer past the matching unmount.
    void OnWidgetMount(const std::string& widgetId,
                       std::function<void(Widget*)> cb);
    // Symmetric: fires just before the widget is removed from its parent.
    void OnWidgetUnmount(const std::string& widgetId,
                          std::function<void(Widget*)> cb);

    // ---- Theme ----
    // Called after `ui_theme_set_mode` flips Light/Dark. Rebuilds the
    // library-provided CSS vars (--bg / --fg / --accent / ...) in the page's
    // shared cssVars table and re-runs the cascade on every widget so any
    // `var(--xxx)` reference resolves against the new theme.
    void RefreshThemeStyles();

    // ---- Menu lookup ----
    void WireMenus();
    ContextMenu* FindMenuById(const std::string& id) const;
    void InvalidateMenuHandles();
    uint64_t GetOrRegisterMenuHandle(const std::string& name);

private:
    // CondInIter (defined in page_state.cpp) holds nested v-for loop runtimes
    // for the v-for > v-if > v-for case. It needs to name the private nested
    // type JsLoopRuntime in its own field.
    friend struct CondInIter;

    CompiledPage page_;
    std::vector<std::string> errors_;
    std::string  currentLocale_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> i18nTables_;

    // build 316: key 绑定。控件用 weak_ptr —— 被 v-if / 宿主销毁后自动失效。
    struct WidgetTextBinding {
        std::weak_ptr<Widget> widget;
        TextSlot              slot = TextSlot::Text;
        KeyText               text;
    };
    std::vector<WidgetTextBinding>              widgetTextBindings_;
    std::unordered_map<uint64_t, KeyText>       windowTitleBindings_;
    std::unordered_map<std::string, KeyText>    textVarBindings_;
    void ApplyWidgetText(Widget* w, TextSlot slot, const std::wstring& text);
    void ApplyWindowTitle(uint64_t win, const std::wstring& text);
    void ReapplyKeyTexts();

    // ---- QuickJS path state ----
    void AttachQuickJS(CompiledPage page);
    void DetachQuickJS();
    void WireQuickJSConditionals();
    // Helper extracted from WireQuickJSConditionals so nested v-if (a v-if
    // declared inside another v-if's subtree, surfaced as sub.conditionals)
    // can wire its own runtime without copy-pasting the watcher body.
    void WireConditional(const CompiledConditional& c);
    void WireQuickJSLoops();
    void WireQuickJSModelWrite(Widget* target, const std::string& propName);
    void WireQuickJSEvent(Widget* target, const std::string& evName,
                           const std::string& sourceJs);
    // DOM 事件名 → widget hook 的唯一分派表, 顶层与 v-for 行共用。invoke
    // 借用 $event (不接管所有权)。返回 false = 不支持的事件名。
    bool BindDomEvent(Widget* target, const std::string& evName,
                      std::function<void(JSValue)> invoke);
    void DispatchQuickJSResize(int width, int height);
    void ApplyBindingToWidget(Widget* w, const std::string& property,
                               const ui::expr::Value& v);

    std::unique_ptr<ui::uix::ScriptRuntime> jsRt_;
    JSValue              jsOptions_ = JS_UNDEFINED;
    JSValue              jsState_   = JS_UNDEFINED;
    std::vector<uint64_t> jsEffectIds_;
    std::vector<JSValue>  jsBindingFns_;
    std::vector<JSValue>  jsEventFns_;

    struct JsCondRuntime;
    std::vector<std::unique_ptr<JsCondRuntime>> jsCondRuntimes_;

    struct JsLoopIteration;
    struct JsLoopRuntime;
    std::vector<std::unique_ptr<JsLoopRuntime>> jsLoopRuntimes_;
    void RebuildJsLoop(JsLoopRuntime& rt);
    void TearDownJsIterations(JsLoopRuntime& rt);
    void TearDownJsLoopRuntime(JsLoopRuntime& rt);
    void DestroyJsIteration(JsLoopRuntime& rt, JsLoopIteration& iter);
    std::unique_ptr<JsLoopRuntime> BuildJsLoopRuntime(const CompiledLoop* spec);
    // Variant for v-for nested inside v-if inside v-for. The new loop's
    // listFn / keyFn / per-iter closures are compiled with the OUTER
    // iteration's loopVar / indexVar prepended in `outerParamNames`, and at
    // call time those positional args come from `outerIter`.
    std::unique_ptr<JsLoopRuntime> BuildJsLoopRuntimeInScope(
        const CompiledLoop* spec,
        const std::set<std::string>& outerLocals,
        const std::vector<std::string>& outerParamNames,
        JsLoopIteration* outerIter,
        bool outerHasIdx);
    std::unique_ptr<JsLoopIteration> BuildJsIteration(
        JsLoopRuntime& rt, JSValue itemValue, JSValue idxValue, uint32_t idx);
    std::string ComputeIterationKey(JsLoopRuntime& rt, JSValue item,
                                     JSValue idx, uint32_t pos);
    void RewireIterationBindings(JsLoopRuntime& rt, JsLoopIteration& iter);
    // Hook a per-iteration event handler (closure already compiled by
    // CompileLoopHandlerClosure) onto a widget. Closure receives
    // (item[, idx], $event) when fired. Used by both BuildJsIteration's
    // top-level events and nested v-if mount events.
    void WireLoopScopeEvent(Widget* target, const std::string& evName,
                             JSValue fn, JsLoopIteration* iter, bool hasIdx);
    // Variant that also threads the runtime's optional outer-loop scope
    // (v-for inside v-if inside v-for). When `rt->outerIter` is set, the
    // handler closure is invoked with outer item/idx prepended.
    void WireLoopScopeEventEx(Widget* target, const std::string& evName,
                               JSValue fn, JsLoopIteration* iter, bool hasIdx,
                               JsLoopRuntime* rt);
    // Build a CondInIter for a v-if compiled inside a v-for iteration's
    // subtree (recursively, so v-if inside v-if inside v-for works). Owned
    // by the parent struct that calls it.
    std::unique_ptr<struct CondInIter> BuildCondInIter(
        const CompiledConditional& c,
        const std::set<std::string>& locals,
        const std::string& loopVar,
        const std::string& indexVar,
        JsLoopIteration* iterRaw,
        bool hasIdx);
    // Walks a widget subtree firing the registered onMount / onUnmount
    // hook for each widget that has both an id and a registered hook.
    void DispatchMountHooks(Widget* root);
    void DispatchUnmountHooks(Widget* root);

    struct LifecycleHook {
        std::function<void(Widget*)> onMount;
        std::function<void(Widget*)> onUnmount;
    };
    std::unordered_map<std::string, LifecycleHook> lifecycleHooks_;

    // ---- Menus ----
    /* Build 73 (L17) reactive: WireSubtreeMenus 给每个 ContextMenu 注册
     * beforeShowHook -> PopulateMenu(menu, compiled). Show 入口先 Clear()
     * 再 PopulateMenu, 按当前 JS state 重 build items.
     * locals: v-for 里嵌套 menuitem 时的 (iter, idx) 变量名 + value pairs,
     *         空 vector 表示顶层 (无 loop scope). */
    void PopulateMenu(ContextMenu* menu, const CompiledMenu& cm,
                       const std::vector<std::pair<std::string, JSValue>>& locals);
    void PopulateMenuItems(ContextMenu* menu, const std::vector<CompiledMenuItem>& items,
                            const std::vector<std::pair<std::string, JSValue>>& locals);
    void PopulateMenuItem(ContextMenu* menu, const CompiledMenuItem& mi,
                           const std::vector<std::pair<std::string, JSValue>>& locals);
    /* build 303: 每次 populate 按当前迭代算出这一项的身份 (:id / :data-* 与
     * 内部数字 id)。循环里的项不再共用编译期的那一份。 */
    std::vector<std::pair<std::string, std::string>> ResolveMenuItemAttrs(
        const CompiledMenuItem& mi,
        const std::vector<std::pair<std::string, JSValue>>& locals,
        std::string& strId);
    int ResolveMenuItemNumericId(const CompiledMenuItem& mi, const std::string& strId,
                                 const std::vector<std::pair<std::string, JSValue>>& locals,
                                 const ContextMenu& menu);
    static constexpr int kDynamicMenuIdBase = 0x40000000;
    /* build 303: <menuitem @click> 编译成绑定了本行迭代变量的动作, 随点击载荷走。 */
    std::function<void()> MakeMenuItemAction(
        const std::string& sourceJs,
        const std::vector<std::pair<std::string, JSValue>>& locals);
    /* JS runtime 存活令牌: AttachQuickJS 建, DetachQuickJS 放。菜单动作等可能
     * 比 runtime 活得久的持有者用 weak_ptr 判断还能不能碰 JS 值。 */
    std::shared_ptr<char> jsAlive_;

    struct HostWatch {
        std::string name;
        WatchCallback cb;
        std::string last;       // 上次通知 (或初值) 的 JSON
        bool primed = false;    // 首次运行只记初值, 不通知
        uint64_t effectId = 0;
    };
    std::vector<std::unique_ptr<HostWatch>> watches_;   // 跨热重载保留
    void InstallWatch(HostWatch& w);
    /* Eval bound expr against jsState_ + optional loop-scope locals.
     * 调用方 JS_FreeValue 返回值. 失败/无 JS state 返 JS_UNDEFINED. */
    JSValue EvalBoundExpr(const std::string& rawExpr,
                           const std::vector<std::pair<std::string, JSValue>>& locals);
    /* helpers: 把 EvalBoundExpr 的 JSValue 拿成具体类型. 返回时 JS_FreeValue
     * 已处理. */
    bool        EvalBool(const std::string& expr,
                          const std::vector<std::pair<std::string, JSValue>>& locals,
                          bool defaultVal);
    std::string EvalString(const std::string& expr,
                            const std::vector<std::pair<std::string, JSValue>>& locals);
    bool        EvalMenuBackdropBlur(const std::string& expr,
                                      const std::vector<std::pair<std::string, JSValue>>& locals,
                                      float& out);

    std::vector<std::shared_ptr<ContextMenu>> menus_;
    std::unordered_map<std::string, ContextMenu*> menuById_;
    /* Build 73 (L17): CompiledMenu* → ContextMenu* 映射. PopulateMenuItem
     * 碰到 mi.submenu 时通过它找到对应的持久 ContextMenu (submenu shell 在
     * WireSubtreeMenus 时已建好, 每次 parent rebuild 用同一个 sub 句柄). */
    std::unordered_map<const CompiledMenu*, ContextMenu*> compiledToMenu_;
    /* build 343: 建页面 (也就是建 QuickJS 运行时) 的线程。菜单 beforeShowHook
     * 只在这个线程上重建 —— 子菜单悬停展开走 popup 的 WM_TIMER, 跑在
     * OverlayService 线程, 那里不能碰 JS; 子菜单内容改由父菜单 populate 时
     * 在本线程一并填好。 */
    std::thread::id ownerThread_ = std::this_thread::get_id();
    struct TriggerSpec {
        Widget*      triggerElement = nullptr;
        ContextMenu* menu = nullptr;
        bool         rclick = false;
    };
    std::vector<TriggerSpec> triggers_;
    uint64_t winHandle_ = 0;
    mutable std::unordered_map<std::string, uint64_t> menuHandleCache_;
};

}  // namespace ui::page
