#pragma once

#include <unordered_map>
#include <vector>
#include <functional>
#include <string>
#include <memory>
#include <mutex>
#include <any>
#include "meta_object.hpp"
#include "connection_types.hpp"

namespace SAK {

class CObject;
struct Connection {
    const CObject* sender;
    std::string signal;
    const CObject* receiver;
    std::string slot;
    ConnectionType type;
    bool enabled;

    bool operator==(const Connection& other) const {
        return sender == other.sender &&
               signal == other.signal &&
               receiver == other.receiver &&
               slot == other.slot;
    }
};

struct ConnectionHash {
    std::size_t operator()(const Connection& conn) const {
        std::size_t h1 = std::hash<const void*>{}(conn.sender);
        std::size_t h2 = std::hash<std::string>{}(conn.signal);
        std::size_t h3 = std::hash<const void*>{}(conn.receiver);
        std::size_t h4 = std::hash<std::string>{}(conn.slot);
        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
    }
};

class ConnectionManager {
public:
    static ConnectionManager& instance();
    bool connect(const CObject* sender, const char* signal, const CObject* receiver, const char* slot, ConnectionType type);
    bool disconnect(const CObject* sender, const char* signal, const CObject* receiver, const char* slot);
    void disconnectAll(const CObject* obj);
    void emitSignal(const CObject* sender, const char* signal, const std::vector<std::any>& args={});

private:
    ConnectionManager() = default;
    ~ConnectionManager() = default;
    ConnectionManager(const ConnectionManager&) = delete;
    ConnectionManager& operator=(const ConnectionManager&) = delete;

    std::vector<Connection> findConnections(const CObject* sender, const char* signal) const;
    void invokeSlot(const Connection& conn, const std::vector<std::any>& args);

    std::unordered_map<const CObject*, std::vector<Connection>> connections_;
    mutable std::mutex mutex_;

};

}

