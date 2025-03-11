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

namespace util {
namespace timer {

/**
 * @brief 定时器类，支持一次性定时器和周期性定时器
 */
class Timer {
public:
    using TimerId = uint64_t;
    using Callback = std::function<void()>;
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = Clock::duration;

    /**
     * @brief 获取单例实例
     * @return Timer& 定时器实例
     */
    static Timer& instance() {
        static Timer instance;
        return instance;
    }

    /**
     * @brief 创建一次性定时器
     * @param delay 延迟时间（毫秒）
     * @param callback 回调函数
     * @return TimerId 定时器ID
     */
    TimerId schedule_once(uint64_t delay_ms, Callback callback) {
        return schedule_at(Clock::now() + std::chrono::milliseconds(delay_ms), 
                          std::move(callback), 0);
    }

    /**
     * @brief 创建周期性定时器
     * @param delay_ms 初始延迟时间（毫秒）
     * @param interval_ms 周期间隔（毫秒）
     * @param callback 回调函数
     * @return TimerId 定时器ID
     */
    TimerId schedule_repeated(uint64_t delay_ms, uint64_t interval_ms, Callback callback) {
        return schedule_at(Clock::now() + std::chrono::milliseconds(delay_ms), 
                          std::move(callback), interval_ms);
    }

    /**
     * @brief 取消定时器
     * @param timer_id 定时器ID
     * @return bool 是否成功取消
     */
    bool cancel(TimerId timer_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = timers_.find(timer_id);
        if (it != timers_.end()) {
            // 标记为已取消
            it->second.cancelled = true;
            return true;
        }
        return false;
    }

    /**
     * @brief 停止定时器线程
     */
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
            // 清空所有定时器
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
     * @brief 析构函数
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

        // 比较函数，用于优先队列排序
        bool operator>(const TimerItem& other) const {
            return next_time > other.next_time;
        }
    };

    // 私有构造函数，确保单例模式
    Timer() : next_timer_id_(1), running_(true) {
        timer_thread_ = std::thread(&Timer::timer_loop, this);
    }

    // 禁止拷贝和赋值
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    // 在指定时间点调度任务
    TimerId schedule_at(TimePoint time, Callback callback, uint64_t interval_ms) {
        std::lock_guard<std::mutex> lock(mutex_);
        TimerId id = next_timer_id_++;
        
        TimerItem item{id, time, std::move(callback), interval_ms, false};
        timers_[id] = item;
        timer_queue_.push(item);
        
        // 如果新添加的定时器是最早的，唤醒定时器线程重新计算等待时间
        if (timer_queue_.top().id == id) {
            cond_.notify_one();
        }
        
        return id;
    }

    // 定时器线程主循环
    void timer_loop() {
        std::unique_lock<std::mutex> lock(mutex_);
        
        while (running_) {
            if (timer_queue_.empty()) {
                // 没有定时器，等待条件变量通知
                cond_.wait(lock);
                continue;
            }
            
            // 获取最早的定时器
            auto item = timer_queue_.top();
            timer_queue_.pop();
            
            // 检查定时器是否已取消
            if (timers_.find(item.id) == timers_.end() || timers_[item.id].cancelled) {
                continue;
            }
            
            // 计算等待时间
            auto now = Clock::now();
            if (item.next_time > now) {
                // 还没到时间，重新放回队列并等待
                timer_queue_.push(item);
                cond_.wait_until(lock, item.next_time);
                continue;
            }
            
            // 保存回调函数，以便在解锁后调用
            auto callback = item.callback;
            
            // 如果是周期性定时器，重新调度
            if (item.interval_ms > 0) {
                item.next_time = now + std::chrono::milliseconds(item.interval_ms);
                timer_queue_.push(item);
                timers_[item.id] = item;
            } else {
                // 一次性定时器，从映射中移除
                timers_.erase(item.id);
            }
            
            // 临时解锁以执行回调，避免长时间持有锁
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
    
    // 存储所有定时器
    std::map<TimerId, TimerItem> timers_;
    
    // 定时器优先队列，按触发时间排序
    std::priority_queue<TimerItem, std::vector<TimerItem>, std::greater<TimerItem>> timer_queue_;
};

/**
 * @brief 便捷函数：创建一次性定时器
 * @param delay_ms 延迟时间（毫秒）
 * @param callback 回调函数
 * @return Timer::TimerId 定时器ID
 */
inline Timer::TimerId schedule_once(uint64_t delay_ms, Timer::Callback callback) {
    return Timer::instance().schedule_once(delay_ms, std::move(callback));
}

/**
 * @brief 便捷函数：创建周期性定时器
 * @param delay_ms 初始延迟时间（毫秒）
 * @param interval_ms 周期间隔（毫秒）
 * @param callback 回调函数
 * @return Timer::TimerId 定时器ID
 */
inline Timer::TimerId schedule_repeated(uint64_t delay_ms, uint64_t interval_ms, Timer::Callback callback) {
    return Timer::instance().schedule_repeated(delay_ms, interval_ms, std::move(callback));
}

/**
 * @brief 便捷函数：取消定时器
 * @param timer_id 定时器ID
 * @return bool 是否成功取消
 */
inline bool cancel_timer(Timer::TimerId timer_id) {
    return Timer::instance().cancel(timer_id);
}

} // namespace timer
} // namespace util

#endif // UTIL_TIMER_HPP
