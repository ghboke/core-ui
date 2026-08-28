/*
 * ocr_demo — ui_ocr_img_view 图片划词集成 demo
 *
 * 不依赖真 OCR 引擎：用 GDI 把文字画进一张 BGRA 位图，画的时候按同一套
 * 几何算出每个词的 quad —— 等价于一份"绝对准确的 OCR 结果"。喂给
 * ui_ocr_img_view 后即可验证划词/复制/缩放跟随/倾斜命中等全部交互。
 *
 * 三个场景（pipe 命令 scene <n> 切换）：
 *   0 flat    水平文字，基线场景
 *   1 angled  整行带角度（+15° / -12° / +28° / 中文 -8°），验证倾斜 quad
 *             的命中测试与平行四边形高亮是否贴合真实字形
 *   2 fan     单词逐个递增角度扇形铺开（0°→75°），压测极端倾角
 *
 * GDI 旋转文字：LOGFONT.lfEscapement 单位 0.1°、逆时针。TA_TOP|TA_LEFT 下
 * (x,y) 是文字在旋转后坐标系里的左上角，于是
 *   u = ( cosθ, -sinθ)   沿字向（屏幕 y 轴朝下，故 sin 取负）
 *   v = ( sinθ,  cosθ)   垂直向下
 *   TL=(x,y)  TR=TL+u*w  BR=TL+u*w+v*h  BL=TL+v*h
 * θ=0 时退化成普通矩形，和 flat 场景共用一条代码路径。
 *
 * 自动化（禁用系统截图，全走库的 debug server）：
 *   pipe 名独占 "ocr_demo"。除 builtin 命令（screenshot / drag_to /
 *   dbl_click_at / key ctrl+c / widget ...）外，注册了 demo 私有命令：
 *     sel              → {"text":"<当前选中文本>"}
 *     copy             → 调 ui_ocr_img_view_copy_selection
 *     clip             → 读系统剪贴板（验证 copy 真写进去了）
 *     word_center <i>  → 第 i 个词中心的窗口 DIP 坐标（drag_to 的输入）
 *     word_count       → 当前场景词数
 *     scene <n>        → 切场景，返回新场景名与词数
 */

#include "ui_core.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------- 测试图生成

struct DemoWord {
    std::string utf8;
    float       quad[8];    // 图像像素坐标 TL,TR,BR,BL
    uint32_t    line = 0, block = 0, word = 0, flags = 0;
};

static std::vector<DemoWord> g_words;
static UiWidget g_view = 0;   /* 句柄是整型 id, 不是指针 */
static UiWindow g_win  = 0;
static int      g_scene = 0;
static UiWidget g_sceneBtns[8] = {};
static UiWidget g_status = 0;
static int      g_lastImageError = 0;

static const wchar_t* kSceneNames[] = { L"flat", L"angled", L"fan", L"persp", L"cjk" };
static const int kSceneCount = 5;
/* 场景 4 直接喂字级坐标 (逐字量出来的), 走 CHAR 主路径; 其余场景喂块,
 * 走 BLOCK 兼容路径由 lib 拆字 —— 两条路都在 demo 里真跑。 */
static bool g_charGranularity = false;

// 画一串词（angleDeg 逆时针，0 = 水平），每词按旋转几何记录 quad。
// 返回沿【垂直方向】推进后的下一行锚点，调用方自己决定行距。
static void DrawWordsAt(HDC dc, float x, float y, float angleDeg,
                        uint32_t line, uint32_t block,
                        const wchar_t* const* words, int count,
                        bool spaceAfter) {
    const double rad = angleDeg * 3.14159265358979323846 / 180.0;
    const float ux = (float)std::cos(rad),  uy = (float)-std::sin(rad);
    const float vx = (float)std::sin(rad),  vy = (float)std::cos(rad);

    LOGFONTW lf{};
    GetObjectW(GetCurrentObject(dc, OBJ_FONT), sizeof(lf), &lf);
    lf.lfEscapement  = (LONG)(angleDeg * 10.0f);   // 0.1° 单位, 逆时针
    lf.lfOrientation = lf.lfEscapement;
    HFONT rot = CreateFontIndirectW(&lf);
    HGDIOBJ oldFont = SelectObject(dc, rot);

    SIZE spaceSz{};
    GetTextExtentPoint32W(dc, L" ", 1, &spaceSz);

    float cursor = 0.0f;   // 沿字向已推进的距离
    for (int i = 0; i < count; ++i) {
        const wchar_t* word = words[i];
        const int len = (int)wcslen(word);
        SIZE sz{};
        GetTextExtentPoint32W(dc, word, len, &sz);

        const float tlx = x + ux * cursor;
        const float tly = y + uy * cursor;
        TextOutW(dc, (int)lroundf(tlx), (int)lroundf(tly), word, len);

        DemoWord dw;
        const int u8 = WideCharToMultiByte(CP_UTF8, 0, word, len, nullptr, 0,
                                           nullptr, nullptr);
        dw.utf8.resize(u8);
        WideCharToMultiByte(CP_UTF8, 0, word, len, dw.utf8.data(), u8,
                            nullptr, nullptr);
        const float w = (float)sz.cx, h = (float)sz.cy;
        dw.quad[0] = tlx;                 dw.quad[1] = tly;                  // TL
        dw.quad[2] = tlx + ux * w;        dw.quad[3] = tly + uy * w;         // TR
        dw.quad[4] = tlx + ux * w + vx * h;
        dw.quad[5] = tly + uy * w + vy * h;                                  // BR
        dw.quad[6] = tlx + vx * h;        dw.quad[7] = tly + vy * h;         // BL
        dw.line  = line;
        dw.block = block;
        dw.flags = (spaceAfter && i + 1 < count) ? UI_OCR_ITEM_SPACE_AFTER : 0;
        g_words.push_back(dw);

        cursor += w + (float)spaceSz.cx;
    }

    SelectObject(dc, oldFont);
    DeleteObject(rot);
}

// 逐字符沿梯形插值缩放，画出真【透视】文字（近大远小），quad 按实际渲染
// 几何反算 —— 得到一个四条边互不平行的真四边形，而不是平行四边形。
// GDI 的 SetWorldTransform 只支持仿射（平行四边形），做不出透视，所以走
// 逐字缩放这条路。scaleL/scaleR = 行首/行尾的字号倍率。
static void DrawWordPerspective(HDC dc, float x, float y, float angleDeg,
                                float scaleL, float scaleR, int baseH,
                                uint32_t line, uint32_t block, uint32_t flags,
                                const wchar_t* word) {
    const double rad = angleDeg * 3.14159265358979323846 / 180.0;
    const float ux = (float)std::cos(rad),  uy = (float)-std::sin(rad);
    const float vx = (float)std::sin(rad),  vy = (float)std::cos(rad);

    LOGFONTW base{};
    GetObjectW(GetCurrentObject(dc, OBJ_FONT), sizeof(base), &base);

    // 先按基准字号量各字符宽度，用来估算总长以求每个字的插值参数 t
    const int len = (int)wcslen(word);
    std::vector<int> nomW((size_t)len);
    int nomTotal = 0;
    for (int i = 0; i < len; ++i) {
        SIZE s{};
        GetTextExtentPoint32W(dc, word + i, 1, &s);
        nomW[(size_t)i] = s.cx;
        nomTotal += s.cx;
    }
    if (nomTotal <= 0) return;

    float cursor = 0.0f;          // 沿字向已推进的距离（已含缩放）
    float lastScale = scaleL;
    int   nomSeen = 0;
    for (int i = 0; i < len; ++i) {
        const float t = (float)nomSeen / (float)nomTotal;
        const float sc = scaleL + (scaleR - scaleL) * t;
        lastScale = sc;

        LOGFONTW lf = base;
        lf.lfHeight      = -(LONG)lroundf(baseH * sc);
        lf.lfEscapement  = (LONG)(angleDeg * 10.0f);
        lf.lfOrientation = lf.lfEscapement;
        HFONT f = CreateFontIndirectW(&lf);
        HGDIOBJ old = SelectObject(dc, f);

        SIZE s{};
        GetTextExtentPoint32W(dc, word + i, 1, &s);
        const float px = x + ux * cursor;
        const float py = y + uy * cursor;
        TextOutW(dc, (int)lroundf(px), (int)lroundf(py), word + i, 1);

        SelectObject(dc, old);
        DeleteObject(f);

        cursor  += (float)s.cx;
        nomSeen += nomW[(size_t)i];
    }

    // quad 按实际渲染几何反算：行首行尾字高不同 → 底边不平行于顶边
    SIZE probe{};
    GetTextExtentPoint32W(dc, L"M", 1, &probe);
    const float hUnit = (float)probe.cy;          // 基准字号下的行高
    const float hL = hUnit * scaleL / 1.0f;
    const float hR = hUnit * lastScale / 1.0f;

    DemoWord dw;
    const int u8 = WideCharToMultiByte(CP_UTF8, 0, word, len, nullptr, 0,
                                       nullptr, nullptr);
    dw.utf8.resize(u8);
    WideCharToMultiByte(CP_UTF8, 0, word, len, dw.utf8.data(), u8,
                        nullptr, nullptr);
    const float trx = x + ux * cursor, try_ = y + uy * cursor;
    dw.quad[0] = x;                 dw.quad[1] = y;                    // TL
    dw.quad[2] = trx;               dw.quad[3] = try_;                 // TR
    dw.quad[4] = trx + vx * hR;     dw.quad[5] = try_ + vy * hR;       // BR
    dw.quad[6] = x + vx * hL;       dw.quad[7] = y + vy * hL;          // BL
    dw.line  = line;
    dw.block = block;
    dw.flags = flags;
    g_words.push_back(dw);
}

// 透视场景：翻拍书页 / 侧拍招牌的形态 —— 一行字近大远小，quad 是真梯形。
// widget 的高亮用 TL/TR/BL 三点定义平行四边形，隐含 BR = TR+BL-TL，
// 和真 BR 差 |hR - hL|。这个场景就是用来把这个差量做出来、量出来的。
static void BuildScenePersp(HDC dc) {
    // 由小变大（远→近）
    DrawWordPerspective(dc, 60, 120, 0, 0.62f, 1.45f, 36, 0, 0,
                        UI_OCR_ITEM_SPACE_AFTER, L"Receding");
    DrawWordPerspective(dc, 520, 100, 0, 1.45f, 1.60f, 36, 0, 0, 0, L"near");
    // 由大变小（近→远）+ 带角度
    DrawWordPerspective(dc, 70, 330, -10.0f, 1.55f, 0.70f, 36, 1, 0,
                        UI_OCR_ITEM_SPACE_AFTER, L"Shrinking");
    DrawWordPerspective(dc, 520, 400, -10.0f, 0.70f, 0.55f, 36, 1, 0, 0, L"away");
    // 中文透视
    DrawWordPerspective(dc, 80, 520, 0, 0.75f, 1.50f, 36, 2, 0, 0,
                        L"中文透视一行");
}

static void BuildSceneFlat(HDC dc) {
    const wchar_t* l0[] = { L"Hello", L"world", L"from", L"core-ui" };
    DrawWordsAt(dc, 40, 40, 0, 0, 0, l0, 4, true);
    const wchar_t* l1[] = { L"Drag", L"to", L"select", L"text" };
    DrawWordsAt(dc, 40, 110, 0, 1, 0, l1, 4, true);
    const wchar_t* l2[] = { L"这是一整行中文文本按行返回" };
    DrawWordsAt(dc, 40, 180, 0, 2, 0, l2, 1, false);
    const wchar_t* l3[] = { L"New", L"paragraph", L"here" };
    DrawWordsAt(dc, 40, 300, 0, 0, 1, l3, 3, true);
}

// 每行一个角度：拍歪的照片 / 扫描件常见形态。
// 各行 quad 的包围盒刻意互不重叠 —— 重叠会让"点某处该命中谁"变得有歧义,
// 自动化断言就失去意义 (命中重叠 quad 时 model 取阅读序靠前者)。
static void BuildSceneAngled(HDC dc) {
    const wchar_t* l0[] = { L"Tilted", L"line", L"at", L"plus", L"15" };
    DrawWordsAt(dc, 50, 220, +15.0f, 0, 0, l0, 5, true);
    const wchar_t* l1[] = { L"Sloping", L"down", L"minus", L"12" };
    DrawWordsAt(dc, 50, 330, -12.0f, 1, 0, l1, 4, true);
    const wchar_t* l2[] = { L"Steep", L"28" };
    DrawWordsAt(dc, 500, 600, +28.0f, 2, 0, l2, 2, true);
    const wchar_t* l3[] = { L"倾斜的中文一整行" };
    DrawWordsAt(dc, 560, 150, -8.0f, 0, 1, l3, 1, false);
}

// 中文长句：逐字量出每个字的真实 quad，走 CHAR 主路径（模拟能给单字坐标
// 的 OCR 引擎）。这是唯一能验证"字级坐标由宿主提供"那条路的场景。
static void BuildSceneCjk(HDC dc) {
    struct Line { const wchar_t* text; int x, y; uint32_t line; };
    static const Line kLines[] = {
        { L"这是一段可以逐字选择的中文文本",  60, 120, 0 },
        { L"拖动鼠标只取其中几个字试试看",    60, 230, 1 },
        { L"混排 English words 也能分词",     60, 340, 2 },
    };
    for (const Line& ln : kLines) {
        int x = ln.x;
        uint32_t word = 0;
        bool prevSpace = false;
        for (const wchar_t* p = ln.text; *p; ++p) {
            SIZE sz{};
            GetTextExtentPoint32W(dc, p, 1, &sz);
            /* 分词规则与 lib 的 SplitItemsIntoChars 保持一致: 空白自成一词,
             * 其后的字另起一词 —— 否则双击选词会带出尾随空格。 */
            const bool isSpace = (*p == L' ');
            if (isSpace || prevSpace) ++word;
            prevSpace = isSpace;
            if (!isSpace) TextOutW(dc, x, ln.y, p, 1);

            DemoWord dw;
            const int u8 = WideCharToMultiByte(CP_UTF8, 0, p, 1, nullptr, 0,
                                               nullptr, nullptr);
            dw.utf8.resize(u8);
            WideCharToMultiByte(CP_UTF8, 0, p, 1, dw.utf8.data(), u8,
                                nullptr, nullptr);
            const float fx = (float)x, fy = (float)ln.y;
            const float fw = (float)sz.cx, fh = (float)sz.cy;
            dw.quad[0] = fx;      dw.quad[1] = fy;
            dw.quad[2] = fx + fw; dw.quad[3] = fy;
            dw.quad[4] = fx + fw; dw.quad[5] = fy + fh;
            dw.quad[6] = fx;      dw.quad[7] = fy + fh;
            dw.line  = ln.line;
            dw.block = 0;
            dw.word  = word;
            dw.flags = 0;
            g_words.push_back(dw);
            x += sz.cx;
        }
    }
}

// 单词逐个递增角度 0°→75°：压测极端倾角下的命中与高亮
static void BuildSceneFan(HDC dc) {
    static const wchar_t* kFan[] = { L"zero", L"fifteen", L"thirty",
                                     L"fortyfive", L"sixty", L"seventyfive" };
    for (int i = 0; i < 6; ++i) {
        DrawWordsAt(dc, 50.0f + i * 160.0f, 650.0f, i * 15.0f,
                    (uint32_t)i, 0, &kFan[i], 1, false);
    }
}

// 生成 1000x700 白底黑字测试图 (BGRA premul, A=255) + 同步的"OCR 结果"。
static std::vector<uint8_t> MakeSceneImage(int scene, int& outW, int& outH) {
    const int W = 1000, H = 700;
    g_words.clear();

    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = W;
    bi.bmiHeader.biHeight      = -H;          // top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC dc     = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);

    RECT full{ 0, 0, W, H };
    FillRect(dc, &full, (HBRUSH)GetStockObject(WHITE_BRUSH));

    LOGFONTW lf{};
    lf.lfHeight  = -36;
    lf.lfWeight  = FW_NORMAL;
    lf.lfQuality = ANTIALIASED_QUALITY;       // 别用 ClearType: 会往 alpha 写垃圾
    wcscpy_s(lf.lfFaceName, L"Microsoft YaHei");
    HFONT font = CreateFontIndirectW(&lf);
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(20, 20, 20));
    SetTextAlign(dc, TA_LEFT | TA_TOP);

    g_charGranularity = (scene == 4);
    switch (scene) {
        case 1:  BuildSceneAngled(dc); break;
        case 2:  BuildSceneFan(dc);    break;
        case 3:  BuildScenePersp(dc);  break;
        case 4:  BuildSceneCjk(dc);    break;
        default: BuildSceneFlat(dc);   break;
    }

    GdiFlush();
    // GDI 文字输出不写 alpha (A=0) → 强制 A=255 (不透明图, premul == straight)
    std::vector<uint8_t> out(W * H * 4);
    memcpy(out.data(), bits, out.size());
    for (size_t i = 3; i < out.size(); i += 4) out[i] = 0xFF;

    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(font);
    DeleteObject(bmp);
    DeleteDC(dc);
    outW = W;
    outH = H;
    return out;
}

static const wchar_t* kSceneDesc[] = {
    L"水平文字 · 基线场景",
    L"整行带角度 +15° / -12° / +28° / 中文 -8° · 拍歪的照片",
    L"单词 0°→75° 递增 · 压测极端倾角",
    L"真透视梯形 · 近大远小, 四边互不平行",
    L"中文长句 · 字级坐标直接喂入 (CHAR 主路径)",
};

// 工具栏按钮高亮当前场景 + 状态栏文案
static void RefreshChrome() {
    for (int i = 0; i < kSceneCount; ++i)
        if (g_sceneBtns[i]) ui_button_set_type(g_sceneBtns[i], i == g_scene ? 1 : 0);
    if (g_status) {
        wchar_t buf[320];
        const int rot = ui_ocr_img_view_get_rotation(g_view);
        _snwprintf_s(buf, _countof(buf), _TRUNCATE,
                     L"%s   ·   %u 个字   ·   旋转 %d°   ·   拖动逐字选 / 双击选词 / "
                     L"三击选整行 / 单击清除 / R 旋转 / 空白拖动平移 / 滚轮缩放 / Ctrl+C 复制",
                     kSceneDesc[g_scene], ui_ocr_img_view_item_count(g_view), rot);
        ui_label_set_text(g_status, buf);
    }
}

// 换图语义: set_image 会清空旧文本与选区, 所以必须先图后文本。
static void LoadScene(int scene) {
    int w = 0, h = 0;
    std::vector<uint8_t> pixels = MakeSceneImage(scene, w, h);
    /* 集成示范: 务必检查返回值。尺寸超 GPU 纹理上限时会返 -2, 宿主该降采样
     * 而不是当作成功继续喂文本 —— 否则画面空白且无从排查。 */
    const int rc = ui_ocr_img_view_set_image(g_view, g_win, pixels.data(),
                                             (uint32_t)w, (uint32_t)h, 0);
    if (rc != 0) {
        g_lastImageError = rc;
        if (g_status) {
            wchar_t err[128];
            _snwprintf_s(err, _countof(err), _TRUNCATE,
                         L"加载图像失败 (错误码 %d) —— -2 表示尺寸超 GPU 纹理上限",
                         rc);
            ui_label_set_text(g_status, err);
        }
        return;
    }
    g_lastImageError = 0;

    std::vector<UiOcrTextItem> items;
    items.reserve(g_words.size());
    for (const DemoWord& dw : g_words) {
        UiOcrTextItem it{};
        it.text = dw.utf8.c_str();
        memcpy(it.quad, dw.quad, sizeof(it.quad));
        it.block = dw.block;
        it.line  = dw.line;
        it.word  = dw.word;
        it.flags = dw.flags;
        items.push_back(it);
    }
    ui_ocr_img_view_set_text(g_view, items.data(), (uint32_t)items.size(),
                             sizeof(UiOcrTextItem),
                             g_charGranularity ? UI_OCR_GRANULARITY_CHAR
                                               : UI_OCR_GRANULARITY_BLOCK);
    g_scene = scene;
    RefreshChrome();
}

// ---------------------------------------------------------------- UI 交互

static void OnSceneBtn(UiWidget w, void* userdata) {
    (void)w;
    LoadScene((int)(intptr_t)userdata);
}

static void OnKey(UiWindow win, int vk, void* userdata) {
    (void)win; (void)userdata;
    if (vk >= '1' && vk < '1' + kSceneCount) { LoadScene(vk - '1'); return; }
    switch (vk) {
        case 'F': case VK_HOME: ui_ocr_img_view_fit(g_view);   break;
        case '0':               ui_ocr_img_view_reset(g_view); break;
        /* R / Shift+R 顺逆时针转 90°，转完 fit 一下让整图仍可见 */
        case 'R': {
            const int step = (GetKeyState(VK_SHIFT) & 0x8000) ? -90 : 90;
            ui_ocr_img_view_set_rotation(g_view,
                ui_ocr_img_view_get_rotation(g_view) + step);
            ui_ocr_img_view_fit(g_view);
            RefreshChrome();
            break;
        }
        /* 左右方向键循环换图 */
        case VK_LEFT:  LoadScene((g_scene + kSceneCount - 1) % kSceneCount); break;
        case VK_RIGHT: LoadScene((g_scene + 1) % kSceneCount);               break;
        default: break;
    }
}

// ---------------------------------------------------------------- debug 命令

static void JsonEscapeInto(const std::string& s, std::string& out) {
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
}

static std::string ReadClipboardUtf8() {
    std::string out;
    if (!OpenClipboard(nullptr)) return out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* w = (const wchar_t*)GlobalLock(h)) {
            const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0,
                                              nullptr, nullptr);
            if (n > 1) {
                out.resize(n - 1);
                WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n,
                                    nullptr, nullptr);
            }
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

static int DemoCmdHandler(const char* cmd, const char* args,
                          char* out_buf, int out_cap, void* /*userdata*/) {
    std::string resp;
    char buf[256];
    if (strcmp(cmd, "sel") == 0) {
        resp = "{\"text\":\"";
        JsonEscapeInto(ui_ocr_img_view_selected_text(g_view), resp);
        resp += "\"}";
    } else if (strcmp(cmd, "copy") == 0) {
        resp = ui_ocr_img_view_copy_selection(g_view)
                   ? "{\"ok\":true}" : "{\"ok\":false}";
    } else if (strcmp(cmd, "clip") == 0) {
        resp = "{\"text\":\"";
        JsonEscapeInto(ReadClipboardUtf8(), resp);
        resp += "\"}";
    } else if (strcmp(cmd, "word_count") == 0) {
        /* fed = 喂进去的项数 (BLOCK 粒度下是块数); chars = lib 里实际生效的
         * 字数。BLOCK 场景下两者不同, 脚本要按 chars 理解选择粒度。 */
        snprintf(buf, sizeof(buf), "{\"fed\":%d,\"chars\":%u,\"granularity\":\"%s\"}",
                 (int)g_words.size(), ui_ocr_img_view_item_count(g_view),
                 g_charGranularity ? "char" : "block");
        resp = buf;
    } else if (strcmp(cmd, "oversize_probe") == 0) {
        /* 验证尺寸上限检查确实在【喂入时】生效: 声明一个超大宽度但只给一小块
         * 缓冲。检查若失效, lib 会按声明尺寸去读缓冲 → 越界崩; 检查生效则
         * 在碰缓冲之前就返回 -2。所以这条命令同时也是越界的护栏测试。
         * 之后重新载入当前场景恢复画面。 */
        static uint8_t tiny[4 * 4 * 4] = {};
        const int rc = ui_ocr_img_view_set_image(g_view, g_win, tiny,
                                                 100000u, 4u, 0u);
        snprintf(buf, sizeof(buf), "{\"rc\":%d}", rc);
        resp = buf;
        LoadScene(g_scene);
    } else if (strcmp(cmd, "rotate") == 0) {
        /* 绝对角度; 不带参数 = 只查询当前角度 */
        if (args && *args) {
            ui_ocr_img_view_set_rotation(g_view, atoi(args));
            ui_ocr_img_view_fit(g_view);
            RefreshChrome();
        }
        snprintf(buf, sizeof(buf), "{\"rotation\":%d}",
                 ui_ocr_img_view_get_rotation(g_view));
        resp = buf;
    } else if (strcmp(cmd, "word_edge") == 0) {
        /* 第 i 个【喂入项】的左(0)/右(1)边缘内侧一点, 窗口 DIP 坐标。
         * 字级拆分后 word_center 只落在块中间的某个字上, 想"整块选中"必须
         * 从块头拖到块尾 —— 这个命令给出那两个点。 */
        int i = -1, side = 0;
        if (args) sscanf(args, "%d %d", &i, &side);
        if (i < 0 || i >= (int)g_words.size()) {
            resp = "{\"error\":\"bad index\"}";
        } else {
            const float* q = g_words[i].quad;
            const float t = (side ? 0.94f : 0.06f);   // 边缘内侧, 避开边界
            const float topX = q[0] + (q[2] - q[0]) * t;
            const float topY = q[1] + (q[3] - q[1]) * t;
            const float botX = q[6] + (q[4] - q[6]) * t;
            const float botY = q[7] + (q[5] - q[7]) * t;
            float sx = 0, sy = 0;
            ui_ocr_img_view_image_to_screen(g_view, (topX + botX) * 0.5f,
                                            (topY + botY) * 0.5f, &sx, &sy);
            snprintf(buf, sizeof(buf), "{\"x\":%.1f,\"y\":%.1f}", sx, sy);
            resp = buf;
        }
    } else if (strcmp(cmd, "scene") == 0) {
        const int n = args ? atoi(args) : 0;
        if (n < 0 || n >= kSceneCount) {
            resp = "{\"error\":\"scene out of range\"}";
        } else {
            LoadScene(n);
            char nameU8[64];
            WideCharToMultiByte(CP_UTF8, 0, kSceneNames[n], -1, nameU8,
                                sizeof(nameU8), nullptr, nullptr);
            snprintf(buf, sizeof(buf), "{\"scene\":%d,\"name\":\"%s\",\"words\":%d}",
                     n, nameU8, (int)g_words.size());
            resp = buf;
        }
    } else if (strcmp(cmd, "quad_err") == 0) {
        /* 量化高亮的平行四边形近似误差。widget 用 TL/TR/BL 三点定义高亮,
         * 隐含 BR' = TR + BL - TL。真 quad 的 BR 与之相差多少 (图像 px,
         * 以及占 quad 对角线的百分比) 就是这条命令的答案。
         * 平行四边形 (含任意旋转矩形) 恒为 0; 梯形/透视 quad 才非 0。 */
        const int i = args ? atoi(args) : -1;
        if (i < 0 || i >= (int)g_words.size()) {
            resp = "{\"error\":\"bad index\"}";
        } else {
            const float* q = g_words[i].quad;
            const float bpx = q[2] + q[6] - q[0];   // TR + BL - TL
            const float bpy = q[3] + q[7] - q[1];
            const float dx = q[4] - bpx, dy = q[5] - bpy;
            const float err = std::sqrt(dx * dx + dy * dy);
            const float diagx = q[4] - q[0], diagy = q[5] - q[1];
            const float diag = std::sqrt(diagx * diagx + diagy * diagy);
            snprintf(buf, sizeof(buf),
                     "{\"err_px\":%.2f,\"diag_px\":%.2f,\"err_pct\":%.2f,"
                     "\"text\":\"%s\"}",
                     err, diag, diag > 0 ? err / diag * 100.0f : 0.0f,
                     g_words[i].utf8.c_str());
            resp = buf;
        }
    } else if (strcmp(cmd, "word_quad") == 0) {
        /* 第 i 个词 quad 的 4 顶点(窗口 DIP)。脚本据此算"AABB 内 / 四边形外"
         * 的探针点, 验证命中走的是真四边形而不是包围盒。 */
        const int i = args ? atoi(args) : -1;
        if (i < 0 || i >= (int)g_words.size()) {
            resp = "{\"error\":\"bad index\"}";
        } else {
            const float* q = g_words[i].quad;
            float s[8];
            for (int k = 0; k < 4; ++k)
                ui_ocr_img_view_image_to_screen(g_view, q[k * 2], q[k * 2 + 1],
                                                &s[k * 2], &s[k * 2 + 1]);
            snprintf(buf, sizeof(buf),
                     "{\"tl\":[%.1f,%.1f],\"tr\":[%.1f,%.1f],"
                     "\"br\":[%.1f,%.1f],\"bl\":[%.1f,%.1f]}",
                     s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]);
            resp = buf;
        }
    } else if (strcmp(cmd, "word_center") == 0) {
        const int i = args ? atoi(args) : -1;
        if (i < 0 || i >= (int)g_words.size()) {
            resp = "{\"error\":\"bad index\"}";
        } else {
            const float* q = g_words[i].quad;
            const float cx = (q[0] + q[2] + q[4] + q[6]) * 0.25f;
            const float cy = (q[1] + q[3] + q[5] + q[7]) * 0.25f;
            float sx = 0, sy = 0;
            ui_ocr_img_view_image_to_screen(g_view, cx, cy, &sx, &sy);
            snprintf(buf, sizeof(buf), "{\"x\":%.1f,\"y\":%.1f,\"text\":\"%s\"}",
                     sx, sy, g_words[i].utf8.c_str());
            resp = buf;
        }
    } else {
        return 0;   // 回退 builtin
    }
    if ((int)resp.size() >= out_cap) return -1;
    memcpy(out_buf, resp.c_str(), resp.size() + 1);
    return (int)resp.size();
}

// ---------------------------------------------------------------- main

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    ui_init_with_theme(UI_THEME_LIGHT);

    UiWindowConfig wc{};
    wc.title        = L"ocr_demo — 图片划词";
    wc.width        = 1000;
    wc.height       = 700;
    wc.resizable    = 1;
    wc.system_frame = 1;
    g_win = ui_window_create(&wc);

    UiWidget root = ui_vbox();

    /* 工具栏: 4 个场景按钮, 当前场景高亮 (primary) */
    UiWidget bar = ui_hbox();
    ui_widget_set_padding(bar, 12, 10, 12, 10);
    ui_widget_set_gap(bar, 8);
    static const wchar_t* kBtnLabels[] = { L"1 水平", L"2 倾斜", L"3 扇形",
                                           L"4 透视", L"5 中文字级" };
    static const char*    kBtnIds[]    = { "btn_flat", "btn_angled", "btn_fan",
                                           "btn_persp", "btn_cjk" };
    for (int i = 0; i < kSceneCount; ++i) {
        UiWidget b = ui_button(kBtnLabels[i]);
        ui_widget_set_id(b, kBtnIds[i]);
        ui_widget_on_click(b, OnSceneBtn, (void*)(intptr_t)i);
        ui_widget_add_child(bar, b);
        g_sceneBtns[i] = b;
    }
    ui_widget_add_child(bar, ui_spacer(0));   /* 0 = 弹性, 把右侧按钮推到末尾 */
    UiWidget fitBtn = ui_button(L"F 适应窗口");
    ui_widget_set_id(fitBtn, "btn_fit");
    ui_widget_on_click(fitBtn, [](UiWidget, void*) { ui_ocr_img_view_fit(g_view); }, nullptr);
    ui_widget_add_child(bar, fitBtn);
    UiWidget oneBtn = ui_button(L"0 原始大小");
    ui_widget_set_id(oneBtn, "btn_reset");
    ui_widget_on_click(oneBtn, [](UiWidget, void*) { ui_ocr_img_view_reset(g_view); }, nullptr);
    ui_widget_add_child(bar, oneBtn);
    UiWidget rotBtn = ui_button(L"R 旋转 90°");
    ui_widget_set_id(rotBtn, "btn_rotate");
    ui_widget_on_click(rotBtn, [](UiWidget, void*) {
        ui_ocr_img_view_set_rotation(g_view,
            ui_ocr_img_view_get_rotation(g_view) + 90);
        ui_ocr_img_view_fit(g_view);
        RefreshChrome();
    }, nullptr);
    ui_widget_add_child(bar, rotBtn);
    ui_widget_add_child(root, bar);

    g_view = ui_ocr_img_view();
    ui_widget_set_id(g_view, "ocr");
    ui_widget_set_expand(g_view, 1);
    ui_widget_add_child(root, g_view);

    /* 状态栏: 当前场景说明 + 操作提示 */
    g_status = ui_label(L"");
    ui_widget_set_id(g_status, "status");
    ui_label_set_font_size(g_status, 12.0f);
    ui_label_set_text_color(g_status, (UiColor){ 0.45f, 0.45f, 0.48f, 1.0f });
    ui_widget_set_padding(g_status, 12, 6, 12, 10);
    ui_widget_add_child(root, g_status);

    ui_window_set_root(g_win, root);
    ui_window_on_key(g_win, OnKey, nullptr);

    LoadScene(0);

    ui_debug_server_set_handler(DemoCmdHandler, nullptr);
    ui_debug_server_start(g_win, "ocr_demo");   // pipe 名独占本 demo

    ui_window_show(g_win);
    const int rc = ui_run();
    ui_debug_server_stop();
    return rc;
}
