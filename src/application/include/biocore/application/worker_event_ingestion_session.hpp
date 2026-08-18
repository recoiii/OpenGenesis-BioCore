#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "biocore/application/generated_output_artifact.hpp"
#include "biocore/application/i_partial_output_cleaner.hpp"
#include "biocore/application/output_artifact_service.hpp"
#include "biocore/application/worker_lifecycle_event.hpp"
#include "biocore/domain/job.hpp"

namespace biocore::application {

class JobService;
class OutputArtifactCleanupService;

enum class WorkerEventIngestionAction {
    job_started,
    heartbeat_observed,
    progress_persisted,
    log_observed,
    artifact_buffered,
    job_completed,
    job_failed
};

struct WorkerEventIngestionResult final {
    WorkerEventIngestionAction action{WorkerEventIngestionAction::heartbeat_observed};
    std::optional<domain::Job> persisted_job;
    std::vector<GeneratedOutputArtifact> registered_artifacts;
    std::optional<PartialOutputCleanupResult> cleanup_result;
    std::optional<std::string> cleanup_error;
};

struct WorkerProcessFinalizationResult final {
    bool matched_terminal_event{false};
    std::optional<domain::Job> persisted_job;
    std::optional<PartialOutputCleanupResult> cleanup_result;
    std::optional<std::string> cleanup_error;
};

class WorkerEventIngestionSession final {
public:
    WorkerEventIngestionSession(
        JobService& job_service,
        std::string expected_job_id,
        std::int64_t expected_launch_revision,
        OutputArtifactService* output_artifact_service = nullptr,
        OutputArtifactCleanupService* output_artifact_cleanup_service = nullptr
    );

    [[nodiscard]] WorkerEventIngestionResult ingest(const WorkerLifecycleEvent& event);
    [[nodiscard]] WorkerProcessFinalizationResult finalize_process_exit(std::int64_t exit_code);
    [[nodiscard]] std::uint64_t last_sequence() const;
    [[nodiscard]] bool ready_received() const;
    [[nodiscard]] bool terminal_received() const;
    [[nodiscard]] std::size_t pending_artifact_count() const;

private:
    void validate_identity_and_sequence(const WorkerLifecycleEvent& event) const;
    [[nodiscard]] domain::Job require_current_job() const;
    void buffer_artifact(const WorkerLifecycleEvent& event);
    void attach_cleanup_observation(WorkerEventIngestionResult& result) noexcept;
    void attach_cleanup_observation(WorkerProcessFinalizationResult& result) noexcept;

    JobService& job_service_;
    OutputArtifactService* output_artifact_service_{nullptr};
    OutputArtifactCleanupService* output_artifact_cleanup_service_{nullptr};
    std::string expected_job_id_;
    std::int64_t expected_launch_revision_;
    mutable std::mutex mutex_;
    std::uint64_t last_sequence_{0U};
    bool ready_received_{false};
    bool terminal_received_{false};
    bool process_finalized_{false};
    std::optional<std::int64_t> terminal_exit_code_;
    std::vector<RegisterGeneratedOutputRequest> pending_artifacts_;
};

}  // namespace biocore::application
