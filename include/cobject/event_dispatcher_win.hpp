#pragma once
#include "event_dispatcher.hpp"
#include <windows.h>
#include <vector>
#include <unordered_map>

namespace SAK {

struct WinTimerInfo {
    int timerId;
    int64_t interval;
    int64_t timeout;
    CObject* receiver;
    UINT_PTR fastTimerId;
    bool inTimerEvent;
    
    WinTimerInfo(int id, int64_t iv, CObject* obj)
        : timerId(id), interval(iv), timeout(0), receiver(obj),
          fastTimerId(0), inTimerEvent(false) {}
};

struct WinSocketNotifier {
    SocketNotifier* notifier;
    int fd;
};

class EventDispatcherWin : public EventDispatcher {
public:
    DECLARE_OBJECT(EventDispatcherWin)
public:
    explicit EventDispatcherWin(CObject* parent = nullptr);
    ~EventDispatcherWin() override;

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
    HWND internalHwnd_;
    bool interrupt_;
    std::vector<WinTimerInfo*> timerVec_;
    std::unordered_map<int, std::vector<WinSocketNotifier*>> socketNotifiers_;
    
    void registerWinTimer(WinTimerInfo* t);
    void unregisterWinTimer(WinTimerInfo* t);
    void activateSocketNotifiers();
    int64_t currentTime() const;
    
    friend LRESULT CALLBACK sak_internal_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);
};

} // namespace SAK