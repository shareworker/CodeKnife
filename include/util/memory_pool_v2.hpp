#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <array>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <thread>

constexpr size_t SIZE_CLASSES[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048};
constexpr size_t NUM_SIZE_CLASSES = 9;
constexpr size_t MAX_SMALL_SIZE = 2048;
constexpr size_t MAX_MEDIUM_SIZE = 65536;
constexpr size_t SLAB_SIZE = 32768;
constexpr uint16_t MEDIUM_OBJECT = 0xFFFE;
constexpr uint16_t LARGE_OBJECT = 0xFFFF;
constexpr uint16_t LARGE_DIRECT = 0x0001;
constexpr size_t MEDIUM_CACHE_SIZE = 8;

namespace SAK {

struct ThreadCache;

struct BlockHeader {
    uint16_t size_class;
    uint16_t flags;
    ThreadCache* owner;
    void* next;
    size_t actual_size;

#ifdef MEMORY_POOL_V2_DEBUG
    uint32_t magic;
    static constexpr uint32_t MAGIC_VALUE = 0xDEADBEEF;
#endif
};

struct alignas(64) SizeClassBin {
    void* private_list;
    std::atomic<void*> mailbox;
    size_t block_size;
    char padding[64 - sizeof(void*) - sizeof(std::atomic<void*>) - sizeof(size_t)];
    SizeClassBin(): private_list(nullptr), mailbox(nullptr), block_size(0) {
    }
};

static_assert(sizeof(SizeClassBin) == 64, "SizeClassBin must be exactly 64 bytes");

struct Slab {
    void* memory;
    size_t block_size;
    size_t num_blocks;
    std::atomic<size_t> free_count;
    Slab* next;
    Slab(): memory(nullptr), block_size(0), num_blocks(0), free_count(0), next(nullptr) {
    }
};

struct ThreadCache {
    SizeClassBin bins[NUM_SIZE_CLASSES];
    Slab* slabs[NUM_SIZE_CLASSES];

    struct MediumBlock {
        void* ptr;
        size_t size;

        MediumBlock() : ptr(nullptr), size(0) {}
    };

    std::array<MediumBlock, MEDIUM_CACHE_SIZE> medium_cache;
    size_t medium_count;
    std::atomic<size_t> ref_count;

    std::atomic<bool> dead;
    size_t alloc_count;
    size_t free_count;

    ThreadCache(): medium_count(0), ref_count(1), dead(false), alloc_count(0), free_count(0) {
        for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
            bins[i].block_size = SIZE_CLASSES[i];
            slabs[i] = nullptr;
        }
    }
};

class MemoryPoolV2 {
public:
    static MemoryPoolV2& GetInstance();
    void* Allocate(size_t size);
    void Deallocate(void* ptr, size_t size);

    void RegisterThread(ThreadCache* tc);
    void UnregisterThread(ThreadCache* tc);
    void UnregisterThreadOnly(ThreadCache* tc);
    void RemoveFromDeadCaches(ThreadCache* tc);

    struct PoolStat {
        size_t total_allocated;
        size_t total_deallocated;
        size_t cross_thread_frees;
        size_t slab_refills;
        size_t mailbox_drains;
    };

    PoolStat GetStats() const;
    void PrintStats() const;
    void RecordAlloc(size_t bytes);
    void RecordDealloc(size_t bytes);
    void RecordCrossThreadFree();
    void RecordSlabRefill();
    void RecordMailboxDrain();

    MemoryPoolV2(const MemoryPoolV2&) = delete;
    MemoryPoolV2& operator=(const MemoryPoolV2&) = delete;

private:
    MemoryPoolV2();
    ~MemoryPoolV2();

    mutable std::mutex registry_mutex_;
    std::unordered_map<std::thread::id, ThreadCache*> thread_caches_;
    std::vector<ThreadCache*> dead_caches_;
    mutable std::mutex stats_mutex_;
    PoolStat stats_;
};

ThreadCache* GetOrCreateThreadCache();
 
size_t FindSizeClass(size_t size);

// Internal helper functions
void RefillFromSlab(ThreadCache* tc, size_t class_idx);

void* AllocateSmall(size_t size);
void DeallocateSmall(void* ptr);
void* AllocateMedium(size_t size);
void DeallocateMedium(void* ptr);
void* AllocateLarge(size_t size);
void CleanupDeadCache(ThreadCache* tc);
void DeallocateLarge(void* ptr);

}