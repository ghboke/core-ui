#include "controls.h"

#include <cmath>
#include <cstdio>
#include <memory>

int main() {
    auto content = std::make_shared<ui::VBoxWidget>();
    content->gap_ = 0.0f;

    auto child = std::make_shared<ui::PanelWidget>();
    child->fixedH = 90.0f;
    content->AddChild(child);

    ui::ScrollViewWidget scrollView;
    scrollView.rect = {0.0f, 0.0f, 200.0f, 100.0f};
    scrollView.padT = 10.0f;
    scrollView.padB = 10.0f;
    scrollView.SetContent(content);
    scrollView.DoLayout();

    if (!scrollView.NeedsScrollbar()) {
        std::fputs("expected content to overflow the padded viewport\n", stderr);
        return 1;
    }

    scrollView.SetScrollY(1000.0f);
    if (std::fabs(scrollView.ScrollY() - 10.0f) > 0.01f) {
        std::fprintf(stderr, "expected max scroll 10, got %.2f\n", scrollView.ScrollY());
        return 1;
    }

    return 0;
}
