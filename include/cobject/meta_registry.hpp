#pragma once

#include <unordered_map>
#include <string>
#include <mutex>
#include <vector>

namespace SAK {

class MetaObject;
class CObject;

class MetaRegistry {
public:
    static MetaRegistry& instance();

    void registerMeta(const MetaObject* metaObject);
    const MetaObject* findMeta(const std::string& className) const;
    CObject* createInstance(const std::string& className) const;
    std::vector<std::string> registeredClasses() const;
    bool isClassRegistered(const std::string& className) const;
    
private:
    MetaRegistry() = default;
    ~MetaRegistry() = default;
    MetaRegistry(const MetaRegistry&) = delete;
    MetaRegistry& operator=(const MetaRegistry&) = delete;

    std::unordered_map<std::string, const MetaObject*> metaMap_;
    mutable std::mutex mutex_;
};

} // namespace SAK
