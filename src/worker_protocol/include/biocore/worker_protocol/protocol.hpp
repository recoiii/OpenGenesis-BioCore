#pragma once

#include <cstdint>
#include <string_view>

namespace biocore::worker_protocol {

inline constexpr std::uint32_t current_protocol_version = 2U;

enum class MessageType {
    ready,
    heartbeat,
    progress,
    log,
    artifact,
    completed,
    failed,
    cancel,
    shutdown,
    ping,
    pong
};

[[nodiscard]] std::string_view to_string(MessageType type) noexcept;

}  // namespace biocore::worker_protocol
