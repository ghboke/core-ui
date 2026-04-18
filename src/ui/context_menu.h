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
    // Debug 模拟模式：为 true 时菜单 popup 的 WM_TIMER 不会因为前台窗口切换而自动关闭，
    // 便于自动化脚本（PowerShell / Python 等持有前台）操作已打开的菜单。
    // 由 ui_debug_set_menu_autoclose(0) 打开。
    static bool g_debugSuppressAutoClose;

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

    // ---- Debug / simulation access ----
    // 用于从 debug API 查询和模拟菜单交互，不直接暴露 items_
    int  ItemCount() const { return (int)items_.size(); }
    int  ItemIdAt(int index) const;          // -1 out of range / separator
    bool ItemEnabled(int index) const;
    bool ItemIsSeparator(int index) const;
    int  FindIndexById(int id) const;         // -1 if not found
    HWND ParentHwnd() const { return parentHwnd_; }
    HWND PopupHwnd() const { return popupHwnd_; }

    // 模拟"用户点击某一项"，等同 HandleMouseUp 命中 + 回传 WM_APP+100 + Close
    // 成功返回 true，失败（index 越界、分隔符、禁用、无父窗口）返回 false。
    bool SimulateClickIndex(int index);

    // 子菜单访问与路径点击 —— path=[2,1] 即"顶层第 2 项的 submenu 里第 1 项"。
    // 路径上所有中间节点必须是 enabled 的 submenu、leaf 是 enabled 的非分隔项。
    // 成功后把 leaf 的 id 回传给 ROOT 菜单的 parentHwnd（主窗口）并 Close 整条链。
    std::shared_ptr<ContextMenu> SubmenuAt(int index) const;
    int  ItemCountAtPath(const int* path, int depth) const;
    int  ItemIdAtPath(const int* path, int depth) const;      // -1 = invalid
    bool HasSubmenuAtPath(const int* path, int depth) const;
    bool SimulateClickPath(const int* path, int depth);

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
