#include <cstdint>
#include <glib.h>
#include <vector>
#include <chrono>
#include <algorithm>

#include "event_dispatcher_linux.hpp"
#include "event.hpp"
#include "cobject.hpp"

namespace SAK {

// ----- Internal helpers/types -----

struct GPollFDWithSocketNotifier {
    GPollFD pollfd;
    SocketNotifier* socketNotifier;
};

struct GSocketNotifierSource {
    GSource source;
    std::vector<GPollFDWithSocketNotifier*> pollfds;
    size_t activeNotifierPos;
};

struct TimerInfo {
    int id;
    int64_t interval;
    int64_t nextTimeout;
    CObject* receiver;
    bool activated = false;
};

struct GTimerSource {
    GSource source;
    std::vector<TimerInfo> timerList;
    bool runWithIdlePriority;
};

struct GIdleTimerSource {
    GSource source;
    GTimerSource* timerSource;
};

static int64_t getCurrentTime() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// ----- GSocketNotifier source -----

static gboolean socketNotifierSourcePrepare(GSource* /*source*/, gint* timeout)
{
    if (timeout) *timeout = -1;
    return false;
}

static gboolean socketNotifierSourceCheck(GSource* source)
{
    auto* s = reinterpret_cast<GSocketNotifierSource*>(source);
    bool pending = false;
    for (size_t i = 0; i < s->pollfds.size(); ++i) {
        auto* pfd = s->pollfds[i];
        if (pfd->pollfd.revents & G_IO_NVAL) {
            pfd->socketNotifier->enable = false;
        } else if (pfd->socketNotifier->enable) {
            pending = pending || ((pfd->pollfd.revents & pfd->pollfd.events) != 0);
        }
    }
    return pending;
}

static gboolean socketNotifierSourceDispatch(GSource* source, GSourceFunc, gpointer)
{
    Event event(Event::Type::SocketAct);
    auto* s = reinterpret_cast<GSocketNotifierSource*>(source);
    for (s->activeNotifierPos = 0; s->activeNotifierPos < s->pollfds.size(); ++s->activeNotifierPos) {
        auto* pfd = s->pollfds[s->activeNotifierPos];
        if (!pfd->socketNotifier->enable)
            continue;
        if ((pfd->pollfd.revents & pfd->pollfd.events) != 0)
            CObject::sendEvent(pfd->socketNotifier->receiver, &event);
    }
    return true;
}

static GSourceFuncs socketNotifierSourceFuncs = {
    socketNotifierSourcePrepare,
    socketNotifierSourceCheck,
    socketNotifierSourceDispatch,
    nullptr,
    nullptr,
    nullptr
};

// ----- GTimer source -----

static int64_t getNextTimerTimeout(GTimerSource* s)
{
    if (s->timerList.empty())
        return -1;
    int64_t now = getCurrentTime();
    int64_t minTimeout = INT64_MAX;
    for (auto& timer : s->timerList) {
        int64_t timeout = timer.nextTimeout - now;
        if (timeout < minTimeout)
            minTimeout = timeout;
    }
    return minTimeout;
}

static gint saturateCast(int64_t value)
{
    if (value < 0) return 0;
    if (value > INT_MAX) return INT_MAX;
    return static_cast<gint>(value);
}

static gboolean timerSourcePrepare(GSource* source, gint* timeout)
{
    auto* s = reinterpret_cast<GTimerSource*>(source);
    if (s->runWithIdlePriority) {
        if (timeout) *timeout = -1;
        return false;
    }
    gint dummy;
    if (!timeout) timeout = &dummy;
    int64_t remaining = getNextTimerTimeout(s);
    if (remaining < 0) { *timeout = -1; return false; }
    if (remaining <= 0) { *timeout = 0; return true; }
    *timeout = saturateCast(remaining);
    return false;
}

static gboolean timerSourceCheck(GSource* source)
{
    auto* s = reinterpret_cast<GTimerSource*>(source);
    if (s->runWithIdlePriority) return false;
    if (s->timerList.empty()) return false;
    int64_t now = getCurrentTime();
    for (const auto& timer : s->timerList) {
        if (timer.nextTimeout <= now)
            return true;
    }
    return false;
}

static gboolean timerSourceDispatch(GSource* source, GSourceFunc, gpointer)
{
    auto* s = reinterpret_cast<GTimerSource*>(source);
    int64_t now = getCurrentTime();
    for (auto& timer : s->timerList) {
        if (timer.nextTimeout <= now) {
            timer.activated = true;
            timer.nextTimeout = now + timer.interval;
        }
    }
    return true;
}

static GSourceFuncs timerSourceFuncs = {
    timerSourcePrepare,
    timerSourceCheck,
    timerSourceDispatch,
    nullptr,
    nullptr,
    nullptr
};

// ----- Idle timer source -----

static gboolean idleTimerSourcePrepare(GSource* /*source*/, gint* timeout)
{
    if (timeout) *timeout = -1;
    return false;
}

static gboolean idleTimerSourceCheck(GSource* /*source*/)
{
    return false;
}

static gboolean idleTimerSourceDispatch(GSource* source, GSourceFunc, gpointer)
{
    auto* s = reinterpret_cast<GIdleTimerSource*>(source);
    if (!s->timerSource) return true;
    auto* time_source = s->timerSource;
    time_source->runWithIdlePriority = true;
    int64_t now = getCurrentTime();
    for (auto& timer : time_source->timerList) {
        if (timer.nextTimeout <= now) {
            timer.activated = true;
            timer.nextTimeout = now + timer.interval;
        }
    }
    return true;
}

static GSourceFuncs idleTimerSourceFuncs = {
    idleTimerSourcePrepare,
    idleTimerSourceCheck,
    idleTimerSourceDispatch,
    nullptr,
    nullptr,
    nullptr
};

// ----- EventDispatcherLinux implementation -----

AUTO_REGISTER_META_OBJECT(EventDispatcherLinux, EventDispatcher)

EventDispatcherLinux::EventDispatcherLinux(CObject* parent)
    : EventDispatcher(parent)
    , mainContext_(g_main_context_default())
    , postEventSource_(nullptr)
    , socketNotifierSource_(nullptr)
    , timerSource_(nullptr)
    , idleTimerSource_(nullptr)
    , wakeUpCalled_(true)
{
    startingUp();
}

EventDispatcherLinux::EventDispatcherLinux(GMainContext* context, CObject* parent)
    : EventDispatcher(parent)
    , mainContext_(context ? context : g_main_context_default())
    , postEventSource_(nullptr)
    , socketNotifierSource_(nullptr)
    , timerSource_(nullptr)
    , idleTimerSource_(nullptr)
    , wakeUpCalled_(true)
{
    startingUp();
}

EventDispatcherLinux::~EventDispatcherLinux()
{
    shuttingDown();
}

bool EventDispatcherLinux::processEvents()
{
    g_main_context_iteration(mainContext_, false);
    if (timerSource_) {
        auto* s = timerSource_;
        for (auto& t : s->timerList) {
            if (t.activated) {
                t.activated = false;
                TimerEvent ev(t.id);
                CObject::sendEvent(t.receiver, &ev);
            }
        }
        s->runWithIdlePriority = false;
    }
    return true;
}

void EventDispatcherLinux::wakeUp()
{
    g_main_context_wakeup(mainContext_);
}

void EventDispatcherLinux::interrupt()
{
    g_main_context_wakeup(mainContext_);
}

void EventDispatcherLinux::registerTimer(int timerId, int64_t interval, CObject* receiver)
{
    if (!timerSource_) return;
    int64_t now = getCurrentTime();
    auto& list = timerSource_->timerList;
    auto it = std::find_if(list.begin(), list.end(), [timerId](const TimerInfo& t) { return t.id == timerId; });
    if (it != list.end()) {
        it->interval = interval;
        it->receiver = receiver;
        it->nextTimeout = now + interval;
        it->activated = false;
        return;
    }
    TimerInfo timer;
    timer.id = timerId;
    timer.interval = interval;
    timer.receiver = receiver;
    timer.nextTimeout = now + interval;
    timer.activated = false;
    list.push_back(timer);
}

bool EventDispatcherLinux::unregisterTimer(int timerId)
{
    if (!timerSource_) return false;
    auto& list = timerSource_->timerList;
    auto it = std::find_if(list.begin(), list.end(), [timerId](const TimerInfo& t) { return t.id == timerId; });
    if (it != list.end()) {
        list.erase(it);
        return true;
    }
    return false;
}

bool EventDispatcherLinux::unregisterTimers(CObject* object)
{
    if (!timerSource_ || !object) return false;
    auto& list = timerSource_->timerList;
    auto oldSize = list.size();
    list.erase(std::remove_if(list.begin(), list.end(), [object](const TimerInfo& t) { return t.receiver == object; }), list.end());
    return list.size() != oldSize;
}

int EventDispatcherLinux::remainingTime(int timerId)
{
    if (!timerSource_) return -1;
    int64_t now = getCurrentTime();
    for (const auto& t : timerSource_->timerList) {
        if (t.id == timerId) {
            int64_t remain = t.nextTimeout - now;
            return remain < 0 ? 0 : static_cast<int>(std::min<int64_t>(remain, INT_MAX));
        }
    }
    return -1;
}

void EventDispatcherLinux::registerSocketNotifier(SocketNotifier* notifier)
{
    if (!socketNotifierSource_ || !notifier) return;
    auto* p = new GPollFDWithSocketNotifier();
    p->socketNotifier = notifier;
    p->pollfd.fd = notifier->socket;
    p->pollfd.revents = 0;
    switch (notifier->type) {
    case SocketNotifierType::Read:     p->pollfd.events = G_IO_IN;  break;
    case SocketNotifierType::Write:    p->pollfd.events = G_IO_OUT; break;
    case SocketNotifierType::Exception:p->pollfd.events = G_IO_PRI; break;
    }
    g_source_add_poll(reinterpret_cast<GSource*>(socketNotifierSource_), &p->pollfd);
    socketNotifierSource_->pollfds.push_back(p);
}

void EventDispatcherLinux::unregisterSocketNotifier(SocketNotifier* notifier)
{
    if (!socketNotifierSource_ || !notifier) return;
    auto& vec = socketNotifierSource_->pollfds;
    for (auto it = vec.begin(); it != vec.end(); ++it) {
        if ((*it)->socketNotifier == notifier) {
            g_source_remove_poll(reinterpret_cast<GSource*>(socketNotifierSource_), &(*it)->pollfd);
            delete *it;
            vec.erase(it);
            return;
        }
    }
}

void EventDispatcherLinux::startingUp()
{
    if (!mainContext_) return;
    // socket notifier source
    {
        GSource* source = g_source_new(&socketNotifierSourceFuncs, sizeof(GSocketNotifierSource));
        socketNotifierSource_ = reinterpret_cast<GSocketNotifierSource*>(source);
        socketNotifierSource_->pollfds.clear();
        socketNotifierSource_->activeNotifierPos = 0;
        g_source_set_can_recurse(reinterpret_cast<GSource*>(socketNotifierSource_), true);
        g_source_attach(reinterpret_cast<GSource*>(socketNotifierSource_), mainContext_);
    }
    // timer source
    {
        GSource* source = g_source_new(&timerSourceFuncs, sizeof(GTimerSource));
        timerSource_ = reinterpret_cast<GTimerSource*>(source);
        timerSource_->timerList.clear();
        timerSource_->runWithIdlePriority = false;
        g_source_set_can_recurse(reinterpret_cast<GSource*>(timerSource_), true);
        g_source_attach(reinterpret_cast<GSource*>(timerSource_), mainContext_);
    }
    // idle timer source
    {
        GSource* source = g_source_new(&idleTimerSourceFuncs, sizeof(GIdleTimerSource));
        idleTimerSource_ = reinterpret_cast<GIdleTimerSource*>(source);
        idleTimerSource_->timerSource = timerSource_;
        g_source_set_can_recurse(reinterpret_cast<GSource*>(idleTimerSource_), true);
        g_source_set_priority(reinterpret_cast<GSource*>(idleTimerSource_), G_PRIORITY_DEFAULT_IDLE);
        g_source_attach(reinterpret_cast<GSource*>(idleTimerSource_), mainContext_);
    }
}

void EventDispatcherLinux::shuttingDown()
{
    if (socketNotifierSource_) {
        for (auto* p : socketNotifierSource_->pollfds) {
            g_source_remove_poll(reinterpret_cast<GSource*>(socketNotifierSource_), &p->pollfd);
            delete p;
        }
        socketNotifierSource_->pollfds.clear();
        g_source_destroy(reinterpret_cast<GSource*>(socketNotifierSource_));
        g_source_unref(reinterpret_cast<GSource*>(socketNotifierSource_));
        socketNotifierSource_ = nullptr;
    }
    if (timerSource_) {
        g_source_destroy(reinterpret_cast<GSource*>(timerSource_));
        g_source_unref(reinterpret_cast<GSource*>(timerSource_));
        timerSource_ = nullptr;
    }
    if (idleTimerSource_) {
        g_source_destroy(reinterpret_cast<GSource*>(idleTimerSource_));
        g_source_unref(reinterpret_cast<GSource*>(idleTimerSource_));
        idleTimerSource_ = nullptr;
    }
}

} // namespace SAK

