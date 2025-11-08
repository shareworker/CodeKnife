
#include "meta_object.hpp"
#include "cobject.hpp"
#include <algorithm>
#include <string>

namespace SAK {

MetaObject::MetaObject(const char* className, const MetaObject* parent, FactoryFunc factory, 
                      const std::vector<MetaProperty>& properties, 
                      const std::vector<MetaMethod>& methods, 
                      const std::vector<MetaSignal>& signals) 
    : className_(className), parent_(parent), factory_(std::move(factory)), 
      properties_(properties), methods_(methods), signals_(signals) {}

CObject* MetaObject::createInstance() const {
    if (factory_) {
        return factory_();
    }
    return nullptr;
}

const MetaProperty* MetaObject::findProperty(const std::string& name) const {
    auto it = std::find_if(properties_.begin(), properties_.end(),
        [&name](const MetaProperty& prop) { return name == prop.name(); });

    if (it != properties_.end()) {
        return &(*it);
    }

    if (parent_) {
        return parent_->findProperty(name);
    }

    return nullptr;
}

const MetaMethod* MetaObject::findMethod(const std::string& name) const {
    auto it = std::find_if(methods_.begin(), methods_.end(),
        [&name](const MetaMethod& method) { return name == method.name(); });
    
    if (it != methods_.end()) {
        return &(*it);
    }

    if (parent_) {
        return parent_->findMethod(name);
    }

    return nullptr;
}

const MetaSignal* MetaObject::findSignal(const std::string& name) const {
    auto it = std::find_if(signals_.begin(), signals_.end(),
        [&name](const MetaSignal& signal) { return name == signal.name(); });
    
    if (it != signals_.end()) {
        return &(*it);
    }

    if (parent_) {
        return parent_->findSignal(name);
    }

    return nullptr;
}

bool MetaObject::inherits(const MetaObject* metaObject) const {
    if (!metaObject) return false;
    if (this == metaObject) return true;
    
    const MetaObject* current = parent_;
    while (current) {
        if (current == metaObject) return true;
        current = current->parent();
    }
    return false;
}

} // namespace SAK

