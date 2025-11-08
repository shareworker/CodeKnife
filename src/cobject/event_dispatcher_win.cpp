
#include <chrono>
#include <winsock2.h>
#include <windows.h>

#include "event_dispatcher_win.hpp"
#include "event.hpp"
#include "cobject.hpp"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")

namespace SAK {

enum {
    WM_SAK_SOCKETNOTIFIER = WM_USER,
    WM_SAK_WAKEUP = WM_USER + 1
};

static int64_t getCurrentTime() {
    using namespace std::chrono;
    auto t = duration_cast<milliseconds>(steady_clock::now().time_since_epoch());
    return t.count();
}

LRESULT CALLBACK sak_internal_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);

static HWND createInternalWindow(EventDispatcherWin* dispatcher) {
    static const wchar_t* className = L"SAK_EventDispatcherWin_Internal";
    static bool registered = false;

    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = sak_internal_proc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = className;

        if (!RegisterClassW(&wc)) {
            return nullptr;
        }
        registered = true;
    }

    HWND hwnd = CreateWindowW(
        className,
        className,
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        GetModuleHandle(nullptr),
        nullptr
    );

    if (hwnd) {
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dispatcher));
    }

    return hwnd;
}

LRESULT CALLBACK sak_internal_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_NCCREATE) {
        return TRUE;
    }

    auto* dispatcher = reinterpret_cast<EventDispatcherWin*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (message) {
    case WM_TIMER: {
        if (dispatcher) {
            int timerId = static_cast<int>(wp);
            for (auto* t : dispatcher->timerVec_) {
                if (t->timerId == timerId && !t->inTimerEvent) {
                    t->inTimerEvent = true;
                    TimerEvent ev(timerId);
                    CObject::sendEvent(t->receiver, &ev);
                    t->inTimerEvent = false;

                    t->timeout = getCurrentTime() + t->interval;
                    break;
                }
            }
        }
        return 0;
    }

    case WM_SAK_SOCKETNOTIFIER: {
        if (dispatcher) {
            int event = WSAGETSELECTEVENT(lp);
            int fd = static_cast<int>(wp);

            int type = -1;
            if (event == FD_READ || event == FD_ACCEPT || event == FD_CLOSE) {
                type = static_cast<int>(SocketNotifierType::Read);
            } else if (event == FD_WRITE || event == FD_CONNECT) {
                type = static_cast<int>(SocketNotifierType::Write);
            } else if (event == FD_OOB) {
                type = static_cast<int>(SocketNotifierType::Exception);
            }

            if (type >= 0) {
                auto it = dispatcher->socketNotifiers_.find(fd);
                if (it != dispatcher->socketNotifiers_.end()) {
                    for (auto* sn : it->second) {
                        if (static_cast<int>(sn->notifier->type) == type) {
                            Event ev(Event::Type::SocketAct);
                            CObject::sendEvent(sn->notifier->receiver, &ev);
                            break;
                        }
                    }
                }
            }
        }
        return 0;
    }

    case WM_SAK_WAKEUP:
        return 0;

    default:
        break;
    }

    return DefWindowProc(hwnd, message, wp, lp);
}

AUTO_REGISTER_META_OBJECT(EventDispatcherWin, EventDispatcher)

EventDispatcherWin::EventDispatcherWin(CObject* parent)
    : EventDispatcher(parent),
      internalHwnd_(nullptr),
      interrupt_(false)
{
    startingUp();
}

EventDispatcherWin::~EventDispatcherWin() {
    shuttingDown();
}

void EventDispatcherWin::startingUp() {
    internalHwnd_ = createInternalWindow(this);
}

void EventDispatcherWin::shuttingDown() {
    for (auto* t : timerVec_) {
        unregisterWinTimer(t);
        delete t;
    }
    timerVec_.clear();

    for (auto& pair : socketNotifiers_) {
        for (auto* sn : pair.second) {
            delete sn;
        }
    }
    socketNotifiers_.clear();

    if (internalHwnd_) {
        DestroyWindow(internalHwnd_);
        internalHwnd_ = nullptr;
    }
}

bool EventDispatcherWin::processEvents() {
    MSG msg;
    bool retVal = false;

    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return false;
        }

        if (interrupt_) {
            interrupt_ = false;
            return retVal;
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);
        retVal = true;
    }

    return retVal;
}

void EventDispatcherWin::wakeUp() {
    if (internalHwnd_) {
        PostMessage(internalHwnd_, WM_SAK_WAKEUP, 0, 0);
    }
}

void EventDispatcherWin::interrupt() {
    interrupt_ = true;
    wakeUp();
}

void EventDispatcherWin::registerTimer(int timerId, int64_t interval, CObject* receiver) {
    if (!internalHwnd_ || !receiver) return;

    for (auto* t : timerVec_) {
        if (t->timerId == timerId) {
            return;
        }
    }

    auto* t = new WinTimerInfo(timerId, interval, receiver);
    t->timeout = getCurrentTime() + interval;
    timerVec_.push_back(t);

    registerWinTimer(t);
}

bool EventDispatcherWin::unregisterTimer(int timerId) {
    for (auto it = timerVec_.begin(); it != timerVec_.end(); ++it) {
        if ((*it)->timerId == timerId) {
            unregisterWinTimer(*it);
            delete *it;
            timerVec_.erase(it);
            return true;
        }
    }
    return false;
}

bool EventDispatcherWin::unregisterTimers(CObject* object) {
    if (!object) return false;

    bool result = false;
    for (auto it = timerVec_.begin(); it != timerVec_.end(); ) {
        if ((*it)->receiver == object) {
            unregisterWinTimer(*it);
            delete *it;
            it = timerVec_.erase(it);
            result = true;
        } else {
            ++it;
        }
    }
    return result;
}

int EventDispatcherWin::remainingTime(int timerId) {
    int64_t now = getCurrentTime();
    for (auto* t : timerVec_) {
        if (t->timerId == timerId) {
            int64_t remaining = t->timeout - now;
            return remaining > 0 ? static_cast<int>(remaining) : 0;
        }
    }
    return -1;
}

void EventDispatcherWin::registerSocketNotifier(SocketNotifier* notifier) {
    if (!notifier || !internalHwnd_) return;

    int fd = notifier->socket;
    auto& vec = socketNotifiers_[fd];

    for (auto* sn : vec) {
        if (sn->notifier == notifier) {
            return;
        }
    }

    auto* sn = new WinSocketNotifier{notifier, fd};
    vec.push_back(sn);

    long event = 0;
    for (auto* s : vec) {
        switch (s->notifier->type) {
        case SocketNotifierType::Read:
            event |= FD_READ | FD_ACCEPT | FD_CLOSE;
            break;
        case SocketNotifierType::Write:
            event |= FD_WRITE | FD_CONNECT;
            break;
        case SocketNotifierType::Exception:
            event |= FD_OOB;
            break;
        }
    }

    WSAAsyncSelect(fd, internalHwnd_, WM_SAK_SOCKETNOTIFIER, event);
}

void EventDispatcherWin::unregisterSocketNotifier(SocketNotifier* notifier) {
    if (!notifier) return;

    int fd = notifier->socket;
    auto it = socketNotifiers_.find(fd);
    if (it == socketNotifiers_.end()) return;

    auto& vec = it->second;
    for (auto vit = vec.begin(); vit != vec.end(); ++vit) {
        if ((*vit)->notifier == notifier) {
            delete *vit;
            vec.erase(vit);
            break;
        }
    }

    if (vec.empty()) {
        WSAAsyncSelect(fd, internalHwnd_, 0, 0);
        socketNotifiers_.erase(it);
    } else {
        long event = 0;
        for (auto* s : vec) {
            switch (s->notifier->type) {
            case SocketNotifierType::Read:
                event |= FD_READ | FD_ACCEPT | FD_CLOSE;
                break;
            case SocketNotifierType::Write:
                event |= FD_WRITE | FD_CONNECT;
                break;
            case SocketNotifierType::Exception:
                event |= FD_OOB;
                break;
            }
        }
        WSAAsyncSelect(fd, internalHwnd_, WM_SAK_SOCKETNOTIFIER, event);
    }
}

void EventDispatcherWin::registerWinTimer(WinTimerInfo* t) {
    if (!internalHwnd_ || !t) return;

    UINT elapse = static_cast<UINT>(t->interval);
    if (elapse == 0) elapse = 1;

    t->fastTimerId = SetTimer(internalHwnd_, t->timerId, elapse, nullptr);
}

void EventDispatcherWin::unregisterWinTimer(WinTimerInfo* t) {
    if (!internalHwnd_ || !t) return;

    if (t->fastTimerId) {
        KillTimer(internalHwnd_, t->fastTimerId);
        t->fastTimerId = 0;
    }
}

void EventDispatcherWin::activateSocketNotifiers() {}

int64_t EventDispatcherWin::currentTime() const {
    return getCurrentTime();
}

} // namespace SAK
