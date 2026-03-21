#include "util/memory_pool_v2.hpp"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <iostream>

namespace SAK {

size_t FindSizeClass(size_t size) {
    if (size == 0) size = 1;
    
    for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
        if (size <= SIZE_CLASSES[i]) {
            return i;
        }
    }
    
    return NUM_SIZE_CLASSES;
}

thread_local ThreadCache* g_thread_cache = nullptr;

void CleanupDeadCache(ThreadCache* tc) {
    if (!tc) return;
    
    // Clean up slabs
    for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
        Slab* slab = tc->slabs[i];
        while (slab) {
            Slab* next = slab->next;
            free(slab->memory);
            delete slab;
            slab = next;
        }
        tc->slabs[i] = nullptr;
    }
    
    delete tc;
}

void CleanupThreadCache() {
    if (!g_thread_cache) return;
    
    ThreadCache* tc = g_thread_cache;
    
    // Clean up medium object cache
    for (size_t i = 0; i < tc->medium_count; ++i) {
        free(tc->medium_cache[i].ptr);
    }
    tc->medium_count = 0;
    
    // Mark as dead
    tc->dead.store(true, std::memory_order_release);
    
    // Decrement ref count (thread exit is equivalent to releasing one reference)
    size_t old_ref = tc->ref_count.fetch_sub(1, std::memory_order_acq_rel);
    
    if (old_ref == 1) {
        // Ref count dropped to 0, remove from thread_caches_ and cleanup immediately (not added to dead_caches_)
        MemoryPoolV2::GetInstance().UnregisterThreadOnly(tc);
        CleanupDeadCache(tc);
    } else {
        // Still has outstanding blocks, transfer to dead_caches_ for later cleanup
        MemoryPoolV2::GetInstance().UnregisterThread(tc);
    }
    
    g_thread_cache = nullptr;
}

ThreadCache* GetOrCreateThreadCache() {
    if (!g_thread_cache) {
        g_thread_cache = new ThreadCache();
        MemoryPoolV2::GetInstance().RegisterThread(g_thread_cache);

        static thread_local struct ThreadCacheCleanup {
            ~ThreadCacheCleanup() {
                CleanupThreadCache();
            }
        } cleanup;
    }
    
    return g_thread_cache;
}

void RefillFromSlab(ThreadCache* tc, size_t class_idx) {
    size_t block_size = SIZE_CLASSES[class_idx];
    size_t total_size = block_size + sizeof(BlockHeader);
    
    void* slab_mem = malloc(SLAB_SIZE);
    if (!slab_mem) return;  // OOM
    
    Slab* slab = new Slab();
    slab->memory = slab_mem;
    slab->block_size = block_size;
    slab->num_blocks = SLAB_SIZE / total_size;
    slab->free_count.store(0, std::memory_order_relaxed);
    slab->next = tc->slabs[class_idx];
    tc->slabs[class_idx] = slab;
    
    char* ptr = static_cast<char*>(slab_mem);
    for (size_t i = 0; i < slab->num_blocks; ++i) {
        BlockHeader* header = reinterpret_cast<BlockHeader*>(ptr);
        header->size_class = static_cast<uint16_t>(class_idx);
        header->flags = 0;
        header->owner = tc;
        
#ifdef MEMORY_POOL_V2_DEBUG
        header->magic = BlockHeader::MAGIC_VALUE;
#endif
        
        header->next = tc->bins[class_idx].private_list;
        tc->bins[class_idx].private_list = header;
        
        ptr += total_size;
    }
    
    MemoryPoolV2::GetInstance().RecordSlabRefill();
}

void* AllocateSmall(size_t size) {
    ThreadCache* tc = GetOrCreateThreadCache();
    size_t class_idx = FindSizeClass(size);
    if (class_idx >= NUM_SIZE_CLASSES) {
        return nullptr;
    }
    
    SizeClassBin& bin = tc->bins[class_idx];
    if (bin.private_list) {
        BlockHeader* header = static_cast<BlockHeader*>(bin.private_list);
        bin.private_list = header->next;
        tc->alloc_count++;
        tc->ref_count.fetch_add(1, std::memory_order_relaxed);
        return reinterpret_cast<char*>(header) + sizeof(BlockHeader);
    }

    void* mailbox_head = bin.mailbox.exchange(nullptr, std::memory_order_acquire);
    if (mailbox_head != nullptr) {
        MemoryPoolV2::GetInstance().RecordMailboxDrain();
        BlockHeader* header = static_cast<BlockHeader*>(mailbox_head);
        bin.private_list = header->next;
        tc->alloc_count++;
        tc->ref_count.fetch_add(1, std::memory_order_relaxed);
        return reinterpret_cast<char*>(header) + sizeof(BlockHeader);
    }

    RefillFromSlab(tc, class_idx);
    
    if (bin.private_list) {
        BlockHeader* header = static_cast<BlockHeader*>(bin.private_list);
        bin.private_list = header->next;
        tc->alloc_count++;
        tc->ref_count.fetch_add(1, std::memory_order_relaxed);
        return reinterpret_cast<char*>(header) + sizeof(BlockHeader);
    }
    
    return nullptr;
}

void DeallocateSmall(void* ptr) {
    if (!ptr) return;
    BlockHeader* header = reinterpret_cast<BlockHeader*>(static_cast<char*>(ptr) - sizeof(BlockHeader));
    ThreadCache* tc = GetOrCreateThreadCache();
    size_t class_idx = header->size_class;

    if (header->owner == tc) {
        // Return to private list
        header->next = tc->bins[class_idx].private_list;
        tc->bins[class_idx].private_list = header;
        tc->free_count++;
        
        // Decrement ref count
        tc->ref_count.fetch_sub(1, std::memory_order_relaxed);
    }
    else if (header->owner->dead.load(std::memory_order_acquire)) {
        // Owner thread is dead, decrement ref count
        // The ThreadCache will be cleaned up in MemoryPoolV2 destructor
        header->owner->ref_count.fetch_sub(1, std::memory_order_acq_rel);
        
        // Reclaim block to current thread
        header->owner = tc;
        header->next = tc->bins[class_idx].private_list;
        tc->bins[class_idx].private_list = header;
        tc->free_count++;
    }
    else {
        MemoryPoolV2::GetInstance().RecordCrossThreadFree();
        
        // Decrement owner ref count
        header->owner->ref_count.fetch_sub(1, std::memory_order_relaxed);
        
        std::atomic<void*>& mailbox = header->owner->bins[class_idx].mailbox;
        void* old_head = mailbox.load(std::memory_order_relaxed);
        do {
            header->next = old_head;
        } while (!mailbox.compare_exchange_weak(old_head, header, std::memory_order_release, std::memory_order_relaxed));
    }
}

void* AllocateMedium(size_t size) {
    ThreadCache* tc = GetOrCreateThreadCache();
    for (size_t i = 0; i < tc->medium_count; i++) {
        if (tc->medium_cache[i].size >= size &&
            tc->medium_cache[i].size < size * 2) {
                void* cached_header = tc->medium_cache[i].ptr;
                tc->medium_cache[i] = tc->medium_cache[tc->medium_count - 1];
                tc->medium_count--;
                tc->alloc_count++;
                return reinterpret_cast<char*>(cached_header) + sizeof(BlockHeader);
            }
    }

    size_t total_size = size + sizeof(BlockHeader);
    void* mem = malloc(total_size);
    if (!mem) return nullptr;
    
    BlockHeader* header = static_cast<BlockHeader*>(mem);
    header->size_class = MEDIUM_OBJECT;  // 0xFFFE
    header->flags = 0;
    header->owner = tc;
    header->next = nullptr;
    header->actual_size = size;
    
#ifdef MEMORY_POOL_V2_DEBUG
    header->magic = BlockHeader::MAGIC_VALUE;
#endif
    
    tc->alloc_count++;
    return reinterpret_cast<char*>(header) + sizeof(BlockHeader);
}

void DeallocateMedium(void* ptr) {
    if (!ptr) return;
    BlockHeader* header = reinterpret_cast<BlockHeader*>(static_cast<char*>(ptr) - sizeof(BlockHeader));
    ThreadCache* tc = GetOrCreateThreadCache();
    if (header->owner != tc) {
        free(header);
        return;
    }

    if (tc->medium_count < MEDIUM_CACHE_SIZE) {
        tc->medium_cache[tc->medium_count].ptr = header;
        tc->medium_cache[tc->medium_count].size = header->actual_size;
        tc->medium_count++;
        tc->free_count++;
        return;
    }
    else {
        free(header);
        tc->free_count++;
    }
}

void* AllocateLarge(size_t size) {
    size_t total_size = size + sizeof(BlockHeader);
    void* mem = malloc(total_size);
    if (!mem) {
        return nullptr;
    }
    BlockHeader* header = static_cast<BlockHeader*>(mem);
    header->size_class = LARGE_OBJECT;
    header->flags = LARGE_DIRECT;
    header->owner = nullptr;
    header->next = nullptr;

#ifdef MEMORY_POOL_V2_DEBUG
    header->magic = BlockHeader::MAGIC_VALUE;
#endif
    
    return reinterpret_cast<char*>(header) + sizeof(BlockHeader);
}

void DeallocateLarge(void* ptr) {
    if (!ptr) return;
    BlockHeader* header = reinterpret_cast<BlockHeader*>(static_cast<char*>(ptr) - sizeof(BlockHeader));
    
    free(header);
}

MemoryPoolV2::MemoryPoolV2() {
    stats_.total_allocated = 0;
    stats_.total_deallocated = 0;
    stats_.cross_thread_frees = 0;
    stats_.slab_refills = 0;
    stats_.mailbox_drains = 0;
}

MemoryPoolV2::~MemoryPoolV2() {
    // Note: As process-level singleton, calling free() in destructor may cause CRT heap corruption
    // Because destruction order is uncertain, CRT heap manager may be partially shut down
    // The safest approach is to let the OS reclaim all memory on process exit
    // Only do minimal cleanup here to avoid triggering CRT issues
    
    // Do not call free(), let OS reclaim memory
    // thread_caches_ and dead_caches_ will be automatically destructed
}

MemoryPoolV2& MemoryPoolV2::GetInstance() {
    static MemoryPoolV2 instance;
    return instance;
}

void MemoryPoolV2::RegisterThread(ThreadCache* tc) {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    thread_caches_[std::this_thread::get_id()] = tc;
}

void MemoryPoolV2::UnregisterThread(ThreadCache* tc) {
    if (!tc) return;
    std::lock_guard<std::mutex> lock(registry_mutex_);
    thread_caches_.erase(std::this_thread::get_id());
    dead_caches_.push_back(tc);
}

void MemoryPoolV2::UnregisterThreadOnly(ThreadCache* tc) {
    if (!tc) return;
    std::lock_guard<std::mutex> lock(registry_mutex_);
    thread_caches_.erase(std::this_thread::get_id());
}

void MemoryPoolV2::RemoveFromDeadCaches(ThreadCache* tc) {
    if (!tc) return;
    std::lock_guard<std::mutex> lock(registry_mutex_);
    auto it = std::find(dead_caches_.begin(), dead_caches_.end(), tc);
    if (it != dead_caches_.end()) {
        dead_caches_.erase(it);
    }
}

void* MemoryPoolV2::Allocate(size_t size) {
    void* ptr = nullptr;
    if (size <= MAX_SMALL_SIZE) {
        ptr = AllocateSmall(size);
    }
    else if (size <= MAX_MEDIUM_SIZE) {
        ptr = AllocateMedium(size);
    }
    else {
        ptr = AllocateLarge(size);
    }
    if (ptr) RecordAlloc(size);
    return ptr;
}

void MemoryPoolV2::Deallocate(void* ptr, size_t size) {
    if (!ptr) return;
    BlockHeader* header = reinterpret_cast<BlockHeader*>(static_cast<char*>(ptr) - sizeof(BlockHeader));
    if (header->size_class < NUM_SIZE_CLASSES) {
        DeallocateSmall(ptr);
    }
    else if (header->size_class == MEDIUM_OBJECT) {
        DeallocateMedium(ptr);
    }
    else {
        DeallocateLarge(ptr);
    }
    RecordDealloc(size);
}

void MemoryPoolV2::RecordAlloc(size_t bytes) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.total_allocated += bytes;
}

void MemoryPoolV2::RecordDealloc(size_t bytes) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.total_deallocated += bytes;
}

void MemoryPoolV2::RecordCrossThreadFree() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.cross_thread_frees++;
}

void MemoryPoolV2::RecordSlabRefill() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.slab_refills++;
}

void MemoryPoolV2::RecordMailboxDrain() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.mailbox_drains++;
}

MemoryPoolV2::PoolStat MemoryPoolV2::GetStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void MemoryPoolV2::PrintStats() const {
    PoolStat stats = GetStats();
    
    std::cout << "=== MemoryPoolV2 Statistics ===" << std::endl;
    std::cout << "Total Allocated:     " << stats.total_allocated << " bytes" << std::endl;
    std::cout << "Total Deallocated:   " << stats.total_deallocated << " bytes" << std::endl;
    std::cout << "Cross-thread Frees:  " << stats.cross_thread_frees << std::endl;
    std::cout << "Slab Refills:        " << stats.slab_refills << std::endl;
    std::cout << "Mailbox Drains:      " << stats.mailbox_drains << std::endl;
    std::cout << "===============================" << std::endl;
}

} // namespace SAK
