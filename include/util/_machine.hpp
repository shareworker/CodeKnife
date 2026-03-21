#pragma once

#include "_config.hpp"
#include <cstdint>
#include <thread>
#include <climits>
 
#if defined(_MSC_VER)
#include <intrin.h> // MSVC hardware instruction intrinsics
#endif

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

inline uintptr_t clz(unsigned int x) { return static_cast<uintptr_t>(__builtin_clz(x)); }
inline uintptr_t clz(unsigned long int x) { return static_cast<uintptr_t>(__builtin_clzl(x)); }
inline uintptr_t clz(unsigned long long int x) { return static_cast<uintptr_t>(__builtin_clzll(x)); }

static inline uintptr_t machine_log2(uintptr_t x) {
    if (x == 0) return 0; 
#if defined(__GNUC__) || defined(__clang__)
    return (sizeof(decltype(x)) * CHAR_BIT - 1 ) ^ clz(x);
#elif defined(_MSC_VER)
    unsigned long j;
    #if defined(_WIN64)
        _BitScanReverse64(&j, x);
    #else
        _BitScanReverse(&j, static_cast<unsigned long>(x));
    #endif
    return static_cast<uintptr_t>(j);
#else
    intptr_t result = 0;

    if( sizeof(x) > 4 && (uintptr_t tmp = x >> 32) ) { x = tmp; result += 32; }
    if( uintptr_t tmp = x >> 16 ) { x = tmp; result += 16; }
    if( uintptr_t tmp = x >> 8 )  { x = tmp; result += 8; }
    if( uintptr_t tmp = x >> 4 )  { x = tmp; result += 4; }
    if( uintptr_t tmp = x >> 2 )  { x = tmp; result += 2; }

    return (x & 2) ? result + 1 : result;
#endif
}

}