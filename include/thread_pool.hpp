#pragma once
#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <atomic>

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
        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            // don't allow enqueueing after stopping the pool
            if(stop.load()) {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }

            tasks.emplace([task](){ (*task)(); });
        }
        condition.notify_one();
        return res;
    }
    
    // Get the current number of tasks in the queue
    size_t get_task_count() const;
    
    // Get the number of threads in the pool
    size_t get_thread_count() const;
        
    ~ThreadPool();

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    
    mutable std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop;
};

} // namespace thread
} // namespace SAK