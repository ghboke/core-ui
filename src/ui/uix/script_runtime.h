#pragma once
//
// script_runtime — QuickJS-based runtime for the .uix `<script>` block.
//
// Owns one JSRuntime + JSContext per page, plus a reactive bridge that
// makes plain JS objects auto-track reads (collect deps) and trigger effect
// re-runs on writes — mirrors Vue 3's `@vue/reactivity` (Proxy-based).
//
// Usage shape (sketch):
//
//     ScriptRuntime rt;
//     rt.Eval("var data = { count: 0 };");                  // user script
//
//     JSContext* ctx = rt.ctx();
//     JSValue global = JS_GetGlobalObject(ctx);
//     JSValue data   = JS_GetPropertyStr(ctx, global, "data");
//     JSValue proxy  = rt.MakeReactive(data);
//     JS_SetPropertyStr(ctx, global, "state", JS_DupValue(ctx, proxy));
//     // free: data, global ... (proxy bound as `state` keeps its own ref)
//
//     rt.WatchEffect([&] {
//         // any read of state.X registers a dep; writing state.X re-runs this
//         JSValue v = JS_Eval(ctx, "state.count", 11, "<eff>", JS_EVAL_TYPE_GLOBAL);
//         /* ... use v ... */ JS_FreeValue(ctx, v);
//     });
//
//     // later: state.count = 5 → effect re-runs automatically.

#include "quickjs.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace ui::uix {

class ScriptRuntime {
public:
    ScriptRuntime();
    ~ScriptRuntime();

    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;

    JSContext* ctx() const { return ctx_; }
    JSRuntime* rt()  const { return rt_;  }

    // Stash an arbitrary pointer (e.g., owning PageState*) so C trampolines
    // installed as JS globals can navigate back. Distinct from the slot we
    // use ourselves for trap callbacks (JS_SetContextOpaque).
    void  SetUserData(void* p) { userData_ = p; }
    void* GetUserData() const  { return userData_; }

    // Evaluate `source` as a global script. Returns true on success.
    // On error, lastError() has the formatted message.
    bool Eval(const std::string& source, const std::string& filename = "<script>");

    // Evaluate `source` as an ES module. Compiles + runs + extracts the
    // module's `export default` value. Result.ok is false on any of:
    //   - parse error in source
    //   - runtime error during top-level evaluation
    //   - no `export default` declaration
    //   - default export resolves to undefined
    // On success, .defaultExport is a refcounted JSValue (caller must
    // JS_FreeValue when done). On failure, lastError() has the message and
    // the result holds JS_UNDEFINED (no free needed).
    struct ModuleEvalResult {
        JSValue defaultExport = JS_UNDEFINED;
        bool    ok = false;
    };
    ModuleEvalResult EvalModule(const std::string& source,
                                 const std::string& filename = "<script>");

    // Compile a JS expression (already rewritten with this. prefix) into a
    // closure for binding evaluation. Output JS:
    //     (function() { return (EXPR); })
    // Returns the function JSValue (refcount=1 → caller frees, or forwards
    // ownership into long-lived storage). On parse error returns
    // JS_EXCEPTION and lastError() has the message.
    JSValue CompileBindingClosure(const std::string& jsExpr,
                                  const std::string& filename = "<binding>");

    // Compile a JS expression as an event handler. The closure takes one
    // implicit param `$event` and:
    //   1. evaluates EXPR
    //   2. if EXPR resolved to a function (e.g. `@click="inc"` where inc is
    //      a method), auto-calls it with $event — Vue 3-style auto-call
    //   3. otherwise the EXPR side-effects (assignment / call) are enough
    // Output JS:
    //   (function($event){var __r=(EXPR);if(typeof __r==='function')__r.call(this,$event);})
    JSValue CompileHandlerClosure(const std::string& jsExpr,
                                  const std::string& filename = "<handler>");

    // v-for binding/handler variants. The compiled closure takes the loop
    // var (and optional index var) as JS parameters, so per-iteration
    // values are passed at call time without recompiling per iteration.
    // EXPR must already have been rewritten with `loopVar` / `indexVar`
    // present in the locals set so they stay as bare names.
    //
    // Output JS (binding, both vars):
    //   (function(<loopVar>, <indexVar>){ return (EXPR); })
    // Output JS (handler, both vars):
    //   (function(<loopVar>, <indexVar>, $event){
    //     var __r = (EXPR);
    //     if (typeof __r === 'function') __r.call(this, $event);
    //   })
    // If indexVar is empty, it's omitted from the parameter list.
    JSValue CompileLoopBindingClosure(const std::string& jsExpr,
                                      const std::string& loopVar,
                                      const std::string& indexVar,
                                      const std::string& filename = "<binding>");
    JSValue CompileLoopHandlerClosure(const std::string& jsExpr,
                                      const std::string& loopVar,
                                      const std::string& indexVar,
                                      const std::string& filename = "<handler>");

    // Multi-param overload — used for nested v-for-in-v-if-in-v-for where
    // an inner expression can reference the outer iteration's locals plus
    // the inner ones. paramNames are concatenated in order, e.g.
    // ["grp", "gi", "item", "i"] → fn(grp, gi, item, i).
    JSValue CompileLoopBindingClosure(const std::string& jsExpr,
                                      const std::vector<std::string>& paramNames,
                                      const std::string& filename = "<binding>");
    JSValue CompileLoopHandlerClosure(const std::string& jsExpr,
                                      const std::vector<std::string>& paramNames,
                                      const std::string& filename = "<handler>");

    // Wrap an object in a reactive Proxy. Reads through the proxy register
    // a dep with the currently-active effect; writes trigger every effect
    // that previously read that (target, key). Nested objects are NOT
    // auto-wrapped in v0 (Vue does this lazily; we'll add when needed).
    //
    // Returns a Proxy JSValue with refcount = 1 (caller must JS_FreeValue
    // when done, or pass ownership via JS_DupValue + bind to global).
    JSValue MakeReactive(JSValueConst target);

    // Run `fn` once with dep tracking active. Any reactive (target, key)
    // it touches is registered. Subsequent writes to those keys schedule
    // `fn` to re-run synchronously (no batching in v0). Returns effect id.
    using Effect = std::function<void()>;
    uint64_t WatchEffect(Effect fn);

    // Stop an effect from re-running. Pending re-runs already in flight
    // for this id may still complete (no preemption).
    void DisposeEffect(uint64_t id);

    // Run `fn` with dependency tracking suspended (build 304). For calling out
    // to host code from inside an effect — reads the host makes through the
    // proxy must not become deps of the effect that happens to be running.
    template <class F>
    void RunUntracked(F&& fn) {
        uint64_t prev = activeEffectId_;
        activeEffectId_ = 0;
        fn();
        activeEffectId_ = prev;
    }

    // Register a Vue 3-style computed property keyed by (target_ptr, key).
    // `userFn` is the body (e.g. `function(){ return this.firstName + ' '
    // + this.lastName }`) and `thisObj` is the proxy bound as `this` —
    // its reads through the proxy register deps against an internal
    // watcher effect.
    //
    // First call seeds deps + caches the result. Subsequent reads of
    // proxy[key] route through GetComputed (via the get-trap) and return
    // the cache; when any dep changes, the watcher fires, marks the
    // cache dirty, and triggers every effect that depends on
    // (target_ptr, key) — typical case: bindings reading the computed.
    //
    // Returns the watcher effect id (informational).
    uint64_t DefineComputed(void* target_ptr, const std::string& key,
                             JSValueConst userFn, JSValueConst thisObj);

    // Internal: called from the proxy get-trap when key is a registered
    // computed. Recomputes if dirty + tracks the outer effect against
    // (target_ptr, key). Returned JSValue has refcount=1 (caller frees,
    // mirroring JS_GetProperty's contract).
    JSValue GetComputed(void* target_ptr, const std::string& key);

    // True if (target_ptr, key) was registered via DefineComputed.
    bool IsComputed(void* target_ptr, const std::string& key) const;

    const std::string& lastError() const { return lastError_; }

    // ---- Internal hooks for the static C trampolines.
    // Public so static methods on this class (which the trampolines forward
    // to) can reach them. Not intended to be called directly by consumers.
    void TrackRead   (void* target_ptr, const std::string& key);
    void TriggerWrite(void* target_ptr, const std::string& key);

private:
    JSRuntime*  rt_;
    JSContext*  ctx_;
    std::string lastError_;
    void*       userData_ = nullptr;

    // (target_ptr, key) → set<effect_id> that read this property.
    struct DepKey {
        void*       target;
        std::string key;
        bool operator==(const DepKey& o) const noexcept {
            return target == o.target && key == o.key;
        }
    };
    struct DepKeyHash {
        size_t operator()(const DepKey& k) const noexcept {
            return std::hash<void*>{}(k.target) ^ (std::hash<std::string>{}(k.key) << 1);
        }
    };
    std::unordered_map<DepKey, std::set<uint64_t>, DepKeyHash> depMap_;

    // Effect storage + the currently-tracking effect (0 = none).
    std::unordered_map<uint64_t, Effect> effects_;
    uint64_t                              activeEffectId_ = 0;
    uint64_t                              nextEffectId_   = 1;

    // C trampolines for Proxy traps — installed once at construction so we
    // can reuse the JSValues for every MakeReactive call.
    JSValue trapGet_ = JS_UNDEFINED;
    JSValue trapSet_ = JS_UNDEFINED;

    // Vue 3-style computed: lazy memoized derived value. Storage owns
    // refcounted JSValues; destructor must run before JSContext teardown.
    struct ComputedRef {
        JSValue userFn      = JS_UNDEFINED;
        JSValue thisObj     = JS_UNDEFINED;
        JSValue cachedValue = JS_UNDEFINED;
        bool    dirty       = true;
        uint64_t watcherId  = 0;
        void*       target  = nullptr;
        std::string key;
    };
    std::unordered_map<DepKey, std::unique_ptr<ComputedRef>, DepKeyHash> computeds_;
};

}  // namespace ui::uix
