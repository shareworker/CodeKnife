add_rules("mode.debug")

-- Set common options
set_languages("cxx17")
add_defines("_LIBCPP_ENABLE_CXX17_REMOVED_FEATURES")

-- External dependencies
add_requires("nlohmann_json")  -- Compatibility with older compilers
add_requires("gtest")  -- GoogleTest for unit testing

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

-- LSM module
target("lsm")
    set_kind("static")
    add_files("src/mini_lsm.cpp", "src/lsm_storage.cpp", "src/compaction_controller.cpp", 
              "src/leveled_compaction_controller.cpp", "src/tiered_compaction_controller.cpp",
              "src/sstable.cpp", "src/sstable_builder.cpp",
              "src/simple_leveled_compaction_controller.cpp", "src/file_object.cpp", "src/manifest.cpp", 
              "src/mem_table.cpp", "src/byte_buffer.cpp", "src/wal.cpp", "src/wal_segment.cpp", "src/crc32c.cpp",
              "src/mvcc.cpp", "src/mvcc_txn.cpp", "src/mvcc_watermark.cpp", "src/skip_map.cpp", "src/mini_lsm_mvcc.cpp",
              "src/mvcc_mem_table.cpp", "src/mvcc_wal.cpp", "src/mvcc_sstable_iterator.cpp", "src/mvcc_merge_iterator.cpp", "src/mvcc_lsm_iterator.cpp",
              "src/mvcc_two_merge_iterator.cpp", "src/mvcc_transaction.cpp", "src/mvcc_lsm_storage.cpp")
    add_headerfiles("include/mini_lsm.hpp", "include/lsm_storage.hpp", "include/sstable.hpp", 
                    "include/sstable_builder.hpp", "include/file_object.hpp", "include/block_cache.hpp", "include/manifest.hpp", 
                    "include/mem_table.hpp", "include/skiplist.hpp", "include/byte_buffer.hpp", 
                    "include/wal.hpp", "include/storage_iterator.hpp", "include/bound.hpp", 
                    "include/block.hpp", "include/block_iterator.hpp", "include/mem_table_iterator.hpp", 
                    "include/merge_iterator.hpp", "include/sstable_iterator.hpp", 
                    "include/two_merge_iterator.hpp", "include/sst_concat_iterator.hpp", "include/bloom_filter.hpp", "include/compaction_controller.hpp",
                    "include/simple_leveled_compaction_controller.hpp", "include/leveled_compaction_controller.hpp", "include/tiered_compaction_controller.hpp",
                    "include/crc32c.hpp", "include/mvcc.hpp", "include/mvcc_txn.hpp", "include/mvcc_watermark.hpp", "include/skip_map.hpp", 
                    "include/mini_lsm_mvcc.hpp", "include/key.hpp", "include/mvcc_lsm_iterator.hpp", "include/mvcc_merge_iterator.hpp",
                    "include/mvcc_sstable_iterator.hpp", "include/mvcc_mem_table.hpp", "include/mvcc_two_merge_iterator.hpp",
                    "include/mvcc_skiplist.hpp", "include/mvcc_transaction.hpp", "include/mvcc_transaction_iterator.hpp")
    add_includedirs("include", {public = true})
    add_deps("libutil_log")
    add_packages("nlohmann_json")
    add_links("pthread", "stdc++fs", "z")  -- Add filesystem and zlib

-- Combine all libraries into one target
target("libutil")
    set_kind("static")
    add_deps("libutil_log", "libutil_thread", "libutil_memory", "libutil_timer", "libutil_object_pool", "ipc", "lsm")
    add_links("pthread")

-- Test program
target("test_util")
    set_kind("binary")
    set_default(false)
    add_tests("default")
    add_deps("libutil")
    add_files("test/test_main.cpp")
    add_includedirs("include")
    add_links("pthread", "rt")  -- Add rt library for shared memory and semaphore operations

-- LSM recovery unit tests
target("lsm_recovery_test")
    set_kind("binary")
    set_default(false)
    add_tests("default")
    add_deps("libutil")
    add_files("test/lsm_recovery_test.cpp")
    add_packages("gtest")
    add_includedirs("include")
    add_links("pthread", "stdc++fs")

-- MVCC unit tests
target("mvcc_test")
    set_kind("binary")
    set_default(false)
    add_tests("default")
    add_deps("libutil")
    add_files("test/mvcc_test.cpp")
    add_packages("gtest")
    add_includedirs("include")
    add_links("pthread", "stdc++fs")

-- MVCC Transaction unit tests
target("mvcc_transaction_test")
    set_kind("binary")
    add_deps("lsm", "libutil")
    add_files("test/mvcc_transaction_test.cpp")
    add_packages("gtest")
    set_rundir("$(projectdir)")

target("mvcc_gc_test")
    set_kind("binary")
    add_deps("lsm", "libutil")
    add_files("test/mvcc_gc_test.cpp")
    add_packages("gtest")
    add_includedirs("include")
    add_links("pthread", "stdc++fs")
    set_rundir("$(projectdir)")

target("mvcc_enhanced_transaction_test")
    set_kind("binary")
    add_deps("lsm", "libutil")
    add_files("test/mvcc_enhanced_transaction_test.cpp")
    add_packages("gtest")
    add_includedirs("include")
    add_links("pthread", "stdc++fs")
    set_rundir("$(projectdir)")

target("basic_iterator_safety_test")
    set_kind("binary")
    add_files("test/basic_iterator_safety_test.cpp")
    add_deps("libutil")
    add_packages("gtest")
    add_includedirs("include")
    set_rundir("$(projectdir)")

target("compaction_test")
    set_kind("binary")
    add_files("test/compaction_test.cpp")
    add_deps("lsm", "libutil")
    add_packages("gtest")
    add_includedirs("include")
    add_links("pthread", "stdc++fs")
    set_rundir("$(projectdir)")

-- Comprehensive Mini-LSM test
target("mini_lsm_comprehensive_test")
    set_kind("binary")
    set_default(false)
    add_tests("default")
    add_deps("libutil")
    add_files("test/mini_lsm_comprehensive_test.cpp")
    add_packages("gtest")
    add_includedirs("include")
    add_links("pthread", "stdc++fs", "z")
