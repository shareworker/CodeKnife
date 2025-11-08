# CodeKnife Library

## Overview
CodeKnife is a comprehensive C++ utility library that provides essential components for high-performance applications including:

- **Logging System**: Thread-safe, configurable logging with file rotation and async support
- **Memory Management**: High-performance memory and object pools
- **Threading Utilities**: Thread pools and concurrent data structures
- **IPC Components**: Cross-platform inter-process communication
- **File Operations**: Enhanced file handling with CRC32C checksums
- **Utility Classes**: Byte buffers, timers, and common data structures

## Core Architecture
- **CObject meta system**: `CObject` base with reflection-like `MetaObject` for properties, methods, and signals.
- **Signal/Slot**: Type-erased invocation with `ConnectionManager`, supports direct/queued/blocking connections.
- **Event Loop**: `CApplication` provides event posting and integrates platform `EventDispatcher` (Win/Linux).
- **Utilities**: Logger, memory/object pools, thread pool, byte buffer, timers.

## Build Requirements
- C++17 compatible compiler
- xmake build system
- Platform support: Windows (MinGW/MSVC) and Linux

## Installation
Clone the repository and build using xmake:

```bash
git clone <repository-url>
cd CodeKnife
# Configure & build (Debug)
xmake f -m debug
xmake
```

This will generate:
- `codeknife` (shared library)
- `codeknife_static` (static library)
- `test_util` (test executable)

To build Release:

```bash
xmake f -m release
xmake
```

## Usage
Headers are available via short-form includes (public include dirs are exported):

```cpp
#include "logger.hpp"
#include "memory_pool.hpp"
#include "thread_pool.hpp"

int main() {
    // Initialize logger
    SAK::log::LogConfig config;
    config.use_stdout = true;
    config.log_dir = "./logs";
    config.min_level = SAK::log::Level::LOG_INFO;
    SAK::log::Logger::instance().configure(config);
    
    // Use memory pool
    auto& pool = SAK::memory::MemoryPool::GetInstance();
    auto ptr = pool.Allocate(1024);
    
    // Use thread pool
    SAK::thread::ThreadPool thread_pool(4);
    
    LOG_INFO("CodeKnife library initialized successfully");
    return 0;
}
```

### Signal-Slot Quickstart
```cpp
#include "cobject.hpp"
#include "connection_manager.hpp"

class Sender : public SAK::CObject {
    DECLARE_OBJECT(Sender)
public:
    PROPERTY(Sender, int, count)
    SIGNAL(Sender, countChanged, "void(int)")
    void inc() { setcount(count() + 1); EMIT_SIGNAL(countChanged, count()); }
};

class Receiver : public SAK::CObject {
    DECLARE_OBJECT(Receiver)
public:
    SLOT(Receiver, void, onCountChanged, "void(int)", int v)
};

void Receiver::onCountChanged(int v) { /* handle */ }

// Connect and emit
Sender s; Receiver r;
SAK::CObject::connect(&s, "countChanged", &r, "onCountChanged", SAK::ConnectionType::kDirectConnection);
s.inc();
```

### Event Loop & Timers
```cpp
#include "capplication.hpp"

int main() {
  SAK::CApplication app; // starts event dispatcher internally per platform
  // post events or run cross-thread queued connections
  app.exec();
  return 0;
}
```

### Available Components

- **Logger**: `#include "logger.hpp"` - Thread-safe logging with macros `LOG_DEBUG`, `LOG_INFO`, `LOG_WARNING`, `LOG_ERROR`
- **Memory Pool**: `#include "memory_pool.hpp"` - High-performance memory allocation
- **Thread Pool**: `#include "thread_pool.hpp"` - Concurrent task execution
- **IPC**: `#include "ipc_implement.hpp"` - Cross-platform inter-process communication
- **File Object**: `#include "file_object.hpp"` - Enhanced file operations with checksums
- **Utilities**: `#include "byte_buffer.hpp"`, `#include "timer.hpp"` - Common data structures

## Testing
Run the unit tests:

```bash
xmake run test_util
```

## Build Targets
- `codeknife`: shared library with platform-specific event dispatcher selected automatically.
- `codeknife_static`: static library (tests link this to avoid DLL issues).
- `test_util`: end-to-end test binary covering core modules.

## Platform Notes
- Windows: uses `event_dispatcher_win.cpp` (Win32 APIs), links `ws2_32`, `advapi32`, `kernel32`, `user32`.
- Linux: uses `event_dispatcher_linux.cpp` (POSIX), links `pthread`, `rt`, `stdc++fs`.
- Build system auto-excludes the non-target dispatcher.

## Cross-Platform Support
The library is designed for cross-platform compatibility:
- **Windows**: MinGW and MSVC support with Win32 APIs
- **Linux**: GCC/Clang with POSIX APIs
- **Threading**: Platform-specific thread implementations
- **IPC**: Named pipes (Windows) and POSIX semaphores/shared memory (Linux)

## Contributing
Contributions are welcome! Please ensure:
- Code follows C++17 standards
- Cross-platform compatibility is maintained
- Unit tests are provided for new features
- Documentation is updated accordingly

## License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
