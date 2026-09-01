#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

#include "biocore/application/worker_launch_request.hpp"

namespace biocore::application {

class IPreparedJobStore;
class IWorkerSupervisor;
class JobService;

struct JobSchedulerTickResult final {
    std::size_t active_jobs_before{0U};
    std::size_t reserved_slots{0U};
    std::size_t available_slots{0U};
    std::size_t queued_jobs_seen{0U};
    std::vector<std::string> launched_job_ids;
    std::vector<WorkerLaunchRequest> launched_workers;
    std::vector<std::string> launch_failed_job_ids;
    std::vector<std::string> skipped_job_ids;
};

class JobScheduler final {
public:
    static constexpr std::size_t default_maximum_concurrent_jobs = 2U;
    static constexpr std::size_t maximum_supported_concurrent_jobs = 64U;

    JobScheduler(
        JobService& job_service,
        IPreparedJobStore& prepared_jobs,
        IWorkerSupervisor& worker_supervisor,
        std::size_t maximum_concurrent_jobs
    );

    [[nodiscard]] JobSchedulerTickResult tick();
    [[nodiscard]] JobSchedulerTickResult tick(std::size_t externally_reserved_slots);
    [[nodiscard]] std::size_t maximum_concurrent_jobs() const noexcept;

private:
    JobService& job_service_;
    IPreparedJobStore& prepared_jobs_;
    IWorkerSupervisor& worker_supervisor_;
    std::size_t maximum_concurrent_jobs_;
    std::atomic_flag tick_in_progress_ = ATOMIC_FLAG_INIT;
};

}  // namespace biocore::application
