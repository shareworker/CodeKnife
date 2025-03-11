#include "ipc_base.hpp"
#include "logger.hpp"
#include <sys/epoll.h>

namespace util {
namespace ipc {

IPCHandlerBase::IPCHandlerBase(const std::string& ipc_name, IPC_TYPE type) : ipc_name_(ipc_name), 
    type_(type), fd_(-1), control_fd_(-1), running_(false), handle_type_(IPC_HANDLE_TYPE::IPC_HANDLE_UNKNOWN) {
    
}

IPCHandlerBase::~IPCHandlerBase() {
    Stop();
    Uninit();
}

bool IPCHandlerBase::Init() {
    GetPipePath(pipe_path_);
    if (!CreateFifo(pipe_path_)) {
        LOG_ERROR("Failed to create FIFO: %s", pipe_path_.c_str());
        return false;
    }
    
    // Special handling for writers to ensure the FIFO exists
    if (handle_type_ == IPC_HANDLE_TYPE::IPC_HANDLE_WRITE) {
        // For writers, attempt to open non-blocking first
        LOG_DEBUG("Opening pipe %s for writing (non-blocking)", pipe_path_.c_str());
        fd_ = open(pipe_path_.c_str(), O_RDWR | O_NONBLOCK);
        
        if (fd_ < 0 && errno == ENXIO) {
            // ENXIO means no reader connected - try blocking open to wait for a reader
            LOG_DEBUG("No readers connected yet, trying blocking open for writer");
            // Open in blocking mode to wait for reader to connect
            fd_ = open(pipe_path_.c_str(), O_WRONLY);
            if (fd_ >= 0) {
                // Successfully opened, set back to non-blocking
                int flags = fcntl(fd_, F_GETFL);
                if (fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
                    LOG_ERROR("Failed to set non-blocking mode: %d", errno);
                    close(fd_);
                    fd_ = -1;
                    return false;
                }
                LOG_DEBUG("Writer successfully opened pipe in non-blocking mode");
            }
        }
    } else if (handle_type_ == IPC_HANDLE_TYPE::IPC_HANDLE_READ) {
        // For readers, open in non-blocking mode
        LOG_DEBUG("Opening pipe %s for reading (non-blocking)", pipe_path_.c_str());
        fd_ = open(pipe_path_.c_str(), O_RDONLY | O_NONBLOCK);
    } else {
        LOG_ERROR("Invalid handle type");
        return false;
    }
    
    if (fd_ < 0) {
        LOG_ERROR("Failed to open pipe: %s, error: %d (%s)", pipe_path_.c_str(), errno, strerror(errno));
        return false;
    }
    
    LOG_DEBUG("Successfully opened pipe: %s, fd: %d", pipe_path_.c_str(), fd_);

    control_fd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (control_fd_ < 0) {
        // Error creating eventfd (replace with proper logging when available)
        return false;
    }
    return true;
}

bool IPCHandlerBase::Uninit() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    if (control_fd_ >= 0) {
        close(control_fd_);
        control_fd_ = -1;
    }

    unlink(pipe_path_.c_str());
    return true;
}

void IPCHandlerBase::Start() {
    if (running_) {
        // IPC handler already started (replace with proper logging when available)
        return;
    }
    
    running_ = true;
    worker_thread_ = std::thread(&IPCHandlerBase::Loop, this);
    // IPC handler started (replace with proper logging when available)
}

void IPCHandlerBase::Stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    // Signal the control fd to wake up the loop
    uint64_t value = 1;
    if (write(control_fd_, &value, sizeof(value)) != sizeof(value)) {
        // Error writing to control_fd (replace with proper logging when available)
    }
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    // IPC handler stopped (replace with proper logging when available)
}

void IPCHandlerBase::Loop() {
    struct epoll_event ev, events[2];
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        // Error creating epoll (replace with proper logging when available)
        return;
    }

    ev.events = handle_type_ == IPC_HANDLE_TYPE::IPC_HANDLE_READ ? EPOLLIN : EPOLLIN | EPOLLET;
    ev.data.fd = fd_;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd_, &ev) < 0) {
        // Error adding fd to epoll (replace with proper logging when available)
        return;
    }

    ev.events = EPOLLIN;
    ev.data.fd = control_fd_;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, control_fd_, &ev) < 0) {
        // Error adding control_fd to epoll (replace with proper logging when available)
        return;
    }
    
    while (running_) {
        if (handle_type_ == IPC_HANDLE_TYPE::IPC_HANDLE_WRITE) {
            struct epoll_event ev_modified;
            ev_modified.events = HasDataToWrite() ? EPOLLOUT : EPOLLET;
            ev_modified.data.fd = fd_;
            if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd_, &ev_modified) < 0) {
                // Error modifying fd in epoll (replace with proper logging when available)
                break;
            }
        }

        int nfds = epoll_wait(epoll_fd, events, 2, -1);
        if (nfds < 0) {
            // Error waiting for events (replace with proper logging when available)
            break;
        }
        
        for (int i = 0; i < nfds; i++) {
            if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                // Handle error or hangup
                running_ = false;
                break;
            }
            else if (events[i].events & (EPOLLOUT | EPOLLIN)) {
                if (events[i].data.fd == fd_) {
                    if (!ProcessData()) {
                        // Process data failed (replace with proper logging when available)
                        break;
                    }
                }
                else if (events[i].data.fd == control_fd_) {
                    uint64_t value;
                    if (read(control_fd_, &value, sizeof(value)) != sizeof(value)) {
                        LOG_ERROR("Failed to read control event");
                        running_ = false;
                        break;
                    }
                    if (!running_) {
                        break;
                    }
                }
            }
        }
        
    }
}

void IPCHandlerBase::GetPipePath(std::string& pipe_path) const {
    const char* user_home = getenv("HOME");
    std::string prefix = (user_home ? std::string(user_home) + "/.util/pipes/" : "");
    pipe_path = prefix + ipc_name_;
    switch (type_) {
        case IPC_TYPE::IPC_REQUEST:
            pipe_path += ".req";
            break;
        case IPC_TYPE::IPC_RESPONSE:
            pipe_path += ".res";
            break;
    }
    LOG_DEBUG("IPC pipe path: %s", pipe_path.c_str());
}

bool IPCHandlerBase::CreateFifo(const std::string& fifo_path) {
    if (access(fifo_path.c_str(), F_OK) == 0) {
        LOG_DEBUG("FIFO already exists: %s", fifo_path.c_str());
        
        // Check if it's actually a FIFO
        struct stat st;
        if (stat(fifo_path.c_str(), &st) != 0) {
            LOG_ERROR("Failed to stat existing file: %s, error: %d", fifo_path.c_str(), errno);
            return false;
        }
        
        if (!S_ISFIFO(st.st_mode)) {
            LOG_ERROR("%s exists but is not a FIFO, removing and recreating", fifo_path.c_str());
            if (unlink(fifo_path.c_str()) != 0) {
                LOG_ERROR("Failed to remove non-FIFO file: %s, error: %d", fifo_path.c_str(), errno);
                return false;
            }
        } else {
            return true; // It's a valid FIFO, we can use it
        }
    }
    
    // Create parent directory if it does not exist
    auto dir = std::filesystem::path(fifo_path).parent_path();
    if (!std::filesystem::exists(dir)) {
        LOG_DEBUG("Creating directory: %s", dir.string().c_str());
        if (!std::filesystem::create_directories(dir)) {
            LOG_ERROR("Failed to create directory: %s, error: %d", dir.string().c_str(), errno);
            return false;
        }
    }
    
    // Create the FIFO with read/write permissions
    LOG_DEBUG("Creating FIFO: %s", fifo_path.c_str());
    if (mkfifo(fifo_path.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP) < 0) {
        LOG_ERROR("Failed to create FIFO: %s, error: %d", fifo_path.c_str(), errno);
        return false;
    }
    
    LOG_DEBUG("Successfully created FIFO: %s", fifo_path.c_str());
    return true;
}

bool IPCHandlerBase::HasDataToWrite() const {
    return false;
}

} // namespace ipc
} // namespace util