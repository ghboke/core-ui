#include "image_view_gdi.h"
#include "theme.h"
#include <windowsx.h>
#include <gdiplus.h>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace ui {

static const wchar_t* kChildClassName = L"UICore_ImageViewGDI_Child";
bool ImageViewGDIWidget::classRegistered_ = false;

static HINSTANCE GetDllInstance() {
    HINSTANCE h = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&GetDllInstance, &h);
    return h ? h : GetModuleHandleW(nullptr);
}

void ImageViewGDIWidget::RegisterChildClass() {
    if (classRegistered_) return;
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ChildWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = kChildClassName;
    classRegistered_ = (RegisterClassExW(&wc) != 0);
}

ImageViewGDIWidget::ImageViewGDIWidget() {
    expanding = true;
    RegisterChildClass();
}

ImageViewGDIWidget::~ImageViewGDIWidget() {
    Clear();
    DestroyChildWindow();
}

/* ---- 子窗口管理 ---- */

void ImageViewGDIWidget::EnsureChildWindow(HWND parent) {
    if (hwndChild_) return;
    hwndParent_ = parent;
    if (!parent) return;

    /* 创建 popup 窗口，用 SetWindowLongPtr 替换 WndProc */
    HINSTANCE hInst = GetModuleHandleW(nullptr);

    hwndChild_ = CreateWindowExW(
        WS_EX_NOACTIVATE,
        L"STATIC", nullptr,
        WS_POPUP | WS_VISIBLE | WS_BORDER | SS_BLACKRECT,
        200, 200, 400, 300,
        parent, nullptr, hInst, nullptr);

    if (hwndChild_) {
        SetWindowLongPtrW(hwndChild_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        SetWindowLongPtrW(hwndChild_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ChildWndProc));
        InvalidateRect(hwndChild_, nullptr, TRUE);
    }
}

void ImageViewGDIWidget::DestroyChildWindow() {
    if (hwndChild_) {
        DestroyWindow(hwndChild_);
        hwndChild_ = nullptr;
    }
    hwndParent_ = nullptr;
}

void ImageViewGDIWidget::UpdateChildPos() {
    if (!hwndChild_ || !hwndParent_) return;

    /* rect 是 D2D 逻辑坐标，需转为屏幕物理像素 */
    UINT dpi = 96;
    {
        typedef UINT(WINAPI* PFN)(HWND);
        static PFN pfn = (PFN)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
        if (pfn) dpi = pfn(hwndParent_);
    }
    float scale = (float)dpi / 96.0f;

    /* 逻辑坐标 → 物理像素（相对于父窗口客户区） */
    int lx = (int)(rect.left * scale);
    int ly = (int)(rect.top * scale);
    int w = (int)((rect.right - rect.left) * scale);
    int h = (int)((rect.bottom - rect.top) * scale);
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    /* 客户区坐标 → 屏幕坐标（popup 窗口需要屏幕坐标） */
    POINT pt = { lx, ly };
    ClientToScreen(hwndParent_, &pt);

    SetWindowPos(hwndChild_, HWND_TOP, pt.x, pt.y, w, h, SWP_NOACTIVATE);
}

/* ---- 子窗口 WndProc ---- */

LRESULT CALLBACK ImageViewGDIWidget::ChildWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<ImageViewGDIWidget*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (self) {
            self->OnChildPaint(hdc, rc);
        } else {
            /* self 还没设置时画黑色背景 */
            FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEWHEEL:
        if (self) {
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            self->OnChildWheel(delta, pt.x, pt.y);
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (self && self->hbmFront_) {
            self->dragging_ = true;
            self->dragStartX_ = GET_X_LPARAM(lParam);
            self->dragStartY_ = GET_Y_LPARAM(lParam);
            self->dragPanX_ = self->panX_;
            self->dragPanY_ = self->panY_;
            SetCapture(hwnd);
        }
        return 0;

    case WM_MOUSEMOVE:
        if (self && self->dragging_) {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            /* 物理像素偏移转逻辑像素 */
            UINT dpi = 96;
            typedef UINT(WINAPI* PFN)(HWND);
            static PFN pfn = (PFN)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
            if (pfn) dpi = pfn(hwnd);
            float scale = (float)dpi / 96.0f;

            self->panX_ = self->dragPanX_ + (float)(x - self->dragStartX_) / scale;
            self->panY_ = self->dragPanY_ + (float)(y - self->dragStartY_) / scale;
            InvalidateRect(hwnd, nullptr, FALSE);
            self->NotifyViewport();
        }
        return 0;

    case WM_LBUTTONUP:
        if (self) {
            self->dragging_ = false;
            ReleaseCapture();
        }
        return 0;

    case WM_APP + 300:
        /* 后台渲染完成通知 */
        if (self && self->hbmFront_) {
            self->UpdateChildPos();
            ShowWindow(hwnd, SW_SHOW);
            self->FitToView();
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* ---- 绘制（XP CZoomWnd::OnEraseBkgnd 同原理）---- */

void ImageViewGDIWidget::OnChildPaint(HDC hdc, const RECT& rc) {
    int viewW = rc.right - rc.left;
    int viewH = rc.bottom - rc.top;

    /* 背景色 */
    auto bg = theme::kContentBg();
    COLORREF bgColor = RGB((int)(bg.r * 255), (int)(bg.g * 255), (int)(bg.b * 255));
    HBRUSH hbrBg = CreateSolidBrush(bgColor);

    if (!hbmFront_) {
        FillRect(hdc, &rc, hbrBg);
        DeleteObject(hbrBg);
        return;
    }

    /* DPI 缩放 */
    UINT dpi = 96;
    typedef UINT(WINAPI* PFN)(HWND);
    static PFN pfn = (PFN)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (pfn && hwndChild_) dpi = pfn(hwndChild_);
    float dpiScale = (float)dpi / 96.0f;

    /* 计算图片绘制区域（逻辑坐标 → 物理像素） */
    float drawW = imgW_ * zoom_ * dpiScale;
    float drawH = imgH_ * zoom_ * dpiScale;
    float cx = viewW / 2.0f + panX_ * dpiScale;
    float cy = viewH / 2.0f + panY_ * dpiScale;

    int dstX = (int)(cx - drawW / 2.0f);
    int dstY = (int)(cy - drawH / 2.0f);
    int dstW = (int)drawW;
    int dstH = (int)drawH;

    /* 四区域背景填充（XP CZoomWnd::OnEraseBkgnd 同逻辑） */
    RECT rcFill;
    /* 上 */
    if (dstY > 0) {
        rcFill = { 0, 0, viewW, std::min(dstY, viewH) };
        FillRect(hdc, &rcFill, hbrBg);
    }
    /* 下 */
    if (dstY + dstH < viewH) {
        rcFill = { 0, std::max(dstY + dstH, 0), viewW, viewH };
        FillRect(hdc, &rcFill, hbrBg);
    }
    /* 左 */
    if (dstX > 0) {
        rcFill = { 0, std::max(dstY, 0), std::min(dstX, viewW), std::min(dstY + dstH, viewH) };
        FillRect(hdc, &rcFill, hbrBg);
    }
    /* 右 */
    if (dstX + dstW < viewW) {
        rcFill = { std::max(dstX + dstW, 0), std::max(dstY, 0), viewW, std::min(dstY + dstH, viewH) };
        FillRect(hdc, &rcFill, hbrBg);
    }
    DeleteObject(hbrBg);

    /* StretchBlt 图片 */
    HDC hdcMem = CreateCompatibleDC(hdc);
    HGDIOBJ oldBm = SelectObject(hdcMem, hbmFront_);

    SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, nullptr);

    /* 裁剪源区域（只 blit 可见部分） */
    int srcX = 0, srcY = 0, srcW = imgW_, srcH = imgH_;
    int clipDstX = dstX, clipDstY = dstY, clipDstW = dstW, clipDstH = dstH;

    if (clipDstX < 0) {
        int clip = -clipDstX;
        srcX += (int)((float)clip / drawW * imgW_);
        srcW -= (int)((float)clip / drawW * imgW_);
        clipDstW -= clip;
        clipDstX = 0;
    }
    if (clipDstY < 0) {
        int clip = -clipDstY;
        srcY += (int)((float)clip / drawH * imgH_);
        srcH -= (int)((float)clip / drawH * imgH_);
        clipDstH -= clip;
        clipDstY = 0;
    }
    if (clipDstX + clipDstW > viewW) {
        int clip = (clipDstX + clipDstW) - viewW;
        srcW -= (int)((float)clip / drawW * imgW_);
        clipDstW -= clip;
    }
    if (clipDstY + clipDstH > viewH) {
        int clip = (clipDstY + clipDstH) - viewH;
        srcH -= (int)((float)clip / drawH * imgH_);
        clipDstH -= clip;
    }

    if (srcW > 0 && srcH > 0 && clipDstW > 0 && clipDstH > 0) {
        StretchBlt(hdc, clipDstX, clipDstY, clipDstW, clipDstH,
                   hdcMem, srcX, srcY, srcW, srcH, SRCCOPY);
    }

    SelectObject(hdcMem, oldBm);
    DeleteDC(hdcMem);
}

void ImageViewGDIWidget::OnChildWheel(short delta, int x, int y) {
    float oldZoom = zoom_;
    float factor = (delta > 0) ? 1.15f : (1.0f / 1.15f);
    float newZoom = std::clamp(zoom_ * factor, minZoom_, maxZoom_);
    /* 跨越 100% 时吸附到 1.0 */
    if ((oldZoom < 1.0f && newZoom > 1.0f) || (oldZoom > 1.0f && newZoom < 1.0f)) {
        newZoom = 1.0f;
    }
    zoom_ = newZoom;

    /* 以鼠标位置为中心缩放 */
    RECT rc;
    GetClientRect(hwndChild_, &rc);
    float viewW = (float)(rc.right - rc.left);
    float viewH = (float)(rc.bottom - rc.top);

    UINT dpi = 96;
    typedef UINT(WINAPI* PFN)(HWND);
    static PFN pfn = (PFN)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (pfn) dpi = pfn(hwndChild_);
    float dpiScale = (float)dpi / 96.0f;

    float mx = (float)x / dpiScale;  /* 物理 → 逻辑 */
    float my = (float)y / dpiScale;
    float logViewW = viewW / dpiScale;
    float logViewH = viewH / dpiScale;

    /* 鼠标对应的图片坐标在缩放前后保持不变 */
    float imgCx = (mx - logViewW / 2.0f - panX_) / oldZoom;
    float imgCy = (my - logViewH / 2.0f - panY_) / oldZoom;
    panX_ = mx - logViewW / 2.0f - imgCx * zoom_;
    panY_ = my - logViewH / 2.0f - imgCy * zoom_;

    InvalidateRect(hwndChild_, nullptr, FALSE);
    NotifyViewport();
}

/* ---- 图片加载 ---- */

void ImageViewGDIWidget::SetParentHwnd(HWND parent) {
    hwndParent_ = parent;
    /* 不立即创建子窗口——延迟到 SetFile 时创建 */
}

void ImageViewGDIWidget::SetFile(const std::wstring& path) {
    Clear();
    if (rendering_) return;

    /* 如果子窗口还没创建，用父窗口的 HWND 创建 */
    if (!hwndChild_ && hwndParent_) {
        EnsureChildWindow(hwndParent_);
    }

    /* 确保子窗口可见并正确定位 */
    if (hwndChild_) {
        UpdateChildPos();
        ShowWindow(hwndChild_, SW_SHOW);
    }

    rendering_ = true;

    std::wstring pathCopy = path;
    HWND hwnd = hwndChild_;

    std::thread([this, pathCopy, hwnd]() {
        auto* img = Gdiplus::Image::FromFile(pathCopy.c_str());
        if (!img || img->GetLastStatus() != Gdiplus::Ok) {
            delete img;
            rendering_ = false;
            return;
        }

        int w = (int)img->GetWidth();
        int h = (int)img->GetHeight();

        /* 创建 DIBSection（内存计入内核 GDI 堆） */
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;  /* top-down */
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP hbm = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!hbm) { delete img; rendering_ = false; return; }

        /* GDI+ 渲染全图到 DIBSection */
        HDC hdcScreen = GetDC(nullptr);
        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        HGDIOBJ oldBm = SelectObject(hdcMem, hbm);

        {
            Gdiplus::Graphics gfx(hdcMem);
            gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            gfx.DrawImage(img, 0, 0, w, h);
        }

        SelectObject(hdcMem, oldBm);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        delete img;

        /* 主线程更新 */
        hbmFront_ = hbm;
        frontBits_ = (uint8_t*)bits;
        imgW_ = w;
        imgH_ = h;
        rendering_ = false;

        /* 通知主线程重绘 */
        if (hwnd && IsWindow(hwnd)) {
            PostMessageW(hwnd, WM_APP + 300, 0, 0);
        }
    }).detach();
}

void ImageViewGDIWidget::SetPixels(const void* pixels, int w, int h, int stride) {
    Clear();
    if (!pixels || w <= 0 || h <= 0) return;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    hbmFront_ = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hbmFront_) return;

    frontBits_ = (uint8_t*)bits;
    imgW_ = w;
    imgH_ = h;

    int srcStride = (stride > 0) ? stride : w * 4;
    int dstStride = w * 4;
    for (int y = 0; y < h; y++) {
        memcpy(frontBits_ + y * dstStride,
               (const uint8_t*)pixels + y * srcStride,
               std::min(srcStride, dstStride));
    }
}

void ImageViewGDIWidget::Clear() {
    if (hbmFront_) {
        DeleteObject(hbmFront_);
        hbmFront_ = nullptr;
        frontBits_ = nullptr;
    }
    imgW_ = imgH_ = 0;
    zoom_ = 1.0f;
    panX_ = panY_ = 0;
    if (hwndChild_) {
        ShowWindow(hwndChild_, SW_HIDE);
        InvalidateRect(hwndChild_, nullptr, FALSE);
    }
}

/* ---- 缩放/平移 ---- */

void ImageViewGDIWidget::SetZoom(float z) {
    zoom_ = std::clamp(z, minZoom_, maxZoom_);
    if (hwndChild_) InvalidateRect(hwndChild_, nullptr, FALSE);
    NotifyViewport();
}

void ImageViewGDIWidget::SetPan(float x, float y) {
    panX_ = x; panY_ = y;
    if (hwndChild_) InvalidateRect(hwndChild_, nullptr, FALSE);
    NotifyViewport();
}

void ImageViewGDIWidget::FitToView() {
    if (imgW_ <= 0 || imgH_ <= 0) return;
    float areaW = rect.right - rect.left;
    float areaH = rect.bottom - rect.top;
    if (areaW <= 0 || areaH <= 0) return;

    float zx = areaW / (float)imgW_;
    float zy = areaH / (float)imgH_;
    zoom_ = std::min(zx, zy);
    if (zoom_ > 1.0f) zoom_ = 1.0f;
    zoom_ = std::clamp(zoom_, minZoom_, maxZoom_);
    panX_ = panY_ = 0;
    if (hwndChild_) InvalidateRect(hwndChild_, nullptr, FALSE);
    NotifyViewport();
}

void ImageViewGDIWidget::NotifyViewport() {
    if (onViewportChanged) onViewportChanged(zoom_, panX_, panY_);
}

/* ---- Widget 虚函数 ---- */

void ImageViewGDIWidget::OnDraw(Renderer& r) {
    /* 确保子窗口存在 */
    HWND parent = r.Hwnd();
    if (parent && !hwndChild_) {
        hwndParent_ = parent;
        EnsureChildWindow(parent);
    }
    /* 每帧同步 popup 窗口位置（跟随父窗口移动/缩放） */
    if (hwndChild_ && hbmFront_) {
        UpdateChildPos();
    }
}

void ImageViewGDIWidget::DoLayout() {
    Widget::DoLayout();
    UpdateChildPos();
}

D2D1_SIZE_F ImageViewGDIWidget::SizeHint() const {
    return { 0, 0 };  /* expanding */
}

} // namespace ui
