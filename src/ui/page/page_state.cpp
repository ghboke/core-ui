#include "page_state.h"
#include "compiler.h"
#include "svg_widget.h"
#include "../event.h"
#include "../expression/json.h"
#include "../ui_context.h"
#include "../ui_window.h"
#ifdef small
#undef small
#endif
#include "../controls.h"
#include "../animation.h"
#include "../asset.h"
#include "../css/value.h"
#include "../debug_trace.h"
#include "../uix/expr_rewriter.h"
#include "../uix/value_convert.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace ui::page {


namespace {

std::wstring ToWide(const std::string& s) {
    std::wstring r;
    r.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        int len = 0;
        if ((c & 0x80) == 0)          { cp = c;        len = 1; }
        else if ((c & 0xE0) == 0xC0)  { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0)  { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0)  { cp = c & 0x07; len = 4; }
        else                           { i++; continue; }
        if (i + len > s.size()) break;
        for (int k = 1; k < len; k++) {
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        }
        i += len;
        if (cp < 0x10000) {
            r += static_cast<wchar_t>(cp);
        } else {
            cp -= 0x10000;
            r += static_cast<wchar_t>(0xD800 | (cp >> 10));
            r += static_cast<wchar_t>(0xDC00 | (cp & 0x3FF));
        }
    }
    return r;
}

std::string ToUtf8(const std::wstring& s) {
    std::string r;
    r.reserve(s.size() * 2);
    for (size_t i = 0; i < s.size(); ++i) {
        uint32_t cp = static_cast<uint32_t>(s[i]);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size()) {
            uint32_t low = static_cast<uint32_t>(s[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        if (cp < 0x80) {
            r += static_cast<char>(cp);
        } else if (cp < 0x800) {
            r += static_cast<char>(0xC0 | (cp >> 6));
            r += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            r += static_cast<char>(0xE0 | (cp >> 12));
            r += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            r += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            r += static_cast<char>(0xF0 | (cp >> 18));
            r += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            r += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            r += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return r;
}

bool ParseBackdropBlurText(const std::string& value, float& out) {
    auto pv = ui::css::ParseValue(value);
    if (!pv.ok) return false;
    auto compToPx = [](const ui::css::Component& c, float& px) -> bool {
        if (c.kind == ui::css::ComponentKind::Number) {
            px = static_cast<float>(c.number);
            return true;
        }
        if (c.kind == ui::css::ComponentKind::Length) {
            double v = 0.0;
            if (ui::css::ResolveLengthPx(c.length, 0, 14, 14, 1920, 1080, v)) {
                px = static_cast<float>(v);
                return true;
            }
        }
        if (c.kind == ui::css::ComponentKind::Ident && c.ident == "auto") {
            px = -1.0f;
            return true;
        }
        if (c.kind == ui::css::ComponentKind::Ident &&
            (c.ident == "none" || c.ident == "off")) {
            px = 0.0f;
            return true;
        }
        return false;
    };
    for (const auto& c : pv.components) {
        if (c.kind == ui::css::ComponentKind::Function && c.ident == "blur" &&
            !c.args.empty()) {
            return compToPx(c.args[0], out);
        }
        if (compToPx(c, out)) return true;
    }
    return false;
}

void ApplyDynamicPositionOverrides(Widget* w) {
    if (w) w->ApplyDynamicPositionOverrides();
}

void ParseDynamicAbsSide(const ui::expr::Value& v, float& slot, std::string& rawSlot) {
    slot = -1.0f;
    rawSlot.clear();
    if (v.IsNumber()) {
        slot = static_cast<float>(v.Number());
        return;
    }
    std::string text = v.ToString();
    if (text.empty() || text == "auto" || text == "null") return;

    ui::css::Length len;
    if (ui::css::ParseLength(text, len) && len.unit != ui::css::Unit::Auto &&
        len.unit != ui::css::Unit::Percent) {
        double px = 0.0;
        if (ui::css::ResolveLengthPx(len, 0, 14, 14, 1920, 1080, px)) {
            slot = static_cast<float>(px);
            return;
        }
    }
    rawSlot = text;
}

bool ApplyDynamicAbsSideBinding(Widget* w, const std::string& prop,
                                const ui::expr::Value& v) {
    if (!w) return false;
    float parsed = -1.0f;
    std::string raw;
    ParseDynamicAbsSide(v, parsed, raw);

    if (prop == "left") {
        w->dynamicPosLeftSet = true;
        w->dynamicPosLeft = parsed;
        w->dynamicPosLeftRaw = std::move(raw);
    } else if (prop == "top") {
        w->dynamicPosTopSet = true;
        w->dynamicPosTop = parsed;
        w->dynamicPosTopRaw = std::move(raw);
    } else if (prop == "right") {
        w->dynamicPosRightSet = true;
        w->dynamicPosRight = parsed;
        w->dynamicPosRightRaw = std::move(raw);
    } else if (prop == "bottom") {
        w->dynamicPosBottomSet = true;
        w->dynamicPosBottom = parsed;
        w->dynamicPosBottomRaw = std::move(raw);
    } else {
        return false;
    }
    ApplyDynamicPositionOverrides(w);
    ui::RequestLayout();
    return true;
}

// C trampoline for `$t(key)`. Bound on the data object so user templates
// see it as `this.$t(...)` after the rewriter pass. Reading `this_val.$locale`
// through the proxy registers a dep on $locale so the binding re-runs whenever
// SetLocale() updates it.
JSValue JsTranslateTrampoline(JSContext* ctx, JSValueConst this_val,
                               int argc, JSValueConst* argv) {
    auto* rt = static_cast<ui::uix::ScriptRuntime*>(JS_GetContextOpaque(ctx));
    if (!rt) return JS_UNDEFINED;
    auto* page = static_cast<PageState*>(rt->GetUserData());
    if (!page || argc < 1) return JS_UNDEFINED;

    JSValue localeV = JS_GetPropertyStr(ctx, this_val, "$locale");
    JS_FreeValue(ctx, localeV);

    const char* key = JS_ToCString(ctx, argv[0]);
    if (!key) return JS_NewString(ctx, "");
    std::string out = page->Translate(key);
    JS_FreeCString(ctx, key);
    return JS_NewStringLen(ctx, out.data(), out.size());
}

PageState* PageFromJs(JSContext* ctx) {
    auto* rt = static_cast<ui::uix::ScriptRuntime*>(JS_GetContextOpaque(ctx));
    return rt ? static_cast<PageState*>(rt->GetUserData()) : nullptr;
}

UiWindowImpl* WindowForPage(PageState* page) {
    if (!page) return nullptr;
    auto root = page->Root();
    return root ? ui::GetContext().FindWindowByWidget(root.get()) : nullptr;
}

void EnsurePageLayout(PageState* page) {
    if (auto* win = WindowForPage(page)) win->LayoutRoot();
}

bool IsEffectivelyVisible(const Widget* w) {
    for (auto* p = w; p; p = p->Parent()) {
        if (!p->visible) return false;
    }
    return true;
}

void SetObjNumber(JSContext* ctx, JSValueConst obj, const char* name, double v) {
    JS_SetPropertyStr(ctx, obj, name, JS_NewFloat64(ctx, v));
}

JSValue JsRectTrampoline(JSContext* ctx, JSValueConst /*this_val*/,
                         int argc, JSValueConst* argv) {
    auto* page = PageFromJs(ctx);
    if (!page || argc < 1) return JS_NULL;

    const char* id = JS_ToCString(ctx, argv[0]);
    if (!id || !*id) {
        if (id) JS_FreeCString(ctx, id);
        return JS_NULL;
    }

    EnsurePageLayout(page);
    auto root = page->Root();
    Widget* w = root ? root->FindById(id) : nullptr;
    JS_FreeCString(ctx, id);
    if (!w) return JS_NULL;

    const float left = w->rect.left;
    const float top = w->rect.top;
    const float right = w->rect.right;
    const float bottom = w->rect.bottom;
    JSValue obj = JS_NewObject(ctx);
    SetObjNumber(ctx, obj, "left", left);
    SetObjNumber(ctx, obj, "top", top);
    SetObjNumber(ctx, obj, "right", right);
    SetObjNumber(ctx, obj, "bottom", bottom);
    SetObjNumber(ctx, obj, "width", std::max(0.0f, right - left));
    SetObjNumber(ctx, obj, "height", std::max(0.0f, bottom - top));
    SetObjNumber(ctx, obj, "centerX", (left + right) * 0.5f);
    SetObjNumber(ctx, obj, "centerY", (top + bottom) * 0.5f);
    JS_SetPropertyStr(ctx, obj, "visible", JS_NewBool(ctx, IsEffectivelyVisible(w)));
    return obj;
}

JSValue JsWindowSizeTrampoline(JSContext* ctx, JSValueConst /*this_val*/,
                               int /*argc*/, JSValueConst* /*argv*/) {
    auto* page = PageFromJs(ctx);
    EnsurePageLayout(page);

    JSValue obj = JS_NewObject(ctx);
    auto root = page ? page->Root() : nullptr;
    float width = 0.0f;
    float height = 0.0f;
    if (root) {
        width = std::max(0.0f, root->rect.right - root->rect.left);
        height = std::max(0.0f, root->rect.bottom - root->rect.top);
    }
    SetObjNumber(ctx, obj, "width", width);
    SetObjNumber(ctx, obj, "height", height);
    SetObjNumber(ctx, obj, "dpiScale",
                 WindowForPage(page) ? WindowForPage(page)->DpiScale() : 1.0f);
    return obj;
}

JSValue JsNextTickTrampoline(JSContext* ctx, JSValueConst this_val,
                             int argc, JSValueConst* argv) {
    auto* page = PageFromJs(ctx);
    EnsurePageLayout(page);
    if (argc > 0 && JS_IsFunction(ctx, argv[0])) {
        return JS_Call(ctx, argv[0], this_val, 0, nullptr);
    }
    return JS_UNDEFINED;
}

// Find a transition spec on this widget for the given AnimProperty id.
const Widget::TransitionSpec* FindTransition(const Widget* w, int propId) {
    for (const auto& t : w->transitions) {
        if (t.property == propId) return &t;
    }
    return nullptr;
}

UiWindowImpl* OwnerWindow(Widget* w) {
    return ui::GetContext().FindWindowByWidget(w);
}

/* 直接落值前掐掉这条属性上在飞的动画。
 *
 * 少了这一步就会丢写: 宿主同一帧先把属性推到 A (起了 transition 动画, 此刻
 * widget 上的值还是旧值), 紧接着又推 B; 第二次因为"当前值跟 B 已经相等"走
 * 直接落值分支, 值写成了 B, 但 A 那条动画还在飞, 下一帧就覆盖回 A。
 * 症状是最后一次写像丢了, invalidate 也救不回来 (下游 GuoheView bug-070)。 */
void CancelAnim(Widget* w, AnimProperty prop) {
    if (auto* win = OwnerWindow(w)) win->CancelPropertyAnimation(w, prop);
}

void SetOpacityMaybeAnimated(Widget* w, float target) {
    const auto* t = FindTransition(w, 0);
    if (t && std::abs(w->opacity - target) > 0.001f) {
        if (auto* win = OwnerWindow(w)) {
            win->AnimateProperty(w, AnimProperty::Opacity, w->opacity, target,
                                 t->durationMs, static_cast<EasingFunction>(t->easing));
            return;
        }
    }
    CancelAnim(w, AnimProperty::Opacity);
    w->opacity = target;
}

void SetWidthMaybeAnimated(Widget* w, float target) {
    const auto* t = FindTransition(w, 3);
    if (t && std::abs(w->fixedW - target) > 0.5f && w->fixedW > 0) {
        if (auto* win = OwnerWindow(w)) {
            win->AnimateProperty(w, AnimProperty::Width, w->fixedW, target,
                                 t->durationMs, static_cast<EasingFunction>(t->easing));
            return;
        }
    }
    CancelAnim(w, AnimProperty::Width);
    w->fixedW = target;
    ui::RequestLayout();
}

void SetHeightMaybeAnimated(Widget* w, float target) {
    const auto* t = FindTransition(w, 4);
    if (t && std::abs(w->fixedH - target) > 0.5f && w->fixedH > 0) {
        if (auto* win = OwnerWindow(w)) {
            win->AnimateProperty(w, AnimProperty::Height, w->fixedH, target,
                                 t->durationMs, static_cast<EasingFunction>(t->easing));
            return;
        }
    }
    CancelAnim(w, AnimProperty::Height);
    w->fixedH = target;
    ui::RequestLayout();
}

void SetBgMaybeAnimated(Widget* w, const D2D1_COLOR_F& target) {
    const auto* tA = FindTransition(w, 8);
    if (tA) {
        if (auto* win = OwnerWindow(w)) {
            auto easing = static_cast<EasingFunction>(tA->easing);
            win->AnimateProperty(w, AnimProperty::BgColorR, w->bgColor.r, target.r, tA->durationMs, easing);
            win->AnimateProperty(w, AnimProperty::BgColorG, w->bgColor.g, target.g, tA->durationMs, easing);
            win->AnimateProperty(w, AnimProperty::BgColorB, w->bgColor.b, target.b, tA->durationMs, easing);
            win->AnimateProperty(w, AnimProperty::BgColorA, w->bgColor.a, target.a, tA->durationMs, easing);
            return;
        }
    }
    CancelAnim(w, AnimProperty::BgColorR);
    CancelAnim(w, AnimProperty::BgColorG);
    CancelAnim(w, AnimProperty::BgColorB);
    CancelAnim(w, AnimProperty::BgColorA);
    w->bgColor = target;
}

}  // namespace

PageState::PageState() = default;
PageState::~PageState() {
    if (winHandle_) {
        if (auto* win = ui::GetContext().GetWindow(winHandle_)) {
            win->onPageResize = nullptr;
        }
    }
    DetachQuickJS();
}

// ---- Reactive state writes ------------------------------------------------
//
// All writes route through the JS proxy's set-trap when the runtime is
// attached. Without a runtime (pre-attach Set* calls — typical for callers
// that prime state via ui_page_set_int before ui_page_open_window), we
// stash the value and replay it inside AttachQuickJS… that path isn't
// implemented here; callers should attach first. The C API guards against
// nullptr state already.

void PageState::SetBool(const std::string& name, bool v) {
    if (!jsRt_ || JS_IsUndefined(jsState_)) return;
    JS_SetPropertyStr(jsRt_->ctx(), jsState_, name.c_str(),
                      JS_NewBool(jsRt_->ctx(), v));
}

void PageState::SetNumber(const std::string& name, double v) {
    if (!jsRt_ || JS_IsUndefined(jsState_)) return;
    JS_SetPropertyStr(jsRt_->ctx(), jsState_, name.c_str(),
                      JS_NewFloat64(jsRt_->ctx(), v));
}

void PageState::SetString(const std::string& name, const std::string& v) {
    if (!jsRt_ || JS_IsUndefined(jsState_)) return;
    JS_SetPropertyStr(jsRt_->ctx(), jsState_, name.c_str(),
                      JS_NewStringLen(jsRt_->ctx(), v.data(), v.size()));
}

void PageState::SetValue(const std::string& name, ui::expr::Value v) {
    // Primitive types route to the typed setters above. Object / Array are
    // serialized to JSON then JS_ParseJSON — covers ui_page_set_json's
    // "set arbitrary structured data" use case without us hand-rolling an
    // expr::Value → JSValue converter.
    if (!jsRt_ || JS_IsUndefined(jsState_)) return;
    JSContext* ctx = jsRt_->ctx();
    if (v.IsBool())   { SetBool(name, v.Bool());     return; }
    if (v.IsNumber()) { SetNumber(name, v.Number()); return; }
    if (v.IsString()) { SetString(name, v.ToString()); return; }
    if (v.IsNull()) {
        JS_SetPropertyStr(ctx, jsState_, name.c_str(), JS_NULL);
        return;
    }
    std::string json = ui::expr::EmitJson(v);
    JSValue parsed = JS_ParseJSON(ctx, json.c_str(), json.size(), "<set_value>");
    if (JS_IsException(parsed)) {
        JSValue exc = JS_GetException(ctx); JS_FreeValue(ctx, exc);
        return;
    }
    JS_SetPropertyStr(ctx, jsState_, name.c_str(), parsed);
}

bool PageState::GetValue(const std::string& name, ui::expr::Value& out) const {
    if (!jsRt_ || JS_IsUndefined(jsState_)) return false;
    JSContext* ctx = jsRt_->ctx();
    JSValue v = JS_GetPropertyStr(ctx, jsState_, name.c_str());
    if (JS_IsUndefined(v) || JS_IsException(v)) {
        JS_FreeValue(ctx, v);
        return false;
    }
    out = ui::uix::JSValueToExprValue(ctx, v);
    JS_FreeValue(ctx, v);
    return true;
}

// ---- i18n ------------------------------------------------------------------

std::string PageState::Translate(const std::string& key) const {
    auto it = i18nTables_.find(currentLocale_);
    if (it != i18nTables_.end()) {
        auto k = it->second.find(key);
        if (k != it->second.end()) return k->second;
    }
    return key;
}

void PageState::LoadTranslations(const std::string& locale,
                                 const std::unordered_map<std::string, std::string>& pairs) {
    i18nTables_[locale] = pairs;
}

void PageState::SetLocale(const std::string& locale) {
    if (locale != currentLocale_) {
        currentLocale_ = locale;
        // Mirror onto the proxy's $locale slot — set-trap fires, every binding
        // that called $t() (which reads $locale internally) re-evaluates.
        SetString("$locale", locale);
    }
    // L83: combobox `<option>@key</option>` items are NOT $t bindings (the
    // compiler bakes them into a static item list), so the $locale reactivity
    // above doesn't refresh them. Walk the tree and re-translate any combobox
    // carrying i18n keys. Run unconditionally so the compiler's @key
    // placeholders resolve on the first SetLocale even when the locale is
    // unchanged (e.g. app default == initial set).
    std::function<void(Widget*)> retranslate = [&](Widget* w) {
        if (!w) return;
        if (auto* cb = dynamic_cast<ComboBoxWidget*>(w)) {
            if (cb->HasI18nKeys()) {
                cb->RetranslateItems([this](const std::string& k) {
                    return ToWide(Translate(k));
                });
            }
        }
        for (auto& c : w->Children()) retranslate(c.get());
    };
    if (page_.root) retranslate(page_.root.get());

    // Locale change rewrote every $t() label's text via ApplyBindingToWidget,
    // which only repaints (InvalidateAllWindows) — it does NOT relayout. A label
    // measured in the old locale keeps that rect, so a wider translation (e.g.
    // zh "拖入图片到这里" → en "Drop an image here") wraps/overflows inside the
    // stale width. Do ONE relayout after the bulk text swap so every label
    // re-measures (LabelWidget::SizeHint is live) and re-fits — independent of
    // whether the window is foreground / gets a later paint. One pass, not
    // per-binding, keeps a 50-label locale switch from doing 50 full relayouts.
    ui::GetContext().RelayoutAllWindows();
}

// ---- Lifecycle hooks --------------------------------------------------

void PageState::OnWidgetMount(const std::string& widgetId,
                               std::function<void(Widget*)> cb) {
    if (widgetId.empty()) return;
    lifecycleHooks_[widgetId].onMount = cb;
    // Fire immediately if a matching widget is already in the live tree.
    // Typical call order is `ui_page_load_* → ui_page_on_widget_mount →
    // ui_page_open_window`, so by the time the hook is registered the
    // initial mount has long since happened — without this catch-up the
    // hook would only ever fire on later v-if / v-for rebuilds. Matches
    // Vue's `watchEffect(immediate)` semantics for "is currently mounted".
    if (cb && page_.root) {
        if (Widget* w = page_.root->FindById(widgetId)) {
            cb(w);
        }
    }
}

void PageState::OnWidgetUnmount(const std::string& widgetId,
                                  std::function<void(Widget*)> cb) {
    if (widgetId.empty()) return;
    lifecycleHooks_[widgetId].onUnmount = std::move(cb);
}

void PageState::DispatchMountHooks(Widget* root) {
    if (!root || lifecycleHooks_.empty()) return;
    std::function<void(Widget*)> walk = [&](Widget* w) {
        if (!w) return;
        if (!w->id.empty()) {
            auto it = lifecycleHooks_.find(w->id);
            if (it != lifecycleHooks_.end() && it->second.onMount) {
                it->second.onMount(w);
            }
        }
        for (auto& c : w->Children()) walk(c.get());
    };
    walk(root);
}

void PageState::DispatchUnmountHooks(Widget* root) {
    if (!root || lifecycleHooks_.empty()) return;
    std::function<void(Widget*)> walk = [&](Widget* w) {
        if (!w) return;
        if (!w->id.empty()) {
            auto it = lifecycleHooks_.find(w->id);
            if (it != lifecycleHooks_.end() && it->second.onUnmount) {
                it->second.onUnmount(w);
            }
        }
        for (auto& c : w->Children()) walk(c.get());
    };
    walk(root);
}

void PageState::RefreshThemeStyles() {
    TraceScope themeScope("page_state", "refresh_theme_duration");
    if (!page_.cssVars) return;
    RebuildThemeVars(*page_.cssVars);
    // Walk the live widget tree and re-cascade. recomputeStyle reads
    // cssVars via the captured shared_ptr, which now points at the rebuilt
    // theme palette — so every `var(--xxx)` resolves against new colors.
    std::function<void(Widget*)> walk = [&](Widget* w) {
        if (!w) return;
        if (w->recomputeStyle) w->recomputeStyle(w->lastStateBits);
        for (auto& c : w->Children()) walk(c.get());
    };
    walk(page_.root.get());
    ui::GetContext().InvalidateAllWindows();
}

// ---- Binding application: JSValue → Widget property ------------------------

void PageState::ApplyBindingToWidget(Widget* w, const std::string& prop, const ui::expr::Value& v) {
    if (!w) return;
    TraceScope bindingScope("page_state", "apply_binding_duration");
    if (IsTraceEnabled()) {
        TraceEvent("page_state", "apply_binding",
                   {TraceStr("prop", prop.c_str()),
                    TraceStr("widget_type", w->cssTag.c_str()),
                    TraceStr("widget_id", w->id.c_str())});
    }
    auto traceBindingNoop = [&]() {
        if (IsTraceEnabled()) {
            TraceEvent("page_state", "binding_noop", {TraceStr("prop", prop.c_str())});
        }
    };
    bool shouldInvalidate = true;

    // Tail-call invalidator: every binding application needs to repaint
    // (LabelWidget::SetText etc. only mutate state without their own
    // InvalidateRect, so a click-handler-driven count++ wouldn't visibly
    // update until the next unrelated event provoked a redraw).
    // Also kicks per-widget animation timers so a programmatic state change
    // that flips a Toggle/CheckBox into animating_=true actually starts
    // ticking even outside a mouse/key event handler.
    struct InvalidateOnExit {
        bool& shouldInvalidate;
        ~InvalidateOnExit() {
            if (!shouldInvalidate) return;
            /* build 285: 走合批版本。批量绑定 (v-for 建列表) 期间只记待办,
             * 批结束统一各做一次 —— UpdateAnimTimers 单次是 O(整棵树), 每条
             * 绑定都调一次会让整体退化成 O(n^2)。非批量场景行为不变。 */
            ui::GetContext().RequestInvalidateAll();
            ui::GetContext().RequestAnimTimerUpdate();
        }
    } _invalidate{shouldInvalidate};

    // SVG shape attribute: "shape[N].<attr>" routes to SvgWidget::SetShapeProperty.
    if (prop.size() > 6 && prop.compare(0, 6, "shape[") == 0) {
        size_t close = prop.find(']', 6);
        if (close != std::string::npos && close + 1 < prop.size() && prop[close + 1] == '.') {
            size_t idx = 0;
            try { idx = (size_t)std::stoul(prop.substr(6, close - 6)); } catch (...) { return; }
            std::string name = prop.substr(close + 2);
            if (auto* svg = dynamic_cast<SvgWidget*>(w)) {
                svg->SetShapeProperty(idx, name, v.ToString());
            }
        }
        return;
    }

    if (prop == "text") {
        if (auto* lbl = dynamic_cast<LabelWidget*>(w))  lbl->SetText(ToWide(v.ToString()));
        else if (auto* btn = dynamic_cast<ButtonWidget*>(w)) btn->SetText(ToWide(v.ToString()));
        else if (auto* nav = dynamic_cast<NavItemWidget*>(w)) nav->SetText(ToWide(v.ToString()));
        return;
    }
    if (prop == "id") {
        /* :id="..." 动态 id (build 98+ L26). 主用例 v-for 给每个 iteration
         * 唯一 id 让 ui_page_on_widget_mount 能挂回调. 调用时机: bindings 首次
         * eval 时设 w->id, 紧接着 DispatchMountHooks 走 tree 命中 hook. id
         * 之后变化只更新值, 不联动 unmount/mount lifecycle — caller 别依赖. */
        w->id = v.ToString();
        return;
    }
    if (prop == "class") {
        std::vector<std::string> tokens;
        std::string s = v.ToString();
        std::string tok;
        for (char c : s) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                if (!tok.empty()) { tokens.push_back(std::move(tok)); tok.clear(); }
            } else tok += c;
        }
        if (!tok.empty()) tokens.push_back(std::move(tok));
        w->dynamicClasses = std::move(tokens);
        std::function<void(Widget*)> refresh = [&](Widget* x) {
            if (x->recomputeStyle) x->recomputeStyle(x->lastStateBits);
            for (auto& c : x->Children()) refresh(c.get());
        };
        refresh(w);
        return;
    }
    if (prop == "chip-labels" || prop == "chipLabels") {
        if (auto* ci = dynamic_cast<ChipsInputWidget*>(w))
            ci->SetChipLabels(ToWide(v.ToString()));
        return;
    }
    if (prop == "draggable") {
        w->draggable = v.ToBool();
        return;
    }
    if (prop == "drag-data" || prop == "dragData") {
        w->draggable = true;  // payload implies draggable (matches factory)
        w->dragData = ToWide(v.ToString());
        return;
    }
    if (prop == "drop-target" || prop == "dropTarget") {
        w->dropTarget = v.ToBool();
        return;
    }
    if (prop == "visible") {
        bool nv = v.ToBool();
        if (w->visible != nv) {
            w->visible = nv;
            // visible flips ⇒ widget participates in flex layout differently
            // (invisible siblings are skipped in DoLayout). Without a relayout
            // the freshly-shown widget keeps the zero rect it got while
            // hidden, so it stays invisible from the user's POV — matches the
            // reported `:visible="flag"` reactive bug.
            ui::RequestLayout();
        }
        return;
    }
    if (prop == "opacity")  { SetOpacityMaybeAnimated(w, static_cast<float>(v.ToNumber())); return; }
    if (prop == "bg-color" || prop == "bgColor" || prop == "background" || prop == "background-color") {
        ui::css::Color c;
        if (ui::css::ParseColor(v.ToString(), c)) {
            SetBgMaybeAnimated(w, D2D1_COLOR_F{c.r, c.g, c.b, c.a});
        }
        return;
    }
    if (prop == "color") {
        ui::css::Color c;
        if (ui::css::ParseColor(v.ToString(), c)) {
            D2D1_COLOR_F d2d{c.r, c.g, c.b, c.a};
            if (auto* lbl = dynamic_cast<LabelWidget*>(w)) lbl->TextColor(d2d);
            else if (auto* btn = dynamic_cast<ButtonWidget*>(w)) btn->SetTextColor(d2d);
        }
        return;
    }
    if (prop == "enabled") {
        bool nv = v.ToBool();
        if (w->enabled != nv) {
            w->enabled = nv;
            // Re-cascade so any `:disabled` selector flips its match. Without
            // this the widget's color/cursor stay stuck at the previous state
            // until some unrelated event triggers a recomputeStyle.
            std::function<void(Widget*)> refresh = [&](Widget* x) {
                if (x->recomputeStyle) x->recomputeStyle(x->lastStateBits);
                for (auto& c : x->Children()) refresh(c.get());
            };
            refresh(w);
        }
        return;
    }
    if (prop == "width")      { SetWidthMaybeAnimated(w, static_cast<float>(v.ToNumber())); return; }
    if (prop == "height")     { SetHeightMaybeAnimated(w, static_cast<float>(v.ToNumber())); return; }
    if (prop == "left" || prop == "top" || prop == "right" || prop == "bottom") {
        if (ApplyDynamicAbsSideBinding(w, prop, v)) return;
    }
    if (prop == "min-width" || prop == "minWidth") {
        w->minW = static_cast<float>(v.ToNumber()); ui::RequestLayout(); return;
    }
    if (prop == "max-width" || prop == "maxWidth") {
        w->maxW = static_cast<float>(v.ToNumber()); ui::RequestLayout(); return;
    }
    if (prop == "min-height" || prop == "minHeight") {
        w->minH = static_cast<float>(v.ToNumber()); ui::RequestLayout(); return;
    }
    if (prop == "max-height" || prop == "maxHeight") {
        w->maxH = static_cast<float>(v.ToNumber()); ui::RequestLayout(); return;
    }
    if (prop == "src") {
        if (auto* img = dynamic_cast<ImageWidget*>(w)) {
            img->SetSrc(v.ToString());
            ui::RequestLayout();
        }
        return;
    }
    if (prop == "icon") {
        if (auto* btn = dynamic_cast<ButtonWidget*>(w)) {
            btn->SetIcon(ToWide(v.ToString()));
        }
        return;
    }
    if (prop == "wrap") {
        if (auto* lbl = dynamic_cast<LabelWidget*>(w)) {
            lbl->SetWrap(v.ToBool());
            ui::RequestLayout();
        }
        return;
    }

    if (prop == "selected" || prop == "checked" || prop == "on") {
        bool b = v.ToBool();
        // L45 mount-phase transition gate: widget 未 painted 时所有 binding push
        // 走 Immediate (snap) 不动画, 让 page 在最终状态 mount; painted 之后才
        // 走带动画 setter (用户运行时切换状态走 transition). 之前用 boundOnce_
        // 判断"第一次 push", 但 WatchEffect 注册时立即 fire 一次 (用 .uix data()
        // 默认值) 就消耗了 snap 通道, caller 在 prepare 之前 set 真实值反而触发
        // 动画 — 现按 widget.PaintedOnce() 判断, 跟"用户实际看到没"对齐.
        const bool painted = w->PaintedOnce();
        if (auto* rb = dynamic_cast<RadioButtonWidget*>(w)) {
            if (rb->Selected() == b) {
                traceBindingNoop();
                shouldInvalidate = false;
                return;
            }
            painted ? rb->SetSelected(b) : rb->SetSelectedImmediate(b);
        } else if (auto* cb = dynamic_cast<CheckBoxWidget*>(w)) {
            if (cb->Checked() == b) {
                traceBindingNoop();
                shouldInvalidate = false;
                return;
            }
            painted ? cb->SetChecked(b)  : cb->SetCheckedImmediate(b);
        } else if (auto* tg = dynamic_cast<ToggleWidget*>(w)) {
            if (tg->On() == b) {
                traceBindingNoop();
                shouldInvalidate = false;
                return;
            }
            painted ? tg->SetOn(b)       : tg->SetOnImmediate(b);
        } else if (auto* nav = dynamic_cast<NavItemWidget*>(w)) {
            if (nav->IsSelected() == b) {
                traceBindingNoop();
                shouldInvalidate = false;
                return;
            }
            nav->SetSelected(b);
        }
        return;
    }
    if (prop == "open") {
        if (auto* split = dynamic_cast<SplitViewWidget*>(w)) {
            const bool open = v.ToBool();
            if (split->IsPaneOpen() == open) {
                traceBindingNoop();
                shouldInvalidate = false;
                return;
            }
            w->PaintedOnce() ? split->SetPaneOpen(open) : split->SetPaneOpenImmediate(open);
        }
        return;
    }
    if (prop == "active" || prop == "activeIndex") {
        if (auto* stack = dynamic_cast<StackWidget*>(w)) {
            stack->SetActiveIndex(static_cast<int>(v.ToNumber()));
            ui::RequestLayout();
        }
        return;
    }
    if (prop == "value") {
        const bool painted = w->PaintedOnce();
        if (auto* ti = dynamic_cast<TextInputWidget*>(w)) {
            ti->SetText(ToWide(v.ToString()));   // no animation, painted check irrelevant
        } else if (auto* ta = dynamic_cast<TextAreaWidget*>(w)) {
            ta->SetText(ToWide(v.ToString()));
        } else if (auto* ci = dynamic_cast<ChipsInputWidget*>(w)) {
            std::wstring nv = ToWide(v.ToString());
            if (ci->Value() == nv) {  // avoid clobbering caret on echo
                traceBindingNoop();
                shouldInvalidate = false;
                return;
            }
            ci->SetValue(nv);
        } else if (auto* slider = dynamic_cast<SliderWidget*>(w)) {
            // SliderWidget::SetValue 不带 value 动画 (只有 thumb scale hover
            // animation, 跟 value 无关), 直接 set 即可, painted 检查无意义.
            slider->SetValue(static_cast<float>(v.ToNumber()));
        } else if (auto* pb = dynamic_cast<ProgressBarWidget*>(w)) {
            float fv = static_cast<float>(v.ToNumber());
            painted ? pb->SetValue(fv) : pb->SetValueImmediate(fv);
        } else if (auto* cb = dynamic_cast<CheckBoxWidget*>(w)) {
            bool b = v.ToBool();
            if (cb->Checked() == b) {
                traceBindingNoop();
                shouldInvalidate = false;
                return;
            }
            painted ? cb->SetChecked(b) : cb->SetCheckedImmediate(b);
        } else if (auto* tg = dynamic_cast<ToggleWidget*>(w)) {
            bool b = v.ToBool();
            if (tg->On() == b) {
                traceBindingNoop();
                shouldInvalidate = false;
                return;
            }
            painted ? tg->SetOn(b) : tg->SetOnImmediate(b);
        } else if (auto* rb = dynamic_cast<RadioButtonWidget*>(w)) {
            bool b = v.ToBool();
            if (rb->Selected() == b) {
                traceBindingNoop();
                shouldInvalidate = false;
                return;
            }
            painted ? rb->SetSelected(b) : rb->SetSelectedImmediate(b);
        } else if (auto* nb = dynamic_cast<NumberBoxWidget*>(w)) {
            nb->SetValue(static_cast<float>(v.ToNumber()));  // NumberBox 无动画字段, 直接 set
        } else if (auto* combo = dynamic_cast<ComboBoxWidget*>(w)) {
            if (v.IsNumber()) combo->SetSelectedIndex(static_cast<int>(v.ToNumber()));
        }
        return;
    }
    (void)v;
}

// ---- Menus -----------------------------------------------------------------

void PageState::WireMenus() {
    /* Build 73 (L17): 顶层菜单同走 WireSubtreeMenus 路径 — 反应式 hook +
     * 完整 icon (SVG / bitmap) 支持都集中在那里, WireMenus 不再单独建静态
     * items. 老 demo 用静态 <menuitem text="..." icon="..."> 也 OK,
     * WireSubtreeMenus 内部 PopulateMenuItem 对 bound 字段为空时走静态
     * fallback (向后兼容). */
    menus_.clear();
    menuById_.clear();
    triggers_.clear();
    menuItemHandlers_.clear();
    compiledToMenu_.clear();
    if (page_.menus.empty()) return;
    WireSubtreeMenus(page_.menus, menus_);
}

void PageState::WireSubtreeMenus(const std::vector<CompiledMenu>& subMenus,
                                  std::vector<std::shared_ptr<ContextMenu>>& outMenus) {
    if (subMenus.empty()) return;
    /* Build 73 (L17): toWide / item 构造细节都搬到 PopulateMenuItem
     * 里, WireSubtreeMenus 只负责 shells + trigger + hook 注册. */
    auto resolveTrigger = [this](const std::string& sel) -> Widget* {
        if (sel.size() < 2 || sel[0] != '#') return nullptr;
        if (!page_.root) return nullptr;
        return page_.root->FindById(sel.substr(1));
    };

    ui::UiWindowImpl* winImpl = nullptr;
    if (winHandle_) {
        auto* w = ui::GetContext().GetWindow(winHandle_);
        if (w) winImpl = w;
    }

    /* Build 73 (L17): 反应式菜单 — 先建 ContextMenu shells (不 populate items),
     * 注册 beforeShowHook -> PopulateMenu. 每次 Show 入口走 hook, Clear + 重
     * eval bound exprs + 重 add items. submenus 也建 shell, 递归注册 hook.
     * compiledToMenu_ 映射 PopulateMenuItem 找 submenu ContextMenu* 用. */
    std::function<std::shared_ptr<ContextMenu>(const CompiledMenu&)> buildOne;
    buildOne = [&](const CompiledMenu& cm) -> std::shared_ptr<ContextMenu> {
        auto menu = std::make_shared<ContextMenu>();
        if (cm.hasBgColor) menu->SetBgColor(cm.bgColor);
        if (cm.hasFrostedMaterial) menu->SetFrostedMaterial(cm.frostedMaterial);
        if (cm.hasBackdropBlur) menu->SetBackdropBlur(cm.backdropBlur);
        compiledToMenu_[&cm] = menu.get();
        /* 递归预建所有 submenu shells, 注册它们自己的 hook. submenu 本身的
         * trigger 是 parent menu item, 不走 widget trigger; 但仍走同款 Show
         * 路径所以 hook 会触发. */
        for (const auto& mi : cm.items) {
            if (mi.submenu) {
                auto sub = buildOne(*mi.submenu);
                outMenus.push_back(sub);
            }
        }
        /* hook capture: this + 持久 cm 指针 (cm 在 page_.menus 里, 跟 page 生命同寿)
         * + 裸 menu 指针 (shared_ptr 在 menus_ / outMenus 里持有). */
        const CompiledMenu* cmPtr = &cm;
        ContextMenu*        rawMenu = menu.get();
        PageState*          self = this;
        menu->SetBeforeShowHook([self, cmPtr, rawMenu] {
            self->PopulateMenu(rawMenu, *cmPtr, {});
        });
        /* 初始 populate 一次, 让没经过 Show 路径的直接调用 (Bounds/MenuWidth)
         * 也能拿到合理数据. 之后每次 Show 都会重 populate, 这里只是兜底. */
        PopulateMenu(rawMenu, cm, {});
        return menu;
    };

    for (const auto& cm : subMenus) {
        auto menu = buildOne(cm);
        outMenus.push_back(menu);
        if (!cm.id.empty()) menuById_[cm.id] = menu.get();

        Widget* trigger = resolveTrigger(cm.triggerSelector);
        if (!trigger) continue;
        bool rclick = (cm.triggerEvent == "rclick");
        triggers_.push_back({trigger, menu.get(), rclick});

        if (winImpl && !rclick) {
            ContextMenu* rawMenu = menu.get();
            Widget* trig = trigger;
            auto* wp = winImpl;
            trigger->onClick = [wp, trig, rawMenu]() {
                wp->ShowMenu(rawMenu->shared_from_this(),
                             trig->rect.left, trig->rect.bottom);
            };
        }
    }

    if (winImpl) {
        bool hasRclick = false;
        for (const auto& t : triggers_) if (t.rclick) { hasRclick = true; break; }
        if (hasRclick) {
            std::vector<TriggerSpec> rclickList;
            for (const auto& t : triggers_) if (t.rclick) rclickList.push_back(t);
            auto* wp = winImpl;
            auto prev = wp->onRightClick;
            /* Build 107 (L28): rclick dispatch first-match → deepest-match.
             * 之前按 rclickList 声明顺序 (.uix `<menu>` 出现顺序) 取第一个
             * Contains(x, y) 的 trigger, 但 child widget 上 rclick 也满足
             * ancestor trigger 的 Contains, 父先声明的话子 widget 自己的
             * trigger 永远被抢. 改成遍历找 Contains(x, y) 里 widget tree
             * 深度最深的 trigger — 子 widget trigger 自然优先于祖先
             * trigger 匹配, 跟 DOM event capturing 直觉一致. depth 用
             * Widget::Parent() 链算, O(N×depth), N 通常 <10 无性能问题. */
            wp->onRightClick = [wp, rclickList, prev](float x, float y) {
                ContextMenu* bestMenu = nullptr;
                int bestDepth = -1;
                for (const auto& t : rclickList) {
                    if (!t.triggerElement || !t.menu) continue;
                    if (!t.triggerElement->Contains(x, y)) continue;
                    /* L90: 跳过不可见 trigger (自身或祖先 visible=false) — 隐藏
                     * widget (如 borderless 隐藏的 minimap) rect 仍 Contains,
                     * 不跳会抢右键菜单, 小窗时盖住大半画布. */
                    {
                        bool t_vis = true;
                        for (Widget* p = t.triggerElement; p; p = p->Parent())
                            if (!p->visible) { t_vis = false; break; }
                        if (!t_vis) continue;
                    }
                    int depth = 0;
                    for (Widget* p = t.triggerElement->Parent();
                         p; p = p->Parent()) {
                        ++depth;
                    }
                    if (depth > bestDepth) {
                        bestDepth = depth;
                        bestMenu  = t.menu;
                    }
                }
                if (bestMenu) {
                    wp->ShowMenu(bestMenu->shared_from_this(), x, y);
                    return;
                }
                if (prev) prev(x, y);
            };
        }
    }
}

// ============================================================================
// Build 73 (L17): Reactive menu rebuild on Show
// ============================================================================

JSValue PageState::EvalBoundExpr(
        const std::string& rawExpr,
        const std::vector<std::pair<std::string, JSValue>>& locals) {
    if (rawExpr.empty() || !jsRt_ || JS_IsUndefined(jsState_)) return JS_UNDEFINED;
    JSContext* ctx = jsRt_->ctx();

    std::set<std::string> localSet;
    std::vector<std::string> paramNames;
    std::vector<JSValueConst> args;
    for (const auto& kv : locals) {
        localSet.insert(kv.first);
        paramNames.push_back(kv.first);
        args.push_back(kv.second);
    }
    std::string rewritten = ui::uix::RewriteTemplateExpr(rawExpr, localSet);
    JSValue fn = locals.empty()
        ? jsRt_->CompileBindingClosure(rewritten, "<menu>")
        : jsRt_->CompileLoopBindingClosure(rewritten, paramNames, "<menu>");
    if (JS_IsException(fn)) { JS_FreeValue(ctx, fn); return JS_UNDEFINED; }
    JSValue r = JS_Call(ctx, fn, jsState_, (int)args.size(),
                        args.empty() ? nullptr : args.data());
    JS_FreeValue(ctx, fn);
    return r;  // caller frees
}

bool PageState::EvalBool(
        const std::string& expr,
        const std::vector<std::pair<std::string, JSValue>>& locals,
        bool defaultVal) {
    if (expr.empty() || !jsRt_) return defaultVal;
    JSValue v = EvalBoundExpr(expr, locals);
    if (JS_IsUndefined(v) || JS_IsException(v)) {
        if (JS_IsException(v)) JS_FreeValue(jsRt_->ctx(), v);
        return defaultVal;
    }
    int b = JS_ToBool(jsRt_->ctx(), v);
    JS_FreeValue(jsRt_->ctx(), v);
    return b > 0;
}

std::string PageState::EvalString(
        const std::string& expr,
        const std::vector<std::pair<std::string, JSValue>>& locals) {
    if (expr.empty() || !jsRt_) return {};
    JSValue v = EvalBoundExpr(expr, locals);
    if (JS_IsUndefined(v) || JS_IsException(v)) {
        if (JS_IsException(v)) JS_FreeValue(jsRt_->ctx(), v);
        return {};
    }
    const char* c = JS_ToCString(jsRt_->ctx(), v);
    std::string r = c ? c : "";
    if (c) JS_FreeCString(jsRt_->ctx(), c);
    JS_FreeValue(jsRt_->ctx(), v);
    return r;
}

bool PageState::EvalMenuBackdropBlur(
        const std::string& expr,
        const std::vector<std::pair<std::string, JSValue>>& locals,
        float& out) {
    if (expr.empty() || !jsRt_) return false;
    JSValue v = EvalBoundExpr(expr, locals);
    if (JS_IsUndefined(v) || JS_IsException(v) || JS_IsNull(v)) {
        if (JS_IsException(v)) JS_FreeValue(jsRt_->ctx(), v);
        return false;
    }

    JSContext* ctx = jsRt_->ctx();
    bool ok = false;
    if (JS_IsNumber(v) || JS_IsBool(v)) {
        double n = 0.0;
        if (JS_ToFloat64(ctx, &n, v) == 0 && std::isfinite(n)) {
            out = static_cast<float>(n);
            ok = true;
        }
    } else if (JS_IsString(v)) {
        const char* c = JS_ToCString(ctx, v);
        if (c) {
            ok = ParseBackdropBlurText(c, out);
            JS_FreeCString(ctx, c);
        }
    }
    JS_FreeValue(ctx, v);
    return ok;
}

void PageState::PopulateMenu(
        ContextMenu* menu, const CompiledMenu& cm,
        const std::vector<std::pair<std::string, JSValue>>& locals) {
    if (!menu) return;
    menu->Clear();

    /* menu-level v-if / v-show — false 时整个菜单为空, Show 出来也是没东西.
     * 空字符串 = 没绑 = 默认 true (show). */
    if (!cm.vIfExpr.empty() && !EvalBool(cm.vIfExpr, locals, true)) return;

    if (!cm.boundFrostedMaterialExpr.empty()) {
        menu->SetFrostedMaterial(
            EvalBool(cm.boundFrostedMaterialExpr, locals,
                     cm.hasFrostedMaterial ? cm.frostedMaterial : false));
    }

    if (!cm.boundBgColorExpr.empty()) {
        ui::css::Color c;
        if (ui::css::ParseColor(EvalString(cm.boundBgColorExpr, locals), c)) {
            menu->SetBgColor(D2D1_COLOR_F{c.r, c.g, c.b, c.a});
        }
    }

    if (!cm.boundBackdropBlurExpr.empty()) {
        float blur = cm.hasBackdropBlur ? cm.backdropBlur : -1.0f;
        if (EvalMenuBackdropBlur(cm.boundBackdropBlurExpr, locals, blur)) {
            menu->SetBackdropBlur(blur);
        }
    }

    /* Build 77+/80+/81+: 扫所有 items (含 v-if=false 隐藏的) 算 shortcut /
     * submenu / content width 的 max 喂给 menu 当 MenuWidth floor — 两态
     * 宽度严格一致. v-for items 用静态 shortcut, content 通过实际 build
     * wrapper widget 走完整 CSS 级联拿 SizeHint (跟 PopulateMenuItem 真正
     * add 进 menu 时用同一套, 估算值跟运行时严格相等). */
    {
        std::function<void(const CompiledMenu&, float&, bool&, float&)> scan;
        scan = [&scan, this](const CompiledMenu& m, float& maxW, bool& hasSub, float& maxC) {
            for (const auto& mi : m.items) {
                if (mi.separator) continue;
                float w = mi.shortcut.length() * 6.5f;
                if (w > maxW) maxW = w;
                if (mi.submenu) {
                    hasSub = true;
                    if (m.shareWidthWithSubmenus) {
                        scan(*mi.submenu, maxW, hasSub, maxC);
                    }
                }
                /* 真实 measurement, build 83 (正确 CSS spec): build wrapper widget tree,
                 * SizeHint() 走 fixedW (CSS `width: NNN`) 或 children sum (fit-content
                 * fallback). max-width 是严格上界 (cap), min-width 是严格下界. 跟
                 * 浏览器 inline-block / block-with-explicit-width 行为一致.
                 *
                 * 调用方控制 menu content col 宽度的方式:
                 *   .menuitem-row { width: NNN }     → SizeHint = NNN, 确定值
                 *   .menuitem-row { max-width: NNN } → 上界, content < NNN 时菜单较窄
                 *   两者都设 → width 赢 (CSS 一致)
                 *
                 * build 82 的 "if maxW > 0 use maxW" 是折中, 现在撤回. */
                if (mi.contentRoot && page_.ownedStylesheet) {
                    auto sub = ui::page::CompileIterationTemplate(
                        *mi.contentRoot, *page_.ownedStylesheet, page_.cssVars);
                    if (sub.root) {
                        float w2 = sub.root->SizeHint().width;
                        if (sub.root->maxW > 0 && w2 > sub.root->maxW) w2 = sub.root->maxW;
                        if (sub.root->minW > 0 && w2 < sub.root->minW) w2 = sub.root->minW;
                        if (w2 > maxC) maxC = w2;
                    }
                }
            }
        };
        float maxShortcutAll = 0;
        bool  anySubmenu     = false;
        float maxContentAll  = 0;
        scan(cm, maxShortcutAll, anySubmenu, maxContentAll);
        menu->SetReservedShortcutWidth(maxShortcutAll);
        menu->SetReservedHasSubmenu(anySubmenu);
        menu->SetReservedContentWidth(maxContentAll);
    }

    for (const auto& mi : cm.items) {
        if (!mi.vForArrayExpr.empty()) {
            /* v-for: eval array expr → 遍历, 每次塞 iter / index 变量进 locals
             * 后 PopulateMenuItem. 空数组 / 非数组 → skip 这条 declaration. */
            JSValue arr = EvalBoundExpr(mi.vForArrayExpr, locals);
            if (jsRt_ && JS_IsArray(arr)) {
                JSContext* ctx = jsRt_->ctx();
                int64_t len = 0;
                JSValue lenV = JS_GetPropertyStr(ctx, arr, "length");
                JS_ToInt64(ctx, &len, lenV);
                JS_FreeValue(ctx, lenV);
                for (int64_t i = 0; i < len; ++i) {
                    JSValue elem = JS_GetPropertyUint32(ctx, arr, (uint32_t)i);
                    auto extended = locals;
                    extended.emplace_back(mi.vForIterVar, elem);
                    JSValue idxV = JS_UNDEFINED;
                    if (!mi.vForIndexVar.empty()) {
                        idxV = JS_NewInt32(ctx, (int32_t)i);
                        extended.emplace_back(mi.vForIndexVar, idxV);
                    }
                    PopulateMenuItem(menu, mi, extended);
                    if (!JS_IsUndefined(idxV)) JS_FreeValue(ctx, idxV);
                    JS_FreeValue(ctx, elem);
                }
            }
            if (jsRt_ && !JS_IsUndefined(arr) && !JS_IsException(arr)) {
                JS_FreeValue(jsRt_->ctx(), arr);
            }
        } else {
            PopulateMenuItem(menu, mi, locals);
        }
    }

    /* Build 85+: 跨菜单树传播 — 算完 parent 的最终 MenuWidth, 回写到所有
     * submenu 当 minPropagatedWidth, 让 submenu 至少跟 parent 同宽. 整族
     * 菜单视觉一致 (不再 submenu 缩到自身文字宽度).
     * build 249: share-width=false 让调用方关闭这条传播, 使子菜单按自身
     * 内容列宽度收缩。 */
    float parentWidth = cm.shareWidthWithSubmenus ? menu->MenuWidth() : 0.0f;
    std::function<void(const CompiledMenu&)> propagate;
    propagate = [&propagate, this, parentWidth](const CompiledMenu& m) {
        for (const auto& mi : m.items) {
            if (mi.submenu) {
                auto it = compiledToMenu_.find(mi.submenu.get());
                if (it != compiledToMenu_.end() && it->second) {
                    it->second->SetMinPropagatedWidth(parentWidth);
                }
                propagate(*mi.submenu);
            }
        }
    };
    propagate(cm);
}

void PageState::PopulateMenuItem(
        ContextMenu* menu, const CompiledMenuItem& mi,
        const std::vector<std::pair<std::string, JSValue>>& locals) {
    /* item-level v-if / v-show: 求 false → skip, 也不占位 (separator 同款) */
    if (!mi.vIfExpr.empty() && !EvalBool(mi.vIfExpr, locals, true)) return;

    auto toWide = [](const std::string& s) -> std::wstring {
        if (s.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        if (len <= 0) return {};
        std::wstring w(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
        return w;
    };

    if (mi.separator) { menu->AddSeparator(); return; }

    /* shortcut: bound 优先, 静态后备. */
    std::string scStr = !mi.boundShortcutExpr.empty()
        ? EvalString(mi.boundShortcutExpr, locals) : mi.shortcut;
    std::wstring shortcut = scStr.empty() ? L"" : toWide(scStr);

    /* BREAKING (build 75): customContent widget tree — 通过 CompileIterationTemplate
     * 把 mi.contentRoot AST (一个 <div class="menuitem-row"> 包了用户写的 svg /
     * label / 任意 widget) 实例化成一棵新 widget tree, 装到 MenuItem.customContent.
     * 反应式 binding (e.g. <svg :style="...">) 通过同款 watchEffect 接到 jsState_,
     * 跟 widget 主路径完全一致. 没有 contentRoot (理论上不该发生, compiler 总给一个
     * wrapper) → 空 widget, ContextMenu 仍能渲染 (只显示 shortcut + arrow). */
    WidgetPtr content;
    if (mi.contentRoot && page_.ownedStylesheet) {
        auto sub = ui::page::CompileIterationTemplate(*mi.contentRoot,
                                                      *page_.ownedStylesheet,
                                                      page_.cssVars);
        if (sub.root) {
            content = sub.root;
            /* Wire bindings + events (locals-scope 版本, 跟 v-for 同款).
             * locals 来自当前 PopulateMenu 调用栈 (v-for menuitem 的 iter var). */
            std::set<std::string> localSet;
            std::vector<std::string> paramNames;
            std::vector<JSValueConst> argVec;
            for (const auto& kv : locals) {
                localSet.insert(kv.first);
                paramNames.push_back(kv.first);
                argVec.push_back(kv.second);
            }
            JSContext* ctx = jsRt_ ? jsRt_->ctx() : nullptr;
            for (auto& b : sub.bindings) {
                if (!ctx) break;
                std::string rew = ui::uix::RewriteTemplateExpr(b.sourceJs, localSet);
                JSValue fn = paramNames.empty()
                    ? jsRt_->CompileBindingClosure(rew, "<menuitem>")
                    : jsRt_->CompileLoopBindingClosure(rew, paramNames, "<menuitem>");
                if (JS_IsException(fn)) { JS_FreeValue(ctx, fn); continue; }
                /* 一次性 eval: PopulateMenu 每次 Show 重建 widget tree, 所以
                 * binding 跟着重 eval 一次足够 — 没必要挂 long-lived WatchEffect
                 * (那会导致老 effect 持 dangling Widget* 当 deps 变化时崩溃,
                 * 且 ContextMenu Show 期间 menu 关闭后 widget tree 也马上销毁,
                 * 反应不到也无意义). */
                JSValue r = JS_Call(ctx, fn, jsState_, (int)argVec.size(),
                                    argVec.empty() ? nullptr
                                                   : const_cast<JSValueConst*>(argVec.data()));
                JS_FreeValue(ctx, fn);
                if (JS_IsException(r)) {
                    JSValue exc = JS_GetException(ctx); JS_FreeValue(ctx, exc);
                    JS_FreeValue(ctx, r); continue;
                }
                ui::expr::Value ev = ui::uix::JSValueToExprValue(ctx, r);
                ApplyBindingToWidget(b.target, b.property, ev);
                JS_FreeValue(ctx, r);
            }
            /* events: @click 等. 跟 v-for 同款 handler closure wiring. */
            for (auto& ev : sub.events) {
                if (!ctx) break;
                WireQuickJSEvent(ev.target, ev.event, ev.sourceJs);
            }
            /* 嵌套 v-if / v-for in menuitem content: 简化版 — 当前迭代不
             * 支持 menuitem 内部 v-for / 嵌套 v-if reactive remount; 用户
             * 在 menuitem body 里用 v-if 走 widget 标准 conditional 路径
             * (sub.conditionals / sub.loops 也得 wire). 后续如要支持, 跟
             * v-for iteration 的 BuildJsLoopRuntime 同款 wire 一遍. */
        }
    }

    /* enabled: bound 优先, 默认 true. ContextMenu 端的 disabled state 走
     * AddItemContent 之后 SetEnabled(id, false). */
    bool enabled = mi.boundEnabledExpr.empty()
        ? true : EvalBool(mi.boundEnabledExpr, locals, true);

    if (mi.submenu) {
        /* Submenu entry: 用 compiler 合成的 <label>title</label> 当 entry widget. */
        auto it = compiledToMenu_.find(mi.submenu.get());
        if (it != compiledToMenu_.end() && it->second) {
            menu->AddSubmenu(content, it->second->shared_from_this());
        }
    } else {
        menu->AddItemContent(mi.itemId, shortcut, content);
        menu->SetLastItemMeta(mi.strId, mi.attrs);  // 补字符串 id + 全部属性 (点击回调用)
    }
    if (!enabled) menu->SetEnabled(mi.itemId, false);
    if (!mi.onClick.empty()) menuItemHandlers_[mi.strId] = mi.onClick;
}

void PageState::AttachWindow(uint64_t winHandle) {
    winHandle_ = winHandle;
    auto& ctx = ui::GetContext();
    auto win = ctx.GetWindow(winHandle);
    if (!win) return;
    auto* winImpl = win;

    for (const auto& t : triggers_) {
        if (t.rclick || !t.triggerElement || !t.menu) continue;
        Widget* trig = t.triggerElement;
        ContextMenu* rawMenu = t.menu;
        auto* wp = winImpl;
        t.triggerElement->onClick = [wp, trig, rawMenu]() {
            wp->ShowMenu(rawMenu->shared_from_this(), trig->rect.left, trig->rect.bottom);
        };
    }

    bool hasRclick = false;
    for (const auto& t : triggers_) if (t.rclick) { hasRclick = true; break; }
    if (hasRclick) {
        auto prev = win->onRightClick;
        std::vector<TriggerSpec> rclickList;
        for (const auto& t : triggers_) if (t.rclick) rclickList.push_back(t);
        auto* wp = winImpl;
        /* Build 107 (L28): deepest-match — 跟 WireSubtreeMenus 中 line 628 同款.
         * AttachWindow 后于 WireSubtreeMenus 调用, 这里的 lambda 才是最终生效
         * 的那个. 之前 WireSubtreeMenus 改成 deepest-match 但 AttachWindow
         * 没改, 用户右键子 widget 时仍走 first-match 命中父 trigger. 两处
         * 保持一致避免类似回归. */
        win->onRightClick = [wp, rclickList, prev](float x, float y) {
            ContextMenu* bestMenu = nullptr;
            int bestDepth = -1;
            for (const auto& t : rclickList) {
                if (!t.triggerElement || !t.menu) continue;
                if (!t.triggerElement->Contains(x, y)) continue;
                /* L90: 跳过不可见 trigger (自身或祖先 visible=false) — 隐藏
                 * widget (如 borderless 隐藏的 minimap) rect 仍 Contains,
                 * 不跳会抢右键菜单, 小窗时盖住大半画布. */
                {
                    bool t_vis = true;
                    for (Widget* p = t.triggerElement; p; p = p->Parent())
                        if (!p->visible) { t_vis = false; break; }
                    if (!t_vis) continue;
                }
                int depth = 0;
                for (Widget* p = t.triggerElement->Parent();
                     p; p = p->Parent()) {
                    ++depth;
                }
                if (depth > bestDepth) {
                    bestDepth = depth;
                    bestMenu  = t.menu;
                }
            }
            if (bestMenu) {
                wp->ShowMenu(bestMenu->shared_from_this(), x, y);
                return;
            }
            if (prev) prev(x, y);
        };
    }

    // <menuitem onclick="methodName"> — dispatch to the JS method bound on
    // jsState_. Methods{} from `export default` were merged onto dataObj
    // before MakeReactive, so jsState_.<name> resolves through the proxy
    // to the JSFunction we then call with this=jsState_.
    auto prev = win->onMenuItemClick;
    PageState* self = this;
    win->onMenuItemClick = [self, prev](const MenuClickInfo* info) {
        auto it = info ? self->menuItemHandlers_.find(info->id)
                       : self->menuItemHandlers_.end();
        if (it != self->menuItemHandlers_.end() && self->jsRt_ &&
            !JS_IsUndefined(self->jsState_)) {
            JSContext* ctx = self->jsRt_->ctx();
            JSValue fn = JS_GetPropertyStr(ctx, self->jsState_, it->second.c_str());
            if (JS_IsFunction(ctx, fn)) {
                JSValue r = JS_Call(ctx, fn, self->jsState_, 0, nullptr);
                if (JS_IsException(r)) {
                    JSValue exc = JS_GetException(ctx); JS_FreeValue(ctx, exc);
                }
                JS_FreeValue(ctx, r);
                JS_FreeValue(ctx, fn);
                return;
            }
            JS_FreeValue(ctx, fn);
        }
        if (prev) prev(info);
    };

    win->onPageResize = [self](int width, int height) {
        if (self) self->DispatchQuickJSResize(width, height);
    };
}

void PageState::DispatchQuickJSResize(int width, int height) {
    if (!jsRt_ || JS_IsUndefined(jsState_) || JS_IsUndefined(jsOptions_)) return;
    EnsurePageLayout(this);

    JSContext* ctx = jsRt_->ctx();
    JSValue fn = JS_GetPropertyStr(ctx, jsOptions_, "onResize");
    if (!JS_IsFunction(ctx, fn)) {
        JS_FreeValue(ctx, fn);
        fn = JS_GetPropertyStr(ctx, jsState_, "onResize");
    }
    if (!JS_IsFunction(ctx, fn)) {
        JS_FreeValue(ctx, fn);
        return;
    }

    JSValue args[2] = { JS_NewInt32(ctx, width), JS_NewInt32(ctx, height) };
    JSValue r = JS_Call(ctx, fn, jsState_, 2, args);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    if (JS_IsException(r)) {
        JSValue exc = JS_GetException(ctx);
        const char* msg = JS_ToCString(ctx, exc);
        errors_.push_back(std::string("onResize threw: ") + (msg ? msg : "(no message)"));
        if (msg) JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, exc);
    }
    JS_FreeValue(ctx, r);
    JS_FreeValue(ctx, fn);
}

ContextMenu* PageState::FindMenuById(const std::string& id) const {
    auto it = menuById_.find(id);
    return it == menuById_.end() ? nullptr : it->second;
}

void PageState::InvalidateMenuHandles() {
    auto& ctx = ui::GetContext();
    for (const auto& kv : menuHandleCache_) {
        ctx.RemoveMenu(kv.second);
    }
    menuHandleCache_.clear();
}

uint64_t PageState::GetOrRegisterMenuHandle(const std::string& name) {
    auto it = menuHandleCache_.find(name);
    if (it != menuHandleCache_.end()) return it->second;
    auto* menu = FindMenuById(name);
    if (!menu) return 0;
    auto sp = menu->shared_from_this();
    uint64_t h = ui::GetContext().RegisterMenu(sp);
    menuHandleCache_[name] = h;
    return h;
}

// ---- Global feature flag (historical; retained for env-var bisection) ----

namespace {
enum class FlagState { Unread, Auto, Off, On };
FlagState g_quickJsFlag = FlagState::Unread;

void EnsureFlagInit() {
    if (g_quickJsFlag != FlagState::Unread) return;
    const char* env = std::getenv("UI_PAGE_QUICKJS");
    if (env && env[0]) {
        g_quickJsFlag = (env[0] == '0' && env[1] == 0)
                        ? FlagState::Off : FlagState::On;
    } else {
        g_quickJsFlag = FlagState::Auto;
    }
}
}  // namespace

void PageState::SetGlobalUseQuickJS(bool v) {
    g_quickJsFlag = v ? FlagState::On : FlagState::Off;
}
bool PageState::GetGlobalUseQuickJS() {
    EnsureFlagInit();
    return g_quickJsFlag == FlagState::On;
}

void PageState::Attach(CompiledPage page) {
    EnsureFlagInit();
    // The flag exists for env-var diff-bisecting; setting UI_PAGE_QUICKJS=0
    // would historically force the legacy AST path. That path no longer
    // exists, so we just route everything through QuickJS.
    AttachQuickJS(std::move(page));
}

// ---- QuickJS path ---------------------------------------------------------

struct PageState::JsCondRuntime {
    Widget*               parent       = nullptr;
    size_t                insertIdx    = 0;
    const ui::uix::Node*  templateNode = nullptr;
    const ui::css::Stylesheet* sheet   = nullptr;
    JSValue               fn           = JS_UNDEFINED;
    WidgetPtr             mounted;
    std::vector<uint64_t> innerEffects;
    std::vector<JSValue>  innerFns;
    // v-for runtimes inside this conditional. spec stored alongside so
    // JsLoopRuntime::spec (raw ptr) stays valid for the runtime's lifetime.
    std::vector<CompiledLoop>                  innerLoopSpecs;
    std::vector<std::unique_ptr<JsLoopRuntime>> innerLoops;
};

// Per-iteration binding bookkeeping. We need (fn, target, property) together
// so keyed reuse can dispose-and-rebind the effect with the same closure +
// dispatch info — without recompiling the closure.
struct IterBinding {
    JSValue     fn       = JS_UNDEFINED;
    Widget*     target   = nullptr;
    std::string property;
    uint64_t    effectId = 0;
};

// v-if nested inside a v-for iteration. Distinct from JsCondRuntime: the
// condition expression and any bindings inside the mounted subtree must be
// evaluated against the iteration's loop scope (item / idx), so closures
// here go through CompileLoopBindingClosure rather than the global one.
// Forward decl for self-nesting (v-if inside v-if inside v-for).
struct CondInIter;

struct CondInIter {
    Widget*               parent       = nullptr;
    size_t                insertIdx    = 0;
    const ui::uix::Node*  templateNode = nullptr;
    JSValue               fn           = JS_UNDEFINED;  // loop closure: fn(item[, idx]) → bool
    uint64_t              effectId     = 0;
    // build 287: 条件求值体本身。keyed diff 复用一行时, iteration 的 itemValue
    // 换成了新数组里的新对象, 旧 effect 的依赖集还挂在被丢弃的旧对象上, 于是
    // v-for 里的 v-if 再也不会重新求值 (表现: 行内容能更新, 但该出现/消失的
    // 分支纹丝不动)。存下求值体, 复用时 Dispose + 重新 WatchEffect 即可让依赖
    // 集在新对象上重建 —— WatchEffect 会立刻跑一次, 顺带完成分支切换。
    std::function<void()> evalFn;
    WidgetPtr             mounted;
    std::vector<JSValue>  innerFns;
    std::vector<uint64_t> innerEffects;
    /* build 289: 与 innerEffects 一一对应的求值体。理由跟上面的 evalFn 完全
     * 一样, 只是管的是 v-if **子树内部**的 binding: keyed diff 复用一行时,
     * 这些 effect 的依赖集同样还挂在被丢弃的旧 item 对象上, 于是子树里的
     * {{ item.xxx }} 再也不更新。build 287 只修了条件本身, 漏了这一半 ——
     * 表现是"该出现的分支出现了, 但里面的数字/文字是旧的"。 */
    std::vector<std::function<void()>> innerEvalFns;
    // v-if nested inside this v-if (still in the outer v-for's loop scope).
    // mount() builds them; unmount() tears them down.
    std::vector<std::unique_ptr<CondInIter>> innerConditionals;
    // v-for nested inside this v-if (so v-for > v-if > v-for). spec storage
    // owns the CompiledLoop so JsLoopRuntime::spec stays valid.
    std::vector<CompiledLoop>                            innerLoopSpecs;
    std::vector<std::unique_ptr<PageState::JsLoopRuntime>> innerLoops;
};

struct PageState::JsLoopIteration {
    std::string key;                  // identity for keyed diff
    JSValue   itemValue = JS_UNDEFINED;
    JSValue   idxValue  = JS_UNDEFINED;
    WidgetPtr mountedRoot;
    CompiledPage subPage;
    std::vector<IterBinding> bindings;
    std::vector<JSValue>     eventFns;
    // v-if nodes that lived directly under the v-for template node. Each
    // mounts/unmounts its own subtree based on a per-item evaluation.
    std::vector<std::unique_ptr<CondInIter>> conditionals;
};

struct PageState::JsLoopRuntime {
    const CompiledLoop* spec = nullptr;
    JSValue   listFn   = JS_UNDEFINED;
    JSValue   keyFn    = JS_UNDEFINED;     // optional — fallback is positional "#i"
    uint64_t  watcherId = 0;
    std::vector<std::unique_ptr<JsLoopIteration>> iterations;

    // ---- Nested-loop scope (v-for inside v-if inside v-for) ----------------
    // When this loop is built inside a CondInIter that itself lives inside a
    // v-for iteration, listFn / keyFn / per-iter binding closures are
    // compiled with the OUTER iteration's loopVar / indexVar prepended as
    // params. At call time we build the args array as
    //   [outerItem (, outerIdx), thisItem (, thisIdx), $event?]
    // matching the param order used at compile time.
    JsLoopIteration*         outerIter = nullptr;
    std::vector<std::string> outerParamNames;   // ordered, matches compile params
    std::set<std::string>    outerLocals;       // for the rewriter
    bool                     outerHasIdx = false;
};

void PageState::DetachQuickJS() {
    if (!jsRt_) return;
    // Fire unmount hooks before tearing anything down — registered onUnmount
    // callbacks (ui_page_on_widget_unmount) need a chance to release
    // external resources tied to the live widget instances. Without this
    // window-close / page-swap leaks any user-attached cleanup.
    if (page_.root) DispatchUnmountHooks(page_.root.get());

    for (uint64_t id : jsEffectIds_) jsRt_->DisposeEffect(id);
    jsEffectIds_.clear();
    JSContext* ctx = jsRt_->ctx();
    for (auto& cr : jsCondRuntimes_) {
        for (uint64_t id : cr->innerEffects) jsRt_->DisposeEffect(id);
        for (auto& f  : cr->innerFns)        JS_FreeValue(ctx, f);
        for (auto& lr : cr->innerLoops)      if (lr) TearDownJsLoopRuntime(*lr);
        cr->innerLoops.clear();
        cr->innerLoopSpecs.clear();
        if (!JS_IsUndefined(cr->fn))         JS_FreeValue(ctx, cr->fn);
        cr->innerEffects.clear();
        cr->innerFns.clear();
        cr->mounted.reset();
    }
    jsCondRuntimes_.clear();
    for (auto& lr : jsLoopRuntimes_) if (lr) TearDownJsLoopRuntime(*lr);
    jsLoopRuntimes_.clear();
    for (auto& fn : jsBindingFns_) JS_FreeValue(ctx, fn);
    jsBindingFns_.clear();
    for (auto& fn : jsEventFns_) JS_FreeValue(ctx, fn);
    jsEventFns_.clear();
    if (!JS_IsUndefined(jsState_))   JS_FreeValue(ctx, jsState_);
    if (!JS_IsUndefined(jsOptions_)) JS_FreeValue(ctx, jsOptions_);
    jsState_   = JS_UNDEFINED;
    jsOptions_ = JS_UNDEFINED;
    jsRt_.reset();
}

void PageState::AttachQuickJS(CompiledPage page) {
    DetachQuickJS();
    InvalidateMenuHandles();
    page_ = std::move(page);

    jsRt_ = std::make_unique<ui::uix::ScriptRuntime>();
    jsRt_->SetUserData(this);
    JSContext* ctx = jsRt_->ctx();

    bool hasScript = false;
    for (char c : page_.scriptSource) {
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') { hasScript = true; break; }
    }

    JSValue dataObj;
    if (hasScript) {
        auto evalRes = jsRt_->EvalModule(page_.scriptSource, "<script>");
        if (!evalRes.ok) {
            errors_.push_back("<script> JS error: " + jsRt_->lastError());
            return;
        }
        jsOptions_ = evalRes.defaultExport;

        JSValue dataFn = JS_GetPropertyStr(ctx, jsOptions_, "data");
        if (JS_IsFunction(ctx, dataFn)) {
            dataObj = JS_Call(ctx, dataFn, JS_UNDEFINED, 0, nullptr);
        } else if (JS_IsObject(dataFn)) {
            dataObj = JS_DupValue(ctx, dataFn);
        } else {
            dataObj = JS_NewObject(ctx);
        }
        JS_FreeValue(ctx, dataFn);

        if (JS_IsException(dataObj)) {
            errors_.push_back("data() threw");
            JSValue exc = JS_GetException(ctx);
            JS_FreeValue(ctx, exc);
            JS_FreeValue(ctx, dataObj);
            return;
        }
    } else {
        jsOptions_ = JS_UNDEFINED;
        dataObj    = JS_NewObject(ctx);
    }

    // 3a. Merge methods{} onto data BEFORE Proxy wrap.
    if (!JS_IsUndefined(jsOptions_)) {
        JSValue methods = JS_GetPropertyStr(ctx, jsOptions_, "methods");
        if (JS_IsObject(methods)) {
            JSPropertyEnum* tab = nullptr;
            uint32_t        len = 0;
            if (JS_GetOwnPropertyNames(ctx, &tab, &len, methods,
                                        JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) >= 0) {
                for (uint32_t i = 0; i < len; ++i) {
                    JSValue m = JS_GetProperty(ctx, methods, tab[i].atom);
                    JS_SetProperty(ctx, dataObj, tab[i].atom, m);
                    JS_FreeAtom(ctx, tab[i].atom);
                }
                js_free(ctx, tab);
            }
        }
        JS_FreeValue(ctx, methods);
    }

    // 3a.bis. i18n: $locale + $t on dataObj.
    JS_SetPropertyStr(ctx, dataObj, "$locale",
                       JS_NewStringLen(ctx, currentLocale_.data(),
                                       currentLocale_.size()));
    JS_SetPropertyStr(ctx, dataObj, "$t",
                       JS_NewCFunction(ctx, &JsTranslateTrampoline, "$t", 1));
    JS_SetPropertyStr(ctx, dataObj, "$rect",
                       JS_NewCFunction(ctx, &JsRectTrampoline, "$rect", 1));
    JS_SetPropertyStr(ctx, dataObj, "$windowSize",
                       JS_NewCFunction(ctx, &JsWindowSizeTrampoline, "$windowSize", 0));
    JS_SetPropertyStr(ctx, dataObj, "$nextTick",
                       JS_NewCFunction(ctx, &JsNextTickTrampoline, "$nextTick", 1));

    // 3b. MakeReactive — capture dataObj's pointer first; the Proxy keeps a
    // ref so the underlying object stays alive, and the get-trap uses the
    // same pointer as the dep-map key.
    void* dataPtr = JS_VALUE_GET_PTR(dataObj);
    jsState_ = jsRt_->MakeReactive(dataObj);
    JS_FreeValue(ctx, dataObj);
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "state", JS_DupValue(ctx, jsState_));
    JS_FreeValue(ctx, global);

    // 3c. computed{} — lazy memoized derived values.
    JSValue computed = JS_IsUndefined(jsOptions_)
                        ? JS_UNDEFINED
                        : JS_GetPropertyStr(ctx, jsOptions_, "computed");
    if (JS_IsObject(computed)) {
        JSPropertyEnum* tab = nullptr;
        uint32_t        len = 0;
        if (JS_GetOwnPropertyNames(ctx, &tab, &len, computed,
                                    JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) >= 0) {
            for (uint32_t i = 0; i < len; ++i) {
                JSValue fn = JS_GetProperty(ctx, computed, tab[i].atom);
                const char* name = JS_AtomToCString(ctx, tab[i].atom);
                if (name && JS_IsFunction(ctx, fn)) {
                    jsRt_->DefineComputed(dataPtr, name, fn, jsState_);
                }
                if (name) JS_FreeCString(ctx, name);
                JS_FreeValue(ctx, fn);
                JS_FreeAtom(ctx, tab[i].atom);
            }
            js_free(ctx, tab);
        }
    }
    JS_FreeValue(ctx, computed);

    // 4. Wire each CompiledBinding as a WatchEffect.
    for (auto& b : page_.bindings) {
        std::string rewritten = ui::uix::RewriteTemplateExpr(b.sourceJs);
        JSValue fn = jsRt_->CompileBindingClosure(rewritten);
        if (JS_IsException(fn)) {
            errors_.push_back("binding compile error: " + jsRt_->lastError() +
                              "\n  source: " + b.sourceJs);
            continue;
        }
        jsBindingFns_.push_back(fn);

        Widget*     target = b.target;
        std::string prop   = b.property;

        uint64_t effectId = jsRt_->WatchEffect([this, ctx, fn, target, prop]() {
            JSValue r = JS_Call(ctx, fn, jsState_, 0, nullptr);
            if (JS_IsException(r)) {
                JSValue exc = JS_GetException(ctx);
                JS_FreeValue(ctx, exc);
                JS_FreeValue(ctx, r);
                return;
            }
            ui::expr::Value ev = ui::uix::JSValueToExprValue(ctx, r);
            ApplyBindingToWidget(target, prop, ev);
            JS_FreeValue(ctx, r);
        });
        jsEffectIds_.push_back(effectId);
    }

    // 5. v-model write-back.
    for (auto& mw : page_.modelWrites) {
        WireQuickJSModelWrite(mw.target, mw.propertyName);
    }

    // 6. v-if conditionals.
    WireQuickJSConditionals();

    // 7. @event handlers.
    for (auto& ev : page_.events) {
        WireQuickJSEvent(ev.target, ev.event, ev.sourceJs);
    }

    // 8. v-for loops.
    WireQuickJSLoops();

    // 9. Menus.
    WireMenus();

    for (auto& e : page_.errors) errors_.push_back(e);

    // 10. Initial mount hooks — fire once for every widget already in the
    // root tree (anything outside v-if/v-for is mounted at this point).
    // v-if and v-for paths fire their own hooks at mount time.
    if (page_.root) DispatchMountHooks(page_.root.get());
}

void PageState::WireQuickJSConditionals() {
    // Snapshot the size up-front: nested v-if mounts append their
    // sub.conditionals to page_.conditionals and wire those entries
    // themselves. We must NOT double-wire those late-added entries here.
    size_t initial = page_.conditionals.size();
    for (size_t i = 0; i < initial; ++i) {
        WireConditional(page_.conditionals[i]);
    }
}

void PageState::WireConditional(const CompiledConditional& c) {
    JSContext* ctx = jsRt_->ctx();
    {
        auto rt = std::make_unique<JsCondRuntime>();
        rt->parent       = c.parentWidget;
        rt->insertIdx    = c.insertIndex;
        rt->templateNode = c.templateNode;
        rt->sheet        = page_.ownedStylesheet.get();

        std::string rewritten = ui::uix::RewriteTemplateExpr(c.condSourceJs);
        rt->fn = jsRt_->CompileBindingClosure(rewritten);
        if (JS_IsException(rt->fn)) {
            errors_.push_back("v-if compile error: " + jsRt_->lastError() +
                              "\n  source: " + c.condSourceJs);
            return;
        }

        JsCondRuntime* rtRaw = rt.get();
        jsCondRuntimes_.push_back(std::move(rt));

        uint64_t effectId = jsRt_->WatchEffect([this, ctx, rtRaw]() {
            JSValue r = JS_Call(ctx, rtRaw->fn, jsState_, 0, nullptr);
            if (JS_IsException(r)) {
                JSValue exc = JS_GetException(ctx); JS_FreeValue(ctx, exc);
                JS_FreeValue(ctx, r);
                return;
            }
            bool truthy = ui::uix::JSValueToBool(ctx, r);
            JS_FreeValue(ctx, r);

            bool isMounted = rtRaw->mounted != nullptr;
            if (truthy && !isMounted) {
                ui::RequestLayout();
                auto sub = CompileConditionalTemplate(*rtRaw->templateNode,
                                                     *rtRaw->sheet,
                                                     page_.cssVars);
                if (!sub.root) return;
                rtRaw->mounted = sub.root;
                if (rtRaw->parent) {
                    rtRaw->parent->InsertChild(rtRaw->insertIdx, sub.root);
                }
                for (auto& b : sub.bindings) {
                    std::string rew = ui::uix::RewriteTemplateExpr(b.sourceJs);
                    JSValue fn = jsRt_->CompileBindingClosure(rew);
                    if (JS_IsException(fn)) continue;
                    rtRaw->innerFns.push_back(fn);
                    Widget*     tg = b.target;
                    std::string pp = b.property;
                    uint64_t id = jsRt_->WatchEffect([this, ctx, fn, tg, pp]() {
                        JSValue rr = JS_Call(ctx, fn, jsState_, 0, nullptr);
                        if (JS_IsException(rr)) {
                            JSValue ex = JS_GetException(ctx); JS_FreeValue(ctx, ex);
                            JS_FreeValue(ctx, rr);
                            return;
                        }
                        ui::expr::Value ev = ui::uix::JSValueToExprValue(ctx, rr);
                        ApplyBindingToWidget(tg, pp, ev);
                        JS_FreeValue(ctx, rr);
                    });
                    rtRaw->innerEffects.push_back(id);
                }
                for (auto& mw : sub.modelWrites) {
                    WireQuickJSModelWrite(mw.target, mw.propertyName);
                }
                for (auto& sev : sub.events) {
                    WireQuickJSEvent(sev.target, sev.event, sev.sourceJs);
                }
                // v-for inside this v-if: move loop specs into long-lived
                // storage on the JsCondRuntime so spec ptrs stay valid for
                // the runtime lifetime; build a JsLoopRuntime per spec.
                rtRaw->innerLoopSpecs = std::move(sub.loops);
                rtRaw->innerLoops.reserve(rtRaw->innerLoopSpecs.size());
                for (auto& spec : rtRaw->innerLoopSpecs) {
                    rtRaw->innerLoops.push_back(BuildJsLoopRuntime(&spec));
                }
                // <menu> declared inside this v-if subtree — wire trigger +
                // dispatch so right-click / click on a freshly-mounted page
                // actually pops the menu. Without this the compiled menus
                // are produced but never registered.
                if (!sub.menus.empty()) {
                    WireSubtreeMenus(sub.menus, menus_);
                }
                // v-if directly inside this v-if — store the spec in
                // page_.conditionals (so the templateNode/parentWidget pointers
                // stay valid for the rest of the page lifetime) and wire each
                // one. WireQuickJSConditionals' main loop snapshots size() at
                // entry, so these late-added entries won't be re-processed.
                for (auto& sc : sub.conditionals) {
                    page_.conditionals.push_back(std::move(sc));
                    WireConditional(page_.conditionals.back());
                }
                // The compile-time cascade for the freshly-mounted subtree
                // sees only the static parent chain captured when the SFC
                // was parsed — runtime :class bindings on ancestors (e.g.
                // shell.dark) are invisible at that point. Walk the new
                // subtree and trigger recomputeStyle so each widget rebuilds
                // its MatchNode chain from the LIVE tree, picking up
                // ancestor dynamicClasses.
                std::function<void(Widget*)> reapply = [&](Widget* x) {
                    if (x->recomputeStyle) x->recomputeStyle(x->lastStateBits);
                    for (auto& c : x->Children()) reapply(c.get());
                };
                if (rtRaw->mounted) reapply(rtRaw->mounted.get());
                // Rebind C-level callbacks (ui_widget_on_click etc.) registered
                // before this v-if was first re-mounted. Keyed by widget id.
                if (rtRaw->mounted) ui::GetContext().RebindWidgetCallbacks(rtRaw->mounted.get());
                // Fire mount lifecycle hooks (Vue parity — let downstream
                // re-attach state on the freshly-built widget instance).
                if (rtRaw->mounted) DispatchMountHooks(rtRaw->mounted.get());
            } else if (!truthy && isMounted) {
                ui::RequestLayout();
                // Fire unmount hooks before tearing the subtree down so
                // user code can read final widget state.
                if (rtRaw->mounted) DispatchUnmountHooks(rtRaw->mounted.get());
                for (auto& lr : rtRaw->innerLoops) if (lr) TearDownJsLoopRuntime(*lr);
                rtRaw->innerLoops.clear();
                rtRaw->innerLoopSpecs.clear();
                for (uint64_t id : rtRaw->innerEffects) jsRt_->DisposeEffect(id);
                rtRaw->innerEffects.clear();
                for (auto& f : rtRaw->innerFns) JS_FreeValue(ctx, f);
                rtRaw->innerFns.clear();
                if (rtRaw->parent && rtRaw->mounted) {
                    rtRaw->parent->RemoveChild(rtRaw->mounted.get());
                }
                rtRaw->mounted.reset();
            }
        });
        jsEffectIds_.push_back(effectId);
    }
}

// Build a JsLoopRuntime for a single CompiledLoop. Used both for the
// page's top-level loops and for v-for loops nested inside v-if subtrees.
// `spec` must outlive the returned runtime — the runtime stores it as a
// raw pointer.
std::unique_ptr<PageState::JsLoopRuntime>
PageState::BuildJsLoopRuntime(const CompiledLoop* spec) {
    return BuildJsLoopRuntimeInScope(spec, {}, {}, nullptr, false);
}

std::unique_ptr<PageState::JsLoopRuntime>
PageState::BuildJsLoopRuntimeInScope(const CompiledLoop* spec,
                                      const std::set<std::string>& outerLocals,
                                      const std::vector<std::string>& outerParamNames,
                                      JsLoopIteration* outerIter,
                                      bool outerHasIdx) {
    auto rt = std::make_unique<JsLoopRuntime>();
    rt->spec            = spec;
    rt->outerIter       = outerIter;
    rt->outerParamNames = outerParamNames;
    rt->outerLocals     = outerLocals;
    rt->outerHasIdx     = outerHasIdx;

    // Compile listFn with outer scope params (so `grp.items` resolves to the
    // current outer iteration's `grp`). When outerParamNames is empty this
    // produces the same `(function(){ return EXPR; })` shape as before.
    std::string listRew = ui::uix::RewriteTemplateExpr(spec->listSourceJs, outerLocals);
    if (outerParamNames.empty()) {
        rt->listFn = jsRt_->CompileBindingClosure(listRew);
    } else {
        rt->listFn = jsRt_->CompileLoopBindingClosure(listRew, outerParamNames);
    }
    if (JS_IsException(rt->listFn)) {
        errors_.push_back("v-for list compile error: " + jsRt_->lastError() +
                          "\n  source: " + spec->listSourceJs);
        rt->listFn = JS_UNDEFINED;
        return rt;
    }

    if (!spec->keySourceJs.empty()) {
        std::set<std::string> locals = outerLocals;
        if (!spec->loopVar.empty())  locals.insert(spec->loopVar);
        if (!spec->indexVar.empty()) locals.insert(spec->indexVar);
        std::vector<std::string> keyParams = outerParamNames;
        if (!spec->loopVar.empty())  keyParams.push_back(spec->loopVar);
        if (!spec->indexVar.empty()) keyParams.push_back(spec->indexVar);
        std::string keyRew = ui::uix::RewriteTemplateExpr(spec->keySourceJs, locals);
        rt->keyFn = jsRt_->CompileLoopBindingClosure(keyRew, keyParams);
        if (JS_IsException(rt->keyFn)) {
            errors_.push_back("v-for :key compile error: " + jsRt_->lastError() +
                              "\n  source: " + spec->keySourceJs);
            rt->keyFn = JS_UNDEFINED;
        }
    }

    JsLoopRuntime* rtRaw = rt.get();
    rt->watcherId = jsRt_->WatchEffect([this, rtRaw]() {
        RebuildJsLoop(*rtRaw);
    });

    return rt;
}

void PageState::WireQuickJSLoops() {
    for (auto& spec : page_.loops) {
        jsLoopRuntimes_.push_back(BuildJsLoopRuntime(&spec));
    }
}

// Tear down a single loop runtime: dispose iterations + watcher + free
// closures. Used at v-if unmount and at DetachQuickJS time.
void PageState::TearDownJsLoopRuntime(JsLoopRuntime& rt) {
    if (!jsRt_) return;
    JSContext* ctx = jsRt_->ctx();
    TearDownJsIterations(rt);
    if (rt.watcherId) jsRt_->DisposeEffect(rt.watcherId);
    if (!JS_IsUndefined(rt.listFn)) JS_FreeValue(ctx, rt.listFn);
    if (!JS_IsUndefined(rt.keyFn))  JS_FreeValue(ctx, rt.keyFn);
}

// Destroy a single iteration: drop its effects, free its closures, free
// stored item/idx JSValues, unmount its widget. Used for both full
// teardown and the keyed-diff "remove unused iter" path.
void PageState::DestroyJsIteration(JsLoopRuntime& rt, JsLoopIteration& iter) {
    JSContext* ctx = jsRt_->ctx();
    // Fire unmount hooks before any teardown so callbacks can still read
    // widget state.
    if (iter.mountedRoot) DispatchUnmountHooks(iter.mountedRoot.get());
    // Tear down nested v-if runtimes first — their inner effects reference
    // the iteration's itemValue/idxValue, which we're about to JS_FreeValue.
    // Recurse through innerConditionals (v-if inside v-if inside v-for).
    std::function<void(CondInIter&)> tearDownCond = [&](CondInIter& cr) {
        // v-for inside v-if — kill iterations + watcher first (they reference
        // the outer iteration's itemValue, which DestroyJsIteration is about
        // to free).
        for (auto& lr : cr.innerLoops) {
            if (lr) TearDownJsLoopRuntime(*lr);
        }
        cr.innerLoops.clear();
        cr.innerLoopSpecs.clear();
        for (auto& innerCr : cr.innerConditionals) {
            if (innerCr) tearDownCond(*innerCr);
        }
        cr.innerConditionals.clear();
        if (cr.effectId) jsRt_->DisposeEffect(cr.effectId);
        for (uint64_t id : cr.innerEffects) jsRt_->DisposeEffect(id);
        for (auto& f : cr.innerFns) JS_FreeValue(ctx, f);
        cr.innerEffects.clear();
        cr.innerEvalFns.clear();
        cr.innerFns.clear();
        if (!JS_IsUndefined(cr.fn)) JS_FreeValue(ctx, cr.fn);
        cr.fn = JS_UNDEFINED;
        cr.mounted.reset();
    };
    for (auto& cr : iter.conditionals) {
        if (cr) tearDownCond(*cr);
    }
    iter.conditionals.clear();

    for (auto& b : iter.bindings) {
        if (b.effectId) jsRt_->DisposeEffect(b.effectId);
        if (!JS_IsUndefined(b.fn)) JS_FreeValue(ctx, b.fn);
    }
    iter.bindings.clear();
    for (auto& fn : iter.eventFns) JS_FreeValue(ctx, fn);
    iter.eventFns.clear();
    if (!JS_IsUndefined(iter.itemValue)) JS_FreeValue(ctx, iter.itemValue);
    if (!JS_IsUndefined(iter.idxValue))  JS_FreeValue(ctx, iter.idxValue);
    iter.itemValue = JS_UNDEFINED;
    iter.idxValue  = JS_UNDEFINED;
    if (iter.mountedRoot && rt.spec && rt.spec->parentWidget) {
        rt.spec->parentWidget->RemoveChild(iter.mountedRoot.get());
    }
    iter.mountedRoot.reset();
}

void PageState::TearDownJsIterations(JsLoopRuntime& rt) {
    if (!jsRt_) return;
    for (auto& iter : rt.iterations) {
        if (iter) DestroyJsIteration(rt, *iter);
    }
    rt.iterations.clear();
}

// Compute the key string for an item. Falls back to "#<idx>" when no
// :key was declared (effectively positional reuse, which still preserves
// widget identity for stable-length lists).
std::string PageState::ComputeIterationKey(JsLoopRuntime& rt, JSValue item,
                                            JSValue idx, uint32_t pos) {
    if (JS_IsUndefined(rt.keyFn)) {
        return "#" + std::to_string(pos);
    }
    JSContext* ctx = jsRt_->ctx();
    bool hasIdx = !rt.spec->indexVar.empty();
    JSValue args[4];
    int argc = 0;
    if (rt.outerIter) {
        args[argc++] = rt.outerIter->itemValue;
        if (rt.outerHasIdx) args[argc++] = rt.outerIter->idxValue;
    }
    args[argc++] = item;
    if (hasIdx) args[argc++] = idx;
    JSValue r = JS_Call(ctx, rt.keyFn, jsState_, argc, args);
    if (JS_IsException(r)) {
        JSValue exc = JS_GetException(ctx); JS_FreeValue(ctx, exc);
        JS_FreeValue(ctx, r);
        return "#" + std::to_string(pos);
    }
    const char* s = JS_ToCString(ctx, r);
    std::string out = s ? s : ("#" + std::to_string(pos));
    if (s) JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, r);
    return out;
}

// Re-bind an existing iteration's binding effects to fresh deps. Called
// when keyed diff reuses the iteration: the (item, idx) JSValues changed,
// so any binding that read `this.something` needs its dep set rebuilt.
// We dispose the old effects (clears them from depMap_) and re-WatchEffect
// using the SAME stored fn JSValues + (target, property) — no recompile,
// no widget rebuild.
void PageState::RewireIterationBindings(JsLoopRuntime& rt, JsLoopIteration& iter) {
    JSContext* ctx = jsRt_->ctx();
    bool hasIdx   = !rt.spec->indexVar.empty();
    JsLoopIteration* iterRaw = &iter;
    JsLoopRuntime*   rtPtr   = &rt;

    for (auto& b : iter.bindings) {
        if (b.effectId) jsRt_->DisposeEffect(b.effectId);
        b.effectId = 0;

        Widget*     target = b.target;
        std::string prop   = b.property;
        JSValue     fn     = b.fn;

        b.effectId = jsRt_->WatchEffect(
            [this, ctx, fn, target, prop, iterRaw, hasIdx, rtPtr]() {
                JSValue args[4];
                int argc = 0;
                if (rtPtr->outerIter) {
                    args[argc++] = rtPtr->outerIter->itemValue;
                    if (rtPtr->outerHasIdx) args[argc++] = rtPtr->outerIter->idxValue;
                }
                args[argc++] = iterRaw->itemValue;
                if (hasIdx) args[argc++] = iterRaw->idxValue;
                JSValue r = JS_Call(ctx, fn, jsState_, argc, args);
                if (JS_IsException(r)) {
                    JSValue exc = JS_GetException(ctx); JS_FreeValue(ctx, exc);
                    JS_FreeValue(ctx, r); return;
                }
                ui::expr::Value ev = ui::uix::JSValueToExprValue(ctx, r);
                ApplyBindingToWidget(target, prop, ev);
                JS_FreeValue(ctx, r);
            });
    }

    // build 287: v-if 也得跟着重接。bindings 走的是 ApplyBindingToWidget, 属性
    // 变了就能看出来; v-if 决定的是 widget 在不在, 依赖集失效的后果是整个分支
    // 卡死在复用前的形态。先重接本层再递归 inner —— 本层 effect 立刻重跑, 若
    // 判 false 会 unmount 并连带销毁 innerConditionals, 那时递归自然跳过。
    std::function<void(std::vector<std::unique_ptr<CondInIter>>&)> rewireConds =
        [&](std::vector<std::unique_ptr<CondInIter>>& conds) {
            for (auto& cr : conds) {
                if (!cr || !cr->evalFn) continue;
                if (cr->effectId) jsRt_->DisposeEffect(cr->effectId);
                cr->effectId = jsRt_->WatchEffect(cr->evalFn);

                /* build 289: 子树**内部**的 binding 也要重接。只修条件不修内部,
                 * 表现是"该出现的分支出现了, 但里面的数字/文字还是旧的"。
                 * 注意顺序: 上面的 effect 刚跑过, 若判 false 会 unmount 并把
                 * innerEffects/innerEvalFns 清空, 这里自然就没得重接了。 */
                const size_t n = std::min(cr->innerEffects.size(),
                                          cr->innerEvalFns.size());
                for (size_t k = 0; k < n; ++k) {
                    if (cr->innerEffects[k]) jsRt_->DisposeEffect(cr->innerEffects[k]);
                    cr->innerEffects[k] = jsRt_->WatchEffect(cr->innerEvalFns[k]);
                }
                if (!cr->innerConditionals.empty()) rewireConds(cr->innerConditionals);
            }
        };
    rewireConds(iter.conditionals);
}

void PageState::RebuildJsLoop(JsLoopRuntime& rt) {

    /* 整个重建过程合成一批: 中途每条绑定各自 InvalidateAllWindows +
     * UpdateAnimTimers 是 O(n^2) 的来源 (见 ui_context.h BeginBatch 注释)。
     * 中途没有任何一帧上屏, 合批不改变可见行为。 */
    ui::Context::BatchScope batch(ui::GetContext());

    if (!rt.spec || !rt.spec->parentWidget || !rt.spec->templateNode) return;
    if (!jsRt_ || JS_IsUndefined(rt.listFn)) return;
    JSContext* ctx = jsRt_->ctx();

    // Build outer args (for nested v-for inside v-if inside v-for).
    JSValue outerArgs[2];
    int outerArgc = 0;
    if (rt.outerIter) {
        outerArgs[outerArgc++] = rt.outerIter->itemValue;
        if (rt.outerHasIdx) outerArgs[outerArgc++] = rt.outerIter->idxValue;
    }
    JSValue list = JS_Call(ctx, rt.listFn, jsState_, outerArgc, outerArgs);
    if (JS_IsException(list)) {
        JSValue exc = JS_GetException(ctx);
        const char* msg = JS_ToCString(ctx, exc);
        if (msg) {
            errors_.push_back(std::string("v-for list eval threw: ") + msg);
            JS_FreeCString(ctx, msg);
        }
        JS_FreeValue(ctx, exc);
        JS_FreeValue(ctx, list);
        return;
    }

    if (!JS_IsArray(list)) {
        TearDownJsIterations(rt);
        JS_FreeValue(ctx, list);
        return;
    }

    JSValue lenV = JS_GetPropertyStr(ctx, list, "length");
    uint32_t len = 0;
    JS_ToUint32(ctx, &len, lenV);
    JS_FreeValue(ctx, lenV);

    // Index existing iterations by key — duplicates collapse to last seen
    // (matches Vue's "last duplicate wins" though duplicates are a user bug).
    std::unordered_map<std::string, std::unique_ptr<JsLoopIteration>> oldByKey;
    for (auto& it : rt.iterations) {
        if (!it) continue;
        oldByKey.emplace(it->key, std::move(it));
    }
    rt.iterations.clear();

    if (len > 0) ui::RequestLayout();

    // Walk new list. For each item: compute its key, reuse if matched, else
    // build fresh.
    std::vector<std::unique_ptr<JsLoopIteration>> newIters;
    newIters.reserve(len);
    for (uint32_t i = 0; i < len; ++i) {
        JSValue itemVal = JS_GetPropertyUint32(ctx, list, i);
        JSValue idxVal  = JS_NewInt32(ctx, static_cast<int>(i));
        std::string key = ComputeIterationKey(rt, itemVal, idxVal, i);

        auto found = oldByKey.find(key);
        if (found != oldByKey.end() && found->second) {
            // Reuse: keep widget tree + onClick wiring, refresh stored
            // (item, idx) JSValues + re-collect dep set for bindings.
            auto iter = std::move(found->second);
            oldByKey.erase(found);
            JS_FreeValue(ctx, iter->itemValue);
            JS_FreeValue(ctx, iter->idxValue);
            iter->itemValue = itemVal;     // takes ownership
            iter->idxValue  = idxVal;
            RewireIterationBindings(rt, *iter);
            newIters.push_back(std::move(iter));
        } else {
            // New: full build (compiles per-iteration template + closures).
            auto iter = BuildJsIteration(rt, itemVal, idxVal, i);
            iter->key = std::move(key);
            newIters.push_back(std::move(iter));
        }
    }

    // Tear down whichever old iterations weren't reused.
    for (auto& kv : oldByKey) {
        if (kv.second) DestroyJsIteration(rt, *kv.second);
    }
    oldByKey.clear();

    // Reorder the parent's children so iterations appear in the new list
    // order. New iters were already InsertChild'd at construction, but at the
    // wrong slot; reused ones sit wherever they were — so the whole block gets
    // lifted out and put back contiguously at insertIndex.
    //
    // build 285: 这里原本是 "n 次 RemoveChild + n 次 InsertChild"。两者各自
    // 都是 O(n) —— RemoveChild 用 remove_if 扫全表, InsertChild 是 vector
    // 中间插入的 memmove —— 合起来 O(n^2)。2000 行实测 6.6 秒, 5000 行 41 秒。
    // 换成 "一趟批量摘除 + 一次整块插入", 两次 O(n)。
    if (rt.spec->parentWidget) {
        std::unordered_set<Widget*> lifted;
        lifted.reserve(newIters.size() * 2);
        std::vector<WidgetPtr> block;
        block.reserve(newIters.size());
        for (auto& iter : newIters) {
            if (iter && iter->mountedRoot) {
                lifted.insert(iter->mountedRoot.get());
                block.push_back(iter->mountedRoot);
            }
        }
        rt.spec->parentWidget->RemoveChildren(lifted);
        size_t pos = rt.spec->insertIndex;
        if (pos > rt.spec->parentWidget->Children().size())
            pos = rt.spec->parentWidget->Children().size();
        rt.spec->parentWidget->ReplaceChildRange(pos, 0, std::move(block));
    }

    rt.iterations = std::move(newIters);
    JS_FreeValue(ctx, list);
}

void PageState::WireLoopScopeEvent(Widget* target, const std::string& evName,
                                    JSValue fn, JsLoopIteration* iter, bool hasIdx) {
    WireLoopScopeEventEx(target, evName, fn, iter, hasIdx, nullptr);
}

void PageState::WireLoopScopeEventEx(Widget* target, const std::string& evName,
                                      JSValue fn, JsLoopIteration* iter, bool hasIdx,
                                      JsLoopRuntime* rt) {
    if (!target || !jsRt_) return;
    JSContext* ctx = jsRt_->ctx();

    // callHandler: prepends [outerItem, outerIdx?,] item, idx? before $event.
    // outer scope only present when this v-for is nested inside v-if inside v-for.
    auto callHandler = [this, ctx, fn, iter, hasIdx, evName, rt](JSValue arg) {
        JSValue args[5];
        int argc = 0;
        if (rt && rt->outerIter) {
            args[argc++] = rt->outerIter->itemValue;
            if (rt->outerHasIdx) args[argc++] = rt->outerIter->idxValue;
        }
        args[argc++] = iter->itemValue;
        if (hasIdx) args[argc++] = iter->idxValue;
        args[argc++] = arg;
        JSValue r = JS_Call(ctx, fn, jsState_, argc, args);
        if (JS_IsException(r)) {
            JSValue exc = JS_GetException(ctx);
            const char* msg = JS_ToCString(ctx, exc);
            if (msg) {
                errors_.push_back("v-for @" + evName + " threw: " + msg);
                JS_FreeCString(ctx, msg);
            }
            JS_FreeValue(ctx, exc);
        }
        JS_FreeValue(ctx, r);
        JS_FreeValue(ctx, arg);
    };

    if (evName == "click") {
        target->onClick = [callHandler]() { callHandler(JS_UNDEFINED); };
    } else if (evName == "change" || evName == "input") {
        target->onValueChanged = [callHandler, ctx](bool v) {
            callHandler(JS_NewBool(ctx, v));
        };
        target->onTextChanged = [callHandler, ctx](const std::wstring& v) {
            std::string utf8 = ToUtf8(v);
            callHandler(JS_NewStringLen(ctx, utf8.data(), utf8.size()));
        };
        target->onFloatChanged = [callHandler, ctx](float v) {
            callHandler(JS_NewFloat64(ctx, static_cast<double>(v)));
        };
    } else if (evName == "dblclick") {
        target->onMouseDblClickHook = [callHandler, ctx](const ui::MouseEvent& e) {
            JSValue obj = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, obj, "x",      JS_NewFloat64(ctx, e.x));
            JS_SetPropertyStr(ctx, obj, "y",      JS_NewFloat64(ctx, e.y));
            JS_SetPropertyStr(ctx, obj, "delta",  JS_NewFloat64(ctx, e.delta));
            JS_SetPropertyStr(ctx, obj, "button", JS_NewInt32(ctx, e.leftBtn ? 0 : -1));
            callHandler(obj);
        };
    }
    // Other events (mousedown / mouseup / wheel / focus / blur) — add as needed.
}

std::unique_ptr<CondInIter> PageState::BuildCondInIter(
    const CompiledConditional& c,
    const std::set<std::string>& locals,
    const std::string& loopVar,
    const std::string& indexVar,
    JsLoopIteration* iterRaw,
    bool hasIdx) {

    if (!jsRt_) return nullptr;
    JSContext* ctx = jsRt_->ctx();

    auto cr = std::make_unique<CondInIter>();
    cr->parent       = c.parentWidget;
    cr->insertIdx    = c.insertIndex;
    cr->templateNode = c.templateNode;

    std::string rew = ui::uix::RewriteTemplateExpr(c.condSourceJs, locals);
    cr->fn = jsRt_->CompileLoopBindingClosure(rew, loopVar, indexVar);
    if (JS_IsException(cr->fn)) {
        errors_.push_back("v-for > v-if compile error: " + jsRt_->lastError() +
                          "\n  source: " + c.condSourceJs);
        return nullptr;
    }

    CondInIter* crRaw = cr.get();

    cr->evalFn =
        [this, ctx, crRaw, iterRaw, hasIdx, loopVar, indexVar, locals]() {
            JSValue args[2];
            int argc = 1;
            args[0] = iterRaw->itemValue;
            if (hasIdx) args[argc++] = iterRaw->idxValue;
            JSValue r = JS_Call(ctx, crRaw->fn, jsState_, argc, args);
            if (JS_IsException(r)) {
                JSValue exc = JS_GetException(ctx); JS_FreeValue(ctx, exc);
                JS_FreeValue(ctx, r);
                return;
            }
            bool truthy = ui::uix::JSValueToBool(ctx, r);
            JS_FreeValue(ctx, r);

            bool isMounted = crRaw->mounted != nullptr;
            if (truthy && !isMounted) {
                ui::RequestLayout();
                auto sub = CompileConditionalTemplate(*crRaw->templateNode,
                                                      *page_.ownedStylesheet,
                                                      page_.cssVars);
                if (!sub.root) return;
                crRaw->mounted = sub.root;
                if (crRaw->parent) {
                    crRaw->parent->InsertChild(crRaw->insertIdx, sub.root);
                }
                // Bindings — loop closure
                for (auto& b : sub.bindings) {
                    std::string brew = ui::uix::RewriteTemplateExpr(b.sourceJs, locals);
                    JSValue bfn = jsRt_->CompileLoopBindingClosure(brew, loopVar, indexVar);
                    if (JS_IsException(bfn)) continue;
                    crRaw->innerFns.push_back(bfn);
                    Widget* tg = b.target;
                    std::string pp = b.property;
                    /* 求值体存一份 (build 289) —— 复用行时要拿它重接依赖。 */
                    std::function<void()> evalOne =
                        [this, ctx, bfn, tg, pp, iterRaw, hasIdx]() {
                            JSValue args2[2];
                            int argc2 = 1;
                            args2[0] = iterRaw->itemValue;
                            if (hasIdx) args2[argc2++] = iterRaw->idxValue;
                            JSValue rr = JS_Call(ctx, bfn, jsState_, argc2, args2);
                            if (JS_IsException(rr)) {
                                JSValue ex = JS_GetException(ctx); JS_FreeValue(ctx, ex);
                                JS_FreeValue(ctx, rr);
                                return;
                            }
                            ui::expr::Value ev = ui::uix::JSValueToExprValue(ctx, rr);
                            ApplyBindingToWidget(tg, pp, ev);
                            JS_FreeValue(ctx, rr);
                        };
                    uint64_t eid = jsRt_->WatchEffect(evalOne);
                    crRaw->innerEffects.push_back(eid);
                    crRaw->innerEvalFns.push_back(std::move(evalOne));
                }
                // Events — loop closure
                for (auto& sev : sub.events) {
                    if (!sev.target) continue;
                    std::string erew = ui::uix::RewriteTemplateExpr(sev.sourceJs, locals);
                    JSValue efn = jsRt_->CompileLoopHandlerClosure(erew, loopVar, indexVar);
                    if (JS_IsException(efn)) {
                        errors_.push_back("v-for > v-if @" + sev.event + " compile error: " +
                                          jsRt_->lastError() + "\n  source: " + sev.sourceJs);
                        continue;
                    }
                    crRaw->innerFns.push_back(efn);
                    WireLoopScopeEvent(sev.target, sev.event, efn, iterRaw, hasIdx);
                }
                // Menus declared inside this v-if subtree.
                if (!sub.menus.empty()) {
                    WireSubtreeMenus(sub.menus, menus_);
                }
                // Recurse: v-if directly inside this v-if (still inside the
                // same v-for iteration, same loop scope). Each builds its own
                // CondInIter and is owned by the outer one so unmount tears
                // them down together.
                for (auto& innerC : sub.conditionals) {
                    auto innerCr = BuildCondInIter(innerC, locals, loopVar, indexVar,
                                                    iterRaw, hasIdx);
                    if (innerCr) crRaw->innerConditionals.push_back(std::move(innerCr));
                }
                // v-for declared inside this v-if subtree (so the full chain
                // is v-for > v-if > v-for). Move the loop specs into long-
                // lived storage on the CondInIter — JsLoopRuntime::spec is a
                // raw pointer and must stay valid for the runtime's lifetime.
                // Each inner v-for's listFn / keyFn / per-iter closures are
                // compiled with the outer iteration's loopVar/indexVar
                // prepended; at call time those values come from `iterRaw`.
                std::vector<std::string> outerParams;
                if (!loopVar.empty())  outerParams.push_back(loopVar);
                if (!indexVar.empty()) outerParams.push_back(indexVar);
                crRaw->innerLoopSpecs = std::move(sub.loops);
                crRaw->innerLoops.reserve(crRaw->innerLoopSpecs.size());
                for (auto& spec : crRaw->innerLoopSpecs) {
                    crRaw->innerLoops.push_back(
                        BuildJsLoopRuntimeInScope(&spec, locals, outerParams,
                                                   iterRaw, hasIdx));
                }
                // Re-cascade + RebindWidgetCallbacks + DispatchMountHooks
                std::function<void(Widget*)> reapply = [&](Widget* x) {
                    if (x->recomputeStyle) x->recomputeStyle(x->lastStateBits);
                    for (auto& cc : x->Children()) reapply(cc.get());
                };
                if (crRaw->mounted) reapply(crRaw->mounted.get());
                if (crRaw->mounted) ui::GetContext().RebindWidgetCallbacks(crRaw->mounted.get());
                if (crRaw->mounted) DispatchMountHooks(crRaw->mounted.get());
            } else if (!truthy && isMounted) {
                ui::RequestLayout();
                if (crRaw->mounted) DispatchUnmountHooks(crRaw->mounted.get());
                // Tear down nested v-for first — their iterations reference
                // iterRaw->itemValue. Then nested v-if (same reason).
                for (auto& lr : crRaw->innerLoops) {
                    if (lr) TearDownJsLoopRuntime(*lr);
                }
                crRaw->innerLoops.clear();
                crRaw->innerLoopSpecs.clear();
                for (auto& innerCr : crRaw->innerConditionals) {
                    if (innerCr->effectId) jsRt_->DisposeEffect(innerCr->effectId);
                    for (uint64_t id : innerCr->innerEffects) jsRt_->DisposeEffect(id);
                    for (auto& f : innerCr->innerFns) JS_FreeValue(ctx, f);
                    if (!JS_IsUndefined(innerCr->fn)) JS_FreeValue(ctx, innerCr->fn);
                    innerCr->mounted.reset();
                }
                crRaw->innerConditionals.clear();
                for (uint64_t id : crRaw->innerEffects) jsRt_->DisposeEffect(id);
                crRaw->innerEffects.clear();
                crRaw->innerEvalFns.clear();
                for (auto& f : crRaw->innerFns) JS_FreeValue(ctx, f);
                crRaw->innerFns.clear();
                if (crRaw->parent && crRaw->mounted) {
                    crRaw->parent->RemoveChild(crRaw->mounted.get());
                }
                crRaw->mounted.reset();
            }
        };

    cr->effectId = jsRt_->WatchEffect(cr->evalFn);

    return cr;
}

std::unique_ptr<PageState::JsLoopIteration> PageState::BuildJsIteration(
    JsLoopRuntime& rt, JSValue itemValue, JSValue idxValue, uint32_t idx) {
    auto iter = std::make_unique<JsLoopIteration>();
    iter->itemValue = itemValue;
    iter->idxValue  = idxValue;

    JSContext* ctx = jsRt_->ctx();

    iter->subPage = CompileIterationTemplate(*rt.spec->templateNode,
                                              *page_.ownedStylesheet,
                                              page_.cssVars);
    if (!iter->subPage.root) return iter;
    iter->mountedRoot = iter->subPage.root;

    if (rt.spec->parentWidget) {
        size_t pos = rt.spec->insertIndex + idx;
        if (pos > rt.spec->parentWidget->Children().size())
            pos = rt.spec->parentWidget->Children().size();
        rt.spec->parentWidget->InsertChild(pos, iter->mountedRoot);
    }

    // Locals + param names: union of outer scope and this loop's own vars.
    // Outer scope is non-empty only when we were built via
    // BuildJsLoopRuntimeInScope (i.e. v-for inside v-if inside v-for).
    std::set<std::string>    locals = rt.outerLocals;
    std::vector<std::string> paramNames = rt.outerParamNames;
    if (!rt.spec->loopVar.empty()) {
        locals.insert(rt.spec->loopVar);
        paramNames.push_back(rt.spec->loopVar);
    }
    if (!rt.spec->indexVar.empty()) {
        locals.insert(rt.spec->indexVar);
        paramNames.push_back(rt.spec->indexVar);
    }

    bool        hasIdx   = !rt.spec->indexVar.empty();
    JsLoopIteration* iterRaw = iter.get();
    JsLoopRuntime*   rtPtr   = &rt;

    for (auto& b : iter->subPage.bindings) {
        std::string rew = ui::uix::RewriteTemplateExpr(b.sourceJs, locals);
        JSValue fn = jsRt_->CompileLoopBindingClosure(rew, paramNames);
        if (JS_IsException(fn)) {
            errors_.push_back("v-for binding compile error: " + jsRt_->lastError() +
                              "\n  source: " + b.sourceJs);
            continue;
        }

        IterBinding ib;
        ib.fn       = fn;
        ib.target   = b.target;
        ib.property = b.property;

        Widget*     target = ib.target;
        std::string prop   = ib.property;

        ib.effectId = jsRt_->WatchEffect(
            [this, ctx, fn, target, prop, iterRaw, hasIdx, rtPtr]() {
                JSValue args[4];
                int argc = 0;
                if (rtPtr->outerIter) {
                    args[argc++] = rtPtr->outerIter->itemValue;
                    if (rtPtr->outerHasIdx) args[argc++] = rtPtr->outerIter->idxValue;
                }
                args[argc++] = iterRaw->itemValue;
                if (hasIdx) args[argc++] = iterRaw->idxValue;
                JSValue r = JS_Call(ctx, fn, jsState_, argc, args);
                if (JS_IsException(r)) {
                    JSValue exc = JS_GetException(ctx); JS_FreeValue(ctx, exc);
                    JS_FreeValue(ctx, r); return;
                }
                ui::expr::Value ev = ui::uix::JSValueToExprValue(ctx, r);
                ApplyBindingToWidget(target, prop, ev);
                JS_FreeValue(ctx, r);
            });
        iter->bindings.push_back(std::move(ib));
    }

    for (auto& ev : iter->subPage.events) {
        std::string rew = ui::uix::RewriteTemplateExpr(ev.sourceJs, locals);
        JSValue fn = jsRt_->CompileLoopHandlerClosure(rew, paramNames);
        if (JS_IsException(fn)) {
            errors_.push_back("v-for @" + ev.event + " compile error: " +
                              jsRt_->lastError() + "\n  source: " + ev.sourceJs);
            continue;
        }
        iter->eventFns.push_back(fn);
        if (!ev.target) continue;
        WireLoopScopeEventEx(ev.target, ev.event, fn, iterRaw, hasIdx, rtPtr);
    }
    // ---- v-if nodes nested directly inside this v-for iteration ----
    // The compiler picks up `<x v-if="...">` children of the loop template
    // and emits CompiledConditional records into iter->subPage.conditionals.
    // BuildCondInIter compiles each one against the loop scope and recurses
    // into nested v-if (v-if inside v-if inside v-for) automatically.
    std::string loopVar  = rt.spec->loopVar;
    std::string indexVar = rt.spec->indexVar;
    for (auto& c : iter->subPage.conditionals) {
        auto cr = BuildCondInIter(c, locals, loopVar, indexVar, iterRaw, hasIdx);
        if (cr) iter->conditionals.push_back(std::move(cr));
    }

    // Rebind any persistent C-level callbacks (ui_widget_on_click etc.) onto
    // the freshly-mounted iteration subtree. Keyed by widget HTML id, so
    // <button id="btn_x"> inside a v-for keeps its handler across re-mounts.
    if (iter->mountedRoot) {
        ui::GetContext().RebindWidgetCallbacks(iter->mountedRoot.get());
        DispatchMountHooks(iter->mountedRoot.get());
    }

    return iter;
}

void PageState::WireQuickJSEvent(Widget* target, const std::string& evName,
                                  const std::string& sourceJs) {
    if (!target || !jsRt_) return;
    JSContext* ctx = jsRt_->ctx();

    std::string rewritten = ui::uix::RewriteTemplateExpr(sourceJs);
    JSValue fn = jsRt_->CompileHandlerClosure(rewritten);
    if (JS_IsException(fn)) {
        errors_.push_back("@" + evName + " compile error: " + jsRt_->lastError() +
                          "\n  source: " + sourceJs);
        return;
    }
    jsEventFns_.push_back(fn);

    auto callHandler = [this, ctx, fn](JSValue arg) {
        JSValue argv[1] = { arg };
        JSValue r = JS_Call(ctx, fn, jsState_, 1, argv);
        if (JS_IsException(r)) {
            JSValue exc = JS_GetException(ctx);
            const char* msg = JS_ToCString(ctx, exc);
            std::string s = msg ? msg : "(no message)";
            if (msg) JS_FreeCString(ctx, msg);
            JS_FreeValue(ctx, exc);
            errors_.push_back("event handler threw: " + s);
        }
        JS_FreeValue(ctx, r);
        JS_FreeValue(ctx, arg);
    };

    // MouseEvent → JS object mirroring DOM MouseEvent/WheelEvent. Legacy
    // fields x/y/delta are kept for pages written against the old shape.
    // `target` is captured raw — safe because the hook lives on the widget
    // itself and dies with it. Each invocation builds a fresh object; the
    // closure callee owns it via JS_FreeValue in callHandler.
    auto mouseEventValue = [ctx, target](const ui::MouseEvent& e) -> JSValue {
        JSValue obj = JS_NewObject(ctx);
        // DOM: client* = window coords, offset* = target-local coords.
        JS_SetPropertyStr(ctx, obj, "clientX", JS_NewFloat64(ctx, e.x));
        JS_SetPropertyStr(ctx, obj, "clientY", JS_NewFloat64(ctx, e.y));
        JS_SetPropertyStr(ctx, obj, "offsetX", JS_NewFloat64(ctx, e.x - target->rect.left));
        JS_SetPropertyStr(ctx, obj, "offsetY", JS_NewFloat64(ctx, e.y - target->rect.top));
        JS_SetPropertyStr(ctx, obj, "button",  JS_NewInt32(ctx, e.button));
        JS_SetPropertyStr(ctx, obj, "buttons", JS_NewInt32(ctx, e.buttons));
        JS_SetPropertyStr(ctx, obj, "ctrlKey",  JS_NewBool(ctx, e.ctrl));
        JS_SetPropertyStr(ctx, obj, "shiftKey", JS_NewBool(ctx, e.shift));
        JS_SetPropertyStr(ctx, obj, "altKey",   JS_NewBool(ctx, e.alt));
        // DOM WheelEvent: deltaY positive = scroll down; Win32 delta is the
        // opposite sign, flip here so pages can use the web convention.
        JS_SetPropertyStr(ctx, obj, "deltaY", JS_NewFloat64(ctx, -e.delta));
        JS_SetPropertyStr(ctx, obj, "deltaX", JS_NewFloat64(ctx, e.deltaX));
        // Legacy (pre-DOM-parity) fields.
        JS_SetPropertyStr(ctx, obj, "x",      JS_NewFloat64(ctx, e.x));
        JS_SetPropertyStr(ctx, obj, "y",      JS_NewFloat64(ctx, e.y));
        JS_SetPropertyStr(ctx, obj, "delta",  JS_NewFloat64(ctx, e.delta));
        return obj;
    };

    if (evName == "click") {
        target->onClick = [callHandler]() { callHandler(JS_UNDEFINED); };
    } else if (evName == "mousedown") {
        target->onMouseDownHook = [callHandler, mouseEventValue](const ui::MouseEvent& e) {
            callHandler(mouseEventValue(e));
        };
    } else if (evName == "mousemove") {
        target->onMouseMoveHook = [callHandler, mouseEventValue](const ui::MouseEvent& e) {
            callHandler(mouseEventValue(e));
        };
    } else if (evName == "mouseup") {
        target->onMouseUpHook = [callHandler, mouseEventValue](const ui::MouseEvent& e) {
            callHandler(mouseEventValue(e));
        };
    } else if (evName == "wheel") {
        target->onMouseWheelHook = [callHandler, mouseEventValue](const ui::MouseEvent& e) {
            callHandler(mouseEventValue(e));
        };
    } else if (evName == "dblclick") {
        target->onMouseDblClickHook = [callHandler, mouseEventValue](const ui::MouseEvent& e) {
            callHandler(mouseEventValue(e));
        };
    } else if (evName == "mouseenter") {
        target->onMouseEnterHook = [callHandler, mouseEventValue](const ui::MouseEvent& e) {
            callHandler(mouseEventValue(e));
        };
    } else if (evName == "mouseleave") {
        // Leave carries no cursor position (see onMouseLeaveHook comment).
        target->onMouseLeaveHook = [callHandler]() { callHandler(JS_UNDEFINED); };
    } else if (evName == "contextmenu") {
        target->onContextMenuHook = [callHandler, mouseEventValue](const ui::MouseEvent& e) {
            callHandler(mouseEventValue(e));
        };
    } else if (evName == "dragstart") {
        // $event carries `data` (the payload, defaulting to drag-data) which
        // the handler may reassign — DOM dataTransfer.setData equivalent.
        // Can't go through callHandler: the object must stay alive after the
        // call so the reassigned `data` can be read back.
        target->onDragStartHook = [this, ctx, fn, mouseEventValue](
                const ui::MouseEvent& e, std::wstring& data) {
            JSValue obj = mouseEventValue(e);
            std::string utf8 = ToUtf8(data);
            JS_SetPropertyStr(ctx, obj, "data",
                              JS_NewStringLen(ctx, utf8.data(), utf8.size()));
            JSValue argv[1] = { obj };
            JSValue r = JS_Call(ctx, fn, jsState_, 1, argv);
            if (JS_IsException(r)) {
                JSValue exc = JS_GetException(ctx);
                const char* msg = JS_ToCString(ctx, exc);
                errors_.push_back(std::string("@dragstart handler threw: ") +
                                  (msg ? msg : "(no message)"));
                if (msg) JS_FreeCString(ctx, msg);
                JS_FreeValue(ctx, exc);
            }
            JS_FreeValue(ctx, r);
            JSValue nd = JS_GetPropertyStr(ctx, obj, "data");
            if (JS_IsString(nd)) {
                const char* s = JS_ToCString(ctx, nd);
                if (s) { data = ToWide(s); JS_FreeCString(ctx, s); }
            }
            JS_FreeValue(ctx, nd);
            JS_FreeValue(ctx, obj);
        };
    } else if (evName == "dragend") {
        target->onDragEndHook = [callHandler, ctx](bool dropped) {
            callHandler(JS_NewBool(ctx, dropped));
        };
    } else if (evName == "dragenter") {
        target->dropTarget = true;
        target->onDragEnterHook = [callHandler, mouseEventValue](const ui::MouseEvent& e) {
            callHandler(mouseEventValue(e));
        };
    } else if (evName == "dragover") {
        target->dropTarget = true;
        target->onDragOverHook = [callHandler, mouseEventValue](const ui::MouseEvent& e) {
            callHandler(mouseEventValue(e));
        };
    } else if (evName == "dragleave") {
        target->dropTarget = true;
        target->onDragLeaveHook = [callHandler]() { callHandler(JS_UNDEFINED); };
    } else if (evName == "drop") {
        // Listening for drop implies droppable (no dragover-preventDefault
        // dance) — CanAcceptDrop() already treats onDropHook as acceptance.
        target->onDropHook = [callHandler, mouseEventValue, ctx](
                const std::wstring& data, const ui::MouseEvent& e) {
            JSValue obj = mouseEventValue(e);
            std::string utf8 = ToUtf8(data);
            JS_SetPropertyStr(ctx, obj, "data",
                              JS_NewStringLen(ctx, utf8.data(), utf8.size()));
            callHandler(obj);
        };
    } else if (evName == "submit") {
        target->onSubmitHook = [callHandler]() { callHandler(JS_UNDEFINED); };
    } else if (evName == "focus") {
        target->onFocusHook = [callHandler]() { callHandler(JS_UNDEFINED); };
    } else if (evName == "blur") {
        target->onBlurHook  = [callHandler]() { callHandler(JS_UNDEFINED); };
    } else if (evName == "change" || evName == "input") {
        target->onValueChanged = [callHandler, ctx](bool v) {
            callHandler(JS_NewBool(ctx, v));
        };
        target->onTextChanged = [callHandler, ctx](const std::wstring& v) {
            std::string utf8 = ToUtf8(v);
            callHandler(JS_NewStringLen(ctx, utf8.data(), utf8.size()));
        };
        target->onFloatChanged = [callHandler, ctx](float v) {
            callHandler(JS_NewFloat64(ctx, static_cast<double>(v)));
        };
    } else {
        errors_.push_back("@" + evName + " not supported on QuickJS path");
    }
}

void PageState::WireQuickJSModelWrite(Widget* target, const std::string& propName) {
    if (!target || !jsRt_) return;
    JSContext* ctx = jsRt_->ctx();

    if (dynamic_cast<TextInputWidget*>(target) ||
        dynamic_cast<TextAreaWidget*>(target) ||
        dynamic_cast<ChipsInputWidget*>(target)) {
        target->onTextChanged = [this, ctx, propName](const std::wstring& v) {
            std::string utf8 = ToUtf8(v);
            JS_SetPropertyStr(ctx, jsState_, propName.c_str(),
                              JS_NewStringLen(ctx, utf8.data(), utf8.size()));
        };
    } else if (dynamic_cast<CheckBoxWidget*>(target) ||
               dynamic_cast<ToggleWidget*>(target) ||
               dynamic_cast<RadioButtonWidget*>(target)) {
        target->onValueChanged = [this, ctx, propName](bool v) {
            JS_SetPropertyStr(ctx, jsState_, propName.c_str(),
                              JS_NewBool(ctx, v));
        };
    } else if (dynamic_cast<SliderWidget*>(target) ||
               dynamic_cast<NumberBoxWidget*>(target) ||
               dynamic_cast<ProgressBarWidget*>(target)) {
        target->onFloatChanged = [this, ctx, propName](float v) {
            double dv = static_cast<double>(v);
            JSValue cur = JS_GetPropertyStr(ctx, jsState_, propName.c_str());
            if (JS_IsNumber(cur)) {
                double n; JS_ToFloat64(ctx, &n, cur);
                if (std::floor(n) == n) dv = std::round(dv);
            }
            JS_FreeValue(ctx, cur);
            JS_SetPropertyStr(ctx, jsState_, propName.c_str(),
                              JS_NewFloat64(ctx, dv));
        };
    } else if (auto* combo = dynamic_cast<ComboBoxWidget*>(target)) {
        // ComboBox emits onSelectionChanged (int idx), NOT onFloatChanged —
        // wiring onFloatChanged was a dead branch and v-model on <select>
        // never updated state.
        combo->onSelectionChanged = [this, ctx, propName](int idx) {
            JS_SetPropertyStr(ctx, jsState_, propName.c_str(),
                              JS_NewInt32(ctx, idx));
        };
    }
}

}  // namespace ui::page
