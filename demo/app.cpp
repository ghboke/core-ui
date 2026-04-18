/*
 * Core UI Demo — all UI defined in app.ui, this file only handles events.
 */
#include <ui_core.h>
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <typeinfo>
#include <thread>

#include "../src/ui/markup/markup.h"
#include "../src/ui/controls.h"
#include "../src/ui/ui_context.h"
#include "../src/ui/ui_window.h"

static ui::UiMarkup g_layout;
static UiWindow g_win = UI_INVALID;
static volatile bool g_shutdown = false;

static const char* g_navIds[] = {
    "nav_home", "nav_button", "nav_check", "nav_input",
    "nav_range", "nav_status", "nav_layout", "nav_crop", "nav_settings"
};
static constexpr int kPageCount = 9;

static void switchPage(int page) {
    auto* stack = g_layout.FindAs<ui::StackWidget>("pages");
    if (!stack) return;
    for (int i = 0; i < kPageCount; i++) {
        auto* item = g_layout.FindAs<ui::NavItemWidget>(g_navIds[i]);
        if (item) item->SetSelected(i == page);
    }
    if (stack->ActiveIndex() == page) { if (g_win) ui_window_invalidate(g_win); return; }
    stack->SetActiveIndex(page);
    stack->DoLayout();
    if (g_win) ui_window_invalidate(g_win);
}

static void updateCropInfo() {
    auto* iv = g_layout.FindAs<ui::ImageViewWidget>("cropImage");
    if (!iv || !iv->IsCropMode()) return;
    float cx, cy, cw, ch;
    iv->GetCropRect(cx, cy, cw, ch);
    wchar_t buf[128];
    swprintf(buf, 128, L"Crop: %d x %d  at (%d, %d)", (int)cw, (int)ch, (int)cx, (int)cy);
    g_layout.SetText("crop_info", buf);
    if (g_win) ui_window_invalidate(g_win);
}

// ================================================================
// Debug pipe command dispatch
// ================================================================

namespace {

// 把 layout 里的 id 翻译成 UiWidget 句柄。
// 注意：用 window 的 root（ui_window_set_root 设置的那棵），因为 g_layout.Root()
// 可能在 OnResize / media query 触发后被重建，和实际挂载到窗口的 Widget* 不同。
UiWidget widgetByIdHandle(const std::string& id) {
    auto& ctx = ui::GetContext();
    auto* wi = ctx.GetWindow(g_win);
    if (!wi) return 0;
    auto root = wi->Root();
    if (!root) return 0;
    // 找到窗口 root 在 handle table 里已有的 handle；若没有就新插入。
    uint64_t rh = ctx.handles.FindHandle(root.get());
    if (!rh) rh = ctx.handles.Insert(root);
    return ui_widget_find_by_id(rh, id.c_str());
}

std::string trimLead(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    return s.substr(i);
}

// 按空格切成最多 N 段
std::vector<std::string> splitWs(const std::string& s, int maxParts = 8) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size() && (int)out.size() < maxParts) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
        if (i >= s.size()) break;
        if ((int)out.size() == maxParts - 1) {
            out.push_back(s.substr(i));
            return out;
        }
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t') j++;
        out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

std::string okJson() { return "{\"ok\":true}"; }

std::string okFmt(const char* fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return buf;
}

std::string errJson(const std::string& msg) {
    std::string out = "{\"error\":\"";
    for (char c : msg) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += "\"}";
    return out;
}

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

// 把 VK 字符串解成 virtual-key 码（"enter"/"esc"/"tab"/... 或原始十进制）。
int parseVk(const std::string& s) {
    std::string lo = s;
    for (auto& c : lo) c = (char)tolower((unsigned char)c);
    if (lo == "enter" || lo == "return") return VK_RETURN;
    if (lo == "esc" || lo == "escape")   return VK_ESCAPE;
    if (lo == "tab")       return VK_TAB;
    if (lo == "space")     return VK_SPACE;
    if (lo == "back" || lo == "backspace") return VK_BACK;
    if (lo == "del" || lo == "delete")     return VK_DELETE;
    if (lo == "left")  return VK_LEFT;
    if (lo == "right") return VK_RIGHT;
    if (lo == "up")    return VK_UP;
    if (lo == "down")  return VK_DOWN;
    if (lo == "home")  return VK_HOME;
    if (lo == "end")   return VK_END;
    return std::atoi(s.c_str());
}

int parseBoolArg(const std::string& s, int deflt) {
    if (s.empty()) return deflt;
    std::string lo = s;
    for (auto& c : lo) c = (char)tolower((unsigned char)c);
    if (lo == "1" || lo == "on"  || lo == "true"  || lo == "show") return 1;
    if (lo == "0" || lo == "off" || lo == "false" || lo == "hide") return 0;
    if (lo == "toggle") return -1;
    return deflt;
}

// 统一的命令分发：cmd 是首 token，rest 是其后的整串参数。
std::string dispatchCommand(const std::string& cmd, const std::string& rest) {
    auto args = splitWs(rest, 6);
    auto arg = [&](int i) -> std::string {
        return (i < (int)args.size()) ? args[i] : std::string();
    };

    // --- 基础 inspector ---
    if (cmd == "tree") {
        char* json = ui_debug_dump_tree(g_win);
        if (!json) return "{}";
        std::string r = json; ui_debug_free(json);
        return r;
    }
    if (cmd == "widget") {
        UiWidget w = widgetByIdHandle(arg(0));
        if (!w) return errJson("not found: " + arg(0));
        char* json = ui_debug_dump_widget(w);
        if (!json) return "{}";
        std::string r = json; ui_debug_free(json);
        return r;
    }
    if (cmd == "highlight") {
        ui_debug_highlight(g_win, arg(0).empty() ? nullptr : arg(0).c_str());
        ui_window_invalidate(g_win);
        return okJson();
    }
    if (cmd == "screenshot") {
        if (arg(0).empty()) return errJson("usage: screenshot <path>");
        std::wstring wp = utf8ToWide(arg(0));
        int r = ui_debug_screenshot(g_win, wp.c_str());
        return r == 0 ? okJson() : errJson("screenshot failed");
    }
    if (cmd == "invalidate") {
        ui_window_invalidate(g_win);
        return okJson();
    }
    if (cmd == "pump") {
        int n = ui_debug_pump();
        return okFmt("{\"ok\":true,\"processed\":%d}", n);
    }

    // --- 页面 / 滚动 / flyout（保留既有，增强参数） ---
    if (cmd == "nav") {
        int page = arg(0).empty() ? -1 : std::atoi(arg(0).c_str());
        if (page < 0 || page >= kPageCount) return errJson("usage: nav <0-8>");
        switchPage(page);
        return okFmt("{\"ok\":true,\"page\":%d}", page);
    }
    if (cmd == "scroll") {
        std::string sid = arg(0);
        float y = arg(1).empty() ? -1.0f : (float)std::atof(arg(1).c_str());
        auto* stack = g_layout.FindAs<ui::StackWidget>("pages");
        ui::ScrollViewWidget* sv = nullptr;
        if (!sid.empty()) sv = g_layout.FindAs<ui::ScrollViewWidget>(sid);
        if (!sv && stack) {
            int ai = stack->ActiveIndex();
            auto& kids = stack->Children();
            if (ai >= 0 && ai < (int)kids.size())
                sv = dynamic_cast<ui::ScrollViewWidget*>(kids[ai].get());
        }
        if (!sv) return errJson("ScrollView not found");
        if (y >= 0) { sv->SetScrollY(y); sv->DoLayout(); ui_window_invalidate(g_win); }
        return okFmt("{\"ok\":true,\"scrollY\":%.1f}", sv->ScrollY());
    }
    if (cmd == "flyout") {
        auto* flyout = g_layout.FindAs<ui::FlyoutWidget>("demoFlyout");
        auto* btn = g_layout.FindById("flyoutBtn");
        if (!flyout || !btn) return errJson("flyout or anchor not found");
        int mode = parseBoolArg(arg(0), -1);
        bool want = (mode == -1) ? !flyout->IsOpen() : (mode == 1);
        if (want) flyout->Show(btn);
        else flyout->Hide();
        ui_window_invalidate(g_win);
        return okFmt("{\"ok\":true,\"open\":%s}", flyout->IsOpen() ? "true" : "false");
    }

    // --- 通用鼠标/键盘模拟 ---
    if (cmd == "click") {
        UiWidget w = widgetByIdHandle(arg(0));
        if (!w) return errJson("widget not found: " + arg(0));
        return ui_debug_click(g_win, w) == 0 ? okJson() : errJson("click failed");
    }
    if (cmd == "click_at") {
        if (arg(1).empty()) return errJson("usage: click_at <x> <y>");
        float x = (float)std::atof(arg(0).c_str());
        float y = (float)std::atof(arg(1).c_str());
        return ui_debug_click_at(g_win, x, y) == 0 ? okJson() : errJson("click_at failed");
    }
    if (cmd == "dbl_click") {
        UiWidget w = widgetByIdHandle(arg(0));
        if (!w) return errJson("widget not found");
        return ui_debug_double_click(g_win, w) == 0 ? okJson() : errJson("dbl_click failed");
    }
    if (cmd == "rclick") {
        UiWidget w = widgetByIdHandle(arg(0));
        if (!w) return errJson("widget not found");
        return ui_debug_right_click(g_win, w) == 0 ? okJson() : errJson("rclick failed");
    }
    if (cmd == "rclick_at") {
        if (arg(1).empty()) return errJson("usage: rclick_at <x> <y>");
        float x = (float)std::atof(arg(0).c_str());
        float y = (float)std::atof(arg(1).c_str());
        int r = ui_debug_right_click_at(g_win, x, y);
        if (r != 0) return errJson("rclick_at failed");
        int open = ui_debug_menu_is_open(g_win);
        return okFmt("{\"ok\":true,\"menuOpen\":%s}", open ? "true" : "false");
    }
    if (cmd == "hover") {
        UiWidget w = widgetByIdHandle(arg(0));
        if (!w) return errJson("widget not found");
        return ui_debug_hover(g_win, w) == 0 ? okJson() : errJson("hover failed");
    }
    if (cmd == "move") {
        if (arg(1).empty()) return errJson("usage: move <x> <y>");
        float x = (float)std::atof(arg(0).c_str());
        float y = (float)std::atof(arg(1).c_str());
        return ui_debug_mouse_move(g_win, x, y) == 0 ? okJson() : errJson("move failed");
    }
    if (cmd == "drag") {
        if (arg(2).empty()) return errJson("usage: drag <id> <dx> <dy>");
        UiWidget w = widgetByIdHandle(arg(0));
        if (!w) return errJson("widget not found");
        float dx = (float)std::atof(arg(1).c_str());
        float dy = (float)std::atof(arg(2).c_str());
        return ui_debug_drag(g_win, w, dx, dy) == 0 ? okJson() : errJson("drag failed");
    }
    if (cmd == "drag_to") {
        if (arg(3).empty()) return errJson("usage: drag_to <x1> <y1> <x2> <y2>");
        float x1 = (float)std::atof(arg(0).c_str());
        float y1 = (float)std::atof(arg(1).c_str());
        float x2 = (float)std::atof(arg(2).c_str());
        float y2 = (float)std::atof(arg(3).c_str());
        return ui_debug_drag_to(g_win, x1, y1, x2, y2) == 0 ? okJson() : errJson("drag_to failed");
    }
    if (cmd == "wheel") {
        if (arg(1).empty()) return errJson("usage: wheel <id> <delta>");
        UiWidget w = widgetByIdHandle(arg(0));
        if (!w) return errJson("widget not found");
        float d = (float)std::atof(arg(1).c_str());
        return ui_debug_wheel(g_win, w, d) == 0 ? okJson() : errJson("wheel failed");
    }
    if (cmd == "wheel_at") {
        if (arg(2).empty()) return errJson("usage: wheel_at <x> <y> <delta>");
        float x = (float)std::atof(arg(0).c_str());
        float y = (float)std::atof(arg(1).c_str());
        float d = (float)std::atof(arg(2).c_str());
        return ui_debug_wheel_at(g_win, x, y, d) == 0 ? okJson() : errJson("wheel_at failed");
    }
    if (cmd == "focus") {
        UiWidget w = widgetByIdHandle(arg(0));
        if (!w) return errJson("widget not found");
        return ui_debug_focus(g_win, w) == 0 ? okJson() : errJson("focus failed");
    }
    if (cmd == "blur") {
        return ui_debug_blur(g_win) == 0 ? okJson() : errJson("blur failed");
    }
    if (cmd == "key") {
        if (arg(0).empty()) return errJson("usage: key <vk-or-name>");
        int vk = parseVk(arg(0));
        return ui_debug_key(g_win, vk) == 0 ? okJson() : errJson("key failed");
    }
    if (cmd == "type") {
        if (rest.empty()) return errJson("usage: type <text>");
        std::wstring w = utf8ToWide(trimLead(rest));
        return ui_debug_type_text(g_win, w.c_str()) == 0 ? okJson() : errJson("type failed");
    }

    // --- 高层控件操作 ---
    if (cmd == "check") {
        UiWidget w = widgetByIdHandle(arg(0));
        if (!w) return errJson("widget not found");
        int v = parseBoolArg(arg(1), -1);
        int r = (v == -1) ? ui_debug_checkbox_toggle(g_win, w)
                          : ui_debug_checkbox_set(g_win, w, v);
        return r == 0 ? okJson() : errJson("check failed");
    }
    if (cmd == "toggle") {
        UiWidget w = widgetByIdHandle(arg(0));
        if (!w) return errJson("widget not found");
        int v = parseBoolArg(arg(1), -1);
        int r = 0;
        if (v == -1) {
            auto* tg = dynamic_cast<ui::ToggleWidget*>(ui::GetContext().handles.LookupRaw(w));
            if (!tg) return errJson("not a Toggle");
            r = ui_debug_toggle_set(g_win, w, tg->On() ? 0 : 1);
        } else {
            r = ui_debug_toggle_set(g_win, w, v);
        }
        return r == 0 ? okJson() : errJson("toggle failed");
    }
    if (cmd == "radio") {
        UiWidget w = widgetByIdHandle(arg(0));
        if (!w) return errJson("widget not found");
        return ui_debug_radio_select(g_win, w) == 0 ? okJson() : errJson("radio failed");
    }
    if (cmd == "combo") {
        UiWidget w = widgetByIdHandle(arg(0));
        if (!w) return errJson("widget not found");
        if (arg(1).empty()) return errJson("usage: combo <id> <index>");
        int idx = std::atoi(arg(1).c_str());
        return ui_debug_combo_select(g_win, w, idx) == 0 ? okJson() : errJson("combo failed");
    }
    if (cmd == "combo_open") {
        UiWidget w = widgetByIdHandle(arg(0));
        if (!w) return errJson("widget not found");
        return ui_debug_combo_open(w) == 0 ? okJson() : errJson("combo_open failed");
    }
    if (cmd == "slider") {
        UiWidget w = widgetByIdHandle(arg(0));
        if (!w) return errJson("widget not found");
        if (arg(1).empty()) return errJson("usage: slider <id> <value>");
        float v = (float)std::atof(arg(1).c_str());
        return ui_debug_slider_set(g_win, w, v) == 0 ? okJson() : errJson("slider failed");
    }
    if (cmd == "number") {
        UiWidget w = widgetByIdHandle(arg(0));
        if (!w) return errJson("widget not found");
        if (arg(1).empty()) return errJson("usage: number <id> <value>");
        float v = (float)std::atof(arg(1).c_str());
        return ui_debug_number_set(g_win, w, v) == 0 ? okJson() : errJson("number failed");
    }
    if (cmd == "tab") {
        UiWidget w = widgetByIdHandle(arg(0));
        if (!w) return errJson("widget not found");
        if (arg(1).empty()) return errJson("usage: tab <id> <index>");
        int idx = std::atoi(arg(1).c_str());
        int r = ui_debug_tab_set(w, idx);
        if (r == 0) ui_window_invalidate(g_win);
        return r == 0 ? okJson() : errJson("tab failed");
    }
    if (cmd == "expander") {
        UiWidget w = widgetByIdHandle(arg(0));
        if (!w) return errJson("widget not found");
        int v = parseBoolArg(arg(1), -1);
        int want;
        if (v == -1) {
            auto* ex = dynamic_cast<ui::ExpanderWidget*>(ui::GetContext().handles.LookupRaw(w));
            if (!ex) return errJson("not an Expander");
            want = ex->IsExpanded() ? 0 : 1;
        } else want = v;
        int r = ui_debug_expander_set(w, want);
        if (r == 0) ui_window_invalidate(g_win);
        return r == 0 ? okJson() : errJson("expander failed");
    }
    if (cmd == "splitview") {
        UiWidget w = widgetByIdHandle(arg(0));
        if (!w) return errJson("widget not found");
        int v = parseBoolArg(arg(1), -1);
        int want;
        if (v == -1) {
            auto* sv = dynamic_cast<ui::SplitViewWidget*>(ui::GetContext().handles.LookupRaw(w));
            if (!sv) return errJson("not a SplitView");
            want = sv->IsPaneOpen() ? 0 : 1;
        } else want = v;
        int r = ui_debug_splitview_set(w, want);
        if (r == 0) ui_window_invalidate(g_win);
        return r == 0 ? okJson() : errJson("splitview failed");
    }
    if (cmd == "input" || cmd == "textarea" || cmd == "set_text") {
        UiWidget w = widgetByIdHandle(arg(0));
        if (!w) return errJson("widget not found");
        std::string txt;
        size_t sp = rest.find(' ');
        if (sp != std::string::npos) txt = rest.substr(sp + 1);
        std::wstring wt = utf8ToWide(txt);
        int r = ui_debug_text_set(w, wt.c_str());
        if (r == 0) ui_window_invalidate(g_win);
        return r == 0 ? okJson() : errJson("text set failed");
    }

    // --- ImageView specific ---
    if (cmd == "zoom") {
        UiWidget w = widgetByIdHandle(arg(0));
        if (!w) return errJson("widget not found");
        if (arg(1).empty()) return errJson("usage: zoom <id> <value>");
        float z = (float)std::atof(arg(1).c_str());
        ui_image_set_zoom(w, z);
        ui_window_invalidate(g_win);
        return okJson();
    }
    if (cmd == "pan") {
        UiWidget w = widgetByIdHandle(arg(0));
        if (!w) return errJson("widget not found");
        if (arg(2).empty()) return errJson("usage: pan <id> <x> <y>");
        ui_image_set_pan(w, (float)std::atof(arg(1).c_str()),
                             (float)std::atof(arg(2).c_str()));
        ui_window_invalidate(g_win);
        return okJson();
    }
    if (cmd == "rotate") {
        UiWidget w = widgetByIdHandle(arg(0));
        if (!w) return errJson("widget not found");
        if (arg(1).empty()) return errJson("usage: rotate <id> <angle>");
        ui_image_set_rotation(w, std::atoi(arg(1).c_str()));
        ui_window_invalidate(g_win);
        return okJson();
    }

    // --- Context menu（对当前已打开 active menu） ---
    if (cmd == "menu_is_open") {
        return okFmt("{\"ok\":true,\"open\":%s}",
                     ui_debug_menu_is_open(g_win) ? "true" : "false");
    }
    if (cmd == "menu_count") {
        int n = ui_debug_menu_item_count(g_win);
        if (n < 0) return errJson("no menu open");
        return okFmt("{\"ok\":true,\"count\":%d}", n);
    }
    if (cmd == "menu_click") {
        if (arg(0).empty()) return errJson("usage: menu_click <index>");
        int idx = std::atoi(arg(0).c_str());
        int r = ui_debug_menu_click_index(g_win, idx);
        if (r == 0) ui_debug_pump();
        return r == 0 ? okJson() : errJson("menu_click failed (no menu / bad index / disabled)");
    }
    if (cmd == "menu_click_id") {
        if (arg(0).empty()) return errJson("usage: menu_click_id <item_id>");
        int id = std::atoi(arg(0).c_str());
        int r = ui_debug_menu_click_id(g_win, id);
        if (r == 0) ui_debug_pump();
        return r == 0 ? okJson() : errJson("menu_click_id failed");
    }
    if (cmd == "menu_close") {
        return ui_debug_menu_close(g_win) == 0 ? okJson() : errJson("menu_close failed");
    }
    // path 形式：用 "/" 分隔索引，如 menu_click_path 2/1
    auto parseMenuPath = [&](const std::string& s, std::vector<int>& out) -> bool {
        out.clear();
        if (s.empty()) return false;
        size_t i = 0;
        while (i < s.size()) {
            size_t j = s.find('/', i);
            std::string tok = s.substr(i, (j == std::string::npos) ? std::string::npos : j - i);
            if (tok.empty()) return false;
            out.push_back(std::atoi(tok.c_str()));
            if (j == std::string::npos) break;
            i = j + 1;
        }
        return !out.empty();
    };
    if (cmd == "menu_click_path") {
        std::vector<int> path;
        if (!parseMenuPath(arg(0), path)) return errJson("usage: menu_click_path i0/i1/...");
        int r = ui_debug_menu_click_path(g_win, path.data(), (int)path.size());
        if (r == 0) ui_debug_pump();
        return r == 0 ? okJson() : errJson("menu_click_path failed");
    }
    if (cmd == "menu_count_at") {
        std::vector<int> path;
        if (!arg(0).empty() && !parseMenuPath(arg(0), path)) return errJson("usage: menu_count_at [i0/i1/...]");
        int n = ui_debug_menu_item_count_at(g_win, path.empty() ? nullptr : path.data(), (int)path.size());
        if (n < 0) return errJson("invalid path or no menu");
        return okFmt("{\"ok\":true,\"count\":%d}", n);
    }
    if (cmd == "menu_has_sub") {
        std::vector<int> path;
        if (!parseMenuPath(arg(0), path)) return errJson("usage: menu_has_sub i0/i1/...");
        int v = ui_debug_menu_has_submenu_at(g_win, path.data(), (int)path.size());
        return okFmt("{\"ok\":true,\"hasSubmenu\":%s}", v ? "true" : "false");
    }
    if (cmd == "menu_autoclose") {
        int v = parseBoolArg(arg(0), -1);
        if (v < 0) return errJson("usage: menu_autoclose 0|1");
        ui_debug_set_menu_autoclose(v);
        return okJson();
    }
    if (cmd == "menu_id_at") {
        std::vector<int> path;
        if (!parseMenuPath(arg(0), path)) return errJson("usage: menu_id_at i0/i1/...");
        int id = ui_debug_menu_item_id_at(g_win, path.data(), (int)path.size());
        if (id < 0) return errJson("invalid path / separator");
        return okFmt("{\"ok\":true,\"itemId\":%d}", id);
    }

    // --- Dialog ---
    if (cmd == "dialog_confirm") {
        return ui_debug_dialog_confirm(g_win) == 0 ? okJson() : errJson("no active dialog");
    }
    if (cmd == "dialog_cancel") {
        return ui_debug_dialog_cancel(g_win) == 0 ? okJson() : errJson("no active dialog");
    }

    // --- HWND 通道 ---
    if (cmd == "post_click") {
        if (arg(1).empty()) return errJson("usage: post_click <x> <y>");
        float x = (float)std::atof(arg(0).c_str());
        float y = (float)std::atof(arg(1).c_str());
        return ui_debug_post_click(g_win, x, y) == 0 ? okJson() : errJson("post_click failed");
    }
    if (cmd == "post_rclick") {
        if (arg(1).empty()) return errJson("usage: post_rclick <x> <y>");
        float x = (float)std::atof(arg(0).c_str());
        float y = (float)std::atof(arg(1).c_str());
        return ui_debug_post_right_click(g_win, x, y) == 0 ? okJson() : errJson("post_rclick failed");
    }
    if (cmd == "post_key") {
        if (arg(0).empty()) return errJson("usage: post_key <vk-or-name>");
        int vk = parseVk(arg(0));
        return ui_debug_post_key(g_win, vk) == 0 ? okJson() : errJson("post_key failed");
    }
    if (cmd == "post_char") {
        if (arg(0).empty()) return errJson("usage: post_char <codepoint>");
        unsigned int ch = (unsigned int)std::atoi(arg(0).c_str());
        return ui_debug_post_char(g_win, ch) == 0 ? okJson() : errJson("post_char failed");
    }

    if (cmd == "help" || cmd == "?") {
        return
        "{\"commands\":["
        "\"tree\",\"widget <id>\",\"highlight <id>\",\"screenshot <path>\",\"invalidate\",\"pump\","
        "\"nav <0-8>\",\"scroll [id] [y]\",\"flyout [show|hide|toggle]\","
        "\"click <id>\",\"click_at <x> <y>\",\"dbl_click <id>\",\"rclick <id>\",\"rclick_at <x> <y>\","
        "\"hover <id>\",\"move <x> <y>\",\"drag <id> <dx> <dy>\",\"drag_to <x1> <y1> <x2> <y2>\","
        "\"wheel <id> <delta>\",\"wheel_at <x> <y> <delta>\","
        "\"focus <id>\",\"blur\",\"key <vk|name>\",\"type <text>\","
        "\"check <id> [0|1|toggle]\",\"toggle <id> [0|1|toggle]\",\"radio <id>\","
        "\"combo <id> <idx>\",\"combo_open <id>\",\"slider <id> <v>\",\"number <id> <v>\","
        "\"tab <id> <idx>\",\"expander <id> [0|1|toggle]\",\"splitview <id> [0|1|toggle]\","
        "\"input <id> <text>\",\"textarea <id> <text>\",\"set_text <id> <text>\","
        "\"zoom <id> <v>\",\"pan <id> <x> <y>\",\"rotate <id> <deg>\","
        "\"menu_is_open\",\"menu_count\",\"menu_click <idx>\",\"menu_click_id <id>\",\"menu_close\","
        "\"dialog_confirm\",\"dialog_cancel\","
        "\"post_click <x> <y>\",\"post_rclick <x> <y>\",\"post_key <vk|name>\",\"post_char <cp>\""
        "]}";
    }

    return errJson("unknown command: " + cmd + "  (send 'help' for list)");
}

} // anonymous namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    ui_init();

    // demo 本身就挂着 \\.\pipe\ui_core_debug，自动化脚本会把前台
    // 抢过去。关掉"前台变化即关菜单"的行为，以便脚本能操作已打开的菜单。
    ui_debug_set_menu_autoclose(0);

    // Toggle pane
    g_layout.SetHandler("onTogglePane", std::function<void()>([]() {
        auto* sv = g_layout.FindAs<ui::SplitViewWidget>("mainSplit");
        if (sv) { sv->TogglePane(); if (g_win) ui_window_invalidate(g_win); }
    }));

    // Navigation (9 pages)
    for (int i = 0; i < kPageCount; i++) {
        char name[16]; snprintf(name, sizeof(name), "onNav%d", i);
        g_layout.SetHandler(name, std::function<void()>([i](){ switchPage(i); }));
    }

    // Theme toggle
    g_layout.SetHandler("onDarkMode", std::function<void(bool)>([](bool on) {
        ui_theme_set_mode(on ? UI_THEME_DARK : UI_THEME_LIGHT);
    }));

    // Flyout demo
    g_layout.SetHandler("onDismissFlyout", std::function<void()>([]() {
        auto* flyout = g_layout.FindAs<ui::FlyoutWidget>("demoFlyout");
        if (flyout && flyout->IsOpen()) {
            flyout->Hide();
            if (g_win) ui_window_invalidate(g_win);
        }
    }));
    g_layout.SetHandler("onShowFlyout", std::function<void()>([]() {
        auto* flyout = g_layout.FindAs<ui::FlyoutWidget>("demoFlyout");
        auto* btn = g_layout.FindById("flyoutBtn");
        if (flyout && btn) {
            if (flyout->IsOpen()) flyout->Hide();
            else flyout->Show(btn);
            if (g_win) ui_window_invalidate(g_win);
        }
    }));

    // Language switch
    g_layout.SetHandler("onLanguage", std::function<void(int)>([](int index) {
        const wchar_t* langFiles[] = {L"lang/zh-CN.lang", L"lang/en.lang"};
        const wchar_t* prefixes[] = {L"../demo/lang/", L"../../demo/lang/"};
        const wchar_t* names[] = {L"zh-CN.lang", L"en.lang"};

        bool loaded = g_layout.LoadLanguage(langFiles[index]);
        if (!loaded) {
            for (auto& p : prefixes) {
                std::wstring path = std::wstring(p) + names[index];
                if (g_layout.LoadLanguage(path)) { loaded = true; break; }
            }
        }
        if (loaded) {
            g_layout.ApplyLanguage();
            if (g_win) ui_window_invalidate(g_win);
        }
    }));

    // Crop toggle
    g_layout.SetHandler("onCropToggle", std::function<void()>([]() {
        auto* iv = g_layout.FindAs<ui::ImageViewWidget>("cropImage");
        if (!iv || !iv->HasImage()) return;
        iv->SetCropMode(!iv->IsCropMode());
        if (iv->IsCropMode()) {
            iv->onCropChanged = [](float x, float y, float w, float h) { updateCropInfo(); };
            updateCropInfo();
        } else {
            g_layout.SetText("crop_info", L"Crop mode off");
        }
        if (g_win) ui_window_invalidate(g_win);
    }));

    // Crop aspect ratio
    g_layout.SetHandler("onCropRatio", std::function<void(int)>([](int index) {
        auto* iv = g_layout.FindAs<ui::ImageViewWidget>("cropImage");
        if (!iv) return;
        float ratios[] = {0, 1.0f, 4.0f/3.0f, 16.0f/9.0f, 3.0f/2.0f};
        iv->SetCropAspectRatio(ratios[index]);
        if (iv->IsCropMode()) { iv->ResetCrop(); updateCropInfo(); }
        if (g_win) ui_window_invalidate(g_win);
    }));

    // Load .ui file
    bool loaded = g_layout.LoadFile(L"app.ui");
    if (!loaded) loaded = g_layout.LoadFile(L"../demo/app.ui");
    if (!loaded) loaded = g_layout.LoadFile(L"../../demo/app.ui");
    if (!loaded) {
        MessageBoxA(nullptr, g_layout.LastError().c_str(), "Load Error", MB_ICONERROR);
        ui_shutdown();
        return 1;
    }

    // Load default language (Chinese)
    if (!g_layout.LoadLanguage(L"lang/zh-CN.lang"))
        if (!g_layout.LoadLanguage(L"../demo/lang/zh-CN.lang"))
            g_layout.LoadLanguage(L"../../demo/lang/zh-CN.lang");
    g_layout.ApplyLanguage();

    // Create window
    auto& wh = g_layout.Window();
    UiWindowConfig cfg = {0};
    cfg.width = wh.width > 0 ? wh.width : 1060;
    cfg.height = wh.height > 0 ? wh.height : 700;
    cfg.resizable = wh.resizable >= 0 ? wh.resizable : 1;
    cfg.accept_files = 1;
    std::wstring winTitle = L"Core UI Demo";
    if (!wh.title.empty()) winTitle.assign(wh.title.begin(), wh.title.end());
    cfg.title = winTitle.c_str();

    g_win = ui_window_create(&cfg);
    if (g_win == UI_INVALID) { ui_shutdown(); return 1; }

    // File drop → load into crop ImageView
    // 右键弹出一个带 submenu 的上下文菜单（给调试 / 自动化测试用）
    //
    //  Copy (id=1001)
    //  Paste (id=1002)
    //  Paste Special  → Plain    (id=2001)
    //                   Match Style (id=2002, disabled)
    //  ────
    //  Refresh (id=1003)
    //  About (id=1004, disabled)
    ui_window_on_right_click(g_win, [](UiWindow win, float x, float y, void*) {
        UiMenu root = ui_menu_create();
        ui_menu_add_item(root, 1001, L"Copy");
        ui_menu_add_item(root, 1002, L"Paste");
        UiMenu special = ui_menu_create();
        ui_menu_add_item(special, 2001, L"Paste as Plain");
        ui_menu_add_item(special, 2002, L"Paste and Match Style");
        ui_menu_set_enabled(special, 2002, 0);
        ui_menu_add_submenu(root, L"Paste Special", special);
        ui_menu_add_separator(root);
        ui_menu_add_item(root, 1003, L"Refresh");
        ui_menu_add_item(root, 1004, L"About");
        ui_menu_set_enabled(root, 1004, 0);
        ui_menu_show(win, root, x, y);
    }, nullptr);

    ui_window_on_menu(g_win, [](UiWindow win, int itemId, void*) {
        wchar_t buf[64];
        swprintf(buf, 64, L"Menu clicked: %d", itemId);
        ui_toast_ex(win, buf, 1500, 0, 1);
    }, nullptr);

    ui_window_on_drop(g_win, [](UiWindow win, const wchar_t* path, void* ud) {
        auto* iv = g_layout.FindAs<ui::ImageViewWidget>("cropImage");
        if (!iv) return;
        auto& ctx = ui::GetContext();
        auto* w = ctx.GetWindow(win);
        if (!w) return;
        iv->LoadFromFile(path, w->GetRenderer());
        iv->FitToView();
        iv->SetCropMode(false);
        g_layout.SetText("crop_info", std::wstring(L"Loaded: ") + path);
        switchPage(7);  // switch to crop page
        ui_window_invalidate(win);
    }, nullptr);

    // Debug pipe: \\.\pipe\ui_core_debug
    // Send "tree" → get widget tree JSON
    // Send "widget <id>" → get single widget info
    // Send "highlight <id>" → highlight widget
    static HANDLE g_pipe = INVALID_HANDLE_VALUE;
    static HANDLE g_pipeEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    std::thread([&]() {
        while (!g_shutdown) {
            HANDLE pipe = CreateNamedPipeA("\\\\.\\pipe\\ui_core_debug",
                PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                1, 65536, 65536, 0, NULL);
            if (pipe == INVALID_HANDLE_VALUE) break;
            g_pipe = pipe;

            // Overlapped ConnectNamedPipe so we can cancel on shutdown
            OVERLAPPED ov = {}; ov.hEvent = g_pipeEvent;
            ResetEvent(g_pipeEvent);
            BOOL connected = ConnectNamedPipe(pipe, &ov);
            if (!connected) {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING) {
                    WaitForSingleObject(g_pipeEvent, INFINITE);
                    if (g_shutdown) { CancelIo(pipe); CloseHandle(pipe); break; }
                } else if (err != ERROR_PIPE_CONNECTED) {
                    CloseHandle(pipe); continue;
                }
            }
            if (g_shutdown) { CloseHandle(pipe); break; }

            char buf[4096] = {};
            DWORD bytesRead = 0;
            // Synchronous read (client already connected)
            if (ReadFile(pipe, buf, sizeof(buf)-1, &bytesRead, NULL) && bytesRead > 0) {
                if (g_shutdown) { CloseHandle(pipe); break; }
                buf[bytesRead] = 0;
                // Strip trailing newline/CR
                while (bytesRead > 0 && (buf[bytesRead-1] == '\n' || buf[bytesRead-1] == '\r')) {
                    buf[--bytesRead] = 0;
                }
                std::string line(buf);
                // Split into head + rest
                size_t sp = line.find_first_of(" \t");
                std::string head = (sp == std::string::npos) ? line : line.substr(0, sp);
                std::string rest = (sp == std::string::npos) ? "" : line.substr(sp + 1);
                // Marshal the actual dispatch onto the UI thread — 命令内部会
                // 直接 mutate widget 状态 / 发送 Sim* 事件，必须在 UI 线程上跑。
                struct DispatchReq { const std::string* head; const std::string* rest; std::string* resp; };
                std::string response;
                DispatchReq req{&head, &rest, &response};
                ui_window_invoke_sync(g_win, [](void* ud) {
                    auto* r = static_cast<DispatchReq*>(ud);
                    *r->resp = dispatchCommand(*r->head, *r->rest);
                }, &req);

                DWORD written;
                WriteFile(pipe, response.c_str(), (DWORD)response.size(), &written, NULL);
                FlushFileBuffers(pipe);
            }
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
        }
        g_pipe = INVALID_HANDLE_VALUE;
    }).detach();

    auto& ctx = ui::GetContext();
    ui_window_set_root(g_win, ctx.handles.Insert(g_layout.Root()));
    ui_window_show(g_win);

    int ret = ui_run();

    // Signal pipe thread to stop and unblock ConnectNamedPipe
    g_shutdown = true;
    SetEvent(g_pipeEvent);
    if (g_pipe != INVALID_HANDLE_VALUE) {
        CancelIoEx(g_pipe, NULL);
    }
    // Brief wait for thread to exit before destroying globals
    Sleep(100);
    CloseHandle(g_pipeEvent);

    ui_shutdown();
    return ret;
}
