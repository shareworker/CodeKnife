#pragma once

#include "_utils.hpp"
#include <atomic>
#include <cassert>

namespace SAK {

template<typename Mutex>
class unique_scoped_lock {
public:
    constexpr unique_scoped_lock() noexcept : mutex_(nullptr) {}
    ~unique_scoped_lock() {
        if (mutex_) {
            Release();
        }
    }
    unique_scoped_lock(Mutex& m) {
        Acquire(m);
    }
    unique_scoped_lock(const unique_scoped_lock&) = delete;
    unique_scoped_lock& operator=(const unique_scoped_lock&) = delete;

    void Acquire(Mutex& m) {
        assert(mutex_ == nullptr);
        mutex_ = &m;
        m.lock();
    }

    bool TryAcquire(Mutex& m) {
        assert(mutex_ == nullptr);
        bool succeed = m.try_lock();
        if (succeed) {
            mutex_ = &m;
        }
        return succeed;
    }

    void Release() {
        assert(mutex_);
        mutex_->unlock();
        mutex_ = nullptr;
    }

private:
    Mutex* mutex_{};
};

class spin_mutex {
public:
    static constexpr bool is_fair_mutex = false;
    static constexpr bool is_recursive_mutex = false;
    static constexpr bool is_rw_mutex = false;

    spin_mutex() noexcept : flag_(false) {}
    ~spin_mutex() = default;

    spin_mutex(const spin_mutex&) = delete;
    spin_mutex& operator=(const spin_mutex&) = delete;

    using scoped_lock = unique_scoped_lock<spin_mutex>;
    void lock() {
        atomic_backoff backoff;
        while (flag_.load(std::memory_order_relaxed) || flag_.exchange(true)) {
            backoff.Pause();
        }
    }

    bool try_lock() {
        return !flag_.load(std::memory_order_relaxed) && !flag_.exchange(true);
    }

    void unlock() {
        flag_.store(false, std::memory_order_release);
    }

private:
    std::atomic<bool> flag_{false};
};

}