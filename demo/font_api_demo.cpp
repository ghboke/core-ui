/*
 * font_api_demo — 公开 C API 版字体/渲染模式演示 (1.3.0)
 *
 * 只用 <ui_core.h>，不触碰 ui::Renderer 内部。展示：
 *   - ui_theme_set_default_font         全局默认字体
 *   - ui_theme_set_cjk_font             中英分离 (latin / cjk)
 *   - ui_theme_set_text_render_mode     全局渲染模式
 *   - ui_window_set_text_render_mode    单窗口渲染模式覆盖
 *
 * 3 个窗口并排：左=全局默认(Smooth)，中=全局设成 Sharp + CJK 分离，
 * 右=中那份窗口又被单独覆盖回 ClearType。
 */
#include <ui_core.h>
#include <windows.h>

static UiWidget BuildPanel(const wchar_t* heading) {
    UiWidget root = ui_vbox();
    ui_widget_set_bg_color(root, UiColor{1, 1, 1, 1});

    UiWidget title = ui_titlebar(heading);
    ui_widget_add_child(root, title);

    UiWidget body = ui_vbox();
    ui_widget_set_expand(body, 1);
    ui_widget_add_child(root, body);

    const wchar_t* samples[] = {
        L"Title 24 标题",
        L"Body 14 Core UI 字体 API Demo",
        L"Caption 12 中英混排 Mix Han 汉字 + Latin",
        L"永字八法 iIl1!|Oo0 abc 123",
    };
    for (auto* s : samples) {
        UiWidget lbl = ui_label(s);
        ui_widget_add_child(body, lbl);
    }
    return root;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    ui_init_with_theme(UI_THEME_LIGHT);

    /* ---- 窗口 1：全局默认（Segoe UI + Smooth） ---- */
    UiWindowConfig c1 = {0};
    c1.width = 520; c1.height = 360;
    c1.title = L"1) global default (Segoe UI + Smooth)";
    c1.resizable = 1;
    UiWindow w1 = ui_window_create(&c1);
    ui_window_set_root(w1, BuildPanel(L"Global default"));
    ui_window_show(w1);

    /* ---- 切全局：Latin=Consolas, CJK=Microsoft YaHei UI, 模式=Sharp ---- */
    ui_theme_set_cjk_font(L"Consolas", L"Microsoft YaHei UI");
    ui_theme_set_text_render_mode(UI_TEXT_RENDER_SHARP);

    /* ---- 窗口 2：全局 Sharp + CJK 分离（继承新全局） ---- */
    UiWindowConfig c2 = {0};
    c2.width = 520; c2.height = 360;
    c2.title = L"2) global CJK split + Sharp";
    c2.resizable = 1;
    UiWindow w2 = ui_window_create(&c2);
    ui_window_set_root(w2, BuildPanel(L"Global Sharp + CJK split"));
    ui_window_show(w2);

    /* ---- 窗口 3：继承全局后单独把模式覆盖成 ClearType，
     *     再把默认字体换成 "SimSun" 以证明 per-window 覆盖独立 ---- */
    UiWindowConfig c3 = {0};
    c3.width = 520; c3.height = 360;
    c3.title = L"3) per-window override: SimSun + ClearType";
    c3.resizable = 1;
    UiWindow w3 = ui_window_create(&c3);
    ui_window_set_root(w3, BuildPanel(L"Per-window override"));
    ui_window_set_default_font(w3, L"SimSun");
    ui_window_set_text_render_mode(w3, UI_TEXT_RENDER_CLEARTYPE);
    ui_window_show(w3);

    return ui_run();
}
