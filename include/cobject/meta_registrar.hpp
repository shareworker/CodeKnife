#pragma once

#include <vector>
#include <functional>
#include <any>
#include <string>

namespace SAK {

class CObject;
class MetaProperty;
class MetaMethod;
class MetaSignal;

template<typename T>
class MetaRegistrar {
public:
    struct PropertyInfo {
        const char* name;
        const char* typeName;
        std::function<std::any(const CObject*)> getter;
        std::function<void(CObject*, const std::any&)> setter;
    };
    
    struct MethodInfo {
        const char* name;
        const char* signature;
        std::function<std::any(CObject*, const std::vector<std::any>&)> invoker;
    };
    
    struct SignalInfo {
        const char* name;
        const char* signature;
    };
    
    // RAII cleanup guard to automatically release static containers on destruction
    struct CleanupGuard {
        ~CleanupGuard() {
            properties_.clear();
            properties_.shrink_to_fit();
            methods_.clear();
            methods_.shrink_to_fit();
            signals_.clear();
            signals_.shrink_to_fit();
        }
    };
    
    inline static std::vector<PropertyInfo> properties_;
    inline static std::vector<MethodInfo> methods_;
    inline static std::vector<SignalInfo> signals_;
    inline static CleanupGuard cleanup_guard_;
    
    static void registerProperty(const char* name, const char* typeName,
                                 std::function<std::any(const CObject*)> getter,
                                 std::function<void(CObject*, const std::any&)> setter) {
        properties_.push_back({name, typeName, getter, setter});
    }
    
    static void registerMethod(const char* name, const char* signature,
                               std::function<std::any(CObject*, const std::vector<std::any>&)> invoker) {
        methods_.push_back({name, signature, invoker});
    }
    
    static void registerSignal(const char* name, const char* signature) {
        signals_.push_back({name, signature});
    }
    
    static const std::vector<PropertyInfo>& getProperties() { return properties_; }
    static const std::vector<MethodInfo>& getMethods() { return methods_; }
    static const std::vector<SignalInfo>& getSignals() { return signals_; }
};

template<typename Class, typename Func>
struct AutoRegister {
    AutoRegister(Func func) { func(); }
};

} // namespace SAK
