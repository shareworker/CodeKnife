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

namespace util {
namespace thread {

class ThreadPool {
public:
    explicit ThreadPool(size_t threads = std::thread::hardware_concurrency());
    
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type> {
        using return_type = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            // don't allow enqueueing after stopping the pool
            if(stop) {
                // Return a future with an empty result for stopped pool
                auto empty_task = std::make_shared<std::packaged_task<return_type()>>(
                    []() -> return_type { return return_type{}; }
                );
                auto res = empty_task->get_future();
                return res;
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
    bool stop;
};

} // namespace thread
} // namespace util