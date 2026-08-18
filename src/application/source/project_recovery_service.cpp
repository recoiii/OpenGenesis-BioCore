#include "biocore/application/project_recovery_service.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_set>

#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/output_artifact_cleanup_service.hpp"
#include "biocore/domain/job_status.hpp"

namespace biocore::application {
namespace {

[[nodiscard]] ProjectRecoveryIssue issue(
    const ProjectRecoveryIssueStage stage,
    std::optional<std::string> job_id,
    const std::string& message
) {
    return ProjectRecoveryIssue{stage, std::move(job_id), message};
}

}  // namespace

ProjectRecoveryService::ProjectRecoveryService(
    JobService& job_service,
    IManagedFileRepository& managed_file_repository,
    OutputArtifactCleanupService* const output_cleanup_service,
    IQuarantineRetentionStore* const quarantine_retention_store,
    ProjectRecoveryPolicy policy
)
    : job_service_{job_service},
      managed_file_repository_{managed_file_repository},
      output_cleanup_service_{output_cleanup_service},
      quarantine_retention_store_{quarantine_retention_store},
      policy_{policy} {
    if (policy_.quarantine_retention <= std::chrono::seconds::zero()) {
        throw std::invalid_argument("Quarantine retention must be positive");
    }
    if (policy_.purge_expired_quarantine && quarantine_retention_store_ == nullptr) {
        throw std::invalid_argument("Quarantine retention is enabled but no retention store is available");
    }
    if (policy_.cleanup_interrupted_jobs && output_cleanup_service_ == nullptr) {
        throw std::invalid_argument("Interrupted-job cleanup is enabled but no cleanup service is available");
    }
}

ProjectRecoveryResult ProjectRecoveryService::recover() {
    ProjectRecoveryResult result;
    const auto jobs = job_service_.list();
    std::unordered_set<std::string> cleanup_jobs;

    for (const auto& snapshot : jobs) {
        if (domain::occupies_worker_slot(snapshot.status())) {
            double recovered_progress = snapshot.progress();
            try {
                const auto checkpoint =
                    managed_file_repository_.latest_generated_output_progress(snapshot.id());
                if (checkpoint.has_value()) {
                    recovered_progress = std::max(recovered_progress, *checkpoint);
                }
            } catch (const std::exception& error) {
                result.issues.push_back(issue(
                    ProjectRecoveryIssueStage::checkpoint_read,
                    std::string{snapshot.id()},
                    error.what()
                ));
                continue;
            } catch (...) {
                result.issues.push_back(issue(
                    ProjectRecoveryIssueStage::checkpoint_read,
                    std::string{snapshot.id()},
                    "Unknown generated-output checkpoint read failure"
                ));
                continue;
            }

            try {
                const auto interrupted = job_service_.transition(
                    snapshot.id(), domain::JobStatus::interrupted, recovered_progress, std::nullopt
                );
                result.recovered_jobs.push_back(RecoveredJob{
                    .job_id = std::string{snapshot.id()},
                    .previous_status = snapshot.status(),
                    .previous_progress = snapshot.progress(),
                    .recovered_progress = interrupted.progress(),
                });
                cleanup_jobs.insert(std::string{snapshot.id()});
            } catch (const std::exception& error) {
                result.issues.push_back(issue(
                    ProjectRecoveryIssueStage::job_transition,
                    std::string{snapshot.id()},
                    error.what()
                ));
            } catch (...) {
                result.issues.push_back(issue(
                    ProjectRecoveryIssueStage::job_transition,
                    std::string{snapshot.id()},
                    "Unknown stale-job recovery transition failure"
                ));
            }
            continue;
        }

        if (snapshot.status() == domain::JobStatus::interrupted) {
            cleanup_jobs.insert(std::string{snapshot.id()});
        }
    }

    if (policy_.cleanup_interrupted_jobs) {
        std::vector<std::string> ordered{cleanup_jobs.begin(), cleanup_jobs.end()};
        std::ranges::sort(ordered);
        for (const auto& job_id : ordered) {
            try {
                static_cast<void>(output_cleanup_service_->quarantine_unregistered_for_job(job_id));
                result.cleanup_job_ids.push_back(job_id);
            } catch (const std::exception& error) {
                result.issues.push_back(issue(
                    ProjectRecoveryIssueStage::partial_output_cleanup, job_id, error.what()
                ));
            } catch (...) {
                result.issues.push_back(issue(
                    ProjectRecoveryIssueStage::partial_output_cleanup,
                    job_id,
                    "Unknown startup partial-output cleanup failure"
                ));
            }
        }
    }

    if (policy_.purge_expired_quarantine) {
        try {
            result.retention = quarantine_retention_store_->purge_expired(
                policy_.quarantine_retention
            );
        } catch (const std::exception& error) {
            result.issues.push_back(issue(
                ProjectRecoveryIssueStage::quarantine_retention, std::nullopt, error.what()
            ));
        } catch (...) {
            result.issues.push_back(issue(
                ProjectRecoveryIssueStage::quarantine_retention,
                std::nullopt,
                "Unknown quarantine retention failure"
            ));
        }
    }

    return result;
}

}  // namespace biocore::application
