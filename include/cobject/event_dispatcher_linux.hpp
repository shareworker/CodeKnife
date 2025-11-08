#pragma once
#include "event_dispatcher.hpp"

namespace SAK {

typedef struct _GMainContext GMainContext;
struct GPostEventSource;
struct GSocketNotifierSource;
struct GTimerSource;
struct GIdleTimerSource;

class EventDispatcherLinux : public EventDispatcher {
public:
    DECLARE_OBJECT(EventDispatcherLinux)
public:
    explicit EventDispatcherLinux(CObject* parent = nullptr);
    explicit EventDispatcherLinux(GMainContext *context, CObject* parent = nullptr);
    ~EventDispatcherLinux();

    bool processEvents() override;
    void wakeUp() override;
    void interrupt() override;

    void registerTimer(int timerId, int64_t interval, CObject* receiver) override;
    bool unregisterTimer(int timerId) override;
    bool unregisterTimers(CObject* object) override;
    int remainingTime(int timerId) override;

    void registerSocketNotifier(SocketNotifier* notifier) override;
    void unregisterSocketNotifier(SocketNotifier* notifier) override;

    void startingUp() override;
    void shuttingDown() override;

private:
    GMainContext *mainContext_;
    GPostEventSource *postEventSource_;
    GSocketNotifierSource *socketNotifierSource_;
    GTimerSource *timerSource_;
    GIdleTimerSource *idleTimerSource_;
    bool wakeUpCalled_ = true;
};

}