#pragma once

#include "_config.hpp"
#include "_machine.hpp"
#include <cstdint>
#include <thread>

namespace SAK {

static inline void yield() {
    std::this_thread::yield();
}

class atomic_backoff {
public:
    static constexpr std::int32_t LOOPS_BEFORE_YIELD = 16;
    std::int32_t count_;
public:
    atomic_backoff() : count_(1) {}
    atomic_backoff(bool) : count_(1) { Pause(); }
    
    atomic_backoff(const atomic_backoff&) = delete;
    atomic_backoff& operator=(const atomic_backoff&) = delete;
    void Pause() {
        if (count_ <= LOOPS_BEFORE_YIELD) {
            machine_pause(count_);
            count_ *= 2;
        }
        else {
            yield();
        }
    }

    bool BoundedPause() {
        machine_pause(count_);
        if (count_ < LOOPS_BEFORE_YIELD) {
            count_ *= 2;
            return true;
        }
        return false;
    }
    
    void Reset() {
        count_ = 1;
    }
};

template<class T, size_t S, size_t R>
struct padded_base : T {
    char pad[S - R];
};
template<class T, size_t S> struct padded_base<T, S, 0> : T {};

template<class T, size_t S = max_nfs_size>
struct padded : padded_base<T, S, sizeof(T) % S> {};

}