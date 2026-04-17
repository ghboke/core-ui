/*
 * ui_debug.cpp — Widget tree debug inspector
 *
 * Uses RTTI (dynamic_cast) to identify widget types and extract
 * type-specific properties. Outputs a JSON string for the entire tree.
 */
#include "ui_debug.h"
#include "controls.h"
#include <sstream>
#include <iomanip>
#include <cstdio>
#include <cmath>

namespace ui {

// ---- JSON helpers ----

static std::string indent(int depth) {
    return std::string(depth * 2, ' ');
}

static std::string jsonStr(const std::wstring& ws) {
    std::string s;
    s.reserve(ws.size());
    for (wchar_t c : ws) {
        if (c == L'"') s += "\\\"";
        else if (c == L'\\') s += "\\\\";
        else if (c == L'\n') s += "\\n";
        else if (c < 0x80) s += (char)c;
        else {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
            s += buf;
        }
    }
    return "\"" + s + "\"";
}

static std::string jsonStr(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) {
        if (c == '"') r += "\\\"";
        else if (c == '\\') r += "\\\\";
        else r += c;
    }
    return "\"" + r + "\"";
}

static std::string jsonRect(const D2D1_RECT_F& r) {
    char buf[128];
    snprintf(buf, sizeof(buf), "[%.0f, %.0f, %.0f, %.0f]",
             r.left, r.top, r.right, r.bottom);
    return buf;
}

static std::string jsonFloat(float v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", v);
    return buf;
}

// ---- Type identification ----

static const char* widgetTypeName(Widget* w) {
    if (dynamic_cast<TitleBarWidget*>(w))    return "TitleBar";
    if (dynamic_cast<CaptionButtonWidget*>(w)) return "CaptionButton";
    if (dynamic_cast<IconButtonWidget*>(w))  return "IconButton";
    if (dynamic_cast<ButtonWidget*>(w))      return "Button";
    if (dynamic_cast<CheckBoxWidget*>(w))    return "CheckBox";
    if (dynamic_cast<RadioButtonWidget*>(w)) return "RadioButton";
    if (dynamic_cast<ToggleWidget*>(w))      return "Toggle";
    if (dynamic_cast<SliderWidget*>(w))      return "Slider";
    if (dynamic_cast<ProgressBarWidget*>(w)) return "ProgressBar";
    if (dynamic_cast<ComboBoxWidget*>(w))    return "ComboBox";
    if (dynamic_cast<TextInputWidget*>(w))   return "TextInput";
    if (dynamic_cast<TextAreaWidget*>(w))    return "TextArea";
    if (dynamic_cast<TabControlWidget*>(w))  return "TabControl";
    if (dynamic_cast<ScrollViewWidget*>(w))  return "ScrollView";
    if (dynamic_cast<ImageViewWidget*>(w))   return "ImageView";
    if (dynamic_cast<DialogWidget*>(w))      return "Dialog";
    if (dynamic_cast<OverlayWidget*>(w))     return "Overlay";
    if (dynamic_cast<MenuBarWidget*>(w))     return "MenuBar";
    if (dynamic_cast<SplitterWidget*>(w))    return "Splitter";
    if (dynamic_cast<GridWidget*>(w))        return "Grid";
    if (dynamic_cast<StackWidget*>(w))       return "Stack";
    if (dynamic_cast<LabelWidget*>(w))       return "Label";
    if (dynamic_cast<SpacerWidget*>(w))      return "Spacer";
    if (dynamic_cast<SeparatorWidget*>(w))   return "Separator";
    if (dynamic_cast<ToolTipWidget*>(w))     return "ToolTip";
    if (dynamic_cast<CustomWidget*>(w))      return "Custom";
    if (dynamic_cast<PanelWidget*>(w))       return "Panel";
    if (dynamic_cast<VBoxWidget*>(w))        return "VBox";
    if (dynamic_cast<HBoxWidget*>(w))        return "HBox";
    return "Widget";
}

// ---- Text measurement helper ----

static void appendTextMetrics(std::ostringstream& ss, const std::string& ind,
                               Renderer* r, const std::wstring& text,
                               float fontSize, float availW) {
    if (!r || text.empty()) return;
    float tw = r->MeasureTextWidth(text, fontSize);
    ss << ",\n" << ind << "\"textWidth\": " << jsonFloat(tw);
    ss << ",\n" << ind << "\"availWidth\": " << jsonFloat(availW);
    if (availW > 0) {
        int lines = (int)std::ceil(tw / availW);
        if (lines < 1) lines = 1;
        ss << ",\n" << ind << "\"lines\": " << lines;
    }
}

// ---- Type-specific properties ----

static void appendTypeProps(std::ostringstream& ss, Widget* w, int d, Renderer* r) {
    std::string ind = indent(d);
    float cw = w->rect.right - w->rect.left;  // content width approx

    if (auto* lbl = dynamic_cast<LabelWidget*>(w)) {
        ss << ",\n" << ind << "\"text\": " << jsonStr(lbl->Text());
        float pad = w->padL + w->padR;
        appendTextMetrics(ss, ind, r, lbl->Text(), theme::kFontSizeNormal, cw - pad);
    }
    else if (auto* btn = dynamic_cast<ButtonWidget*>(w)) {
        ss << ",\n" << ind << "\"text\": " << jsonStr(btn->Text());
        appendTextMetrics(ss, ind, r, btn->Text(), theme::kFontSizeNormal, cw - 24.0f);
    }
    else if (auto* cb = dynamic_cast<CheckBoxWidget*>(w)) {
        ss << ",\n" << ind << "\"text\": " << jsonStr(cb->Text());
        ss << ",\n" << ind << "\"checked\": " << (cb->Checked() ? "true" : "false");
        appendTextMetrics(ss, ind, r, cb->Text(), theme::kFontSizeNormal, cw - 26.0f);
    }
    else if (auto* rb = dynamic_cast<RadioButtonWidget*>(w)) {
        ss << ",\n" << ind << "\"text\": " << jsonStr(rb->Text());
        ss << ",\n" << ind << "\"selected\": " << (rb->Selected() ? "true" : "false");
        ss << ",\n" << ind << "\"group\": " << jsonStr(rb->Group());
        appendTextMetrics(ss, ind, r, rb->Text(), theme::kFontSizeNormal, cw - 26.0f);
    }
    else if (auto* tg = dynamic_cast<ToggleWidget*>(w)) {
        ss << ",\n" << ind << "\"text\": " << jsonStr(tg->Text());
        ss << ",\n" << ind << "\"on\": " << (tg->On() ? "true" : "false");
        appendTextMetrics(ss, ind, r, tg->Text(), theme::kFontSizeNormal, cw - 44.0f);
    }
    else if (auto* sl = dynamic_cast<SliderWidget*>(w)) {
        ss << ",\n" << ind << "\"value\": " << jsonFloat(sl->Value());
    }
    else if (auto* pb = dynamic_cast<ProgressBarWidget*>(w)) {
        ss << ",\n" << ind << "\"value\": " << jsonFloat(pb->Value());
    }
    else if (auto* combo = dynamic_cast<ComboBoxWidget*>(w)) {
        ss << ",\n" << ind << "\"selectedIndex\": " << combo->SelectedIndex();
        ss << ",\n" << ind << "\"selectedText\": " << jsonStr(combo->SelectedText());
        ss << ",\n" << ind << "\"open\": " << (combo->IsOpen() ? "true" : "false");
    }
    else if (auto* iv = dynamic_cast<ImageViewWidget*>(w)) {
        ss << ",\n" << ind << "\"imageW\": " << iv->ImageWidth();
        ss << ",\n" << ind << "\"imageH\": " << iv->ImageHeight();
        ss << ",\n" << ind << "\"zoom\": " << jsonFloat(iv->Zoom());
        ss << ",\n" << ind << "\"loading\": " << (iv->IsLoading() ? "true" : "false");
    }
    else if (auto* tb = dynamic_cast<TitleBarWidget*>(w)) {
        ss << ",\n" << ind << "\"title\": " << jsonStr(tb->Title());
    }
    else if (auto* dlg = dynamic_cast<DialogWidget*>(w)) {
        ss << ",\n" << ind << "\"active\": " << (dlg->IsActive() ? "true" : "false");
    }
    else if (auto* ov = dynamic_cast<OverlayWidget*>(w)) {
        ss << ",\n" << ind << "\"active\": " << (ov->IsActive() ? "true" : "false");
        ss << ",\n" << ind << "\"text\": " << jsonStr(ov->Text());
    }
    else if (auto* tab = dynamic_cast<TabControlWidget*>(w)) {
        ss << ",\n" << ind << "\"activeIndex\": " << tab->ActiveIndex();
    }
    else if (auto* sv = dynamic_cast<ScrollViewWidget*>(w)) {
        ss << ",\n" << ind << "\"scrollY\": " << jsonFloat(sv->ScrollY());
    }
    else if (auto* sp = dynamic_cast<SplitterWidget*>(w)) {
        ss << ",\n" << ind << "\"ratio\": " << jsonFloat(sp->Ratio());
        ss << ",\n" << ind << "\"vertical\": " << (sp->IsVertical() ? "true" : "false");
    }
    else if (auto* stk = dynamic_cast<StackWidget*>(w)) {
        ss << ",\n" << ind << "\"activeIndex\": " << stk->ActiveIndex();
    }

    // VBox/HBox gap
    if (auto* vb = dynamic_cast<VBoxWidget*>(w)) {
        ss << ",\n" << ind << "\"gap\": " << jsonFloat(vb->gap_);
    }
    else if (auto* hb = dynamic_cast<HBoxWidget*>(w)) {
        ss << ",\n" << ind << "\"gap\": " << jsonFloat(hb->gap_);
    }
}

// ---- Main dump ----

static void dumpWidget(std::ostringstream& ss, Widget* w, int d, Renderer* r) {
    std::string ind = indent(d);
    std::string ind1 = indent(d + 1);

    ss << ind << "{\n";
    ss << ind1 << "\"type\": \"" << widgetTypeName(w) << "\"";

    // id
    if (!w->id.empty())
        ss << ",\n" << ind1 << "\"id\": " << jsonStr(w->id);

    // geometry
    ss << ",\n" << ind1 << "\"rect\": " << jsonRect(w->rect);

    float rw = w->rect.right - w->rect.left;
    float rh = w->rect.bottom - w->rect.top;
    ss << ",\n" << ind1 << "\"size\": [" << jsonFloat(rw) << ", " << jsonFloat(rh) << "]";

    // constraints (only if non-default)
    if (w->fixedW > 0)  ss << ",\n" << ind1 << "\"fixedW\": " << jsonFloat(w->fixedW);
    if (w->fixedH > 0)  ss << ",\n" << ind1 << "\"fixedH\": " << jsonFloat(w->fixedH);
    if (w->expanding)   ss << ",\n" << ind1 << "\"expanding\": true";
    if (w->flex != 1.0f) ss << ",\n" << ind1 << "\"flex\": " << jsonFloat(w->flex);

    // padding (if any non-zero)
    if (w->padL > 0 || w->padT > 0 || w->padR > 0 || w->padB > 0)
        ss << ",\n" << ind1 << "\"padding\": [" << jsonFloat(w->padL) << ", "
           << jsonFloat(w->padT) << ", " << jsonFloat(w->padR) << ", "
           << jsonFloat(w->padB) << "]";

    // margin (if any non-zero)
    if (w->marginL > 0 || w->marginT > 0 || w->marginR > 0 || w->marginB > 0)
        ss << ",\n" << ind1 << "\"margin\": [" << jsonFloat(w->marginL) << ", "
           << jsonFloat(w->marginT) << ", " << jsonFloat(w->marginR) << ", "
           << jsonFloat(w->marginB) << "]";

    // state
    if (!w->visible)  ss << ",\n" << ind1 << "\"visible\": false";
    if (!w->enabled)  ss << ",\n" << ind1 << "\"enabled\": false";
    if (w->opacity < 1.0f) ss << ",\n" << ind1 << "\"opacity\": " << jsonFloat(w->opacity);

    // type-specific
    appendTypeProps(ss, w, d + 1, r);

    // children
    auto& children = w->Children();
    if (!children.empty()) {
        ss << ",\n" << ind1 << "\"children\": [\n";
        for (size_t i = 0; i < children.size(); i++) {
            if (i > 0) ss << ",\n";
            dumpWidget(ss, children[i].get(), d + 2, r);
        }
        ss << "\n" << ind1 << "]";
    }

    ss << "\n" << ind << "}";
}

std::string DebugDumpTree(Widget* root, Renderer* renderer, int depth) {
    if (!root) return "null";
    std::ostringstream ss;
    dumpWidget(ss, root, depth, renderer);
    return ss.str();
}

} // namespace ui
