#pragma once
#include "meta_registry.hpp"
#include "meta_object.hpp"
#include "connection_types.hpp"
#include <vector>
#include <any>
#include <unordered_map>
#include <memory>
#include <string>

namespace SAK {

class ConnectionManager;

class CObject {
public:
    static const MetaObject staticMetaObject;
    CObject(CObject* parent = nullptr);
    virtual ~CObject();
    virtual const MetaObject* metaObject() const = 0;
    void setParent(CObject* parent);
    CObject* parent() const { return parent_; }
    const std::vector<CObject*>& children() const { return children_; }
    void setObjectName(const std::string& name) { object_name_ = name; }
    const std::string& objectName() const { return object_name_; }
    bool setProperty(const std::string& name, const std::any& value);
    std::any property(const std::string& name) const;
    bool setDynamicProperty(const std::string& name, const std::any& value);
    std::any dynamicProperty(const std::string& name) const;
    std::vector<std::string> dynamicPropertyNames() const;
    static bool connect(const CObject* sender, const char* signal, const CObject* receiver, const char* slot, ConnectionType type = ConnectionType::kDirectConnection);
    static bool disconnect(const CObject* sender, const char* signal, const CObject* receiver, const char* slot);

protected:
    void emitSignal(const char* signal, const std::vector<std::any>& args = {});
    
private:
    void removeChild(CObject* child);
    void addChild(CObject* child);
    friend class ConnectionManager;

    std::string object_name_;
    CObject* parent_ = nullptr;
    std::vector<CObject*> children_;
    std::unordered_map<std::string, std::any> dynamic_properties_;
};

#define DECLARE_OBJECT(className) \
public: \
    static const SAK::MetaObject staticMetaObject; \
    virtual const SAK::MetaObject* metaObject() const override { return &staticMetaObject; } \
    static SAK::CObject* createInstance() { return new className(); } \
private: \
    static std::vector<SAK::MetaProperty> __properties(); \
    static std::vector<SAK::MetaMethod> __methods(); \
    static std::vector<SAK::MetaSignal> __signals(); \
  
#define REGISTER_OBJECT(className, parentClassName) \
    const SAK::MetaObject className::staticMetaObject( \
        #className, \
        &parentClassName::staticMetaObject, \
        &className::createInstance, \
        className::__properties(), \
        className::__methods(), \
        className::__signals() \
    ); \

#define PROPERTY(type, name) \
private: \
    type name##_; \
public: \
    type name() const { return name##_; } \
    void set##name(const type& value) { \
        if (name##_ != value) { \
            name##_ = value; \
            emitSignal(#name "Changed"); \
        } \
    }

#define SIGNAL(signature) \
    static constexpr const char* signal_##signature = #signature;

#define SLOT(returnType, name, ...) \
    returnType name(__VA_ARGS__); \
    static constexpr const char* slot_##name = #name;

} // namespace SAK
