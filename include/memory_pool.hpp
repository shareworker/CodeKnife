#pragma once
#include <cstddef>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <cassert>
#include <iostream>

// 配置选项
// 定义此宏以在分配时将内存块清零
// #define MEMORY_POOL_ZERO_ON_ALLOCATE
// 定义此宏以在释放时验证指针
// #define MEMORY_POOL_VALIDATE_POINTERS

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
    
    // 添加内存使用率统计
    double usage_ratio() const { 
        return blocks_.empty() ? 0.0 : 
            static_cast<double>(blocks_.size() - free_blocks_.size()) / blocks_.size(); 
    }

private:
    void expand(size_t num_blocks);

    const size_t block_size_;
    std::vector<char*> blocks_;        // 所有分配的内存块
    std::vector<char*> free_blocks_;   // 可用的内存块
    mutable std::mutex mutex_;
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
    size_t GetLargeAllocations() const;
    double GetMemoryUsage() const;
    void PrintStats() const;
    
    // 添加内存池清理方法
    void Trim();

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
    mutable std::mutex large_alloc_mutex_;
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

// 添加数组分配支持
template <typename T>
struct MemoryPoolArrayDeleter {
    size_t size;
    
    explicit MemoryPoolArrayDeleter(size_t n) : size(n) {}
    
    void operator()(T* ptr) {
        if (ptr) {
            // 调用所有元素的析构函数
            for (size_t i = 0; i < size; ++i) {
                ptr[i].~T();
            }
            MemoryPool::GetInstance().Deallocate(ptr, sizeof(T) * size);
        }
    }
};

// 从内存池分配数组的辅助函数
template <typename T>
std::unique_ptr<T[], MemoryPoolArrayDeleter<T>> make_pool_array(size_t n) {
    if (n == 0) return std::unique_ptr<T[], MemoryPoolArrayDeleter<T>>(nullptr, MemoryPoolArrayDeleter<T>(0));
    
    void* mem = MemoryPool::GetInstance().Allocate(sizeof(T) * n);
    T* arr = static_cast<T*>(mem);
    
    // 使用默认构造函数初始化所有元素
    for (size_t i = 0; i < n; ++i) {
        new(&arr[i]) T();
    }
    
    return std::unique_ptr<T[], MemoryPoolArrayDeleter<T>>(arr, MemoryPoolArrayDeleter<T>(n));
}

} // namespace memory
} // namespace util