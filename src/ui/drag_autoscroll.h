#pragma once
//
// drag_autoscroll.h — 拖放时贴边自动滚动的步长 (纯算术, 不碰 Win32, 有单测)。
//
// 浏览器拖放的行为: 拖着东西停在可滚动区域上/下边缘的一条带里, 区域按固定
// 节拍持续往那个方向滚, 越贴边越快 (线性), 滚到头就停; 光标离开区域不滚。
// 窗口在拖动期间按 kTickMs 调一次 Step, 拿到非零就滚, 零就停表。
//
namespace ui::dragscroll {

constexpr float kEdgeZoneDip = 32.0f;   // 边缘带高度 (矮视口收到高度的 1/3)
constexpr float kMaxStepDip  = 20.0f;   // 贴边时每拍滚动量 → 约 1250 DIP/s
constexpr unsigned kTickMs   = 16;

// cursorY / top / bottom 同一坐标系 (窗口 DIP); scrollY ∈ [0, maxScrollY]。
// 返回本拍应滚动的量: 负 = 往上 (scrollY 减小), 正 = 往下, 0 = 不滚。
// 结果已钳在剩余可滚范围内。
inline float Step(float cursorY, float top, float bottom,
                  float scrollY, float maxScrollY,
                  float zone = kEdgeZoneDip, float maxStep = kMaxStepDip) {
    if (cursorY < top || cursorY > bottom || maxScrollY <= 0.0f) return 0.0f;
    const float height = bottom - top;
    if (zone > height / 3.0f) zone = height / 3.0f;   // 上下两带不重叠
    if (zone <= 0.0f) return 0.0f;

    const float toTop = cursorY - top;
    const float toBottom = bottom - cursorY;
    if (toTop < zone && scrollY > 0.0f) {
        float step = maxStep * (1.0f - toTop / zone);
        return -(step < scrollY ? step : scrollY);
    }
    if (toBottom < zone && scrollY < maxScrollY) {
        float room = maxScrollY - scrollY;
        float step = maxStep * (1.0f - toBottom / zone);
        return step < room ? step : room;
    }
    return 0.0f;
}

}  // namespace ui::dragscroll
