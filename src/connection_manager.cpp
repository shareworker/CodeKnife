#include "../include/connection_manager.hpp"
#include "../include/cobject.hpp"
#include "../include/meta_object.hpp"
#include <algorithm>
#include <iostream>

namespace SAK {

ConnectionManager& ConnectionManager::instance() {
    static ConnectionManager instance;
    return instance;
}

bool ConnectionManager::connect(const CObject* sender, const char* signal, 
                               const CObject* receiver, const char* slot, 
                               ConnectionType type) {
    if (!sender || !receiver || !signal || !slot) {
        return false;
    }
    
    // Verify that the signal exists in sender's meta object
    const MetaSignal* metaSignal = sender->metaObject()->findSignal(signal);
    if (!metaSignal) {
        return false;
    }
    
    // Verify that the slot exists in receiver's meta object  
    const MetaMethod* metaSlot = receiver->metaObject()->findMethod(slot);
    if (!metaSlot) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    Connection conn = {sender, signal, receiver, slot, type, true};
    
    // Check if connection already exists
    auto it = connections_.find(sender);
    if (it != connections_.end()) {
        auto& connList = it->second;
        auto existingConn = std::find_if(connList.begin(), connList.end(),
            [&conn](const Connection& existing) {
                return existing == conn;
            });
        if (existingConn != connList.end()) {
            return false; // Connection already exists
        }
        connList.push_back(conn);
    } else {
        connections_[sender] = {conn};
    }
    
    return true;
}

bool ConnectionManager::disconnect(const CObject* sender, const char* signal, 
                                  const CObject* receiver, const char* slot) {
    if (!sender) return false;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = connections_.find(sender);
    if (it == connections_.end()) {
        return false;
    }
    
    auto& connList = it->second;
    auto connIt = std::find_if(connList.begin(), connList.end(),
        [signal, receiver, slot](const Connection& conn) {
            bool signalMatch = !signal || conn.signal == signal;
            bool receiverMatch = !receiver || conn.receiver == receiver;
            bool slotMatch = !slot || conn.slot == slot;
            return signalMatch && receiverMatch && slotMatch;
        });
    
    if (connIt != connList.end()) {
        connList.erase(connIt);
        if (connList.empty()) {
            connections_.erase(it);
        }
        return true;
    }
    
    return false;
}

void ConnectionManager::disconnectAll(const CObject* obj) {
    if (!obj) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Remove all connections where obj is sender
    connections_.erase(obj);
    
    // Remove all connections where obj is receiver
    for (auto& [sender, connList] : connections_) {
        connList.erase(
            std::remove_if(connList.begin(), connList.end(),
                [obj](const Connection& conn) {
                    return conn.receiver == obj;
                }),
            connList.end());
    }
    
    // Remove empty connection lists
    auto it = connections_.begin();
    while (it != connections_.end()) {
        if (it->second.empty()) {
            it = connections_.erase(it);
        } else {
            ++it;
        }
    }
}

void ConnectionManager::emitSignal(const CObject* sender, const char* signal, 
                                  const std::vector<std::any>& args) {
    if (!sender || !signal) return;
    
    std::vector<Connection> activeConnections;
    
    // Find all active connections for this signal
    {
        std::lock_guard<std::mutex> lock(mutex_);
        activeConnections = findConnections(sender, signal);
    }
    
    // Invoke slots for each connection
    for (const auto& conn : activeConnections) {
        if (conn.enabled) {
            invokeSlot(conn, args);
        }
    }
}

std::vector<Connection> ConnectionManager::findConnections(const CObject* sender, 
                                                          const char* signal) const {
    std::vector<Connection> result;
    
    auto it = connections_.find(sender);
    if (it == connections_.end()) {
        return result;
    }
    
    const auto& connList = it->second;
    std::copy_if(connList.begin(), connList.end(), std::back_inserter(result),
        [signal](const Connection& conn) {
            return conn.signal == signal && conn.enabled;
        });
    
    return result;
}

void ConnectionManager::invokeSlot(const Connection& conn, const std::vector<std::any>& args) {
    try {
        const MetaMethod* slot = conn.receiver->metaObject()->findMethod(conn.slot);
        if (slot) {
            // Cast away const for method invocation - this is safe as we're calling a slot
            slot->invoke(const_cast<CObject*>(conn.receiver), args);
        }
    } catch (const std::exception& e) {
        // Log error but don't crash the application
        std::cerr << "Error invoking slot " << conn.slot << ": " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown error invoking slot " << conn.slot << std::endl;
    }
}

} // namespace SAK
