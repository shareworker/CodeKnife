# Thread-Local Memory Pool Design

**Date**: 2026-03-15  
**Author**: CodeKnife Team  
**Status**: Design Approved

## Overview

This document specifies the design for refactoring CodeKnife's memory pool to use a thread-local dual-layer architecture inspired by oneTBB's scalable allocator. The new design aims to achieve extreme hot-path performance while supporting cross-thread deallocation scenarios common in signal-slot and event-driven systems.

## Goals

- **Extreme hot-path performance**: Thread-local allocation/deallocation with zero locks
- **Cross-thread deallocation support**: Handle "thread A allocates, thread B frees" scenarios
- **Hybrid large object strategy**: Pool small objects, cache medium objects, pass-through large objects
- **System malloc backend**: Use system `malloc`/`free` as the underlying allocator (no custom memory management)

## Non-Goals

- Custom page-level memory management
- NUMA-aware allocation
- Real-time guarantees

## Architecture

### High-Level Design

```
┌──────────────────────────────────────────────────────────┐
│                   User Code                              │
│   SAK::memory::Allocate(size) / Deallocate(ptr, size)   │
└────────────────────┬─────────────────────────────────────┘
                     │
                     ▼
┌──────────────────────────────────────────────────────────┐
│              MemoryPool (Singleton Router)               │
│  - Route: small/medium/large object dispatch             │
│  - Thread lifecycle management                           │
└────────────────────┬─────────────────────────────────────┘
                     │
        ┌────────────┼────────────┐
        ▼            ▼            ▼
   ┌─────────┐  ┌─────────┐  ┌─────────┐
   │  Small  │  │ Medium  │  │  Large  │
   │ ≤2KB    │  │4KB-64KB │  │ >64KB   │
   └────┬────┘  └────┬────┘  └────┬────┘
        │            │            │
        ▼            ▼            └──▶ malloc/free
┌──────────────────────────────────────┐
│   ThreadCache (thread_local)         │
│                                      │
│  SizeClass[0..N]:                    │
│    - private_list (lock-free LIFO)   │
│    - mailbox (atomic LIFO)           │
│                                      │
│  MediumCache:                        │
│    - FIFO cache (max 8 blocks)       │
└──────────────────┬───────────────────┘
                   │
                   ▼ (refill when empty)
            ┌──────────────┐
            │ System Malloc│
            │  (Slab alloc)│
            └──────────────┘
```

### Size Class Configuration

| Size Class | Block Size | Use Case |
|-----------|-----------|----------|
| 0 | 8 B | Tiny objects |
| 1 | 16 B | Small strings, flags |
| 2 | 32 B | Small structs |
| 3 | 64 B | Cache-line sized |
| 4 | 128 B | Medium structs |
| 5 | 256 B | Small buffers |
| 6 | 512 B | Medium buffers |
| 7 | 1024 B | 1KB buffers |
| 8 | 2048 B | 2KB buffers |
| Medium | 4KB - 64KB | Large buffers, cached |
| Large | > 64KB | Direct malloc/free |

### Slab Configuration

- **Slab size**: 32KB (default)
- **Allocation strategy**: Batch allocate from system malloc, split into blocks
- **Refill trigger**: When both `private_list` and `mailbox` are empty

## Data Structures

### BlockHeader

Every allocated block has a header prepended before the user pointer:

```cpp
struct BlockHeader {
    uint16_t size_class;      // Size class index (0..N) or special marker
    uint16_t flags;           // Reserved (e.g., LARGE_DIRECT flag)
    ThreadCache* owner;       // Pointer to owning thread's cache
    void* next;               // For freelist/mailbox linkage
};

// Special size_class values
constexpr uint16_t MEDIUM_OBJECT = 0xFFFE;
constexpr uint16_t LARGE_OBJECT = 0xFFFF;

// User pointer = (char*)header + sizeof(BlockHeader)
```

### SizeClassBin

Each size class maintains two lists:

```cpp
struct alignas(64) SizeClassBin {  // Cache-line aligned to avoid false sharing
    void* private_list;              // Private freelist (owner thread only)
    std::atomic<void*> mailbox;      // Cross-thread free mailbox (atomic stack)
    size_t block_size;               // Block size for this class
    char padding[...];               // Pad to 64 bytes
};
```

### ThreadCache

Per-thread cache structure:

```cpp
struct ThreadCache {
    // Small object bins
    SizeClassBin bins[NUM_SIZE_CLASSES];
    
    // Slab metadata (for cleanup)
    Slab* slabs[NUM_SIZE_CLASSES];
    
    // Medium object cache (simple FIFO)
    struct MediumBlock {
        void* ptr;
        size_t size;
    };
    std::array<MediumBlock, 8> medium_cache;
    size_t medium_count = 0;
    
    // Reference counting for safe cross-thread deallocation
    std::atomic<size_t> ref_count{1};
    
    // Statistics
    size_t alloc_count = 0;
    size_t free_count = 0;
};

// Thread-local storage
thread_local ThreadCache* g_thread_cache = nullptr;
```

### Slab Metadata

Track slabs for batch cleanup:

```cpp
struct Slab {
    void* memory;                    // Raw malloc pointer
    size_t block_size;               // Block size for this slab
    size_t num_blocks;               // Total blocks in slab
    std::atomic<size_t> free_count;  // Current free blocks (for future optimization)
    Slab* next;                      // Linked list
};
```

## Algorithms

### Small Object Allocation (≤ 2KB)

**Hot path** (private_list hit):
1. Get thread-local cache (lazy init if needed)
2. Find size class via lookup table or bit manipulation
3. Pop from `private_list` (lock-free)
4. Return user pointer

**Cold path 1** (mailbox drain):
1. Atomic exchange `mailbox` to get all cross-thread freed blocks
2. Move first block to return, rest to `private_list`
3. Return user pointer

**Cold path 2** (slab refill):
1. `malloc(SLAB_SIZE)` from system
2. Create Slab metadata
3. Split into blocks, initialize headers with `owner = current_thread`
4. Push all blocks to `private_list`
5. Retry allocation

### Small Object Deallocation (≤ 2KB)

**Same-thread free** (hot path):
1. Extract header from user pointer
2. Check `header->owner == current_thread_cache`
3. Push to `private_list` (lock-free)

**Cross-thread free** (atomic path):
1. Extract header
2. CAS push to `header->owner->bins[size_class].mailbox`
3. Retry on CAS failure

### Medium Object (4KB - 64KB)

**Allocation**:
1. Search cache for suitable block (linear search, max 8 entries)
2. If found, remove and return
3. If not found, `malloc(size + header)`, initialize header
4. Return user pointer

**Deallocation**:
1. If same-thread and cache not full: add to cache
2. Otherwise: `free(header)`

### Large Object (> 64KB)

**Allocation**:
1. `malloc(size + header)`
2. Set `header->size_class = LARGE_OBJECT`
3. Return user pointer

**Deallocation**:
1. `free(header)` directly

## Thread Lifecycle Management

### Initialization

- **Lazy initialization**: ThreadCache created on first allocation in a thread
- **Registration**: Register with global MemoryPool singleton for cleanup tracking
- **atexit hook**: Register thread exit callback

### Cleanup on Thread Exit

1. Release all blocks in `private_list` (via Slab batch free)
2. Drain and release all blocks in `mailbox`
3. Free all medium cache entries
4. Free all Slab metadata and memory
5. Decrement `ref_count`
6. If `ref_count == 0`, delete ThreadCache immediately
7. Otherwise, defer deletion until last block is freed (cross-thread safety)

### Reference Counting

To handle cross-thread deallocation to exited threads:

- **Initial ref_count**: 1 (owned by thread)
- **Increment**: On each allocation (block holds reference)
- **Decrement**: On each deallocation and thread exit
- **Destruction**: When ref_count reaches 0

## Error Handling

| Scenario | Behavior |
|----------|----------|
| System malloc fails | Return `nullptr` to caller |
| Deallocate `nullptr` | No-op, return immediately |
| Deallocate invalid pointer | Undefined behavior (same as standard `free`) |
| Deallocate with wrong size | Debug: assert, Release: best-effort |
| Cross-thread free to exited thread | Safe via reference counting |

## Performance Optimizations

### Compiler Optimizations

- Mark hot-path functions as `inline` or `__attribute__((always_inline))`
- Use `__builtin_expect` for branch prediction hints
- Align SizeClassBin to cache line (64 bytes) to avoid false sharing

### Algorithm Optimizations

**Size class lookup**:
- Option 1: Lookup table for sizes ≤ 2KB
- Option 2: Bit manipulation for power-of-2 sizes: `clz(size - 1)`

**Mailbox drain**:
- Reverse linked list to maintain LIFO order
- Batch process to amortize atomic operation cost

### Memory Optimizations

**Dynamic slab sizing**:
- Small size classes (≤32B): 16KB slabs
- Medium size classes (64-256B): 32KB slabs
- Large size classes (512-2048B): 64KB slabs

**Medium cache eviction**:
- Use LRU instead of FIFO for better hit rate
- Track `last_access` timestamp per entry

## Testing Strategy

### Unit Tests

1. **Basic functionality**: Allocate/deallocate various sizes
2. **Size class coverage**: Test all size classes
3. **Mass allocation**: Trigger slab refill (10K+ allocations)
4. **Cross-thread free**: Allocate in thread A, free in thread B
5. **Multi-producer/multi-consumer**: 8+ threads, 1000+ ops each
6. **Edge cases**: Zero size, nullptr, very large objects

### Stress Tests

- **Long-running**: 60+ seconds, 16 threads, random sizes (8B-4KB)
- **Memory pressure**: Allocate until near system limit, verify graceful degradation
- **Thread churn**: Rapidly create/destroy threads with allocations

### Performance Benchmarks

- **Single-thread alloc/free**: Target < 10ns (hot path)
- **Multi-thread alloc/free**: 8 threads, measure throughput
- **Comparison**: vs system malloc, vs old MemoryPool
- **Expected speedup**: 5-10x for small objects (< 256B)

## Monitoring & Debugging

### Statistics Collection

```cpp
struct PoolStats {
    size_t total_allocations;
    size_t total_deallocations;
    size_t cross_thread_frees;
    size_t slab_refills;
    size_t mailbox_drains;
    
    struct SizeClassStats {
        size_t allocations;
        size_t private_hits;
        size_t mailbox_hits;
        size_t slab_refills;
    } size_class_stats[NUM_SIZE_CLASSES];
};
```

### Debug Mode

- **Magic number**: Add `uint32_t magic = 0xDEADBEEF` to BlockHeader
- **Validation**: Check magic on deallocation
- **Assertions**: Verify invariants (e.g., ref_count > 0)

## Implementation Plan

### Phase 1: Core Infrastructure
1. Define data structures (BlockHeader, SizeClassBin, ThreadCache, Slab)
2. Implement ThreadCache lazy initialization
3. Implement size class lookup

### Phase 2: Small Object Allocator
1. Implement private_list allocation (hot path)
2. Implement slab refill
3. Implement same-thread deallocation
4. Implement cross-thread deallocation (mailbox)
5. Implement mailbox drain

### Phase 3: Medium & Large Objects
1. Implement medium object cache
2. Implement large object pass-through

### Phase 4: Thread Lifecycle
1. Implement thread exit cleanup
2. Implement reference counting
3. Implement slab batch free

### Phase 5: Testing & Optimization
1. Write unit tests
2. Write stress tests
3. Write benchmarks
4. Profile and optimize hot paths
5. Tune slab sizes and cache parameters

## Migration Strategy

### Compatibility

- **New API**: `SAK::memory::Allocate()` / `Deallocate()`
- **Old API**: Keep existing `MemoryPool::GetInstance()` as wrapper (deprecated)
- **Gradual migration**: Replace call sites incrementally

### Rollout

1. Implement new pool as `MemoryPoolV2`
2. Add feature flag to switch between old/new
3. Test in development builds
4. Enable in production after validation
5. Remove old implementation after full migration

## Open Questions

- **Slab fragmentation**: Should we implement slab compaction/defragmentation?
  - **Decision**: Defer to future optimization, monitor fragmentation in production
- **NUMA awareness**: Should we support NUMA-local allocation?
  - **Decision**: No, out of scope for initial implementation
- **Custom allocator interface**: Should we provide `std::allocator` compatible interface?
  - **Decision**: Yes, add in Phase 6 (post-MVP)

## References

- oneTBB scalable allocator: `src/tbbmalloc/frontend.cpp`
- jemalloc design: http://jemalloc.net/
- TCMalloc design: https://google.github.io/tcmalloc/design.html
