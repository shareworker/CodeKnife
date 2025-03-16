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
        // Use aligned_alloc to ensure memory alignment for improved access efficiency
        // Align to at least 64 bytes (cache line size), or a multiple of block_size
        size_t alignment = std::max(static_cast<size_t>(64), block_size_);
        
        // Ensure alignment is a power of 2
        if (alignment & (alignment - 1)) {
            alignment = 1 << (sizeof(size_t) * 8 - __builtin_clzl(alignment));
        }
        
        // Ensure block_size is a multiple of alignment
        size_t aligned_size = ((block_size_ + alignment - 1) / alignment) * alignment;
        
#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L
        // Use posix_memalign on supported platforms
        void* p = nullptr;
        if (posix_memalign(&p, alignment, aligned_size) != 0) {
            throw std::bad_alloc();
        }
        blocks_[old_size + i] = static_cast<char*>(p);
#else
        // Fall back to standard allocation
        blocks_[old_size + i] = static_cast<char*>(::operator new(aligned_size));
#endif
        
        free_blocks_.push_back(blocks_[old_size + i]);
    }
}

void* FixedSizeMemoryPool::allocate() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (free_blocks_.empty()) {
        // Expand the memory pool when there are no free blocks
        // Use exponential growth strategy, limiting maximum growth
        size_t current_size = blocks_.size();
        size_t new_blocks = std::min(current_size, static_cast<size_t>(1024));
        new_blocks = std::max(new_blocks, static_cast<size_t>(8));
        expand(new_blocks);
    }
    
    // Use LIFO strategy to improve cache locality
    void* block = free_blocks_.back();
    free_blocks_.pop_back();
    
    // Clear memory block to avoid using uninitialized memory
    // Note: This may need to be disabled in performance-sensitive scenarios
#ifdef MEMORY_POOL_ZERO_ON_ALLOCATE
    std::memset(block, 0, block_size_);
#endif
    
    return block;
}

void FixedSizeMemoryPool::deallocate(void* ptr) {
    if (!ptr) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Validate that the pointer belongs to this memory pool
    // This is an O(n) operation and may impact performance in DEBUG mode
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
    
    // Check for double free
    for (auto free_block : free_blocks_) {
        if (ptr == free_block) {
            std::cerr << "Warning: Attempted to deallocate already freed pointer" << std::endl;
            return;
        }
    }
#endif
    
    // Add the pointer to the free list
    free_blocks_.push_back(static_cast<char*>(ptr));
}

// MemoryPool implementation
constexpr size_t MemoryPool::kSmallBlockSizes[];

MemoryPool::MemoryPool() {
    // Initialize various fixed-size memory pools
    for (size_t i = 0; i < kNumPools; ++i) {
        pools_[i] = std::make_unique<FixedSizeMemoryPool>(kSmallBlockSizes[i]);
    }
}

MemoryPool::~MemoryPool() {
    // Check for memory leaks
    if (!large_allocations_.empty()) {
        std::cerr << "Warning: Memory leak detected. " 
                  << large_allocations_.size() 
                  << " large allocations not freed." << std::endl;
        
        // Release all large memory blocks
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
    
    // Update statistics
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        ++total_allocations_;
        ++current_allocations_;
    }
    
    // For small memory blocks, use fixed-size memory pools
    if (size > 0) {
        // Fix logic error: use binary search to find the appropriate memory pool
        // This is more efficient than linear search, especially when there are many memory pools
        size_t left = 0;
        size_t right = kNumPools - 1;
        
        while (left <= right) {
            size_t mid = left + (right - left) / 2;
            
            if (kSmallBlockSizes[mid] < size) {
                left = mid + 1;
            } else if (mid > 0 && kSmallBlockSizes[mid - 1] >= size) {
                right = mid - 1;
            } else {
                // Found the appropriate memory pool
                ptr = pools_[mid]->allocate();
                return ptr;
            }
        }
    }
    
    // For large memory blocks, use the global allocator
    {
        std::lock_guard<std::mutex> lock(large_alloc_mutex_);
        
        // Align size to at least 8-byte boundaries for improved memory access efficiency
        size_t aligned_size = ((size + 7) / 8) * 8;
        
        try {
            ptr = ::operator new(aligned_size);
            large_allocations_[ptr] = aligned_size;
        } catch (const std::bad_alloc& e) {
            // Handle memory allocation failure
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            --current_allocations_;
            --total_allocations_;
            throw; // Re-throw exception
        }
    }
    
    return ptr;
}

void MemoryPool::Deallocate(void* ptr, size_t size) {
    if (!ptr) return;

    // Update statistics
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        --current_allocations_;
    }
    
    // For small memory blocks, return to the corresponding memory pool
    if (size > 0) {
        for (size_t i = 0; i < kNumPools; ++i) {
            if (size <= kSmallBlockSizes[i]) {
                pools_[i]->deallocate(ptr);
                return;
            }
        }
    }
    
    // For large memory blocks, release directly
    {
        std::lock_guard<std::mutex> lock(large_alloc_mutex_);
        
        auto it = large_allocations_.find(ptr);
        if (it != large_allocations_.end()) {
            if (it->second != size) {
                // Attempt to deallocate with mismatched size
                // This may be due to the user passing an incorrect size parameter
                // Should log instead of outputting to standard error
                std::cerr << "Warning: Attempted to deallocate with mismatched size" << std::endl;
            } else {
                ::operator delete(ptr);
                large_allocations_.erase(it);
            }
        } else {
            // Attempt to deallocate unknown pointer
            // This may be due to the user passing an incorrect pointer
            // Should log instead of outputting to standard error
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
    
    // Calculate the average usage ratio of all small memory pools
    for (size_t i = 0; i < kNumPools; ++i) {
        if (pools_[i]->num_blocks() > 0) {
            total_usage += pools_[i]->usage_ratio();
            total_pools++;
        }
    }
    
    return total_pools > 0 ? total_usage / total_pools : 0.0;
}

void MemoryPool::Trim() {
    // This method attempts to release some unused memory
    // Note: This is an expensive operation and should be called when memory pressure is high
    
    // Currently, we do not actually release memory, as this may cause performance issues
    // In a real-world application, a more complex memory recovery strategy can be implemented
    
    std::cout << "Memory pool trim operation requested" << std::endl;
    std::cout << "Current memory usage: " << (GetMemoryUsage() * 100.0) << "%" << std::endl;
    
    // Here, actual memory recovery logic can be added
    // For example, when the usage ratio of a memory pool is below a certain threshold, release some memory
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