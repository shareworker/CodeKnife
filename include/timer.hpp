#ifndef UTIL_TIMER_HPP
#define UTIL_TIMER_HPP

#include <chrono>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <map>
#include <queue>
#include <memory>
#include <utility>
#include <cstdint>

namespace SAK {
namespace timer {

/**
 * @brief Timer class, supports one-time and periodic timers
 */
class Timer {
public:
    using TimerId = uint64_t;
    using Callback = std::function<void()>;
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = Clock::duration;

    /**
     * @brief Get singleton instance
     * @return Timer& Timer instance
     */
    static Timer& instance() {
        static Timer instance;
        return instance;
    }

    /**
     * @brief Create a one-time timer
     * @param delay Delay time (milliseconds)
     * @param callback Callback function
     * @return TimerId Timer ID
     */
    TimerId schedule_once(uint64_t delay_ms, Callback callback) {
        return schedule_at(Clock::now() + std::chrono::milliseconds(delay_ms), 
                          std::move(callback), 0);
    }

    /**
     * @brief Create a periodic timer
     * @param delay_ms Initial delay time (milliseconds)
     * @param interval_ms Interval time (milliseconds)
     * @param callback Callback function
     * @return TimerId Timer ID
     */
    TimerId schedule_repeated(uint64_t delay_ms, uint64_t interval_ms, Callback callback) {
        return schedule_at(Clock::now() + std::chrono::milliseconds(delay_ms), 
                          std::move(callback), interval_ms);
    }

    /**
     * @brief Cancel a timer
     * @param timer_id Timer ID
     * @return bool Whether the cancellation is successful
     */
    bool cancel(TimerId timer_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = timers_.find(timer_id);
        if (it != timers_.end()) {
            // Mark as cancelled
            it->second.cancelled = true;
            return true;
        }
        return false;
    }

    /**
     * @brief Stop the timer thread
     */
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
            // Clear all timers
            timers_.clear();
            while (!timer_queue_.empty()) {
                timer_queue_.pop();
            }
        }
        cond_.notify_one();
        if (timer_thread_.joinable()) {
            timer_thread_.join();
        }
    }

    /**
     * @brief Destructor
     */
    ~Timer() {
        stop();
    }

private:
    struct TimerItem {
        TimerId id;
        TimePoint next_time;
        Callback callback;
        uint64_t interval_ms;
        bool cancelled;

        // Comparison function for priority queue sorting
        bool operator>(const TimerItem& other) const {
            return next_time > other.next_time;
        }
    };

    // Private constructor to ensure singleton pattern
    Timer() : running_(true), next_timer_id_(1) {
        timer_thread_ = std::thread(&Timer::timer_loop, this);
    }

    // Disable copying and assignment
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    // Schedule a task at a specific time point
    TimerId schedule_at(TimePoint time, Callback callback, uint64_t interval_ms) {
        std::lock_guard<std::mutex> lock(mutex_);
        TimerId id = next_timer_id_++;
        
        TimerItem item{id, time, std::move(callback), interval_ms, false};
        timers_[id] = item;
        timer_queue_.push(item);
        
        // If the newly added timer is the earliest, wake up the timer thread to recalculate the waiting time
        if (timer_queue_.top().id == id) {
            cond_.notify_one();
        }
        
        return id;
    }

    // Timer thread main loop
    void timer_loop() {
        std::unique_lock<std::mutex> lock(mutex_);
        
        while (running_) {
            if (timer_queue_.empty()) {
                // No timers, wait for condition variable notification
                cond_.wait(lock);
                continue;
            }
            
            // Get the earliest timer
            auto item = timer_queue_.top();
            timer_queue_.pop();
            
            // Check if the timer has been cancelled
            if (timers_.find(item.id) == timers_.end() || timers_[item.id].cancelled) {
                continue;
            }
            
            // Calculate the waiting time
            auto now = Clock::now();
            if (item.next_time > now) {
                // Not yet time, put it back in the queue and wait
                timer_queue_.push(item);
                cond_.wait_until(lock, item.next_time);
                continue;
            }
            
            // Save the callback function to be called after unlocking
            auto callback = item.callback;
            
            // If it's a periodic timer, reschedule
            if (item.interval_ms > 0) {
                item.next_time = now + std::chrono::milliseconds(item.interval_ms);
                timer_queue_.push(item);
                timers_[item.id] = item;
            } else {
                // One-time timer, remove from the map
                timers_.erase(item.id);
            }
            
            // Temporarily unlock to execute the callback, avoiding long-term lock holding
            lock.unlock();
            callback();
            lock.lock();
        }
    }

    std::thread timer_thread_;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::atomic<bool> running_;
    std::atomic<TimerId> next_timer_id_;
    
    // Store all timers
    std::map<TimerId, TimerItem> timers_;
    
    // Timer priority queue, sorted by trigger time
    std::priority_queue<TimerItem, std::vector<TimerItem>, std::greater<TimerItem>> timer_queue_;
};

/**
 * @brief Convenience function: Create a one-time timer
 * @param delay Delay time (milliseconds)
 * @param callback Callback function
 * @return Timer::TimerId Timer ID
 */
inline Timer::TimerId schedule_once(uint64_t delay_ms, Timer::Callback callback) {
    return Timer::instance().schedule_once(delay_ms, std::move(callback));
}

/**
 * @brief Convenience function: Create a periodic timer
 * @param delay_ms Initial delay time (milliseconds)
 * @param interval_ms Interval time (milliseconds)
 * @param callback Callback function
 * @return Timer::TimerId Timer ID
 */
inline Timer::TimerId schedule_repeated(uint64_t delay_ms, uint64_t interval_ms, Timer::Callback callback) {
    return Timer::instance().schedule_repeated(delay_ms, interval_ms, std::move(callback));
}

/**
 * @brief Convenience function: Cancel a timer
 * @param timer_id Timer ID
 * @return bool Whether the cancellation is successful
 */
inline bool cancel_timer(Timer::TimerId timer_id) {
    return Timer::instance().cancel(timer_id);
}

} // namespace timer
} // namespace SAK

#endif // UTIL_TIMER_HPP
