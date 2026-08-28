/*
 * ocr_uix_demo — <ocr-img-view> 的 .uix 集成 demo (build 277+)
 *
 * 证明两件事:
 *   1. 画布可以直接在 .uix 里声明 (<ocr-img-view id="ocr"/>), 宿主用
 *      ui_widget_find_by_id 拿句柄后照常调用全部 ui_ocr_img_view_* C API —
 *      跟 <gh-img-view> 同款用法。
 *   2. 宿主侧行列表 ↔ 画布的双向联动: 点右侧行 → select_range 高亮图上
 *      对应文字; 图上划词 → selection_range + item_meta 反查行号 → 高亮
 *      右侧行。
 *
 * 图片是合成的 (每行文字画成一个色块), 不依赖任何 OCR 引擎: 画的时候就
 * 知道每行的 quad, 等价于一份"绝对准确的识别结果"。
 *
 * 自动化 (pipe 名独占 "ocr_uix_demo"), 除 builtin 命令外:
 *   rows            → 行列表 JSON
 *   item_count      → 拆字后的 item 数
 *   select_line <i> → 走跟点击行完全相同的代码路径
 *   sel             → {"text":"<选中文本>"}
 *   sel_range       → {"first":..,"last":..,"line":..}
 */

#include <ui_core.h>

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ocr_uix_demo_uix.embed.h"

namespace {

struct DemoLine {
    std::string utf8;
    float       quad[8];   // 图像像素坐标 TL,TR,BR,BL
};

constexpr uint32_t kImageW = 880;
constexpr uint32_t kImageH = 520;

UiPage   g_page = 0;
UiWindow g_win  = 0;
UiWidget g_view = 0;
std::vector<DemoLine> g_lines;
std::vector<UiWidget> g_rowWidgets;   // v-for 挂载顺序 == g_lines 顺序
int g_activeLine = -1;

// ---------------------------------------------------------------- 合成图片

std::vector<uint8_t> BuildImage() {
    std::vector<uint8_t> bgra((size_t)kImageW * kImageH * 4, 0);
    for (uint32_t y = 0; y < kImageH; ++y) {
        for (uint32_t x = 0; x < kImageW; ++x) {
            const size_t i = ((size_t)y * kImageW + x) * 4;
            const uint8_t v = (uint8_t)(238 - (int)(y * 18 / kImageH));
            bgra[i + 0] = v;                  // B
            bgra[i + 1] = v;                  // G
            bgra[i + 2] = (uint8_t)(v + 4);   // R
            bgra[i + 3] = 255;
        }
    }
    // 每行文字画成一个深色条 —— 划词高亮是否贴合就靠肉眼跟它对齐。
    for (const DemoLine& line : g_lines) {
        const uint32_t x0 = (uint32_t)line.quad[0], y0 = (uint32_t)line.quad[1];
        const uint32_t x1 = (uint32_t)line.quad[4], y1 = (uint32_t)line.quad[5];
        for (uint32_t y = y0; y < y1 && y < kImageH; ++y) {
            for (uint32_t x = x0; x < x1 && x < kImageW; ++x) {
                const size_t i = ((size_t)y * kImageW + x) * 4;
                bgra[i + 0] = 96; bgra[i + 1] = 92; bgra[i + 2] = 88; bgra[i + 3] = 255;
            }
        }
    }
    return bgra;
}

void BuildLines() {
    static const char* kTexts[] = {
        "\xE7\xAC\xAC\xE4\xB8\x80\xE8\xA1\x8C\xEF\xBC\x9A\xE5\x9B\xBE\xE7\x89\x87\xE5\x88\x92\xE8\xAF\x8D\xE6\xBC\x94\xE7\xA4\xBA",   // 第一行：图片划词演示
        "\xE7\xAC\xAC\xE4\xBA\x8C\xE8\xA1\x8C\xEF\xBC\x9A\xE7\x82\xB9\xE5\x8F\xB3\xE4\xBE\xA7\xE8\xA1\x8C\xE8\xAF\x95\xE8\xAF\x95",   // 第二行：点右侧行试试
        "\xE7\xAC\xAC\xE4\xB8\x89\xE8\xA1\x8C\xEF\xBC\x9A\xE5\x9C\xA8\xE5\x9B\xBE\xE4\xB8\x8A\xE6\x8B\x96\xE5\x8A\xA8\xE5\x88\x92\xE9\x80\x89",   // 第三行：在图上拖动划选
        "Line 4: mixed ASCII and CJK content",
    };
    g_lines.clear();
    for (int i = 0; i < 4; ++i) {
        DemoLine line;
        line.utf8 = kTexts[i];
        const float top    = 60.0f + i * 110.0f;
        const float bottom = top + 56.0f;
        const float left   = 60.0f;
        const float right  = left + 300.0f + i * 110.0f;
        line.quad[0] = left;  line.quad[1] = top;
        line.quad[2] = right; line.quad[3] = top;
        line.quad[4] = right; line.quad[5] = bottom;
        line.quad[6] = left;  line.quad[7] = bottom;
        g_lines.push_back(line);
    }
}

// ------------------------------------------------------------ 行列表 ↔ 画布

void PushRows() {
    std::string json = "[";
    for (size_t i = 0; i < g_lines.size(); ++i) {
        if (i) json += ',';
        json += "{\"text\":\"";
        json += g_lines[i].utf8;
        json += "\",\"cls\":\"";
        json += ((int)i == g_activeLine) ? "row active" : "row";
        json += "\"}";
    }
    json += "]";
    ui_page_set_json(g_page, "lines", json.c_str());
}

void SetStatus(const std::string& utf8) {
    std::string json = "\"" + utf8 + "\"";
    ui_page_set_json(g_page, "status", json.c_str());
}

/* 行号 → 拆字后的 item 下标闭区间。BLOCK 粒度下一行会被拆成很多字, 靠
 * item_meta 的 line 字段找回边界 —— 这正是宿主该做的映射。 */
bool ItemRangeForLine(int lineIndex, uint32_t& first, uint32_t& last) {
    const uint32_t count = ui_ocr_img_view_item_count(g_view);
    bool found = false;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t block = 0, line = 0, word = 0;
        if (!ui_ocr_img_view_item_meta(g_view, i, &block, &line, &word)) continue;
        if ((int)line != lineIndex) continue;
        if (!found) { first = i; found = true; }
        last = i;
    }
    return found;
}

void SelectLine(int lineIndex) {
    uint32_t first = 0, last = 0;
    if (!ItemRangeForLine(lineIndex, first, last)) return;
    ui_ocr_img_view_select_range(g_view, first, last);
}

/* 选区变化 (拖动划词 / select_range / 全选) 统一回到这里, 反查行号刷新
 * 右侧列表高亮 —— 图 → 列表方向。 */
void OnSelectionChanged(UiWidget, void*) {
    uint32_t first = 0, last = 0;
    int line = -1;
    if (ui_ocr_img_view_selection_range(g_view, &first, &last)) {
        uint32_t block = 0, lineNo = 0, word = 0;
        if (ui_ocr_img_view_item_meta(g_view, first, &block, &lineNo, &word)) {
            line = (int)lineNo;
        }
    }
    if (line != g_activeLine) {
        g_activeLine = line;
        PushRows();
    }
    const char* text = ui_ocr_img_view_selected_text(g_view);
    char buf[512];
    snprintf(buf, sizeof(buf), "item %u | 选中行 %d | 选中文本: %s",
             ui_ocr_img_view_item_count(g_view), line,
             (text && *text) ? text : "(无)");
    SetStatus(buf);
}

void OnRowClick(UiWidget w, void*) {
    for (size_t i = 0; i < g_rowWidgets.size(); ++i) {
        if (g_rowWidgets[i] == w) { SelectLine((int)i); return; }
    }
}

void OnRowMount(UiPage, UiWidget w, void*) {
    g_rowWidgets.push_back(w);
    ui_widget_on_click(w, OnRowClick, nullptr);
}

// ------------------------------------------------------------------ 自动化

int DemoCmdHandler(const char* cmd, const char* args, char* out, int cap, void*) {
    std::string resp;
    char buf[1024];
    if (!cmd) return 0;

    if (strcmp(cmd, "rows") == 0) {
        resp = "[";
        for (size_t i = 0; i < g_lines.size(); ++i) {
            if (i) resp += ',';
            resp += "\"" + g_lines[i].utf8 + "\"";
        }
        resp += "]";
    } else if (strcmp(cmd, "item_count") == 0) {
        snprintf(buf, sizeof(buf), "{\"count\":%u}", ui_ocr_img_view_item_count(g_view));
        resp = buf;
    } else if (strcmp(cmd, "select_line") == 0) {
        const int i = args ? atoi(args) : -1;
        if (i < 0 || i >= (int)g_lines.size()) {
            resp = "{\"error\":\"bad line\"}";
        } else {
            SelectLine(i);
            uint32_t first = 0, last = 0;
            ui_ocr_img_view_selection_range(g_view, &first, &last);
            snprintf(buf, sizeof(buf), "{\"line\":%d,\"first\":%u,\"last\":%u}",
                     i, first, last);
            resp = buf;
        }
    } else if (strcmp(cmd, "line_edge") == 0) {
        /* 第 i 行左/右边缘内侧的窗口 DIP 坐标 (drag_to 的输入)。选择粒度是
         * 字, 想整行选中就必须从行头拖到行尾, 用中心点只会选到半行。 */
        int i = -1, side = 0;
        if (args) sscanf_s(args, "%d %d", &i, &side);
        if (i < 0 || i >= (int)g_lines.size()) {
            resp = "{\"error\":\"bad line\"}";
        } else {
            const float* q = g_lines[i].quad;
            const float inset = 6.0f;
            const float ix = side ? (q[2] - inset) : (q[0] + inset);
            const float iy = (q[1] + q[5]) * 0.5f;
            float sx = 0, sy = 0;
            ui_ocr_img_view_image_to_screen(g_view, ix, iy, &sx, &sy);
            snprintf(buf, sizeof(buf), "{\"x\":%.1f,\"y\":%.1f}", sx, sy);
            resp = buf;
        }
    } else if (strcmp(cmd, "clear_sel") == 0) {
        ui_ocr_img_view_clear_selection(g_view);
        resp = "{\"ok\":true}";
    } else if (strcmp(cmd, "select_all") == 0) {
        ui_ocr_img_view_select_all(g_view);
        uint32_t first = 0, last = 0;
        ui_ocr_img_view_selection_range(g_view, &first, &last);
        snprintf(buf, sizeof(buf), "{\"first\":%u,\"last\":%u}", first, last);
        resp = buf;
    } else if (strcmp(cmd, "sel") == 0) {
        const char* text = ui_ocr_img_view_selected_text(g_view);
        resp = std::string("{\"text\":\"") + (text ? text : "") + "\"}";
    } else if (strcmp(cmd, "sel_range") == 0) {
        uint32_t first = 0, last = 0;
        if (!ui_ocr_img_view_selection_range(g_view, &first, &last)) {
            resp = "{\"empty\":true}";
        } else {
            uint32_t block = 0, line = 0, word = 0;
            ui_ocr_img_view_item_meta(g_view, first, &block, &line, &word);
            snprintf(buf, sizeof(buf),
                     "{\"first\":%u,\"last\":%u,\"line\":%u,\"block\":%u}",
                     first, last, line, block);
            resp = buf;
        }
    } else {
        return 0;   // 回退 builtin
    }

    if ((int)resp.size() >= cap) return -1;
    memcpy(out, resp.c_str(), resp.size() + 1);
    return (int)resp.size();
}

void BindButton(UiWidget root, const char* id, UiClickCallback cb) {
    if (UiWidget w = ui_widget_find_by_id(root, id)) ui_widget_on_click(w, cb, nullptr);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    ui_init_with_theme(UI_THEME_LIGHT);

    g_page = ui_page_load_string(k_ocr_uix_demo_uix);
    if (!g_page) { ui_shutdown(); return 1; }

    /* v-for 行的 on_click 要在首次渲染前挂上 (mount 时才有 widget)。 */
    ui_page_on_widget_mount(g_page, "line_row", OnRowMount, nullptr);

    g_win = ui_page_open_window(g_page, nullptr);
    if (!g_win) { ui_page_destroy(g_page); ui_shutdown(); return 2; }

    UiWidget root = ui_page_root(g_page);
    g_view = root ? ui_widget_find_by_id(root, "ocr") : 0;
    if (!g_view) { ui_page_destroy(g_page); ui_shutdown(); return 3; }

    BuildLines();

    /* 图像 —— .uix 建出来的 widget 跟 C API 建的走完全同一条喂图路径。 */
    const std::vector<uint8_t> bgra = BuildImage();
    const int imageResult = ui_ocr_img_view_set_image(g_view, g_win, bgra.data(),
                                                      kImageW, kImageH, kImageW * 4);

    /* 文本项: 每行一个块, 由 lib 在入口拆成字 (中文 OCR 常态)。 */
    std::vector<UiOcrTextItem> items;
    items.reserve(g_lines.size());
    for (uint32_t i = 0; i < g_lines.size(); ++i) {
        UiOcrTextItem item{};
        item.text = g_lines[i].utf8.c_str();
        memcpy(item.quad, g_lines[i].quad, sizeof(item.quad));
        item.block = 0;
        item.line  = i;
        item.word  = i;
        items.push_back(item);
    }
    ui_ocr_img_view_set_text(g_view, items.data(), (uint32_t)items.size(),
                             sizeof(UiOcrTextItem), UI_OCR_GRANULARITY_BLOCK);
    ui_ocr_img_view_on_selection_changed(g_view, OnSelectionChanged, nullptr);
    ui_ocr_img_view_fit(g_view);

    BindButton(root, "btn_fit",   [](UiWidget, void*) { ui_ocr_img_view_fit(g_view); });
    BindButton(root, "btn_reset", [](UiWidget, void*) { ui_ocr_img_view_reset(g_view); });
    BindButton(root, "btn_rotate", [](UiWidget, void*) {
        ui_ocr_img_view_set_rotation(g_view, ui_ocr_img_view_get_rotation(g_view) + 90);
        ui_ocr_img_view_fit(g_view);
    });
    BindButton(root, "btn_all",   [](UiWidget, void*) { ui_ocr_img_view_select_all(g_view); });
    BindButton(root, "btn_clear", [](UiWidget, void*) { ui_ocr_img_view_clear_selection(g_view); });

    g_rowWidgets.clear();
    PushRows();
    char status[256];
    snprintf(status, sizeof(status),
             "set_image=%d | item %u | 点右侧行或在图上拖动划词",
             imageResult, ui_ocr_img_view_item_count(g_view));
    SetStatus(status);

    ui_debug_server_set_handler(DemoCmdHandler, nullptr);
    ui_debug_server_start(g_win, "ocr_uix_demo");

    const int rc = ui_run();
    ui_debug_server_stop();
    ui_page_destroy(g_page);
    ui_shutdown();
    return rc;
}
