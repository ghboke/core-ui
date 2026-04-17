#pragma once
#include "widget.h"
#include "renderer.h"
#include <windows.h>
#include <functional>

namespace ui {

/*
 * ImageViewGDI — GDI 子窗口图片查看组件
 *
 * 跟 Windows XP 照片查看器（CZoomWnd）同架构：
 * - 后台线程 GDI+ 渲染全图到 HBITMAP (CreateDIBSection)
 * - 拖拽/缩放只做 StretchBlt，零解码，即时响应
 * - 内存计入内核 GDI 堆，任务管理器显示小
 */
class UI_API ImageViewGDIWidget : public Widget {
public:
    ImageViewGDIWidget();
    ~ImageViewGDIWidget();

    /* 设置父窗口（必须在 SetFile 之前调用） */
    void SetParentHwnd(HWND parent);

    /* 加载图片文件（后台线程 GDI+ 渲染到 HBITMAP） */
    void SetFile(const std::wstring& path);

    /* 从外部像素设置（用于非 GDI+ 格式，像素会拷贝到 DIBSection） */
    void SetPixels(const void* pixels, int w, int h, int stride);

    /* 清除图片 */
    void Clear();

    /* 图片信息 */
    int ImageWidth() const { return imgW_; }
    int ImageHeight() const { return imgH_; }
    bool HasImage() const { return hbmFront_ != nullptr; }

    /* 缩放/平移 */
    float Zoom() const { return zoom_; }
    void SetZoom(float z);
    void SetPan(float x, float y);
    float PanX() const { return panX_; }
    float PanY() const { return panY_; }
    void FitToView();
    void SetZoomRange(float minZ, float maxZ) { minZoom_ = minZ; maxZoom_ = maxZ; }

    /* 缩放/平移回调 */
    std::function<void(float zoom, float panX, float panY)> onViewportChanged;

    /* Widget 虚函数 */
    void OnDraw(Renderer& r) override;
    void DoLayout() override;
    D2D1_SIZE_F SizeHint() const override;

private:
    /* GDI 子窗口 */
    HWND hwndChild_ = nullptr;
    HWND hwndParent_ = nullptr;
    void EnsureChildWindow(HWND parent);
    void DestroyChildWindow();
    void UpdateChildPos();

    /* 子窗口 WndProc */
    static LRESULT CALLBACK ChildWndProc(HWND, UINT, WPARAM, LPARAM);
    void OnChildPaint(HDC hdc, const RECT& rc);
    void OnChildWheel(short delta, int x, int y);

    /* 图片缓存（XP 的 m_pFront） */
    HBITMAP hbmFront_ = nullptr;    /* GDI DIBSection */
    uint8_t* frontBits_ = nullptr;  /* DIBSection 像素指针 */
    int imgW_ = 0, imgH_ = 0;

    /* 视口参数 */
    float zoom_ = 1.0f;
    float panX_ = 0, panY_ = 0;
    float minZoom_ = 0.01f, maxZoom_ = 64.0f;

    /* 拖拽状态 */
    bool dragging_ = false;
    int dragStartX_ = 0, dragStartY_ = 0;
    float dragPanX_ = 0, dragPanY_ = 0;

    /* 后台渲染 */
    bool rendering_ = false;

    void NotifyViewport();

    /* 注册窗口类（一次性） */
    static bool classRegistered_;
    static void RegisterChildClass();
};

} // namespace ui
