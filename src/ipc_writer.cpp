#include "ipc_writer.hpp"
#include "logger.hpp"
#include "ipc_packet.hpp"
#include <climits>
#include <unistd.h>
#include <sys/types.h>
#include <cstring>

namespace util {
namespace ipc {

IPCWriter::IPCWriter(const std::string& ipc_name, IPC_TYPE type, IPC_HANDLE_TYPE handle_type) : IPCHandlerBase(ipc_name, type)
{
    SetHandleType(handle_type);
}

IPCWriter::~IPCWriter() {
    Uninit();
}

bool IPCWriter::Init() {
    IPCHandlerBase::Init();

    // pid_t pid = getpid();
    // std::shared_ptr<IPCPacket> packet = std::make_shared<IPCPacket>(MessageType::REQUEST, 0, &pid, sizeof(pid_t));

    // AddPacket(packet);
    return true;
}

bool IPCWriter::Uninit() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while(!packet_queue_.empty()) {
            packet_queue_.pop();
        }
    }
    IPCHandlerBase::Uninit();
    return true;
}

bool IPCWriter::AddPacket(std::shared_ptr<IPCPacket> packet) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    packet_queue_.push(packet);
    int control_fd = GetControlFD();
    if (control_fd >= 0) {
        uint64_t event = 1;
        if (write(control_fd, &event, sizeof(event)) != sizeof(event)) {
            LOG_ERROR("Failed to write control event");
            return false;
        }
    }
    return true;
}

bool IPCWriter::ProcessData() {
    return WriteData();
}

bool IPCWriter::HasDataToWrite() const {
    return !packet_queue_.empty();
}

bool IPCWriter::WriteData() {
    LOG_DEBUG("Writing data");
    std::shared_ptr<IPCPacket> packet;  
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (packet_queue_.empty()) {
            return true;
        }
        packet = packet_queue_.front();
        packet_queue_.pop();
    }

    if (!packet->IsValid()) {
        LOG_ERROR("Invalid packet");
        return false;
    }

    int fd = GetFD();
    
    // Write packet header first
    if (write(fd, &packet->GetHeader(), sizeof(PacketHeader)) != sizeof(PacketHeader)) {
        LOG_ERROR("Failed to write packet header");
        return false;
    }

    // Then write payload if present
    if (packet->GetPayloadLength() > 0 && packet->GetPayload() != nullptr) {
        if (write(fd, packet->GetPayload(), packet->GetPayloadLength()) != packet->GetPayloadLength()) {
            LOG_ERROR("Failed to write packet payload");
            return false;
        }
    }

    // Write checksum field
    uint32_t checksum = packet->GetChecksum();
    if (write(fd, &checksum, sizeof(uint32_t)) != sizeof(uint32_t)) {
        LOG_ERROR("Failed to write checksum");
        return false;
    }

    return true;
}
} // namespace ipc
} // namespace util