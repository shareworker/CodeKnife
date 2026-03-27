#include "thread_pool.hpp"

namespace SAK {
namespace thread {

ThreadPool::ThreadPool(size_t threads) : stop(false) {
    if (threads == 0) {
        threads = 1;
    }

    worker_states.reserve(threads);
    for (size_t i = 0; i < threads; ++i) {
        worker_states.push_back(std::make_unique<WorkerState>());
    }

    for(size_t i = 0; i < threads; ++i)
        workers.emplace_back(
            [this, i] {
                while(true) {
                    std::function<void()> task;

                    if (this->try_acquire_task(i, task)) {
                        task();
                        continue;
                    }

                    std::unique_lock<std::mutex> lock(this->wait_mutex);
                    this->condition.wait(lock, [this] {
                        return this->stop.load(std::memory_order_acquire) ||
                               this->pending_tasks.load(std::memory_order_acquire) > 0;
                    });

                    if (this->stop.load(std::memory_order_acquire) &&
                        this->pending_tasks.load(std::memory_order_acquire) == 0) {
                        return;
                    }
                }
            }
        );
}

void ThreadPool::submit_task(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(control_mutex);
    if (stop.load(std::memory_order_acquire)) {
        throw std::runtime_error("enqueue on stopped ThreadPool");
    }

    pending_tasks.fetch_add(1, std::memory_order_release);

    const size_t index = submission_index.fetch_add(1, std::memory_order_relaxed) % worker_states.size();
    {
        std::lock_guard<std::mutex> inbox_lock(worker_states[index]->inbox_mutex);
        worker_states[index]->inbox.push_back(std::move(task));
    }

    condition.notify_one();
}

bool ThreadPool::try_acquire_task(size_t worker_index, std::function<void()>& task) {
    if (auto local = worker_states[worker_index]->local_tasks.pop_bottom()) {
        task = std::move(*local);
        pending_tasks.fetch_sub(1, std::memory_order_acq_rel);
        return true;
    }

    if (try_drain_inbox_to_local(worker_index)) {
        if (auto local = worker_states[worker_index]->local_tasks.pop_bottom()) {
            task = std::move(*local);
            pending_tasks.fetch_sub(1, std::memory_order_acq_rel);
            return true;
        }
    }

    if (try_steal_task(worker_index, task)) {
        pending_tasks.fetch_sub(1, std::memory_order_acq_rel);
        return true;
    }

    return false;
}

bool ThreadPool::try_drain_inbox_to_local(size_t worker_index) {
    std::deque<std::function<void()>> staged;
    {
        std::lock_guard<std::mutex> lock(worker_states[worker_index]->inbox_mutex);
        if (worker_states[worker_index]->inbox.empty()) {
            return false;
        }
        staged.swap(worker_states[worker_index]->inbox);
    }

    for (auto& queued_task : staged) {
        worker_states[worker_index]->local_tasks.push_bottom(std::move(queued_task));
    }
    return true;
}

bool ThreadPool::try_steal_task(size_t worker_index, std::function<void()>& task) {
    const size_t worker_count = worker_states.size();
    for (size_t offset = 1; offset < worker_count; ++offset) {
        const size_t victim = (worker_index + offset) % worker_count;
        if (auto stolen = worker_states[victim]->local_tasks.steal_top()) {
            task = std::move(*stolen);
            return true;
        }

        std::lock_guard<std::mutex> inbox_lock(worker_states[victim]->inbox_mutex);
        if (!worker_states[victim]->inbox.empty()) {
            task = std::move(worker_states[victim]->inbox.front());
            worker_states[victim]->inbox.pop_front();
            return true;
        }
    }

    return false;
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(control_mutex);
        stop.store(true, std::memory_order_release);
    }
    condition.notify_all();
    for(std::thread &worker: workers) {
        if(worker.joinable()) {
            worker.join();
        }
    }
}

size_t ThreadPool::get_task_count() const {
    return pending_tasks.load(std::memory_order_acquire);
}

size_t ThreadPool::get_thread_count() const {
    return workers.size();
}

} // namespace thread
} // namespace SAK

