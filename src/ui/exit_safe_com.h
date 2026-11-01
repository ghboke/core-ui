#pragma once
// ExitLeakComPtr<T> —— 进程级全局 / 静态 COM 对象 (D3D / DXGI / D2D / DComp 设备)
// 专用的持有者。用法与 Microsoft::WRL::ComPtr 完全一致, 唯一区别是**析构时只放手,
// 不 Release**。
//
// 为什么: 全局对象的析构发生在 DLL 卸载 (ExitProcess → LdrShutdownProcess →
// atexit 表) 时, 那时持着加载器锁、进程正在拆。此刻 Release 最后一个 D3D 设备
// 会连带 DXGI factory FinalRelease, 部分机器上 DXGI 直接 RaiseException(0x87A),
// 宿主表现为"关闭即已停止工作" (GuoheView 用户 Win10 19045 的 dump 实证)。
//
// 正确的释放时机是 ui_shutdown() —— 那里显式 Reset()。本类只是保险: 宿主没调
// ui_shutdown (或之后又懒建了设备) 时, 退出阶段也不再碰 DXGI, 交给系统回收。
// core-ui 是随进程加载的 DLL, 不会中途 FreeLibrary, 不存在真正的泄漏。
#include <wrl/client.h>

namespace ui {

template <class T>
class ExitLeakComPtr : public Microsoft::WRL::ComPtr<T> {
    using Base = Microsoft::WRL::ComPtr<T>;

public:
    using Base::Base;
    using Base::operator=;

    ExitLeakComPtr() = default;
    /* 自定义了析构, 拷贝/移动必须写全, 语义与 ComPtr 相同 (移动不加引用)。 */
    ExitLeakComPtr(const ExitLeakComPtr& o) : Base(o) {}
    ExitLeakComPtr(ExitLeakComPtr&& o) noexcept : Base(static_cast<Base&&>(o)) {}
    ExitLeakComPtr& operator=(const ExitLeakComPtr& o) {
        Base::operator=(static_cast<const Base&>(o));
        return *this;
    }
    ExitLeakComPtr& operator=(ExitLeakComPtr&& o) noexcept {
        Base::operator=(static_cast<Base&&>(o));
        return *this;
    }

    ~ExitLeakComPtr() {
        // 先摘掉指针, 随后基类析构看到空指针就不会 Release。
        (void)this->Detach();
    }
};

}  // namespace ui
