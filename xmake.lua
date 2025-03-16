add_rules("mode.debug")

-- Set common options
set_languages("c++17")
add_defines("_LIBCPP_ENABLE_CXX17_REMOVED_FEATURES")  -- Compatibility with older compilers

-- Logger library
target("libutil_log")
    set_kind("static")
    add_files("src/logger.cpp")
    add_headerfiles("include/logger.hpp")
    add_links("stdc++fs")  -- Link filesystem library

-- Thread pool library
target("libutil_thread")
    set_kind("static")
    add_files("src/thread_pool.cpp")
    add_headerfiles("include/thread_pool.hpp")
    add_links("pthread")

-- Memory pool library
target("libutil_memory")
    set_kind("static")
    add_files("src/memory_pool.cpp")
    add_headerfiles("include/memory_pool.hpp")

-- Object pool library
target("libutil_object_pool")
    set_kind("static")
    add_files("src/object_pool.cpp")
    add_headerfiles("include/object_pool.hpp")
    add_links("pthread")

-- Timer library
target("libutil_timer")
    set_kind("static")
    add_files("src/timer.cpp")
    add_headerfiles("include/timer.hpp")
    add_links("pthread")

-- IPC module
target("ipc")
    set_kind("static")
    add_files("src/ipc_implement.cpp", "src/ipc_shared_memory.cpp")
    add_headerfiles("include/ipc_implement.hpp", "include/ipc_shared_memory.hpp")
    add_includedirs("include", {public = true})
    add_deps("libutil_log")
    add_links("pthread", "rt")  -- Add rt library for shared memory and semaphore operations

-- Combine all libraries into one target
target("libutil")
    set_kind("static")
    add_deps("libutil_log", "libutil_thread", "libutil_memory", "libutil_timer", "libutil_object_pool", "ipc")
    add_links("pthread")

-- Test program
target("test_util")
    set_kind("binary")
    add_deps("libutil")
    add_files("test/test_main.cpp")
    add_includedirs("include")
    add_links("pthread", "rt")  -- Add rt library for shared memory and semaphore operations
