
#include "../include/cobject/meta_registry.hpp"
#include "../include/cobject/meta_object.hpp"

namespace SAK {

MetaRegistry& MetaRegistry::instance() {
    static MetaRegistry instance;
    return instance;
}

void MetaRegistry::registerMeta(const MetaObject* metaObject) {
    if (!metaObject) return;
    std::lock_guard<std::mutex> lock(mutex_);
    metaMap_[metaObject->className()] = metaObject;
}

const MetaObject* MetaRegistry::findMeta(const std::string& className) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = metaMap_.find(className);
    return it != metaMap_.end() ? it->second : nullptr;
}

CObject* MetaRegistry::createInstance(const std::string& className) const {
    const MetaObject* metaObject = findMeta(className);
    if (!metaObject) return nullptr;
    return metaObject->createInstance();
}

std::vector<std::string> MetaRegistry::registeredClasses() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> classes;
    for (const auto& it : metaMap_) {
        classes.push_back(it.first);
    }
    return classes;
}

bool MetaRegistry::isClassRegistered(const std::string& className) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return metaMap_.find(className) != metaMap_.end();
}

} // namespace SAK

