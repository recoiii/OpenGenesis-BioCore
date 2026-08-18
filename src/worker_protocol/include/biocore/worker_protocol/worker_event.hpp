#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "biocore/worker_protocol/protocol.hpp"

namespace biocore::worker_protocol {

inline constexpr std::size_t maximum_event_line_bytes = 64U * 1024U;

enum class WorkerLogLevel {
    trace,
    debug,
    info,
    warning,
    error,
    critical
};

struct WorkerEvent final {
    std::uint32_t protocol_version{current_protocol_version};
    MessageType type{MessageType::ready};
    std::string job_id;
    std::int64_t job_revision{0};
    std::uint64_t sequence{0U};
    std::string timestamp_utc;
    std::optional<double> progress;
    std::optional<std::string> active_step_id;
    std::optional<WorkerLogLevel> log_level;
    std::optional<std::string> component;
    std::optional<std::string> message;
    std::optional<std::string> artifact_step_id;
    std::optional<std::string> artifact_output_port;
    std::optional<std::string> artifact_plugin_id;
    std::optional<std::string> artifact_plugin_version;
    std::optional<std::string> artifact_module_id;
    std::optional<std::string> artifact_file_type;
    std::optional<std::string> artifact_relative_project_path;
    std::optional<std::int64_t> exit_code;
};

[[nodiscard]] std::string_view to_string(WorkerLogLevel level) noexcept;
[[nodiscard]] std::optional<WorkerLogLevel> worker_log_level_from_string(
    std::string_view value
) noexcept;
[[nodiscard]] std::optional<MessageType> message_type_from_string(
    std::string_view value
) noexcept;

// Throws std::invalid_argument when the event violates protocol-v1 invariants.
void validate_worker_event(const WorkerEvent& event);

}  // namespace biocore::worker_protocol
