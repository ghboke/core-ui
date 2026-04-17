#include <cstdio>
#include <cmath>
#include <cassert>
#include <memory>
#include "../../src/ui/renderer.h"
#include "../../src/ui/event.h"
#include "../../src/ui/widget.h"

// Stub out Renderer::FillRect (only symbol referenced by Widget::OnDraw)
namespace ui {
    void Renderer::FillRect(const D2D1_RECT_F&, const D2D1_COLOR_F&) {}
}

using namespace ui;

static bool feq(float a, float b, float eps = 0.5f) {
    return std::fabs(a - b) < eps;
}

static float W(const Widget* w) { return w->rect.right - w->rect.left; }
static float H(const Widget* w) { return w->rect.bottom - w->rect.top; }
static float X(const Widget* w) { return w->rect.left; }
static float Y(const Widget* w) { return w->rect.top; }

// ---- Test 1: min/max constraints ----
static void test_minmax() {
    auto vbox = std::make_shared<VBoxWidget>();
    vbox->gap_ = 0;
    vbox->rect = {0, 0, 400, 400};

    // Child wants to expand but has maxH=100
    auto c1 = std::make_shared<Widget>();
    c1->expanding = true;
    c1->maxH = 100;
    vbox->AddChild(c1);

    // Child wants fixedH=10 but has minH=50
    auto c2 = std::make_shared<Widget>();
    c2->fixedH = 10;
    c2->minH = 50;
    vbox->AddChild(c2);

    vbox->DoLayout();

    assert(H(c1.get()) <= 100.0f && "c1 should be clamped to maxH=100");
    assert(H(c2.get()) >= 50.0f  && "c2 should be clamped to minH=50");
    printf("  [PASS] min/max constraints\n");
}

// ---- Test 2: flex weight ----
static void test_flex_weight() {
    auto hbox = std::make_shared<HBoxWidget>();
    hbox->gap_ = 0;
    hbox->rect = {0, 0, 300, 100};

    // flex=1
    auto c1 = std::make_shared<Widget>();
    c1->expanding = true;
    c1->flex = 1.0f;
    hbox->AddChild(c1);

    // flex=2
    auto c2 = std::make_shared<Widget>();
    c2->expanding = true;
    c2->flex = 2.0f;
    hbox->AddChild(c2);

    hbox->DoLayout();

    assert(feq(W(c1.get()), 100) && "flex=1 should get 100px of 300");
    assert(feq(W(c2.get()), 200) && "flex=2 should get 200px of 300");
    printf("  [PASS] flex weight (1:2 ratio)\n");
}

// ---- Test 3: cross-axis alignment ----
static void test_cross_align() {
    auto hbox = std::make_shared<HBoxWidget>();
    hbox->gap_ = 0;
    hbox->crossAlign_ = LayoutAlign::Center;
    hbox->rect = {0, 0, 300, 100};

    auto c1 = std::make_shared<Widget>();
    c1->fixedW = 100;
    c1->fixedH = 40;
    hbox->AddChild(c1);

    hbox->DoLayout();

    // Centered vertically: (100 - 40) / 2 = 30
    assert(feq(Y(c1.get()), 30) && "center cross-align should be at y=30");
    assert(feq(H(c1.get()), 40) && "height should stay 40");
    printf("  [PASS] cross-axis center alignment\n");
}

// ---- Test 4: main-axis justify center ----
static void test_justify_center() {
    auto vbox = std::make_shared<VBoxWidget>();
    vbox->gap_ = 0;
    vbox->mainJustify_ = LayoutJustify::Center;
    vbox->rect = {0, 0, 200, 400};

    auto c1 = std::make_shared<Widget>();
    c1->fixedW = 200;
    c1->fixedH = 100;
    vbox->AddChild(c1);

    vbox->DoLayout();

    // Centered: (400 - 100) / 2 = 150
    assert(feq(Y(c1.get()), 150) && "justify center should place at y=150");
    printf("  [PASS] main-axis justify center\n");
}

// ---- Test 5: main-axis justify space-between ----
static void test_justify_space_between() {
    auto vbox = std::make_shared<VBoxWidget>();
    vbox->gap_ = 0;
    vbox->mainJustify_ = LayoutJustify::SpaceBetween;
    vbox->rect = {0, 0, 200, 300};

    auto c1 = std::make_shared<Widget>();
    c1->fixedW = 200; c1->fixedH = 50;
    vbox->AddChild(c1);

    auto c2 = std::make_shared<Widget>();
    c2->fixedW = 200; c2->fixedH = 50;
    vbox->AddChild(c2);

    auto c3 = std::make_shared<Widget>();
    c3->fixedW = 200; c3->fixedH = 50;
    vbox->AddChild(c3);

    vbox->DoLayout();

    // Total content = 150, free = 150, between 3 items = 2 gaps of 75
    assert(feq(Y(c1.get()), 0)   && "first item at top");
    assert(feq(Y(c2.get()), 125) && "second item at 50+75=125");
    assert(feq(Y(c3.get()), 250) && "third item at 300-50=250");
    printf("  [PASS] main-axis justify space-between\n");
}

// ---- Test 6: margin ----
static void test_margin() {
    auto vbox = std::make_shared<VBoxWidget>();
    vbox->gap_ = 0;
    vbox->rect = {0, 0, 200, 400};

    auto c1 = std::make_shared<Widget>();
    c1->fixedH = 50;
    c1->marginT = 20;
    c1->marginL = 10;
    c1->marginR = 10;
    vbox->AddChild(c1);

    vbox->DoLayout();

    assert(feq(Y(c1.get()), 20) && "marginT should push y to 20");
    assert(feq(X(c1.get()), 10) && "marginL should push x to 10");
    assert(feq(W(c1.get()), 180) && "stretch should respect margins: 200-10-10=180");
    printf("  [PASS] margin\n");
}

// ---- Test 7: flex + minmax combined ----
static void test_flex_with_minmax() {
    auto hbox = std::make_shared<HBoxWidget>();
    hbox->gap_ = 0;
    hbox->rect = {0, 0, 600, 100};

    // flex=1, maxW=100
    auto c1 = std::make_shared<Widget>();
    c1->expanding = true;
    c1->flex = 1.0f;
    c1->maxW = 100;
    hbox->AddChild(c1);

    // flex=1, no max
    auto c2 = std::make_shared<Widget>();
    c2->expanding = true;
    c2->flex = 1.0f;
    hbox->AddChild(c2);

    hbox->DoLayout();

    assert(W(c1.get()) <= 100.0f && "c1 clamped to maxW=100");
    printf("  [PASS] flex + min/max combined\n");
}

// ---- Test 8: VBox cross-align End ----
static void test_cross_align_end() {
    auto vbox = std::make_shared<VBoxWidget>();
    vbox->gap_ = 0;
    vbox->crossAlign_ = LayoutAlign::End;
    vbox->rect = {0, 0, 300, 100};

    auto c1 = std::make_shared<Widget>();
    c1->fixedW = 100;
    c1->fixedH = 50;
    vbox->AddChild(c1);

    vbox->DoLayout();

    // Right-aligned: x = 300 - 100 = 200
    assert(feq(X(c1.get()), 200) && "cross-align end should be at x=200");
    printf("  [PASS] VBox cross-align end\n");
}

// ---- Test 9: Expand with DSL ----
static void test_expand_dsl() {
    auto hbox = std::make_shared<HBoxWidget>();
    hbox->gap_ = 0;
    hbox->rect = {0, 0, 900, 100};

    auto c1 = std::make_shared<Widget>();
    c1->Expand(1);
    hbox->AddChild(c1);

    auto c2 = std::make_shared<Widget>();
    c2->Expand(2);
    hbox->AddChild(c2);

    auto c3 = std::make_shared<Widget>();
    c3->Expand(3);
    hbox->AddChild(c3);

    hbox->DoLayout();

    assert(feq(W(c1.get()), 150) && "flex=1 of 6 → 150");
    assert(feq(W(c2.get()), 300) && "flex=2 of 6 → 300");
    assert(feq(W(c3.get()), 450) && "flex=3 of 6 → 450");
    printf("  [PASS] Expand(flex) DSL\n");
}

int main() {
    printf("=== Layout Engine Tests ===\n");
    test_minmax();
    test_flex_weight();
    test_cross_align();
    test_justify_center();
    test_justify_space_between();
    test_margin();
    test_flex_with_minmax();
    test_cross_align_end();
    test_expand_dsl();
    printf("=== All %d tests passed ===\n", 9);
    return 0;
}
