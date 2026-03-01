#pragma once

#include "_config.hpp"
#include <cstdint>
#include <thread>

#if __SAK_X86_64__ || __SAK_X86_32__
#include <immintrin.h>
#endif

namespace SAK {

static inline void machine_pause(int32_t delay) {
#if __SAK_X86_64__ || __SAK_X86_32__
    while (delay-- > 0) {
        _mm_pause();
    }
#elif __ARM_ARCH_7A__ || __aarch64__
    while (delay-- > 0) {
        __asm__ __volatile__("isb sy" ::: "memory");
    }
#else
    (void)delay;
    std::this_thread::yield();
#endif
}

}