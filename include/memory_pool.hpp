#pragma once
#include <cstddef>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <cassert>
#include <iostream>

namespace util {
namespace memory {

// 固定大小内存块的内存池
class FixedSizeMemoryPool {
public:
    explicit FixedSizeMemoryPool(size_t block_size, size_t initial_blocks = 8);
    ~FixedSizeMemoryPool();

    void* allocate();
    void deallocate(void* ptr);

    size_t block_size() const { return block_size_; }
    size_t num_blocks() const { return blocks_.size(); }
    size_t num_free_blocks() const { return free_blocks_.size(); }

private:
    void expand(size_t num_blocks);

    const size_t block_size_;
    std::vector<char*> blocks_;
    std::vector<char*> free_blocks_;
    std::mutex mutex_;
};

// 通用内存池，支持不同大小的内存分配
class MemoryPool {
public:
    static MemoryPool& GetInstance();
    
    void* Allocate(size_t size);
    void Deallocate(void* ptr, size_t size);
    
    // 添加统计和调试功能
    size_t GetTotalAllocations() const { return total_allocations_; }
    size_t GetCurrentAllocations() const { return current_allocations_; }
    void PrintStats() const;

    // 禁止拷贝和赋值
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

private:
    MemoryPool();
    ~MemoryPool();

    // 常见的内存块大小
    static constexpr size_t kSmallBlockSizes[] = {
        8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096
    };
    static constexpr size_t kNumPools = sizeof(kSmallBlockSizes) / sizeof(kSmallBlockSizes[0]);

    std::unique_ptr<FixedSizeMemoryPool> pools_[kNumPools];
    std::mutex large_alloc_mutex_;
    std::unordered_map<void*, size_t> large_allocations_;

    // 统计信息
    mutable std::mutex stats_mutex_;
    size_t total_allocations_ = 0;
    size_t current_allocations_ = 0;
};

// 智能指针的自定义删除器
template <typename T>
struct MemoryPoolDeleter {
    void operator()(T* ptr) {
        if (ptr) {
            ptr->~T();
            MemoryPool::GetInstance().Deallocate(ptr, sizeof(T));
        }
    }
};

// 从内存池分配对象的辅助函数
template <typename T, typename... Args>
std::unique_ptr<T, MemoryPoolDeleter<T>> make_pool_ptr(Args&&... args) {
    void* mem = MemoryPool::GetInstance().Allocate(sizeof(T));
    T* obj = new(mem) T(std::forward<Args>(args)...);
    return std::unique_ptr<T, MemoryPoolDeleter<T>>(obj);
}

} // namespace memory
} // namespace util