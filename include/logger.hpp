#pragma once

#include <string>
#include <fstream>
#include <memory>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <sstream>
// Platform-specific includes
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <process.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

// Force C++20 std::filesystem usage
#define _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING
#include <filesystem>
namespace fs = std::filesystem;

namespace SAK {
namespace log {

enum class Level {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR
};

struct LogConfig {
    std::string log_dir = "/tmp/.util_log";
    bool use_stdout = false;
    Level min_level = Level::LOG_DEBUG;
    size_t max_file_size = 10 * 1024 * 1024;  // 10MB
    size_t max_files = 5;
    bool async_mode = true;
    size_t flush_interval_ms = 1000;
};

class Logger {
public:
    static Logger& instance() {
        static Logger instance;
        return instance;
    }

    void configure(const LogConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        bool was_async = config_.async_mode;
        config_ = config;
        init();
        // Manage async thread based on mode transition
        if (was_async && !config_.async_mode) {
            // Stop async thread if switching to sync mode
            if (async_thread_.joinable()) {
                stop_async_thread();
            }
        } else if (!was_async && config_.async_mode) {
            // Start async thread if switching to async mode
            if (!async_thread_.joinable()) {
                start_async_thread();
            }
        }
    }

    void log(Level level, const char* file, const char* func, int line, const char* fmt, ...) {
        if (level < config_.min_level) return;
        if (!init_success_) return;

        char buffer[256];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);

        auto log_entry = format_log(level, file, func, line, buffer);
        
        if (config_.async_mode) {
            enqueue_log(log_entry);
        } else {
            write_log(log_entry);
        }
    }

    ~Logger() noexcept {
        try {
            // Always stop/join async thread if running to avoid hangs
            std::cout << "Logger destructor called" << std::endl;
            std::cout.flush();
            if (async_thread_.joinable()) {
                std::cout << "Stopping async thread in destructor" << std::endl;
                std::cout.flush();
                stop_async_thread();
                std::cout << "Async thread stopped in destructor" << std::endl;
                std::cout.flush();
            }
            std::cout << "Logger destructor finished" << std::endl;
            std::cout.flush();
        } catch (...) {
            // Destructors must not throw - this is critical for program stability
            try {
                std::cout << "Logger destructor exception caught - terminating gracefully" << std::endl;
                std::cout.flush();
            } catch (...) {
                // Ignore even cout failures at this point
            }
        }
    }

private:
    Logger() : config_(), stop_flag_(false) {
        init();
        if (config_.async_mode) {
            start_async_thread();
        }
    }

    void init() {
        log_dir_ = fs::path(config_.log_dir);
        max_file_size_ = config_.max_file_size;
        
        if (config_.use_stdout) {
            init_success_ = true;
            return;
        }

        if (!fs::exists(log_dir_)) {
            fs::create_directories(log_dir_);
        }

        open_new_log_file();
        init_success_ = file_stream_ && file_stream_->is_open();
    }

    void rotate_log_files() {
        if (file_stream_) {
            file_stream_->close();
        }
        
        // Simple rotation - create new file with timestamp
        open_new_log_file();
    }

    fs::path get_log_path() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
#ifdef _WIN32
        auto pid = _getpid();
#else
        auto pid = getpid();
#endif
        
        std::ostringstream filename;
        filename << "log_" << pid << "_" << time_t << ".log";
        return log_dir_ / filename.str();
    }

    void open_new_log_file() {
        current_log_path_ = get_log_path();
        file_stream_ = std::make_unique<std::ofstream>(
            current_log_path_, std::ios::out | std::ios::app);
    }

    std::string format_log(Level level, const char* file, const char* func, int line, const std::string& message) {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        char time_str[20];
#ifdef _WIN32
        struct tm tm_info;
        localtime_s(&tm_info, &t);
        std::strftime(time_str, sizeof(time_str), "%Y%m%d%H%M%S", &tm_info);
#else
        std::strftime(time_str, sizeof(time_str), "%Y%m%d%H%M%S", std::localtime(&t));
#endif

        std::string level_str;
        switch(level) {
            case Level::LOG_DEBUG: level_str = "DEBUG"; break;
            case Level::LOG_INFO: level_str = "INFO"; break;
            case Level::LOG_WARNING: level_str = "WARNING"; break;
            case Level::LOG_ERROR: level_str = "ERROR"; break;
        }

        std::ostringstream oss;
        oss << "[" << time_str << "] "
            << "[" << level_str << "] "
#ifdef _WIN32
            << "[" << _getpid() << "] "
#else
            << "[" << getpid() << "] "
#endif
            << "[" << file << ":" << func << ":" << line << "] "
            << message << "\n";
        return oss.str();
    }

    void write_log(const std::string& log_entry) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (config_.use_stdout) {
            std::cout << log_entry;
            return;
        }

        if (file_stream_ && file_stream_->is_open()) {
            *file_stream_ << log_entry;
            file_stream_->flush();
            
            if (fs::exists(current_log_path_) && fs::file_size(current_log_path_) >= max_file_size_) {
                rotate_log_files();
            }
        }
    }

    void enqueue_log(const std::string& log_entry) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            log_queue_.push(log_entry);
        }
        queue_cv_.notify_one();
    }

    void start_async_thread() {
        if (!async_thread_.joinable()) {
            stop_flag_ = false;  // Reset stop flag for new thread
            async_thread_ = std::thread([this] { async_logging_thread(); });
        }
    }

    void stop_async_thread() {
        if (!stop_flag_.exchange(true)) {  // Only proceed if not already stopped
            queue_cv_.notify_one();
            if (async_thread_.joinable()) {
                try {
                    async_thread_.join();
                } catch (const std::exception& e) {
                    // Log join failure but continue shutdown
                    try {
                        std::cerr << "Logger async thread join failed: " << e.what() << std::endl;
                    } catch (...) {
                        // Ignore even logging failures during shutdown
                    }
                } catch (...) {
                    // Ignore unknown exceptions during thread join
                }
            }
        }
    }

    void async_logging_thread() {
        while (!stop_flag_) {
            std::vector<std::string> batch;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                if (log_queue_.empty()) {
                    queue_cv_.wait_for(lock, 
                        std::chrono::milliseconds(config_.flush_interval_ms),
                        [this] { return !log_queue_.empty() || stop_flag_; });
                }
                
                while (!log_queue_.empty()) {
                    batch.push_back(std::move(log_queue_.front()));
                    log_queue_.pop();
                }
            }

            for (const auto& entry : batch) {
                write_log(entry);
            }
        }
    }

    LogConfig config_;
    std::unique_ptr<std::ofstream> file_stream_;
    bool init_success_ = false;
    fs::path log_dir_;
    fs::path current_log_path_;
    size_t max_file_size_ = 10 * 1024 * 1024;  // 10MB

    // Async logging members
    std::queue<std::string> log_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::thread async_thread_;
    std::atomic<bool> stop_flag_;
    
    std::mutex mutex_;
};

} // namespace log
} // namespace SAK

#define LOG_DEBUG(fmt, ...) \
    SAK::log::Logger::instance().log(SAK::log::Level::LOG_DEBUG, __FILE__, __FUNCTION__, __LINE__, fmt, ## __VA_ARGS__)

#define LOG_INFO(fmt, ...) \
    SAK::log::Logger::instance().log(SAK::log::Level::LOG_INFO, __FILE__, __FUNCTION__, __LINE__, fmt, ## __VA_ARGS__)

#define LOG_WARNING(fmt, ...) \
    SAK::log::Logger::instance().log(SAK::log::Level::LOG_WARNING, __FILE__, __FUNCTION__, __LINE__, fmt, ## __VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
    SAK::log::Logger::instance().log(SAK::log::Level::LOG_ERROR, __FILE__, __FUNCTION__, __LINE__, fmt, ## __VA_ARGS__)
