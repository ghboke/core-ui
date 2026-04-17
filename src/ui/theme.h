#pragma once
#include <d2d1.h>

/* wingdi.h defines RGBA macro which conflicts with our Rgba function */
#ifdef RGBA
#undef RGBA
#endif

namespace theme {

// ============================================================================
// Fluent 2 Design Tokens — based on Microsoft official @fluentui/tokens
// Source: github.com/microsoft/fluentui  packages/tokens/src/
// ============================================================================

// ---- Helper: construct D2D1_COLOR_F from 0-255 int RGB ----
constexpr D2D1_COLOR_F Rgb(int r, int g, int b) {
    return { r / 255.0f, g / 255.0f, b / 255.0f, 1.0f };
}
constexpr D2D1_COLOR_F Rgba(int r, int g, int b, float a) {
    return { r / 255.0f, g / 255.0f, b / 255.0f, a };
}

// ---- Brand Color Ramp (default: Fluent Blue) ----
namespace brand {
    constexpr D2D1_COLOR_F shade50  = Rgb(0x06, 0x17, 0x24);  // #061724
    constexpr D2D1_COLOR_F shade40  = Rgb(0x08, 0x23, 0x38);  // #082338
    constexpr D2D1_COLOR_F shade30  = Rgb(0x0A, 0x2E, 0x4A);  // #0a2e4a
    constexpr D2D1_COLOR_F shade20  = Rgb(0x0C, 0x3B, 0x5E);  // #0c3b5e
    constexpr D2D1_COLOR_F shade10  = Rgb(0x0E, 0x47, 0x75);  // #0e4775
    constexpr D2D1_COLOR_F primary  = Rgb(0x0F, 0x6C, 0xBD);  // #0f6cbd  brand[80]
    constexpr D2D1_COLOR_F tint10   = Rgb(0x28, 0x86, 0xDE);  // #2886de
    constexpr D2D1_COLOR_F tint20   = Rgb(0x47, 0x9E, 0xF5);  // #479ef5
    constexpr D2D1_COLOR_F tint30   = Rgb(0x62, 0xAB, 0xF5);  // #62abf5
    constexpr D2D1_COLOR_F tint40   = Rgb(0x77, 0xB7, 0xF7);  // #77b7f7
    constexpr D2D1_COLOR_F tint50   = Rgb(0x96, 0xC6, 0xFA);  // #96c6fa
    constexpr D2D1_COLOR_F tint60   = Rgb(0xEB, 0xF3, 0xFC);  // #ebf3fc
}

// ---- Grey Scale (key values from Fluent 2) ----
namespace grey {
    constexpr D2D1_COLOR_F g2   = Rgb(0x05, 0x05, 0x05);  // #050505
    constexpr D2D1_COLOR_F g4   = Rgb(0x0A, 0x0A, 0x0A);  // #0a0a0a
    constexpr D2D1_COLOR_F g6   = Rgb(0x0F, 0x0F, 0x0F);  // #0f0f0f
    constexpr D2D1_COLOR_F g8   = Rgb(0x14, 0x14, 0x14);  // #141414
    constexpr D2D1_COLOR_F g10  = Rgb(0x1A, 0x1A, 0x1A);  // #1a1a1a
    constexpr D2D1_COLOR_F g12  = Rgb(0x1F, 0x1F, 0x1F);  // #1f1f1f
    constexpr D2D1_COLOR_F g14  = Rgb(0x24, 0x24, 0x24);  // #242424
    constexpr D2D1_COLOR_F g16  = Rgb(0x29, 0x29, 0x29);  // #292929
    constexpr D2D1_COLOR_F g18  = Rgb(0x2E, 0x2E, 0x2E);  // #2e2e2e
    constexpr D2D1_COLOR_F g20  = Rgb(0x33, 0x33, 0x33);  // #333333
    constexpr D2D1_COLOR_F g22  = Rgb(0x38, 0x38, 0x38);  // #383838
    constexpr D2D1_COLOR_F g24  = Rgb(0x3D, 0x3D, 0x3D);  // #3d3d3d
    constexpr D2D1_COLOR_F g26  = Rgb(0x42, 0x42, 0x42);  // #424242
    constexpr D2D1_COLOR_F g28  = Rgb(0x47, 0x47, 0x47);  // #474747
    constexpr D2D1_COLOR_F g30  = Rgb(0x4D, 0x4D, 0x4D);  // #4d4d4d
    constexpr D2D1_COLOR_F g36  = Rgb(0x5C, 0x5C, 0x5C);  // #5c5c5c
    constexpr D2D1_COLOR_F g38  = Rgb(0x61, 0x61, 0x61);  // #616161
    constexpr D2D1_COLOR_F g44  = Rgb(0x70, 0x70, 0x70);  // #707070
    constexpr D2D1_COLOR_F g50  = Rgb(0x80, 0x80, 0x80);  // #808080
    constexpr D2D1_COLOR_F g60  = Rgb(0x99, 0x99, 0x99);  // #999999
    constexpr D2D1_COLOR_F g68  = Rgb(0xAD, 0xAD, 0xAD);  // #adadad
    constexpr D2D1_COLOR_F g74  = Rgb(0xBD, 0xBD, 0xBD);  // #bdbdbd
    constexpr D2D1_COLOR_F g78  = Rgb(0xC7, 0xC7, 0xC7);  // #c7c7c7
    constexpr D2D1_COLOR_F g82  = Rgb(0xD1, 0xD1, 0xD1);  // #d1d1d1
    constexpr D2D1_COLOR_F g84  = Rgb(0xD6, 0xD6, 0xD6);  // #d6d6d6
    constexpr D2D1_COLOR_F g86  = Rgb(0xDB, 0xDB, 0xDB);  // #dbdbdb
    constexpr D2D1_COLOR_F g88  = Rgb(0xE0, 0xE0, 0xE0);  // #e0e0e0
    constexpr D2D1_COLOR_F g90  = Rgb(0xE6, 0xE6, 0xE6);  // #e6e6e6
    constexpr D2D1_COLOR_F g92  = Rgb(0xEB, 0xEB, 0xEB);  // #ebebeb
    constexpr D2D1_COLOR_F g94  = Rgb(0xF0, 0xF0, 0xF0);  // #f0f0f0
    constexpr D2D1_COLOR_F g96  = Rgb(0xF5, 0xF5, 0xF5);  // #f5f5f5
    constexpr D2D1_COLOR_F g98  = Rgb(0xFA, 0xFA, 0xFA);  // #fafafa
}

constexpr D2D1_COLOR_F white = { 1.0f, 1.0f, 1.0f, 1.0f };
constexpr D2D1_COLOR_F black = { 0.0f, 0.0f, 0.0f, 1.0f };
constexpr D2D1_COLOR_F transparent = { 0.0f, 0.0f, 0.0f, 0.0f };

// ---- Status Colors ----
namespace status {
    constexpr D2D1_COLOR_F danger       = Rgb(0xD1, 0x34, 0x38);  // #d13438
    constexpr D2D1_COLOR_F success      = Rgb(0x10, 0x7C, 0x10);  // #107c10
    constexpr D2D1_COLOR_F warning      = Rgb(0xFD, 0xE3, 0x00);  // #fde300
    constexpr D2D1_COLOR_F info         = Rgb(0x00, 0x78, 0xD4);  // #0078d4
}

// ============================================================================
// Theme mode
// ============================================================================
enum class Mode { Dark, Light };

// ============================================================================
// Semantic Color Tokens — Fluent 2 alias tokens mapped per theme
// ============================================================================
struct Colors {
    // ---- Backgrounds ----
    D2D1_COLOR_F titleBarBg;
    D2D1_COLOR_F titleBarText;
    D2D1_COLOR_F windowBg;            // neutralBackground1
    D2D1_COLOR_F windowBorder;        // neutralStroke1
    D2D1_COLOR_F toolbarBg;           // neutralBackground3
    D2D1_COLOR_F statusBarBg;         // neutralBackground2
    D2D1_COLOR_F statusBarText;       // neutralForeground3
    D2D1_COLOR_F contentBg;           // neutralBackground1
    D2D1_COLOR_F contentText;         // neutralForeground2

    // ---- Buttons ----
    D2D1_COLOR_F btnNormal;           // subtleBackground
    D2D1_COLOR_F btnHover;            // subtleBackgroundHover
    D2D1_COLOR_F btnPress;            // subtleBackgroundPressed
    D2D1_COLOR_F btnText;             // neutralForeground1
    D2D1_COLOR_F closeBtnHover;       // status danger
    D2D1_COLOR_F closeBtnPress;       // status danger darker

    // ---- Accent / Brand ----
    D2D1_COLOR_F accent;              // brandBackground
    D2D1_COLOR_F accentHover;         // brandBackgroundHover
    D2D1_COLOR_F accentPress;         // brandBackgroundPressed
    D2D1_COLOR_F accentText;          // brandForeground1
    D2D1_COLOR_F accentSelected;      // brandBackgroundSelected

    // ---- Sidebar / Navigation ----
    D2D1_COLOR_F sidebarBg;           // neutralBackground2
    D2D1_COLOR_F sidebarItemHover;    // subtleBackgroundHover
    D2D1_COLOR_F sidebarText;         // neutralForeground2

    // ---- Divider / Stroke ----
    D2D1_COLOR_F divider;             // neutralStroke1
    D2D1_COLOR_F dividerSubtle;       // neutralStroke2 (lighter)

    // ---- Input ----
    D2D1_COLOR_F inputBg;             // neutralBackground1 (light) / neutralBackground3 (dark)
    D2D1_COLOR_F inputBorder;         // neutralStroke1
    D2D1_COLOR_F inputBorderHover;    // neutralStroke1Hover
    D2D1_COLOR_F inputBorderFocus;    // brandStroke1

    // ---- Card / Surface ----
    D2D1_COLOR_F cardBg;              // neutralCardBackground
    D2D1_COLOR_F cardBorder;          // neutralStroke1

    // ---- Disabled ----
    D2D1_COLOR_F disabledBg;          // neutralBackgroundDisabled
    D2D1_COLOR_F disabledText;        // neutralForegroundDisabled

    // ---- Foreground layers ----
    D2D1_COLOR_F foreground1;         // neutralForeground1 (primary text)
    D2D1_COLOR_F foreground2;         // neutralForeground2
    D2D1_COLOR_F foreground3;         // neutralForeground3 (tertiary)
    D2D1_COLOR_F foreground4;         // neutralForeground4 (quaternary)
    D2D1_COLOR_F foregroundOnBrand;   // white on brand background

    // ---- Background layers ----
    D2D1_COLOR_F background1;         // neutralBackground1
    D2D1_COLOR_F background2;         // neutralBackground2
    D2D1_COLOR_F background3;         // neutralBackground3
    D2D1_COLOR_F background4;         // neutralBackground4
    D2D1_COLOR_F background5;         // neutralBackground5

    // ---- Shadow colors ----
    D2D1_COLOR_F shadowAmbient;       // neutralShadowAmbient
    D2D1_COLOR_F shadowKey;           // neutralShadowKey
};

// ============================================================================
// Fluent 2 Dark Theme — official alias tokens
// ============================================================================
inline Colors MakeDark() {
    return {
        // titleBarBg              — neutralBackground2
        grey::g12,
        // titleBarText            — neutralForeground1
        white,
        // windowBg                — neutralBackground1
        grey::g16,
        // windowBorder            — neutralStroke1
        grey::g28,
        // toolbarBg               — neutralBackground3
        grey::g8,
        // statusBarBg             — neutralBackground2
        grey::g12,
        // statusBarText           — neutralForeground3
        grey::g68,
        // contentBg               — neutralBackground1
        grey::g16,
        // contentText             — neutralForeground2
        grey::g84,

        // btnNormal               — subtleBackground (transparent)
        transparent,
        // btnHover                — subtleBackgroundHover
        Rgba(0xFF, 0xFF, 0xFF, 0.08f),
        // btnPress                — subtleBackgroundPressed
        Rgba(0xFF, 0xFF, 0xFF, 0.04f),
        // btnText                 — neutralForeground1
        white,
        // closeBtnHover
        status::danger,
        // closeBtnPress
        Rgb(0xA4, 0x26, 0x2C),

        // accent                  — brandBackground (brand[70] in dark)
        Rgb(0x11, 0x5E, 0xA3),
        // accentHover             — brandBackgroundHover
        Rgb(0x0F, 0x6C, 0xBD),
        // accentPress             — brandBackgroundPressed
        Rgb(0x0E, 0x47, 0x75),
        // accentText              — brandForeground1 (brand[100] in dark)
        brand::tint20,
        // accentSelected          — brandBackgroundSelected
        Rgb(0x0F, 0x54, 0x8C),

        // sidebarBg               — neutralBackground2
        grey::g12,
        // sidebarItemHover        — subtleBackgroundHover
        Rgba(0xFF, 0xFF, 0xFF, 0.08f),
        // sidebarText             — neutralForeground2
        grey::g84,

        // divider                 — neutralStroke1 (grey[40])
        grey::g26,
        // dividerSubtle           — neutralStroke2
        grey::g22,

        // inputBg                 — neutralBackground3
        grey::g8,
        // inputBorder             — neutralStroke1
        grey::g26,
        // inputBorderHover
        grey::g30,
        // inputBorderFocus        — brandStroke1 (brand[100] in dark)
        brand::tint20,

        // cardBg                  — neutralCardBackground
        grey::g20,
        // cardBorder
        grey::g26,

        // disabledBg
        grey::g8,
        // disabledText            — neutralForegroundDisabled
        grey::g36,

        // foreground1             — neutralForeground1
        white,
        // foreground2
        grey::g84,
        // foreground3
        grey::g68,
        // foreground4
        grey::g60,
        // foregroundOnBrand
        white,

        // background1-5
        grey::g16,
        grey::g12,
        grey::g8,
        grey::g4,
        grey::g2,

        // shadowAmbient
        Rgba(0, 0, 0, 0.24f),
        // shadowKey
        Rgba(0, 0, 0, 0.28f),
    };
}

// ============================================================================
// Fluent 2 Light Theme — official alias tokens
// ============================================================================
inline Colors MakeLight() {
    return {
        // titleBarBg              — neutralBackground2
        grey::g98,
        // titleBarText            — neutralForeground1
        grey::g14,
        // windowBg                — neutralBackground1
        white,
        // windowBorder            — neutralStroke1
        grey::g82,
        // toolbarBg               — neutralBackground3
        grey::g96,
        // statusBarBg             — neutralBackground2
        grey::g98,
        // statusBarText           — neutralForeground3
        grey::g38,
        // contentBg               — neutralBackground1
        white,
        // contentText             — neutralForeground2
        grey::g26,

        // btnNormal               — subtleBackground (transparent)
        transparent,
        // btnHover                — subtleBackgroundHover
        grey::g96,
        // btnPress                — subtleBackgroundPressed
        grey::g88,
        // btnText                 — neutralForeground1
        grey::g14,
        // closeBtnHover
        status::danger,
        // closeBtnPress
        Rgb(0xA4, 0x26, 0x2C),

        // accent                  — brandBackground (brand[80] in light)
        brand::primary,
        // accentHover             — brandBackgroundHover
        Rgb(0x11, 0x5E, 0xA3),
        // accentPress             — brandBackgroundPressed
        Rgb(0x0C, 0x3B, 0x5E),
        // accentText              — brandForeground1 (brand[80] in light)
        brand::primary,
        // accentSelected          — brandBackgroundSelected
        Rgb(0x0F, 0x54, 0x8C),

        // sidebarBg               — neutralBackground2
        grey::g98,
        // sidebarItemHover        — subtleBackgroundHover
        grey::g96,
        // sidebarText             — neutralForeground2
        grey::g26,

        // divider                 — neutralStroke1
        grey::g82,
        // dividerSubtle           — neutralStroke2
        grey::g88,

        // inputBg                 — neutralBackground1
        white,
        // inputBorder
        grey::g82,
        // inputBorderHover
        grey::g78,
        // inputBorderFocus        — brandStroke1
        brand::primary,

        // cardBg                  — neutralCardBackground
        white,
        // cardBorder
        grey::g82,

        // disabledBg
        grey::g94,
        // disabledText            — neutralForegroundDisabled
        grey::g74,

        // foreground1             — neutralForeground1
        grey::g14,
        // foreground2
        grey::g26,
        // foreground3
        grey::g38,
        // foreground4
        grey::g44,
        // foregroundOnBrand
        white,

        // background1-5
        white,
        grey::g98,
        grey::g96,
        grey::g94,
        grey::g92,

        // shadowAmbient
        Rgba(0, 0, 0, 0.12f),
        // shadowKey
        Rgba(0, 0, 0, 0.14f),
    };
}

// ============================================================================
// Global current theme (singleton)
// ============================================================================
inline Colors& Current() {
    static Colors colors = MakeLight();
    return colors;
}

inline Mode& CurrentMode() {
    static Mode mode = Mode::Light;
    return mode;
}

inline void SetMode(Mode m) {
    CurrentMode() = m;
    Current() = (m == Mode::Light) ? MakeLight() : MakeDark();
}

inline bool IsDark() { return CurrentMode() == Mode::Dark; }

// ============================================================================
// Backward-compatible accessors (used by all existing controls)
// ============================================================================
#define THEME_COLOR(name) (theme::Current().name)

inline const D2D1_COLOR_F& kTitleBarBg()      { return Current().titleBarBg; }
inline const D2D1_COLOR_F& kTitleBarText()     { return Current().titleBarText; }
inline const D2D1_COLOR_F& kWindowBg()         { return Current().windowBg; }
inline const D2D1_COLOR_F& kWindowBorder()     { return Current().windowBorder; }
inline const D2D1_COLOR_F& kToolbarBg()        { return Current().toolbarBg; }
inline const D2D1_COLOR_F& kStatusBarBg()      { return Current().statusBarBg; }
inline const D2D1_COLOR_F& kStatusBarText()    { return Current().statusBarText; }
inline const D2D1_COLOR_F& kContentBg()        { return Current().contentBg; }
inline const D2D1_COLOR_F& kContentText()      { return Current().contentText; }
inline const D2D1_COLOR_F& kBtnNormal()        { return Current().btnNormal; }
inline const D2D1_COLOR_F& kBtnHover()         { return Current().btnHover; }
inline const D2D1_COLOR_F& kBtnPress()         { return Current().btnPress; }
inline const D2D1_COLOR_F& kBtnText()          { return Current().btnText; }
inline const D2D1_COLOR_F& kCloseBtnHover()    { return Current().closeBtnHover; }
inline const D2D1_COLOR_F& kCloseBtnPress()    { return Current().closeBtnPress; }
inline const D2D1_COLOR_F& kAccent()           { return Current().accent; }
inline const D2D1_COLOR_F& kAccentHover()      { return Current().accentHover; }
inline const D2D1_COLOR_F& kSidebarBg()        { return Current().sidebarBg; }
inline const D2D1_COLOR_F& kSidebarItemHover() { return Current().sidebarItemHover; }
inline const D2D1_COLOR_F& kSidebarText()      { return Current().sidebarText; }
inline const D2D1_COLOR_F& kDivider()          { return Current().divider; }
inline const D2D1_COLOR_F& kInputBg()          { return Current().inputBg; }
inline const D2D1_COLOR_F& kInputBorder()      { return Current().inputBorder; }

// ============================================================================
// Typography — Fluent 2 Type Ramp
// ============================================================================
constexpr wchar_t kFontFamily[]    = L"Segoe UI";

// Font sizes (Fluent 2 official)
constexpr float kFontSizeCaption2  =  10.0f;   // caption2
constexpr float kFontSizeCaption   =  12.0f;   // caption1
constexpr float kFontSizeBody      =  14.0f;   // body1 (Fluent 2 default body)
constexpr float kFontSizeBody2     =  16.0f;   // body2
constexpr float kFontSizeSubtitle  =  20.0f;   // subtitle1
constexpr float kFontSizeTitle3    =  24.0f;   // title3
constexpr float kFontSizeTitle2    =  28.0f;   // title2
constexpr float kFontSizeTitle1    =  32.0f;   // title1
constexpr float kFontSizeLarge     =  40.0f;   // largeTitle
constexpr float kFontSizeDisplay   =  68.0f;   // display

// Backward-compatible aliases (map old names to Fluent 2)
constexpr float kFontSizeTitle     =  14.0f;   // was 13 → now body1 (primary UI text)
constexpr float kFontSizeNormal    =  14.0f;   // was 12 → now body1
constexpr float kFontSizeSmall     =  12.0f;   // was 11 → now caption1
constexpr float kFontSizeIcon      =  10.0f;   // was 10 → caption2

// ============================================================================
// Spacing — Fluent 2 spacing scale
// ============================================================================
namespace spacing {
    constexpr float none  =  0.0f;
    constexpr float xxs   =  2.0f;
    constexpr float xs    =  4.0f;
    constexpr float sNudge=  6.0f;
    constexpr float s     =  8.0f;
    constexpr float mNudge= 10.0f;
    constexpr float m     = 12.0f;
    constexpr float l     = 16.0f;
    constexpr float xl    = 20.0f;
    constexpr float xxl   = 24.0f;
    constexpr float xxxl  = 32.0f;
}

// ============================================================================
// Border Radius — Fluent 2
// ============================================================================
namespace radius {
    constexpr float none    =  0.0f;
    constexpr float small   =  2.0f;
    constexpr float medium  =  4.0f;   // standard controls
    constexpr float large   =  6.0f;
    constexpr float xLarge  =  8.0f;   // cards, dialogs
    constexpr float xxLarge = 12.0f;
    constexpr float circular = 9999.0f;
}

// ============================================================================
// Stroke Widths — Fluent 2
// ============================================================================
namespace stroke {
    constexpr float thin      = 1.0f;
    constexpr float thick     = 2.0f;
    constexpr float thicker   = 3.0f;
    constexpr float thickest  = 4.0f;
}

// ============================================================================
// Elevation / Shadow — Fluent 2 (6 levels, each: ambient + key)
// shadow{N}: ambient = 0 0 2px color, key = 0 {N/2}px {N}px color
// Colors from theme: Current().shadowAmbient / shadowKey
// ============================================================================
struct Shadow {
    float ambientBlur;    // ambient blur radius
    float keyOffsetY;     // key shadow Y offset
    float keyBlur;        // key shadow blur radius
};

namespace shadow {
    constexpr Shadow s2   = {  2.0f,  1.0f,   2.0f };
    constexpr Shadow s4   = {  2.0f,  2.0f,   4.0f };
    constexpr Shadow s8   = {  2.0f,  4.0f,   8.0f };
    constexpr Shadow s16  = {  2.0f,  8.0f,  16.0f };
    constexpr Shadow s28  = {  8.0f, 14.0f,  28.0f };
    constexpr Shadow s64  = {  8.0f, 32.0f,  64.0f };
}

// ============================================================================
// Motion — Fluent 2 durations & curves
// ============================================================================
namespace duration {
    constexpr float ultraFast =  50.0f;   // ms
    constexpr float faster    = 100.0f;
    constexpr float fast      = 150.0f;
    constexpr float normal    = 200.0f;   // standard interactions
    constexpr float gentle    = 250.0f;
    constexpr float slow      = 300.0f;
    constexpr float slower    = 400.0f;
    constexpr float ultraSlow = 500.0f;
}

// Cubic bezier control points: {x1, y1, x2, y2}
struct CubicBezier { float x1, y1, x2, y2; };

namespace curve {
    constexpr CubicBezier decelerateMax = { 0.1f,  0.9f,  0.2f, 1.0f };
    constexpr CubicBezier decelerateMid = { 0.0f,  0.0f,  0.0f, 1.0f };
    constexpr CubicBezier decelerateMin = { 0.33f, 0.0f,  0.1f, 1.0f };
    constexpr CubicBezier accelerateMax = { 0.9f,  0.1f,  1.0f, 0.2f };
    constexpr CubicBezier accelerateMid = { 1.0f,  0.0f,  1.0f, 1.0f };
    constexpr CubicBezier accelerateMin = { 0.8f,  0.0f,  0.78f,1.0f };
    constexpr CubicBezier easyEaseMax   = { 0.8f,  0.0f,  0.2f, 1.0f };
    constexpr CubicBezier easyEase      = { 0.33f, 0.0f,  0.67f,1.0f };
    constexpr CubicBezier linear        = { 0.0f,  0.0f,  1.0f, 1.0f };
}

// ============================================================================
// Layout Sizes (backward-compatible)
// ============================================================================
constexpr float kTitleBarHeight    = 36.0f;
constexpr float kToolbarHeight     = 44.0f;
constexpr float kStatusBarHeight   = 28.0f;
constexpr float kSidebarWidth      = 200.0f;
constexpr float kBorderWidth       = 1.0f;
constexpr float kResizeBorder      = 6.0f;
constexpr float kCornerRadius      = radius::xLarge;      // 8px — window/dialog
constexpr float kBtnCornerRadius   = radius::medium;      // 4px — buttons/controls

constexpr float kCaptionBtnWidth   = 46.0f;

// ---- Window defaults ----
constexpr int kDefaultWidth        = 1024;
constexpr int kDefaultHeight       = 680;
constexpr int kMinWidth            = 480;
constexpr int kMinHeight           = 360;

} // namespace theme
