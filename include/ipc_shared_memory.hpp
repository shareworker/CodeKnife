#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <cstdint>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <cstring>
#include "logger.hpp"
#include "ipc_packet.hpp"

namespace util {
namespace ipc {

// Shared memory constants
constexpr size_t SHM_BUFFER_SIZE = 1024 * 1024; // 1MB buffer size
constexpr int SHM_PERMISSIONS = 0666;           // Read/write permissions

// Semaphore indices
enum SEM_INDEX {
    SEM_SERVER_WRITE = 0,  // Server can write to shared memory
    SEM_SERVER_READ = 1,   // Server can read from shared memory
    SEM_CLIENT_WRITE = 2,  // Client can write to shared memory
    SEM_CLIENT_READ = 3,   // Client can read from shared memory
    SEM_COUNT = 4          // Total number of semaphores
};

// Shared memory buffer structure
#pragma pack(push, 1)
struct SharedMemoryHeader {
    std::atomic<uint32_t> server_write_pos;  // Position where server writes
    std::atomic<uint32_t> server_read_pos;   // Position where server reads
    std::atomic<uint32_t> client_write_pos;  // Position where client writes
    std::atomic<uint32_t> client_read_pos;   // Position where client reads
};

struct SharedMemoryBuffer {
    SharedMemoryHeader header;
    char server_to_client[SHM_BUFFER_SIZE];  // Buffer for server-to-client communication
    char client_to_server[SHM_BUFFER_SIZE];  // Buffer for client-to-server communication
};
#pragma pack(pop)

// Forward declaration
class IPCPacket;

class IPCSharedMemory {
public:
    IPCSharedMemory(const std::string& ipc_name, bool is_server);
    ~IPCSharedMemory();

    bool Init();
    bool Uninit();

    // Writer methods
    bool WritePacket(const IPCPacket& packet);
    
    // Reader methods
    bool ReadPacket(IPCPacket *packet);
    
    // Common methods
    bool IsInitialized() const { return initialized_; }

private:
    // Generate a unique key for shared memory and semaphores
    key_t GenerateKey(const std::string& name, bool is_sem);
    
    // Semaphore operations
    bool CreateSemaphore();
    bool SemaphoreWait(int sem_index);
    bool SemaphoreSignal(int sem_index);
    bool DestroySemaphore();
    
    // Shared memory operations
    bool CreateSharedMemory();
    bool DestroySharedMemory();

private:
    std::string ipc_name_;
    bool is_server_;
    
    // Shared memory
    key_t shm_key_;
    int shm_id_;
    SharedMemoryBuffer* shm_buffer_;
    
    // Semaphores
    key_t sem_key_;
    int sem_id_;
    
    // State
    bool initialized_;
};

} // namespace ipc
} // namespace util
