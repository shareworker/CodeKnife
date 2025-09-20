#include "ipc_shared_memory.hpp"
#include "ipc_packet.hpp"
#include <errno.h>
#include <cstring>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/stat.h>
#endif

namespace SAK {
namespace ipc {

IPCSharedMemory::IPCSharedMemory(const std::string& ipc_name, bool is_server)
    : ipc_name_(ipc_name), is_server_(is_server), shm_key_(0), sem_key_(0),
      shm_buffer_(nullptr), initialized_(false) {
#ifdef _WIN32
    shm_handle_ = nullptr;
    for (int i = 0; i < SEM_COUNT; i++) {
        sem_handles_[i] = nullptr;
    }
#else
    shm_id_ = -1;
    sem_id_ = -1;
#endif
}

IPCSharedMemory::~IPCSharedMemory() {
    Uninit();
}

bool IPCSharedMemory::Init() {
    if (ipc_name_.empty()) {
        LOG_ERROR("IPC name is empty");
        return false;
    }

    // Generate keys for shared memory and semaphores
    shm_key_ = GenerateKey(ipc_name_, false);
    sem_key_ = GenerateKey(ipc_name_, true);

    if (shm_key_ == -1 || sem_key_ == -1) {
        LOG_ERROR("Failed to generate keys for shared memory or semaphores");
        return false;
    }

    // Create shared memory
    if (!CreateSharedMemory()) {
        LOG_ERROR("Failed to create shared memory");
        return false;
    }

    // Create semaphores
    if (!CreateSemaphore()) {
        LOG_ERROR("Failed to create semaphores");
        DestroySharedMemory();
        return false;
    }

    // Initialize buffer positions
    // For server, we need to initialize the header
    // For client, we need to ensure we can read the header values
    if (is_server_) {
        // Initialize header as server with explicit memory ordering for cross-process safety
        shm_buffer_->header.server_write_pos.store(0, std::memory_order_seq_cst);
        shm_buffer_->header.server_read_pos.store(0, std::memory_order_seq_cst);
        shm_buffer_->header.client_write_pos.store(0, std::memory_order_seq_cst);
        shm_buffer_->header.client_read_pos.store(0, std::memory_order_seq_cst);
        
        // Clear buffers for safety
        std::memset(shm_buffer_->server_to_client, 0, SHM_BUFFER_SIZE);
        std::memset(shm_buffer_->client_to_server, 0, SHM_BUFFER_SIZE);
        
        LOG_DEBUG("Server initialized shared memory with all positions set to 0");
    } else {
        // Client should wait briefly to ensure server has initialized the header
        // This is a simple approach; a more robust solution would use a synchronization mechanism
        for (int retry = 0; retry < 10; retry++) {
            // Check if header values are initialized using safe cross-process atomic access
            uint32_t server_write = shm_buffer_->header.server_write_pos.load(std::memory_order_seq_cst);
            uint32_t server_read = shm_buffer_->header.server_read_pos.load(std::memory_order_seq_cst);
            uint32_t client_write = shm_buffer_->header.client_write_pos.load(std::memory_order_seq_cst);
            uint32_t client_read = shm_buffer_->header.client_read_pos.load(std::memory_order_seq_cst);
            
            if (server_write == 0 && server_read == 0 && client_write == 0 && client_read == 0) {
                LOG_DEBUG("Client verified header initialization: server_write=%u, server_read=%u, client_write=%u, client_read=%u",
                         server_write, server_read, client_write, client_read);
                break;
            }
            
            LOG_WARNING("Client waiting for server to initialize header (retry %d): server_write=%u, server_read=%u, client_write=%u, client_read=%u",
                      retry, server_write, server_read, client_write, client_read);
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            if (retry == 9) {
                LOG_ERROR("Client timed out waiting for server to initialize header");
                return false;
            }
        }
    }

    initialized_ = true;
    LOG_DEBUG("Shared memory initialized successfully (is_server=%d)", is_server_);
    return true;
}

bool IPCSharedMemory::Uninit() {
    bool result = true;

    if (shm_buffer_) {
#ifdef _WIN32
        if (!UnmapViewOfFile(shm_buffer_)) {
            LOG_ERROR("Failed to unmap shared memory: %lu", GetLastError());
            result = false;
        }
#else
        if (shmdt(shm_buffer_) == -1) {
            LOG_ERROR("Failed to detach shared memory: %s", strerror(errno));
            result = false;
        }
#endif
        shm_buffer_ = nullptr;
    }

    // Only destroy resources if we're the server
    if (is_server_) {
#ifndef _WIN32
        if (shm_id_ != -1) {
#endif
            if (!DestroySharedMemory()) {
                LOG_ERROR("Failed to destroy shared memory");
                result = false;
            }
#ifndef _WIN32
        }
#endif
#ifdef _WIN32
        if (shm_handle_ != nullptr) {
            DestroySharedMemory();
        }
        if (sem_handles_[0] != nullptr) {
            DestroySemaphore();
        }
#else
        if (shm_id_ != -1) {
            DestroySharedMemory();
        }
        if (sem_id_ != -1) {
            DestroySemaphore();
        }
#endif
        if (!DestroySemaphore()) {
            LOG_ERROR("Failed to destroy semaphores");
            result = false;
        }
    }

    initialized_ = false;
    return result;
}

bool IPCSharedMemory::WritePacket(const IPCPacket& packet) {
    if (!initialized_ || !shm_buffer_) {
        LOG_ERROR("Shared memory not initialized");
        return false;
    }
    
    LOG_DEBUG("WritePacket: is_server=%d, packet_size=%u", is_server_, packet.GetTotalSize());
    
    // Determine which buffer and positions to use based on server/client role
    char* buffer = nullptr;
    
    // For server: write to server_to_client buffer, read from client_to_server buffer
    // For client: write to client_to_server buffer, read from server_to_client buffer
    
    // Write position is our own write position
    std::atomic<uint32_t>& write_pos = is_server_ ? 
        shm_buffer_->header.server_write_pos : 
        shm_buffer_->header.client_write_pos;
    
    // Read position is the other side's read position
    std::atomic<uint32_t>& read_pos = is_server_ ? 
        shm_buffer_->header.client_read_pos : 
        shm_buffer_->header.server_read_pos;
    
    // Semaphores for synchronization
    int write_sem = is_server_ ? SEM_SERVER_WRITE : SEM_CLIENT_WRITE;
    int read_sem = is_server_ ? SEM_CLIENT_READ : SEM_SERVER_READ;
    
    // Select the appropriate buffer for writing
    // Server writes to server_to_client, client writes to client_to_server
    buffer = is_server_ ? shm_buffer_->server_to_client : shm_buffer_->client_to_server;
    
    LOG_DEBUG("WritePacket: Role=%s, Using buffer=%s, write_pos=%u, read_pos=%u, write_sem=%d, read_sem=%d",
             is_server_ ? "SERVER" : "CLIENT",
             is_server_ ? "server_to_client" : "client_to_server",
             write_pos.load(), read_pos.load(), write_sem, read_sem);
    
    // Get packet size
    uint32_t packet_size = packet.GetTotalSize();
    
    // Check if packet fits in buffer
    if (packet_size > SHM_BUFFER_SIZE) {
        LOG_ERROR("Packet size %u exceeds buffer size %u", packet_size, SHM_BUFFER_SIZE);
        return false;
    }
    
    // Try to acquire write semaphore (non-blocking). If unavailable, caller can retry later.
    if (!SemaphoreTryWait(write_sem)) {
        LOG_DEBUG("WritePacket: Failed to acquire write semaphore %d", write_sem);
        return false;
    }
    
    LOG_DEBUG("WritePacket: Acquired write semaphore %d", write_sem);
    
    // Get current write position
    uint32_t current_write_pos = write_pos.load(std::memory_order_acquire);
    uint32_t current_read_pos = read_pos.load(std::memory_order_acquire);
    
    // Check if there's enough space (considering circular buffer)
    uint32_t available_space;
    
    // Log current positions for debugging
    LOG_DEBUG("WritePacket: Before calculation - current_write_pos=%u, current_read_pos=%u", 
             current_write_pos, current_read_pos);
    
    if (current_write_pos >= current_read_pos) {
        // Write position is after or equal to read position
        // Need to consider wrap-around case
        if (current_read_pos == 0) {
            // Special case: if read position is at start, we can use up to the end of buffer
            available_space = SHM_BUFFER_SIZE - current_write_pos;
        } else {
            // Available space = buffer size - write position + read position - 1 (keep one byte gap)
            available_space = SHM_BUFFER_SIZE - current_write_pos + current_read_pos - 1;
        }
    } else {
        // Write position is before read position
        // Available space = read position - write position - 1 (keep one byte gap)
        available_space = current_read_pos - current_write_pos - 1;
    }
    
    LOG_DEBUG("WritePacket: Available space calculation: %u bytes", available_space);
    
    // Ensure there's enough space to write the packet
    if (available_space <= packet_size) {
        LOG_ERROR("Not enough space in buffer: available=%u, needed=%u", available_space, packet_size);
        SemaphoreSignal(write_sem);
        return false;
    }
    
    // Serialize packet to buffer
    // Check if we need to handle wrap-around case
    if (current_write_pos + packet_size > SHM_BUFFER_SIZE) {
        LOG_DEBUG("WritePacket: Handling wrap-around case for serialization");
        
        // Calculate how much data fits before the end of the buffer
        uint32_t first_chunk_size = SHM_BUFFER_SIZE - current_write_pos;
        
        // Create a temporary buffer to hold the serialized packet
        std::vector<uint8_t> temp_buffer(packet_size);
        
        // Serialize to temporary buffer
        if (!packet.Serialize(temp_buffer.data(), packet_size)) {
            LOG_ERROR("Failed to serialize packet to temporary buffer");
            SemaphoreSignal(write_sem);
            return false;
        }
        
        // Copy first chunk to end of buffer
        std::memcpy(buffer + current_write_pos, temp_buffer.data(), first_chunk_size);
        
        // Copy second chunk to beginning of buffer
        std::memcpy(buffer, temp_buffer.data() + first_chunk_size, packet_size - first_chunk_size);
        
        LOG_DEBUG("WritePacket: Wrote packet in two chunks: %u bytes at end, %u bytes at beginning", 
                 first_chunk_size, packet_size - first_chunk_size);
    } else {
        // Normal case - no wrap-around
        if (!packet.Serialize(buffer + current_write_pos, SHM_BUFFER_SIZE - current_write_pos)) {
            LOG_ERROR("Failed to serialize packet");
            SemaphoreSignal(write_sem);
            return false;
        }
        
        LOG_DEBUG("WritePacket: Wrote packet in one chunk: %u bytes", packet_size);
    }
    
    // Update write position
    uint32_t new_write_pos = (current_write_pos + packet_size) % SHM_BUFFER_SIZE;
    write_pos.store(new_write_pos, std::memory_order_release);
    
    // Signal read semaphore to indicate data is available for the other side
    SemaphoreSignal(read_sem);

    // Release write semaphore
    SemaphoreSignal(write_sem);

    LOG_DEBUG("WritePacket: Successfully wrote packet, signaled read_sem=%d and released write_sem=%d", read_sem, write_sem);
    return true;
}

bool IPCSharedMemory::ReadPacket(IPCPacket* packet) {
    if (!initialized_ || !shm_buffer_ || !packet) {
        LOG_ERROR("Shared memory not initialized or packet is null");
        return false;
    }
    
    LOG_DEBUG("ReadPacket: is_server=%d", is_server_);
    
    // Determine which buffer and positions to use based on server/client role
    char* buffer = nullptr;
    
    // For server: read from client_to_server buffer, write to server_to_client buffer
    // For client: read from server_to_client buffer, write to client_to_server buffer
    
    // Write position is the other side's write position
    std::atomic<uint32_t>& write_pos = is_server_ ? 
        shm_buffer_->header.client_write_pos : 
        shm_buffer_->header.server_write_pos;
    
    // Read position is our own read position
    std::atomic<uint32_t>& read_pos = is_server_ ? 
        shm_buffer_->header.server_read_pos : 
        shm_buffer_->header.client_read_pos;
    
    // Semaphores for synchronization
    int write_sem = is_server_ ? SEM_CLIENT_WRITE : SEM_SERVER_WRITE;
    int read_sem = is_server_ ? SEM_SERVER_READ : SEM_CLIENT_READ;
    
    // Select the appropriate buffer for reading
    // Server reads from client_to_server, client reads from server_to_client
    buffer = is_server_ ? shm_buffer_->client_to_server : shm_buffer_->server_to_client;
    
    LOG_DEBUG("ReadPacket: Role=%s, Using buffer=%s, write_pos=%u, read_pos=%u, write_sem=%d, read_sem=%d",
             is_server_ ? "SERVER" : "CLIENT",
             is_server_ ? "client_to_server" : "server_to_client",
             write_pos.load(), read_pos.load(), write_sem, read_sem);
    
    // Check if there's data available (non-blocking check)
    uint32_t current_write_pos = write_pos.load(std::memory_order_acquire);
    uint32_t current_read_pos = read_pos.load(std::memory_order_acquire);
    LOG_DEBUG("ReadPacket: current_write_pos=%u, current_read_pos=%u", current_write_pos, current_read_pos);
    
    // Calculate available data size
    uint32_t available_data;
    if (current_write_pos >= current_read_pos) {
        // Write position is after or equal to read position
        available_data = current_write_pos - current_read_pos;
    } else {
        // Write position is before read position (wrap-around case)
        available_data = SHM_BUFFER_SIZE - current_read_pos + current_write_pos;
    }
    
    LOG_DEBUG("ReadPacket: Available data: %u bytes", available_data);
    
    if (available_data == 0) {
        // No data available
        LOG_DEBUG("ReadPacket: No data available (no bytes to read)");
        return false;
    }
    
    // Try to acquire read semaphore (non-blocking). If unavailable, no data can be consumed now.
    if (!SemaphoreTryWait(read_sem)) {
        LOG_DEBUG("ReadPacket: Failed to acquire read semaphore %d", read_sem);
        return false;
    }
    
    LOG_DEBUG("ReadPacket: Acquired read semaphore %d", read_sem);
    
    // Re-check positions since they may have changed while waiting for semaphore
    current_read_pos = read_pos.load(std::memory_order_acquire);
    current_write_pos = write_pos.load(std::memory_order_acquire);
    
    // If no data is available, release semaphore and return
    if (current_read_pos == current_write_pos) {
        SemaphoreSignal(read_sem);
        return false;
    }
    
    // Read packet header to get total size
    SAK::ipc::PacketHeader header;
    
    // Check if header crosses buffer boundary
    if (current_read_pos + sizeof(header) > SHM_BUFFER_SIZE) {
        // Header is split across buffer boundary
        LOG_DEBUG("ReadPacket: Header crosses buffer boundary");
        
        // Calculate how much of the header is at the end of the buffer
        uint32_t first_chunk_size = SHM_BUFFER_SIZE - current_read_pos;
        
        // Create a temporary buffer to hold the header
        uint8_t temp_header[sizeof(PacketHeader)];
        
        // Copy first part from end of buffer
        std::memcpy(temp_header, buffer + current_read_pos, first_chunk_size);
        
        // Copy second part from beginning of buffer
        std::memcpy(temp_header + first_chunk_size, buffer, sizeof(header) - first_chunk_size);
        
        // Copy to header struct
        std::memcpy(&header, temp_header, sizeof(header));
    } else {
        // Header is contiguous
        std::memcpy(&header, buffer + current_read_pos, sizeof(header));
    }
    
    // Validate magic ID
    if (header.magic_id != IPC_PACKET_MAGIC) {
        LOG_ERROR("Invalid packet magic ID: %u", header.magic_id);
        SemaphoreSignal(read_sem);
        return false;
    }
    
    LOG_DEBUG("ReadPacket: Valid packet header found, payload length: %u", header.payload_len);
    
    // Calculate packet total size, including header, payload, and checksum
    uint32_t packet_size = sizeof(PacketHeader) + header.payload_len + sizeof(uint32_t);
    
    if (packet_size > SHM_BUFFER_SIZE) {
        LOG_ERROR("Packet size %u exceeds buffer size %u", packet_size, SHM_BUFFER_SIZE);
        SemaphoreSignal(read_sem);
        return false;
    }
    
    // Check if packet crosses buffer boundary
    if (current_read_pos + packet_size > SHM_BUFFER_SIZE) {
        // Packet is split across buffer boundary
        LOG_DEBUG("ReadPacket: Packet crosses buffer boundary");
        
        // Create a temporary buffer to hold the entire packet
        std::vector<uint8_t> temp_buffer(packet_size);
        
        // Calculate sizes of the two chunks
        uint32_t first_chunk_size = SHM_BUFFER_SIZE - current_read_pos;
        uint32_t second_chunk_size = packet_size - first_chunk_size;
        
        // Copy first chunk from end of buffer
        std::memcpy(temp_buffer.data(), buffer + current_read_pos, first_chunk_size);
        
        // Copy second chunk from beginning of buffer
        std::memcpy(temp_buffer.data() + first_chunk_size, buffer, second_chunk_size);
        
        // Create packet from temporary buffer
        *packet = IPCPacket(temp_buffer.data(), packet_size);
        
        LOG_DEBUG("ReadPacket: Read packet in two chunks: %u bytes from end, %u bytes from beginning", 
                 first_chunk_size, second_chunk_size);
    } else {
        // Packet is contiguous
        *packet = IPCPacket(buffer + current_read_pos, packet_size);
        LOG_DEBUG("ReadPacket: Read packet in one chunk: %u bytes", packet_size);
    }
    
    // Validate packet
    if (!packet->IsValid()) {
        LOG_ERROR("Invalid packet read from shared memory");
        SemaphoreSignal(read_sem);
        return false;
    }
    
    // Update read position
    uint32_t new_read_pos = (current_read_pos + packet_size) % SHM_BUFFER_SIZE;
    read_pos.store(new_read_pos, std::memory_order_release);
    
    // Signal write semaphore to indicate buffer space is available to the other side
    SemaphoreSignal(write_sem);

    LOG_DEBUG("ReadPacket: Successfully read packet, signaled write_sem=%d", write_sem);
    return true;
}

int IPCSharedMemory::GenerateKey(const std::string& name, bool is_sem) {
    // Generate a unique key based on the IPC name and type
    // Use a hash of the name instead of ftok since ftok requires an existing file
    int key = 0;
    std::string key_str = name + (is_sem ? "_sem" : "_shm");
    
    // Simple hash function to generate a key from the string
    for (char c : key_str) {
        key = ((key << 5) + key) + c; // hash * 33 + c
    }
    
    // Ensure key is positive and not IPC_PRIVATE (0)
    key = (key & 0x7FFFFFFF);
    if (key == 0) {
        key = 1;
    }
    
    LOG_DEBUG("Generated key %d for %s", key, key_str.c_str());
    return key;
}

bool IPCSharedMemory::CreateSharedMemory() {
    // Try to get existing shared memory
#ifdef _WIN32
    // Windows uses named shared memory
    // Use local namespace instead of Global to avoid privilege issues
    std::string shm_name = "Local\\" + ipc_name_ + "_shm";
    shm_handle_ = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, shm_name.c_str());
    if (shm_handle_ == nullptr) {
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND) {
            LOG_ERROR("Failed to open shared memory: %lu", error);
            return false;
        }
        // Create new shared memory mapping if not found
        shm_handle_ = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                         0, (DWORD)sizeof(SharedMemoryBuffer), shm_name.c_str());
        if (shm_handle_ == nullptr) {
            LOG_ERROR("Failed to create shared memory: %lu", GetLastError());
            return false;
        }
    }
#else
    shm_id_ = shmget(shm_key_, sizeof(SharedMemoryBuffer), 0);
    
    if (shm_id_ == -1) {
        // Create new shared memory if it doesn't exist
        shm_id_ = shmget(shm_key_, sizeof(SharedMemoryBuffer), IPC_CREAT | SHM_PERMISSIONS);
        
        if (shm_id_ == -1) {
            LOG_ERROR("Failed to create shared memory: %s", strerror(errno));
            return false;
        }
        
        LOG_DEBUG("Created new shared memory segment with ID %d", shm_id_);
    } else {
        LOG_DEBUG("Using existing shared memory segment with ID %d", shm_id_);
    }
#endif
    
    // Attach to shared memory
#ifdef _WIN32
    shm_buffer_ = (SharedMemoryBuffer*)MapViewOfFile(shm_handle_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedMemoryBuffer));
    if (shm_buffer_ == nullptr) {
        LOG_ERROR("Failed to map shared memory: %lu", GetLastError());
        return false;
    }
#else
    shm_buffer_ = (SharedMemoryBuffer*)shmat(shm_id_, nullptr, 0);
#endif

    if (shm_buffer_ == (void*)-1) {
        LOG_ERROR("Failed to attach to shared memory: %s", strerror(errno));
        shm_buffer_ = nullptr;
        return false;
    }

    return true;
}

bool IPCSharedMemory::SemaphoreSignal(int sem_index) {
#ifdef _WIN32
    if (sem_handles_[sem_index] == nullptr) {
        LOG_ERROR("Invalid semaphore handle for index %d", sem_index);
        return false;
    }
    
    // For read semaphores (counting semaphores), we increment to indicate data is available
    // For write semaphores (mutex semaphores), we release the lock
    bool is_read_sem = (sem_index == SEM_SERVER_READ || sem_index == SEM_CLIENT_READ);
    
    LOG_DEBUG("SemaphoreSignal: Attempting to signal %s semaphore %d", 
             is_read_sem ? "read" : "write", sem_index);
    
    // Release the semaphore
    LONG previous_count = 0;
    if (!ReleaseSemaphore(sem_handles_[sem_index], 1, &previous_count)) {
        DWORD error = GetLastError();
        
        // ERROR_TOO_MANY_POSTS (298) means the semaphore is already at its maximum count
        if (error == 298) {
            LOG_DEBUG("SemaphoreSignal: Semaphore %d already at maximum count", sem_index);
            return true; // Not really an error in our use case
        }
        
        LOG_ERROR("SemaphoreSignal: Failed to signal semaphore %d: %lu", sem_index, error);
        return false;
    }
    
    LOG_DEBUG("SemaphoreSignal: Successfully signaled semaphore %d (previous count: %ld)", 
             sem_index, previous_count);
#else
    if (sem_id_ == -1) {
        LOG_ERROR("Invalid semaphore ID");
        return false;
    }
    
    struct sembuf op;
    op.sem_num = sem_index;
    op.sem_op = 1;   // Increment by 1
    op.sem_flg = 0;  // No special flags
    
    if (semop(sem_id_, &op, 1) == -1) {
        LOG_ERROR("Failed to signal semaphore %d: %s", sem_index, strerror(errno));
        return false;
    }
#endif
    
    return true;
}

bool IPCSharedMemory::CreateSemaphore() {
    LOG_DEBUG("Creating semaphores for IPC channel '%s', is_server=%d", ipc_name_.c_str(), is_server_);
    
#ifdef _WIN32
    // Define semaphore creation parameters
    struct SemaphoreConfig {
        const char* name_suffix;
        LONG initial_count;
        LONG max_count;
        const char* description;
    };

    // Configure each semaphore
    // Write semaphores start at 1 (available for writing)
    // Read semaphores start at 0 (no data available for reading)
    SemaphoreConfig configs[SEM_COUNT] = {
        {"_server_write", 1, 1, "Server write semaphore (mutex)"},
        {"_server_read", 0, 1000, "Server read semaphore (counting)"},
        {"_client_write", 1, 1, "Client write semaphore (mutex)"},
        {"_client_read", 0, 1000, "Client read semaphore (counting)"}
    };
    
    // Create or open each semaphore
    for (int i = 0; i < SEM_COUNT; i++) {
        std::string sem_name = "Local\\" + ipc_name_ + configs[i].name_suffix;
        LOG_DEBUG("Processing semaphore %d (%s)", i, configs[i].description);
        
        // First try to open existing semaphore
        sem_handles_[i] = OpenSemaphoreA(SEMAPHORE_ALL_ACCESS, FALSE, sem_name.c_str());
        
        if (sem_handles_[i] == nullptr) {
            // If client, wait for server to create semaphores
            if (!is_server_) {
                for (int retry = 0; retry < 10; retry++) {
                    LOG_DEBUG("Client waiting for server to create semaphore %d (retry %d)", i, retry);
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    sem_handles_[i] = OpenSemaphoreA(SEMAPHORE_ALL_ACCESS, FALSE, sem_name.c_str());
                    if (sem_handles_[i] != nullptr) {
                        LOG_DEBUG("Client successfully opened semaphore %d after retry %d", i, retry);
                        break;
                    }
                    
                    if (retry == 9) {
                        LOG_ERROR("Client failed to open semaphore %d after 10 retries", i);
                        return false;
                    }
                }
            } else {
                // Server creates the semaphore
                LOG_DEBUG("Server creating semaphore %d (%s) with initial=%ld, max=%ld", 
                         i, configs[i].description, configs[i].initial_count, configs[i].max_count);
                sem_handles_[i] = ::CreateSemaphoreA(nullptr, configs[i].initial_count, 
                                                    configs[i].max_count, sem_name.c_str());
                if (sem_handles_[i] == nullptr) {
                    DWORD error = GetLastError();
                    LOG_ERROR("Failed to create semaphore %d: %lu", i, error);
                    return false;
                }
                LOG_DEBUG("Server successfully created semaphore %d", i);
            }
        } else {
            LOG_DEBUG("Successfully opened existing semaphore %d", i);
        }
    }
    
    // Verify all semaphores were created/opened
    for (int i = 0; i < SEM_COUNT; i++) {
        if (sem_handles_[i] == nullptr) {
            LOG_ERROR("Semaphore %d is null after initialization", i);
            return false;
        }
    }
    
    LOG_DEBUG("All semaphores successfully initialized");
#else
    sem_id_ = semget(sem_key_, SEM_COUNT, IPC_CREAT | SEM_PERMISSIONS);
    if (sem_id_ == -1) {
        LOG_ERROR("Failed to create semaphore: %s", strerror(errno));
        return false;
    }
    
    // Initialize semaphores
    union semun {
        int val;
        struct semid_ds *buf;
        unsigned short *array;
    } sem_union;
    
    // Set initial values: write semaphores = 1, read semaphores = 0
    unsigned short initial_vals[SEM_COUNT];
    initial_vals[SEM_SERVER_WRITE] = 1;
    initial_vals[SEM_SERVER_READ]  = 0;
    initial_vals[SEM_CLIENT_WRITE] = 1;
    initial_vals[SEM_CLIENT_READ]  = 0;
    sem_union.array = initial_vals;
    if (semctl(sem_id_, 0, SETALL, sem_union) == -1) {
        LOG_ERROR("Failed to initialize semaphores with SETALL: %s", strerror(errno));
        return false;
    }
#endif
    return true;
}

bool IPCSharedMemory::SemaphoreTryWait(int sem_index) {
#ifdef _WIN32
    if (sem_handles_[sem_index] == nullptr) {
        LOG_ERROR("Invalid semaphore handle for index %d", sem_index);
        return false;
    }
    
    // For read semaphores (counting semaphores), we need to check if there's data available
    // For write semaphores (mutex semaphores), we need to check if we can acquire the lock
    bool is_read_sem = (sem_index == SEM_SERVER_READ || sem_index == SEM_CLIENT_READ);
    
    LOG_DEBUG("SemaphoreTryWait: Attempting to acquire %s semaphore %d", 
             is_read_sem ? "read" : "write", sem_index);
    
    // For non-blocking wait, use a timeout of 0
    DWORD timeout = 0;
    
    // Try to acquire the semaphore
    DWORD result = WaitForSingleObject(sem_handles_[sem_index], timeout);
    
    if (result == WAIT_OBJECT_0) {
        LOG_DEBUG("SemaphoreTryWait: Successfully acquired semaphore %d", sem_index);
        return true;
    } else if (result == WAIT_TIMEOUT) {
        // This is not an error, just means the semaphore is not available
        LOG_DEBUG("SemaphoreTryWait: Semaphore %d not available (timeout)", sem_index);
        return false;
    } else if (result == WAIT_FAILED) {
        DWORD error = GetLastError();
        LOG_ERROR("SemaphoreTryWait: Failed to wait for semaphore %d: %lu", sem_index, error);
        return false;
    } else {
        LOG_ERROR("SemaphoreTryWait: Unexpected result %lu for semaphore %d", result, sem_index);
        return false;
    }
#else
    if (sem_id_ == -1) {
        LOG_ERROR("Invalid semaphore ID");
        return false;
    }
    struct sembuf op;
    op.sem_num = sem_index;
    op.sem_op = -1;      // decrement
    op.sem_flg = IPC_NOWAIT; // non-blocking
    if (semop(sem_id_, &op, 1) == -1) {
        if (errno == EAGAIN) {
            return false; // not available
        }
        LOG_ERROR("Failed to try-wait semaphore %d: %s", sem_index, strerror(errno));
        return false;
    }
    return true;
#endif
}

bool IPCSharedMemory::SemaphoreWait(int sem_index) {
#ifdef _WIN32
    if (sem_handles_[sem_index] == nullptr) {
        LOG_ERROR("Invalid semaphore handle");
        return false;
    }
    
    DWORD result = WaitForSingleObject(sem_handles_[sem_index], INFINITE);
    if (result != WAIT_OBJECT_0) {
        LOG_ERROR("Failed to wait for semaphore: %lu", GetLastError());
        return false;
    }
#else
    if (sem_id_ == -1) {
        LOG_ERROR("Invalid semaphore ID");
        return false;
    }
    
    struct sembuf op;
    op.sem_num = sem_index;
    op.sem_op = -1;
    op.sem_flg = 0;
    
    if (semop(sem_id_, &op, 1) == -1) {
        LOG_ERROR("Failed to wait for semaphore %d: %s", sem_index, strerror(errno));
        return false;
    }
#endif
    return true;
}

bool IPCSharedMemory::DestroySemaphore() {
#ifdef _WIN32
    for (int i = 0; i < SEM_COUNT; i++) {
        if (sem_handles_[i] != nullptr) {
            CloseHandle(sem_handles_[i]);
            sem_handles_[i] = nullptr;
        }
    }
#else
    if (sem_id_ != -1) {
        if (semctl(sem_id_, 0, IPC_RMID) == -1) {
            LOG_ERROR("Failed to destroy semaphore: %s", strerror(errno));
            return false;
        }
        sem_id_ = -1;
    }
#endif
    return true;
}

bool IPCSharedMemory::DestroySharedMemory() {
#ifdef _WIN32
    if (shm_handle_ != nullptr) {
        CloseHandle(shm_handle_);
        shm_handle_ = nullptr;
    }
#else
    if (shm_id_ != -1) {
        if (shmctl(shm_id_, IPC_RMID, nullptr) == -1) {
            LOG_ERROR("Failed to destroy shared memory: %s", strerror(errno));
            return false;
        }
        shm_id_ = -1;
    }
#endif
    return true;
}

} // namespace ipc
} // namespace SAK
