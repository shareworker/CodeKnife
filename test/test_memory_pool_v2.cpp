#include "util/memory_pool_v2.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace SAK;

TEST(MemoryPoolV2, FindSizeClass) {
    EXPECT_EQ(FindSizeClass(1), 0);    // 1 -> 8
    EXPECT_EQ(FindSizeClass(8), 0);    // 8 -> 8
    EXPECT_EQ(FindSizeClass(9), 1);    // 9 -> 16
    EXPECT_EQ(FindSizeClass(16), 1);   // 16 -> 16
    EXPECT_EQ(FindSizeClass(17), 2);   // 17 -> 32
    EXPECT_EQ(FindSizeClass(64), 3);   // 64 -> 64
    EXPECT_EQ(FindSizeClass(128), 4);  // 128 -> 128
    EXPECT_EQ(FindSizeClass(2048), 8); // 2048 -> 2048
    EXPECT_EQ(FindSizeClass(2049), 9); // Out of range, return NUM_SIZE_CLASSES
}

TEST(MemoryPoolV2, ThreadCacheCreation) {
    ThreadCache* tc1 = GetOrCreateThreadCache();
    ASSERT_NE(tc1, nullptr);
    EXPECT_EQ(tc1->ref_count.load(), 1);
    EXPECT_EQ(tc1->medium_count, 0);

    for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
        EXPECT_EQ(tc1->bins[i].block_size, SIZE_CLASSES[i]);
        EXPECT_EQ(tc1->bins[i].private_list, nullptr);
        EXPECT_EQ(tc1->bins[i].mailbox.load(), nullptr);
        EXPECT_EQ(tc1->slabs[i], nullptr);
    }

    ThreadCache* tc2 = GetOrCreateThreadCache();
    EXPECT_EQ(tc1, tc2);
}

TEST(MemoryPoolV2, SlabRefill) {
    ThreadCache* tc = GetOrCreateThreadCache();
    size_t class_idx = 0;  // 8-byte blocks
    
    // Initially empty
    EXPECT_EQ(tc->bins[class_idx].private_list, nullptr);
    
    // Trigger refill
    RefillFromSlab(tc, class_idx);
    
    // Should have blocks now
    EXPECT_NE(tc->bins[class_idx].private_list, nullptr);
    EXPECT_NE(tc->slabs[class_idx], nullptr);
    
    // Verify block count
    size_t block_size = SIZE_CLASSES[class_idx];
    size_t total_size = block_size + sizeof(BlockHeader);
    size_t expected_blocks = SLAB_SIZE / total_size;
    
    // Count linked list length
    size_t count = 0;
    void* p = tc->bins[class_idx].private_list;
    while (p && count < expected_blocks + 10) {  // +10 to prevent infinite loop
        BlockHeader* h = static_cast<BlockHeader*>(p);
        EXPECT_EQ(h->size_class, class_idx);
        EXPECT_EQ(h->owner, tc);
        p = h->next;
        count++;
    }
    
    EXPECT_EQ(count, expected_blocks);
}

TEST(MemoryPoolV2, SmallObjectAllocation) {
    void* ptr = MemoryPoolV2::GetInstance().Allocate(64);
    ASSERT_NE(ptr, nullptr);
    
    // Verify header
    BlockHeader* header = reinterpret_cast<BlockHeader*>(
        static_cast<char*>(ptr) - sizeof(BlockHeader)
    );
    EXPECT_EQ(header->size_class, 3);  // 64-byte class
    EXPECT_NE(header->owner, nullptr);
    
    // Write data to verify usability
    memset(ptr, 0xAB, 64);
    
    MemoryPoolV2::GetInstance().Deallocate(ptr, 64);
}

TEST(MemoryPoolV2, SameThreadDeallocation) {
    void* ptr = MemoryPoolV2::GetInstance().Allocate(64);
    ASSERT_NE(ptr, nullptr);
    
    MemoryPoolV2::GetInstance().Deallocate(ptr, 64);
    
    // Reallocate should reuse same block (LIFO)
    void* ptr2 = MemoryPoolV2::GetInstance().Allocate(64);
    EXPECT_EQ(ptr, ptr2);
}

TEST(MemoryPoolV2, CrossThreadDeallocation) {
    // Allocate and free multiple blocks to exhaust private_list
    ThreadCache* tc = GetOrCreateThreadCache();
    size_t class_idx = 3;  // 64-byte
    
    // Allocate blocks until private_list is empty
    std::vector<void*> ptrs;
    for (int i = 0; i < 1000; ++i) {
        void* p = MemoryPoolV2::GetInstance().Allocate(64);
        if (!p) break;
        ptrs.push_back(p);
        if (tc->bins[class_idx].private_list == nullptr) {
            break;
        }
    }
    
    // Ensure private_list is empty
    ASSERT_EQ(tc->bins[class_idx].private_list, nullptr);
    
    // Free last block in another thread
    void* test_ptr = ptrs.back();
    std::thread t([test_ptr]() {
        MemoryPoolV2::GetInstance().Deallocate(test_ptr, 64);
    });
    t.join();
    
    // Main thread allocates again, should get from mailbox
    void* ptr2 = MemoryPoolV2::GetInstance().Allocate(64);
    EXPECT_EQ(test_ptr, ptr2);
    
    // Clean up
    for (size_t i = 0; i < ptrs.size() - 1; ++i) {
        MemoryPoolV2::GetInstance().Deallocate(ptrs[i], 64);
    }
}

TEST(MemoryPoolV2, LargeObjectAllocation) {
    // Allocate a 128KB large object
    size_t large_size = 128 * 1024;
    void* ptr = MemoryPoolV2::GetInstance().Allocate(large_size);
    ASSERT_NE(ptr, nullptr);
    
    // Verify header
    BlockHeader* header = reinterpret_cast<BlockHeader*>(
        static_cast<char*>(ptr) - sizeof(BlockHeader)
    );
    EXPECT_EQ(header->size_class, LARGE_OBJECT);
    EXPECT_EQ(header->flags, LARGE_DIRECT);
    EXPECT_EQ(header->owner, nullptr);
    
    // Write data to verify usability
    memset(ptr, 0xCD, large_size);
    
    // Free
    MemoryPoolV2::GetInstance().Deallocate(ptr, large_size);
}

TEST(MemoryPoolV2, MediumObjectAllocation) {
    // Allocate an 8KB medium object
    size_t medium_size = 8 * 1024;
    void* ptr = MemoryPoolV2::GetInstance().Allocate(medium_size);
    ASSERT_NE(ptr, nullptr);
    
    // Verify header
    BlockHeader* header = reinterpret_cast<BlockHeader*>(
        static_cast<char*>(ptr) - sizeof(BlockHeader)
    );
    EXPECT_EQ(header->size_class, MEDIUM_OBJECT);
    EXPECT_EQ(header->actual_size, medium_size);
    EXPECT_NE(header->owner, nullptr);
    
    // Write data
    memset(ptr, 0xEF, medium_size);
    
    // Free and reallocate, should reuse from cache
    MemoryPoolV2::GetInstance().Deallocate(ptr, medium_size);
    void* ptr2 = MemoryPoolV2::GetInstance().Allocate(medium_size);
    EXPECT_EQ(ptr, ptr2);  // Should be same block
    
    // Clean up
    MemoryPoolV2::GetInstance().Deallocate(ptr2, medium_size);
}

TEST(MemoryPoolV2, ThreadCleanup) {
    std::thread t([]() {
        // Allocate various types of objects
        void* small = MemoryPoolV2::GetInstance().Allocate(64);
        void* medium = MemoryPoolV2::GetInstance().Allocate(8192);
        void* large = MemoryPoolV2::GetInstance().Allocate(128 * 1024);
        
        ASSERT_NE(small, nullptr);
        ASSERT_NE(medium, nullptr);
        ASSERT_NE(large, nullptr);
        
        // Free all objects
        MemoryPoolV2::GetInstance().Deallocate(small, 64);
        MemoryPoolV2::GetInstance().Deallocate(medium, 8192);
        MemoryPoolV2::GetInstance().Deallocate(large, 128 * 1024);
    });
    t.join();
    
    // Mainly verify no memory leaks and crashes
}

TEST(MemoryPoolV2, CrossThreadDeallocAfterOwnerExit) {
    // Regression test: cross-thread dealloc after owner thread exits
    void* ptr_from_dead_thread = nullptr;
    
    std::thread t([&ptr_from_dead_thread]() {
        // Allocate but do not free
        ptr_from_dead_thread = MemoryPoolV2::GetInstance().Allocate(64);
        ASSERT_NE(ptr_from_dead_thread, nullptr);
        
        // Write data to verify usability
        memset(ptr_from_dead_thread, 0xDD, 64);
        
        // Thread exits, ThreadCache marked as dead
    });
    t.join();
    
    // Main thread frees block from exited thread
    // This should trigger dead-owner path and cleanup correctly
    MemoryPoolV2::GetInstance().Deallocate(ptr_from_dead_thread, 64);
    
    // Verify no crash or UAF
}

TEST(MemoryPoolV2, Statistics) {
    // Get initial statistics
    auto stats_before = MemoryPoolV2::GetInstance().GetStats();
    
    // Allocate some objects
    void* ptr1 = MemoryPoolV2::GetInstance().Allocate(64);
    void* ptr2 = MemoryPoolV2::GetInstance().Allocate(128);
    
    auto stats_after_alloc = MemoryPoolV2::GetInstance().GetStats();
    
    // Verify allocation stats increased
    EXPECT_EQ(stats_after_alloc.total_allocated - stats_before.total_allocated, 64 + 128);
    
    // Clean up
    MemoryPoolV2::GetInstance().Deallocate(ptr1, 64);
    MemoryPoolV2::GetInstance().Deallocate(ptr2, 128);
    
    auto stats_after_dealloc = MemoryPoolV2::GetInstance().GetStats();
    
    // Verify deallocation stats increased
    EXPECT_EQ(stats_after_dealloc.total_deallocated - stats_before.total_deallocated, 64 + 128);
    
    MemoryPoolV2::GetInstance().PrintStats();
}

TEST(MemoryPoolV2, MultiThreadStress) {
    const int num_threads = 4;
    const int iterations = 1000;
    
    std::vector<std::thread> threads;
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([iterations]() {
            std::vector<void*> ptrs;
            
            // Allocate objects of various sizes
            for (int i = 0; i < iterations; ++i) {
                size_t size = (i % 3 == 0) ? 64 :           // Small object
                              (i % 3 == 1) ? 8192 :         // Medium object
                                             128 * 1024;    // Large object
                
                void* ptr = MemoryPoolV2::GetInstance().Allocate(size);
                ASSERT_NE(ptr, nullptr);
                
                // Write data to verify
                memset(ptr, 0xFF, size);
                
                ptrs.push_back(ptr);
            }
            
            // Use sizes array to precisely track each allocation size
            std::vector<size_t> sizes;
            for (int i = 0; i < iterations; ++i) {
                sizes.push_back((i % 3 == 0) ? 64 : (i % 3 == 1) ? 8192 : 128 * 1024);
            }
            
            // Free first half
            for (size_t i = 0; i < ptrs.size() / 2; ++i) {
                MemoryPoolV2::GetInstance().Deallocate(ptrs[i], sizes[i]);
                ptrs[i] = nullptr;
            }
            
            // Allocate some more
            for (int i = 0; i < iterations / 2; ++i) {
                size_t size = 128;
                void* ptr = MemoryPoolV2::GetInstance().Allocate(size);
                ASSERT_NE(ptr, nullptr);
                ptrs.push_back(ptr);
                sizes.push_back(size);
            }
            
            // Clean up all unfreed
            for (size_t i = 0; i < ptrs.size(); ++i) {
                if (ptrs[i]) {
                    MemoryPoolV2::GetInstance().Deallocate(ptrs[i], sizes[i]);
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Print final statistics
    std::cout << "\n=== After Multi-thread Stress Test ===" << std::endl;
    MemoryPoolV2::GetInstance().PrintStats();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
