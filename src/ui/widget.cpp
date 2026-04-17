#include "widget.h"
#include "renderer.h"
#include "event.h"
#include "theme.h"

namespace ui {

// ---- Widget tree ----

void Widget::AddChild(WidgetPtr child) {
    child->parent_ = this;
    children_.push_back(std::move(child));
}

void Widget::RemoveChild(Widget* child) {
    children_.erase(
        std::remove_if(children_.begin(), children_.end(),
            [child](const WidgetPtr& p) { return p.get() == child; }),
        children_.end());
}

// ---- Default draw ----

void Widget::OnDraw(Renderer& r) {
    if (bgColorFn) bgColor = bgColorFn();

    // Apply state-dependent background overrides
    D2D1_COLOR_F drawBg = bgColor;
    if (!enabled && stateColors.disabledBg) drawBg = stateColors.disabledBg();
    else if (pressed && stateColors.pressedBg) drawBg = stateColors.pressedBg();
    else if (hovered && stateColors.hoverBg)   drawBg = stateColors.hoverBg();

    if (drawBg.a > 0) {
        r.FillRect(rect, drawBg);
    }
}

// ---- Default event handlers ----

bool Widget::OnMouseMove(const MouseEvent&)  { return false; }
bool Widget::OnMouseDown(const MouseEvent&)  { return false; }
bool Widget::OnMouseUp(const MouseEvent&)    { return false; }
bool Widget::OnMouseWheel(const MouseEvent&) { return false; }

// ---- Default layout (just position children at content area) ----

void Widget::DoLayout() {
    float cx = ContentLeft(), cy = ContentTop();
    float cw = ContentWidth(), ch = ContentHeight();
    for (auto& child : children_) {
        if (!child->visible) continue;

        // Resolve percentage sizes
        if (child->percentW >= 0) child->fixedW = cw * child->percentW / 100.0f;
        if (child->percentH >= 0) child->fixedH = ch * child->percentH / 100.0f;

        if (child->positionAbsolute) {
            auto hint = child->SizeHint();
            float w = child->fixedW > 0 ? child->fixedW : (hint.width > 0 ? hint.width : cw);
            float h = child->fixedH > 0 ? child->fixedH : (hint.height > 0 ? hint.height : 24.0f);
            float x = cx, y = cy;
            if (child->posLeft >= 0) x = cx + child->posLeft;
            else if (child->posRight >= 0) x = cx + cw - w - child->posRight;
            if (child->posTop >= 0) y = cy + child->posTop;
            else if (child->posBottom >= 0) y = cy + ch - h - child->posBottom;
            child->rect = {x, y, x + w, y + h};
        } else if (child->fixedW > 0 && child->fixedH > 0) {
            child->rect = {cx, cy, cx + child->fixedW, cy + child->fixedH};
        } else {
            child->rect = {cx, cy, cx + cw, cy + ch};
        }
        child->DoLayout();
    }
}

// ---- Draw tree ----

void Widget::DrawTree(Renderer& r) {
    if (!visible || opacity <= 0.0f) return;

    bool useLayer = (opacity < 1.0f);
    if (useLayer) r.PushOpacity(opacity, rect);

    OnDraw(r);
    DrawFocusRing(r);
    for (auto& child : children_) {
        child->DrawTree(r);
    }

    if (useLayer) r.PopOpacity();
}

// ---- Draw overlays (e.g. dropdowns) on top of everything ----

void Widget::DrawOverlays(Renderer& r) {
    if (!visible) return;
    OnDrawOverlay(r);
    for (auto& child : children_) {
        child->DrawOverlays(r);
    }
}

// ---- Hit test (deepest child first) ----

Widget* Widget::HitTest(float x, float y) {
    if (!visible || !enabled || opacity <= 0.0f || !Contains(x, y)) return nullptr;
    for (int i = (int)children_.size() - 1; i >= 0; --i) {
        if (auto* hit = children_[i]->HitTest(x, y)) return hit;
    }
    return hitTransparent ? nullptr : this;
}

// ---- Find by ID ----

Widget* Widget::FindById(const std::string& targetId) {
    if (id == targetId) return this;
    for (auto& child : children_) {
        if (auto* found = child->FindById(targetId)) return found;
    }
    return nullptr;
}

// ---- Focus ----

void Widget::DrawFocusRing(Renderer& r) {
    if (!focused_ || !focusable || !ShowFocusRing()) return;
    D2D1_RECT_F ring = {rect.left - 1, rect.top - 1, rect.right + 1, rect.bottom + 1};
    r.DrawRoundedRect(ring, 3, 3, theme::kAccent(), 1.5f);
}

void Widget::CollectFocusable(std::vector<Widget*>& out) {
    if (!visible || !enabled) return;
    if (focusable && tabStop) out.push_back(this);
    for (auto& child : children_)
        child->CollectFocusable(out);
}

// ---- VBoxWidget layout ----
// Main axis = vertical (Y), Cross axis = horizontal (X)

void VBoxWidget::DoLayout() {
    float cx = ContentLeft();
    float cy = ContentTop();
    float cw = ContentWidth();
    float totalH = ContentHeight();

    // Collect visible children (skip absolute-positioned)
    std::vector<Widget*> vis;
    std::vector<Widget*> absChildren;
    for (auto& child : children_) {
        if (!child->visible) continue;
        if (child->positionAbsolute) absChildren.push_back(child.get());
        else vis.push_back(child.get());
    }

    // Resolve percentage sizes
    for (auto* child : vis) {
        if (child->percentW >= 0) child->fixedW = cw * child->percentW / 100.0f;
        if (child->percentH >= 0) child->fixedH = totalH * child->percentH / 100.0f;
    }

    // First pass: measure fixed children, sum flex weights
    float usedH = 0;
    float totalFlex = 0;
    for (auto* child : vis) {
        usedH += child->marginT + child->marginB;
        if (child->expanding && child->fixedH <= 0) {
            totalFlex += child->flex;
        } else {
            float h = child->fixedH > 0 ? child->fixedH : child->SizeHint().height;
            if (h <= 0) h = 24.0f;
            h = std::clamp(h, child->minH, child->maxH);
            usedH += h;
        }
    }
    float gapTotal = (int)vis.size() > 1 ? gap_ * ((int)vis.size() - 1) : 0;
    float remaining = std::max(0.0f, totalH - usedH - gapTotal);

    // Compute each child's main-axis size (height)
    std::vector<float> heights(vis.size());
    for (size_t i = 0; i < vis.size(); i++) {
        auto* child = vis[i];
        float h;
        if (child->expanding && child->fixedH <= 0) {
            h = totalFlex > 0 ? remaining * (child->flex / totalFlex) : 0;
        } else {
            h = child->fixedH > 0 ? child->fixedH : child->SizeHint().height;
            if (h <= 0) h = 24.0f;
        }
        heights[i] = std::clamp(h, child->minH, child->maxH);
    }

    // Compute total content height (for justify)
    float contentH = 0;
    for (size_t i = 0; i < vis.size(); i++)
        contentH += heights[i] + vis[i]->marginT + vis[i]->marginB;
    contentH += gapTotal;

    // Main-axis start position + gap override for justify
    float y = cy;
    float extraGap = 0;
    float freeSpace = totalH - contentH;
    if (freeSpace > 0 && totalFlex == 0) {
        switch (mainJustify_) {
            case LayoutJustify::Start:        break;
            case LayoutJustify::Center:       y += freeSpace * 0.5f; break;
            case LayoutJustify::End:          y += freeSpace; break;
            case LayoutJustify::SpaceBetween:
                if (vis.size() > 1) extraGap = freeSpace / ((int)vis.size() - 1);
                break;
            case LayoutJustify::SpaceAround:
                extraGap = freeSpace / (float)vis.size();
                y += extraGap * 0.5f;
                break;
        }
    }

    // Second pass: position children
    for (size_t i = 0; i < vis.size(); i++) {
        auto* child = vis[i];
        float h = heights[i];

        y += child->marginT;

        // Cross-axis (width + horizontal position)
        float availW = cw - child->marginL - child->marginR;
        float childW;
        if (child->fixedW > 0) {
            childW = std::clamp(child->fixedW, child->minW, child->maxW);
        } else if (crossAlign_ == LayoutAlign::Stretch) {
            childW = std::clamp(availW, child->minW, child->maxW);
        } else {
            float hintW = child->SizeHint().width;
            if (hintW <= 0) hintW = availW;
            childW = std::clamp(hintW, child->minW, child->maxW);
        }

        float childX = cx + child->marginL;
        switch (crossAlign_) {
            case LayoutAlign::Stretch:
            case LayoutAlign::Start:   break;
            case LayoutAlign::Center:  childX += (availW - childW) * 0.5f; break;
            case LayoutAlign::End:     childX += availW - childW; break;
        }

        child->rect = {childX, y, childX + childW, y + h};
        child->DoLayout();

        y += h + child->marginB + gap_ + extraGap;
    }

    // Layout absolute-positioned children
    for (auto* child : absChildren) {
        auto hint = child->SizeHint();
        float w = child->fixedW > 0 ? child->fixedW : (hint.width > 0 ? hint.width : cw);
        float h = child->fixedH > 0 ? child->fixedH : (hint.height > 0 ? hint.height : 24.0f);
        float x = cx, y2 = cy;
        if (child->posLeft >= 0) x = cx + child->posLeft;
        else if (child->posRight >= 0) x = cx + cw - w - child->posRight;
        if (child->posTop >= 0) y2 = cy + child->posTop;
        else if (child->posBottom >= 0) y2 = cy + totalH - h - child->posBottom;
        child->rect = {x, y2, x + w, y2 + h};
        child->DoLayout();
    }
}

D2D1_SIZE_F VBoxWidget::SizeHint() const {
    float w = fixedW, h = 0;
    int count = 0;
    for (auto& child : children_) {
        if (!child->visible) continue;
        auto hint = child->SizeHint();
        float cw = (hint.width > 0 ? hint.width : 0) + child->marginL + child->marginR;
        if (cw > w) w = cw;
        float ch = hint.height > 0 ? hint.height : 24.0f;
        ch = std::clamp(ch, child->minH, child->maxH);
        h += ch + child->marginT + child->marginB;
        count++;
    }
    if (count > 1) h += gap_ * (count - 1);
    h += padT + padB;
    w += padL + padR;
    return {fixedW > 0 ? fixedW : w, fixedH > 0 ? fixedH : h};
}

// ---- HBoxWidget layout ----
// Main axis = horizontal (X), Cross axis = vertical (Y)

void HBoxWidget::DoLayout() {
    float cx = ContentLeft();
    float cy = ContentTop();
    float ch = ContentHeight();
    float totalW = ContentWidth();

    // Collect visible children (skip absolute-positioned)
    std::vector<Widget*> vis;
    std::vector<Widget*> absChildren;
    for (auto& child : children_) {
        if (!child->visible) continue;
        if (child->positionAbsolute) absChildren.push_back(child.get());
        else vis.push_back(child.get());
    }
    if (vis.empty() && absChildren.empty()) return;

    // Resolve percentage sizes
    for (auto* child : vis) {
        if (child->percentW >= 0) child->fixedW = totalW * child->percentW / 100.0f;
        if (child->percentH >= 0) child->fixedH = ch * child->percentH / 100.0f;
    }

    // First pass: measure fixed children, sum flex weights
    float usedW = 0;
    float totalFlex = 0;
    for (auto* child : vis) {
        usedW += child->marginL + child->marginR;
        if (child->expanding && child->fixedW <= 0) {
            totalFlex += child->flex;
        } else {
            float w = child->fixedW > 0 ? child->fixedW : child->SizeHint().width;
            if (w <= 0) w = 60.0f;
            w = std::clamp(w, child->minW, child->maxW);
            usedW += w;
        }
    }
    float gapTotal = (int)vis.size() > 1 ? gap_ * ((int)vis.size() - 1) : 0;
    float remaining = std::max(0.0f, totalW - usedW - gapTotal);

    // Compute each child's main-axis size (width)
    std::vector<float> widths(vis.size());
    for (size_t i = 0; i < vis.size(); i++) {
        auto* child = vis[i];
        float w;
        if (child->expanding && child->fixedW <= 0) {
            w = totalFlex > 0 ? remaining * (child->flex / totalFlex) : 0;
        } else {
            w = child->fixedW > 0 ? child->fixedW : child->SizeHint().width;
            if (w <= 0) w = 60.0f;
        }
        widths[i] = std::clamp(w, child->minW, child->maxW);
    }

    // Compute total content width (for justify)
    float contentW = 0;
    for (size_t i = 0; i < vis.size(); i++)
        contentW += widths[i] + vis[i]->marginL + vis[i]->marginR;
    contentW += gapTotal;

    // Main-axis start position + gap override for justify
    float x = cx;
    float extraGap = 0;
    float freeSpace = totalW - contentW;
    if (freeSpace > 0 && totalFlex == 0) {
        switch (mainJustify_) {
            case LayoutJustify::Start:        break;
            case LayoutJustify::Center:       x += freeSpace * 0.5f; break;
            case LayoutJustify::End:          x += freeSpace; break;
            case LayoutJustify::SpaceBetween:
                if (vis.size() > 1) extraGap = freeSpace / ((int)vis.size() - 1);
                break;
            case LayoutJustify::SpaceAround:
                extraGap = freeSpace / (float)vis.size();
                x += extraGap * 0.5f;
                break;
        }
    }

    // Second pass: position children
    for (size_t i = 0; i < vis.size(); i++) {
        auto* child = vis[i];
        float w = widths[i];

        x += child->marginL;

        // Cross-axis (height + vertical position)
        float availH = ch - child->marginT - child->marginB;
        float childH;
        if (child->fixedH > 0) {
            childH = std::clamp(child->fixedH, child->minH, child->maxH);
        } else if (crossAlign_ == LayoutAlign::Stretch) {
            childH = std::clamp(availH, child->minH, child->maxH);
        } else {
            float hintH = child->SizeHint().height;
            if (hintH <= 0) hintH = availH;
            childH = std::clamp(hintH, child->minH, child->maxH);
        }

        float childY = cy + child->marginT;
        switch (crossAlign_) {
            case LayoutAlign::Stretch:
            case LayoutAlign::Start:   break;
            case LayoutAlign::Center:  childY += (availH - childH) * 0.5f; break;
            case LayoutAlign::End:     childY += availH - childH; break;
        }

        child->rect = {x, childY, x + w, childY + childH};
        child->DoLayout();

        x += w + child->marginR + gap_ + extraGap;
    }

    // Layout absolute-positioned children
    for (auto* child : absChildren) {
        auto hint = child->SizeHint();
        float w = child->fixedW > 0 ? child->fixedW : (hint.width > 0 ? hint.width : totalW);
        float h = child->fixedH > 0 ? child->fixedH : (hint.height > 0 ? hint.height : ch);
        float x2 = cx, y2 = cy;
        if (child->posLeft >= 0) x2 = cx + child->posLeft;
        else if (child->posRight >= 0) x2 = cx + totalW - w - child->posRight;
        if (child->posTop >= 0) y2 = cy + child->posTop;
        else if (child->posBottom >= 0) y2 = cy + ch - h - child->posBottom;
        child->rect = {x2, y2, x2 + w, y2 + h};
        child->DoLayout();
    }
}

D2D1_SIZE_F HBoxWidget::SizeHint() const {
    float w = 0, h = fixedH;
    int count = 0;
    for (auto& child : children_) {
        if (!child->visible) continue;
        auto hint = child->SizeHint();
        float cw = hint.width > 0 ? hint.width : 60.0f;
        cw = std::clamp(cw, child->minW, child->maxW);
        w += cw + child->marginL + child->marginR;
        float ch = (hint.height > 0 ? hint.height : 0) + child->marginT + child->marginB;
        if (ch > h) h = ch;
        count++;
    }
    if (count > 1) w += gap_ * (count - 1);
    w += padL + padR;
    h += padT + padB;
    return {fixedW > 0 ? fixedW : w, fixedH > 0 ? fixedH : h};
}

// ---- GridWidget layout ----

void GridWidget::DoLayout() {
    float cx = ContentLeft();
    float cy = ContentTop();
    float totalW = ContentWidth();

    // Collect visible children
    std::vector<Widget*> vis;
    for (auto& child : children_)
        if (child->visible) vis.push_back(child.get());
    if (vis.empty()) return;

    int cols = std::max(1, cols_);

    // Column widths: equal share of available width
    float colGapTotal = (cols > 1) ? colGap_ * (cols - 1) : 0;
    float colW = (totalW - colGapTotal) / cols;

    // Arrange children into grid cells, respecting colspan
    // Build row info: each row's height = max child height in that row
    struct Cell { Widget* w; int col; int colspan; };
    std::vector<std::vector<Cell>> rows;
    {
        int col = 0;
        std::vector<Cell> currentRow;
        for (auto* child : vis) {
            int span = std::clamp(child->gridColSpan, 1, cols);
            if (col + span > cols) {
                // Move to next row
                rows.push_back(std::move(currentRow));
                currentRow.clear();
                col = 0;
            }
            currentRow.push_back({child, col, span});
            col += span;
            if (col >= cols) {
                rows.push_back(std::move(currentRow));
                currentRow.clear();
                col = 0;
            }
        }
        if (!currentRow.empty()) rows.push_back(std::move(currentRow));
    }

    // Layout each row
    float y = cy;
    for (auto& row : rows) {
        // Determine row height
        float rowH = 0;
        for (auto& cell : row) {
            float h = cell.w->fixedH > 0 ? cell.w->fixedH : cell.w->SizeHint().height;
            if (h <= 0) h = 24.0f;
            h = std::clamp(h, cell.w->minH, cell.w->maxH);
            h += cell.w->marginT + cell.w->marginB;
            if (h > rowH) rowH = h;
        }

        // Position each cell
        for (auto& cell : row) {
            float cellX = cx + cell.col * (colW + colGap_) + cell.w->marginL;
            float cellW = colW * cell.colspan + colGap_ * (cell.colspan - 1)
                          - cell.w->marginL - cell.w->marginR;
            cellW = std::clamp(cellW, cell.w->minW, cell.w->maxW);

            float cellY = y + cell.w->marginT;
            float h = cell.w->fixedH > 0 ? cell.w->fixedH : rowH - cell.w->marginT - cell.w->marginB;
            h = std::clamp(h, cell.w->minH, cell.w->maxH);

            cell.w->rect = {cellX, cellY, cellX + cellW, cellY + h};
            cell.w->DoLayout();
        }

        y += rowH + rowGap_;
    }
}

D2D1_SIZE_F GridWidget::SizeHint() const {
    int cols = std::max(1, cols_);
    float maxChildW = 0, totalH = 0;
    int idx = 0, rowCount = 0;
    float rowH = 0;

    for (auto& child : children_) {
        if (!child->visible) continue;
        auto hint = child->SizeHint();
        float w = hint.width > 0 ? hint.width : 60.0f;
        if (w > maxChildW) maxChildW = w;
        float h = hint.height > 0 ? hint.height : 24.0f;
        if (h > rowH) rowH = h;

        idx++;
        if (idx >= cols) {
            totalH += rowH;
            rowCount++;
            rowH = 0;
            idx = 0;
        }
    }
    if (idx > 0) { totalH += rowH; rowCount++; }
    if (rowCount > 1) totalH += rowGap_ * (rowCount - 1);

    float w = maxChildW * cols + colGap_ * (cols - 1) + padL + padR;
    totalH += padT + padB;
    return {fixedW > 0 ? fixedW : w, fixedH > 0 ? fixedH : totalH};
}

// ---- StackWidget ----

void StackWidget::SetActiveIndex(int i) {
    if (i < 0 || i >= (int)children_.size()) return;
    activeIndex_ = i;
    // Update visibility
    for (int j = 0; j < (int)children_.size(); j++)
        children_[j]->visible = (j == activeIndex_);
    if (onActiveChanged) onActiveChanged(activeIndex_);
}

void StackWidget::DoLayout() {
    for (int i = 0; i < (int)children_.size(); i++) {
        auto& child = children_[i];
        child->visible = (i == activeIndex_);
        child->rect = {ContentLeft(), ContentTop(), ContentRight(), ContentBottom()};
        if (child->visible) child->DoLayout();
    }
}

void StackWidget::DrawTree(Renderer& r) {
    if (!visible) return;
    OnDraw(r);
    DrawFocusRing(r);
    if (activeIndex_ >= 0 && activeIndex_ < (int)children_.size()) {
        children_[activeIndex_]->DrawTree(r);
    }
}

D2D1_SIZE_F StackWidget::SizeHint() const {
    // Size = max of all children
    float w = 0, h = 0;
    for (auto& child : children_) {
        auto hint = child->SizeHint();
        if (hint.width > w) w = hint.width;
        if (hint.height > h) h = hint.height;
    }
    w += padL + padR;
    h += padT + padB;
    return {fixedW > 0 ? fixedW : w, fixedH > 0 ? fixedH : h};
}

} // namespace ui
