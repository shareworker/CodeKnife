#pragma once

#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <condition_variable>
#include "ipc_shared_memory.hpp"

namespace SAK {
namespace ipc {

// Forward declarations
class IPCPacket;

/**
 * @brief IPC implementation class that provides bidirectional communication
 * using shared memory with non-blocking message queues.
 */
class IPCImplement {
public:
    /**
     * @brief Constructor with IPC name and server mode
     * @param ipc_name The name of the IPC channel
     * @param is_server Whether this instance is a server
     */
    IPCImplement(const std::string& ipc_name = "", bool is_server = false);
    
    /**
     * @brief Destructor
     */
    ~IPCImplement();
    
    /**
     * @brief Set the IPC name
     * @param ipc_name The name of the IPC channel
     */
    void setIpcName(const std::string& ipc_name);
    
    /**
     * @brief Set whether this instance is a server
     * @param is_server True if server, false if client
     */
    void setIsServer(bool is_server);
    
    /**
     * @brief Start the IPC communication
     */
    void start();
    
    /**
     * @brief Stop the IPC communication
     */
    void stop();
    
    /**
     * @brief Send a message through the IPC channel (non-blocking)
     * @param message The message to send
     * @return True if message was queued successfully, false otherwise
     */
    bool sendMessage(const std::string& message);
    
    /**
     * @brief Receive a message from the IPC channel (non-blocking)
     * @param message Reference to store the received message
     * @return True if a message was received, false otherwise
     */
    bool receiveMessage(std::string& message);
    
    /**
     * @brief Receive a raw packet from the IPC channel (for backward compatibility)
     * @param packet Pointer to store the received packet
     * @return True if a packet was received, false otherwise
     */
    bool ReceiveMsg(IPCPacket* packet);
    
    /**
     * @brief Check if the IPC implementation is running
     * @return True if running, false otherwise
     */
    bool isRunning() const;

private:
    /**
     * @brief Thread function for sending messages
     */
    void senderThreadFunc();
    
    /**
     * @brief Thread function for receiving messages
     */
    void receiverThreadFunc();

private:
    std::string ipc_name_;
    bool is_server_;
    std::atomic<bool> running_;
    std::unique_ptr<IPCSharedMemory> shared_memory_;
    
    // Sender thread and queue
    std::thread sender_thread_;
    std::mutex send_mutex_;
    std::condition_variable send_cv_;
    std::queue<std::string> send_queue_;
    
    // Receiver thread and queue
    std::thread receiver_thread_;
    std::mutex receive_mutex_;
    std::condition_variable receive_cv_;
    std::queue<std::string> receive_queue_;
};

} // namespace ipc
} // namespace SAK