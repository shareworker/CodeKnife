#pragma once

#include <functional>
#include <queue>
#include <memory>
#include <mutex>
#include "ipc_base.hpp"
#include "ipc_packet.hpp"

namespace util {
namespace ipc {

class IPCWriter : public IPCHandlerBase {
public:
    IPCWriter(const std::string& ipc_name, IPC_TYPE type, IPC_HANDLE_TYPE handle_type);
    ~IPCWriter();

    bool Init() override;
    bool Uninit() override;

    bool AddPacket(std::shared_ptr<IPCPacket> packet);

protected:
    bool ProcessData() override;
    bool HasDataToWrite() const override;

private:
    bool WriteData();

private:
    std::queue<std::shared_ptr<IPCPacket>> packet_queue_;
    std::mutex queue_mutex_;
};

} // namespace ipc
} // namespace util 