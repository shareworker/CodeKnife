#include "ipc_base.hpp"
#include "ipc_packet.hpp"
#include <functional>

namespace util {
namespace ipc {

// Forward declaration of IPCSink interface
// Full definition is in ipc_base.hpp
class IPCSink;

class IPCReader : public IPCHandlerBase {
public:
    IPCReader(const std::string& ipc_name, IPC_TYPE type, IPC_HANDLE_TYPE handle_type);
    ~IPCReader();

    bool ProcessData() override;

    bool Init() override;
    bool Uninit() override;

    bool ReadData();
    void SetSink(IPCSink* sink) { sink_ = sink; }
    
private:
    char* buffer_;
    unsigned int read_cursor_;
    unsigned int buffer_size_;
    IPCSink* sink_;
};

} // namespace ipc
} // namespace util