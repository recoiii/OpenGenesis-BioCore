#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "biocore/application/job_scheduler.hpp"
#include "biocore/application/worker_process.hpp"

namespace biocore::application {

class IMonotonicClock;
class IWorkerLifecycleEventSink;
class IWorkerSupervisor;
class JobService;
class OutputArtifactCleanupService;
class OutputArtifactService;
class WorkerEventIngestionSession;

enum class WorkerRuntimeIssueStage {
    scheduler,
    output_poll,
    event_ingestion,
    process_reap,
    process_finalization,
    heartbeat_timeout,
    cancellation,
    termination,
    persistence,
    artifact_cleanup,
    event_broadcast,
    observation_retention
};

struct WorkerRuntimeIssue final {
    WorkerRuntimeIssueStage stage{WorkerRuntimeIssueStage::scheduler};
    std::string job_id;
    std::string message;
};

struct WorkerRuntimePolicy final {
    std::chrono::milliseconds poll_interval{100};
    std::chrono::milliseconds heartbeat_timeout{30'000};
    std::size_t maximum_background_issues{1024U};
    std::size_t maximum_background_diagnostics{1024U};
    std::size_t maximum_background_protocol_issues{1024U};
};

struct WorkerRuntimeCycleResult final {
    JobSchedulerTickResult scheduler;
    std::vector<std::string> ingested_job_ids;
    std::vector<std::string> exited_job_ids;
    std::vector<std::string> timed_out_job_ids;
    std::vector<std::string> cancelled_job_ids;
    std::vector<std::string> diagnostics;
    std::vector<WorkerProtocolIssue> protocol_issues;
    std::vector<WorkerRuntimeIssue> issues;
};

class WorkerRuntime final {
public:
    WorkerRuntime(
        JobScheduler& scheduler,
        JobService& job_service,
        IWorkerSupervisor& worker_supervisor,
        IMonotonicClock& monotonic_clock,
        WorkerRuntimePolicy policy,
        OutputArtifactService* output_artifact_service = nullptr,
        OutputArtifactCleanupService* output_artifact_cleanup_service = nullptr,
        IWorkerLifecycleEventSink* lifecycle_event_sink = nullptr
    );
    ~WorkerRuntime();

    WorkerRuntime(const WorkerRuntime&) = delete;
    WorkerRuntime& operator=(const WorkerRuntime&) = delete;

    [[nodiscard]] WorkerRuntimeCycleResult run_cycle();
    void start();
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] WorkerRuntimePolicy policy() const noexcept;
    [[nodiscard]] std::size_t active_session_count() const;
    [[nodiscard]] std::vector<WorkerRuntimeIssue> drain_background_issues();
    [[nodiscard]] std::vector<std::string> drain_background_diagnostics();
    [[nodiscard]] std::vector<WorkerProtocolIssue> drain_background_protocol_issues();

private:
    struct SessionState;

    void register_launches(
        const JobSchedulerTickResult& scheduler_result,
        std::chrono::steady_clock::time_point now,
        WorkerRuntimeCycleResult& result
    );
    void ingest_output_batch(
        WorkerProcessOutput output,
        std::chrono::steady_clock::time_point now,
        WorkerRuntimeCycleResult& result
    );
    void process_exit_batch(
        WorkerProcessExit process_exit,
        std::chrono::steady_clock::time_point now,
        WorkerRuntimeCycleResult& result
    );
    void process_cancellation_requests(WorkerRuntimeCycleResult& result);
    void enforce_heartbeat_timeouts(
        std::chrono::steady_clock::time_point now,
        WorkerRuntimeCycleResult& result
    );
    void append_background_issues(std::vector<WorkerRuntimeIssue> issues);
    void append_background_observations(
        std::vector<std::string> diagnostics,
        std::vector<WorkerProtocolIssue> protocol_issues
    );
    void background_main(std::stop_token stop_token) noexcept;

    JobScheduler& scheduler_;
    JobService& job_service_;
    IWorkerSupervisor& worker_supervisor_;
    IMonotonicClock& monotonic_clock_;
    WorkerRuntimePolicy policy_;
    OutputArtifactService* output_artifact_service_{nullptr};
    OutputArtifactCleanupService* output_artifact_cleanup_service_{nullptr};
    IWorkerLifecycleEventSink* lifecycle_event_sink_{nullptr};

    mutable std::mutex sessions_mutex_;
    std::map<std::string, std::unique_ptr<SessionState>, std::less<>> sessions_;
    std::atomic_flag cycle_in_progress_ = ATOMIC_FLAG_INIT;

    mutable std::mutex thread_mutex_;
    std::jthread background_thread_;
    std::atomic<bool> running_{false};
    std::mutex wait_mutex_;
    std::condition_variable wait_condition_;

    std::mutex background_issue_mutex_;
    std::vector<WorkerRuntimeIssue> background_issues_;
    std::vector<std::string> background_diagnostics_;
    std::vector<WorkerProtocolIssue> background_protocol_issues_;
    std::size_t dropped_background_issues_{0U};
    std::size_t dropped_background_diagnostics_{0U};
    std::size_t dropped_background_protocol_issues_{0U};
};

}  // namespace biocore::application
