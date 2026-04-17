#pragma once
#include <windows.h>
#include "renderer.h"
#include "theme.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace ui {

class ContextMenu;
using ContextMenuPtr = std::shared_ptr<ContextMenu>;

struct MenuItem {
    int id = 0;
    std::wstring text;
    std::wstring shortcut;
    SvgIcon icon;
    bool hasIcon = false;
    bool enabled = true;
    bool isSeparator = false;
    ContextMenuPtr submenu;
};

class ContextMenu : public std::enable_shared_from_this<ContextMenu> {
public:
    // Build
    void AddItem(int id, const std::wstring& text);
    void AddItemEx(int id, const std::wstring& text,
                   const std::wstring& shortcut, const std::string& svg, Renderer* r);
    void AddSeparator();
    void AddSubmenu(const std::wstring& text, ContextMenuPtr submenu);
    void SetEnabled(int id, bool enabled);
    void SetBgColor(D2D1_COLOR_F color) { bgColor_ = color; hasBgColor_ = true; }

    // Show / hide
    // parentHwnd: owner window; x,y: screen coordinates
    void Show(float x, float y, const D2D1_RECT_F& viewport);
    void ShowPopup(HWND parentHwnd, int screenX, int screenY);
    void Close();
    bool IsVisible() const { return visible_; }

    // Rendering
    void Draw(Renderer& r);

    // Event handling — returns true if consumed
    bool HandleMouseMove(float x, float y);
    bool HandleMouseDown(float x, float y);
    bool HandleMouseUp(float x, float y);

    // Hit result after HandleMouseUp returns true
    int ClickedItemId() const { return clickedId_; }

    // Geometry
    D2D1_RECT_F Bounds() const;
    bool Contains(float x, float y) const;

private:
    std::vector<MenuItem> items_;
    bool visible_ = false;
    float x_ = 0, y_ = 0;
    int hoveredIndex_ = -1;
    int openSubmenuIndex_ = -1;
    int clickedId_ = -1;
    D2D1_COLOR_F bgColor_ = {};
    bool hasBgColor_ = false;

    // Layout constants
    static constexpr float kItemHeight = 32.0f;
    static constexpr float kSepHeight = 9.0f;
    static constexpr float kIconColWidth = 32.0f;
    static constexpr float kPadding = 4.0f;
    static constexpr float kCornerRadius = 8.0f;
    static constexpr float kMinWidth = 180.0f;
    static constexpr float kShadowOffset = 3.0f;
    static constexpr float kSubmenuArrowWidth = 20.0f;

    float MenuWidth() const;
    float MenuHeight() const;
    bool HasAnyIcon() const;
    D2D1_RECT_F ItemRect(int index) const;
    int HitTest(float x, float y) const;

    // Popup window (owns its own HWND + Renderer)
    HWND popupHwnd_ = nullptr;
    Renderer popupRenderer_;
    HWND parentHwnd_ = nullptr;

    void CreatePopupWindow(HWND parent, int screenX, int screenY);
    void DestroyPopupWindow();
    void PaintPopup();

    static bool popupClassRegistered_;
    static LRESULT CALLBACK PopupWndProc(HWND, UINT, WPARAM, LPARAM);
};

} // namespace ui
