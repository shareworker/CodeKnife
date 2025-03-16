#pragma once
#include <cstddef>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <cassert>
#include <iostream>

// Configuration options
// Define this macro to zero out memory blocks on allocation
// #define MEMORY_POOL_ZERO_ON_ALLOCATE
// Define this macro to validate pointers on deallocation
// #define MEMORY_POOL_VALIDATE_POINTERS

namespace util {
namespace memory {

// Memory pool for fixed-size memory blocks
class FixedSizeMemoryPool {
public:
    explicit FixedSizeMemoryPool(size_t block_size, size_t initial_blocks = 8);
    ~FixedSizeMemoryPool();

    void* allocate();
    void deallocate(void* ptr);

    size_t block_size() const { return block_size_; }
    size_t num_blocks() const { return blocks_.size(); }
    size_t num_free_blocks() const { return free_blocks_.size(); }
    
    // Add memory usage statistics
    double usage_ratio() const { 
        return blocks_.empty() ? 0.0 : 
            static_cast<double>(blocks_.size() - free_blocks_.size()) / blocks_.size(); 
    }

private:
    void expand(size_t num_blocks);

    const size_t block_size_;
    std::vector<char*> blocks_;        // All allocated memory blocks
    std::vector<char*> free_blocks_;   // Available memory blocks
    mutable std::mutex mutex_;
};

// General memory pool supporting different sizes of memory allocation
class MemoryPool {
public:
    static MemoryPool& GetInstance();
    
    void* Allocate(size_t size);
    void Deallocate(void* ptr, size_t size);
    
    // Add statistics and debugging features
    size_t GetTotalAllocations() const { return total_allocations_; }
    size_t GetCurrentAllocations() const { return current_allocations_; }
    size_t GetLargeAllocations() const;
    double GetMemoryUsage() const;
    void PrintStats() const;
    
    // Add memory pool cleanup method
    void Trim();

    // Disable copy and assignment
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

private:
    MemoryPool();
    ~MemoryPool();

    // Common memory block sizes
    static constexpr size_t kSmallBlockSizes[] = {
        8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096
    };
    static constexpr size_t kNumPools = sizeof(kSmallBlockSizes) / sizeof(kSmallBlockSizes[0]);

    std::unique_ptr<FixedSizeMemoryPool> pools_[kNumPools];
    mutable std::mutex large_alloc_mutex_;
    std::unordered_map<void*, size_t> large_allocations_;

    // Statistics
    mutable std::mutex stats_mutex_;
    size_t total_allocations_ = 0;
    size_t current_allocations_ = 0;
};

// Custom deleter for smart pointers
template <typename T>
struct MemoryPoolDeleter {
    void operator()(T* ptr) {
        if (ptr) {
            ptr->~T();
            MemoryPool::GetInstance().Deallocate(ptr, sizeof(T));
        }
    }
};

// Helper function to allocate objects from memory pool
template <typename T, typename... Args>
std::unique_ptr<T, MemoryPoolDeleter<T>> make_pool_ptr(Args&&... args) {
    void* mem = MemoryPool::GetInstance().Allocate(sizeof(T));
    T* obj = new(mem) T(std::forward<Args>(args)...);
    return std::unique_ptr<T, MemoryPoolDeleter<T>>(obj);
}

// Add array allocation support
template <typename T>
struct MemoryPoolArrayDeleter {
    size_t size;
    
    explicit MemoryPoolArrayDeleter(size_t n) : size(n) {}
    
    void operator()(T* ptr) {
        if (ptr) {
            // Call destructors for all elements
            for (size_t i = 0; i < size; ++i) {
                ptr[i].~T();
            }
            MemoryPool::GetInstance().Deallocate(ptr, sizeof(T) * size);
        }
    }
};

// Helper function to allocate arrays from memory pool
template <typename T>
std::unique_ptr<T[], MemoryPoolArrayDeleter<T>> make_pool_array(size_t n) {
    if (n == 0) return std::unique_ptr<T[], MemoryPoolArrayDeleter<T>>(nullptr, MemoryPoolArrayDeleter<T>(0));
    
    void* mem = MemoryPool::GetInstance().Allocate(sizeof(T) * n);
    T* arr = static_cast<T*>(mem);
    
    // Use default constructor to initialize all elements
    for (size_t i = 0; i < n; ++i) {
        new(&arr[i]) T();
    }
    
    return std::unique_ptr<T[], MemoryPoolArrayDeleter<T>>(arr, MemoryPoolArrayDeleter<T>(n));
}

} // namespace memory
} // namespace util