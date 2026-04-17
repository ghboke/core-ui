#include <cstdio>
#include <cassert>
#include <string>
#include <memory>

// Include markup engine
#include "../../src/ui/markup/markup.h"
#include "../../src/ui/controls.h"

using namespace ui;

// ---- Test 1: Basic parsing + tree structure ----
static void test_basic_parse() {
    UiMarkup m;
    bool ok = m.LoadString(R"(
        <ui version="1">
            <VBox id="root" gap="8" padding="16">
                <Label id="title" text="Hello World" fontSize="18" bold="true" />
                <HBox gap="4">
                    <Button id="btn1" text="OK" />
                    <Button id="btn2" text="Cancel" />
                </HBox>
            </VBox>
        </ui>
    )");

    assert(ok && "parse should succeed");
    assert(m.Root() != nullptr && "root should exist");
    assert(m.Root()->id == "root" && "root id should be 'root'");

    // Check tree structure
    auto* root = m.Root().get();
    assert(root->Children().size() == 2 && "root should have 2 children");

    // First child is Label
    auto* label = m.FindById("title");
    assert(label != nullptr && "should find label by id");
    auto* lbl = dynamic_cast<LabelWidget*>(label);
    assert(lbl != nullptr && "title should be a LabelWidget");

    // HBox with 2 buttons
    auto& hbox = root->Children()[1];
    assert(hbox->Children().size() == 2 && "HBox should have 2 children");

    auto* btn1 = m.FindById("btn1");
    auto* btn2 = m.FindById("btn2");
    assert(btn1 != nullptr && btn2 != nullptr && "should find buttons by id");

    // Verify VBox gap
    auto* vbox = dynamic_cast<VBoxWidget*>(root);
    assert(vbox != nullptr);
    assert(vbox->gap_ == 8.0f && "gap should be 8");
    assert(root->padL == 16.0f && "padding should be 16");

    printf("  [PASS] basic parse + tree structure\n");
}

// ---- Test 2: Style system ----
static void test_styles() {
    UiMarkup m;
    bool ok = m.LoadString(R"(
        <ui version="1">
            <style>
                .big { fontSize: 24; bold: true; }
                #special { textColor: 1,0,0,1; }
            </style>
            <VBox>
                <Label id="a" text="A" class="big" />
                <Label id="special" text="B" class="big" />
            </VBox>
        </ui>
    )");
    assert(ok && "parse with styles should succeed");

    // Both labels should exist
    assert(m.FindById("a") != nullptr);
    assert(m.FindById("special") != nullptr);

    printf("  [PASS] style system\n");
}

// ---- Test 3: Event handlers ----
static void test_handlers() {
    UiMarkup m;

    int clickCount = 0;
    m.SetHandler("onTest", std::function<void()>([&clickCount]() { clickCount++; }));

    bool ok = m.LoadString(R"(
        <ui version="1">
            <VBox>
                <Button id="btn" text="Click" onClick="onTest" />
            </VBox>
        </ui>
    )");
    assert(ok);

    auto* btn = m.FindById("btn");
    assert(btn != nullptr);
    assert(btn->onClick && "onClick should be wired");

    btn->onClick();
    assert(clickCount == 1 && "handler should have been called");

    btn->onClick();
    assert(clickCount == 2 && "handler should be callable multiple times");

    printf("  [PASS] event handlers\n");
}

// ---- Test 4: Data bindings ----
static void test_bindings() {
    UiMarkup m;
    bool ok = m.LoadString(R"(
        <ui version="1">
            <VBox>
                <Label id="lbl" text="{greeting}" />
                <CheckBox id="cb" text="Enable" checked="{isEnabled}" />
                <Slider id="sl" min="0" max="100" value="{level}" />
            </VBox>
        </ui>
    )");
    assert(ok);

    // Push values via bindings
    m.SetText("greeting", L"Hello Binding!");
    auto* lbl = m.FindAs<LabelWidget>("lbl");
    assert(lbl != nullptr);
    assert(lbl->Text() == L"Hello Binding!" && "text binding should work");

    m.SetBool("isEnabled", true);
    auto* cb = m.FindAs<CheckBoxWidget>("cb");
    assert(cb != nullptr);
    assert(cb->Checked() == true && "checkbox binding should work");

    m.SetFloat("level", 42.0f);
    auto* sl = m.FindAs<SliderWidget>("sl");
    assert(sl != nullptr);
    assert(sl->Value() == 42.0f && "slider binding should work");

    // Update again
    m.SetText("greeting", L"Updated!");
    assert(lbl->Text() == L"Updated!" && "re-binding should work");

    printf("  [PASS] data bindings\n");
}

// ---- Test 5: Layout attributes (flex, align, justify, margin) ----
static void test_layout_attrs() {
    UiMarkup m;
    bool ok = m.LoadString(R"(
        <ui version="1">
            <HBox id="hbox" gap="0" align="center" justify="space-between">
                <Label text="A" flex="1" />
                <Label text="B" flex="2" />
                <Label text="C" width="100" margin="8" />
            </HBox>
        </ui>
    )");
    assert(ok);

    auto* hbox = dynamic_cast<HBoxWidget*>(m.Root().get());
    assert(hbox != nullptr);
    assert(hbox->crossAlign_ == LayoutAlign::Center && "align should be center");
    assert(hbox->mainJustify_ == LayoutJustify::SpaceBetween && "justify should be space-between");

    auto& children = hbox->Children();
    assert(children.size() == 3);

    // First child: flex=1, expanding=true
    assert(children[0]->expanding == true && "flex=1 should set expanding");
    assert(children[0]->flex == 1.0f);

    // Second child: flex=2
    assert(children[1]->expanding == true);
    assert(children[1]->flex == 2.0f);

    // Third child: width=100, margin=8
    assert(children[2]->fixedW == 100.0f);
    assert(children[2]->marginL == 8.0f);
    assert(children[2]->marginT == 8.0f);

    printf("  [PASS] layout attributes\n");
}

// ---- Test 6: Self-closing tags + comments ----
static void test_selfclose_comments() {
    UiMarkup m;
    bool ok = m.LoadString(R"(
        <ui version="1">
            <VBox>
                <!-- This is a comment -->
                <Spacer />
                <Separator />
                <Separator vertical="true" />
            </VBox>
        </ui>
    )");
    assert(ok);
    assert(m.Root()->Children().size() == 3 && "should have 3 children (comment ignored)");

    printf("  [PASS] self-closing tags + comments\n");
}

// ---- Test 7: Controls - ComboBox, Toggle, ProgressBar ----
static void test_controls() {
    UiMarkup m;
    bool ok = m.LoadString(R"(
        <ui version="1">
            <VBox>
                <ComboBox id="combo" items="Red,Green,Blue" selected="1" />
                <Toggle id="tgl" text="Dark Mode" on="true" />
                <ProgressBar id="prog" min="0" max="100" value="50" />
                <RadioButton id="rb" text="Option A" group="grp1" selected="true" />
                <TextInput id="input" placeholder="Type here..." maxLength="50" />
            </VBox>
        </ui>
    )");
    assert(ok);

    auto* combo = m.FindAs<ComboBoxWidget>("combo");
    assert(combo != nullptr);
    assert(combo->SelectedIndex() == 1 && "selected should be 1");

    auto* tgl = m.FindAs<ToggleWidget>("tgl");
    assert(tgl != nullptr);
    assert(tgl->On() == true && "toggle should be on");

    auto* prog = m.FindAs<ProgressBarWidget>("prog");
    assert(prog != nullptr);

    auto* rb = m.FindAs<RadioButtonWidget>("rb");
    assert(rb != nullptr);
    assert(rb->Selected() == true && "radio should be selected");

    auto* input = m.FindAs<TextInputWidget>("input");
    assert(input != nullptr);
    assert(input->maxLength == 50);

    printf("  [PASS] controls (ComboBox, Toggle, ProgressBar, RadioButton, TextInput)\n");
}

// ---- Test 8: Error handling ----
static void test_errors() {
    UiMarkup m;

    // Missing closing tag
    bool ok = m.LoadString(R"(
        <ui version="1">
            <VBox>
                <Label text="unclosed">
            </VBox>
        </ui>
    )");
    assert(!ok && "should fail with mismatched tags");
    assert(!m.LastError().empty() && "should have error message");

    // Invalid root
    ok = m.LoadString(R"(<div>hello</div>)");
    assert(!ok && "should fail with non-ui root");

    printf("  [PASS] error handling\n");
}

// ---- Test 9: ScrollView + TabControl ----
static void test_containers() {
    UiMarkup m;
    bool ok = m.LoadString(R"(
        <ui version="1">
            <VBox>
                <ScrollView id="sv" expand="true">
                    <VBox padding="16">
                        <Label text="Scrollable content" />
                    </VBox>
                </ScrollView>
                <TabControl id="tabs">
                    <Tab title="Tab 1">
                        <Label text="Content 1" />
                    </Tab>
                    <Tab title="Tab 2">
                        <Label text="Content 2" />
                    </Tab>
                </TabControl>
            </VBox>
        </ui>
    )");
    assert(ok);

    auto* sv = m.FindAs<ScrollViewWidget>("sv");
    assert(sv != nullptr && "ScrollView should exist");

    auto* tabs = m.FindAs<TabControlWidget>("tabs");
    assert(tabs != nullptr && "TabControl should exist");

    printf("  [PASS] ScrollView + TabControl containers\n");
}

// ---- Test 10: Reload ----
static void test_reload() {
    UiMarkup m;
    bool ok = m.LoadString(R"(
        <ui version="1">
            <VBox>
                <Label id="lbl" text="Original" />
            </VBox>
        </ui>
    )");
    assert(ok);
    auto* lbl = m.FindAs<LabelWidget>("lbl");
    assert(lbl != nullptr);
    assert(lbl->Text() == L"Original");

    // Reload from cached source (same content)
    ok = m.Reload();
    assert(ok && "reload should succeed");
    lbl = m.FindAs<LabelWidget>("lbl");
    assert(lbl != nullptr && "should find label after reload");

    printf("  [PASS] reload\n");
}

int main() {
    printf("=== Markup Engine Tests ===\n");
    test_basic_parse();
    test_styles();
    test_handlers();
    test_bindings();
    test_layout_attrs();
    test_selfclose_comments();
    test_controls();
    test_errors();
    test_containers();
    test_reload();
    printf("=== All %d tests passed ===\n", 10);
    return 0;
}
