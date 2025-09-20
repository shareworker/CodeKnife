#include "../include/thread_pool.hpp"

namespace SAK {
namespace thread {

ThreadPool::ThreadPool(size_t threads) : stop(false) {
    for(size_t i = 0; i < threads; ++i)
        workers.emplace_back(
            [this] {
                while(true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock,
                            [this] { return this->stop.load() || !this->tasks.empty(); });

                        if(this->stop.load() && this->tasks.empty())
                            return;
                            
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            }
        );
}

ThreadPool::~ThreadPool() {
    stop.store(true);
    condition.notify_all();
    for(std::thread &worker: workers) {
        if(worker.joinable()) {
            worker.join();
        }
    }
}

size_t ThreadPool::get_task_count() const {
    std::lock_guard<std::mutex> lock(queue_mutex);
    return tasks.size();
}

size_t ThreadPool::get_thread_count() const {
    return workers.size();
}

} // namespace thread
} // namespace SAK
