#pragma once
#include <windows.h>
#include "overlay_service.h"
#include "render_handles.h"
#include "renderer.h"
#include "theme.h"
#include "widget.h"
#include <atomic>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace ui {

class ContextMenu;
using ContextMenuPtr = std::shared_ptr<ContextMenu>;

/* BREAKING (build 75 / L17 follow-up): MenuItem 重设计成 widget slot.
 * 老的 text / icon / hasIcon / bitmap / hasColor / overrideColor / SvgIcon
 * 字段全删, 由 customContent (一棵完整 widget tree) 接管渲染. shortcut /
 * submenu / id / enabled / isSeparator 这些 menu 语义相关的留下. */
struct MenuItem {
    int id = 0;                 // 数字 id (内部 hit-test/debug); 非数字 id 时 autoId
    std::string  strId;         // 原始 id 字符串 (key 或数字串), C callback 用
    std::vector<std::pair<std::string,std::string>> attrs;  // 全部静态属性 (含 id)
    std::wstring shortcut;
    bool enabled = true;
    bool isSeparator = false;
    ContextMenuPtr submenu;
    /* build 303: 声明式 @click / onclick, 已绑定本行的迭代变量。空 = 没写。 */
    std::function<void()> action;
    /* 整个 item row 的 content widget tree — 跟普通 widget 同款渲染路径
     * (CSS cascade / reactive bindings / OnDraw / HitTest). separator
     * 时为 null. PageState::PopulateMenuItem 实例化并装入. */
    WidgetPtr customContent;
};

/* 一次菜单点击的载荷 — 点击时 heap-new, post 给父窗口 WM_APP+100, 回调读完
 * delete (解决菜单 popup transient 的生命周期)。C API UiMenuItem 即指向它。
 * 暴露点击项的 id 字符串 + 全部静态属性, 让宿主按 key / data-* 路由。 */
struct MenuClickInfo {
    std::string id;                                          // "id" attr (key 或数字串), 可空
    std::vector<std::pair<std::string,std::string>> attrs;   // 该项全部属性 (含 id)
    std::function<void()> action;   // 声明式 @click (有则由窗口先执行, 见 WM_APP+100)
    const char* Attr(const char* name) const {
        if (!name) return nullptr;
        for (const auto& kv : attrs) if (kv.first == name) return kv.second.c_str();
        return nullptr;
    }
};

class ContextMenu : public std::enable_shared_from_this<ContextMenu>,
                    public OverlayWindowHandler {
public:
    // Debug 模拟模式：为 true 时菜单 popup 的 WM_TIMER 不会因为前台窗口切换而自动关闭，
    // 便于自动化脚本（PowerShell / Python 等持有前台）操作已打开的菜单。
    // 由 ui_debug_set_menu_autoclose(0) 打开。
    static bool g_debugSuppressAutoClose;

    /* BREAKING (build 75): 老的 AddItem / AddItemEx / AddItemBitmap /
     * SetLastItemColor 全删. 唯一的 build 入口是 AddItemContent —
     * 调用方负责给一棵 widget tree, lib 负责 layout + render + hit-test.
     * shortcut 仍是 menu 框架渲染 (右对齐, 不跟 content widget tree 同色). */
    void AddItemContent(int id, const std::wstring& shortcut, WidgetPtr content);
    /* 给最近 AddItemContent 的项补字符串 id + 全部属性 (反应式 menu 走声明式
     * .uix 时, PageState::PopulateMenuItem 在 AddItemContent 后调)。供点击回调
     * 暴露 key / data-* 给宿主。老 imperative 路径不调 → strId/attrs 空。 */
    void SetLastItemMeta(std::string strId,
                         std::vector<std::pair<std::string,std::string>> attrs);
    void AddSeparator();
    void AddSubmenu(WidgetPtr entryContent, ContextMenuPtr submenu);
    void SetEnabled(int id, bool enabled);
    /* 只改最近 Add 的那一项 —— 声明式 populate 用。按 id 批量改会连累同一
     * v-for 的其它行 (它们曾共用编译期 id), submenu 行 id 又恒为 -1 改不到。 */
    void SetLastItemEnabled(bool enabled);
    void SetLastItemAction(std::function<void()> action);
    void SetBgColor(D2D1_COLOR_F color) { bgColor_ = color; hasBgColor_ = true; }
    void SetFrostedMaterial(bool enabled) { frostedMaterial_ = enabled; ReleaseBackdropResource(); }
    void SetBackdropBlur(float radius) { backdropBlurRadius_ = radius; ReleaseBackdropResource(); }
    /* Build 69+ (L19): 设单个菜单的圆角半径. <0 表示恢复 kCornerRadius 默认.
     * 影响 shadow + card bg 的 FillRoundedRect, hover item highlight (6px)
     * 不动 (那是 item 级视觉不属于容器圆角). */
    void SetCornerRadius(float r) { cornerRadius_ = r; }
    float CornerRadius() const {
        return cornerRadius_ >= 0.0f ? cornerRadius_ : kCornerRadius;
    }

    /* popup 窗口现在是"关了也留着复用"的 (见 HidePopupWindow), 所以对象
     * 析构时必须真销毁, 否则隐藏窗口活到进程结束. */
    ~ContextMenu();

    /* popup 菜单共享的 D3D / DComp 设备是进程内懒建的, ui_shutdown 收尾时放掉.
     * 跟 Renderer::ReleaseOffscreenDevice 同款口径. */
    static void ReleaseSharedGpu();

    /* 打开动画 (对标 WinUI MenuPopupThemeTransition, 见 menu_open_motion.h)。进程全局,
     * 下次弹出生效; 由 DComp 合成器驱动, 不占 UI / overlay 线程。timeScale 是调试用的
     * 放慢倍数, 夹在 [1, 20]。 */
    static void SetOpenAnimationEnabled(bool enabled);
    static bool OpenAnimationEnabled();
    static void SetOpenAnimationTimeScale(float scale);

    // Show / hide
    // parentHwnd: owner window; x,y: screen coordinates
    void Show(float x, float y, const D2D1_RECT_F& viewport);
    void ShowPopup(HWND parentHwnd, int screenX, int screenY);
    void Close();
    bool IsVisible() const { return visible_; }

    // Build 73 (L17): 反应式菜单支持. PageState::WireSubtreeMenus 给走
    // 声明式 <menu> 路径的菜单挂这个 hook —— 每次 Show/ShowPopup 入口
    // 先调它 (它内部 Clear() 再 Populate*), 把 items 按当前 JS state 重建.
    // 老的 imperative ui_menu_add_item / SetLastItemColor 用法不设此 hook,
    // items 保持调用方建好的样子. 设为 nullptr 等价禁用.
    void SetBeforeShowHook(std::function<void()> hook) {
        beforeShowHook_ = std::move(hook);
    }
    // 清空 items_ 用于 reactive rebuild. submenu shared_ptr 自然 release;
    // 调用方应当在 PopulateMenu 内部立即重 build, 否则下次 Show 是空菜单.
    void Clear();

    /* Build 77+: 给 v-if 切换 items 集合的反应式菜单用 — caller (PageState)
     * 扫所有 CompiledMenuItems 的 shortcut (含 v-if=false 隐藏的) 算 max,
     * 喂这个字段后 MenuWidth 把它当 shortcut 列宽 floor, 隐藏 item 也预留
     * shortcut 空间. 结果: 无图 / 有图 menu width 一致. <=0 = 不预留. */
    void SetReservedShortcutWidth(float w) { reservedShortcutWidth_ = w; InvalidateLayout(); }
    /* Build 80+: 同上, submenu arrow col 预留 — 任何 item 有 submenu (含
     * v-if=false 的) 都让 arrow col 永远占位, 两态视觉宽度一致. */
    void SetReservedHasSubmenu(bool has) { reservedHasSubmenu_ = has; InvalidateLayout(); }
    /* Build 81+: 同上 — content (wrapper SizeHint) max 也按"全 items"预留,
     * 两态彻底同款宽度. PageState 扫所有 items 的 wrapper natural width
     * 算 max, 喂这里. */
    void SetReservedContentWidth(float w) { reservedContentWidth_ = w; InvalidateLayout(); }
    /* Build 85+: 跨菜单树共享同款宽度 — PageState 把整树 (parent + 所有
     * submenu) 计算出的最大 MenuWidth 回写到每个 ContextMenu, submenu 至少
     * 跟主菜单同宽. 用户视觉感受 "菜单系列宽度一致". */
    void SetMinPropagatedWidth(float w) { minPropagatedWidth_ = w; InvalidateLayout(); }
    /* build 307: 本菜单自己的最小宽度 (<menu min-width>), 0 = 不限, 不传给子菜单。 */
    void SetMinWidth(float w) { minWidth_ = w; InvalidateLayout(); }
    /* build 85: expose MenuWidth — PageState 算完 reserved 后查 final 宽,
     * 再 propagate 到 submenu. */
    float MenuWidth() const;
    float MenuHeight() const;
    /* 快捷键列宽 (含 16 DIP 呼吸), 宽度 / 排版 / 绘制共用; 0 = 无快捷键。 */
    float ShortcutColumnWidth() const;

    // Rendering
    void Draw(Renderer& r);

    // Event handling — returns true if consumed
    bool HandleMouseMove(float x, float y);
    bool HandleMouseDown(float x, float y);
    bool HandleMouseUp(float x, float y);

    // Hit result after HandleMouseUp returns true
    int ClickedItemId() const { return clickedId_; }
    // 点击项的字符串 id + 全部属性 (submenu 点击会沿链传播到 root)。
    const std::string& ClickedItemStrId() const { return clickedStrId_; }
    const std::vector<std::pair<std::string,std::string>>& ClickedItemAttrs() const { return clickedAttrs_; }
    const std::function<void()>& ClickedItemAction() const { return clickedAction_; }

    // Geometry
    D2D1_RECT_F Bounds() const;
    bool Contains(float x, float y) const;

    // ---- Debug / simulation access ----
    // 用于从 debug API 查询和模拟菜单交互，不直接暴露 items_
    int  ItemCount() const { return (int)items_.size(); }
    const MenuItem* ItemAt(int index) const {
        return (index >= 0 && index < (int)items_.size()) ? &items_[index] : nullptr;
    }
    int  ItemIdAt(int index) const;          // -1 out of range / separator
    bool ItemEnabled(int index) const;
    bool ItemIsSeparator(int index) const;
    int  FindIndexById(int id) const;         // -1 if not found
    HWND ParentHwnd() const { return parentHwnd_; }
    HWND PopupHwnd() const { return popupHwnd_; }

    // Build 27: 把当前 popup 内容存成 PNG. render-thread present 启用后，
    // 这里是显式同步 screenshot request，不在 UI 线程读 D2D target。
    int Screenshot(const wchar_t* outPath);

    // 模拟"用户点击某一项"，等同 HandleMouseUp 命中 + 回传 WM_APP+100 + Close
    // 成功返回 true，失败（index 越界、分隔符、禁用、无父窗口）返回 false。
    bool SimulateClickIndex(int index);

    /* Build 85+ (测试用): 模拟 "鼠标 hover 到 submenu 入口" — 在指定 index
     * 的 submenu 上调 ShowPopup, 跟 HandleMouseMove 实际 hover 触发同款效果.
     * 调试 / 截图 submenu 视觉用. 返 sub ContextMenu* (失败时 nullptr).
     * 链式: parent->OpenSubmenuAt(i)->OpenSubmenuAt(j) 走深层 submenu. */
    ContextMenu* OpenSubmenuAt(int index);

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
    /* hover 到带 submenu 的行后, 等 kSubmenuHoverDelayMs 才真开 —— 快速划过
     * 整张菜单时一个 popup 都不建. -1 = 没有待开的. */
    int pendingSubmenuIndex_ = -1;
    int clickedId_ = -1;
    std::string  clickedStrId_;                                          // 点击项字符串 id (跟 clickedId_ 同步设)
    std::vector<std::pair<std::string,std::string>> clickedAttrs_;       // 点击项全部属性
    std::function<void()> clickedAction_;                                 // 点击项的声明式 @click
    /* 记下点击项 (id + 属性 + 动作), 并生成投递给父窗口的载荷。 */
    void RecordClick(const MenuItem& item);
    void RecordClickFrom(const ContextMenu& sub);
    MenuClickInfo* MakeClickInfo() const {
        return new MenuClickInfo{clickedStrId_, clickedAttrs_, clickedAction_};
    }
    D2D1_COLOR_F bgColor_ = {};
    bool hasBgColor_ = false;
    bool frostedMaterial_ = false;
    /* Frosted-material blur radius in DIP. -1 = default auto radius, 0 = no
     * backdrop capture, >0 = explicit caller-controlled radius. The material
     * itself is controlled by frostedMaterial_. */
    float backdropBlurRadius_ = -1.0f;
    /* Build 69+ (L19): per-menu 圆角. -1 = 用 kCornerRadius 默认. 公共 API
     * ui_menu_set_corner_radius 写这个字段. */
    float cornerRadius_ = -1.0f;
    /* Build 73 (L17): reactive menu — 反应式 .uix 走 PageState 注册的 hook
     * 在 Show 入口先重 populate items. 详见 h::SetBeforeShowHook. */
    std::function<void()> beforeShowHook_;
    /* Build 77+: PageState 算的 "max shortcut across all items including
     * v-if=false hidden" 宽度. MenuWidth 用它做 shortcut col floor. */
    float reservedShortcutWidth_ = 0.0f;
    /* Build 80+: 同上 — 任何 item 有 submenu (含 v-if=false 的) 时 true,
     * MenuWidth 永远预留 arrow col. */
    bool reservedHasSubmenu_ = false;
    /* Build 81+: max wrapper natural width across all items (含 v-if 隐的). */
    float reservedContentWidth_ = 0.0f;
    /* Build 85+: tree-wide max MenuWidth, submenu 至少跟主菜单同宽. */
    float minPropagatedWidth_ = 0.0f;
    float minWidth_ = 0.0f;
    bool layoutValid_ = false;
    float layoutWidth_ = 0.0f;
    float layoutHeight_ = 0.0f;
    std::vector<D2D1_RECT_F> itemRects_;

    // Layout constants — compact menu (13px body, small icons, tight rows).
    static constexpr float kItemHeight = 30.0f;
    static constexpr float kSepHeight = 7.0f;
    static constexpr float kIconColWidth = 36.0f;       // left-pad + icon + gap
    static constexpr float kPadding = 6.0f;             // container inset
    static constexpr float kCornerRadius = 10.0f;
    /* Build 83+: kMinWidth 250 → 120 — kMinWidth 是"什么都没指定"时的兜底,
     * 不该 override 用户 CSS 显式 width / max-width. 降到一个 sanity 下限,
     * 让 caller 的 .menuitem-row { width: NNN } 真正生效, 不被 floor 挡住. */
    static constexpr float kMinWidth = 120.0f;
    static constexpr float kShadowOffset = 4.0f;
    static constexpr float kSubmenuArrowWidth = 20.0f;
    /* Build 86+: submenu 跟 parent menu 之间的 X 方向重叠 (DIP). 正值 =
     * submenu 往左移, 跟 parent 右边重叠. macOS / Win11 风格 submenu 微
     * 微叠在 parent 上. */
    static constexpr float kSubmenuOverlap = 6.0f;
    static constexpr float kFontSize     = 13.5f;       // body text size
    static constexpr float kShortcutFont = 13.5f;
    // Soft drop shadow margin around the popup card. The popup HWND is
    // expanded by 2× this on each axis; menu content draws at offset
    // (kShadowMargin, kShadowMargin), leaving room for the blurred shadow
    // halo to spread outward.
    static constexpr float kShadowMargin = 18.0f;
    /* submenu hover 延时开 (Win11 / macOS 同款手感). 实测一次建 popup 要
     * ~107ms, 划过就开会把 OverlayService 的消息泵堵死. */
    static constexpr UINT kSubmenuHoverTimerId = 2;
    static constexpr UINT kSubmenuHoverDelayMs = 160;
    static constexpr UINT kAutoCloseTimerId = 1;

    // (MenuWidth / MenuHeight 移到 public)
    bool HasAnyIcon() const;
    void InvalidateLayout();
    void LayoutItems();
    D2D1_RECT_F ItemRect(int index) const;
    int HitTest(float x, float y) const;

    // Popup window and renderer live on OverlayService's window thread.
    HWND popupHwnd_ = nullptr;
    Renderer popupRenderer_;
    HWND parentHwnd_ = nullptr;
    /* 建 popup 窗口时用的 owner. 复用前要比对 —— owner 变了只能重建. */
    HWND popupOwnerHwnd_ = nullptr;
    bool popupRendererReady_ = false;
    int popupWidthPx_ = 0;
    int popupHeightPx_ = 0;
    UINT popupDpi_ = 0;
    ComPtr<ID3D11Device> popupD3DDevice_;
    ComPtr<ID2D1Device> popupD2DDevice_;
    ComPtr<ID2D1DeviceContext> popupD2DContext_;
    ComPtr<IDXGISwapChain1> popupSwapChain_;
    ComPtr<ID2D1Bitmap1> popupTargetBitmap_;
    ComPtr<IUnknown> popupDcompDevice_;
    ComPtr<IUnknown> popupDcompTarget_;
    ComPtr<IUnknown> popupDcompVisual_;
    /* 打开动画: 挂在 popupDcompVisual_ 上的裁剪框 + 透明度效果组。 */
    ComPtr<IUnknown> popupDcompClip_;
    ComPtr<IUnknown> popupDcompEffect_;
    /* 这次弹出被屏幕边缘往上推了 (落在锚点上方): 打开动画改成从下往上展开。 */
    bool openUpward_ = false;
    ResourceKey backdropResourceKey_{};
    // 父级菜单（子菜单对象里指向打开它的那个菜单）。ROOT 为 nullptr。
    // 用于子菜单 leaf 被点击后沿链向上 Close，避免 root 菜单残留。
    ContextMenu* parentMenu_ = nullptr;

    void CreatePopupWindow(HWND parent, int screenX, int screenY);
    /* 复用已有窗口: 移到新位置 + 重绘 + 显示. 省掉 CreateWindowEx (~37ms)
     * 和第一次 ShowWindow 的 DWM 开销 (~35ms). */
    void ReshowPopupWindow(HWND parent, int screenX, int screenY);
    /* Close 走这条: 只 SW_HIDE, 保留 hwnd / swapchain / target bitmap. */
    void HidePopupWindow();
    void DestroyPopupWindow();
    void OpenSubmenuPopup(int index);
    void OpenPendingSubmenu();
    void CancelPendingSubmenu();
    void CloseOpenSubmenu();
    void CaptureBackdrop(int screenX, int screenY, int width, int height, float dpiScale);
    void ReleaseBackdropResource();
    /* 弹出 = 截屏 + 模糊 + 移位 + 绘制 + 显示, 整段投到 overlay 线程异步做,
     * 宿主 UI 线程不等 (以前同步等 60ms+, 宿主动图停帧)。几何在 UI 线程算好传入。 */
    void PostShowPopup(HWND hwnd, int screenX, int screenY, int w, int h,
                       int marginPx, int cardW, int cardH, float dpiScale, bool move,
                       const char* traceName);
    /* 屏障: 还有没做完的异步弹出就等它做完。UI 线程要改菜单状态 (再次 Show
     * 重建 items_ / Close / 销毁窗口) 之前调, 避免和 overlay 线程上的绘制同时
     * 读写。没有挂起的弹出时零开销。 */
    void WaitPendingShow();
    std::atomic<int> pendingShows_{0};
    bool EnsurePopupRenderer(int widthPx, int heightPx);
    bool BindPopupTarget(int widthPx, int heightPx);
    void ReleasePopupRenderer();
    void RenderPopupFrame();
    /* 显示前调 (overlay 线程): 开关打开时给 visual 挂好打开动画, 关闭时复位成静止
     * 终态。窗口复用, 每次弹出都要重设。返回是否真的挂上了动画。 */
    bool ApplyOpenAnimation(int windowW, int windowH, int marginPx, int cardH, bool upward);
    int  WritePopupScreenshot(const wchar_t* outPath);

    LRESULT HandleOverlayMessage(HWND hwnd, UINT msg, WPARAM wParam,
                                 LPARAM lParam, bool& handled) override;
};

} // namespace ui
