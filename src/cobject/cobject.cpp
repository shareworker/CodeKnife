
#include "cobject.hpp"
#include "connection_manager.hpp"
#include "event.hpp"
#include "capplication.hpp"
#include "event_dispatcher.hpp"
#include <algorithm>
#include <iostream>
#include <future>
#include <atomic>

namespace SAK {

const MetaObject CObject::staticMetaObject(
    "CObject",
    nullptr,
    nullptr,
    {},
    {},
    {}
);

CObject::CObject(CObject* parent) : parent_(nullptr) {
    setParent(parent);
}

CObject::~CObject() {
    // Disconnect all signal-slot connections involving this object
    ConnectionManager::instance().disconnectAll(this);
    
    // Remove from parent
    setParent(nullptr);
    
    // Delete all children safely
    while (!children_.empty()) {
        CObject* child = children_.back();
        children_.pop_back(); // Remove from list first
        delete child; // Then delete - child's destructor will call setParent(nullptr)
    }
}

void CObject::setParent(CObject* parent) {
    if (parent_ == parent) return;
    if (parent_) parent_->removeChild(this);
    parent_ = parent;
    if (parent_) parent_->addChild(this);
}

void CObject::addChild(CObject* child) {
    if (child && std::find(children_.begin(), children_.end(), child) == children_.end()) {
        children_.push_back(child);
    }
}

void CObject::removeChild(CObject* child) {
    if (child) {
        children_.erase(std::remove(children_.begin(), children_.end(), child), children_.end());
    }
}

bool CObject::setProperty(const std::string& name, const std::any& value) {
    const MetaProperty* prop = metaObject()->findProperty(name);
    if (prop) {
        prop->set(this, value);
        return true;
    }
    return setDynamicProperty(name, value);
}

std::any CObject::property(const std::string& name) const {
    const MetaProperty* prop = metaObject()->findProperty(name);
    if (prop) {
        return prop->get(this);
    }
    return dynamicProperty(name);
}

bool CObject::setDynamicProperty(const std::string& name, const std::any& value) {
    dynamic_properties_[name] = value;
    return true;
}

std::any CObject::dynamicProperty(const std::string& name) const {
    auto it = dynamic_properties_.find(name);
    if (it != dynamic_properties_.end()) {
        return it->second;
    }
    return std::any();
}

std::vector<std::string> CObject::dynamicPropertyNames() const {
    std::vector<std::string> names;
    names.reserve(dynamic_properties_.size()); // Pre-allocate memory
    for (const auto& [key, value] : dynamic_properties_) {
        names.emplace_back(key); // Avoid copy construction
    }
    return names;
}

bool CObject::connect(const CObject* sender, const char* signal, const CObject* receiver, const char* slot, ConnectionType type) {
    return ConnectionManager::instance().connect(sender, signal, receiver, slot, type);
}

bool CObject::disconnect(const CObject* sender, const char* signal, const CObject* receiver, const char* slot) {
    return ConnectionManager::instance().disconnect(sender, signal, receiver, slot);
}

void CObject::emitSignal(const char* signal, const std::vector<std::any>& args) {
    ConnectionManager::instance().emitSignal(this, signal, args);
}

void CObject::metacall(const char* slot, const std::vector<std::any>& args, 
                       ConnectionType type, const CObject* sender) {
    if (!slot || !sender) return;
    
    // Check if sender and receiver are in the same thread
    bool sameThread = (sender->thread() == this->thread_id_);
    ConnectionType effectiveType = type;
    
    // AutoConnection: choose based on thread affinity
    if (effectiveType == ConnectionType::kAutoConnection) {
        effectiveType = sameThread ? ConnectionType::kDirectConnection : ConnectionType::kQueuedConnection;
    }
    
    try {
        const MetaMethod* method = metaObject()->findMethod(slot);
        if (!method) {
            return;
        }
        
        switch (effectiveType) {
        case ConnectionType::kDirectConnection:
            // Direct call in sender's thread
            method->invoke(this, args);
            break;
            
        case ConnectionType::kQueuedConnection: {
            // Async call via event queue
            auto* event = new MetaCallEvent(slot, args);
            CApplication::postEvent(this, event);
            break;
        }
        
        case ConnectionType::kBlockingConnection: {
            if (sameThread) {
                // Blocking in same thread would cause deadlock - use direct call
                method->invoke(this, args);
            } else {
                // Blocking call: post event and wait for completion
                auto promise = std::make_shared<std::promise<void>>();
                auto future = promise->get_future();
                
                auto* event = new MetaCallEvent(slot, args);
                event->setPromise(promise);
                CApplication::postEvent(this, event);
                
                // Wait for slot to complete
                future.wait();
            }
            break;
        }
        
        default:
            break;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error invoking slot " << slot << ": " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown error invoking slot " << slot << std::endl;
    }
}

void CObject::deleteLater() {
    auto* event = new Event(Event::Type::DeferredDelete);
    CApplication::postEvent(this, event);
}

int CObject::startTimer(int64_t interval) {
    if (interval < 0) {
        std::cerr << "CObject::startTimer: Timers cannot have negative intervals" << std::endl;
        return 0;
    }
    
    // Get the event dispatcher from the application
    CApplication* app = CApplication::instance();
    if (!app) {
        std::cerr << "CObject::startTimer: No application instance" << std::endl;
        return 0;
    }
    
    EventDispatcher* dispatcher = app->eventDispatcher();
    if (!dispatcher) {
        std::cerr << "CObject::startTimer: No event dispatcher" << std::endl;
        return 0;
    }
    
    // Generate a unique timer ID
    static std::atomic<int> nextTimerId{1};
    int timerId = nextTimerId.fetch_add(1, std::memory_order_relaxed);
    
    // Register the timer with the dispatcher
    dispatcher->registerTimer(timerId, interval, this);
    
    return timerId;
}

void CObject::killTimer(int timerId) {
    if (timerId <= 0) {
        return;
    }
    
    // Get the event dispatcher from the application
    CApplication* app = CApplication::instance();
    if (!app) {
        return;
    }
    
    EventDispatcher* dispatcher = app->eventDispatcher();
    if (!dispatcher) {
        return;
    }
    
    // Unregister the specific timer
    dispatcher->unregisterTimer(timerId);
}

bool CObject::unregisterTimers() {
    // Get the event dispatcher from the application
    CApplication* app = CApplication::instance();
    if (!app) {
        return false;
    }
    
    EventDispatcher* dispatcher = app->eventDispatcher();
    if (!dispatcher) {
        return false;
    }
    
    // Unregister all timers for this object
    return dispatcher->unregisterTimers(this);
}

bool CObject::event(Event* event) {
    if (!event) return false;
    
    switch (event->type()) {
    case Event::Type::MetaCall: {
        // Handle cross-thread slot invocation
        MetaCallEvent* mce = static_cast<MetaCallEvent*>(event);
        const MetaMethod* method = metaObject()->findMethod(mce->slot());
        if (method) {
            try {
                method->invoke(this, mce->args());
                
                // If this is a blocking call, notify the sender thread
                if (mce->promise()) {
                    mce->promise()->set_value();
                }
            } catch (const std::exception& e) {
                std::cerr << "Error in MetaCallEvent: " << e.what() << std::endl;
                if (mce->promise()) {
                    try {
                        mce->promise()->set_exception(std::current_exception());
                    } catch (...) {
                        // Promise already satisfied
                    }
                }
            }
        }
        return true;
    }
    
    case Event::Type::Timer:
        timerEvent(static_cast<TimerEvent*>(event));
        return true;
        
    case Event::Type::ChildAdded:
    case Event::Type::ChildRemoved:
        childEvent(static_cast<ChildEvent*>(event));
        return true;
        
    case Event::Type::DeferredDelete:
        delete this;
        return true;
        
    default:
        break;
    }
    
    return false;
}

}

