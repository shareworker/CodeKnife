#pragma once

#include <vector>
#include <any>
#include <unordered_map>
#include <string>
#include <thread>
#include "meta_registry.hpp"
#include "meta_object.hpp"
#include "meta_registrar.hpp"
#include "invoker_helper.hpp"
#include "connection_types.hpp"
#include "event.hpp"

namespace SAK {

class ConnectionManager;

class CObject {
public:
    static const MetaObject staticMetaObject;
    CObject(CObject* parent = nullptr);
    virtual ~CObject();
    virtual const MetaObject* metaObject() const = 0;
    virtual bool event(Event* event);
    virtual void timerEvent(TimerEvent* event) { (void)event; }
    virtual void childEvent(ChildEvent* event) { (void)event; }
    void deleteLater();
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
    static bool sendEvent(CObject* receiver, Event* event) {
        if (!receiver || !event)
            return false;
        return receiver->event(event);
    }

    int startTimer(int64_t interval);
    void killTimer(int timerId);
    bool unregisterTimers();
    std::thread::id thread() const { return thread_id_; }

protected:
    void emitSignal(const char* signal, const std::vector<std::any>& args = {});
    
    // Qt-style metacall for slot invocation
    void metacall(const char* slot, const std::vector<std::any>& args, 
                  ConnectionType type, const CObject* sender);
    
private:
    void removeChild(CObject* child);
    void addChild(CObject* child);
    friend class ConnectionManager;

    std::string object_name_;
    CObject* parent_ = nullptr;
    std::vector<CObject*> children_;
    std::unordered_map<std::string, std::any> dynamic_properties_;
    std::thread::id thread_id_{std::this_thread::get_id()};
};

#define DECLARE_OBJECT(className) \
public: \
    static const MetaObject staticMetaObject; \
    virtual const MetaObject* metaObject() const override { return &staticMetaObject; } \
    static CObject* createInstance() { return new className(); } \
private: \
    static std::vector<MetaProperty> __properties(); \
    static std::vector<MetaMethod> __methods(); \
    static std::vector<MetaSignal> __signals(); \
public:

#define REGISTER_OBJECT(className, parentClassName) \
    const MetaObject className::staticMetaObject( \
        #className, \
        &parentClassName::staticMetaObject, \
        &className::createInstance, \
        className::__properties(), \
        className::__methods(), \
        className::__signals() \
    );

#define PROPERTY(className, type, name) \
private: \
    type name##_; \
public: \
    type name() const { return name##_; } \
    void set##name(const type& value) { \
        if (name##_ != value) { \
            name##_ = value; \
            emitSignal(#name "Changed"); \
        } \
    } \
private: \
    static bool __register_property_##name() { \
        MetaRegistrar<className>::registerProperty( \
            #name, #type, \
            [](const CObject* obj) -> std::any { \
                return static_cast<const className*>(obj)->name(); \
            }, \
            [](CObject* obj, const std::any& v) { \
                static_cast<className*>(obj)->set##name(std::any_cast<type>(v)); \
            } \
        ); \
        return true; \
    } \
    inline static const bool name##_registered_ = __register_property_##name();

#define SIGNAL(className, signature, signatureStr) \
private: \
    inline static const bool signal_##signature##_registered_ = []() { \
        MetaRegistrar<className>::registerSignal(#signature, signatureStr); \
        return true; \
    }(); \
public: \
    static constexpr const char* signal_##signature = #signature;

#define SLOT(className, returnType, name, signature, ...) \
public: \
    returnType name(__VA_ARGS__); \
private: \
    inline static const bool slot_##name##_registered_ = []() { \
        MetaRegistrar<className>::registerMethod( \
            #name, signature, \
            make_invoker(&className::name) \
        ); \
        return true; \
    }(); \
public: \
    static constexpr const char* slot_##name = #name;

#define EMIT_SIGNAL(signalName, ...) \
    emitSignal(#signalName, std::vector<std::any>{__VA_ARGS__})
#define AUTO_REGISTER_META_OBJECT(className, parentClassName) \
std::vector<MetaProperty> className::__properties() { \
    std::vector<MetaProperty> props; \
    const auto& regProps = MetaRegistrar<className>::getProperties(); \
    for (const auto& p : regProps) { \
        props.emplace_back(p.name, p.typeName, p.getter, p.setter); \
    } \
    return props; \
} \
std::vector<MetaMethod> className::__methods() { \
    std::vector<MetaMethod> meths; \
    const auto& regMeths = MetaRegistrar<className>::getMethods(); \
    for (const auto& m : regMeths) { \
        meths.emplace_back(m.name, m.signature, m.invoker); \
    } \
    return meths; \
} \
std::vector<MetaSignal> className::__signals() { \
    std::vector<MetaSignal> sigs; \
    const auto& regSigs = MetaRegistrar<className>::getSignals(); \
    for (const auto& s : regSigs) { \
        sigs.emplace_back(s.name, s.signature, [](CObject*, const std::vector<std::any>&) {}); \
    } \
    return sigs; \
} \
REGISTER_OBJECT(className, parentClassName)
} // namespace SAK
