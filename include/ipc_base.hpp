#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <sys/eventfd.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <filesystem>
#include <errno.h>
#include "ipc_packet.hpp"

namespace util {
namespace ipc {

enum class IPC_TYPE {
    IPC_REQUEST,
    IPC_RESPONSE
};

enum class IPC_HANDLE_TYPE {
    IPC_HANDLE_UNKNOWN,
    IPC_HANDLE_READ,
    IPC_HANDLE_WRITE
};

class IPCHandlerBase {
public:
    IPCHandlerBase(const std::string& ipc_name, IPC_TYPE type);
    virtual ~IPCHandlerBase();

    virtual bool Init();
    virtual bool Uninit();
    virtual void Start();
    virtual void Stop();

protected:
    virtual bool ProcessData() = 0;
    virtual bool HasDataToWrite() const;
    
    void SetHandleType(IPC_HANDLE_TYPE handle_type) { handle_type_ = handle_type; }
    int GetControlFD() const { return control_fd_; }
    int GetFD() const { return fd_; }

private:
    void Loop();
    void GetPipePath(std::string& pipe_path) const;
    bool CreateFifo(const std::string& fifo_path);

    std::string ipc_name_;
    std::string pipe_path_;
    IPC_TYPE type_;
    IPC_HANDLE_TYPE handle_type_;
    
    int fd_;
    int control_fd_;
    
    std::thread worker_thread_;
    std::atomic<bool> running_;
};

class IPCSink {
public:
    virtual ~IPCSink() = default;
    virtual void ReceiveMsg(const IPCPacket& packet) = 0;
};

} // namespace ipc
} // namespace util