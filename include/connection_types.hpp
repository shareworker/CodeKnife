#pragma once

namespace SAK {

enum class ConnectionType {
    kAutoConnection,
    kDirectConnection,
    kQueuedConnection,
    kBlockingConnection
};

} // namespace SAK
