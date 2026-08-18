#pragma once

#include "biocore/application/i_job_submitter.hpp"

namespace biocore::application {

class IExecutionPlanStore;
class IIdGenerator;
class IPipelineCatalog;
class IPreparedJobStore;
class IUtcClock;
class PipelinePreparationService;

class JobSubmissionService final : public IJobSubmitter {
public:
    static constexpr int maximum_identifier_attempts = 8;

    JobSubmissionService(
        IPreparedJobStore& prepared_jobs,
        IPipelineCatalog& pipelines,
        PipelinePreparationService& preparation,
        IExecutionPlanStore& execution_plan_store,
        IIdGenerator& id_generator,
        IUtcClock& clock
    ) noexcept;

    [[nodiscard]] domain::Job submit(const SubmitJobRequest& request) override;

private:
    IPreparedJobStore& prepared_jobs_;
    IPipelineCatalog& pipelines_;
    PipelinePreparationService& preparation_;
    IExecutionPlanStore& execution_plan_store_;
    IIdGenerator& id_generator_;
    IUtcClock& clock_;
};

}  // namespace biocore::application
