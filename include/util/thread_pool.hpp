#pragma once
#include <vector>
#include <deque>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <atomic>
#include "work_stealing_deque.hpp"

namespace SAK {
namespace thread {

class ThreadPool {
public:
    explicit ThreadPool(size_t threads = std::thread::hardware_concurrency());
    
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<decltype(std::declval<F>()(std::declval<Args>()...))> {
        using return_type = decltype(std::declval<F>()(std::declval<Args>()...));

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> res = task->get_future();
        submit_task([task](){ (*task)(); });
        return res;
    }
    
    // Get the current number of tasks in the queue
    size_t get_task_count() const;
    
    // Get the number of threads in the pool
    size_t get_thread_count() const;
        
    ~ThreadPool();

private:
    struct WorkerState {
        WorkerState() = default;

        work_stealing_deque<std::function<void()>> local_tasks;
        std::deque<std::function<void()>> inbox;
        std::mutex inbox_mutex;
    };

    void submit_task(std::function<void()> task);
    bool try_acquire_task(size_t worker_index, std::function<void()>& task);
    bool try_drain_inbox_to_local(size_t worker_index);
    bool try_steal_task(size_t worker_index, std::function<void()>& task);

    std::vector<std::thread> workers;
    std::vector<std::unique_ptr<WorkerState>> worker_states;
    
    mutable std::mutex control_mutex;
    mutable std::mutex wait_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop;
    std::atomic<size_t> pending_tasks{0};
    std::atomic<size_t> submission_index{0};
};

} // namespace thread
} // namespace SAK