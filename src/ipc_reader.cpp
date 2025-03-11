#include "ipc_reader.hpp"
#include <limits.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstring>
#include "logger.hpp"

// Maximum payload size (10MB)
#define MAX_PAYLOAD_SIZE (10 * 1024 * 1024)

namespace util {
namespace ipc { 


IPCReader::IPCReader(const std::string& ipc_name, IPC_TYPE type, IPC_HANDLE_TYPE handle_type) : IPCHandlerBase(ipc_name, type),
        buffer_{nullptr}, buffer_size_{USHRT_MAX}, read_cursor_{0}, sink_{nullptr}
{
    SetHandleType(handle_type);
}

IPCReader::~IPCReader() {
    delete[] buffer_;
}

bool IPCReader::Init() {
    IPCHandlerBase::Init();
    buffer_ = new char[buffer_size_];
    memset(buffer_, 0, buffer_size_);
    read_cursor_ = 0;
    return true;
}

bool IPCReader::Uninit() {
    delete[] buffer_;
    buffer_ = nullptr;
    read_cursor_ = 0;
    return IPCHandlerBase::Uninit();
}

bool IPCReader::ProcessData() {
    // First read data from the IPC channel
    bool read_success = ReadData();
    
    // If reading was successful, we've already processed any complete packets in ReadData()
    // The sink_->ReceiveMsg() call is done within ReadData() when a valid packet is found
    
    // Log processing status
    if (read_success) {
        LOG_DEBUG("ProcessData: Successfully processed IPC data");
    } else {
        LOG_ERROR("ProcessData: Failed to process IPC data");
    }
    
    return read_success;
}

bool IPCReader::ReadData() {
    LOG_DEBUG("Reading data");
    int fd = GetFD();
    if (fd < 0) {
        LOG_ERROR("Invalid file descriptor");
        return false;
    }

    // Initialize buffer if not already done
    if (buffer_ == nullptr) {
        try {
            buffer_ = new char[buffer_size_];
            read_cursor_ = 0;
        } catch (const std::bad_alloc& e) {
            LOG_ERROR("Failed to allocate buffer: %s", e.what());
            return false;
        }
    }

    // Check how many bytes are available to read
    int bytes_available = 0;
    if (ioctl(fd, FIONREAD, &bytes_available) < 0) {
        LOG_ERROR("Failed to get available bytes: %s", strerror(errno));
        return false;
    }

    if (bytes_available <= 0) {
        return true; // No data available, not an error
    }

    // Calculate how much space we have left in the buffer
    unsigned int space_left = buffer_size_ - read_cursor_;
    
    // If we don't have enough space, reset the buffer or resize it
    if (space_left < static_cast<unsigned int>(bytes_available)) {
        if (read_cursor_ > 0) {
            // Move remaining data to the beginning of the buffer
            memmove(buffer_, buffer_ + read_cursor_, buffer_size_ - read_cursor_);
            read_cursor_ = 0;
            space_left = buffer_size_;
        }
        
        // If we still don't have enough space after moving data, resize the buffer
        if (space_left < static_cast<unsigned int>(bytes_available)) {
            try {
                unsigned int new_size = buffer_size_ + bytes_available;
                char* new_buffer = new char[new_size];
                
                // Copy existing data to new buffer
                memcpy(new_buffer, buffer_, buffer_size_);
                
                // Delete old buffer and update pointers
                delete[] buffer_;
                buffer_ = new_buffer;
                buffer_size_ = new_size;
                space_left = buffer_size_ - read_cursor_;
            } catch (const std::bad_alloc& e) {
                LOG_ERROR("Failed to resize buffer: %s", e.what());
                return false;
            }
        }
    }

    ssize_t bytes_read = read(fd, buffer_ + read_cursor_, bytes_available);
    
    if (bytes_read < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true; // Non-blocking read with no data, not an error
        }
        LOG_ERROR("Failed to read data: %s", strerror(errno));
        return false;
    }

    if (bytes_read == 0) {
        // End of file reached
        return true; // End of file, not an error
    }

    // Process packets in the buffer
    unsigned int processed_bytes = read_cursor_;
    read_cursor_ += bytes_read;

    while (processed_bytes < read_cursor_) {
        // Check if we have enough data for the header
        if (read_cursor_ - processed_bytes < sizeof(PacketHeader)) {
            break; // Not enough data for a header, wait for more
        }

        char* current_pos = buffer_ + processed_bytes;
        uint32_t magic_id;
        memcpy(&magic_id, current_pos, sizeof(uint32_t));

        if (magic_id != IPC_PACKET_MAGIC) {
            LOG_DEBUG("Invalid magic ID: 0x%X at position %u", magic_id, processed_bytes);
            bool found_magic = false;
            unsigned int search_limit = read_cursor_ - sizeof(uint32_t);
            for (unsigned int i = processed_bytes; i <= search_limit; ++i) {
                memcpy(&magic_id, buffer_ + i, sizeof(uint32_t));
                if (magic_id == IPC_PACKET_MAGIC) {
                    processed_bytes = i;
                    found_magic = true;
                    break;
                }
            }
            if (!found_magic) {
                LOG_DEBUG("No valid magic ID found in buffer");
                if (read_cursor_ > 0) {
                    // Move all remaining data to the beginning of the buffer
                    unsigned int remaining_bytes = read_cursor_ - processed_bytes;
                    if (remaining_bytes > 0) {
                        memmove(buffer_, buffer_ + processed_bytes, remaining_bytes);
                    }
                    read_cursor_ = remaining_bytes;
                }
                LOG_WARNING("No valid packet found, remaining data moved to buffer start");
                return true; // Or return false based on requirements
            }
            continue;
        }

        // Read and validate packet header
        PacketHeader header;
        memcpy(&header, current_pos, sizeof(PacketHeader));
        
        // Verify magic ID again to ensure no copy error
        if (header.magic_id != IPC_PACKET_MAGIC) {
            LOG_ERROR("Header magic ID mismatch after copy: 0x%X", header.magic_id);
            processed_bytes += 1; // Move forward by one byte and continue searching
            continue;
        }

        // Validate payload length
        if (header.payload_len > MAX_PAYLOAD_SIZE || header.payload_len == 0) {
            LOG_WARNING("Invalid packet payload size: %u bytes at position %u", header.payload_len, processed_bytes);
            processed_bytes += sizeof(PacketHeader); // Skip the header
            if (read_cursor_ > processed_bytes) {
                // Move remaining data to the beginning of the buffer
                unsigned int remaining_bytes = read_cursor_ - processed_bytes;
                memmove(buffer_, buffer_ + processed_bytes, remaining_bytes);
                read_cursor_ = remaining_bytes;
            }
            continue;
        }

        unsigned int total_packet_size = sizeof(PacketHeader) + header.payload_len + sizeof(uint32_t);

        if (read_cursor_ - processed_bytes < total_packet_size) {
            break; // Wait for more data
        }

        // Create packet object and handle potential exceptions
        std::shared_ptr<IPCPacket> packet;
        try {
            packet = std::make_shared<IPCPacket>(current_pos, total_packet_size);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to create packet: %s", e.what());
            processed_bytes += sizeof(PacketHeader);
            continue;
        }

        if (!packet->IsValid()) {
            LOG_WARNING("Invalid packet received at position %u", processed_bytes);
            processed_bytes += sizeof(PacketHeader); // Skip the header
            if (read_cursor_ > processed_bytes) {
                // Move remaining data to the beginning of the buffer
                unsigned int remaining_bytes = read_cursor_ - processed_bytes;
                memmove(buffer_, buffer_ + processed_bytes, remaining_bytes);
                read_cursor_ = remaining_bytes;
            }
            continue;
        }
        if (sink_) {
            try {
                sink_->ReceiveMsg(*packet);
            } catch (const std::exception& e) {
                LOG_ERROR("Exception in sink callback: %s", e.what());
            } catch (...) {
                LOG_ERROR("Unknown exception in sink callback");
            }
        }

        processed_bytes += total_packet_size;
    }

    // Handle remaining data
    if (processed_bytes == read_cursor_) {
        // All data processed, reset read cursor
        read_cursor_ = 0;
        LOG_INFO("All data processed, reset cursor");
    } else if (processed_bytes > 0) {
        // Move unprocessed data to the beginning of the buffer
        unsigned int remaining_bytes = read_cursor_ - processed_bytes;
        if (remaining_bytes > 0) {
            memmove(buffer_, buffer_ + processed_bytes, remaining_bytes);
        }
        read_cursor_ = remaining_bytes;
        LOG_INFO("Moved %u remaining bytes to buffer start", remaining_bytes);
    } else if (read_cursor_ > 0) {
        // If processed_bytes is 0 and read_cursor_ is not 0, reset cursor directly
        read_cursor_ = 0;
        LOG_INFO("No data processed, reset cursor");
    }

    return true;
}

} // namespace ipc
} // namespace util