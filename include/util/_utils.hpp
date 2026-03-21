#pragma once

#include "_config.hpp"
#include "_machine.hpp"
#include <atomic>
#include <cstdint>
#include <climits>
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

#if SAK_USE_ASSERT
static void* const poisoned_ptr = reinterpret_cast<void*>(-1);

template<typename T>
inline void poison_pointer(T*& p) { p = reinterpret_cast<T*>(poisoned_ptr); }

template<typename T>
inline void poison_pointer(std::atomic<T*>& p) { p.store(reinterpret_cast<T*>(poisoned_ptr), std::memory_order_relaxed); }

template<typename T>
inline bool is_poisoned(T* p) { return p == reinterpret_cast<T*>(poisoned_ptr); }

template<typename T>
inline bool is_poisoned(const std::atomic<T*>& p) { return is_poisoned(p.load(std::memory_order_relaxed)); }
#else
template<typename T>
inline void poison_pointer(T&) {/*do nothing*/}
#endif

template<int> struct Int2Type {};

class FastRandom {
private:
    unsigned x, c;
    static constexpr unsigned a = 0x9e3779b1;

public:
    unsigned short Get() {
        return Get(x);
    }

    unsigned short Get(unsigned& seed) {
        unsigned short r = (unsigned short)(seed >> 16);
        seed = seed * a + c;
        return r;
    }

    FastRandom(void* unique_ptr) { init(uintptr_t(unique_ptr)); }

    template<typename T>
    void init(T seed) {
        init(seed, Int2Type<sizeof(seed)>());
    }

    void init(uint64_t seed, Int2Type<8>) {
        init(uint32_t((seed>>32) + seed), Int2Type<4>());
    }

    void init(uint32_t seed, Int2Type<4>) {
        c = (seed | 1) * 0xba5703f5;
        x = c^(seed >> 1);
    }

};

inline intptr_t BitScanRev(uintptr_t x) {
    return x == 0 ? -1 : static_cast<intptr_t>(machine_log2(uintptr_t(x)));
}

template<unsigned NUM>
class BitMaskBasic {
    static constexpr unsigned SZ = (NUM-1)/(CHAR_BIT*sizeof(uintptr_t))+1;
    static constexpr unsigned WORD_LEN = CHAR_BIT*sizeof(uintptr_t);
    std::atomic<uintptr_t> mask_[SZ];

protected:
    void Set(size_t idx, bool val) {
        size_t i = idx / WORD_LEN;
        int pos = WORD_LEN - idx % WORD_LEN - 1;
        if (val) {
            mask_[i].fetch_or(1ULL << pos);
        } else {
            mask_[i].fetch_and(~(1ULL << pos));
        }
    }

    int GetMinTrue(unsigned startIdx) const {
        unsigned idx = startIdx / WORD_LEN;
        int pos;

        if (startIdx % WORD_LEN) {
            pos = WORD_LEN - startIdx % WORD_LEN;
            uintptr_t actualMask = mask_[idx].load(std::memory_order_relaxed) & (((uintptr_t)1<<pos) - 1);
            idx++;
            if (-1 != (pos = BitScanRev(actualMask)))
                return idx*WORD_LEN - pos - 1;
        }
        
        while (idx<SZ)
            if (-1 != (pos = BitScanRev(mask_[idx++].load(std::memory_order_relaxed))))
                return idx*WORD_LEN - pos - 1;
        return -1;
    }
};

template<unsigned NUM>
class BitMaskMin : public BitMaskBasic<NUM> {
public:
    void set(size_t idx, bool val) { BitMaskBasic<NUM>::Set(idx, val); }
    int getMinTrue(unsigned startIdx) const {
        return BitMaskBasic<NUM>::GetMinTrue(startIdx);
    }
};

template<unsigned NUM>
class BitMaskMax : public BitMaskBasic<NUM> {
public:
    void set(size_t idx, bool val) {
        BitMaskBasic<NUM>::Set(NUM - 1 - idx, val);
    }
    int getMaxTrue(unsigned startIdx) const {
        int p = BitMaskBasic<NUM>::GetMinTrue(NUM-startIdx-1);
        return -1==p? -1 : (int)NUM - 1 - p;
    }
};

}