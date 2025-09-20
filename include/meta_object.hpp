#pragma once

#include <string>
#include <functional>
#include <vector>
#include <any>
#include <algorithm>
#include <memory>

namespace SAK {

class CObject;
class MetaProperty;
class MetaMethod;
class MetaSignal;

class MetaProperty {
public:
    using Getter = std::function<std::any(const CObject*)>;
    using Setter = std::function<void(CObject*, const std::any&)>;
    using NotifySignal = std::function<void(CObject*)>;
    
    MetaProperty(const char* name, const char* typeName, Getter getter, Setter setter, NotifySignal notifySignal = nullptr)
        : name_(name), type_name_(typeName), getter_(getter), setter_(setter), notify_signal_(notifySignal) {}
    const char* name() const { return name_; }
    const char* typeName() const { return type_name_; }
    
    std::any get(const CObject* object) const { return getter_(object); }
    void set(CObject* object, const std::any& value) const { 
        setter_(object, value); 
        if (notify_signal_ && object) {
            notify_signal_(object);
        }
    }

    bool hasNotifySignal() const { return notify_signal_ != nullptr; }
    void notify(CObject* object) const { 
        if (notify_signal_ && object) {
            notify_signal_(object);
        }
    }
private:
    const char* name_;
    const char* type_name_;
    Getter getter_;
    Setter setter_;
    NotifySignal notify_signal_;
};

class MetaMethod {
public:
    using Invoker = std::function<std::any(CObject*, const std::vector<std::any>&)>;
    MetaMethod(const char* name, const char* signature, Invoker invoker)
        : name_(name), signature_(signature), invoker_(invoker) {}
    const char* name() const { return name_; }
    const char* signature() const { return signature_; }
    std::any invoke(CObject* object, const std::vector<std::any>& args) const { 
        if (invoker_ && object) {
            return invoker_(object, args);
        }
        return {};
    }
private:
    const char* name_;
    const char* signature_;
    Invoker invoker_;
};

class MetaSignal {
public:
    using Invoker = std::function<void(CObject*, const std::vector<std::any>&)>;

    MetaSignal(const char* name, const char* signature, Invoker invoker)
        : name_(name), signature_(signature), invoker_(invoker) {}
    const char* name() const { return name_; }
    const char* signature() const { return signature_; }
    void invoke(CObject* object, const std::vector<std::any>& args) const { 
        if (invoker_ && object) {
            invoker_(object, args);
        }
    }
private:
    const char* name_;
    const char* signature_;
    Invoker invoker_;
};

class MetaObject {
public:
    using FactoryFunc = std::function<CObject* ()>;
    MetaObject(const char* className, const MetaObject* parent, FactoryFunc factory, const std::vector<MetaProperty>& properties = {},
            const std::vector<MetaMethod>& methods = {}, const std::vector<MetaSignal>& signals = {});
    const char* className() const { return className_; }
    const MetaObject* parent() const { return parent_; }
    CObject* createInstance() const;

    int propertyCount() const { return static_cast<int>(properties_.size()); }
    const MetaProperty* property(int index) const {
        return (index >= 0 && index < propertyCount()) ? &properties_[index] : nullptr;
    }
    const MetaProperty* findProperty(const std::string& name) const;

    int methodCount() const { return static_cast<int>(methods_.size()); }
    const MetaMethod* method(int index) const {
        return (index >= 0 && index < methodCount()) ? &methods_[index] : nullptr;
    }
    const MetaMethod* findMethod(const std::string& name) const;

    int signalCount() const { return static_cast<int>(signals_.size()); }
    const MetaSignal* signal(int index) const {
        return (index >= 0 && index < signalCount()) ? &signals_[index] : nullptr;
    }
    const MetaSignal* findSignal(const std::string& name) const;
    bool inherits(const MetaObject* metaObject) const;

private:
    const char* className_;
    const MetaObject* parent_;
    FactoryFunc factory_;
    std::vector<MetaProperty> properties_;
    std::vector<MetaMethod> methods_;
    std::vector<MetaSignal> signals_;
};

} // namespace SAK
