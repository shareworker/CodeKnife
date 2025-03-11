#include "ipc_implement.hpp"
#include <iostream>

namespace util {
namespace ipc {

IPCImplement::IPCImplement() : ipc_name_(""), is_server_(false) {
    // Default constructor
}

void IPCImplement::setIpcName(const std::string& name) {
    ipc_name_ = name;
}

void IPCImplement::setIsServer(bool is_server) {
    is_server_ = is_server;
}

void IPCImplement::start() {
    if (ipc_name_.empty()) {
        LOG_ERROR("IPC name not set");
        return;
    }

    LOG_DEBUG("Starting IPC implementation (%s mode) with name: %s", 
        is_server_ ? "server" : "client", 
        ipc_name_.c_str());

    if (is_server_) {
        writer_ = std::make_unique<IPCWriter>(ipc_name_, IPC_TYPE::IPC_RESPONSE, IPC_HANDLE_TYPE::IPC_HANDLE_WRITE);
        reader_ = std::make_unique<IPCReader>(ipc_name_, IPC_TYPE::IPC_REQUEST, IPC_HANDLE_TYPE::IPC_HANDLE_READ);
    } else {
        writer_ = std::make_unique<IPCWriter>(ipc_name_, IPC_TYPE::IPC_REQUEST, IPC_HANDLE_TYPE::IPC_HANDLE_WRITE);
        reader_ = std::make_unique<IPCReader>(ipc_name_, IPC_TYPE::IPC_RESPONSE, IPC_HANDLE_TYPE::IPC_HANDLE_READ);
    }
    
    if (!reader_ || !writer_) {
        LOG_ERROR("Failed to create reader or writer");
        return;
    }

    LOG_DEBUG("Initializing reader...");
    reader_->SetSink(this);
    if (!reader_->Init()) {
        LOG_ERROR("Failed to initialize reader");
        return;
    }

    LOG_DEBUG("Initializing writer...");
    if (!writer_->Init()) {
        LOG_ERROR("Failed to initialize writer");
        reader_->Uninit();
        return;
    }

    LOG_DEBUG("Starting reader and writer...");
    reader_->Start();
    writer_->Start();

    LOG_DEBUG("IPC implementation started successfully");
}

void IPCImplement::stop() {
    if (reader_) {
        reader_->Stop();
        reader_->Uninit();
    }

    if (writer_) {
        writer_->Stop();
        writer_->Uninit();
    }
}

void IPCImplement::sendMessage(const std::string& message) {
    if (!writer_) {
        LOG_ERROR("Cannot send message: writer not initialized");
        return;
    }

    static uint32_t sequence = 0;
    auto packet = std::make_shared<IPCPacket>(
        MessageType::REQUEST,
        sequence++,
        message.c_str(),
        message.size()
    );

    LOG_DEBUG("Sending message [seq:%u]: %s", sequence-1, message.c_str());
    
    if (!writer_->AddPacket(packet)) {
        LOG_ERROR("Failed to add packet to writer queue");
        return;
    }
}

void IPCImplement::recvMessage() {
    if (!reader_) {
        LOG_ERROR("Cannot receive message: reader not initialized");
        return;
    }

    LOG_DEBUG("Processing incoming messages...");
    if (!reader_->ProcessData()) {
        LOG_ERROR("Failed to process reader data");
    }
}

void IPCImplement::ReceiveMsg(const IPCPacket& packet) {
    std::string msg((const char*)packet.GetPayload(), packet.GetPayloadLength());
    LOG_DEBUG("Received message [seq:%u, type:%d]: %s", 
              packet.GetSequenceNumber(),
              static_cast<int>(packet.GetMessageType()),
              msg.c_str());
}

} // namespace ipc
} // namespace util