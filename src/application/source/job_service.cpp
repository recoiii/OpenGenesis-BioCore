#include "biocore/application/job_service.hpp"

#include <stdexcept>
#include <utility>

#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_job_repository.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/job_service_error.hpp"

namespace biocore::application {

JobService::JobService(
    IJobRepository& repository,
    IIdGenerator& id_generator,
    IUtcClock& clock
) noexcept
    : repository_{repository}, id_generator_{id_generator}, clock_{clock} {}

domain::Job JobService::create(const CreateJobRequest& request) {
    const std::string timestamp = clock_.now_utc_iso8601();

    for (int attempt = 0; attempt < maximum_identifier_attempts; ++attempt) {
        domain::Job job{
            id_generator_.generate(),
            request.analysis_id,
            request.pipeline_id,
            request.pipeline_version,
            domain::JobStatus::draft,
            request.priority,
            0.0,
            std::nullopt,
            timestamp,
            timestamp,
            std::nullopt,
            std::nullopt,
            0,
        };

        if (repository_.add(job)) {
            return job;
        }
    }

    throw JobServiceError{
        JobServiceErrorCode::identifier_generation_exhausted,
        "Unable to generate a unique job identifier",
    };
}

domain::Job JobService::transition(
    const std::string_view job_id,
    const domain::JobStatus target,
    const double progress,
    std::optional<std::string> active_step_id,
    std::optional<JobFailureContext> failure
) {
    auto job = repository_.find_by_id(job_id);
    if (!job.has_value()) {
        throw JobServiceError{JobServiceErrorCode::job_not_found, "Job was not found"};
    }

    if (job->status() == domain::JobStatus::interrupted && target == domain::JobStatus::queued) {
        throw std::invalid_argument("Interrupted jobs must be retried through JobRetryService");
    }
    if (!domain::can_transition(job->status(), target)) {
        throw std::invalid_argument("Invalid job status transition");
    }

    const std::int64_t expected_revision = job->revision();
    const std::string transition_at = clock_.now_utc_iso8601();
    std::optional<domain::JobFailure> failure_evidence;
    if (failure.has_value()) {
        failure_evidence.emplace(
            failure->kind,
            std::move(failure->message),
            failure->exit_code,
            std::move(failure->worker_timestamp_utc),
            transition_at
        );
    }
    job->transition_to(
        target, progress, std::move(active_step_id), transition_at, std::move(failure_evidence)
    );

    if (!repository_.update_runtime_state(*job, expected_revision)) {
        throw JobServiceError{
            JobServiceErrorCode::concurrent_update,
            "Job changed before its runtime state could be persisted",
        };
    }

    return *job;
}

domain::Job JobService::update_progress(
    const std::string_view job_id,
    const double progress,
    std::optional<std::string> active_step_id
) {
    auto job = repository_.find_by_id(job_id);
    if (!job.has_value()) {
        throw JobServiceError{JobServiceErrorCode::job_not_found, "Job was not found"};
    }

    const std::int64_t expected_revision = job->revision();
    job->update_progress(progress, std::move(active_step_id), clock_.now_utc_iso8601());
    if (!repository_.update_runtime_state(*job, expected_revision)) {
        throw JobServiceError{
            JobServiceErrorCode::concurrent_update,
            "Job changed before its progress could be persisted",
        };
    }
    return *job;
}

std::optional<domain::Job> JobService::find_by_id(const std::string_view job_id) {
    return repository_.find_by_id(job_id);
}

std::vector<domain::Job> JobService::list() {
    return repository_.list();
}

}  // namespace biocore::application
