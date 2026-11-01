#pragma once

#include <windows.h>
#include <functional>
#include <string>

namespace ui {

class OverlayWindowHandler {
public:
    virtual ~OverlayWindowHandler() = default;
    virtual LRESULT HandleOverlayMessage(HWND hwnd, UINT msg, WPARAM wParam,
                                         LPARAM lParam, bool& handled) = 0;
};

struct OverlayWindowOptions {
    HWND owner = nullptr;
    DWORD exStyle = 0;
    DWORD style = WS_POPUP;
    int x = 0;
    int y = 0;
    int width = 1;
    int height = 1;
    std::wstring title;
};

class OverlayService {
public:
    static OverlayService& Instance();

    HWND CreateHostWindowSync(OverlayWindowHandler* handler,
                              const OverlayWindowOptions& options);
    void DestroyHostWindowSync(HWND hwnd);
    void InvokeSync(std::function<void()> fn);
    /* 投到 overlay 线程就返回, 不等执行完; 与 InvokeSync 同一队列, 先投先做,
     * 所以之后的 InvokeSync 可当屏障。在 overlay 线程上调用时就地执行。 */
    void InvokeAsync(std::function<void()> fn);

    void ShowToast(HWND owner, const std::wstring& text, int durationMs,
                   int position, int icon, int anim);
    void DismissOwner(HWND owner);

    /* 调试: owner 窗口上当前 toast 的状态 (JSON, UTF-8)。toast 画在 overlay 线程的
     * 独立分层窗口上, 宿主 debug 管道的 tree / screenshot 看不到它。
     * 先排一个 InvokeSync 屏障: 刚 ShowToast 完马上查也能看到 (请求同队列)。
     *   {"visible":false} 或
     *   {"visible":true,"text":..,"icon":..,"position":..,"anim":..,
     *    "duration_ms":..,"elapsed_ms":..,"alpha":0-255,
     *    "x":..,"y":..,"width":..,"height":..   (相对 owner 客户区, 物理 px, 当前帧)
     *    "dpi":..,"text_clipped":bool}         (超宽自动折行, 超过 4 行才截断) */
    std::string DebugToastInfoJson(HWND owner);
    /* 调试: 把 owner 当前 toast 的位图 (满不透明度、不含淡入淡出) 存成 PNG。
     * 0 成功; -1 没有 toast; 其余负数 = 编码 / 写文件失败。 */
    int DebugScreenshotToast(HWND owner, const wchar_t* outPath);
    void Stop();

private:
    OverlayService();
    ~OverlayService();
    OverlayService(const OverlayService&) = delete;
    OverlayService& operator=(const OverlayService&) = delete;

    struct Impl;
    Impl* impl_;
};

} // namespace ui
