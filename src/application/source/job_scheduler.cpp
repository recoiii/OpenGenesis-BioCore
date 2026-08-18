#include "biocore/application/job_scheduler.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "biocore/application/i_prepared_job_store.hpp"
#include "biocore/application/i_worker_supervisor.hpp"
#include "biocore/application/job_scheduler_error.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/job_service_error.hpp"
#include "biocore/application/worker_launch_request.hpp"
#include "biocore/domain/job.hpp"
#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/job_status.hpp"

namespace biocore::application {
namespace {

[[nodiscard]] int priority_rank(const domain::JobPriority priority) noexcept {
    switch (priority) {
        case domain::JobPriority::high:
            return 0;
        case domain::JobPriority::normal:
            return 1;
        case domain::JobPriority::low:
            return 2;
    }
    return 3;
}

[[nodiscard]] bool prepared_execution_matches_job(
    const domain::Job& job,
    const PreparedJobExecution& execution,
    const std::int64_t expected_launch_revision
) {
    return execution.job_id == job.id() &&
           execution.launch_revision == expected_launch_revision &&
           !execution.execution_plan_path.empty() &&
           job.pipeline_id().has_value() &&
           job.pipeline_version().has_value() &&
           execution.pipeline_id == *job.pipeline_id() &&
           execution.pipeline_version == *job.pipeline_version();
}

[[nodiscard]] WorkerLaunchRequest make_launch_request(
    const domain::Job& job,
    const PreparedJobExecution& execution
) {
    if (!prepared_execution_matches_job(job, execution, job.revision())) {
        throw std::invalid_argument("Prepared execution does not match the preparing job");
    }

    return WorkerLaunchRequest{
        .job_id = std::string{job.id()},
        .analysis_id = job.analysis_id(),
        .pipeline_id = job.pipeline_id(),
        .pipeline_version = job.pipeline_version(),
        .priority = job.priority(),
        .job_revision = job.revision(),
        .execution_plan_path = execution.execution_plan_path,
    };
}

[[nodiscard]] bool is_scheduler_race(const JobServiceError& error) noexcept {
    return error.code() == JobServiceErrorCode::job_not_found ||
           error.code() == JobServiceErrorCode::concurrent_update;
}

class TickGuard final {
public:
    explicit TickGuard(std::atomic_flag& flag) : flag_{flag} {
        if (flag_.test_and_set(std::memory_order_acquire)) {
            throw JobSchedulerError{
                JobSchedulerErrorCode::tick_already_in_progress,
                {},
                "A scheduler tick is already in progress",
            };
        }
    }

    ~TickGuard() {
        flag_.clear(std::memory_order_release);
    }

    TickGuard(const TickGuard&) = delete;
    TickGuard& operator=(const TickGuard&) = delete;

private:
    std::atomic_flag& flag_;
};

}  // namespace

JobScheduler::JobScheduler(
    JobService& job_service,
    IPreparedJobStore& prepared_jobs,
    IWorkerSupervisor& worker_supervisor,
    const std::size_t maximum_concurrent_jobs
)
    : job_service_{job_service},
      prepared_jobs_{prepared_jobs},
      worker_supervisor_{worker_supervisor},
      maximum_concurrent_jobs_{maximum_concurrent_jobs} {
    if (maximum_concurrent_jobs_ == 0U) {
        throw std::invalid_argument("Maximum concurrent jobs must be greater than zero");
    }
}

JobSchedulerTickResult JobScheduler::tick() {
    return tick(0U);
}

JobSchedulerTickResult JobScheduler::tick(const std::size_t externally_reserved_slots) {
    const TickGuard tick_guard{tick_in_progress_};
    std::vector<domain::Job> jobs = job_service_.list();

    JobSchedulerTickResult result;
    result.reserved_slots = externally_reserved_slots;
    result.active_jobs_before = static_cast<std::size_t>(std::ranges::count_if(
        jobs, [](const domain::Job& job) { return domain::occupies_worker_slot(job.status()); }
    ));
    std::size_t occupied_slots =
        std::min(result.active_jobs_before, maximum_concurrent_jobs_);
    occupied_slots += std::min(result.reserved_slots, maximum_concurrent_jobs_ - occupied_slots);
    result.available_slots = maximum_concurrent_jobs_ - occupied_slots;

    std::vector<domain::Job> queued_jobs;
    std::ranges::copy_if(jobs, std::back_inserter(queued_jobs), [](const domain::Job& job) {
        return job.status() == domain::JobStatus::queued;
    });
    result.queued_jobs_seen = queued_jobs.size();

    std::ranges::sort(queued_jobs, [](const domain::Job& left, const domain::Job& right) {
        return std::tuple{
                   priority_rank(left.priority()), left.created_at_utc(), left.id()
               } <
               std::tuple{
                   priority_rank(right.priority()), right.created_at_utc(), right.id()
               };
    });

    for (const domain::Job& queued_job : queued_jobs) {
        if (result.launched_job_ids.size() >= result.available_slots) {
            break;
        }

        const std::string job_id{queued_job.id()};
        const auto prepared_execution = prepared_jobs_.find_execution(job_id);
        if (!prepared_execution.has_value()) {
            result.skipped_job_ids.push_back(job_id);
            continue;
        }
        if (!prepared_execution_matches_job(
                queued_job, *prepared_execution, queued_job.revision() + 1
            )) {
            result.skipped_job_ids.push_back(job_id);
            continue;
        }

        domain::Job preparing_job = queued_job;
        try {
            preparing_job = job_service_.transition(
                job_id,
                domain::JobStatus::preparing,
                queued_job.progress(),
                std::nullopt
            );
        } catch (const JobServiceError& error) {
            if (is_scheduler_race(error)) {
                result.skipped_job_ids.push_back(job_id);
                continue;
            }
            throw;
        } catch (const std::invalid_argument&) {
            result.skipped_job_ids.push_back(job_id);
            continue;
        }

        try {
            WorkerLaunchRequest launch_request = make_launch_request(preparing_job, *prepared_execution);
            worker_supervisor_.launch(launch_request);
            result.launched_job_ids.push_back(job_id);
            result.launched_workers.push_back(std::move(launch_request));
        } catch (...) {
            try {
                static_cast<void>(job_service_.transition(
                    job_id,
                    domain::JobStatus::failed,
                    preparing_job.progress(),
                    std::nullopt
                ));
                result.launch_failed_job_ids.push_back(job_id);
            } catch (...) {
                throw JobSchedulerError{
                    JobSchedulerErrorCode::launch_failure_recovery_failed,
                    job_id,
                    "Worker launch failed and the job could not be persisted as failed",
                };
            }
        }
    }

    return result;
}

std::size_t JobScheduler::maximum_concurrent_jobs() const noexcept {
    return maximum_concurrent_jobs_;
}

}  // namespace biocore::application
