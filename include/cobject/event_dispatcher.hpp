#pragma once
#include "cobject.hpp"

namespace SAK {

enum class SocketNotifierType {
    Read,
    Write,
    Exception
};

struct SocketNotifier {
    int socket;
    SocketNotifierType type;
    CObject* receiver;
    bool enable = true;
};

class AbstractEventDispatcher: public CObject {
public:
    AbstractEventDispatcher(CObject* parent = nullptr) : CObject(parent) {}
    virtual ~AbstractEventDispatcher() = default;
};


class EventDispatcher : public AbstractEventDispatcher {
public:
    EventDispatcher(CObject* parent = nullptr) : AbstractEventDispatcher(parent) {}
    virtual ~EventDispatcher() = default;

    virtual bool processEvents() = 0;
    virtual void wakeUp() = 0;
    virtual void interrupt() = 0;

    virtual void registerTimer(int timerId, int64_t interval, CObject* receiver) = 0;
    virtual bool unregisterTimer(int timerId) = 0;
    virtual bool unregisterTimers(CObject* object) = 0;
    virtual int remainingTime(int timerId) = 0;

    virtual void registerSocketNotifier(SocketNotifier* notifier) = 0;
    virtual void unregisterSocketNotifier(SocketNotifier* notifier) = 0;

    virtual void startingUp() {};
    virtual void shuttingDown() {};

};


}