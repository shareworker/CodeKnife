# CodeKnife Library

## Overview
CodeKnife is a comprehensive C++ utility library that provides essential components for high-performance applications including:

- **Logging System**: Thread-safe, configurable logging with file rotation and async support
- **Memory Management**: High-performance memory and object pools
- **Threading Utilities**: Thread pools and concurrent data structures
- **IPC Components**: Cross-platform inter-process communication
- **File Operations**: Enhanced file handling with CRC32C checksums
- **Utility Classes**: Byte buffers, timers, and common data structures

## Build Requirements
- C++17 compatible compiler
- xmake build system
- Platform support: Windows (MinGW/MSVC) and Linux

## Installation
Clone the repository and build using xmake:

```bash
git clone <repository-url>
cd CodeKnife
xmake config
xmake build
```

This will generate:
- `codeknife.dll` (Windows) or `libcodeknife.so` (Linux) - The main dynamic library
- `test_util` - Unit test executable

## Usage
Link against the CodeKnife dynamic library in your project:

```cpp
#include "logger.hpp"
#include "memory_pool.hpp"
#include "thread_pool.hpp"

int main() {
    // Initialize logger
    SAK::log::Logger::instance().init({
        .enable_console = true,
        .enable_file = true,
        .log_dir = "./logs",
        .level = SAK::log::Level::LOG_INFO
    });
    
    // Use memory pool
    SAK::util::MemoryPool<1024> pool;
    auto ptr = pool.allocate();
    
    // Use thread pool
    SAK::util::ThreadPool thread_pool(4);
    
    LOG_INFO("CodeKnife library initialized successfully");
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
