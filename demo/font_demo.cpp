/*
 * font_demo — 文字渲染模式 × 字体 对比
 *
 * 布局：3 行字体（Segoe UI / 微软雅黑 / 宋体）× 4 列模式（A/B/C/D）
 * 同一段文字在每个格子用不同组合渲染，眼睛挑最清晰的。
 */
#include <ui_core.h>
#include "../src/ui/renderer.h"
#include <windows.h>
#include <dwrite.h>
#include <string>

/* ------------------------------------------------------------------ */
/* 渲染参数组                                                          */
/* ------------------------------------------------------------------ */
struct FontMode {
    const wchar_t* label;
    D2D1_TEXT_ANTIALIAS_MODE antialias;
    DWRITE_RENDERING_MODE    renderMode;
    float gamma;
    float enhancedContrast;
    float clearTypeLevel;
};

static const FontMode g_modes[] = {
    { L"A  GRAY + NATURAL_SYM\n当前默认 / WinUI",
      D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE,
      DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
      1.8f, 0.5f, 0.0f },
    { L"B  GRAY + NATURAL\n灰度+亚像素",
      D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE,
      DWRITE_RENDERING_MODE_NATURAL,
      1.8f, 1.0f, 0.0f },
    { L"C  ClearType + NAT_SYM\nClearType 平滑",
      D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE,
      DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
      1.8f, 1.0f, 1.0f },
    { L"D  ClearType + NATURAL\nOffice/Chrome 风格",
      D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE,
      DWRITE_RENDERING_MODE_NATURAL,
      1.8f, 1.0f, 1.0f },
    { L"E  ClearType + GDI_CLS\n记事本 最锐",
      D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE,
      DWRITE_RENDERING_MODE_GDI_CLASSIC,
      1.8f, 1.0f, 1.0f },
    { L"F  GRAY + GDI_CLASSIC\n灰度+像素对齐",
      D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE,
      DWRITE_RENDERING_MODE_GDI_CLASSIC,
      1.8f, 1.0f, 0.0f },
    { L"G  ALIASED\n无抗锯齿 / 像素块",
      D2D1_TEXT_ANTIALIAS_MODE_ALIASED,
      DWRITE_RENDERING_MODE_ALIASED,
      1.8f, 0.0f, 0.0f },
};
static const int g_modeCount = sizeof(g_modes) / sizeof(g_modes[0]);

struct FontFamily { const wchar_t* name; const wchar_t* display; };
static const FontFamily g_fonts[] = {
    { L"Segoe UI",      L"Segoe UI"      },
    { L"Microsoft YaHei UI", L"Microsoft YaHei (微软雅黑)" },
    { L"SimSun",        L"SimSun (宋体)"  },
};
static const int g_fontCount = 3;

/* ------------------------------------------------------------------ */

static void ApplyFontMode(ui::Renderer& r, const FontMode& m) {
    auto* ctx = r.RT();
    if (!ctx) return;
    ctx->SetTextAntialiasMode(m.antialias);
    IDWriteRenderingParams* base = nullptr;
    IDWriteRenderingParams* custom = nullptr;
    if (SUCCEEDED(r.DWFactory()->CreateRenderingParams(&base))) {
        r.DWFactory()->CreateCustomRenderingParams(
            m.gamma, m.enhancedContrast, m.clearTypeLevel,
            base->GetPixelGeometry(), m.renderMode, &custom);
        if (custom) { ctx->SetTextRenderingParams(custom); custom->Release(); }
        base->Release();
    }
}

/* 自画文字（可指定 font family / 粗细 / 是否允许换行） */
static void DrawTextFamily(ui::Renderer& r, const std::wstring& text,
                           const D2D1_RECT_F& rect, const D2D1_COLOR_F& color,
                           float fontSize, const wchar_t* family, bool bold,
                           bool wrap = false) {
    auto* ctx = r.RT();
    if (!ctx || !r.DWFactory()) return;

    IDWriteTextFormat* fmt = nullptr;
    HRESULT hr = r.DWFactory()->CreateTextFormat(
        family, nullptr,
        bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"en-us", &fmt);
    if (FAILED(hr) || !fmt) return;
    fmt->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP
                               : DWRITE_WORD_WRAPPING_NO_WRAP);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    ID2D1SolidColorBrush* brush = nullptr;
    hr = ctx->CreateSolidColorBrush(color, &brush);
    if (SUCCEEDED(hr) && brush) {
        ctx->DrawText(text.c_str(), (UINT32)text.length(), fmt, rect, brush);
        brush->Release();
    }
    fmt->Release();
}

/* ------------------------------------------------------------------ */
/* 单格示例文字                                                         */
/* ------------------------------------------------------------------ */
struct Sample { const wchar_t* text; float size; bool bold; };
static const Sample g_samples[] = {
    { L"Title 24 标题",          24.0f, true  },
    { L"Body 14 Core UI 正文",   14.0f, false },
    { L"Bold 14 粗体字",         14.0f, true  },
    { L"Caption 12 小字 abc",    12.0f, false },
    { L"10px 细节 abc 123",      10.0f, false },
    { L"永字八法 iIl1!|Oo0",     14.0f, false },
};
static const int g_sampleCount = sizeof(g_samples) / sizeof(g_samples[0]);

/* ------------------------------------------------------------------ */

static void DrawComparison(UiWidget, UiDrawCtx ctxPtr, UiRect rect, void*) {
    auto* r = reinterpret_cast<ui::Renderer*>(ctxPtr);
    if (!r) return;

    float rowLabelW = 150.0f;   /* 最左一列：显示字体名 */
    float contentW  = (rect.right - rect.left) - rowLabelW;
    float colW      = contentW / g_modeCount;
    float headerH   = 48.0f;    /* 两行 label */
    float rowH      = ((rect.bottom - rect.top) - headerH) / g_fontCount;

    D2D1_COLOR_F textColor    = { 0.12f, 0.12f, 0.12f, 1.0f };
    D2D1_COLOR_F bgColor      = { 1.00f, 1.00f, 1.00f, 1.0f };
    D2D1_COLOR_F dividerColor = { 0.82f, 0.82f, 0.82f, 1.0f };
    D2D1_COLOR_F labelColor   = { 0.20f, 0.40f, 0.85f, 1.0f };
    D2D1_COLOR_F rowLabelBg   = { 0.96f, 0.96f, 0.96f, 1.0f };

    /* 背景白 */
    UiRect full = { rect.left, rect.top, rect.right, rect.bottom };
    ui_draw_fill_rect(ctxPtr, full, UiColor{bgColor.r, bgColor.g, bgColor.b, bgColor.a});

    /* 左列背景灰 */
    UiRect rowLabelColRect = { rect.left, rect.top, rect.left + rowLabelW, rect.bottom };
    ui_draw_fill_rect(ctxPtr, rowLabelColRect, UiColor{rowLabelBg.r, rowLabelBg.g, rowLabelBg.b, 1.0f});

    /* header 行：每列模式 label（两行，第一行大写缩写、第二行备注） */
    ApplyFontMode(*r, g_modes[0]);  /* header 统一用模式 A 画 */
    for (int col = 0; col < g_modeCount; ++col) {
        float x0 = rect.left + rowLabelW + col * colW;
        float x1 = x0 + colW;
        D2D1_RECT_F hdr = { x0 + 8, rect.top + 4, x1 - 6, rect.top + headerH };
        DrawTextFamily(*r, g_modes[col].label, hdr,
                       {labelColor.r, labelColor.g, labelColor.b, 1.0f},
                       10.0f, L"Segoe UI", true, /*wrap*/ true);
    }

    /* header 下横线 */
    ui_draw_line(ctxPtr, rect.left, rect.top + headerH,
                 rect.right, rect.top + headerH,
                 UiColor{dividerColor.r, dividerColor.g, dividerColor.b, 1.0f}, 1.0f);

    /* 每行字体 × 每列模式 */
    for (int row = 0; row < g_fontCount; ++row) {
        float y0 = rect.top + headerH + row * rowH;
        float y1 = y0 + rowH;

        /* 行间横线 */
        if (row > 0) {
            ui_draw_line(ctxPtr, rect.left, y0, rect.right, y0,
                         UiColor{dividerColor.r, dividerColor.g, dividerColor.b, 1.0f}, 1.0f);
        }

        /* 行标签（字体名） */
        ApplyFontMode(*r, g_modes[0]);
        D2D1_RECT_F labelRect = { rect.left + 10, y0 + 8, rect.left + rowLabelW - 8, y1 };
        DrawTextFamily(*r, g_fonts[row].display, labelRect,
                       {0.3f, 0.3f, 0.3f, 1.0f}, 13.0f, L"Segoe UI", true);

        /* 4 列模式 */
        for (int col = 0; col < g_modeCount; ++col) {
            float x0 = rect.left + rowLabelW + col * colW;
            float x1 = x0 + colW;

            /* 列间竖线 */
            if (col > 0) {
                ui_draw_line(ctxPtr, x0, y0, x0, y1,
                             UiColor{dividerColor.r, dividerColor.g, dividerColor.b, 1.0f}, 1.0f);
            }

            /* 切模式并用指定字体画样本 */
            ApplyFontMode(*r, g_modes[col]);

            float y = y0 + 8;
            for (int s = 0; s < g_sampleCount; ++s) {
                auto& smp = g_samples[s];
                float lineH = smp.size + 4.0f;
                D2D1_RECT_F rc = { x0 + 10, y, x1 - 6, y + lineH };
                DrawTextFamily(*r, smp.text, rc,
                               {textColor.r, textColor.g, textColor.b, 1.0f},
                               smp.size, g_fonts[row].name, smp.bold);
                y += lineH + 2;
            }
        }
    }

    /* 列间竖线（贯穿所有行） */
    ApplyFontMode(*r, g_modes[0]);
    for (int col = 1; col < g_modeCount; ++col) {
        float x = rect.left + rowLabelW + col * colW;
        ui_draw_line(ctxPtr, x, rect.top, x, rect.bottom,
                     UiColor{dividerColor.r, dividerColor.g, dividerColor.b, 1.0f}, 1.0f);
    }
    /* rowLabel 列与 mode 内容的分界 */
    ui_draw_line(ctxPtr, rect.left + rowLabelW, rect.top,
                 rect.left + rowLabelW, rect.bottom,
                 UiColor{dividerColor.r, dividerColor.g, dividerColor.b, 1.0f}, 1.0f);
}

/* ------------------------------------------------------------------ */

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    ui_init_with_theme(UI_THEME_LIGHT);

    UiWindowConfig cfg = {0};
    cfg.system_frame = 0;
    cfg.resizable    = 1;
    cfg.width        = 2000;
    cfg.height       = 900;
    cfg.title        = L"Font Demo  (Segoe UI / YaHei / SimSun)  ×  (A..G)";
    UiWindow win = ui_window_create(&cfg);

    UiWidget root = ui_vbox();
    ui_widget_set_bg_color(root, UiColor{1, 1, 1, 1});

    UiWidget title = ui_titlebar(L"Font × Rendering Mode");
    ui_widget_add_child(root, title);

    UiWidget canvas = ui_custom();
    ui_custom_on_draw(canvas, DrawComparison, nullptr);
    ui_widget_set_expand(canvas, 1);
    ui_widget_add_child(root, canvas);

    ui_window_set_root(win, root);
    ui_window_show(win);
    return ui_run();
}
