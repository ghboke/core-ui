// ui-demo-uix — single-file Vue 3 SFC demo.
// Default locale is zh; the i18n / settings pages let the user switch.
//
// Methods{} can't call the C ui_page_set_locale directly, so the .uix
// writes its desired locale to state.requestedLocale and a 200ms
// SetTimer here forwards changes to set_locale. The reactive bindings
// then auto-refire and every $t() call returns the new translation.

#include <ui_core.h>
#include <windows.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <vector>

#include "app_uix.embed.h"
#include "lang_zh.embed.h"
#include "lang_en.embed.h"

static UiPage g_page = 0;
static UiWindow g_win = 0;
static UiWidget g_overlayGh = 0;
static UiMenu g_eventsMenu = 0;
static char   g_curLocale[16] = "zh";
static int    g_curDark = 0;
static int    g_menuTransparent = 1;
static int    g_menuBlur = 22;
static int    g_appliedMenuTransparent = -1;
static int    g_appliedMenuBlur = -1;
static int    g_appliedMenuDark = -1;
static FILE*  g_traceFile = nullptr;

static void JsonEsc(FILE* f, const char* s) {
    if (!s) return;
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        if (*p == '\\' || *p == '"') std::fprintf(f, "\\%c", *p);
        else if (*p == '\n') std::fputs("\\n", f);
        else if (*p == '\r') std::fputs("\\r", f);
        else if (*p == '\t') std::fputs("\\t", f);
        else std::fputc(*p, f);
    }
}

static void TraceSink(const UiTraceEvent* ev, void*) {
    if (!g_traceFile || !ev) return;
    std::fputs("{\"source\":\"", g_traceFile); JsonEsc(g_traceFile, ev->source);
    std::fputs("\",\"category\":\"", g_traceFile); JsonEsc(g_traceFile, ev->category);
    std::fputs("\",\"name\":\"", g_traceFile); JsonEsc(g_traceFile, ev->name);
    std::fputs("\",\"fields\":{", g_traceFile);
    for (uint32_t i = 0; i < ev->field_count; ++i) {
        const UiTraceField& field = ev->fields[i];
        if (i) std::fputc(',', g_traceFile);
        std::fputc('"', g_traceFile); JsonEsc(g_traceFile, field.key); std::fputs("\":", g_traceFile);
        switch (field.type) {
            case UI_TRACE_FIELD_I64:  std::fprintf(g_traceFile, "%lld", (long long)field.i64); break;
            case UI_TRACE_FIELD_U64:  std::fprintf(g_traceFile, "%llu", (unsigned long long)field.u64); break;
            case UI_TRACE_FIELD_F64:  std::fprintf(g_traceFile, "%.6f", field.f64); break;
            case UI_TRACE_FIELD_BOOL: std::fputs(field.boolean ? "true" : "false", g_traceFile); break;
            case UI_TRACE_FIELD_STR:
                std::fputc('"', g_traceFile); JsonEsc(g_traceFile, field.str); std::fputc('"', g_traceFile);
                break;
            default: std::fputs("null", g_traceFile); break;
        }
    }
    std::fputs("}}\n", g_traceFile);
    std::fflush(g_traceFile);
}

static unsigned char ClampByte(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return static_cast<unsigned char>(v);
}

static int ClampInt(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int JsonBool(const char* json, int fallback) {
    if (!json) return fallback;
    if (std::strstr(json, "true")) return 1;
    if (std::strstr(json, "false")) return 0;
    return fallback;
}

static int JsonInt(const char* json, int fallback) {
    if (!json) return fallback;
    const char* p = json;
    while (*p && !((*p >= '0' && *p <= '9') || *p == '-')) ++p;
    if (!*p) return fallback;
    char* end = nullptr;
    double v = std::strtod(p, &end);
    if (end == p) return fallback;
    return static_cast<int>(v + (v >= 0 ? 0.5 : -0.5));
}

static void ApplyEventsMenuMaterial() {
    if (!g_eventsMenu) return;
    if (g_appliedMenuTransparent == g_menuTransparent &&
        g_appliedMenuBlur == g_menuBlur &&
        g_appliedMenuDark == g_curDark) {
        return;
    }

    float alpha = g_menuTransparent ? (g_curDark ? 0.58f : 0.52f) : 1.0f;
    UiColor bg = g_curDark
        ? UiColor{0x2C / 255.0f, 0x2C / 255.0f, 0x2C / 255.0f, alpha}
        : UiColor{1.0f, 1.0f, 1.0f, alpha};
    ui_menu_set_bg_color(g_eventsMenu, bg);
    ui_menu_set_backdrop_blur(g_eventsMenu, g_menuTransparent ? (float)g_menuBlur : 0.0f);

    g_appliedMenuTransparent = g_menuTransparent;
    g_appliedMenuBlur = g_menuBlur;
    g_appliedMenuDark = g_curDark;
}

static void InitOverlayGhImageIfNeeded() {
    if (!g_page || !g_win) return;
    UiWidget root = ui_page_root(g_page);
    UiWidget gh = root ? ui_widget_find_by_id(root, "overlayGh") : 0;
    if (!gh) {
        g_overlayGh = 0;
        return;
    }
    if (gh == g_overlayGh) return;

    constexpr uint32_t W = 1800;
    constexpr uint32_t H = 900;
    std::vector<unsigned char> pixels(static_cast<size_t>(W) * H * 4);
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            int r = 18 + static_cast<int>(x * 130 / W);
            int g = 50 + static_cast<int>(y * 138 / H);
            int b = 94 + static_cast<int>((W - x) * 96 / W);

            auto blend = [&](int tr, int tg, int tb, int a) {
                r = (r * (255 - a) + tr * a) / 255;
                g = (g * (255 - a) + tg * a) / 255;
                b = (b * (255 - a) + tb * a) / 255;
            };

            int dx = static_cast<int>(x) - 350;
            int dy = static_cast<int>(y) - 240;
            if (dx * dx + dy * dy < 210 * 210) blend(248, 113, 113, 175);
            dx = static_cast<int>(x) - 980;
            dy = static_cast<int>(y) - 260;
            if (dx * dx + dy * dy < 250 * 250) blend(59, 130, 246, 180);
            dx = static_cast<int>(x) - 1420;
            dy = static_cast<int>(y) - 650;
            if (dx * dx + dy * dy < 230 * 230) blend(250, 204, 21, 160);

            if ((x % 160) < 3 || (y % 120) < 3) blend(255, 255, 255, 65);
            if ((x > 190 && x < 430 && y > 590 && y < 720) ||
                (x > 760 && x < 1040 && y > 420 && y < 560) ||
                (x > 1260 && x < 1580 && y > 220 && y < 360)) {
                blend(255, 255, 255, 80);
            }

            size_t i = (static_cast<size_t>(y) * W + x) * 4;
            pixels[i + 0] = ClampByte(b);
            pixels[i + 1] = ClampByte(g);
            pixels[i + 2] = ClampByte(r);
            pixels[i + 3] = 255;
        }
    }

    UiGhImgViewInfo info{};
    info.full_width = W;
    info.full_height = H;
    info.tile_size = 256;
    info.levels = 1;
    ui_gh_img_view_begin(gh, g_win, &info);
    ui_gh_img_view_set_preview(gh, g_win, pixels.data(), W, H, W * 4);
    ui_gh_img_view_fit(gh);
    g_overlayGh = gh;
}

static VOID CALLBACK OnPoll(HWND, UINT, UINT_PTR, DWORD) {
    if (!g_page) return;

    // Locale: settings page writes requestedLocale via setLang(); forward
    // it to ui_page_set_locale so $t bindings refire.
    if (char* req = ui_page_get_json(g_page, "requestedLocale")) {
        const char* want = nullptr;
        if      (std::strstr(req, "\"zh\"")) want = "zh";
        else if (std::strstr(req, "\"en\"")) want = "en";
        if (want && std::strcmp(g_curLocale, want) != 0) {
            ui_page_set_locale(g_page, want);
            std::strncpy(g_curLocale, want, sizeof(g_curLocale) - 1);
            g_curLocale[sizeof(g_curLocale) - 1] = 0;
        }
        ui_page_free(req);
    }

    // Dark mode: <toggle v-model="dark"/> writes state.dark; forward to
    // ui_theme_set_mode so native widgets (toggle/checkbox/button/input)
    // repaint with dark colors. Demo CSS responds via theme CSS variables.
    if (char* j = ui_page_get_json(g_page, "dark")) {
        int wantDark = (std::strstr(j, "true") != nullptr) ? 1 : 0;
        if (wantDark != g_curDark) {
            ui_theme_set_mode(wantDark ? UI_THEME_DARK : UI_THEME_LIGHT);
            g_curDark = wantDark;
        }
        ui_page_free(j);
    }

    if (char* j = ui_page_get_json(g_page, "menuTransparent")) {
        g_menuTransparent = JsonBool(j, g_menuTransparent);
        ui_page_free(j);
    }
    if (char* j = ui_page_get_json(g_page, "menuBlur")) {
        g_menuBlur = ClampInt(JsonInt(j, g_menuBlur), 0, 36);
        ui_page_free(j);
    }
    ApplyEventsMenuMaterial();

    // 打开动画开关 / 慢放倍数是进程全局的, 每次轮询直接转发 (都是写个原子量)。
    if (char* j = ui_page_get_json(g_page, "menuOpenAnim")) {
        ui_menu_set_open_animation(JsonBool(j, 1));
        ui_page_free(j);
    }
    if (char* j = ui_page_get_json(g_page, "menuSlowmo")) {
        ui_debug_set_menu_anim_slowmo((float)ClampInt(JsonInt(j, 1), 1, 10));
        ui_page_free(j);
    }
    if (char* j = ui_page_get_json(g_page, "widgetSlowmo")) {
        ui_debug_set_widget_anim_slowmo((float)ClampInt(JsonInt(j, 1), 1, 10));
        ui_page_free(j);
    }

    InitOverlayGhImageIfNeeded();
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    char tracePath[MAX_PATH] = {};
    if (GetEnvironmentVariableA("UI_DEMO_TRACE", tracePath, MAX_PATH) > 0) {
        fopen_s(&g_traceFile, tracePath, "wb");
        if (g_traceFile) ui_trace_set_sink(TraceSink, nullptr);
    }

    ui_init_with_theme(UI_THEME_LIGHT);

    UiPage page = ui_page_load_string(k_app_uix);
    if (!page) {
        ui_shutdown();
        return 1;
    }
    g_page = page;

    ui_page_load_language_string(page, "zh", k_lang_zh);
    ui_page_load_language_string(page, "en", k_lang_en);
    ui_page_set_locale(page, "zh");
    std::strcpy(g_curLocale, "zh");

    g_eventsMenu = ui_page_menu(page, "eventsContext");
    ApplyEventsMenuMaterial();

    UiWindow win = ui_page_open_window(page, NULL);
    if (!win) {
        ui_page_destroy(page);
        ui_shutdown();
        return 2;
    }
    g_win = win;

    // 200ms poll: the i18n / settings pages write requestedLocale into
    // state via the @click="setLang('zh'|'en')" methods; we forward the
    // change to ui_page_set_locale so $t bindings refire.
    HWND hwnd = (HWND)ui_window_hwnd(win);
    SetTimer(hwnd, 1, 200, OnPoll);

    ui_debug_server_start(win, NULL);
    int exitCode = ui_run();
    KillTimer(hwnd, 1);
    ui_debug_server_stop();
    if (g_traceFile) {
        ui_trace_set_sink(nullptr, nullptr);
        std::fclose(g_traceFile);
        g_traceFile = nullptr;
    }
    ui_page_destroy(page);
    ui_shutdown();
    return exitCode;
}
