#pragma once

#include "cobject.hpp"
#include <mutex>
#include <vector>

namespace SAK {

class EventDispatcher;

class CApplication : public CObject {
public:
    DECLARE_OBJECT(CApplication)
    
    explicit CApplication(CObject* parent = nullptr);
    ~CApplication();

    static CApplication* instance();

    int exec();
    void quit();
    void exit(int returnCode = 0);

    EventDispatcher* eventDispatcher() const {
        return dispatcher_;
    }
    void setEventDispatcher(EventDispatcher* dispatcher);
    static bool sendEvent(CObject* receiver, Event* event);
    static void postEvent(CObject* receiver, Event* event);
    static void removePostedEvents(CObject* receiver, Event::Type eventType = Event::Type::None);

private:
    void processPostedEvents();
    
    struct PostedEvent {
        CObject* receiver;
        Event* event;
        int priority;
    };
    static CApplication* instance_;
    EventDispatcher* dispatcher_;
    std::mutex eventQueueMutex_;
    std::vector<PostedEvent> eventQueue_;
    bool quitFlag_;
    int returnCode_;
};

}