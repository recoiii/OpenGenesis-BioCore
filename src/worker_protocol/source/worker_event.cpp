#include "biocore/worker_protocol/worker_event.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace biocore::worker_protocol {
namespace {

constexpr std::size_t maximum_job_id_length = 128U;
constexpr std::size_t maximum_timestamp_length = 200U;
constexpr std::size_t maximum_step_id_length = 200U;
constexpr std::size_t maximum_component_length = 200U;
constexpr std::size_t maximum_artifact_text_length = 4096U;
constexpr std::size_t maximum_message_length = 16U * 1024U;

[[nodiscard]] bool is_blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
               return std::isspace(static_cast<unsigned char>(character)) != 0;
           });
}

void require_text(
    const std::string_view value,
    const std::string_view field,
    const std::size_t maximum_length
) {
    if (is_blank(value)) {
        throw std::invalid_argument(std::string{field} + " must not be blank");
    }
    if (value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(std::string{field} + " must not contain NUL characters");
    }
    if (value.size() > maximum_length) {
        throw std::invalid_argument(std::string{field} + " exceeds the maximum length");
    }
}

void require_absent(const WorkerEvent& event) {
    if (event.progress.has_value() || event.active_step_id.has_value() ||
        event.log_level.has_value() || event.component.has_value() ||
        event.message.has_value() || event.artifact_step_id.has_value() ||
        event.artifact_output_port.has_value() || event.artifact_plugin_id.has_value() ||
        event.artifact_plugin_version.has_value() || event.artifact_module_id.has_value() ||
        event.artifact_file_type.has_value() || event.artifact_relative_project_path.has_value() ||
        event.exit_code.has_value()) {
        throw std::invalid_argument("Worker event contains fields not allowed for its type");
    }
}

void require_no_log_or_exit_fields(const WorkerEvent& event) {
    if (event.log_level.has_value() || event.component.has_value() ||
        event.message.has_value() || event.artifact_step_id.has_value() ||
        event.artifact_output_port.has_value() || event.artifact_plugin_id.has_value() ||
        event.artifact_plugin_version.has_value() || event.artifact_module_id.has_value() ||
        event.artifact_file_type.has_value() || event.artifact_relative_project_path.has_value() ||
        event.exit_code.has_value()) {
        throw std::invalid_argument("Worker progress event contains unrelated fields");
    }
}

}  // namespace

std::string_view to_string(const WorkerLogLevel level) noexcept {
    switch (level) {
        case WorkerLogLevel::trace:
            return "trace";
        case WorkerLogLevel::debug:
            return "debug";
        case WorkerLogLevel::info:
            return "info";
        case WorkerLogLevel::warning:
            return "warning";
        case WorkerLogLevel::error:
            return "error";
        case WorkerLogLevel::critical:
            return "critical";
    }
    return "unknown";
}

std::optional<WorkerLogLevel> worker_log_level_from_string(
    const std::string_view value
) noexcept {
    if (value == "trace") return WorkerLogLevel::trace;
    if (value == "debug") return WorkerLogLevel::debug;
    if (value == "info") return WorkerLogLevel::info;
    if (value == "warning") return WorkerLogLevel::warning;
    if (value == "error") return WorkerLogLevel::error;
    if (value == "critical") return WorkerLogLevel::critical;
    return std::nullopt;
}

std::optional<MessageType> message_type_from_string(const std::string_view value) noexcept {
    if (value == "ready") return MessageType::ready;
    if (value == "heartbeat") return MessageType::heartbeat;
    if (value == "progress") return MessageType::progress;
    if (value == "log") return MessageType::log;
    if (value == "artifact") return MessageType::artifact;
    if (value == "completed") return MessageType::completed;
    if (value == "failed") return MessageType::failed;
    if (value == "cancel") return MessageType::cancel;
    if (value == "shutdown") return MessageType::shutdown;
    if (value == "ping") return MessageType::ping;
    if (value == "pong") return MessageType::pong;
    return std::nullopt;
}

void validate_worker_event(const WorkerEvent& event) {
    if (event.protocol_version != current_protocol_version) {
        throw std::invalid_argument("Worker event protocol version is unsupported");
    }
    require_text(event.job_id, "Worker event job id", maximum_job_id_length);
    if (event.job_revision < 0) {
        throw std::invalid_argument("Worker event job revision must not be negative");
    }
    if (event.sequence == 0U ||
        event.sequence > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        throw std::invalid_argument("Worker event sequence is outside the supported range");
    }
    require_text(event.timestamp_utc, "Worker event timestamp", maximum_timestamp_length);

    switch (event.type) {
        case MessageType::ready:
        case MessageType::heartbeat:
            require_absent(event);
            return;
        case MessageType::progress:
            if (!event.progress.has_value() || !std::isfinite(*event.progress) ||
                *event.progress < 0.0 || *event.progress > 1.0) {
                throw std::invalid_argument("Worker progress must be finite and between 0 and 1");
            }
            if (event.active_step_id.has_value()) {
                require_text(*event.active_step_id, "Worker active step id", maximum_step_id_length);
            }
            require_no_log_or_exit_fields(event);
            return;
        case MessageType::log:
            if (!event.log_level.has_value() || !event.component.has_value() ||
                !event.message.has_value()) {
                throw std::invalid_argument("Worker log event is missing required fields");
            }
            require_text(*event.component, "Worker log component", maximum_component_length);
            require_text(*event.message, "Worker log message", maximum_message_length);
            if (event.progress.has_value() || event.active_step_id.has_value() ||
                event.artifact_step_id.has_value() || event.artifact_output_port.has_value() ||
                event.artifact_plugin_id.has_value() || event.artifact_plugin_version.has_value() ||
                event.artifact_module_id.has_value() || event.artifact_file_type.has_value() ||
                event.artifact_relative_project_path.has_value() || event.exit_code.has_value()) {
                throw std::invalid_argument("Worker log event contains unrelated fields");
            }
            return;
        case MessageType::artifact:
            if (!event.artifact_step_id.has_value() || !event.artifact_output_port.has_value() ||
                !event.artifact_plugin_id.has_value() || !event.artifact_plugin_version.has_value() ||
                !event.artifact_module_id.has_value() || !event.artifact_file_type.has_value() ||
                !event.artifact_relative_project_path.has_value()) {
                throw std::invalid_argument("Worker artifact event is missing required fields");
            }
            require_text(*event.artifact_step_id, "Worker artifact step id", maximum_step_id_length);
            require_text(*event.artifact_output_port, "Worker artifact output port", maximum_step_id_length);
            require_text(*event.artifact_plugin_id, "Worker artifact plugin id", maximum_step_id_length);
            require_text(*event.artifact_plugin_version, "Worker artifact plugin version", maximum_step_id_length);
            require_text(*event.artifact_module_id, "Worker artifact module id", maximum_step_id_length);
            require_text(*event.artifact_file_type, "Worker artifact file type", 128U);
            require_text(*event.artifact_relative_project_path, "Worker artifact relative path", maximum_artifact_text_length);
            if (event.progress.has_value() || event.active_step_id.has_value() ||
                event.log_level.has_value() || event.component.has_value() ||
                event.message.has_value() || event.exit_code.has_value()) {
                throw std::invalid_argument("Worker artifact event contains unrelated fields");
            }
            return;
        case MessageType::completed:
            if (!event.exit_code.has_value() || *event.exit_code != 0) {
                throw std::invalid_argument("Worker completed event must have exit code zero");
            }
            if (event.progress.has_value() || event.active_step_id.has_value() ||
                event.log_level.has_value() || event.component.has_value() ||
                event.message.has_value() || event.artifact_step_id.has_value() ||
                event.artifact_output_port.has_value() || event.artifact_plugin_id.has_value() ||
                event.artifact_plugin_version.has_value() || event.artifact_module_id.has_value() ||
                event.artifact_file_type.has_value() || event.artifact_relative_project_path.has_value()) {
                throw std::invalid_argument("Worker completed event contains unrelated fields");
            }
            return;
        case MessageType::failed:
            if (!event.exit_code.has_value() || *event.exit_code == 0 ||
                !event.message.has_value()) {
                throw std::invalid_argument("Worker failed event requires a non-zero exit code and message");
            }
            require_text(*event.message, "Worker failure message", maximum_message_length);
            if (event.progress.has_value() || event.active_step_id.has_value() ||
                event.log_level.has_value() || event.component.has_value() ||
                event.artifact_step_id.has_value() || event.artifact_output_port.has_value() ||
                event.artifact_plugin_id.has_value() || event.artifact_plugin_version.has_value() ||
                event.artifact_module_id.has_value() || event.artifact_file_type.has_value() ||
                event.artifact_relative_project_path.has_value()) {
                throw std::invalid_argument("Worker failed event contains unrelated fields");
            }
            return;
        case MessageType::cancel:
        case MessageType::shutdown:
        case MessageType::ping:
        case MessageType::pong:
            throw std::invalid_argument("Worker stdout event type is not a lifecycle event");
    }

    throw std::invalid_argument("Worker event type is unknown");
}

}  // namespace biocore::worker_protocol
