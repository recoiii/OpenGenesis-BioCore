#include "biocore/infrastructure/worker_event_mapper.hpp"

#include <stdexcept>

namespace biocore::infrastructure {
namespace {

[[nodiscard]] application::WorkerLifecycleEventType map_type(
    const worker_protocol::MessageType type
) {
    switch (type) {
        case worker_protocol::MessageType::ready:
            return application::WorkerLifecycleEventType::ready;
        case worker_protocol::MessageType::heartbeat:
            return application::WorkerLifecycleEventType::heartbeat;
        case worker_protocol::MessageType::progress:
            return application::WorkerLifecycleEventType::progress;
        case worker_protocol::MessageType::log:
            return application::WorkerLifecycleEventType::log;
        case worker_protocol::MessageType::artifact:
            return application::WorkerLifecycleEventType::artifact;
        case worker_protocol::MessageType::completed:
            return application::WorkerLifecycleEventType::completed;
        case worker_protocol::MessageType::failed:
            return application::WorkerLifecycleEventType::failed;
        case worker_protocol::MessageType::cancel:
        case worker_protocol::MessageType::shutdown:
        case worker_protocol::MessageType::ping:
        case worker_protocol::MessageType::pong:
            throw std::invalid_argument("Control message cannot be mapped to a lifecycle event");
    }
    throw std::invalid_argument("Unknown worker message type");
}

[[nodiscard]] application::WorkerLifecycleLogLevel map_log_level(
    const worker_protocol::WorkerLogLevel level
) noexcept {
    switch (level) {
        case worker_protocol::WorkerLogLevel::trace:
            return application::WorkerLifecycleLogLevel::trace;
        case worker_protocol::WorkerLogLevel::debug:
            return application::WorkerLifecycleLogLevel::debug;
        case worker_protocol::WorkerLogLevel::info:
            return application::WorkerLifecycleLogLevel::info;
        case worker_protocol::WorkerLogLevel::warning:
            return application::WorkerLifecycleLogLevel::warning;
        case worker_protocol::WorkerLogLevel::error:
            return application::WorkerLifecycleLogLevel::error;
        case worker_protocol::WorkerLogLevel::critical:
            return application::WorkerLifecycleLogLevel::critical;
    }
    return application::WorkerLifecycleLogLevel::error;
}

}  // namespace

application::WorkerLifecycleEvent to_application_event(
    const worker_protocol::WorkerEvent& event
) {
    worker_protocol::validate_worker_event(event);
    return application::WorkerLifecycleEvent{
        .type = map_type(event.type),
        .job_id = event.job_id,
        .launch_revision = event.job_revision,
        .sequence = event.sequence,
        .worker_timestamp_utc = event.timestamp_utc,
        .progress = event.progress,
        .active_step_id = event.active_step_id,
        .log_level = event.log_level.has_value()
                         ? std::optional{map_log_level(*event.log_level)}
                         : std::nullopt,
        .component = event.component,
        .message = event.message,
        .artifact_step_id = event.artifact_step_id,
        .artifact_output_port = event.artifact_output_port,
        .artifact_plugin_id = event.artifact_plugin_id,
        .artifact_plugin_version = event.artifact_plugin_version,
        .artifact_module_id = event.artifact_module_id,
        .artifact_file_type = event.artifact_file_type,
        .artifact_relative_project_path = event.artifact_relative_project_path,
        .exit_code = event.exit_code,
    };
}

}  // namespace biocore::infrastructure
