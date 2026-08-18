#include "biocore/worker_protocol/protocol.hpp"

namespace biocore::worker_protocol {

std::string_view to_string(const MessageType type) noexcept {
    switch (type) {
        case MessageType::ready:
            return "ready";
        case MessageType::heartbeat:
            return "heartbeat";
        case MessageType::progress:
            return "progress";
        case MessageType::log:
            return "log";
        case MessageType::artifact:
            return "artifact";
        case MessageType::completed:
            return "completed";
        case MessageType::failed:
            return "failed";
        case MessageType::cancel:
            return "cancel";
        case MessageType::shutdown:
            return "shutdown";
        case MessageType::ping:
            return "ping";
        case MessageType::pong:
            return "pong";
    }

    return "unknown";
}

}  // namespace biocore::worker_protocol
