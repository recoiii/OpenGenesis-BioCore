#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "biocore/application/worker_lifecycle_event.hpp"

namespace biocore::application {

struct WorkerProcessInfo final {
    std::string job_id;
    std::uint64_t process_id{0U};
};

struct WorkerProtocolIssue final {
    std::string job_id;
    std::string message;
};

struct WorkerProcessOutput final {
    std::string job_id;
    std::uint64_t process_id{0U};
    std::vector<WorkerLifecycleEvent> events;
    std::vector<std::string> diagnostics;
    std::vector<WorkerProtocolIssue> protocol_issues;
};

struct WorkerProcessExit final {
    std::string job_id;
    std::uint64_t process_id{0U};
    std::int64_t exit_code{0};
    std::vector<WorkerLifecycleEvent> events;
    std::vector<std::string> diagnostics;
    std::vector<WorkerProtocolIssue> protocol_issues;
};

enum class WorkerTerminationResult {
    requested,
    already_exited,
    not_found
};

}  // namespace biocore::application
