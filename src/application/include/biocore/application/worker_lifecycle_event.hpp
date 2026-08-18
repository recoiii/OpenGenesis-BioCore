#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace biocore::application {

enum class WorkerLifecycleEventType {
    ready,
    heartbeat,
    progress,
    log,
    artifact,
    completed,
    failed
};

enum class WorkerLifecycleLogLevel {
    trace,
    debug,
    info,
    warning,
    error,
    critical
};

struct WorkerLifecycleEvent final {
    WorkerLifecycleEventType type{WorkerLifecycleEventType::ready};
    std::string job_id;
    std::int64_t launch_revision{0};
    std::uint64_t sequence{0U};
    std::string worker_timestamp_utc;
    std::optional<double> progress;
    std::optional<std::string> active_step_id;
    std::optional<WorkerLifecycleLogLevel> log_level;
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

}  // namespace biocore::application
