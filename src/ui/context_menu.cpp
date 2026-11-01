#include "context_menu.h"
#include "resource_store.h"
#include "debug_trace.h"
#include "exit_safe_com.h"
#include "box_blur.h"
#include "menu_open_motion.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <cstdint>
#include <cstring>
#include <windowsx.h>
#include <dcomp.h>
#include <dwmapi.h>

/* Windows 10 1607+ 提供 GetDpiForWindow。本项目 CMakeLists 定义 _WIN32_WINNT=0x0A00
 * 后系统头会声明它，此处 fallback 只在老 SDK 上启用。*/
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0A00
static inline UINT GetDpiForWindow(HWND hwnd) {
    HMODULE hModule = LoadLibraryW(L"user32.dll");
    if (hModule) {
        typedef UINT(WINAPI* PFN_GetDpiForWindow)(HWND);
        PFN_GetDpiForWindow pfn = (PFN_GetDpiForWindow)GetProcAddress(hModule, "GetDpiForWindow");
        if (pfn) {
            UINT dpi = pfn(hwnd);
            FreeLibrary(hModule);
            return dpi;
        }
        FreeLibrary(hModule);
    }
    HDC hdc = GetDC(hwnd);
    UINT dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(hwnd, hdc);
    return dpi ? dpi : 96;
}
#endif

namespace ui {

bool ContextMenu::g_debugSuppressAutoClose = false;

namespace {
/* UI 线程写, overlay 线程弹出时读。 */
std::atomic<bool>  g_openAnimEnabled{true};
std::atomic<float> g_openAnimTimeScale{1.0f};
}  // namespace

void ContextMenu::SetOpenAnimationEnabled(bool enabled) { g_openAnimEnabled.store(enabled); }
bool ContextMenu::OpenAnimationEnabled() { return g_openAnimEnabled.load(); }
void ContextMenu::SetOpenAnimationTimeScale(float scale) {
    g_openAnimTimeScale.store(std::clamp(scale, 1.0f, 20.0f));
}

namespace {

/* Menu perf probe. TraceEvent is a no-op when no sink is attached and QPC is
 * ~20ns, so these can stay on the interactive path permanently. */
struct MenuStopwatch {
    LARGE_INTEGER start{};
    MenuStopwatch() { QueryPerformanceCounter(&start); }
    void Reset() { QueryPerformanceCounter(&start); }
    double Ms() const {
        LARGE_INTEGER now{}, freq{};
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        if (freq.QuadPart <= 0) return 0.0;
        return (double)(now.QuadPart - start.QuadPart) * 1000.0 / (double)freq.QuadPart;
    }
};

uint64_t NextPopupBackdropGeneration() {
    static std::atomic<uint64_t> next{1ull << 61};
    return next.fetch_add(1, std::memory_order_relaxed);
}

/* Popup 菜单共享的 GPU 设备 —— 进程内复用。
 *
 * 原实现每个 popup (根菜单 + 每个 submenu) 各建一整套 D3D11 + DComp。实测
 * D3D11CreateDevice 单次 32ms, 而 submenu 是 hover 划过就建、离开就毁, 一趟
 * 上下扫菜单要付十几次。跟 renderer.cpp 的离屏设备复用是同一个道理。
 *
 * 所有 popup 渲染都在 OverlayService 的窗口线程上, 不跨线程; 这里的 mutex
 * 只保护"懒建"本身 (ShowPopup 可能从宿主 UI 线程发起)。 */
std::mutex g_popupGpuMu;
/* 退出阶段不 Release (见 exit_safe_com.h); 正常释放走 ReleaseSharedGpu。 */
ui::ExitLeakComPtr<ID3D11Device> g_popupD3D;
ui::ExitLeakComPtr<IDCompositionDevice> g_popupDcomp;

ComPtr<ID3D11Device> CreateOverlayD3DDevice() {
    ComPtr<ID3D11Device> dev;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT |
                 D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;
    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
    };
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   flags, levels, _countof(levels),
                                   D3D11_SDK_VERSION, dev.GetAddressOf(),
                                   nullptr, nullptr);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                               flags, levels, _countof(levels),
                               D3D11_SDK_VERSION, dev.GetAddressOf(),
                               nullptr, nullptr);
        if (FAILED(hr)) {
            dev.Reset();
        }
    }
    return dev;
}

bool EnsureSharedPopupGpu(ComPtr<ID3D11Device>& d3d,
                          ComPtr<IDCompositionDevice>& dcomp) {
    std::lock_guard<std::mutex> lk(g_popupGpuMu);
    if (!g_popupD3D || !g_popupDcomp) {
        g_popupDcomp.Reset();
        g_popupD3D.Reset();

        ComPtr<ID3D11Device> dev = CreateOverlayD3DDevice();
        if (!dev) return false;
        ComPtr<IDXGIDevice> dxgi;
        if (FAILED(dev.As(&dxgi)) || !dxgi) return false;
        ComPtr<IDCompositionDevice> dc;
        if (FAILED(DCompositionCreateDevice(dxgi.Get(), __uuidof(IDCompositionDevice),
                                            reinterpret_cast<void**>(dc.GetAddressOf()))) ||
            !dc) {
            return false;
        }
        g_popupD3D = dev;
        g_popupDcomp = dc;
    }
    d3d = g_popupD3D;
    dcomp = g_popupDcomp;
    return true;
}

/* 设备丢失 (DXGI_ERROR_DEVICE_REMOVED / RESET / D2DERR_RECREATE_TARGET) 时
 * 必须把共享设备也丢掉, 否则所有 popup 一起永久黑屏。 */
void DropSharedPopupGpu() {
    std::lock_guard<std::mutex> lk(g_popupGpuMu);
    g_popupDcomp.Reset();
    g_popupD3D.Reset();
}

bool IsDeviceLost(HRESULT hr) {
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET ||
           hr == D2DERR_RECREATE_TARGET;
}

bool CaptureScreenBgra(int screenX, int screenY, int width, int height,
                       std::vector<uint8_t>& out) {
    if (width <= 0 || height <= 0) return false;
    out.assign((size_t)width * (size_t)height * 4, 0);

    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    HDC mem = CreateCompatibleDC(screen);
    if (!mem) {
        ReleaseDC(nullptr, screen);
        return false;
    }

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        return false;
    }

    HGDIOBJ old = SelectObject(mem, bmp);
    BOOL ok = BitBlt(mem, 0, 0, width, height, screen, screenX, screenY,
                     SRCCOPY | CAPTUREBLT);
    if (ok) {
        std::memcpy(out.data(), bits, out.size());
        for (size_t i = 3; i < out.size(); i += 4) out[i] = 255;
    }

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return ok == TRUE;
}

}  // namespace

// ---- Build (build 75 BREAKING: widget-tree based items) ----

void ContextMenu::AddItemContent(int id, const std::wstring& shortcut,
                                  WidgetPtr content) {
    MenuItem item;
    item.id = id;
    item.shortcut = shortcut;
    item.customContent = std::move(content);
    items_.push_back(std::move(item));
    InvalidateLayout();
}

void ContextMenu::SetLastItemMeta(std::string strId,
                                   std::vector<std::pair<std::string,std::string>> attrs) {
    if (items_.empty()) return;
    items_.back().strId = std::move(strId);
    items_.back().attrs = std::move(attrs);
}

void ContextMenu::AddSeparator() {
    MenuItem item;
    item.isSeparator = true;
    items_.push_back(std::move(item));
    InvalidateLayout();
}

void ContextMenu::AddSubmenu(WidgetPtr entryContent, ContextMenuPtr submenu) {
    MenuItem item;
    item.id = -1;
    item.customContent = std::move(entryContent);
    item.submenu = std::move(submenu);
    items_.push_back(std::move(item));
    InvalidateLayout();
}

void ContextMenu::SetEnabled(int id, bool enabled) {
    for (auto& item : items_) {
        if (item.id == id) item.enabled = enabled;
    }
}

void ContextMenu::SetLastItemEnabled(bool enabled) {
    if (!items_.empty()) items_.back().enabled = enabled;
}

void ContextMenu::SetLastItemAction(std::function<void()> action) {
    if (!items_.empty()) items_.back().action = std::move(action);
}

void ContextMenu::RecordClick(const MenuItem& item) {
    clickedId_ = item.id;
    clickedStrId_ = item.strId;
    clickedAttrs_ = item.attrs;
    clickedAction_ = item.action;
}

void ContextMenu::RecordClickFrom(const ContextMenu& sub) {
    clickedId_ = sub.clickedId_;
    clickedStrId_ = sub.clickedStrId_;     // 沿链向上传播 id+属性+动作
    clickedAttrs_ = sub.clickedAttrs_;
    clickedAction_ = sub.clickedAction_;
}

ContextMenu::~ContextMenu() {
    /* popup 窗口是 Close 之后留着复用的, 对象没了才真销毁. */
    DestroyPopupWindow();
}

void ContextMenu::ReleaseSharedGpu() {
    DropSharedPopupGpu();
}

// ---- Reactive rebuild (build 73 / L17) ----

ContextMenu* ContextMenu::OpenSubmenuAt(int index) {
    /* Build 85: mimic HandleMouseMove submenu-open path. 调试 / 测试用,
     * 没真鼠标 hover 时也能打开 submenu (拍截图验证). */
    if (index < 0 || index >= (int)items_.size()) return nullptr;
    auto& item = items_[index];
    if (item.isSeparator || !item.submenu) return nullptr;

    /* 调试入口不走 hover 延时, 立刻开. */
    CancelPendingSubmenu();
    OpenSubmenuPopup(index);
    return item.submenu.get();
}

/* hover / 调试共用的"真正把 submenu 弹出来"。openSubmenuIndex_ 只在这里置位 ——
 * 延时期间它仍是 -1, 免得 hit-test 和重绘判定认为子菜单已经开了。 */
void ContextMenu::OpenSubmenuPopup(int index) {
    if (index < 0 || index >= (int)items_.size()) return;
    auto& item = items_[index];
    if (item.isSeparator || !item.submenu) return;

    if (openSubmenuIndex_ != index) CloseOpenSubmenu();
    openSubmenuIndex_ = index;
    auto& sub = item.submenu;
    sub->parentMenu_ = this;  // leaf 点击后可沿链 Close
    if (popupHwnd_) {
        RECT rc{};
        GetWindowRect(popupHwnd_, &rc);
        D2D1_RECT_F ir = ItemRect(index);
        // rc 是 hwnd 外圈, 含 kShadowMargin 的 drop-shadow padding.
        // 减掉 marginPx 拿"可见卡片"的右边作 submenu anchor — 否则
        // submenu 跟父菜单之间留 ~18px (高 DPI 更大) 透明空白 (L17).
        UINT subDpi = GetDpiForWindow(popupHwnd_);
        float subScale = (float)subDpi / 96.0f;
        int marginPx = (int)(kShadowMargin * subScale);
        /* Build 86+: 减 kSubmenuOverlap 让 submenu 往左挪, 跟 parent 右边重叠. */
        int overlapPx = (int)(kSubmenuOverlap * subScale);
        int subX = rc.right - marginPx - overlapPx;
        int subY = rc.top + (int)(ir.top * subScale);
        sub->ShowPopup(parentHwnd_ ? parentHwnd_ : popupHwnd_, subX, subY);
    } else {
        D2D1_RECT_F ir = ItemRect(index);
        D2D1_RECT_F vp = Bounds();
        vp.right += 400; vp.bottom += 400;
        sub->Show(ir.right - 4, ir.top, vp);
    }
}

/* hover 延时到点: 鼠标还停在原来那行才开。 */
void ContextMenu::OpenPendingSubmenu() {
    int index = pendingSubmenuIndex_;
    pendingSubmenuIndex_ = -1;
    if (index < 0 || index != hoveredIndex_) return;
    if (index >= (int)items_.size()) return;
    const auto& item = items_[index];
    if (item.isSeparator || !item.submenu || !item.enabled) return;
    OpenSubmenuPopup(index);
}

void ContextMenu::CancelPendingSubmenu() {
    if (pendingSubmenuIndex_ < 0) return;
    pendingSubmenuIndex_ = -1;
    if (!popupHwnd_) return;
    HWND hwnd = popupHwnd_;
    /* KillTimer 必须在建 timer 的那个线程上调 —— popup 消息全在
     * OverlayService 线程, 同线程时 InvokeSync 就是直接调用。 */
    OverlayService::Instance().InvokeSync([hwnd]() {
        if (IsWindow(hwnd)) KillTimer(hwnd, kSubmenuHoverTimerId);
    });
}

void ContextMenu::CloseOpenSubmenu() {
    if (openSubmenuIndex_ >= 0 && openSubmenuIndex_ < (int)items_.size()) {
        auto& sub = items_[openSubmenuIndex_].submenu;
        if (sub) sub->Close();
    }
    openSubmenuIndex_ = -1;
}

void ContextMenu::Clear() {
    /* 反应式 rebuild 入口: 把 items_ 全清掉, 让 PopulateMenu 重新喂.
     * submenu shared_ptr 在这一刻 release; 如果用户正打开 submenu, 那条
     * pointer 会变 dangling — 但 reactive 重 build 只发生在 Show 入口,
     * 那时 Close 已经把 openSubmenuIndex_ 重置. 安全. */
    WaitPendingShow();   /* 上一次异步弹出可能还在 overlay 线程上读 items_ */
    items_.clear();
    hoveredIndex_ = -1;
    openSubmenuIndex_ = -1;
    clickedId_ = -1;
    clickedAction_ = nullptr;  // 动作可能持有 JS 值, 随 items 一起放掉
    InvalidateLayout();
}

// ---- Show / Hide ----

void ContextMenu::Show(float x, float y, const D2D1_RECT_F& viewport) {
    /* Build 73 (L17): reactive .uix 菜单先 rebuild items 再算宽高. 老的
     * imperative 路径没设 hook, items 早就建好, no-op. */
    if (beforeShowHook_) beforeShowHook_();

    float w = MenuWidth();
    float h = MenuHeight();
    if (x + w > viewport.right) x = viewport.right - w;
    if (y + h > viewport.bottom) y = viewport.bottom - h;
    if (x < viewport.left) x = viewport.left;
    if (y < viewport.top) y = viewport.top;
    x_ = x; y_ = y;
    visible_ = true;
    hoveredIndex_ = -1;
    openSubmenuIndex_ = -1;
    clickedId_ = -1;
    LayoutItems();
}

void ContextMenu::ShowPopup(HWND parentHwnd, int screenX, int screenY) {
    /* 同 Show: 先 reactive rebuild, 再走原 popup 路径 (clamp/sizing 才能拿
     * 到最新 items 的宽高). 连点右键时上一次异步弹出可能还没做完, 先等它。 */
    WaitPendingShow();
    if (beforeShowHook_) beforeShowHook_();

    parentHwnd_ = parentHwnd;

    // Get DPI scale from parent window
    UINT dpi = GetDpiForWindow(parentHwnd);
    float dpiScale = (float)dpi / 96.0f;

    int pw = (int)(MenuWidth() * dpiScale);
    int ph = (int)(MenuHeight() * dpiScale);

    // Clamp to the monitor under the requested point — based on visual menu rect
    // (not the shadow-padded hwnd). 旧代码用 GetSystemMetrics(SM_CXSCREEN/CYSCREEN)
    // (主屏尺寸) + clamp 到 [0, 主屏宽], 会把副屏的菜单坐标 (screenX 可能 > 主屏宽
    // 或 < 0) 硬拉回主屏 → 副屏右键菜单 / 菜单栏下拉 / submenu 全弹到主屏 (L120)。
    // 改用菜单所在屏 (鼠标点所在 monitor) 的 rcWork (工作区, 顺带避开任务栏)。
    const int requestedY = screenY;
    HMONITOR hMon = MonitorFromPoint(POINT{ screenX, screenY }, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    if (GetMonitorInfoW(hMon, &mi)) {
        const RECT& wa = mi.rcWork;
        if (screenX + pw > wa.right)  screenX = wa.right  - pw;
        if (screenY + ph > wa.bottom) screenY = wa.bottom - ph;
        if (screenX < wa.left) screenX = wa.left;
        if (screenY < wa.top)  screenY = wa.top;
    } else {
        // Fallback: 拿不到 monitor info 时退回主屏 clamp (退化但不崩)。
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        if (screenX + pw > sw) screenX = sw - pw;
        if (screenY + ph > sh) screenY = sh - ph;
        if (screenX < 0) screenX = 0;
        if (screenY < 0) screenY = 0;
    }
    /* 同 WinUI FlyoutBase: 菜单最终落在请求点上方就从下往上展开。 */
    openUpward_ = screenY < requestedY;

    // Menu content is drawn at (kShadowMargin, kShadowMargin) inside the
    // hwnd; the surrounding margin holds the blurred drop shadow.
    x_ = kShadowMargin;
    y_ = kShadowMargin;
    visible_ = true;
    hoveredIndex_ = -1;
    openSubmenuIndex_ = -1;
    clickedId_ = -1;
    LayoutItems();

    // Position the hwnd offset by -kShadowMargin so the visible card lands
    // at the requested screen coordinate.
    int marginPx = (int)(kShadowMargin * dpiScale);
    /* perf: 同一个菜单反复弹出时复用上次的窗口。CreateWindowEx 实测 ~37ms、
     * 首次 ShowWindow 另有 ~35ms 的 DWM 开销, 而菜单对象由 PageState::menus_
     * 长期持有, 窗口完全可以跨 session 留着。owner 换了只能重建。 */
    if (popupHwnd_ && IsWindow(popupHwnd_) && popupOwnerHwnd_ == parentHwnd) {
        ReshowPopupWindow(parentHwnd, screenX - marginPx, screenY - marginPx);
    } else {
        if (popupHwnd_) DestroyPopupWindow();
        CreatePopupWindow(parentHwnd, screenX - marginPx, screenY - marginPx);
    }
}

void ContextMenu::Close() {
    WaitPendingShow();   /* 弹出还在路上就关: 先让它显示完, 下面再按序隐藏 */
    visible_ = false;
    hoveredIndex_ = -1;
    CancelPendingSubmenu();
    CloseOpenSubmenu();
    HidePopupWindow();
    /* Build 88+: debug flag 自动恢复 — `ui_debug_set_menu_autoclose(0)` 是
     * 调试用一次性 helper, 不该跨菜单 session 持续生效. 否则脚本测一次后
     * GuoheView 用户右键所有菜单都不会自动关, 调试痕迹污染业务. 每次菜单
     * 关闭重置, caller 想下一个菜单也抑制就再调一次. */
    g_debugSuppressAutoClose = false;
}

// ---- Debug / simulation accessors ----

int ContextMenu::ItemIdAt(int index) const {
    if (index < 0 || index >= (int)items_.size()) return -1;
    if (items_[index].isSeparator) return -1;
    return items_[index].id;
}

bool ContextMenu::ItemEnabled(int index) const {
    if (index < 0 || index >= (int)items_.size()) return false;
    return items_[index].enabled && !items_[index].isSeparator;
}

bool ContextMenu::ItemIsSeparator(int index) const {
    if (index < 0 || index >= (int)items_.size()) return false;
    return items_[index].isSeparator;
}

int ContextMenu::FindIndexById(int id) const {
    for (int i = 0; i < (int)items_.size(); i++) {
        if (!items_[i].isSeparator && items_[i].id == id) return i;
    }
    return -1;
}

bool ContextMenu::SimulateClickIndex(int index) {
    if (index < 0 || index >= (int)items_.size()) return false;
    const MenuItem& it = items_[index];
    if (it.isSeparator || !it.enabled) return false;
    RecordClick(it);
    // 复刻 overlay popup 鼠标释放分派路径：把 item id + 属性载荷回传父窗口
    if (parentHwnd_) {
        PostMessageW(parentHwnd_, WM_APP + 100, (WPARAM)it.id, (LPARAM)MakeClickInfo());
    }
    Close();
    return true;
}

ContextMenuPtr ContextMenu::SubmenuAt(int index) const {
    if (index < 0 || index >= (int)items_.size()) return nullptr;
    return items_[index].submenu;
}

// 沿 path 前 depth-1 层走到内层菜单；若路径在中途断裂返回 nullptr。
// 最后一层不要求是 submenu —— 调用方根据需要自行处理叶子。
static const ContextMenu* WalkPath(const ContextMenu* root, const int* path, int depth) {
    if (depth < 0 || (!path && depth > 0)) return nullptr;
    const ContextMenu* cur = root;
    for (int i = 0; i < depth - 1; i++) {
        if (!cur) return nullptr;
        int idx = path[i];
        auto sub = cur->SubmenuAt(idx);
        if (!sub) return nullptr;
        if (!cur->ItemEnabled(idx)) return nullptr;
        cur = sub.get();
    }
    return cur;
}

int ContextMenu::ItemCountAtPath(const int* path, int depth) const {
    const ContextMenu* m = WalkPath(this, path, depth + 1);
    // 当 depth==0，要返回自身 count；WalkPath(root, path, 1) 走 0 圈后返回自身。OK。
    return m ? m->ItemCount() : -1;
}

int ContextMenu::ItemIdAtPath(const int* path, int depth) const {
    if (depth < 1) return -1;
    const ContextMenu* m = WalkPath(this, path, depth);
    if (!m) return -1;
    return m->ItemIdAt(path[depth - 1]);
}

bool ContextMenu::HasSubmenuAtPath(const int* path, int depth) const {
    if (depth < 1) return false;
    const ContextMenu* m = WalkPath(this, path, depth);
    if (!m) return false;
    return m->SubmenuAt(path[depth - 1]) != nullptr;
}

bool ContextMenu::SimulateClickPath(const int* path, int depth) {
    if (depth < 1 || !path) return false;
    // 先一路把 path 走到叶子所在的菜单层。
    ContextMenu* cur = this;
    for (int i = 0; i < depth - 1; i++) {
        int idx = path[i];
        if (idx < 0 || idx >= (int)cur->items_.size()) return false;
        const MenuItem& it = cur->items_[idx];
        if (it.isSeparator || !it.enabled || !it.submenu) return false;
        cur = it.submenu.get();
    }
    int leafIdx = path[depth - 1];
    if (leafIdx < 0 || leafIdx >= (int)cur->items_.size()) return false;
    const MenuItem& leaf = cur->items_[leafIdx];
    if (leaf.isSeparator || !leaf.enabled) return false;
    cur->RecordClick(leaf);
    // 回传给 ROOT 菜单的 parentHwnd（主窗口）。
    if (parentHwnd_) {
        PostMessageW(parentHwnd_, WM_APP + 100, (WPARAM)leaf.id,
                     (LPARAM)cur->MakeClickInfo());
    }
    Close();
    return true;
}

// ---- Popup Window ----

void ContextMenu::CreatePopupWindow(HWND parent, int screenX, int screenY) {
    UINT dpi = parent ? GetDpiForWindow(parent) : 96;
    float dpiScale = (float)dpi / 96.0f;
    // Hwnd encompasses both the menu card and the surrounding shadow halo.
    int w = (int)((MenuWidth()  + 2 * kShadowMargin) * dpiScale);
    int h = (int)((MenuHeight() + 2 * kShadowMargin) * dpiScale);
    int marginPx = (int)(kShadowMargin * dpiScale);
    int cardW = std::max<int>(1, (int)(MenuWidth() * dpiScale));
    int cardH = std::max<int>(1, (int)(MenuHeight() * dpiScale));

    OverlayWindowOptions options;
    options.owner = parent;
    options.exStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE |
                      WS_EX_NOREDIRECTIONBITMAP;
    options.style = WS_POPUP;
    options.x = screenX;
    options.y = screenY;
    options.width = w;
    options.height = h;
    options.title = L"";
    MenuStopwatch swCreate;
    MenuStopwatch swStage;
    popupHwnd_ = OverlayService::Instance().CreateHostWindowSync(this, options);
    double windowMs = swStage.Ms();
    swStage.Reset();

    if (!popupHwnd_) return;
    popupOwnerHwnd_ = parent;
    /* 新建的窗口已在目标位置, 不用再移。 */
    PostShowPopup(popupHwnd_, screenX, screenY, w, h, marginPx, cardW, cardH, dpiScale,
                  /*move=*/false, "menu_popup_create");
    TraceEvent("core_menu", "menu_popup_create_ui",
               {TraceF64("window_ms", windowMs),
                TraceF64("ui_ms", swCreate.Ms()),
                TraceBool("submenu", parentMenu_ != nullptr)});
}

void ContextMenu::ReshowPopupWindow(HWND parent, int screenX, int screenY) {
    if (!popupHwnd_) return;

    UINT dpi = parent ? GetDpiForWindow(parent) : 96;
    float dpiScale = (float)dpi / 96.0f;
    int w = (int)((MenuWidth()  + 2 * kShadowMargin) * dpiScale);
    int h = (int)((MenuHeight() + 2 * kShadowMargin) * dpiScale);
    int marginPx = (int)(kShadowMargin * dpiScale);
    int cardW = std::max<int>(1, (int)(MenuWidth() * dpiScale));
    int cardH = std::max<int>(1, (int)(MenuHeight() * dpiScale));
    PostShowPopup(popupHwnd_, screenX, screenY, w, h, marginPx, cardW, cardH, dpiScale,
                  /*move=*/true, "menu_popup_reshow");
}

void ContextMenu::PostShowPopup(HWND hwnd, int screenX, int screenY, int w, int h,
                                int marginPx, int cardW, int cardH, float dpiScale,
                                bool move, const char* traceName) {
    pendingShows_.fetch_add(1);
    MenuStopwatch swQueued;
    const bool upward = openUpward_;
    OverlayService::Instance().InvokeAsync(
        [this, hwnd, screenX, screenY, w, h, marginPx, cardW, cardH, dpiScale, move,
         upward, traceName, swQueued]() {
        const double queueMs = swQueued.Ms();
        MenuStopwatch swTotal;
        MenuStopwatch swStage;
        if (IsWindow(hwnd)) {
            /* 截屏必须在菜单显示之前 (否则截到菜单自己); 同一个任务里先截再显示。 */
            CaptureBackdrop(screenX + marginPx, screenY + marginPx, cardW, cardH, dpiScale);
            const double backdropMs = swStage.Ms();
            swStage.Reset();
            /* 先在隐藏状态下移到新位置并按新尺寸重绘, 再 show —— 否则会先闪一帧
             * 上次的旧内容。
             *
             * 移动时 SWP_NOZORDER: z 序留给下面显示那一步统一处理。 */
            if (move) {
                SetWindowPos(hwnd, nullptr, screenX, screenY, w, h,
                             SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOZORDER |
                             SWP_NOOWNERZORDER);
            }
            const double moveMs = swStage.Ms();
            swStage.Reset();
            RenderPopupFrame();
            /* 动画挂在显示之前: 复用窗口上一次停在终态, 不先复位会闪一帧完整菜单。 */
            const bool animated = ApplyOpenAnimation(w, h, marginPx, cardH, upward);
            const double renderMs = swStage.Ms();
            swStage.Reset();
            /* 不用 ShowWindow(SW_SHOWNOACTIVATE): 它内部要走 owner 链 z 序 + 激活
             * 判定, 实测 33ms。
             *
             * 每次显示都重新插到 HWND_TOPMOST (同系统 TrackPopupMenu): 窗口是复用的,
             * 隐藏期间 z 序会过时 —— owner 取消置顶时系统会连带抹掉它所有 owned
             * 窗口的 WS_EX_TOPMOST, 之后再沿用旧 z 序显示, 菜单会被主窗口压住,
             * 子菜单也可能落在主菜单下面。子菜单总在父菜单之后显示, 插到顶层
             * 自然在父菜单之上。NOOWNERZORDER: 只排菜单自己, 不动 owner。 */
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOSIZE |
                         SWP_NOOWNERZORDER | SWP_NOACTIVATE);
            SetTimer(hwnd, kAutoCloseTimerId, 50, nullptr);
            TraceEvent("core_menu", traceName,
                       {TraceF64("queue_ms", queueMs),
                        TraceF64("backdrop_ms", backdropMs),
                        TraceF64("move_ms", moveMs),
                        TraceF64("render_ms", renderMs),
                        TraceF64("show_ms", swStage.Ms()),
                        TraceF64("total_ms", swTotal.Ms()),
                        TraceI64("items", (int64_t)items_.size()),
                        TraceBool("animated", animated),
                        TraceBool("submenu", parentMenu_ != nullptr)});
        }
        pendingShows_.fetch_sub(1);
    });
}

void ContextMenu::WaitPendingShow() {
    if (pendingShows_.load() <= 0) return;
    OverlayService::Instance().InvokeSync([]() {});   // 同队列, 前面的弹出都做完了
}

void ContextMenu::HidePopupWindow() {
    if (!popupHwnd_) return;
    MenuStopwatch swHide;
    double timerMs = 0.0, hideMs = 0.0;
    HWND hwnd = popupHwnd_;
    OverlayService::Instance().InvokeSync([&, hwnd]() {
        if (!IsWindow(hwnd)) return;
        MenuStopwatch swStage;
        KillTimer(hwnd, kAutoCloseTimerId);
        KillTimer(hwnd, kSubmenuHoverTimerId);
        timerMs = swStage.Ms();
        swStage.Reset();
        /* 同 Reshow: ShowWindow(SW_HIDE) 实测 14~17ms, 全花在 owner z 序和
         * 激活转移上。 */
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOOWNERZORDER | SWP_NOACTIVATE);
        hideMs = swStage.Ms();
    });
    ReleaseBackdropResource();
    TraceEvent("core_menu", "menu_popup_hide",
               {TraceF64("timer_ms", timerMs),
                TraceF64("hide_ms", hideMs),
                TraceF64("total_ms", swHide.Ms()),
                TraceBool("submenu", parentMenu_ != nullptr)});
}

void ContextMenu::DestroyPopupWindow() {
    WaitPendingShow();   /* 异步任务捕获了 this 和 hwnd, 销毁前必须跑完 */
    if (popupHwnd_) {
        MenuStopwatch swDestroy;
        MenuStopwatch swStage;
        HWND hwnd = popupHwnd_;
        OverlayService::Instance().InvokeSync([this, hwnd]() {
            if (IsWindow(hwnd)) {
                KillTimer(hwnd, kAutoCloseTimerId);
                KillTimer(hwnd, kSubmenuHoverTimerId);
            }
            ReleasePopupRenderer();
        });
        double releaseMs = swStage.Ms();
        swStage.Reset();
        OverlayService::Instance().DestroyHostWindowSync(popupHwnd_);
        popupHwnd_ = nullptr;
        popupOwnerHwnd_ = nullptr;
        TraceEvent("core_menu", "menu_popup_destroy",
                   {TraceF64("release_ms", releaseMs),
                    TraceF64("window_ms", swStage.Ms()),
                    TraceF64("total_ms", swDestroy.Ms()),
                    TraceBool("submenu", parentMenu_ != nullptr)});
    }
    ReleaseBackdropResource();
}

void ContextMenu::CaptureBackdrop(int screenX, int screenY, int width, int height,
                                  float dpiScale) {
    ReleaseBackdropResource();
    if (!frostedMaterial_) return;
    if (backdropBlurRadius_ == 0.0f) return;

    MenuStopwatch swStage;
    std::vector<uint8_t> pixels;
    if (!CaptureScreenBgra(screenX, screenY, width, height, pixels)) return;
    const double captureMs = swStage.Ms();
    swStage.Reset();

    int radius = 0;
    if (backdropBlurRadius_ < 0.0f) {
        radius = std::clamp(std::min(width, height) / 18, 8, 18);
    } else {
        radius = std::clamp((int)(backdropBlurRadius_ * dpiScale + 0.5f), 1, 64);
    }
    BoxBlurBgra(pixels, width, height, radius);
    BoxBlurBgra(pixels, width, height, radius);
    const double blurMs = swStage.Ms();
    swStage.Reset();
    backdropResourceKey_ = GlobalResourceStore().AddImage(
        ResourceKind::Bitmap,
        NextPopupBackdropGeneration(),
        width,
        height,
        width * 4,
        PixelFormat::BgraPremul,
        pixels.data());
    TraceEvent("core_menu", "menu_backdrop_capture",
               {TraceF64("capture_ms", captureMs),
                TraceF64("blur_ms", blurMs),
                TraceF64("store_ms", swStage.Ms()),
                TraceI64("w", width),
                TraceI64("h", height),
                TraceI64("radius", radius)});
}

void ContextMenu::ReleaseBackdropResource() {
    if (!backdropResourceKey_.IsValid()) return;
    GlobalResourceStore().Remove(backdropResourceKey_);
    backdropResourceKey_ = {};
}

bool ContextMenu::EnsurePopupRenderer(int widthPx, int heightPx) {
    if (!popupHwnd_) return false;

    if (!popupRendererReady_) {
        MenuStopwatch swInit;
        MenuStopwatch swStage;
        double d3dMs = 0.0, swapMs = 0.0, dcompMs = 0.0;
        ReleasePopupRenderer();

        if (!popupRenderer_.Init()) {
            return false;
        }
        popupRenderer_.SetTextRenderMode(theme::TextRenderMode::Smooth);

        ComPtr<IDCompositionDevice> dcomp;
        if (!EnsureSharedPopupGpu(popupD3DDevice_, dcomp)) {
            ReleasePopupRenderer();
            return false;
        }
        d3dMs = swStage.Ms();
        swStage.Reset();

        ComPtr<IDXGIDevice> dxgiDevice;
        if (FAILED(popupD3DDevice_.As(&dxgiDevice)) || !dxgiDevice) {
            ReleasePopupRenderer();
            return false;
        }

        if (FAILED(popupRenderer_.Factory()->CreateDevice(dxgiDevice.Get(),
                                                          popupD2DDevice_.GetAddressOf())) ||
            !popupD2DDevice_) {
            ReleasePopupRenderer();
            return false;
        }

        if (FAILED(popupD2DDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                        popupD2DContext_.GetAddressOf())) ||
            !popupD2DContext_) {
            ReleasePopupRenderer();
            return false;
        }

        ComPtr<IDXGIAdapter> adapter;
        if (FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf())) || !adapter) {
            ReleasePopupRenderer();
            return false;
        }
        ComPtr<IDXGIFactory2> dxgiFactory;
        if (FAILED(adapter->GetParent(__uuidof(IDXGIFactory2),
                                      reinterpret_cast<void**>(dxgiFactory.GetAddressOf()))) ||
            !dxgiFactory) {
            ReleasePopupRenderer();
            return false;
        }

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = std::max(1, widthPx);
        desc.Height = std::max(1, heightPx);
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

        if (FAILED(dxgiFactory->CreateSwapChainForComposition(
                popupD3DDevice_.Get(), &desc, nullptr, popupSwapChain_.GetAddressOf())) ||
            !popupSwapChain_) {
            ReleasePopupRenderer();
            return false;
        }

        swapMs = swStage.Ms();
        swStage.Reset();

        ComPtr<IDCompositionTarget> target;
        if (FAILED(dcomp->CreateTargetForHwnd(popupHwnd_, TRUE, target.GetAddressOf())) ||
            !target) {
            ReleasePopupRenderer();
            return false;
        }
        ComPtr<IDCompositionVisual> visual;
        if (FAILED(dcomp->CreateVisual(visual.GetAddressOf())) || !visual) {
            ReleasePopupRenderer();
            return false;
        }
        visual->SetContent(popupSwapChain_.Get());
        /* 打开动画用的裁剪框 / 透明度。建不出来只是没有动画, 不算失败。 */
        ComPtr<IDCompositionRectangleClip> clip;
        ComPtr<IDCompositionEffectGroup> effect;
        if (FAILED(dcomp->CreateRectangleClip(clip.GetAddressOf()))) clip.Reset();
        if (FAILED(dcomp->CreateEffectGroup(effect.GetAddressOf()))) effect.Reset();
        target->SetRoot(visual.Get());
        dcomp->Commit();
        dcomp.As(&popupDcompDevice_);
        target.As(&popupDcompTarget_);
        visual.As(&popupDcompVisual_);
        if (clip) clip.As(&popupDcompClip_);
        if (effect) effect.As(&popupDcompEffect_);

        dcompMs = swStage.Ms();
        TraceEvent("core_menu", "menu_popup_device_init",
                   {TraceF64("d3d_ms", d3dMs),
                    TraceF64("swapchain_ms", swapMs),
                    TraceF64("dcomp_ms", dcompMs),
                    TraceF64("total_ms", swInit.Ms()),
                    TraceBool("submenu", parentMenu_ != nullptr)});

        popupRendererReady_ = true;
        popupWidthPx_ = 0;
        popupHeightPx_ = 0;
    }

    return BindPopupTarget(widthPx, heightPx);
}

bool ContextMenu::BindPopupTarget(int widthPx, int heightPx) {
    if (!popupRendererReady_ || !popupD2DContext_ || !popupSwapChain_) return false;

    widthPx = std::max(1, widthPx);
    heightPx = std::max(1, heightPx);
    UINT dpi = popupHwnd_ ? GetDpiForWindow(popupHwnd_) : 96;

    /* 窗口复用后可能被挪到另一块屏, 像素尺寸没变但 DPI 变了 —— 这时 target
     * bitmap 的 dpi 也得重设, 否则内容按旧缩放画。 */
    if (!popupTargetBitmap_ || popupWidthPx_ != widthPx || popupHeightPx_ != heightPx ||
        popupDpi_ != dpi) {
        popupD2DContext_->SetTarget(nullptr);
        popupTargetBitmap_.Reset();

        if (popupWidthPx_ != 0 || popupHeightPx_ != 0) {
            HRESULT resizeHr = popupSwapChain_->ResizeBuffers(0, widthPx, heightPx,
                                                              DXGI_FORMAT_UNKNOWN, 0);
            if (FAILED(resizeHr)) {
                ReleasePopupRenderer();
                return false;
            }
        }

        ComPtr<IDXGISurface> surface;
        if (FAILED(popupSwapChain_->GetBuffer(0, __uuidof(IDXGISurface),
                                              reinterpret_cast<void**>(surface.GetAddressOf()))) ||
            !surface) {
            ReleasePopupRenderer();
            return false;
        }

        D2D1_BITMAP_PROPERTIES1 props{};
        props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                              D2D1_ALPHA_MODE_PREMULTIPLIED);
        props.dpiX = static_cast<float>(dpi);
        props.dpiY = static_cast<float>(dpi);
        props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

        if (FAILED(popupD2DContext_->CreateBitmapFromDxgiSurface(
                surface.Get(), props, popupTargetBitmap_.GetAddressOf())) ||
            !popupTargetBitmap_) {
            ReleasePopupRenderer();
            return false;
        }

        popupD2DContext_->SetTarget(popupTargetBitmap_.Get());
        popupD2DContext_->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
        popupD2DContext_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        popupRenderer_.SetTarget(popupD2DContext_.Get());
        popupRenderer_.ApplyTextRenderMode();
        popupWidthPx_ = widthPx;
        popupHeightPx_ = heightPx;
        popupDpi_ = dpi;
    }

    return true;
}

void ContextMenu::ReleasePopupRenderer() {
    popupRenderer_.SetTarget(nullptr);
    if (popupD2DContext_) {
        popupD2DContext_->SetTarget(nullptr);
    }
    popupTargetBitmap_.Reset();
    popupSwapChain_.Reset();
    popupD2DContext_.Reset();
    popupD2DDevice_.Reset();
    popupD3DDevice_.Reset();
    popupDcompVisual_.Reset();
    popupDcompEffect_.Reset();
    popupDcompClip_.Reset();
    popupDcompTarget_.Reset();
    popupDcompDevice_.Reset();
    popupRenderer_.ReleaseRenderTarget();
    popupRendererReady_ = false;
    popupWidthPx_ = 0;
    popupHeightPx_ = 0;
    popupDpi_ = 0;
}

void ContextMenu::RenderPopupFrame() {
    if (!visible_ || !popupHwnd_ || !IsWindow(popupHwnd_)) return;

    RECT rc{};
    GetClientRect(popupHwnd_, &rc);
    int widthPx = std::max<int>(1, rc.right - rc.left);
    int heightPx = std::max<int>(1, rc.bottom - rc.top);
    MenuStopwatch swFrame;
    MenuStopwatch swStage;
    if (!EnsurePopupRenderer(widthPx, heightPx)) return;
    double ensureMs = swStage.Ms();
    swStage.Reset();

    popupRenderer_.BeginDraw();
    popupRenderer_.Clear({0, 0, 0, 0});
    Draw(popupRenderer_);
    HRESULT hr = popupRenderer_.EndDraw();
    double drawMs = swStage.Ms();
    swStage.Reset();
    if (FAILED(hr)) {
        ReleasePopupRenderer();
        if (IsDeviceLost(hr)) DropSharedPopupGpu();
        return;
    }

    DXGI_PRESENT_PARAMETERS params{};
    HRESULT presentHr = popupSwapChain_ ? popupSwapChain_->Present1(0, 0, &params) : E_FAIL;
    double presentMs = swStage.Ms();
    TraceEvent("core_menu", "menu_render_frame",
               {TraceF64("ensure_ms", ensureMs),
                TraceF64("draw_ms", drawMs),
                TraceF64("present_ms", presentMs),
                TraceF64("total_ms", swFrame.Ms()),
                TraceI64("items", (int64_t)items_.size()),
                TraceBool("submenu", parentMenu_ != nullptr)});
    if (FAILED(presentHr)) {
        ReleasePopupRenderer();
        if (IsDeviceLost(presentHr)) DropSharedPopupGpu();
    }
}

namespace {

ComPtr<IDCompositionAnimation> MakeEasedAnimation(IDCompositionDevice* dcomp, double durSec,
                                                  double from, double to) {
    ComPtr<IDCompositionAnimation> anim;
    if (FAILED(dcomp->CreateAnimation(anim.GetAddressOf())) || !anim) return nullptr;
    for (const menumotion::CubicSegment& s : menumotion::EaseOutSegments(durSec, from, to)) {
        if (FAILED(anim->AddCubic(s.beginSec, (float)s.c0, (float)s.c1, (float)s.c2,
                                  (float)s.c3))) {
            return nullptr;
        }
    }
    if (FAILED(anim->End(durSec, (float)to))) return nullptr;
    return anim;
}

ComPtr<IDCompositionAnimation> MakeLinearAnimation(IDCompositionDevice* dcomp, double durSec,
                                                   double from, double to) {
    ComPtr<IDCompositionAnimation> anim;
    if (FAILED(dcomp->CreateAnimation(anim.GetAddressOf())) || !anim) return nullptr;
    if (FAILED(anim->AddCubic(0.0, (float)from, (float)((to - from) / durSec), 0.0f, 0.0f)) ||
        FAILED(anim->End(durSec, (float)to))) {
        return nullptr;
    }
    return anim;
}

}  // namespace

bool ContextMenu::ApplyOpenAnimation(int windowW, int windowH, int marginPx, int cardH,
                                     bool upward) {
    ComPtr<IDCompositionDevice> dcomp;
    ComPtr<IDCompositionVisual> visual;
    if (!popupDcompDevice_ || !popupDcompVisual_ ||
        FAILED(popupDcompDevice_.As(&dcomp)) || FAILED(popupDcompVisual_.As(&visual))) {
        return false;
    }
    ComPtr<IDCompositionRectangleClip> clip;
    ComPtr<IDCompositionEffectGroup> effect;
    if (popupDcompClip_) popupDcompClip_.As(&clip);
    if (popupDcompEffect_) popupDcompEffect_.As(&effect);

    bool animated = false;
    if (g_openAnimEnabled.load() && clip && effect && cardH > 0) {
        const double scale = g_openAnimTimeScale.load();
        const double openSec = menumotion::kOpenDurationMs / 1000.0 * scale;
        const double fadeSec = menumotion::kFadeDurationMs / 1000.0 * scale;
        const menumotion::OpenGeometry g =
            menumotion::ComputeOpenGeometry(windowH, marginPx, cardH, upward);
        auto top    = MakeEasedAnimation(dcomp.Get(), openSec, g.clipTopFrom, g.clipTopTo);
        auto bottom = MakeEasedAnimation(dcomp.Get(), openSec, g.clipBottomFrom, g.clipBottomTo);
        auto fade   = MakeLinearAnimation(dcomp.Get(), fadeSec, 0.0, 1.0);
        animated = top && bottom && fade &&
                   SUCCEEDED(clip->SetLeft(0.0f)) &&
                   SUCCEEDED(clip->SetRight((float)windowW)) &&
                   SUCCEEDED(clip->SetTop(top.Get())) &&
                   SUCCEEDED(clip->SetBottom(bottom.Get())) &&
                   SUCCEEDED(effect->SetOpacity(fade.Get())) &&
                   SUCCEEDED(visual->SetClip(clip.Get())) &&
                   SUCCEEDED(visual->SetEffect(effect.Get()));
    }
    if (!animated) {
        /* 关着或挂失败: 落回静止终态 (上一次可能停在动画里)。 */
        visual->SetClip(nullptr);
        visual->SetEffect(nullptr);
    }
    dcomp->Commit();
    return animated;
}

int ContextMenu::WritePopupScreenshot(const wchar_t* outPath) {
    if (!popupRenderer_.RT() || !popupTargetBitmap_ || !outPath) return -1;

    auto pixelSize = popupRenderer_.RT()->GetPixelSize();
    int width = static_cast<int>(pixelSize.width);
    int height = static_cast<int>(pixelSize.height);
    if (width <= 0 || height <= 0) return -2;

    D2D1_BITMAP_PROPERTIES1 cpuProps{};
    cpuProps.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                             D2D1_ALPHA_MODE_PREMULTIPLIED);
    cpuProps.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    ComPtr<ID2D1Bitmap1> cpuBitmap;
    HRESULT hr = popupRenderer_.RT()->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0,
                                                   cpuProps, cpuBitmap.GetAddressOf());
    if (FAILED(hr) || !cpuBitmap) return -3;

    D2D1_POINT_2U dst{0, 0};
    D2D1_RECT_U src{0, 0, static_cast<UINT32>(width), static_cast<UINT32>(height)};
    hr = cpuBitmap->CopyFromBitmap(&dst, popupTargetBitmap_.Get(), &src);
    if (FAILED(hr)) return -4;

    D2D1_MAPPED_RECT mapped{};
    hr = cpuBitmap->Map(D2D1_MAP_OPTIONS_READ, &mapped);
    if (FAILED(hr)) return -5;

    auto unmap = [&]() { cpuBitmap->Unmap(); };
    IWICImagingFactory* wic = popupRenderer_.WIC();
    if (!wic) { unmap(); return -6; }

    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IWICStream> stream;

    hr = wic->CreateStream(stream.GetAddressOf());
    if (FAILED(hr)) { unmap(); return -7; }
    hr = stream->InitializeFromFilename(outPath, GENERIC_WRITE);
    if (FAILED(hr)) { unmap(); return -8; }
    hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
    if (FAILED(hr)) { unmap(); return -9; }
    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) { unmap(); return -10; }
    hr = encoder->CreateNewFrame(frame.GetAddressOf(), nullptr);
    if (FAILED(hr)) { unmap(); return -11; }
    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) { unmap(); return -12; }
    hr = frame->SetSize(width, height);
    if (FAILED(hr)) { unmap(); return -13; }
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&fmt);
    if (FAILED(hr)) { unmap(); return -14; }
    hr = frame->WritePixels(height, mapped.pitch, mapped.pitch * height, mapped.bits);
    unmap();
    if (FAILED(hr)) return -15;
    hr = frame->Commit();
    if (FAILED(hr)) return -16;
    hr = encoder->Commit();
    if (FAILED(hr)) return -17;
    return 0;
}

LRESULT ContextMenu::HandleOverlayMessage(HWND hwnd, UINT msg, WPARAM wParam,
                                          LPARAM lParam, bool& handled) {
    ContextMenu* self = this;
    handled = true;

    switch (msg) {
    case WM_PAINT: {
        self->RenderPopupFrame();
        ValidateRect(hwnd, nullptr);
        return 0;
    }
    case WM_MOUSEMOVE: {
        // Convert physical pixels to DIPs
        UINT dpi = GetDpiForWindow(hwnd);
        float scale = (float)dpi / 96.0f;
        float x = (float)GET_X_LPARAM(lParam) / scale;
        float y = (float)GET_Y_LPARAM(lParam) / scale;
        int oldHovered = self->hoveredIndex_;
        int oldOpenSubmenu = self->openSubmenuIndex_;
        MenuStopwatch swMove;
        self->HandleMouseMove(x, y);
        double hitMs = swMove.Ms();
        bool hoverChanged = self->hoveredIndex_ != oldHovered;
        bool submenuChanged = self->openSubmenuIndex_ != oldOpenSubmenu;
        if (hoverChanged || submenuChanged) {
            self->RenderPopupFrame();
        }
        TraceEvent("core_menu", "menu_mouse_move",
                   {TraceF64("hit_ms", hitMs),
                    TraceF64("total_ms", swMove.Ms()),
                    TraceBool("hover_changed", hoverChanged),
                    TraceBool("submenu_changed", submenuChanged),
                    TraceI64("hovered", self->hoveredIndex_)});

        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (self->hoveredIndex_ != -1) {
            self->hoveredIndex_ = -1;
            self->RenderPopupFrame();
        }
        return 0;
    case WM_LBUTTONDOWN: {
        UINT dpi = GetDpiForWindow(hwnd);
        float scale = (float)dpi / 96.0f;
        float x = (float)GET_X_LPARAM(lParam) / scale;
        float y = (float)GET_Y_LPARAM(lParam) / scale;
        /* 用户已经按下去了, 没必要再等 hover 延时. */
        if (self->pendingSubmenuIndex_ >= 0 &&
            self->pendingSubmenuIndex_ == self->hoveredIndex_) {
            KillTimer(hwnd, kSubmenuHoverTimerId);
            self->OpenPendingSubmenu();
        }
        self->HandleMouseDown(x, y);
        return 0;
    }
    case WM_LBUTTONUP: {
        UINT dpi = GetDpiForWindow(hwnd);
        float scale = (float)dpi / 96.0f;
        float x = (float)GET_X_LPARAM(lParam) / scale;
        float y = (float)GET_Y_LPARAM(lParam) / scale;
        if (self->HandleMouseUp(x, y)) {
            int clickedId = self->ClickedItemId();
            // Post message to parent so it can handle the callback (id + 属性载荷)
            if (clickedId >= 0 && self->parentHwnd_) {
                PostMessage(self->parentHwnd_, WM_APP + 100, (WPARAM)clickedId,
                            (LPARAM)self->MakeClickInfo());
            }
            self->Close();
            // 如果本身是子菜单 popup（parentMenu_ 非空），沿链向上关闭父菜单，
            // 否则 leaf 点完后 root popup 还留在屏上。
            ContextMenu* p = self->parentMenu_;
            while (p) { p->Close(); p = p->parentMenu_; }
        }
        return 0;
    }
    case WM_NCCALCSIZE:
        if (wParam) {
            // Remove all non-client area (WS_THICKFRAME border) — keep only DWM shadow
            return 0;
        }
        break;
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_SIZE:
        self->RenderPopupFrame();
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_ACTIVATEAPP:
        /* Build 86+: app 失去激活立即关菜单, 不等 WM_TIMER 50ms 轮询.
         * wParam=FALSE 表示 app 进入非激活态 (用户切到其它软件). 这条
         * 跟 timer Check 2 互补 — timer 有 50ms 延迟, 这里立即响应. */
        if (!wParam && !g_debugSuppressAutoClose) {
            KillTimer(hwnd, 1);
            self->Close();
            return 0;
        }
        break;
    case WM_TIMER:
        /* timer 2 = submenu hover 延时到点. */
        if (wParam == kSubmenuHoverTimerId) {
            KillTimer(hwnd, kSubmenuHoverTimerId);
            self->OpenPendingSubmenu();
            return 0;
        }
        // Poll: if mouse is pressed outside menu, close it
        if (wParam == kAutoCloseTimerId) {
            if (g_debugSuppressAutoClose) return 0;  // 测试模式：不做自动关闭检查
            if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
                POINT pt;
                GetCursorPos(&pt);
                /* Build 86+: 用 "可见菜单矩形" 而不是 popup hwnd 整框 ——
                 * hwnd 比可见 menu 大 kShadowMargin (18 DIP / each side) 一圈,
                 * 用整框做命中导致 click 在阴影带 (visual 外) 不关菜单. 算
                 * 可见 rect: hwnd.topLeft + kShadowMargin × dpiScale. */
                auto visibleRect = [](HWND h) -> RECT {
                    RECT rc; GetWindowRect(h, &rc);
                    UINT dpi = GetDpiForWindow(h);
                    float scale = (float)dpi / 96.0f;
                    int marginPx = (int)(kShadowMargin * scale);
                    return RECT{rc.left + marginPx, rc.top + marginPx,
                                 rc.right - marginPx, rc.bottom - marginPx};
                };
                RECT rc = visibleRect(hwnd);
                if (pt.x < rc.left || pt.x >= rc.right || pt.y < rc.top || pt.y >= rc.bottom) {
                    bool inSubmenu = false;
                    if (self->openSubmenuIndex_ >= 0 && self->openSubmenuIndex_ < (int)self->items_.size()) {
                        auto& sub = self->items_[self->openSubmenuIndex_].submenu;
                        if (sub && sub->popupHwnd_) {
                            RECT src = visibleRect(sub->popupHwnd_);
                            if (pt.x >= src.left && pt.x < src.right && pt.y >= src.top && pt.y < src.bottom)
                                inSubmenu = true;
                        }
                    }
                    bool inAncestor = false;
                    for (ContextMenu* p = self->parentMenu_; p && !inAncestor; p = p->parentMenu_) {
                        if (!p->popupHwnd_) continue;
                        RECT pc = visibleRect(p->popupHwnd_);
                        if (pt.x >= pc.left && pt.x < pc.right && pt.y >= pc.top && pt.y < pc.bottom)
                            inAncestor = true;
                    }
                    if (!inSubmenu && !inAncestor) {
                        KillTimer(hwnd, 1);
                        self->Close();
                        return 0;
                    }
                }
            }
            // Also check if another window got focus
            HWND fg = GetForegroundWindow();
            if (fg && fg != hwnd && fg != self->parentHwnd_) {
                // Check it's not a submenu
                bool isSubmenu = false;
                if (self->openSubmenuIndex_ >= 0 && self->openSubmenuIndex_ < (int)self->items_.size()) {
                    auto& sub = self->items_[self->openSubmenuIndex_].submenu;
                    if (sub && sub->popupHwnd_ == fg) isSubmenu = true;
                }
                // 或是任一祖先菜单 popup —— 用户点击父菜单时不应把子菜单关掉
                bool isAncestor = false;
                for (ContextMenu* p = self->parentMenu_; p && !isAncestor; p = p->parentMenu_) {
                    if (p->popupHwnd_ == fg) isAncestor = true;
                }
                if (!isSubmenu && !isAncestor) {
                    KillTimer(hwnd, 1);
                    self->Close();
                    return 0;
                }
            }
        }
        return 0;
    }
    handled = false;
    return 0;
}

// ---- Geometry ----

bool ContextMenu::HasAnyIcon() const {
    /* BREAKING (build 75): MenuItem 没有 hasIcon 字段了, icon 是 customContent
     * widget tree 的一部分. 始终预留 icon 列宽度 — 不预留会让用户写
     * <menuitem><svg/><label/></menuitem> 时 svg 紧贴左边. */
    return !items_.empty();
}

/* 快捷键列宽 (含与内容之间 16 DIP 的呼吸), 0 = 本菜单没有快捷键。算菜单宽度、
 * 排版内容区、画快捷键三处共用 —— 以前后两处写死 100 DIP, 快捷键短 (Esc) 时菜单
 * 只按估算宽预留, 排版却减 100, 内容区被挤成几十 DIP, 文字截成 "退..."。 */
float ContextMenu::ShortcutColumnWidth() const {
    float maxShortcut = 0;
    for (const auto& item : items_) {
        if (item.isSeparator) continue;
        float sw = item.shortcut.length() * 6.5f;
        if (sw > maxShortcut) maxShortcut = sw;
    }
    if (reservedShortcutWidth_ > maxShortcut) maxShortcut = reservedShortcutWidth_;
    return maxShortcut > 0 ? maxShortcut + 16.0f : 0.0f;
}

float ContextMenu::MenuWidth() const {
    /* BREAKING (build 75): 老的 text/shortcut 文字宽度估算改成: 用 customContent
     * widget tree 的 SizeHint() 取 max. customContent 还没 DoLayout 过, SizeHint
     * 在大多数 widget 上返 (fixedW, fixedH) 或 (0,0), 估算可能不准. 兜底走
     * kMinWidth = 200. 后续如要更准, 应在 PopulateMenuItem 末尾对每个 customContent
     * 做一遍 measure pass 写 fixedW. */
    float maxContent  = 0;
    bool  hasSubmenu  = false;
    for (auto& item : items_) {
        if (item.isSeparator) continue;
        if (item.customContent) {
            auto h = item.customContent->SizeHint();
            float w = h.width;
            /* HBox/VBox SizeHint 不自己 clamp 到 maxW (那是 Layout 期的事),
             * MenuWidth 这里手动 cap — 让 .uix CSS 写 `.menuitem-row {
             * max-width: 240px }` 真正约束菜单宽度. minW 同样 honor. */
            if (item.customContent->maxW > 0 && w > item.customContent->maxW) {
                w = item.customContent->maxW;
            }
            if (item.customContent->minW > 0 && w < item.customContent->minW) {
                w = item.customContent->minW;
            }
            if (w > maxContent) maxContent = w;
        }
        if (item.submenu) hasSubmenu = true;
    }
    /* Build 77+/80+/81+: 反应式菜单 v-if 在不同 state 隐不同 items, 让
     * visible items 的 max content / shortcut / hasSubmenu 都跟着变 → 两
     * 态自然宽度差. PageState 计算 "全 items (含 v-if=false 的)" 喂三个
     * reserved 字段, 这里 floor 住保证两态宽度完全一致. */
    if (reservedHasSubmenu_) hasSubmenu = true;
    if (reservedContentWidth_ > maxContent) maxContent = reservedContentWidth_;
    float shortcutCol = ShortcutColumnWidth();
    float arrowCol    = hasSubmenu ? kSubmenuArrowWidth : 0;
    float w = maxContent + shortcutCol + arrowCol + kPadding * 2;
    /* Build 85+: 跨菜单树共享宽度 — submenu 撑到 parent 的 MenuWidth, 整族
     * 菜单视觉一致. minPropagatedWidth_ 由 PageState 在 PopulateMenu 末尾
     * 算整树最大 MenuWidth 后回写到每个 ContextMenu. */
    if (minPropagatedWidth_ > w) w = minPropagatedWidth_;
    if (minWidth_ > w) w = minWidth_;
    return std::max(w, kMinWidth);
}

float ContextMenu::MenuHeight() const {
    float h = kPadding * 2;
    for (auto& item : items_) {
        h += item.isSeparator ? kSepHeight : kItemHeight;
    }
    return h;
}

void ContextMenu::InvalidateLayout() {
    layoutValid_ = false;
    layoutWidth_ = 0.0f;
    layoutHeight_ = 0.0f;
    itemRects_.clear();
}

void ContextMenu::LayoutItems() {
    layoutWidth_ = MenuWidth();
    layoutHeight_ = MenuHeight();
    itemRects_.assign(items_.size(), D2D1_RECT_F{});

    const float shortcutCol = ShortcutColumnWidth();
    float iy = y_ + kPadding;
    for (int i = 0; i < (int)items_.size(); i++) {
        auto& item = items_[i];
        float rowH = item.isSeparator ? kSepHeight : kItemHeight;
        D2D1_RECT_F row = {x_, iy, x_ + layoutWidth_, iy + rowH};
        itemRects_[i] = row;

        if (!item.isSeparator && item.customContent) {
            float contentLeft  = x_ + kPadding;
            float contentRight = x_ + layoutWidth_ - kPadding;
            if (!item.shortcut.empty()) contentRight -= shortcutCol;
            if (item.submenu) contentRight -= kSubmenuArrowWidth;
            item.customContent->rect = {contentLeft, row.top, contentRight, row.bottom};
            item.customContent->DoLayout();
        }

        iy += rowH;
    }

    layoutValid_ = true;
}

D2D1_RECT_F ContextMenu::Bounds() const {
    float w = layoutValid_ ? layoutWidth_ : MenuWidth();
    float h = layoutValid_ ? layoutHeight_ : MenuHeight();
    return {x_, y_, x_ + w, y_ + h};
}

bool ContextMenu::Contains(float x, float y) const {
    auto b = Bounds();
    return x >= b.left && x < b.right && y >= b.top && y < b.bottom;
}

D2D1_RECT_F ContextMenu::ItemRect(int index) const {
    if (layoutValid_ && index >= 0 && index < (int)itemRects_.size()) {
        return itemRects_[index];
    }
    float y = y_ + kPadding;
    for (int i = 0; i < index && i < (int)items_.size(); i++) {
        y += items_[i].isSeparator ? kSepHeight : kItemHeight;
    }
    float h = items_[index].isSeparator ? kSepHeight : kItemHeight;
    return {x_, y, x_ + MenuWidth(), y + h};
}

int ContextMenu::HitTest(float x, float y) const {
    if (!Contains(x, y)) return -1;
    if (layoutValid_ && itemRects_.size() == items_.size()) {
        for (int i = 0; i < (int)itemRects_.size(); i++) {
            const auto& r = itemRects_[i];
            if (y >= r.top && y < r.bottom) return i;
        }
        return -1;
    }
    float iy = y_ + kPadding;
    for (int i = 0; i < (int)items_.size(); i++) {
        float h = items_[i].isSeparator ? kSepHeight : kItemHeight;
        if (y >= iy && y < iy + h) return i;
        iy += h;
    }
    return -1;
}

// ---- Draw ----

void ContextMenu::Draw(Renderer& r) {
    if (!visible_) return;
    if (!layoutValid_) LayoutItems();

    float w = layoutWidth_;
    float h = layoutHeight_;

    // Soft drop shadow — concentric semi-transparent rounded rects expand
    // outward in 1 px steps. Each ring is a slightly larger rounded rect
    // with quadratically-falloff alpha. Cheaper than a Gaussian-blur D2D
    // effect (no offscreen bitmap) and the curve is smooth enough for the
    // 16-18 px halo we have around popup mode.
    constexpr int   kShadowSteps      = 12;
    constexpr float kShadowVerticalY  = 2.0f;
    // Per-ring peak alpha. With 12 stacked rings the inner edge accumulates
    // alpha quickly; 0.012 keeps the halo a whisper-faint depth cue.
    constexpr float kShadowPeakAlpha  = 0.012f;
    for (int i = kShadowSteps; i >= 1; --i) {
        float spread = (float)i;
        float t = (float)i / (float)kShadowSteps;
        float alpha = kShadowPeakAlpha * (1.0f - t) * (1.0f - t);
        D2D1_COLOR_F sc{0, 0, 0, alpha};
        D2D1_RECT_F sr{
            x_ - spread,
            y_ - spread + kShadowVerticalY,
            x_ + w + spread,
            y_ + h + spread + kShadowVerticalY,
        };
        float rr = CornerRadius() + spread;
        r.FillRoundedRect(sr, rr, rr, sc);
    }

    // Single-fill rounded card. No outer stroke — on a transparent DComp
    // surface a 0.5px stroke crosses the alpha-blended corner band and
    // shows up as a darker rim against contrast-y desktops.
    D2D1_RECT_F bgRect = {x_, y_, x_ + w, y_ + h};
    // Card surface — pure white in Light mode, an elevated grey in Dark
    // mode (slightly above page bg so the popup reads as "above" the
    // canvas). hasBgColor_ override still wins for explicit tints.
    D2D1_COLOR_F cardBg;
    if (hasBgColor_) {
        cardBg = bgColor_;
    } else if (theme::CurrentMode() == theme::Mode::Dark) {
        float alpha = frostedMaterial_ ? 0.75f : 1.0f;
        cardBg = D2D1_COLOR_F{0x2C / 255.0f, 0x2C / 255.0f, 0x2C / 255.0f, alpha};
    } else {
        float alpha = frostedMaterial_ ? 0.75f : 1.0f;
        cardBg = D2D1_COLOR_F{1.0f, 1.0f, 1.0f, alpha};
    }
    float materialAlpha = std::clamp(cardBg.a, 0.0f, 1.0f);
    D2D1_COLOR_F separatorColor = (theme::CurrentMode() == theme::Mode::Dark)
        ? D2D1_COLOR_F{1.0f, 1.0f, 1.0f, 0.22f * materialAlpha}
        : D2D1_COLOR_F{0.0f, 0.0f, 0.0f, 0.18f * materialAlpha};
    {
        const float cr = CornerRadius();
        if (backdropResourceKey_.IsValid()) {
            r.PushRoundedClip(bgRect, cr, cr);
            r.DrawImageResource(backdropResourceKey_, bgRect, ImageSampling::Linear, 1.0f);
            r.PopRoundedClip();
        }
        r.FillRoundedRect(bgRect, cr, cr, cardBg);
    }

    // BREAKING (build 75): items 走 widget-tree 渲染. 每个 menuitem 的
    // customContent (WidgetPtr) 在 item 的 content-rect 内 DoLayout + DrawTree.
    // ContextMenu 自己只画: separator / hover 高亮 / shortcut 文字 (右对齐) /
    // submenu arrow. disabled 通过临时 set opacity dim.
    for (int i = 0; i < (int)items_.size(); i++) {
        auto& item = items_[i];
        D2D1_RECT_F row = ItemRect(i);

        if (item.isSeparator) {
            float sepY = row.top + (row.bottom - row.top) / 2.0f;
            r.DrawLine(x_ + kPadding, sepY, x_ + w - kPadding, sepY,
                       separatorColor);
            continue;
        }

        if (i == hoveredIndex_ && item.enabled) {
            D2D1_RECT_F hlRect = {x_ + kPadding, row.top,
                                   x_ + w - kPadding, row.bottom};
            D2D1_COLOR_F hlColor = (theme::CurrentMode() == theme::Mode::Dark)
                ? D2D1_COLOR_F{1.0f, 1.0f, 1.0f, 0.10f}
                : D2D1_COLOR_F{0.0f, 0.0f, 0.0f, 0.06f};
            r.FillRoundedRect(hlRect, 6.0f, 6.0f, hlColor);
        }

        if (item.customContent) {
            float savedOpacity = item.customContent->opacity;
            if (!item.enabled) item.customContent->opacity = 0.4f;
            item.customContent->DrawTree(r);
            item.customContent->opacity = savedOpacity;
        }

        if (!item.shortcut.empty()) {
            D2D1_COLOR_F base = theme::kForeground3();
            D2D1_COLOR_F shortcutColor = {base.r, base.g, base.b, item.enabled ? 1.0f : 0.4f};
            D2D1_RECT_F scRect = {x_ + w - kPadding - ShortcutColumnWidth(), row.top,
                                   x_ + w - 14, row.bottom};
            r.DrawText(item.shortcut, scRect, shortcutColor, kShortcutFont,
                       DWRITE_TEXT_ALIGNMENT_TRAILING);
        }

        if (item.submenu) {
            D2D1_COLOR_F arrowColor = item.enabled
                ? theme::kBtnText() : D2D1_COLOR_F{0.5f, 0.5f, 0.5f, 0.6f};
            D2D1_RECT_F arrowRect = {x_ + w - kPadding - 16, row.top,
                                      x_ + w - kPadding - 2, row.bottom};
            /* Build 86: › "›" (single right-pointing angle quotation) —
             * 跟 macOS / Win11 submenu arrow 同款 outline 风格. 比 ▸ ▸
             * (黑色实心三角) 看着更克制. */
            r.DrawText(L"›", arrowRect, arrowColor, theme::kFontSizeBody,
                       DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }

    // Draw open submenu (only in overlay mode, popup submenus have their own window)
    if (!popupHwnd_ && openSubmenuIndex_ >= 0 && openSubmenuIndex_ < (int)items_.size()) {
        auto& sub = items_[openSubmenuIndex_].submenu;
        if (sub && sub->IsVisible()) {
            sub->Draw(r);
        }
    }
}

// ---- Event Handling ----

bool ContextMenu::HandleMouseMove(float x, float y) {
    if (!visible_) return false;

    // Check submenu first
    if (openSubmenuIndex_ >= 0 && openSubmenuIndex_ < (int)items_.size()) {
        auto& sub = items_[openSubmenuIndex_].submenu;
        if (sub && sub->IsVisible()) {
            if (sub->popupHwnd_) {
                // Submenu has its own window — it handles its own events
            } else if (sub->Contains(x, y)) {
                sub->HandleMouseMove(x, y);
                return true;
            }
        }
    }

    int hit = HitTest(x, y);
    hoveredIndex_ = hit;

    // Open/close submenus on hover
    if (hit >= 0 && hit < (int)items_.size() && !items_[hit].isSeparator) {
        if (items_[hit].submenu && items_[hit].enabled) {
            if (openSubmenuIndex_ != hit) {
                if (popupHwnd_) {
                    /* perf: popup 模式下不在 WM_MOUSEMOVE 里直接建 submenu。
                     * 建窗 + 建设备 + ShowWindow 实测 ~107ms, 上下扫一遍菜单
                     * 要连开带关十几次, OverlayService 的消息泵直接堵死, 表现
                     * 就是 hover 高亮跟不上鼠标。改成停 kSubmenuHoverDelayMs
                     * 才真开 (Win11 / macOS 同款), 快速划过一个都不建。 */
                    if (pendingSubmenuIndex_ != hit) {
                        pendingSubmenuIndex_ = hit;
                        SetTimer(popupHwnd_, kSubmenuHoverTimerId,
                                 kSubmenuHoverDelayMs, nullptr);
                    }
                    CloseOpenSubmenu();
                } else {
                    OpenSubmenuPopup(hit);
                }
            }
        } else {
            CancelPendingSubmenu();
            CloseOpenSubmenu();
        }
    }

    return Contains(x, y);
}

bool ContextMenu::HandleMouseDown(float x, float y) {
    if (!visible_) return false;

    if (openSubmenuIndex_ >= 0 && openSubmenuIndex_ < (int)items_.size()) {
        auto& sub = items_[openSubmenuIndex_].submenu;
        if (sub && sub->IsVisible() && !sub->popupHwnd_ && sub->Contains(x, y)) {
            return sub->HandleMouseDown(x, y);
        }
    }

    return Contains(x, y);
}

bool ContextMenu::HandleMouseUp(float x, float y) {
    if (!visible_) return false;

    // Check inline submenu
    if (openSubmenuIndex_ >= 0 && openSubmenuIndex_ < (int)items_.size()) {
        auto& sub = items_[openSubmenuIndex_].submenu;
        if (sub && sub->IsVisible() && !sub->popupHwnd_ && sub->Contains(x, y)) {
            bool handled = sub->HandleMouseUp(x, y);
            if (handled && sub->ClickedItemId() >= 0) {
                RecordClickFrom(*sub);
                Close();
                return true;
            }
            return handled;
        }
    }

    int hit = HitTest(x, y);
    if (hit >= 0 && hit < (int)items_.size()) {
        auto& item = items_[hit];
        if (!item.isSeparator && !item.submenu && item.enabled) {
            RecordClick(item);
            Close();
            return true;
        }
    }

    if (!Contains(x, y)) {
        Close();
        return true;
    }

    return false;
}

int ContextMenu::Screenshot(const wchar_t* outPath) {
    if (!visible_ || !popupHwnd_ || !outPath) return -1;
    int result = -1;
    OverlayService::Instance().InvokeSync([this, outPath, &result]() {
        RenderPopupFrame();
        result = WritePopupScreenshot(outPath);
    });
    return result;
}

} // namespace ui
