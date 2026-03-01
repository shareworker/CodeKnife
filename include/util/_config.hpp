#pragma once

#include <cstddef>

#if _WIN32 || _WIN64
    #if defined(_M_X64) || defined(__x86_64__)
        #define __SAK_X86_64__ 1
    #elif defined(_M_IX86) || defined(__i386__)
        #define __SAK_X86_32__ 1
    #endif
#else
    #if __x86_64__
        #define __SAK_X86_64__ 1
    #elif __i386__
        #define __SAK_X86_32__ 1
    #endif
#endif

constexpr size_t max_nfs_size = 128;
constexpr std::size_t max_nfs_size_exp = 7;