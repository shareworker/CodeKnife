#include "ipc_implement.hpp"
#include "ipc_packet.hpp"
#include "logger.hpp"
#include <thread>
#include <chrono>
#include <queue>
#include <mutex>
#include <condition_variable>

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
    if (!running_) {
        return;
    }

    // Set running flag to false to signal threads to exit
    running_ = false;
    
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
    
    // Wait for threads to finish
    if (sender_thread_.joinable()) {
        sender_thread_.join();
    }
    
    if (receiver_thread_.joinable()) {
        receiver_thread_.join();
    }

    // Clean up shared memory
    if (shared_memory_) {
        shared_memory_->Uninit();
        shared_memory_.reset();
    }

    LOG_INFO("IPC stopped (name=%s, is_server=%d)", ipc_name_.c_str(), is_server_);
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
    
    // Adaptive waiting parameters
    int consecutive_empty_checks = 0;
    [[maybe_unused]] const int max_empty_checks = 5;
    const int min_wait_time = 10;
    const int max_wait_time = 100;
    
    // Retry parameters
    const int max_retries = 3;
    const int base_retry_delay_ms = 10;
    
    while (running_) {
        std::string message;
        bool has_message = false;
        
        // Wait for a message in the queue or until stopped
        {
            std::unique_lock<std::mutex> lock(send_mutex_);
            if (send_queue_.empty()) {
                // Calculate adaptive wait time
                int wait_time = std::min(min_wait_time * (consecutive_empty_checks + 1), max_wait_time);
                
                // Wait for a message or stop signal
                send_cv_.wait_for(lock, std::chrono::milliseconds(wait_time), 
                    [this]() { return !send_queue_.empty() || !running_; });
                
                // Check if we were woken up because we're stopping
                if (!running_ && send_queue_.empty()) {
                    break;
                }
                
                // Increment consecutive empty checks counter if queue is still empty
                if (send_queue_.empty()) {
                    consecutive_empty_checks++;
                }
            }
            
            // Get message from queue if not empty
            if (!send_queue_.empty()) {
                message = std::move(send_queue_.front());
                send_queue_.pop();
                has_message = true;
                consecutive_empty_checks = 0;
            }
        }
        
        // Send message if we got one
        if (has_message && shared_memory_) {
            // Create packet with message as payload
            IPCPacket packet(
                is_server_ ? MessageType::MSG_RESPONSE : MessageType::MSG_REQUEST,
                0, // sequence number
                message.data(),
                message.size()
            );
            
            // Try to write packet to shared memory with retries
            bool write_success = false;
            for (int retry = 0; retry < max_retries && !write_success && running_; retry++) {
                if (retry > 0) {
                    LOG_DEBUG("Retrying packet write, attempt %d/%d", retry + 1, max_retries);
                    // Exponential backoff strategy
                    std::this_thread::sleep_for(std::chrono::milliseconds(base_retry_delay_ms * (1 << retry)));
                }
                
                write_success = shared_memory_->WritePacket(packet);
            }
            
            if (!write_success) {
                LOG_ERROR("Failed to write packet to shared memory after retries: %s", message.c_str());
                
                // Put the message back in the queue to try again later
                std::lock_guard<std::mutex> lock(send_mutex_);
                // Avoid infinite growth of the queue, discard messages if too large
                if (send_queue_.size() < 1000) {
                    send_queue_.push(std::move(message));
                } else {
                    LOG_WARNING("Sending queue is full, discarding message: %s", message.c_str());
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
    
    // Track consecutive empty reads to implement adaptive waiting
    int consecutive_empty_reads = 0;
    [[maybe_unused]] const int max_empty_reads = 5;
    const int min_sleep_ms = 1;
    const int max_sleep_ms = 50;
    
    // Buffer for batch processing
    std::vector<IPCPacket> packet_buffer;
    const size_t max_batch_size = 10;
    packet_buffer.reserve(max_batch_size);
    
    while (running_) {
        bool received_any = false;
        packet_buffer.clear();
        
        // Try to read multiple packets from shared memory
        if (shared_memory_) {
            // Read up to max_batch_size packets in a single loop
            for (size_t i = 0; i < max_batch_size && running_; ++i) {
                IPCPacket packet;
                if (shared_memory_->ReadPacket(&packet)) {
                    packet_buffer.push_back(std::move(packet));
                    received_any = true;
                    consecutive_empty_reads = 0;
                } else {
                    // No more packets available
                    break;
                }
            }
            
            // Process all received packets
            for (auto& packet : packet_buffer) {
                ReceiveMsg(&packet);
            }
        }
        
        // Adjust sleep time based on activity
        if (!received_any) {
            consecutive_empty_reads++;
            
            // Calculate adaptive sleep time
            int sleep_ms = std::min(min_sleep_ms * consecutive_empty_reads, max_sleep_ms);
            
            // Sleep to reduce CPU usage when there's no activity
            if (sleep_ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
            }
        }
    }
    
    LOG_DEBUG("Receiver thread stopped");
}

} // namespace ipc
} // namespace SAK