add_rules("mode.debug", "mode.release")

-- Set common options
set_languages("cxx20")
add_defines("_LIBCPP_ENABLE_CXX17_REMOVED_FEATURES")

add_rules("plugin.compile_commands.autoupdate")

-- External dependencies
add_requires("gtest")  -- GoogleTest for unit testing

-- Add common compiler flags
add_cxxflags("-Wall", "-Wextra", "-Werror", "-fno-exceptions", "-fno-rtti")
add_cxxflags("-fstack-protector-strong", "-D_FORTIFY_SOURCE=2", "-Wformat-security")

-- Debug flags
add_cxxflags("-fsanitize=address,undefined", {mode = "debug"})
add_ldflags("-fsanitize=address,undefined", {mode = "debug"})

-- Release flags  
add_cxxflags("-O2", "-DNDEBUG", {mode = "release"})

-- Include directories
add_includedirs("include")

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

-- Core Utility library
target("libutil")
    set_kind("static")
    add_files("src/byte_buffer.cpp", "src/object_pool.cpp", "src/memory_pool.cpp", 
              "src/thread_pool.cpp", "src/timer.cpp", "src/logger.cpp", 
              "src/ipc_implement.cpp", "src/ipc_shared_memory.cpp", 
              "src/crc32c.cpp", "src/file_object.cpp")
    add_deps("libutil_log", "libutil_thread", "libutil_memory", "libutil_timer", "libutil_object_pool", "ipc")
    add_links("pthread", "rt", "stdc++fs")

-- Utility tests
target("test_util")
    set_kind("binary")
    add_deps("libutil")
    add_files("test/test_main.cpp")
    add_packages("gtest")
    add_tests("default")
    add_links("pthread", "stdc++fs")
    set_rundir("$(projectdir)")
