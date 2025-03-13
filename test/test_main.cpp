#include "../include/logger.hpp"
#include "../include/thread_pool.hpp"
#include "../include/memory_pool.hpp"
#include "../include/timer.hpp"
#include "../include/ipc_implement.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <iomanip>

// 测试日志模块
void test_logger() {
    std::cout << "\n===== 测试日志模块 =====\n" << std::endl;
    
    // 配置日志
    util::log::LogConfig config;
    config.use_stdout = true;  // 输出到控制台方便查看
    config.min_level = util::log::Level::DEBUG;
    util::log::Logger::instance().configure(config);
    
    // 测试不同级别的日志
    LOG_DEBUG("这是一条调试日志");
    LOG_INFO("这是一条信息日志");
    LOG_WARNING("这是一条警告日志");
    LOG_ERROR("这是一条错误日志");
    
    std::cout << "日志测试完成\n" << std::endl;
}

// 测试线程池
void test_thread_pool() {
    std::cout << "\n===== 测试线程池 =====\n" << std::endl;
    
    util::thread::ThreadPool pool(4);
    std::cout << "创建了" << pool.get_thread_count() << "个线程的线程池" << std::endl;
    
    // 提交一些任务
    std::vector<std::future<int>> results;
    
    for (int i = 0; i < 8; ++i) {
        auto result = pool.enqueue([i] {
            std::cout << "任务 " << i << " 在线程 " 
                      << std::this_thread::get_id() << " 上执行" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * i));
            return i * i;
        });
        results.push_back(std::move(result));
    }
    
    // 获取结果
    for (size_t i = 0; i < results.size(); ++i) {
        std::cout << "任务 " << i << " 的结果: " << results[i].get() << std::endl;
    }
    
    std::cout << "线程池测试完成\n" << std::endl;
}

// 测试内存池
void test_memory_pool() {
    std::cout << "\n===== 测试内存池 =====\n" << std::endl;
    
    auto& pool = util::memory::MemoryPool::GetInstance();
    
    // 分配和释放不同大小的内存
    std::vector<void*> pointers;
    std::vector<size_t> sizes = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};
    
    // 分配
    for (auto size : sizes) {
        void* ptr = pool.Allocate(size);
        pointers.push_back(ptr);
        std::cout << "分配了 " << size << " 字节的内存: " << ptr << std::endl;
    }
    
    // 打印统计信息
    std::cout << "\n内存池统计信息:" << std::endl;
    pool.PrintStats();
    
    // 释放
    for (size_t i = 0; i < pointers.size(); ++i) {
        pool.Deallocate(pointers[i], sizes[i]);
        std::cout << "释放了 " << sizes[i] << " 字节的内存: " << pointers[i] << std::endl;
    }
    
    // 测试智能指针
    std::cout << "\n测试内存池智能指针:" << std::endl;
    {
        auto ptr = util::memory::make_pool_ptr<std::string>("这是一个从内存池分配的字符串");
        std::cout << "创建了内存池智能指针: " << *ptr << std::endl;
    }
    
    // 测试新增的数组分配功能
    std::cout << "\n测试内存池数组分配:" << std::endl;
    {
        auto arr = util::memory::make_pool_array<int>(10);
        for (int i = 0; i < 10; ++i) {
            arr[i] = i * i;
        }
        std::cout << "创建了内存池数组，前5个元素: ";
        for (int i = 0; i < 5; ++i) {
            std::cout << arr[i] << " ";
        }
        std::cout << std::endl;
    }
    
    // 测试内存使用率统计
    std::cout << "\n测试内存使用率统计:" << std::endl;
    {
        // 分配一批内存以提高使用率
        std::vector<void*> test_ptrs;
        for (int i = 0; i < 100; ++i) {
            test_ptrs.push_back(pool.Allocate(64));
        }
        
        std::cout << "当前内存使用率: " << (pool.GetMemoryUsage() * 100.0) << "%" << std::endl;
        std::cout << "当前大内存块数量: " << pool.GetLargeAllocations() << std::endl;
        
        // 释放内存
        for (auto ptr : test_ptrs) {
            pool.Deallocate(ptr, 64);
        }
    }
    
    // 测试内存池Trim功能
    std::cout << "\n测试内存池Trim功能:" << std::endl;
    pool.Trim();
    
    // 再次打印统计信息
    std::cout << "\n释放后内存池统计信息:" << std::endl;
    pool.PrintStats();
    
    std::cout << "内存池测试完成\n" << std::endl;
}

// 测试定时器
void test_timer() {
    std::cout << "\n===== 测试定时器模块 =====\n" << std::endl;
    
    // 测试一次性定时器
    auto timer_id = util::timer::schedule_once(500, []() {
        std::cout << "一次性定时器触发" << std::endl;
    });
    
    std::cout << "创建了一次性定时器，ID: " << timer_id << std::endl;
    
    // 等待定时器执行完成
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    std::cout << "定时器测试完成\n" << std::endl;
}

// 测试IPC通信
void test_ipc_communication() {
    std::cout << "\n===== 测试IPC通信模块 (共享内存实现) =====\n" << std::endl;
    
    const std::string ipc_name = "test_ipc";
    
    // 创建服务端和客户端IPC实现
    std::cout << "创建服务端和客户端..." << std::endl;
    util::ipc::IPCImplement server_ipc(ipc_name, true);  // 服务端
    util::ipc::IPCImplement client_ipc(ipc_name, false); // 客户端
    
    // 启动IPC通信
    std::cout << "启动服务端..." << std::endl;
    server_ipc.start();
    
    std::cout << "启动客户端..." << std::endl;
    client_ipc.start();
    
    // 等待启动完成
    std::cout << "等待IPC通道建立连接..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 测试双向通信
    std::cout << "\n===== 测试双向通信 =====\n" << std::endl;
    
    // 客户端发送请求
    std::cout << "客户端发送请求..." << std::endl;
    client_ipc.sendMessage("客户端请求: 获取当前时间");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    

    server_ipc.sendMessage("服务端响应: 当前时间是 " + 
                              std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    
    // 客户端发送另一个请求
    std::cout << "客户端发送另一个请求..." << std::endl;
    client_ipc.sendMessage("客户端请求: 获取系统信息");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    

    server_ipc.sendMessage("服务端响应: 系统信息 - Linux x86_64");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    
    // 测试并发双向通信
    std::cout << "\n===== 测试并发双向通信 =====\n" << std::endl;
    
    // 创建两个线程，一个用于客户端发送，一个用于服务端发送
    std::thread client_send_thread([&client_ipc]() {
        for (int i = 0; i < 10; i++) {
            std::string msg = "客户端并发消息 #" + std::to_string(i);
            client_ipc.sendMessage(msg);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    
    std::thread server_send_thread([&server_ipc]() {
        for (int i = 0; i < 10; i++) {
            std::string msg = "服务端并发消息 #" + std::to_string(i);
            server_ipc.sendMessage(msg);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
    });

    
    // 等待线程完成
    client_send_thread.join();
    server_send_thread.join();
    
    // 等待一段时间，确保所有消息都被处理
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 测试性能
    std::cout << "\n===== 测试IPC性能 =====\n" << std::endl;
    
    const int message_count = 1000;
    std::cout << "发送 " << message_count << " 条消息进行性能测试..." << std::endl;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 客户端发送大量消息
    for (int i = 0; i < message_count; i++) {
        client_ipc.sendMessage("性能测试消息 #" + std::to_string(i));
    }

    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    // 停止IPC通信
    std::cout << "停止IPC通信..." << std::endl;
    server_ipc.stop();
    client_ipc.stop();
    
    std::cout << "IPC通信测试完成\n" << std::endl;
}

// 性能测试：比较标准分配器和内存池
void benchmark_memory_pool() {
    std::cout << "\n===== 内存池性能测试 =====\n" << std::endl;
    
    // 减少迭代次数以避免长时间运行
    const int iterations = 10000;
    const std::vector<size_t> alloc_sizes = {16, 64, 256, 1024, 4096};
    
    std::cout << "测试不同大小的内存分配性能..." << std::endl;
    
    for (size_t alloc_size : alloc_sizes) {
        std::cout << "\n测试大小: " << alloc_size << " 字节" << std::endl;
        
        // 使用标准分配器
        auto start_std = std::chrono::high_resolution_clock::now();
        std::vector<void*> std_ptrs;
        std_ptrs.reserve(iterations);
        
        for (int i = 0; i < iterations; ++i) {
            std_ptrs.push_back(malloc(alloc_size));
        }
        
        for (auto ptr : std_ptrs) {
            free(ptr);
        }
        std_ptrs.clear();
        
        auto end_std = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> std_time = end_std - start_std;
        
        // 使用内存池
        auto& pool = util::memory::MemoryPool::GetInstance();
        auto start_pool = std::chrono::high_resolution_clock::now();
        std::vector<void*> pool_ptrs;
        pool_ptrs.reserve(iterations);
        
        for (int i = 0; i < iterations; ++i) {
            pool_ptrs.push_back(pool.Allocate(alloc_size));
        }
        
        for (auto ptr : pool_ptrs) {
            pool.Deallocate(ptr, alloc_size);
        }
        
        auto end_pool = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> pool_time = end_pool - start_pool;
        
        // 输出结果
        std::cout << "标准分配器: " << std::fixed << std::setprecision(2) << std_time.count() << " ms" << std::endl;
        std::cout << "内存池: " << std::fixed << std::setprecision(2) << pool_time.count() << " ms" << std::endl;
        std::cout << "性能提升: " << std::fixed << std::setprecision(2) << (std_time.count() / pool_time.count()) << "x" << std::endl;
    }
    
    // 测试多线程场景下的性能
    std::cout << "\n测试多线程场景下的内存池性能..." << std::endl;
    const int thread_count = 4;
    const int per_thread_iterations = iterations / thread_count;
    
    // 标准分配器多线程测试
    auto start_std_mt = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> std_threads;
    for (int t = 0; t < thread_count; ++t) {
        std_threads.emplace_back([per_thread_iterations]() {
            std::vector<void*> ptrs;
            ptrs.reserve(per_thread_iterations);
            
            for (int i = 0; i < per_thread_iterations; ++i) {
                ptrs.push_back(malloc(64));
            }
            
            for (auto ptr : ptrs) {
                free(ptr);
            }
        });
    }
    
    for (auto& t : std_threads) {
        t.join();
    }
    
    auto end_std_mt = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> std_mt_time = end_std_mt - start_std_mt;
    
    // 内存池多线程测试
    auto& pool = util::memory::MemoryPool::GetInstance();
    auto start_pool_mt = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> pool_threads;
    for (int t = 0; t < thread_count; ++t) {
        pool_threads.emplace_back([per_thread_iterations, &pool]() {
            std::vector<void*> ptrs;
            ptrs.reserve(per_thread_iterations);
            
            for (int i = 0; i < per_thread_iterations; ++i) {
                ptrs.push_back(pool.Allocate(64));
            }
            
            for (auto ptr : ptrs) {
                pool.Deallocate(ptr, 64);
            }
        });
    }
    
    for (auto& t : pool_threads) {
        t.join();
    }
    
    auto end_pool_mt = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> pool_mt_time = end_pool_mt - start_pool_mt;
    
    // 输出多线程测试结果
    std::cout << "多线程标准分配器: " << std::fixed << std::setprecision(2) << std_mt_time.count() << " ms" << std::endl;
    std::cout << "多线程内存池: " << std::fixed << std::setprecision(2) << pool_mt_time.count() << " ms" << std::endl;
    std::cout << "多线程性能提升: " << std::fixed << std::setprecision(2) << (std_mt_time.count() / pool_mt_time.count()) << "x" << std::endl;
}

int main() {
    // 测试日志模块
    test_logger();
    
    // 测试线程池
    test_thread_pool();
    
    // 测试内存池
    test_memory_pool();
    
    // 测试定时器
    test_timer();
    
    // 测试IPC通信
    test_ipc_communication();
    
    // 性能测试
    benchmark_memory_pool();
    
    return 0;
}
