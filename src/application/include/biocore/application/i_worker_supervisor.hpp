#pragma once

#include <string_view>
#include <vector>

#include "biocore/application/worker_launch_request.hpp"
#include "biocore/application/worker_process.hpp"

namespace biocore::application {

class IWorkerSupervisor {
public:
    virtual ~IWorkerSupervisor() = default;

    // A successful return means the launch request was accepted by the execution adapter.
    // Concrete adapters report launch failures by throwing an exception.
    virtual void launch(const WorkerLaunchRequest& request) = 0;

    // Default no-op implementations keep lightweight launch-only test doubles small while
    // allowing the autonomous runtime to consume richer process lifecycle adapters.
    [[nodiscard]] virtual std::vector<WorkerProcessOutput> poll_output() { return {}; }
    [[nodiscard]] virtual std::vector<WorkerProcessExit> reap_exited() { return {}; }
    [[nodiscard]] virtual WorkerTerminationResult terminate(std::string_view) {
        return WorkerTerminationResult::not_found;
    }
};

}  // namespace biocore::application
