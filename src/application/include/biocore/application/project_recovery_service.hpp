#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "biocore/application/i_partial_output_cleaner.hpp"
#include "biocore/application/i_quarantine_retention_store.hpp"
#include "biocore/domain/job_status.hpp"

namespace biocore::application {

class IManagedFileRepository;
class JobService;
class OutputArtifactCleanupService;

enum class ProjectRecoveryIssueStage {
    checkpoint_read,
    job_transition,
    partial_output_cleanup,
    quarantine_retention,
};

struct ProjectRecoveryIssue final {
    ProjectRecoveryIssueStage stage;
    std::optional<std::string> job_id;
    std::string message;
};

struct RecoveredJob final {
    std::string job_id;
    domain::JobStatus previous_status;
    double previous_progress;
    double recovered_progress;
};

struct ProjectRecoveryPolicy final {
    bool cleanup_interrupted_jobs{true};
    bool purge_expired_quarantine{true};
    std::chrono::seconds quarantine_retention{std::chrono::hours{24 * 30}};
};

struct ProjectRecoveryResult final {
    std::vector<RecoveredJob> recovered_jobs;
    std::vector<std::string> cleanup_job_ids;
    std::optional<QuarantineRetentionResult> retention;
    std::vector<ProjectRecoveryIssue> issues;
};

class ProjectRecoveryService final {
public:
    ProjectRecoveryService(
        JobService& job_service,
        IManagedFileRepository& managed_file_repository,
        OutputArtifactCleanupService* output_cleanup_service,
        IQuarantineRetentionStore* quarantine_retention_store,
        ProjectRecoveryPolicy policy = {}
    );

    [[nodiscard]] ProjectRecoveryResult recover();

private:
    JobService& job_service_;
    IManagedFileRepository& managed_file_repository_;
    OutputArtifactCleanupService* output_cleanup_service_;
    IQuarantineRetentionStore* quarantine_retention_store_;
    ProjectRecoveryPolicy policy_;
};

}  // namespace biocore::application
