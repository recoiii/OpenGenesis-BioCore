#include "biocore/application/job_submission_service.hpp"

#include <cstdint>
#include <stdexcept>
#include <utility>

#include "biocore/application/i_execution_plan_store.hpp"
#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_pipeline_catalog.hpp"
#include "biocore/application/i_prepared_job_store.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/job_submission_service_error.hpp"
#include "biocore/application/pipeline_preparation_service.hpp"
#include "biocore/domain/job_status.hpp"

namespace biocore::application {

JobSubmissionService::JobSubmissionService(
    IPreparedJobStore& prepared_jobs,
    IPipelineCatalog& pipelines,
    PipelinePreparationService& preparation,
    IExecutionPlanStore& execution_plan_store,
    IIdGenerator& id_generator,
    IUtcClock& clock
) noexcept
    : prepared_jobs_{prepared_jobs}, pipelines_{pipelines}, preparation_{preparation},
      execution_plan_store_{execution_plan_store}, id_generator_{id_generator}, clock_{clock} {}

domain::Job JobSubmissionService::submit(const SubmitJobRequest& request) {
    if (request.pipeline_id.empty() || request.pipeline_version.empty()) {
        throw std::invalid_argument("Pipeline id and version are required");
    }
    const auto definition = pipelines_.find(request.pipeline_id, request.pipeline_version);
    if (!definition.has_value()) {
        throw JobSubmissionError{
            JobSubmissionErrorCode::pipeline_not_found,
            "Requested pipeline id/version was not found",
        };
    }

    for (int attempt = 0; attempt < maximum_identifier_attempts; ++attempt) {
        const std::string timestamp = clock_.now_utc_iso8601();
        domain::Job job{
            id_generator_.generate(),
            request.analysis_id,
            std::string{definition->id()},
            std::string{definition->version()},
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

        const std::int64_t prepared_revision = job.revision() + 2;
        const PreparedExecutionPlan prepared = preparation_.prepare(
            *definition, job.id(), prepared_revision, request.bindings
        );
        job.transition_to(domain::JobStatus::queued, 0.0, std::nullopt, clock_.now_utc_iso8601());

        const PreparedJobExecution execution{
            .job_id = std::string{job.id()},
            .launch_revision = prepared_revision,
            .pipeline_id = std::string{definition->id()},
            .pipeline_version = std::string{definition->version()},
            .execution_plan_path = prepared.snapshot_path,
            .prepared_at_utc = std::string{job.updated_at_utc()},
        };
        try {
            if (prepared_jobs_.add_prepared_job(job, execution)) {
                return job;
            }
        } catch (...) {
            try {
                execution_plan_store_.discard(prepared.snapshot_path);
            } catch (...) {
            }
            throw;
        }
        execution_plan_store_.discard(prepared.snapshot_path);
    }

    throw JobSubmissionError{
        JobSubmissionErrorCode::identifier_generation_exhausted,
        "Unable to generate a unique prepared job identifier",
    };
}

}  // namespace biocore::application
