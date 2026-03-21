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
    EXPECT_EQ(FindSizeClass(2049), 9); // 超出范围，返回 NUM_SIZE_CLASSES
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
    
    // 初始为空
    EXPECT_EQ(tc->bins[class_idx].private_list, nullptr);
    
    // 触发 refill
    RefillFromSlab(tc, class_idx);
    
    // 应该有块了
    EXPECT_NE(tc->bins[class_idx].private_list, nullptr);
    EXPECT_NE(tc->slabs[class_idx], nullptr);
    
    // 验证块数量
    size_t block_size = SIZE_CLASSES[class_idx];
    size_t total_size = block_size + sizeof(BlockHeader);
    size_t expected_blocks = SLAB_SIZE / total_size;
    
    // 数一下链表长度
    size_t count = 0;
    void* p = tc->bins[class_idx].private_list;
    while (p && count < expected_blocks + 10) {  // +10 防止死循环
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
    
    // 验证 header
    BlockHeader* header = reinterpret_cast<BlockHeader*>(
        static_cast<char*>(ptr) - sizeof(BlockHeader)
    );
    EXPECT_EQ(header->size_class, 3);  // 64-byte class
    EXPECT_NE(header->owner, nullptr);
    
    // 写入数据验证可用
    memset(ptr, 0xAB, 64);
    
    // 暂时不释放，等 Task 10 实现
}

TEST(MemoryPoolV2, SameThreadDeallocation) {
    void* ptr = MemoryPoolV2::GetInstance().Allocate(64);
    ASSERT_NE(ptr, nullptr);
    
    MemoryPoolV2::GetInstance().Deallocate(ptr, 64);
    
    // 再分配应该复用同一个块（LIFO）
    void* ptr2 = MemoryPoolV2::GetInstance().Allocate(64);
    EXPECT_EQ(ptr, ptr2);
}

TEST(MemoryPoolV2, CrossThreadDeallocation) {
    // 先分配并释放多个块，耗尽 private_list
    ThreadCache* tc = GetOrCreateThreadCache();
    size_t class_idx = 3;  // 64-byte
    
    // 分配所有块直到 private_list 空
    std::vector<void*> ptrs;
    for (int i = 0; i < 1000; ++i) {
        void* p = MemoryPoolV2::GetInstance().Allocate(64);
        if (!p) break;
        ptrs.push_back(p);
        if (tc->bins[class_idx].private_list == nullptr) {
            break;
        }
    }
    
    // 确保 private_list 为空
    ASSERT_EQ(tc->bins[class_idx].private_list, nullptr);
    
    // 在另一个线程释放最后一个块
    void* test_ptr = ptrs.back();
    std::thread t([test_ptr]() {
        MemoryPoolV2::GetInstance().Deallocate(test_ptr, 64);
    });
    t.join();
    
    // 主线程再分配，应该从 mailbox 拿到
    void* ptr2 = MemoryPoolV2::GetInstance().Allocate(64);
    EXPECT_EQ(test_ptr, ptr2);
    
    // 清理
    for (size_t i = 0; i < ptrs.size() - 1; ++i) {
        MemoryPoolV2::GetInstance().Deallocate(ptrs[i], 64);
    }
}

TEST(MemoryPoolV2, LargeObjectAllocation) {
    // 分配一个 128KB 的大对象
    size_t large_size = 128 * 1024;
    void* ptr = MemoryPoolV2::GetInstance().Allocate(large_size);
    ASSERT_NE(ptr, nullptr);
    
    // 验证 header
    BlockHeader* header = reinterpret_cast<BlockHeader*>(
        static_cast<char*>(ptr) - sizeof(BlockHeader)
    );
    EXPECT_EQ(header->size_class, LARGE_OBJECT);
    EXPECT_EQ(header->flags, LARGE_DIRECT);
    EXPECT_EQ(header->owner, nullptr);
    
    // 写入数据验证可用
    memset(ptr, 0xCD, large_size);
    
    // 释放
    MemoryPoolV2::GetInstance().Deallocate(ptr, large_size);
}

TEST(MemoryPoolV2, MediumObjectAllocation) {
    // 分配一个 8KB 的中等对象
    size_t medium_size = 8 * 1024;
    void* ptr = MemoryPoolV2::GetInstance().Allocate(medium_size);
    ASSERT_NE(ptr, nullptr);
    
    // 验证 header
    BlockHeader* header = reinterpret_cast<BlockHeader*>(
        static_cast<char*>(ptr) - sizeof(BlockHeader)
    );
    EXPECT_EQ(header->size_class, MEDIUM_OBJECT);
    EXPECT_EQ(header->actual_size, medium_size);
    EXPECT_NE(header->owner, nullptr);
    
    // 写入数据
    memset(ptr, 0xEF, medium_size);
    
    // 释放并再次分配，应该从缓存复用
    MemoryPoolV2::GetInstance().Deallocate(ptr, medium_size);
    void* ptr2 = MemoryPoolV2::GetInstance().Allocate(medium_size);
    EXPECT_EQ(ptr, ptr2);  // 应该是同一个块
    
    // 清理
    MemoryPoolV2::GetInstance().Deallocate(ptr2, medium_size);
}

TEST(MemoryPoolV2, ThreadCleanup) {
    std::thread t([]() {
        // 分配各种类型的对象
        void* small = MemoryPoolV2::GetInstance().Allocate(64);
        void* medium = MemoryPoolV2::GetInstance().Allocate(8192);
        void* large = MemoryPoolV2::GetInstance().Allocate(128 * 1024);
        
        ASSERT_NE(small, nullptr);
        ASSERT_NE(medium, nullptr);
        ASSERT_NE(large, nullptr);
        
        // 释放中等对象，让它进入缓存
        MemoryPoolV2::GetInstance().Deallocate(medium, 8192);
        
        // 线程退出时会自动清理 Slab 和缓存
    });
    t.join();
    
    // 主要验证没有内存泄漏和崩溃
}

TEST(MemoryPoolV2, Statistics) {
    // 获取初始统计
    auto stats_before = MemoryPoolV2::GetInstance().GetStats();
    
    // 分配一些对象
    void* ptr1 = MemoryPoolV2::GetInstance().Allocate(64);
    void* ptr2 = MemoryPoolV2::GetInstance().Allocate(128);
    
    // 打印统计（手动验证输出）
    MemoryPoolV2::GetInstance().PrintStats();
    
    // 清理
    MemoryPoolV2::GetInstance().Deallocate(ptr1, 64);
    MemoryPoolV2::GetInstance().Deallocate(ptr2, 128);
}

TEST(MemoryPoolV2, MultiThreadStress) {
    const int num_threads = 4;
    const int iterations = 1000;
    
    std::vector<std::thread> threads;
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([iterations]() {
            std::vector<void*> ptrs;
            
            // 分配各种大小的对象
            for (int i = 0; i < iterations; ++i) {
                size_t size = (i % 3 == 0) ? 64 :           // 小对象
                              (i % 3 == 1) ? 8192 :         // 中等对象
                                             128 * 1024;    // 大对象
                
                void* ptr = MemoryPoolV2::GetInstance().Allocate(size);
                ASSERT_NE(ptr, nullptr);
                
                // 写入数据验证
                memset(ptr, 0xFF, size);
                
                ptrs.push_back(ptr);
            }
            
            // 释放一半对象
            for (size_t i = 0; i < ptrs.size() / 2; ++i) {
                size_t size = (i % 3 == 0) ? 64 :
                              (i % 3 == 1) ? 8192 :
                                             128 * 1024;
                MemoryPoolV2::GetInstance().Deallocate(ptrs[i], size);
            }
            
            // 再分配一些
            for (int i = 0; i < iterations / 2; ++i) {
                size_t size = 128;
                void* ptr = MemoryPoolV2::GetInstance().Allocate(size);
                ASSERT_NE(ptr, nullptr);
                ptrs.push_back(ptr);
            }
            
            // 清理所有
            for (size_t i = ptrs.size() / 2; i < ptrs.size(); ++i) {
                size_t size = (i < iterations) ? 
                              ((i % 3 == 0) ? 64 : (i % 3 == 1) ? 8192 : 128 * 1024) :
                              128;
                MemoryPoolV2::GetInstance().Deallocate(ptrs[i], size);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // 打印最终统计
    std::cout << "\n=== After Multi-thread Stress Test ===" << std::endl;
    MemoryPoolV2::GetInstance().PrintStats();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
