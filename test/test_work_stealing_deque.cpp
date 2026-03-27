#include "util/work_stealing_deque.hpp"
#include "util/thread_pool.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <optional>

TEST(WorkStealingDeque, PushAndPopFromBottomUsesLocalLifoOrder) {
    SAK::thread::work_stealing_deque<int> deque;

    deque.push_bottom(1);
    deque.push_bottom(2);
    deque.push_bottom(3);

    auto first = deque.pop_bottom();
    auto second = deque.pop_bottom();
    auto third = deque.pop_bottom();
    auto empty = deque.pop_bottom();

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(third.has_value());
    EXPECT_EQ(*first, 3);
    EXPECT_EQ(*second, 2);
    EXPECT_EQ(*third, 1);
    EXPECT_FALSE(empty.has_value());
    EXPECT_TRUE(deque.empty());
}

TEST(WorkStealingDeque, StealFromTopGetsOldestPublishedElement) {
    SAK::thread::work_stealing_deque<int> deque;

    deque.push_bottom(10);
    deque.push_bottom(20);
    deque.push_bottom(30);

    auto stolen = deque.steal_top();
    auto local = deque.pop_bottom();

    ASSERT_TRUE(stolen.has_value());
    ASSERT_TRUE(local.has_value());
    EXPECT_EQ(*stolen, 10);
    EXPECT_EQ(*local, 30);
}

TEST(WorkStealingDeque, LastElementIsClaimedExactlyOnce) {
    SAK::thread::work_stealing_deque<int> deque;
    deque.push_bottom(42);

    std::optional<int> stolen;
    std::optional<int> local;

    std::thread thief([&]() {
        stolen = deque.steal_top();
    });

    local = deque.pop_bottom();
    thief.join();

    const bool owner_won = local.has_value();
    const bool thief_won = stolen.has_value();

    EXPECT_NE(owner_won, thief_won);
    if (owner_won) {
        EXPECT_EQ(*local, 42);
    }
    if (thief_won) {
        EXPECT_EQ(*stolen, 42);
    }
}

TEST(WorkStealingDeque, CapacityGrowsWithoutLosingElements) {
    SAK::thread::work_stealing_deque<int> deque(4);

    for (int i = 0; i < 64; ++i) {
        deque.push_bottom(i);
    }

    EXPECT_GE(deque.capacity(), 64u);

    std::vector<int> values;
    while (auto value = deque.pop_bottom()) {
        values.push_back(*value);
    }

    ASSERT_EQ(values.size(), 64u);
    for (int i = 0; i < 64; ++i) {
        EXPECT_EQ(values[static_cast<std::size_t>(i)], 63 - i);
    }
}

TEST(ThreadPoolWorkStealing, ZeroThreadConstructionStillExecutesAcceptedTasks) {
    SAK::thread::ThreadPool pool(0);

    auto future = pool.enqueue([]() {
        return 7;
    });

    EXPECT_EQ(pool.get_thread_count(), 1u);
    ASSERT_EQ(future.wait_for(std::chrono::milliseconds(200)), std::future_status::ready);
    EXPECT_EQ(future.get(), 7);
}

TEST(ThreadPoolWorkStealing, PendingCountTracksAcceptedButNotYetAcquiredWork) {
    SAK::thread::ThreadPool pool(1);

    std::promise<void> started;
    std::shared_future<void> release_signal(std::async(std::launch::deferred, [] {}).share());
    std::promise<void> release_promise;
    release_signal = release_promise.get_future().share();

    auto first = pool.enqueue([&]() {
        started.set_value();
        release_signal.wait();
        return 1;
    });

    started.get_future().wait();
    auto second = pool.enqueue([]() {
        return 2;
    });

    EXPECT_EQ(pool.get_task_count(), 1u);

    release_promise.set_value();

    EXPECT_EQ(first.get(), 1);
    EXPECT_EQ(second.get(), 2);
    EXPECT_EQ(pool.get_task_count(), 0u);
}

TEST(ThreadPoolWorkStealing, IdleWorkerCanExecuteTaskQueuedInBlockedWorkersInbox) {
    SAK::thread::ThreadPool pool(3);

    std::promise<void> two_workers_blocked;
    std::promise<void> release_blockers;
    auto release_signal = release_blockers.get_future().share();

    std::atomic<int> blocked_count{0};

    auto blocker1 = pool.enqueue([&]() {
        if (blocked_count.fetch_add(1, std::memory_order_acq_rel) == 1) {
            two_workers_blocked.set_value();
        }
        release_signal.wait();
    });

    auto blocker2 = pool.enqueue([&]() {
        if (blocked_count.fetch_add(1, std::memory_order_acq_rel) == 1) {
            two_workers_blocked.set_value();
        }
        release_signal.wait();
    });

    two_workers_blocked.get_future().wait();

    auto task_in_inbox = pool.enqueue([]() {
        return 42;
    });

    EXPECT_EQ(task_in_inbox.wait_for(std::chrono::milliseconds(100)), std::future_status::ready);
    EXPECT_EQ(task_in_inbox.get(), 42);

    release_blockers.set_value();
    blocker1.get();
    blocker2.get();
}

TEST(ThreadPoolWorkStealing, DestructionDrainsAlreadyAcceptedTasks) {
    std::atomic<int> completed{0};

    {
        SAK::thread::ThreadPool pool(2);
        for (int i = 0; i < 16; ++i) {
            pool.enqueue([&completed]() {
                completed.fetch_add(1, std::memory_order_relaxed);
            });
        }
    }

    EXPECT_EQ(completed.load(std::memory_order_relaxed), 16);
}

TEST(ThreadPoolWorkStealing, IdleWorkerCanStealPublishedWork) {
    bool observed_stolen_execution = false;

    for (int attempt = 0; attempt < 20 && !observed_stolen_execution; ++attempt) {
        SAK::thread::ThreadPool pool(2);
        std::promise<void> release_promise;
        auto release_signal = release_promise.get_future().share();
        std::atomic<bool> blocker_started{false};
        std::atomic<std::thread::id*> blocker_thread_ptr{nullptr};
        std::thread::id blocker_thread_id;
        std::vector<std::future<void>> futures;
        futures.reserve(64);

        for (int task_index = 0; task_index < 64; ++task_index) {
            if (task_index == 62) {
                futures.push_back(pool.enqueue([&]() {
                    blocker_thread_id = std::this_thread::get_id();
                    blocker_thread_ptr.store(&blocker_thread_id, std::memory_order_release);
                    blocker_started.store(true, std::memory_order_release);
                    release_signal.wait();
                }));
            } else if (task_index == 0) {
                futures.push_back(pool.enqueue([&]() {
                    for (int spin = 0; spin < 1000 && !blocker_started.load(std::memory_order_acquire); ++spin) {
                        std::this_thread::yield();
                    }

                    if (blocker_started.load(std::memory_order_acquire)) {
                        observed_stolen_execution = std::this_thread::get_id() != blocker_thread_id;
                    }
                }));
            } else {
                futures.push_back(pool.enqueue([]() {}));
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        release_promise.set_value();

        for (auto& future : futures) {
            future.get();
        }
    }

    EXPECT_TRUE(observed_stolen_execution);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
