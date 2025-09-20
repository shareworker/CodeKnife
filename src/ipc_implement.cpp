#include "ipc_implement.hpp"
#include "ipc_packet.hpp"
#include "logger.hpp"
#include <thread>
#include <chrono>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>

namespace SAK {
namespace ipc {

IPCImplement::IPCImplement(const std::string& ipc_name, bool is_server)
    : ipc_name_(ipc_name), is_server_(is_server), running_(false), shared_memory_(nullptr) {}

IPCImplement::~IPCImplement() {
    stop();
}

void IPCImplement::setIpcName(const std::string& ipc_name) {
    if (running_) {
        LOG_ERROR("Cannot set IPC name while running");
        return;
    }
    ipc_name_ = ipc_name;
}

void IPCImplement::setIsServer(bool is_server) {
    if (running_) {
        LOG_ERROR("Cannot set server mode while running");
        return;
    }
    is_server_ = is_server;
}

void IPCImplement::start() {
    if (ipc_name_.empty()) {
        LOG_ERROR("IPC name not set");
        return;
    }

    if (running_) {
        LOG_WARNING("IPC already running");
        return;
    }

    // Create shared memory
    shared_memory_ = std::make_unique<IPCSharedMemory>(ipc_name_, is_server_);
    if (!shared_memory_->Init()) {
        LOG_ERROR("Failed to initialize shared memory");
        shared_memory_.reset();
        return;
    }

    // Set running flag before starting threads
    running_ = true;

    // Start sender and receiver threads
    sender_thread_ = std::thread(&IPCImplement::senderThreadFunc, this);
    receiver_thread_ = std::thread(&IPCImplement::receiverThreadFunc, this);

    LOG_INFO("IPC started (name=%s, is_server=%d)", ipc_name_.c_str(), is_server_);
}

void IPCImplement::stop() {
    // Use atomic compare-exchange to prevent race condition
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return; // Already stopped or being stopped by another thread
    }

    // Wake up sender thread if it's waiting
    {
        std::lock_guard<std::mutex> lock(send_mutex_);
        send_cv_.notify_all();
    }

    // Wake up receiver thread if it's waiting
    {
        std::lock_guard<std::mutex> lock(receive_mutex_);
        receive_cv_.notify_all();
    }

    // Join threads with timeout handling to prevent infinite hang
    if (sender_thread_.joinable()) {
        try {
            // Use a simple join since the thread should exit quickly
            sender_thread_.join();
            LOG_DEBUG("Sender thread joined successfully");
        } catch (const std::exception& e) {
            LOG_ERROR("Exception joining sender thread: %s", e.what());
        } catch (...) {
            LOG_ERROR("Unknown exception joining sender thread");
        }
    }

    if (receiver_thread_.joinable()) {
        try {
            receiver_thread_.join();
            LOG_DEBUG("Receiver thread joined successfully");
        } catch (const std::exception& e) {
            LOG_ERROR("Exception joining receiver thread: %s", e.what());
        } catch (...) {
            LOG_ERROR("Unknown exception joining receiver thread");
        }
    }

    // Clean up shared memory
    if (shared_memory_) {
        try {
            shared_memory_->Uninit();
            shared_memory_.reset();
        } catch (const std::exception& e) {
            LOG_ERROR("Exception cleaning up shared memory: %s", e.what());
            shared_memory_.reset(); // Force reset even if Uninit() fails
        }
    }

    LOG_INFO("IPC stopped (name=%s, is_server=%d)", ipc_name_.c_str(), is_server_);
}

bool IPCImplement::receiveMessage(std::string& message) {
    if (!running_) {
        LOG_ERROR("IPC not running");
        return false;
    }

    // Try to get a message from the receive queue
    std::lock_guard<std::mutex> lock(receive_mutex_);
    if (receive_queue_.empty()) {
        return false;
    }

    message = std::move(receive_queue_.front());
    receive_queue_.pop();

    LOG_DEBUG("Received message: %s", message.c_str());
    return true;
}

bool IPCImplement::sendMessage(const std::string& message) {
    if (!running_) {
        LOG_ERROR("IPC not running");
        return false;
    }

    // Add message to send queue
    {
        std::lock_guard<std::mutex> lock(send_mutex_);
        send_queue_.push(message);
    }

    // Notify sender thread
    send_cv_.notify_one();

    LOG_DEBUG("Queued message for sending: %s", message.c_str());
    return true;
}

bool IPCImplement::ReceiveMsg(IPCPacket* packet) {
    if (packet == nullptr) {
        LOG_ERROR("Packet is null");
        return false;
    }

    MessageType type = packet->GetMessageType();
    uint32_t seq_num = packet->GetSequenceNumber();
    uint32_t timestamp = packet->GetTimestamp();
    const void* payload = packet->GetPayload();
    uint32_t payload_len = packet->GetPayloadLength();

    std::string message;
    if (payload != nullptr && payload_len > 0) {
        message.assign(static_cast<const char*>(payload), payload_len);
    }

    LOG_DEBUG("ReceiveMsg: seq_num=%u, timestamp=%u, type=%d, message=%s", seq_num, timestamp, type, message.c_str());
    return true;
}

bool IPCImplement::isRunning() const {
    return running_;
}

void IPCImplement::senderThreadFunc() {
    LOG_DEBUG("Sender thread started");

    // Retry parameters
    const int max_retries = 3;
    const int base_retry_delay_ms = 10;

    while (running_.load()) {
        std::string message;
        bool has_message = false;

        // Wait for a message in the queue or until stopped
        {
            std::unique_lock<std::mutex> lock(send_mutex_);

            // Wait for a message or stop signal with timeout
            send_cv_.wait_for(lock, std::chrono::milliseconds(50),
                [this]() { return !send_queue_.empty() || !running_.load(); });

            // Double-check running state after waking up
            if (!running_.load()) {
                break;
            }

            // Get message from queue if not empty
            if (!send_queue_.empty()) {
                message = std::move(send_queue_.front());
                send_queue_.pop();
                has_message = true;
            }
        }

        // Send message if we got one
        if (has_message && shared_memory_ && running_.load()) {
            // Create packet with message as payload
            IPCPacket packet(
                is_server_ ? MessageType::MSG_RESPONSE : MessageType::MSG_REQUEST,
                0, // sequence number
                message.data(),
                message.size()
            );

            // Try to write packet to shared memory with retries
            bool write_success = false;
            for (int retry = 0; retry < max_retries && !write_success && running_.load(); retry++) {
                if (retry > 0) {
                    LOG_DEBUG("Retrying packet write, attempt %d/%d", retry + 1, max_retries);
                    // Exponential backoff strategy
                    std::this_thread::sleep_for(std::chrono::milliseconds(base_retry_delay_ms * (1 << retry)));
                }

                write_success = shared_memory_->WritePacket(packet);
            }

            if (!write_success) {
                LOG_ERROR("Failed to write packet to shared memory after retries: %s", message.c_str());

                // Put the message back in the queue to try again later, but only if still running
                if (running_.load()) {
                    std::lock_guard<std::mutex> lock(send_mutex_);
                    // Avoid infinite growth of the queue, discard messages if too large
                    if (send_queue_.size() < 1000) {
                        send_queue_.push(std::move(message));
                    } else {
                        LOG_WARNING("Sending queue is full, discarding message: %s", message.c_str());
                    }
                }
            } else {
                LOG_DEBUG("Sent message: %s", message.c_str());
            }
        }
    }

    LOG_DEBUG("Sender thread stopped");
}

void IPCImplement::receiverThreadFunc() {
    LOG_DEBUG("Receiver thread started");

    // Buffer for batch processing
    std::vector<IPCPacket> packet_buffer;
    const size_t max_batch_size = 10;
    packet_buffer.reserve(max_batch_size);

    while (running_.load()) {
        bool received_any = false;
        packet_buffer.clear();

        // Try to read multiple packets from shared memory
        if (shared_memory_ && running_.load()) {
            // Read up to max_batch_size packets in a single loop
            for (size_t i = 0; i < max_batch_size && running_.load(); ++i) {
                IPCPacket packet;
                if (shared_memory_->ReadPacket(&packet)) {
                    packet_buffer.push_back(std::move(packet));
                    received_any = true;
                } else {
                    // No more packets available
                    break;
                }
            }

            // Process all received packets
            for (auto& packet : packet_buffer) {
                if (!running_.load()) {
                    break;
                }

                // Extract message from packet payload
                if (packet.GetPayloadLength() > 0 && packet.GetPayload() != nullptr) {
                    std::string message(reinterpret_cast<const char*>(packet.GetPayload()),
                                        packet.GetPayloadLength());

                    // Add message to receive queue
                    {
                        std::lock_guard<std::mutex> lock(receive_mutex_);
                        receive_queue_.push(message);
                    }

                    // Notify any waiting threads
                    receive_cv_.notify_one();

                    LOG_DEBUG("Queued received message: %s", message.c_str());
                }

                // Also call the legacy handler for backward compatibility
                ReceiveMsg(&packet);
            }
        }

        // Sleep briefly to reduce CPU usage, with regular checks of running state
        if (!received_any) {
            // Use shorter sleep intervals with running state checks
            for (int i = 0; i < 10 && running_.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    }

    LOG_DEBUG("Receiver thread stopped");
}

} // namespace ipc
} // namespace SAK