
#include "capplication.hpp"

#include "event_dispatcher.hpp"
#if defined(__linux__)
#include "event_dispatcher_linux.hpp"
#elif defined(_WIN32)
#include "event_dispatcher_win.hpp"
#endif
#include "meta_object.hpp"

namespace SAK {

CApplication* CApplication::instance_ = nullptr;

const MetaObject CApplication::staticMetaObject = {
    "CApplication",
    &CObject::staticMetaObject,
    &CApplication::createInstance,
    {},
    {},
    {}
};

CApplication::CApplication(CObject* parent) 
    : CObject(parent),
      dispatcher_(nullptr),
      quitFlag_(false),
      returnCode_(0) {
    if (instance_) {
        // Warning: multiple CApplication instances
    }
    instance_ = this;
    
#if defined(__linux__)
    dispatcher_ = new EventDispatcherLinux(this);
#elif defined(_WIN32)
    dispatcher_ = new EventDispatcherWin(this);
#endif
}

CApplication::~CApplication() {
    if (dispatcher_) {
        delete dispatcher_;
        dispatcher_ = nullptr;
    }
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

CApplication* CApplication::instance() {
    return instance_;
}

int CApplication::exec() {
    if (!dispatcher_) {
        return -1;
    }
    quitFlag_ = false;
    returnCode_ = 0;
    
    while(!quitFlag_) {
        dispatcher_->processEvents();
        processPostedEvents();
    }
    
    return returnCode_;
}

void CApplication::quit() {
    quitFlag_ = true;
}

void CApplication::exit(int returnCode) {
    quitFlag_ = true;
    returnCode_ = returnCode;
}

void CApplication::setEventDispatcher(EventDispatcher* dispatcher) {
    if (dispatcher_ && dispatcher_ != dispatcher) {
        delete dispatcher_;
    }
    dispatcher_ = dispatcher;
}

bool CApplication::sendEvent(CObject* receiver, Event *event) {
    return CObject::sendEvent(receiver, event);
}

void CApplication::postEvent(CObject* receiver, Event* event) {
    if (!instance_ || !receiver || !event) {
        delete event;
        return;
    }
    
    std::lock_guard<std::mutex> lock(instance_->eventQueueMutex_);
    instance_->eventQueue_.push_back({receiver, event, 0});
    
    if (instance_->dispatcher_) {
        instance_->dispatcher_->wakeUp();
    }
}

void CApplication::removePostedEvents(CObject* receiver, Event::Type eventType) {
    if (!instance_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(instance_->eventQueueMutex_);
    auto& queue = instance_->eventQueue_;
    
    queue.erase(
        std::remove_if(queue.begin(), queue.end(),
            [receiver, eventType](const PostedEvent& pe) {
                bool match = (receiver == nullptr || pe.receiver == receiver) &&
                             (eventType == Event::Type::None || pe.event->type() == eventType);
                if (match) {
                    delete pe.event;
                }
                return match;
            }),
        queue.end()
    );
}

void CApplication::processPostedEvents() {
    if (!instance_) {
        return;
    }
    
    std::vector<PostedEvent> events;
    {
        std::lock_guard<std::mutex> lock(instance_->eventQueueMutex_);
        events.swap(instance_->eventQueue_);
    }
    
    for (auto& pe : events) {
        if (pe.receiver) {
            CObject::sendEvent(pe.receiver, pe.event);
        }
        // Always delete the event after processing
        // Note: For DeferredDelete events, the receiver deletes itself but the event still needs cleanup
        delete pe.event;
    }
}

} // namespace SAK
