add_rules("mode.debug")

-- 设置公共选项
set_languages("c++17")
add_defines("_LIBCPP_ENABLE_CXX17_REMOVED_FEATURES")  -- 兼容旧版本编译器

-- 日志库
target("libutil_log")
    set_kind("static")
    add_files("src/logger.cpp")
    add_headerfiles("include/logger.hpp")
    add_links("stdc++fs")  -- 链接filesystem库

-- 线程池库
target("libutil_thread")
    set_kind("static")
    add_files("src/thread_pool.cpp")
    add_headerfiles("include/thread_pool.hpp")
    add_links("pthread")

-- 内存池库
target("libutil_memory")
    set_kind("static")
    add_files("src/memory_pool.cpp")
    add_headerfiles("include/memory_pool.hpp")

-- 定时器库
target("libutil_timer")
    set_kind("static")
    add_files("src/timer.cpp")
    add_headerfiles("include/timer.hpp")
    add_links("pthread")

-- IPC模块
target("ipc")
    set_kind("static")
    add_files("src/ipc_base.cpp", "src/ipc_writer.cpp", "src/ipc_reader.cpp", "src/ipc_implement.cpp")
    add_headerfiles("include/ipc_base.hpp", "include/ipc_packet.hpp", "include/ipc_writer.hpp", "include/ipc_reader.hpp", "include/ipc_implement.hpp")
    add_includedirs("include", {public = true})
    add_deps("libutil_log")
    add_links("pthread")

-- 整合所有库到一个目标
target("libutil")
    set_kind("static")
    add_deps("libutil_log", "libutil_thread", "libutil_memory", "libutil_timer", "ipc")
    add_links("pthread")

-- 测试程序
target("test_util")
    set_kind("binary")
    add_deps("libutil")
    add_files("test/test_main.cpp")
    add_includedirs("include")
    add_links("pthread")
