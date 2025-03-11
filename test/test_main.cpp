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
    std::cout << "\n===== 测试IPC通信模块 =====\n" << std::endl;
    
    // 创建服务端和客户端IPC实现
    util::ipc::IPCImplement server_ipc;
    util::ipc::IPCImplement client_ipc;
    
    const std::string ipc_name = "test_ipc";
    
    // 配置服务端和客户端
    std::cout << "配置IPC实现..." << std::endl;
    server_ipc.setIpcName(ipc_name);
    server_ipc.setIsServer(true);
    
    client_ipc.setIpcName(ipc_name);
    client_ipc.setIsServer(false);
    
    // 启动IPC通信
    std::cout << "启动服务端..." << std::endl;
    server_ipc.start();
    
    std::cout << "启动客户端..." << std::endl;
    client_ipc.start();
    
    // 等待启动完成
    std::cout << "等待IPC通道建立连接..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 准备测试消息
    const std::string test_msg1 = "这是测试消息1";
    const std::string test_msg2 = "这是测试消息2";
    const std::string test_msg3 = "这是测试消息3";
    
    // 从客户端发送消息到服务端
    std::cout << "\n开始发送测试消息..." << std::endl;
    
    std::cout << "发送消息1: " << test_msg1 << std::endl;
    client_ipc.sendMessage(test_msg1);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // 主动处理服务端接收的消息
    // server_ipc.recvMessage();
    // std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::cout << "发送消息2: " << test_msg2 << std::endl;
    client_ipc.sendMessage(test_msg2);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // 主动处理服务端接收的消息
    // server_ipc.recvMessage();
    // std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    std::cout << "发送消息3: " << test_msg3 << std::endl;
    client_ipc.sendMessage(test_msg3);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // 主动处理服务端接收的消息
    // server_ipc.recvMessage();
    // std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 从服务端发送响应消息到客户端
    std::cout << "\n服务端发送响应消息..." << std::endl;
    server_ipc.sendMessage("服务端响应: 已收到所有消息");
    
    // 主动处理客户端接收的消息
    // std::this_thread::sleep_for(std::chrono::milliseconds(200));
    // client_ipc.recvMessage();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // 停止IPC通信
    std::cout << "\n停止IPC通信..." << std::endl;
    client_ipc.stop();
    server_ipc.stop();
    
    std::cout << "IPC通信测试完成" << std::endl;
}

// 性能测试：比较标准分配器和内存池
void benchmark_memory_pool() {
    std::cout << "\n===== 内存池性能测试 =====\n" << std::endl;
    
    const int iterations = 100000;
    const size_t alloc_size = 64;
    
    // 标准分配器
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        void* ptr = ::operator new(alloc_size);
        ::operator delete(ptr);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto std_duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    
    // 内存池
    auto& pool = util::memory::MemoryPool::GetInstance();
    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        void* ptr = pool.Allocate(alloc_size);
        pool.Deallocate(ptr, alloc_size);
    }
    end = std::chrono::high_resolution_clock::now();
    auto pool_duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    
    std::cout << "标准分配器: " << std_duration << " 微秒" << std::endl;
    std::cout << "内存池: " << pool_duration << " 微秒" << std::endl;
    
    if (pool_duration < std_duration) {
        double speedup = static_cast<double>(std_duration) / pool_duration;
        std::cout << "内存池快 " << std::fixed << std::setprecision(2) << speedup << " 倍" << std::endl;
    } else {
        double slowdown = static_cast<double>(pool_duration) / std_duration;
        std::cout << "内存池慢 " << std::fixed << std::setprecision(2) << slowdown << " 倍" << std::endl;
    }
    
    std::cout << "性能测试完成\n" << std::endl;
}

int main() {
    std::cout << "开始测试 libutil 库..." << std::endl;
    
    test_logger();
    test_thread_pool();
    test_memory_pool();
    test_timer();
    test_ipc_communication();
    benchmark_memory_pool();
    
    std::cout << "所有测试完成!" << std::endl;
    return 0;
}
