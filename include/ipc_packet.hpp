#pragma once

#include <cstdint>
#include <string>
#include <cstring>
#include <chrono>
#include "logger.hpp"

namespace util {
namespace ipc {

// Magic ID "UTIL" in ASCII
constexpr uint32_t IPC_PACKET_MAGIC = 0x5554494C; // "UTIL" in little-endian

// Message types
enum class MessageType : uint8_t {
    REQUEST = 0x01,
    RESPONSE = 0x02,
    HEARTBEAT = 0x03,
    ERROR = 0x04,
    // 0x05-0xFF reserved for future use
};

// Packet header structure
#pragma pack(push, 1)
struct PacketHeader {
    uint32_t magic_id;     // Magic identifier "UTIL"
    uint8_t version;       // Protocol version
    uint8_t msg_type;      // Message type
    uint16_t reserved;     // Reserved for future use
    uint32_t payload_len;  // Length of the payload data
    uint32_t seq_num;      // Sequence number
    uint64_t timestamp;    // Timestamp in milliseconds
};
#pragma pack(pop)

// Complete packet structure
class IPCPacket {
public:
    // Default constructor for creating an empty packet
    IPCPacket()
        : header_{}
        , payload_(nullptr)
        , checksum_(0)
        , total_size_(sizeof(PacketHeader) + sizeof(uint32_t))
    {
        header_.magic_id = IPC_PACKET_MAGIC;
        header_.version = 1;
        header_.msg_type = static_cast<uint8_t>(MessageType::REQUEST);
        header_.reserved = 0;
        header_.payload_len = 0;
        header_.seq_num = 0;
        header_.timestamp = GetCurrentTimestampMs();
        
        // Calculate checksum
        CalculateChecksum();
    }

    // Constructor for creating a new packet
    IPCPacket(MessageType type, uint32_t seq_num, const void* payload = nullptr, uint32_t payload_len = 0)
        : header_{}
        , payload_(nullptr)
        , checksum_(0)
        , total_size_(sizeof(PacketHeader) + payload_len + sizeof(uint32_t))
    {
        header_.magic_id = IPC_PACKET_MAGIC;
        header_.version = 1;
        header_.msg_type = static_cast<uint8_t>(type);
        header_.reserved = 0;
        header_.payload_len = payload_len;
        header_.seq_num = seq_num;
        header_.timestamp = GetCurrentTimestampMs();

        if (payload_len > 0 && payload != nullptr) {
            try {
                payload_ = new uint8_t[payload_len];
                std::memcpy(payload_, payload, payload_len);
            } catch (const std::bad_alloc& e) {
                payload_ = nullptr;
                header_.payload_len = 0;
                total_size_ = sizeof(PacketHeader) + sizeof(uint32_t);
            }
        }

        // Calculate checksum
        CalculateChecksum();
    }

    // Constructor for parsing received data
    IPCPacket(const void* data, uint32_t data_size)
        : payload_(nullptr)
        , checksum_(0)
        , total_size_(0)
    {
        if (!data || data_size < sizeof(PacketHeader) + sizeof(uint32_t)) {
            return; // Invalid data or not enough data for a valid packet
        }

        // Copy header
        std::memcpy(&header_, data, sizeof(PacketHeader));

        // Validate magic ID and size
        if (header_.magic_id != IPC_PACKET_MAGIC || header_.payload_len > data_size - sizeof(PacketHeader) - sizeof(uint32_t)) {
            header_.magic_id = 0; // Mark as invalid
            return;
        }

        // Calculate total size
        total_size_ = sizeof(PacketHeader) + header_.payload_len + sizeof(uint32_t);

        // Copy payload if present
        if (header_.payload_len > 0) {
            try {
                payload_ = new uint8_t[header_.payload_len];
                std::memcpy(payload_, static_cast<const uint8_t*>(data) + sizeof(PacketHeader), header_.payload_len);
            } catch (const std::bad_alloc& e) {
                header_.magic_id = 0; // Mark as invalid
                header_.payload_len = 0;
                total_size_ = 0;
                return;
            }
        }

        // Copy checksum
        std::memcpy(&checksum_, static_cast<const uint8_t*>(data) + sizeof(PacketHeader) + header_.payload_len, sizeof(uint32_t));
    }
    
    // Copy constructor
    IPCPacket(const IPCPacket& other)
        : header_(other.header_)
        , payload_(nullptr)
        , checksum_(other.checksum_)
        , total_size_(other.total_size_)
    {
        if (other.payload_ && other.header_.payload_len > 0) {
            payload_ = new uint8_t[other.header_.payload_len];
            std::memcpy(payload_, other.payload_, other.header_.payload_len);
        }
    }
    
    // Assignment operator
    IPCPacket& operator=(const IPCPacket& other) {
        if (this != &other) {
            delete[] payload_;
            payload_ = nullptr;
            
            header_ = other.header_;
            checksum_ = other.checksum_;
            total_size_ = other.total_size_;
            
            if (other.payload_ && other.header_.payload_len > 0) {
                payload_ = new uint8_t[other.header_.payload_len];
                std::memcpy(payload_, other.payload_, other.header_.payload_len);
            }
        }
        return *this;
    }

    // Destructor
    ~IPCPacket() {
        delete[] payload_;
    }

    // Serialize packet to buffer
    bool Serialize(void* buffer, uint32_t buffer_size) const {
        if (buffer_size < total_size_) {
            return false; // Buffer too small
        }

        // Copy header
        std::memcpy(buffer, &header_, sizeof(PacketHeader));

        // Copy payload if present
        if (header_.payload_len > 0 && payload_ != nullptr) {
            std::memcpy(static_cast<uint8_t*>(buffer) + sizeof(PacketHeader), payload_, header_.payload_len);
        }

        // Copy checksum
        std::memcpy(static_cast<uint8_t*>(buffer) + sizeof(PacketHeader) + header_.payload_len, &checksum_, sizeof(uint32_t));

        return true;
    }
    
    // Serialize packet to string
    std::string Serialize() const {
        std::string result;
        result.resize(total_size_);
        
        if (Serialize(&result[0], total_size_)) {
            return result;
        }
        
        return "";
    }

    // Getters
    const PacketHeader& GetHeader() const { return header_; }
    const uint8_t* GetPayload() const { return payload_; }
    uint32_t GetPayloadLength() const { return header_.payload_len; }
    uint32_t GetChecksum() const { return checksum_; }
    uint32_t GetTotalSize() const { return total_size_; }
    MessageType GetMessageType() const { return static_cast<MessageType>(header_.msg_type); }
    uint32_t GetSequenceNumber() const { return header_.seq_num; }
    uint64_t GetTimestamp() const { return header_.timestamp; }

    // Validate the packet's checksum and structure
    bool IsValid() const {
        if (header_.magic_id != IPC_PACKET_MAGIC || total_size_ == 0) {
            LOG_ERROR("Invalid magic ID %u or total size %u", header_.magic_id, total_size_);
            return false;
        }
        if (header_.payload_len > 0 && payload_ == nullptr) {
            LOG_ERROR("Invalid payload");
            return false;
        }
        uint32_t calculated_checksum = CalculateChecksumInternal();
        LOG_DEBUG("Calculated checksum: %u, stored checksum: %u", calculated_checksum, checksum_);
        return calculated_checksum == checksum_;
    }

private:
    // Calculate CRC32 checksum
    void CalculateChecksum() {
        checksum_ = CalculateChecksumInternal();
    }

    uint32_t CalculateChecksumInternal() const {
        // Simple CRC32 implementation
        // In a real implementation, you would use a standard CRC32 algorithm
        uint32_t crc = 0xFFFFFFFF;
        
        // Include header in checksum
        const uint8_t* header_bytes = reinterpret_cast<const uint8_t*>(&header_);
        for (size_t i = 0; i < sizeof(PacketHeader); ++i) {
            crc ^= header_bytes[i];
            for (int j = 0; j < 8; ++j) {
                crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
            }
        }
        
        // Include payload in checksum if present
        if (header_.payload_len > 0 && payload_ != nullptr) {
            for (uint32_t i = 0; i < header_.payload_len; ++i) {
                crc ^= payload_[i];
                for (int j = 0; j < 8; ++j) {
                    crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
                }
            }
        }
        
        return ~crc;
    }

    // Get current timestamp in milliseconds
    static uint64_t GetCurrentTimestampMs() {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }

    PacketHeader header_;
    uint8_t* payload_;
    uint32_t checksum_;
    uint32_t total_size_;
};

} // namespace ipc
} // namespace util
