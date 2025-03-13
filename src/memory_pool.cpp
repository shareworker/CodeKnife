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
    
    size_t old_size = blocks_.size();
    blocks_.resize(old_size + num_blocks);
    free_blocks_.reserve(free_blocks_.size() + num_blocks);
    
    for (size_t i = 0; i < num_blocks; ++i) {
        // 使用aligned_alloc来确保内存对齐，提高访问效率
        // 对齐到至少64字节（缓存行大小），或者block_size的倍数
        size_t alignment = std::max(static_cast<size_t>(64), block_size_);
        
        // 确保alignment是2的幂
        if (alignment & (alignment - 1)) {
            alignment = 1 << (sizeof(size_t) * 8 - __builtin_clzl(alignment));
        }
        
        // 确保block_size是alignment的倍数
        size_t aligned_size = ((block_size_ + alignment - 1) / alignment) * alignment;
        
#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L
        // 使用posix_memalign在支持的平台上
        void* p = nullptr;
        if (posix_memalign(&p, alignment, aligned_size) != 0) {
            throw std::bad_alloc();
        }
        blocks_[old_size + i] = static_cast<char*>(p);
#else
        // 回退到标准分配
        blocks_[old_size + i] = static_cast<char*>(::operator new(aligned_size));
#endif
        
        free_blocks_.push_back(blocks_[old_size + i]);
    }
}

void* FixedSizeMemoryPool::allocate() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (free_blocks_.empty()) {
        // 当没有空闲块时，扩展内存池
        // 使用指数增长策略，但限制最大增长量
        size_t current_size = blocks_.size();
        size_t new_blocks = std::min(current_size, static_cast<size_t>(1024));
        new_blocks = std::max(new_blocks, static_cast<size_t>(8));
        expand(new_blocks);
    }
    
    // 使用LIFO策略，提高缓存局部性
    void* block = free_blocks_.back();
    free_blocks_.pop_back();
    
    // 清零内存块以避免使用未初始化的内存
    // 注意：在性能敏感的场景中可能需要禁用此功能
#ifdef MEMORY_POOL_ZERO_ON_ALLOCATE
    std::memset(block, 0, block_size_);
#endif
    
    return block;
}

void FixedSizeMemoryPool::deallocate(void* ptr) {
    if (!ptr) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 验证指针是否属于该内存池
    // 这是一个O(n)操作，在DEBUG模式下可能会影响性能
#ifdef MEMORY_POOL_VALIDATE_POINTERS
    bool valid_ptr = false;
    for (auto block : blocks_) {
        if (ptr == block) {
            valid_ptr = true;
            break;
        }
    }
    
    if (!valid_ptr) {
        std::cerr << "Warning: Attempted to deallocate pointer not from this pool" << std::endl;
        return;
    }
    
    // 检查是否重复释放
    for (auto free_block : free_blocks_) {
        if (ptr == free_block) {
            std::cerr << "Warning: Attempted to deallocate already freed pointer" << std::endl;
            return;
        }
    }
#endif
    
    // 将指针添加到空闲列表
    free_blocks_.push_back(static_cast<char*>(ptr));
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
        // 修复逻辑错误：使用二分查找找到合适的内存池
        // 这比线性搜索更高效，尤其是当内存池数量较多时
        size_t left = 0;
        size_t right = kNumPools - 1;
        
        while (left <= right) {
            size_t mid = left + (right - left) / 2;
            
            if (kSmallBlockSizes[mid] < size) {
                left = mid + 1;
            } else if (mid > 0 && kSmallBlockSizes[mid - 1] >= size) {
                right = mid - 1;
            } else {
                // 找到合适的内存池
                ptr = pools_[mid]->allocate();
                return ptr;
            }
        }
    }
    
    // 对于大内存块，直接使用全局分配器
    {
        std::lock_guard<std::mutex> lock(large_alloc_mutex_);
        
        // 对齐大小到至少8字节边界，提高内存访问效率
        size_t aligned_size = ((size + 7) / 8) * 8;
        
        try {
            ptr = ::operator new(aligned_size);
            large_allocations_[ptr] = aligned_size;
        } catch (const std::bad_alloc& e) {
            // 处理内存分配失败
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            --current_allocations_;
            --total_allocations_;
            throw; // 重新抛出异常
        }
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
            if (it->second != size) {
                // 尝试释放大小不匹配的内存块
                // 这可能是由于用户传入了错误的size参数导致的
                // 应该记录日志而不是输出到标准错误
                std::cerr << "Warning: Attempted to deallocate with mismatched size" << std::endl;
            } else {
                ::operator delete(ptr);
                large_allocations_.erase(it);
            }
        } else {
            // 尝试释放未知指针
            // 这可能是由于用户传入了错误的指针导致的
            // 应该记录日志而不是输出到标准错误
            std::cerr << "Warning: Attempted to deallocate unknown pointer" << std::endl;
        }
    }
}

size_t MemoryPool::GetLargeAllocations() const {
    std::lock_guard<std::mutex> lock(large_alloc_mutex_);
    return large_allocations_.size();
}

double MemoryPool::GetMemoryUsage() const {
    double total_usage = 0.0;
    size_t total_pools = 0;
    
    // 计算所有小内存池的平均使用率
    for (size_t i = 0; i < kNumPools; ++i) {
        if (pools_[i]->num_blocks() > 0) {
            total_usage += pools_[i]->usage_ratio();
            total_pools++;
        }
    }
    
    return total_pools > 0 ? total_usage / total_pools : 0.0;
}

void MemoryPool::Trim() {
    // 这个方法会尝试释放一些未使用的内存
    // 注意：这是一个昂贵的操作，应该在内存压力大时调用
    
    // 目前我们不实际释放内存，因为这可能会导致性能问题
    // 在实际应用中，可以根据需要实现更复杂的内存回收策略
    
    std::cout << "Memory pool trim operation requested" << std::endl;
    std::cout << "Current memory usage: " << (GetMemoryUsage() * 100.0) << "%" << std::endl;
    
    // 这里可以添加实际的内存回收逻辑
    // 例如，当某个内存池的使用率低于某个阈值时，释放部分内存
}

void MemoryPool::PrintStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    std::cout << "Memory Pool Statistics:" << std::endl;
    std::cout << "  Total allocations: " << total_allocations_ << std::endl;
    std::cout << "  Current allocations: " << current_allocations_ << std::endl;
    std::cout << "  Large allocations: " << large_allocations_.size() << std::endl;
    std::cout << "  Overall memory usage: " << (GetMemoryUsage() * 100.0) << "%" << std::endl;
    
    std::cout << "  Pool statistics:" << std::endl;
    for (size_t i = 0; i < kNumPools; ++i) {
        std::cout << "    Size " << kSmallBlockSizes[i] 
                  << ": " << pools_[i]->num_blocks() << " blocks, " 
                  << pools_[i]->num_free_blocks() << " free, "
                  << (pools_[i]->usage_ratio() * 100.0) << "% used" << std::endl;
    }
}

} // namespace memory
} // namespace util