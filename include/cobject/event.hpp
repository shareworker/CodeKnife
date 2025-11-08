#pragma once
#include <memory>
#include <atomic>
#include <vector>
#include <future>
#include <any>
#include <string>

namespace SAK {

class CObject;  // Forward declaration

class Event {
public:
    enum class Type {
        None = 0,
        Timer = 1,
        ThreadChange = 2,
        MetaCall = 3,
        SocketAct = 4,
        DeferredDelete = 5,
        ChildAdded = 6,
        ChildRemoved = 7,
        User = 1000,
        MaxUser = 65535
    };

    explicit Event(Type type) : type_(type), accepted_(false) {}
    virtual ~Event() = default;

    Type type() const { return type_; }
    bool isAccepted() const { return accepted_; }
    void accept() { accepted_ = true; }
    void ignore() { accepted_ = false; }

    Type type_;
    bool accepted_;
};

class TimerEvent : public Event {
public:
    explicit TimerEvent(int timerId) : Event(Type::Timer), timerId_(timerId) {}
    int timerId() const { return timerId_; }
private:
    int timerId_;
};

class ChildEvent : public Event {
public:
    ChildEvent(Type type, CObject* child) : Event(type), child_(child) {}
    CObject* child() const { return child_; }
private:
    CObject* child_;
};

class MetaCallEvent : public Event {
public:
    MetaCallEvent(const char* slot, std::vector<std::any> args)
        : Event(Type::MetaCall), slot_(slot), args_(std::move(args)), promise_(nullptr) {}
    const char* slot() const { return slot_.c_str(); }
    const std::vector<std::any>& args() const { return args_; }
    void setPromise(std::shared_ptr<std::promise<void>> promise) { promise_ = promise; }
    std::shared_ptr<std::promise<void>> promise() const { return promise_; }

private:
    std::string slot_;  // Store as string to avoid dangling pointer
    std::vector<std::any> args_;
    std::shared_ptr<std::promise<void>> promise_;
};

}