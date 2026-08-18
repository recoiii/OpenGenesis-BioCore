#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/i_worker_supervisor.hpp"
#include "biocore/application/worker_lifecycle_event.hpp"

namespace biocore::infrastructure {

using WorkerProcessInfo = application::WorkerProcessInfo;
using WorkerProtocolIssue = application::WorkerProtocolIssue;
using WorkerProcessOutput = application::WorkerProcessOutput;
using WorkerProcessExit = application::WorkerProcessExit;
using WorkerTerminationResult = application::WorkerTerminationResult;

enum class WorkerSupervisorErrorCode {
    invalid_configuration,
    invalid_launch_request,
    duplicate_active_job,
    spawn_failed,
    process_query_failed,
    termination_failed
};

class WorkerSupervisorError final : public std::runtime_error {
public:
    WorkerSupervisorError(
        WorkerSupervisorErrorCode code,
        std::string job_id,
        std::string message
    );

    [[nodiscard]] WorkerSupervisorErrorCode code() const noexcept;
    [[nodiscard]] std::string_view job_id() const noexcept;

private:
    WorkerSupervisorErrorCode code_;
    std::string job_id_;
};

class PlatformWorkerSupervisor final : public application::IWorkerSupervisor {
public:
    PlatformWorkerSupervisor(
        std::filesystem::path worker_executable,
        std::filesystem::path project_root
    );
    ~PlatformWorkerSupervisor() override;

    PlatformWorkerSupervisor(const PlatformWorkerSupervisor&) = delete;
    PlatformWorkerSupervisor& operator=(const PlatformWorkerSupervisor&) = delete;
    PlatformWorkerSupervisor(PlatformWorkerSupervisor&&) noexcept;
    PlatformWorkerSupervisor& operator=(PlatformWorkerSupervisor&&) noexcept;

    void launch(const application::WorkerLaunchRequest& request) override;

    [[nodiscard]] std::optional<WorkerProcessInfo> find_process(
        std::string_view job_id
    ) const;
    [[nodiscard]] std::vector<WorkerProcessInfo> tracked_processes() const;

    // Non-blockingly drains stdout NDJSON lifecycle events and stderr diagnostics.
    [[nodiscard]] std::vector<WorkerProcessOutput> poll_output() override;

    // Performs a non-blocking OS query, returns completed children, and releases their
    // native tracking resources. Callers should invoke this regularly to avoid zombies.
    [[nodiscard]] std::vector<WorkerProcessExit> reap_exited() override;
    [[nodiscard]] WorkerTerminationResult terminate(std::string_view job_id) override;

    [[nodiscard]] const std::filesystem::path& worker_executable() const noexcept;
    [[nodiscard]] const std::filesystem::path& project_root() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace biocore::infrastructure
