#pragma once
#include "logger.hpp"
#include "ipc_writer.hpp"
#include "ipc_reader.hpp"
#include <memory>

namespace util {
namespace ipc {

// Use the IPC_TYPE from ipc_base.hpp instead
// enum class IPC_TYPE {
//     IPC_REQUEST = 0,
//     SOCK_RESPONSE,
// };

class IPCImplement : public IPCSink {
public:
    IPCImplement();
    ~IPCImplement() = default;

    void start();
    void stop();
    void sendMessage(const std::string& message);
    void recvMessage();
    
    // Implement the IPCSink interface
    void ReceiveMsg(const IPCPacket& packet) override;
    void setIpcName(const std::string& ipc_name);
    void setIsServer(bool is_server);

private:
    std::unique_ptr<IPCWriter> writer_;
    std::unique_ptr<IPCReader> reader_;
    std::string ipc_name_;
    bool is_server_;
};

} // namespace ipc
} // namespace util