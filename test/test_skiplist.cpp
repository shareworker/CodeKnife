#include "skiplist.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>

TEST(ConcurrentSkipList, InsertFindAndAliasWorkForUniqueKeys) {
    SAK::ConcurrentSkipList<int, int> list;
    SAK::SkipList<int, int> alias;

    auto inserted = list.insert({3, 30});
    auto alias_inserted = alias.insert({1, 10});

    ASSERT_TRUE(inserted.second);
    ASSERT_TRUE(alias_inserted.second);
    ASSERT_NE(inserted.first, list.end());
    ASSERT_NE(alias_inserted.first, alias.end());
    EXPECT_EQ(inserted.first->first, 3);
    EXPECT_EQ(inserted.first->second, 30);
    EXPECT_EQ(alias.find(1)->second, 10);
    EXPECT_TRUE(list.contains(3));
    EXPECT_EQ(list.count(3), 1u);
    EXPECT_EQ(list.find(3)->second, 30);
}

TEST(ConcurrentSkipList, DuplicateInsertDoesNotOverwriteExistingValue) {
    SAK::ConcurrentSkipList<int, int> list;

    ASSERT_TRUE(list.insert({5, 50}).second);
    auto duplicate = list.insert({5, 500});

    EXPECT_FALSE(duplicate.second);
    ASSERT_NE(duplicate.first, list.end());
    EXPECT_EQ(duplicate.first->second, 50);
    EXPECT_EQ(list.find(5)->second, 50);
}

TEST(ConcurrentSkipList, BoundsAndIterationFollowAscendingKeyOrder) {
    SAK::ConcurrentSkipList<int, int> list;
    ASSERT_TRUE(list.insert({10, 100}).second);
    ASSERT_TRUE(list.insert({30, 300}).second);
    ASSERT_TRUE(list.insert({20, 200}).second);

    auto lower = list.lower_bound(15);
    auto upper = list.upper_bound(20);
    auto range = list.equal_range(20);

    ASSERT_NE(lower, list.end());
    EXPECT_EQ(lower->first, 20);
    ASSERT_NE(upper, list.end());
    EXPECT_EQ(upper->first, 30);
    ASSERT_NE(range.first, list.end());
    EXPECT_EQ(range.first->first, 20);
    ASSERT_NE(range.second, list.end());
    EXPECT_EQ(range.second->first, 30);

    std::vector<int> keys;
    for (auto it = list.begin(); it != list.end(); ++it) {
        keys.push_back(it->first);
    }

    EXPECT_EQ(keys, (std::vector<int>{10, 20, 30}));
}

TEST(ConcurrentSkipList, UnsafeEraseAndSizeQueriesReflectContainerState) {
    SAK::ConcurrentSkipList<int, int> list;
    EXPECT_TRUE(list.empty());
    EXPECT_EQ(list.size(), 0u);

    ASSERT_TRUE(list.insert({7, 70}).second);
    ASSERT_TRUE(list.insert({9, 90}).second);
    EXPECT_FALSE(list.empty());
    EXPECT_EQ(list.size(), 2u);

    EXPECT_EQ(list.unsafe_erase(7), 1u);
    EXPECT_EQ(list.unsafe_erase(7), 0u);
    EXPECT_FALSE(list.contains(7));
    EXPECT_EQ(list.size(), 1u);

    list.unsafe_clear();
    EXPECT_TRUE(list.empty());
    EXPECT_EQ(list.size(), 0u);
}

TEST(ConcurrentSkipList, ConcurrentInsertAndFindSmokeTest) {
    SAK::ConcurrentSkipList<int, int> list;
    constexpr int kThreads = 4;
    constexpr int kItemsPerThread = 64;
    std::vector<std::thread> threads;
    std::atomic<int> inserted_count{0};

    for (int thread_index = 0; thread_index < kThreads; ++thread_index) {
        threads.emplace_back([&list, &inserted_count, thread_index]() {
            const int base = thread_index * kItemsPerThread;
            for (int i = 0; i < kItemsPerThread; ++i) {
                auto result = list.insert({base + i, base + i});
                if (result.second) {
                    inserted_count.fetch_add(1, std::memory_order_relaxed);
                }
                auto found = list.find(base + i);
                if (found != list.end()) {
                    EXPECT_EQ(found->first, base + i);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(inserted_count.load(std::memory_order_relaxed), kThreads * kItemsPerThread);
    EXPECT_EQ(list.size(), static_cast<std::size_t>(kThreads * kItemsPerThread));
    EXPECT_TRUE(list.contains(0));
    EXPECT_TRUE(list.contains(kThreads * kItemsPerThread - 1));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
