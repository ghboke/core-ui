#pragma once

#include "render_handles.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace ui {

/* 资源的像素字节: 要么 store 自己持有一份拷贝 (AddImage), 要么借用宿主的缓冲
 * (owner 保活, 最后一个引用放掉时归还给宿主)。使用方只看 data() / size()。 */
class PixelBytes {
public:
    explicit PixelBytes(std::vector<uint8_t> owned)
        : owned_(std::move(owned)), data_(owned_.data()), size_(owned_.size()) {}
    PixelBytes(const uint8_t* data, size_t size, std::shared_ptr<const void> owner)
        : owner_(std::move(owner)), data_(data), size_(size) {}
    PixelBytes(const PixelBytes&) = delete;
    PixelBytes& operator=(const PixelBytes&) = delete;

    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }

private:
    std::vector<uint8_t>        owned_;
    std::shared_ptr<const void> owner_;
    const uint8_t*              data_ = nullptr;
    size_t                      size_ = 0;
};

struct CpuImageResource {
    ResourceKey key;
    int width = 0;
    int height = 0;
    int stride = 0;
    PixelFormat format = PixelFormat::BgraPremul;
    std::shared_ptr<const PixelBytes> bytes;
    size_t byte_size = 0;
    bool pinned_visible = false;
    bool evictable = false;
};

class ResourceStore {
public:
    ResourceKey AddImage(ResourceKind kind,
                         uint64_t imageGeneration,
                         int width,
                         int height,
                         int stride,
                         PixelFormat format,
                         const void* pixels,
                         bool pinnedVisible = false);

    /* 免拷贝版: 直接借用宿主缓冲 pixels, owner 负责保活。资源被移除 / Clear / 驱逐,
     * 且渲染端不再 Acquire 着之后, owner 的最后一个引用才放掉 (可能在任意线程)。
     * 期间宿主不得修改这块像素。参数不合法时返回无效 key, owner 立即放掉。 */
    ResourceKey AddImageShared(ResourceKind kind,
                               uint64_t imageGeneration,
                               int width,
                               int height,
                               int stride,
                               PixelFormat format,
                               const void* pixels,
                               std::shared_ptr<const void> owner,
                               bool pinnedVisible = false);

    bool UpdateImage(const ResourceKey& key,
                     int width,
                     int height,
                     int stride,
                     PixelFormat format,
                     const void* pixels,
                     bool pinnedVisible = false);

    std::shared_ptr<const CpuImageResource> Acquire(const ResourceKey& key) const;
    void SetPinnedVisible(const ResourceKey& key, bool pinned);
    void Remove(const ResourceKey& key);
    void MarkGenerationEvictable(uint64_t imageGeneration);
    void PurgeGeneration(uint64_t imageGeneration);
    void Clear();

    void SetByteBudget(size_t bytes);
    size_t ByteBudget() const;
    size_t TotalBytes() const;

private:
    static int BytesPerPixel(PixelFormat format);
    ResourceKey InsertImage_(ResourceKind kind, uint64_t imageGeneration,
                             int width, int height, int stride, PixelFormat format,
                             std::shared_ptr<const PixelBytes> bytes,
                             bool pinnedVisible, bool shared);
    void EvictOverBudgetLocked();

    mutable std::mutex mutex_;
    std::unordered_map<ResourceKey, std::shared_ptr<CpuImageResource>, ResourceKeyHash> resources_;
    uint64_t nextResourceId_ = 1;
    size_t totalBytes_ = 0;
    size_t byteBudget_ = 512ull * 1024ull * 1024ull;
};

ResourceStore& GlobalResourceStore();

} // namespace ui
