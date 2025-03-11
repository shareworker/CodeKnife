#include "../include/memory_pool.hpp"
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <iostream>

namespace util {
namespace memory {

// FixedSizeMemoryPool implementation
FixedSizeMemoryPool::FixedSizeMemoryPool(size_t block_size, size_t initial_blocks)
    : block_size_(block_size) {
    expand(initial_blocks);
}

FixedSizeMemoryPool::~FixedSizeMemoryPool() {
    for (auto block : blocks_) {
        ::operator delete(block);
    }
}

void FixedSizeMemoryPool::expand(size_t num_blocks) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t old_size = blocks_.size();
    blocks_.resize(old_size + num_blocks);
    free_blocks_.reserve(free_blocks_.size() + num_blocks);
    
    for (size_t i = 0; i < num_blocks; ++i) {
        blocks_[old_size + i] = static_cast<char*>(::operator new(block_size_));
        free_blocks_.push_back(blocks_[old_size + i]);
    }
}

void* FixedSizeMemoryPool::allocate() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (free_blocks_.empty()) {
        // 当没有空闲块时，扩展内存池
        expand(blocks_.size()); // 每次扩展当前大小
    }
    
    void* block = free_blocks_.back();
    free_blocks_.pop_back();
    return block;
}

void FixedSizeMemoryPool::deallocate(void* ptr) {
    if (!ptr) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 验证指针是否属于此内存池
    auto it = std::find_if(blocks_.begin(), blocks_.end(), 
                          [ptr](char* block) { return block == ptr; });
    
    if (it != blocks_.end()) {
        free_blocks_.push_back(static_cast<char*>(ptr));
    } else {
        std::cerr << "Warning: Attempted to deallocate a pointer not owned by this pool" << std::endl;
    }
}

// MemoryPool implementation
constexpr size_t MemoryPool::kSmallBlockSizes[];

MemoryPool::MemoryPool() {
    // 初始化各个固定大小的内存池
    for (size_t i = 0; i < kNumPools; ++i) {
        pools_[i] = std::make_unique<FixedSizeMemoryPool>(kSmallBlockSizes[i]);
    }
}

MemoryPool::~MemoryPool() {
    // 检查内存泄漏
    if (!large_allocations_.empty()) {
        std::cerr << "Warning: Memory leak detected. " 
                  << large_allocations_.size() 
                  << " large allocations not freed." << std::endl;
        
        // 释放所有大块内存
        for (auto& pair : large_allocations_) {
            ::operator delete(pair.first);
        }
    }
}

MemoryPool& MemoryPool::GetInstance() {
    static MemoryPool instance;
    return instance;
}

void* MemoryPool::Allocate(size_t size) {
    void* ptr = nullptr;
    
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        ++total_allocations_;
        ++current_allocations_;
    }
    
    // 对于小内存块，使用固定大小的内存池
    if (size > 0) {
        for (size_t i = 0; i < kNumPools; ++i) {
            if (size <= kSmallBlockSizes[i]) {
                ptr = pools_[i]->allocate();
                return ptr;
            }
        }
    }
    
    // 对于大内存块，直接使用全局分配器
    {
        std::lock_guard<std::mutex> lock(large_alloc_mutex_);
        ptr = ::operator new(size);
        large_allocations_[ptr] = size;
    }
    
    return ptr;
}

void MemoryPool::Deallocate(void* ptr, size_t size) {
    if (!ptr) return;
    
    // 更新统计信息
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        --current_allocations_;
    }
    
    // 对于小内存块，返回到对应的内存池
    if (size > 0) {
        for (size_t i = 0; i < kNumPools; ++i) {
            if (size <= kSmallBlockSizes[i]) {
                pools_[i]->deallocate(ptr);
                return;
            }
        }
    }
    
    // 对于大内存块，直接释放
    {
        std::lock_guard<std::mutex> lock(large_alloc_mutex_);
        auto it = large_allocations_.find(ptr);
        if (it != large_allocations_.end()) {
            ::operator delete(ptr);
            large_allocations_.erase(it);
        } else {
            std::cerr << "Warning: Attempted to deallocate unknown pointer" << std::endl;
        }
    }
}

void MemoryPool::PrintStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    std::cout << "Memory Pool Statistics:" << std::endl;
    std::cout << "  Total allocations: " << total_allocations_ << std::endl;
    std::cout << "  Current allocations: " << current_allocations_ << std::endl;
    std::cout << "  Large allocations: " << large_allocations_.size() << std::endl;
    
    std::cout << "  Pool statistics:" << std::endl;
    for (size_t i = 0; i < kNumPools; ++i) {
        std::cout << "    Size " << kSmallBlockSizes[i] 
                  << ": " << pools_[i]->num_blocks() << " blocks, " 
                  << pools_[i]->num_free_blocks() << " free" << std::endl;
    }
}

} // namespace memory
} // namespace util