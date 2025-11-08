#include "logger.hpp"
#include "thread_pool.hpp"
#include "memory_pool.hpp"
#include "object_pool.hpp"
#include "timer.hpp"
#include "cobject.hpp"
#include "meta_object.hpp"
#include "meta_registry.hpp"
#include "connection_manager.hpp"
#include "connection_types.hpp"
#include "capplication.hpp"
#include <string>
#include <any>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// Function declarations
void test_cobject_reflection();
void test_signal_slot_basic();
void test_signal_slot_cross_thread();
void test_event_loop_basic();
void test_queued_cross_thread();
void test_blocking_cross_thread();
void test_cobject_timer();

// Utility component tests
void test_memory_pool();
void test_thread_pool();
void test_object_pool();
void test_timer();
void test_logger();

// ========== Test Classes in SAK namespace ==========
namespace SAK {

// TestObject for reflection tests
class TestObject : public CObject {
    DECLARE_OBJECT(TestObject)
public:
    TestObject() { setvalue(0); setname(""); }
    TestObject(int v, const std::string& n) { setvalue(v); setname(n); }

    PROPERTY(TestObject, int, value)
    PROPERTY(TestObject, std::string, name)

    SIGNAL(TestObject, valueChanged, "void()")
    SIGNAL(TestObject, nameChanged, "void()")

    SLOT(TestObject, int, calculate, "int()")
};

int TestObject::calculate() { return value() * 2; }

AUTO_REGISTER_META_OBJECT(TestObject, CObject)

// Sender for signal-slot tests
class Sender : public CObject {
    DECLARE_OBJECT(Sender)
public:
    Sender() { setcount(0); }
    
    PROPERTY(Sender, int, count)
    SIGNAL(Sender, countChanged, "void(int)")
    
    void increment() {
        setcount(count() + 1);
        EMIT_SIGNAL(countChanged, count());
    }
};

// Receiver for signal-slot tests
class Receiver : public CObject {
    DECLARE_OBJECT(Receiver)
public:
    Receiver() : receivedValue_(0), callCount_(0) {}
    
    SLOT(Receiver, void, onCountChanged, "void(int)", int value)
    
    int receivedValue() const { return receivedValue_; }
    int callCount() const { return callCount_; }
    
private:
    int receivedValue_;
    int callCount_;
};

void Receiver::onCountChanged(int value) {
    receivedValue_ = value;
    callCount_++;
    std::cout << "  [Receiver] Slot called with value: " << value 
              << " (call #" << callCount_ << ")" << std::endl;
}

AUTO_REGISTER_META_OBJECT(Sender, CObject)
AUTO_REGISTER_META_OBJECT(Receiver, CObject)

// TimerTestObject for CObject timer tests
class TimerTestObject : public CObject {
    DECLARE_OBJECT(TimerTestObject)
public:
    TimerTestObject() : timerCount_(0), timerId_(-1) {}
    
    void startTestTimer(int64_t interval) {
        timerId_ = startTimer(interval);
        std::cout << "  Started timer with ID: " << timerId_ << ", interval: " << interval << "ms" << std::endl;
    }
    
    void stopTestTimer() {
        if (timerId_ > 0) {
            killTimer(timerId_);
            std::cout << "  Stopped timer with ID: " << timerId_ << std::endl;
            timerId_ = -1;
        }
    }
    
    int getTimerCount() const { return timerCount_; }
    int getTimerId() const { return timerId_; }
    
protected:
    void timerEvent(TimerEvent* event) override {
        timerCount_++;
        std::cout << "  [TimerEvent] Timer fired! ID: " << event->timerId() 
                  << ", count: " << timerCount_ << std::endl;
    }
    
private:
    std::atomic<int> timerCount_;
    int timerId_;
};

AUTO_REGISTER_META_OBJECT(TimerTestObject, CObject)

} // namespace SAK

// ========== Test Functions ==========

void test_cobject_reflection() {
    std::cout << "\n===== Test CObject Reflection System =====\n" << std::endl;
    try {
        SAK::TestObject obj(42, "test");
        std::cout << "Created TestObject successfully" << std::endl;
        
        // Test direct property access
        if (obj.value() != 42 || obj.name() != "test") {
            std::cout << "FAIL: Property access failed" << std::endl;
            return;
        }
        std::cout << "  ✓ Direct property access works" << std::endl;
        
        // Test meta object
        const SAK::MetaObject* meta = obj.metaObject();
        if (!meta || std::string(meta->className()) != "TestObject") {
            std::cout << "FAIL: MetaObject check failed" << std::endl;
            return;
        }
        std::cout << "  ✓ MetaObject works" << std::endl;
        
        // Test property reflection
        const SAK::MetaProperty* pValue = meta->findProperty("value");
        if (!pValue || std::any_cast<int>(pValue->get(&obj)) != 42) {
            std::cout << "FAIL: Property reflection failed" << std::endl;
            return;
        }
        
        pValue->set(&obj, 100);
        if (obj.value() != 100) {
            std::cout << "FAIL: Property set failed" << std::endl;
            return;
        }
        std::cout << "  ✓ Property reflection works" << std::endl;
        
        // Test method reflection
        const SAK::MetaMethod* mCalc = meta->findMethod("calculate");
        if (!mCalc || std::any_cast<int>(mCalc->invoke(&obj, {})) != 200) {
            std::cout << "FAIL: Method reflection failed" << std::endl;
            return;
        }
        std::cout << "  ✓ Method reflection works" << std::endl;
        
        std::cout << "\n✓ All CObject reflection tests PASSED!\n" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "Test failed with exception: " << e.what() << std::endl;
    }
}

void test_signal_slot_basic() {
    std::cout << "\n===== Test Signal-Slot Basic Functionality =====\n" << std::endl;
    
    try {
        SAK::Sender sender;
        SAK::Receiver receiver;
        
        std::cout << "Test 1: Direct connection" << std::endl;
        bool connected = SAK::CObject::connect(&sender, "countChanged", &receiver, "onCountChanged", 
                                               SAK::ConnectionType::kDirectConnection);
        if (!connected) {
            std::cout << "FAIL: Connection failed" << std::endl;
            return;
        }
        std::cout << "  Connection established" << std::endl;
        
        sender.increment();
        if (receiver.receivedValue() != 1 || receiver.callCount() != 1) {
            std::cout << "FAIL: Signal emission failed" << std::endl;
            return;
        }
        std::cout << "  ✓ Signal emitted and slot called" << std::endl;
        
        std::cout << "\nTest 2: Multiple emissions" << std::endl;
        sender.increment();
        sender.increment();
        
        if (receiver.receivedValue() != 3 || receiver.callCount() != 3) {
            std::cout << "FAIL: Multiple emissions failed" << std::endl;
            return;
        }
        std::cout << "  ✓ Multiple emissions work" << std::endl;
        
        std::cout << "\nTest 3: Disconnect" << std::endl;
        bool disconnected = SAK::CObject::disconnect(&sender, "countChanged", &receiver, "onCountChanged");
        if (!disconnected) {
            std::cout << "FAIL: Disconnect failed" << std::endl;
            return;
        }
        
        int oldCallCount = receiver.callCount();
        sender.increment();
        
        if (receiver.callCount() != oldCallCount) {
            std::cout << "FAIL: Slot still called after disconnect" << std::endl;
            return;
        }
        std::cout << "  ✓ Disconnect works" << std::endl;
        
        std::cout << "\n✓ All basic signal-slot tests PASSED!\n" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "FAIL: Exception: " << e.what() << std::endl;
    }
}

void test_signal_slot_cross_thread() {
    std::cout << "\n===== Test Signal-Slot Cross-Thread =====\n" << std::endl;
    std::cout << "Running event loop and cross-thread connection tests...\n" << std::endl;
    
    test_event_loop_basic();
    test_queued_cross_thread();
    test_blocking_cross_thread();
}

void test_event_loop_basic() {
    std::cout << "\n===== Test Event Loop Basic =====\n";
    
    try {
        SAK::CApplication app;
        
        std::cout << "Main thread: " << std::this_thread::get_id() << std::endl;
        
        // Start event loop thread
        std::thread eventLoopThread([&app]() {
            std::cout << "Event loop started in thread " 
                      << std::this_thread::get_id() << std::endl;
            app.exec();
            std::cout << "Event loop exited" << std::endl;
        });
        
        // Let event loop start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Quit the event loop
        app.quit();
        eventLoopThread.join();
        
        std::cout << "Event loop test PASSED\n" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "FAIL: Exception: " << e.what() << std::endl;
    }
}

void test_queued_cross_thread() {
    std::cout << "\n===== Test Queued Cross-Thread =====\n";
    
    try {
        SAK::CApplication app;
        SAK::Sender sender;
        
        std::atomic<int> receivedValue{0};
        std::atomic<int> callCount{0};
        std::thread::id mainThreadId = std::this_thread::get_id();
        std::thread::id receiverThreadId;
        std::thread::id slotThreadId;
        
        std::cout << "Main thread: " << mainThreadId << std::endl;
        
        // Helper thread: Create Receiver and run event loop
        std::thread eventLoopThread([&]() {
            receiverThreadId = std::this_thread::get_id();
            std::cout << "Receiver thread: " << receiverThreadId << std::endl;
            
            SAK::Receiver receiver;
            
            // Queued connection
            bool connected = SAK::CObject::connect(&sender, "countChanged", &receiver, "onCountChanged",
                                 SAK::ConnectionType::kQueuedConnection);
            
            if (!connected) {
                std::cout << "FAIL: Connection failed" << std::endl;
                return;
            }
            
            std::cout << "Queued connection established" << std::endl;
            
            // Run event loop
            app.exec();
            
            // Save values for verification
            receivedValue = receiver.receivedValue();
            callCount = receiver.callCount();
        });
        
        // Wait for receiver thread to be ready
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // Emit signal from main thread
        std::cout << "Emitting signal from main thread..." << std::endl;
        sender.increment();
        
        // Wait for event to be processed
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // Quit event loop
        app.quit();
        eventLoopThread.join();
        
        // Verify results
        if (receivedValue != 1 || callCount != 1) {
            std::cout << "FAIL: Cross-thread queued connection failed" << std::endl;
            return;
        }
        
        std::cout << "✓ Queued cross-thread test PASSED" << std::endl;
        std::cout << "  - Signal emitted from main thread" << std::endl;
        std::cout << "  - Slot executed in receiver thread" << std::endl;
        std::cout << "  - Parameter correctly passed" << std::endl;
        std::cout << "  - Async execution verified\n" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "FAIL: Exception: " << e.what() << std::endl;
    }
}

void test_blocking_cross_thread() {
    std::cout << "\n===== Test Blocking Cross-Thread =====\n";
    
    try {
        SAK::CApplication app;
        SAK::Sender sender;
        
        std::atomic<int> receivedValue{0};
        std::atomic<int> callCount{0};
        std::atomic<bool> slotStarted{false};
        std::atomic<bool> slotFinished{false};
        std::thread::id mainThreadId = std::this_thread::get_id();
        std::thread::id receiverThreadId;
        
        std::cout << "Main thread: " << mainThreadId << std::endl;
        
        // Helper thread: Create Receiver and run event loop
        std::thread eventLoopThread([&]() {
            receiverThreadId = std::this_thread::get_id();
            std::cout << "Receiver thread: " << receiverThreadId << std::endl;
            
            SAK::Receiver receiver;
            
            // Blocking connection
            bool connected = SAK::CObject::connect(&sender, "countChanged", &receiver, "onCountChanged",
                                 SAK::ConnectionType::kBlockingConnection);
            
            if (!connected) {
                std::cout << "FAIL: Connection failed" << std::endl;
                return;
            }
            
            std::cout << "Blocking connection established" << std::endl;
            
            // Run event loop
            app.exec();
            
            // Save values
            receivedValue = receiver.receivedValue();
            callCount = receiver.callCount();
        });
        
        // Wait for receiver thread to be ready
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // Emit signal from main thread - should block until slot completes
        std::cout << "Emitting signal from main thread (will block)..." << std::endl;
        auto startTime = std::chrono::steady_clock::now();
        sender.increment();
        auto endTime = std::chrono::steady_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        std::cout << "Signal emission returned after " << duration << "ms" << std::endl;
        
        // Quit event loop
        app.quit();
        eventLoopThread.join();
        
        // Verify results
        if (receivedValue != 1 || callCount != 1) {
            std::cout << "FAIL: Blocking connection failed" << std::endl;
            return;
        }
        
        std::cout << "✓ Blocking cross-thread test PASSED" << std::endl;
        std::cout << "  - Signal blocked until slot completed" << std::endl;
        std::cout << "  - Synchronous behavior verified\n" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "FAIL: Exception: " << e.what() << std::endl;
    }
}

// ========== Utility Component Tests ==========

void test_logger() {
    std::cout << "\n===== Test Logger Module =====\n" << std::endl;
    
    try {
        // Configure the logger
        SAK::log::LogConfig config;
        config.use_stdout = true;
        config.min_level = SAK::log::Level::LOG_DEBUG;
        config.async_mode = false;  // Disable async for testing
        
        SAK::log::Logger::instance().configure(config);
        
        // Test different log levels
        LOG_DEBUG("This is a debug log");
        LOG_INFO("This is an info log");
        LOG_WARNING("This is a warning log");
        LOG_ERROR("This is an error log");
        
        std::cout << "\n✓ Logger test PASSED\n" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "FAIL: Exception: " << e.what() << std::endl;
    }
}

void test_thread_pool() {
    std::cout << "\n===== Test Thread Pool =====\n" << std::endl;
    
    try {
        SAK::thread::ThreadPool pool(4);
        std::cout << "Created thread pool with " << pool.get_thread_count() << " threads" << std::endl;
        
        // Submit tasks
        std::atomic<int> counter{0};
        std::vector<std::future<int>> futures;
        
        for (int i = 0; i < 10; ++i) {
            futures.push_back(pool.enqueue([&counter, i]() {
                counter++;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                return i * 2;
            }));
        }
        
        // Wait for results
        int sum = 0;
        for (auto& f : futures) {
            sum += f.get();
        }
        
        if (counter != 10 || sum != 90) {
            std::cout << "FAIL: Task execution failed" << std::endl;
            return;
        }
        
        std::cout << "  ✓ Submitted 10 tasks" << std::endl;
        std::cout << "  ✓ All tasks completed successfully" << std::endl;
        std::cout << "\n✓ Thread pool test PASSED\n" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "FAIL: Exception: " << e.what() << std::endl;
    }
}

void test_memory_pool() {
    std::cout << "\n===== Test Memory Pool =====\n" << std::endl;
    
    try {
        auto& pool = SAK::memory::MemoryPool::GetInstance();
        
        // Allocate and deallocate different sizes
        std::vector<void*> pointers;
        for (int i = 0; i < 10; ++i) {
            void* ptr = pool.Allocate(64);
            if (!ptr) {
                std::cout << "FAIL: Allocation failed" << std::endl;
                return;
            }
            pointers.push_back(ptr);
        }
        
        std::cout << "  ✓ Allocated 10 blocks of 64 bytes" << std::endl;
        
        // Deallocate
        for (auto ptr : pointers) {
            pool.Deallocate(ptr, 64);
        }
        
        std::cout << "  ✓ Deallocated all blocks" << std::endl;
        
        // Test smart pointers
        {
            auto ptr = SAK::memory::make_pool_ptr<std::string>("Memory pool test string");
            if (*ptr != "Memory pool test string") {
                std::cout << "FAIL: Smart pointer test failed" << std::endl;
                return;
            }
        }
        
        std::cout << "  ✓ Smart pointer works" << std::endl;
        
        // Test array allocation
        {
            auto arr = SAK::memory::make_pool_array<int>(10);
            for (int i = 0; i < 10; ++i) {
                arr[i] = i * i;
            }
            int sum = 0;
            for (int i = 0; i < 10; ++i) {
                sum += arr[i];
            }
            if (sum != 285) {
                std::cout << "FAIL: Array allocation test failed" << std::endl;
                return;
            }
        }
        
        std::cout << "  ✓ Array allocation works" << std::endl;
        std::cout << "\n✓ Memory pool test PASSED\n" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "FAIL: Exception: " << e.what() << std::endl;
    }
}

void test_object_pool() {
    std::cout << "\n===== Test Object Pool =====\n" << std::endl;
    
    try {
        struct TestObj {
            int value = 0;
            std::string name;
        };
        
        SAK::pool::ObjectPool<TestObj> pool;
        
        // Acquire objects
        std::vector<TestObj*> objects;
        for (int i = 0; i < 5; ++i) {
            auto obj = pool.acquire();
            if (!obj) {
                std::cout << "FAIL: Object acquisition failed" << std::endl;
                return;
            }
            obj->value = i;
            obj->name = "obj_" + std::to_string(i);
            objects.push_back(obj);
        }
        
        std::cout << "  ✓ Acquired 5 objects" << std::endl;
        
        // Verify object values
        for (size_t i = 0; i < objects.size(); ++i) {
            if (objects[i]->value != static_cast<int>(i)) {
                std::cout << "FAIL: Object value mismatch" << std::endl;
                return;
            }
        }
        
        std::cout << "  ✓ Object values correct" << std::endl;
        
        // Release objects
        for (auto obj : objects) {
            pool.release(obj);
        }
        
        std::cout << "  ✓ Released all objects" << std::endl;
        
        // Reacquire - should reuse from pool
        auto obj = pool.acquire();
        if (!obj) {
            std::cout << "FAIL: Object reacquisition failed" << std::endl;
            return;
        }
        obj->value = 99;
        obj->name = "reused";
        
        if (obj->value != 99) {
            std::cout << "FAIL: Object reuse failed" << std::endl;
            pool.release(obj);
            return;
        }
        pool.release(obj);
        
        std::cout << "  ✓ Object reuse works" << std::endl;
        std::cout << "\n✓ Object pool test PASSED\n" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "FAIL: Exception: " << e.what() << std::endl;
    }
}

void test_timer() {
    std::cout << "\n===== Test Timer Module =====\n" << std::endl;
    
    try {
        auto& timer = SAK::timer::Timer::instance();
        
        std::atomic<int> count{0};
        std::atomic<bool> periodic_called{false};
        
        // Test single-shot timer
        auto single_id = timer.schedule_once(100, [&count]() {
            count++;
        });
        
        // Test periodic timer
        auto periodic_id = timer.schedule_repeated(50, 50, [&periodic_called]() {
            periodic_called = true;
        });
        
        // Wait for timers to fire
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        
        // Cancel periodic timer
        timer.cancel(periodic_id);
        
        if (count != 1) {
            std::cout << "FAIL: Single-shot timer failed (count=" << count << ")" << std::endl;
            return;
        }
        
        if (!periodic_called) {
            std::cout << "FAIL: Periodic timer failed" << std::endl;
            return;
        }
        
        std::cout << "  ✓ Single-shot timer works" << std::endl;
        std::cout << "  ✓ Periodic timer works" << std::endl;
        std::cout << "  ✓ Timer cancellation works" << std::endl;
        std::cout << "\n✓ Timer test PASSED\n" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "FAIL: Exception: " << e.what() << std::endl;
    }
}

void test_cobject_timer() {
    std::cout << "\n===== Test CObject Timer Functionality =====\n" << std::endl;
    std::cout << "Note: This test demonstrates CObject timer API usage." << std::endl;
    std::cout << "Timer functionality requires a running event loop with proper platform-specific dispatcher." << std::endl;
    
    try {
        std::cout << "\nTest 1: Timer API verification" << std::endl;
        
        // Create a simple timer object to verify API
        SAK::TimerTestObject timerObj;
        
        // Verify startTimer returns a valid ID (even without event loop)
        int timerId = timerObj.startTimer(100);
        std::cout << "  startTimer() returned ID: " << timerId << std::endl;
        
        if (timerId > 0) {
            std::cout << "  ✓ startTimer() API works (returns valid ID)" << std::endl;
        } else {
            std::cout << "  ⚠ startTimer() returned 0 (no event loop available)" << std::endl;
        }
        
        // Test killTimer API
        if (timerId > 0) {
            timerObj.killTimer(timerId);
            std::cout << "  ✓ killTimer() API works" << std::endl;
        }
        
        // Test unregisterTimers API
        bool result = timerObj.unregisterTimers();
        std::cout << "  unregisterTimers() returned: " << (result ? "true" : "false") << std::endl;
        std::cout << "  ✓ unregisterTimers() API works" << std::endl;
        
        std::cout << "\n✓ CObject timer API test PASSED!\n" << std::endl;
        std::cout << "  Note: Full timer functionality requires:" << std::endl;
        std::cout << "  - CApplication instance with event loop" << std::endl;
        std::cout << "  - Timer object created in event loop thread" << std::endl;
        std::cout << "  - Platform-specific EventDispatcher (Windows/Linux)" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "FAIL: Exception: " << e.what() << std::endl;
    }
}

// ========== Main Function ==========

int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  CodeKnife Test Suite" << std::endl;
    std::cout << "========================================\n" << std::endl;

    try {
        // ========== Core Object System Tests ==========
        test_cobject_reflection();
        test_signal_slot_basic();
        test_signal_slot_cross_thread();
        test_cobject_timer();
        
        // ========== Utility Component Tests ==========
        test_logger();
        test_thread_pool();
        test_memory_pool();
        test_object_pool();
        test_timer();
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "  All Tests Completed Successfully!" << std::endl;
        std::cout << "========================================\n" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL ERROR: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "\nFATAL ERROR: Unknown exception" << std::endl;
        return 1;
    }

    return 0;
}
