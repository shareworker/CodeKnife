#include "../include/cobject.hpp"
#include "../include/connection_manager.hpp"
#include <algorithm>

namespace SAK {

const MetaObject CObject::staticMetaObject(
    "CObject",
    nullptr,
    nullptr,
    {},
    {},
    {}
);

CObject::CObject(CObject* parent) : parent_(nullptr) {
    setParent(parent);
}

CObject::~CObject() {
    // Disconnect all signal-slot connections involving this object
    ConnectionManager::instance().disconnectAll(this);
    
    // Remove from parent
    setParent(nullptr);
    
    // Delete all children safely
    while (!children_.empty()) {
        CObject* child = children_.back();
        children_.pop_back(); // Remove from list first
        delete child; // Then delete - child's destructor will call setParent(nullptr)
    }
}

void CObject::setParent(CObject* parent) {
    if (parent_ == parent) return;
    if (parent_) parent_->removeChild(this);
    parent_ = parent;
    if (parent_) parent_->addChild(this);
}

void CObject::addChild(CObject* child) {
    if (child && std::find(children_.begin(), children_.end(), child) == children_.end()) {
        children_.push_back(child);
    }
}

void CObject::removeChild(CObject* child) {
    if (child) {
        children_.erase(std::remove(children_.begin(), children_.end(), child), children_.end());
    }
}

bool CObject::setProperty(const std::string& name, const std::any& value) {
    const MetaProperty* prop = metaObject()->findProperty(name);
    if (prop) {
        prop->set(this, value);
        return true;
    }
    return setDynamicProperty(name, value);
}

std::any CObject::property(const std::string& name) const {
    const MetaProperty* prop = metaObject()->findProperty(name);
    if (prop) {
        return prop->get(this);
    }
    return dynamicProperty(name);
}

bool CObject::setDynamicProperty(const std::string& name, const std::any& value) {
    dynamic_properties_[name] = value;
    return true;
}

std::any CObject::dynamicProperty(const std::string& name) const {
    auto it = dynamic_properties_.find(name);
    if (it != dynamic_properties_.end()) {
        return it->second;
    }
    return std::any();
}

std::vector<std::string> CObject::dynamicPropertyNames() const {
    std::vector<std::string> names;
    names.reserve(dynamic_properties_.size()); // Pre-allocate memory
    for (const auto& [key, value] : dynamic_properties_) {
        names.emplace_back(key); // Avoid copy construction
    }
    return names;
}

bool CObject::connect(const CObject* sender, const char* signal, const CObject* receiver, const char* slot, ConnectionType type) {
    return ConnectionManager::instance().connect(sender, signal, receiver, slot, type);
}

bool CObject::disconnect(const CObject* sender, const char* signal, const CObject* receiver, const char* slot) {
    return ConnectionManager::instance().disconnect(sender, signal, receiver, slot);
}

void CObject::emitSignal(const char* signal, const std::vector<std::any>& args) {
    ConnectionManager::instance().emitSignal(this, signal, args);
}

}
