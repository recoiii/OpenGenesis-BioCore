#include <algorithm>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_job_repository.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/i_worker_supervisor.hpp"
#include "biocore/application/job_scheduler.hpp"
#include "biocore/application/job_scheduler_error.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/worker_launch_request.hpp"
#include "biocore/domain/job.hpp"
#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/job_status.hpp"

#include "prepared_job_store_test_support.hpp"

namespace {

using biocore::application::IIdGenerator;
using biocore::application::IJobRepository;
using biocore::application::IUtcClock;
using biocore::application::IWorkerSupervisor;
using biocore::application::JobScheduler;
using biocore::application::JobSchedulerError;
using biocore::application::JobSchedulerErrorCode;
using biocore::application::JobService;
using biocore::application::WorkerLaunchRequest;
using biocore::domain::Job;
using biocore::domain::JobPriority;
using biocore::domain::JobStatus;

class UnusedIdGenerator final : public IIdGenerator {
public:
    std::string generate() override {
        throw std::runtime_error("Scheduler must not generate identifiers");
    }
};

class TraceClock final : public IUtcClock {
public:
    TraceClock(std::deque<std::string> values, std::vector<std::string>& trace)
        : values_{std::move(values)}, trace_{trace} {}

    std::string now_utc_iso8601() override {
        trace_.push_back("clock");
        if (values_.empty()) {
            throw std::runtime_error("Fake scheduler clock exhausted");
        }
        std::string value = std::move(values_.front());
        values_.pop_front();
        return value;
    }

private:
    std::deque<std::string> values_;
    std::vector<std::string>& trace_;
};

class FakeJobRepository final : public IJobRepository {
public:
    explicit FakeJobRepository(std::vector<std::string>& trace) : trace_{trace} {}

    bool add(const Job& job) override {
        if (find_index(job.id()).has_value()) {
            return false;
        }
        jobs.push_back(job);
        return true;
    }

    std::optional<Job> find_by_id(const std::string_view job_id) override {
        trace_.push_back("find:" + std::string{job_id});
        const auto index = find_index(job_id);
        if (!index.has_value()) {
            return std::nullopt;
        }
        return jobs[*index];
    }

    std::vector<Job> list() override {
        trace_.push_back("list");
        return jobs;
    }

    bool update_runtime_state(const Job& job, const std::int64_t expected_revision) override {
        ++update_call_count;
        trace_.push_back("update:" + std::string{job.id()});
        if (failed_update_calls.erase(update_call_count) > 0U) {
            return false;
        }
        const auto index = find_index(job.id());
        if (!index.has_value() || jobs[*index].revision() != expected_revision) {
            return false;
        }
        jobs[*index] = job;
        return true;
    }

    [[nodiscard]] const Job* find_stored(const std::string_view id) const {
        const auto index = find_index(id);
        return index.has_value() ? &jobs[*index] : nullptr;
    }

    std::vector<Job> jobs;
    std::set<int> failed_update_calls;
    int update_call_count{0};

private:
    [[nodiscard]] std::optional<std::size_t> find_index(const std::string_view id) const {
        for (std::size_t index = 0; index < jobs.size(); ++index) {
            if (jobs[index].id() == id) {
                return index;
            }
        }
        return std::nullopt;
    }

    std::vector<std::string>& trace_;
};

class FakeWorkerSupervisor final : public IWorkerSupervisor {
public:
    explicit FakeWorkerSupervisor(std::vector<std::string>& trace) : trace_{trace} {}

    void launch(const WorkerLaunchRequest& request) override {
        trace_.push_back("launch:" + request.job_id);
        requests.push_back(request);
        if (failing_job_ids.contains(request.job_id)) {
            throw std::runtime_error("Synthetic worker launch failure");
        }
    }

    std::vector<WorkerLaunchRequest> requests;
    std::set<std::string> failing_job_ids;

private:
    std::vector<std::string>& trace_;
};

class ReentrantWorkerSupervisor final : public IWorkerSupervisor {
public:
    void launch(const WorkerLaunchRequest&) override {
        if (scheduler == nullptr) {
            throw std::runtime_error("Scheduler callback was not configured");
        }
        try {
            static_cast<void>(scheduler->tick());
        } catch (const JobSchedulerError& error) {
            rejected_reentrant_tick = error.code() == JobSchedulerErrorCode::tick_already_in_progress &&
                                      error.job_id().empty();
            return;
        }
        throw std::runtime_error("Reentrant scheduler tick was unexpectedly accepted");
    }

    JobScheduler* scheduler{nullptr};
    bool rejected_reentrant_tick{false};
};

[[nodiscard]] bool needs_start_timestamp(const JobStatus status) noexcept {
    return status == JobStatus::preparing || status == JobStatus::running ||
           status == JobStatus::paused || status == JobStatus::cancelling ||
           status == JobStatus::completed || status == JobStatus::failed ||
           status == JobStatus::interrupted;
}

[[nodiscard]] Job make_job(
    std::string id,
    const JobStatus status,
    const JobPriority priority,
    std::string created_at,
    std::optional<std::string> analysis_id = std::string{"analysis-1"}
) {
    const double progress = status == JobStatus::completed ? 1.0 : 0.25;
    const std::optional<std::string> started_at = needs_start_timestamp(status)
                                                       ? std::optional<std::string>{created_at}
                                                       : std::nullopt;
    const std::optional<std::string> finished_at = biocore::domain::is_terminal(status)
                                                        ? std::optional<std::string>{created_at}
                                                        : std::nullopt;
    const std::optional<std::string> active_step =
        !biocore::domain::is_terminal(status) && biocore::domain::occupies_worker_slot(status)
            ? std::optional<std::string>{"existing-step"}
            : std::nullopt;

    return Job{
        std::move(id),
        std::move(analysis_id),
        std::string{"org.biocore.pipeline"},
        std::string{"1.2.3"},
        status,
        priority,
        progress,
        active_step,
        created_at,
        created_at,
        started_at,
        finished_at,
        0,
    };
}

[[nodiscard]] bool constructor_contract() {
    std::vector<std::string> trace;
    FakeJobRepository repository{trace};
    UnusedIdGenerator ids;
    TraceClock clock{{}, trace};
    JobService service{repository, ids, clock};
    FakeWorkerSupervisor supervisor{trace};

    try {
        PreparedJobTestStore prepared_jobs;
        JobScheduler scheduler{service, prepared_jobs, supervisor, 0U};
        static_cast<void>(scheduler);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

[[nodiscard]] bool capacity_priority_and_metadata_contract() {
    std::vector<std::string> trace;
    FakeJobRepository repository{trace};
    repository.jobs = {
        make_job("active-preparing", JobStatus::preparing, JobPriority::normal, "2026-08-06T20:00:00Z"),
        make_job("active-running", JobStatus::running, JobPriority::normal, "2026-08-06T20:00:01Z"),
        make_job("active-paused", JobStatus::paused, JobPriority::normal, "2026-08-06T20:00:02Z"),
        make_job("active-cancelling", JobStatus::cancelling, JobPriority::normal, "2026-08-06T20:00:03Z"),
        make_job("low-old", JobStatus::queued, JobPriority::low, "2026-08-06T19:00:00Z"),
        make_job("high-later", JobStatus::queued, JobPriority::high, "2026-08-06T19:30:00Z"),
        make_job("high-earlier", JobStatus::queued, JobPriority::high, "2026-08-06T19:20:00Z", std::string{"analysis-special"}),
        make_job("normal", JobStatus::queued, JobPriority::normal, "2026-08-06T19:10:00Z"),
    };
    UnusedIdGenerator ids;
    TraceClock clock{{"2026-08-06T21:00:00Z", "2026-08-06T21:00:01Z"}, trace};
    JobService service{repository, ids, clock};
    FakeWorkerSupervisor supervisor{trace};
    PreparedJobTestStore prepared_jobs;
    JobScheduler scheduler{service, prepared_jobs, supervisor, 6U};

    const auto result = scheduler.tick();
    if (scheduler.maximum_concurrent_jobs() != 6U || result.active_jobs_before != 4U ||
        result.available_slots != 2U || result.queued_jobs_seen != 4U ||
        result.launched_job_ids != std::vector<std::string>{"high-earlier", "high-later"} ||
        !result.launch_failed_job_ids.empty() || !result.skipped_job_ids.empty() ||
        supervisor.requests.size() != 2U) {
        return false;
    }

    const auto& first = supervisor.requests.front();
    if (first.job_id != "high-earlier" || first.analysis_id != std::optional<std::string>{"analysis-special"} ||
        first.pipeline_id != std::optional<std::string>{"org.biocore.pipeline"} ||
        first.pipeline_version != std::optional<std::string>{"1.2.3"} ||
        first.priority != JobPriority::high || first.job_revision != 1) {
        return false;
    }

    const Job* first_stored = repository.find_stored("high-earlier");
    const Job* second_stored = repository.find_stored("high-later");
    return first_stored != nullptr && second_stored != nullptr &&
           first_stored->status() == JobStatus::preparing &&
           second_stored->status() == JobStatus::preparing &&
           trace == std::vector<std::string>{
                        "list",
                        "find:high-earlier",
                        "clock",
                        "update:high-earlier",
                        "launch:high-earlier",
                        "find:high-later",
                        "clock",
                        "update:high-later",
                        "launch:high-later",
                    };
}

[[nodiscard]] bool concurrency_skip_backfills_contract() {
    std::vector<std::string> trace;
    FakeJobRepository repository{trace};
    repository.jobs = {
        make_job("high-raced", JobStatus::queued, JobPriority::high, "2026-08-06T19:00:00Z"),
        make_job("normal-next", JobStatus::queued, JobPriority::normal, "2026-08-06T19:01:00Z"),
        make_job("low-last", JobStatus::queued, JobPriority::low, "2026-08-06T19:02:00Z"),
    };
    repository.failed_update_calls.insert(1);
    UnusedIdGenerator ids;
    TraceClock clock{{"2026-08-06T21:10:00Z", "2026-08-06T21:10:01Z"}, trace};
    JobService service{repository, ids, clock};
    FakeWorkerSupervisor supervisor{trace};
    PreparedJobTestStore prepared_jobs;
    JobScheduler scheduler{service, prepared_jobs, supervisor, 1U};

    const auto result = scheduler.tick();
    const Job* raced = repository.find_stored("high-raced");
    const Job* next = repository.find_stored("normal-next");
    return result.skipped_job_ids == std::vector<std::string>{"high-raced"} &&
           result.launched_job_ids == std::vector<std::string>{"normal-next"} &&
           result.launch_failed_job_ids.empty() && supervisor.requests.size() == 1U &&
           raced != nullptr && raced->status() == JobStatus::queued && next != nullptr &&
           next->status() == JobStatus::preparing;
}

[[nodiscard]] bool launch_failure_backfills_contract() {
    std::vector<std::string> trace;
    FakeJobRepository repository{trace};
    repository.jobs = {
        make_job("high-fails", JobStatus::queued, JobPriority::high, "2026-08-06T19:00:00Z"),
        make_job("normal-launches", JobStatus::queued, JobPriority::normal, "2026-08-06T19:01:00Z"),
    };
    UnusedIdGenerator ids;
    TraceClock clock{{
        "2026-08-06T21:20:00Z",
        "2026-08-06T21:20:01Z",
        "2026-08-06T21:20:02Z",
    }, trace};
    JobService service{repository, ids, clock};
    FakeWorkerSupervisor supervisor{trace};
    supervisor.failing_job_ids.insert("high-fails");
    PreparedJobTestStore prepared_jobs;
    JobScheduler scheduler{service, prepared_jobs, supervisor, 1U};

    const auto result = scheduler.tick();
    const Job* failed = repository.find_stored("high-fails");
    const Job* launched = repository.find_stored("normal-launches");
    return result.launch_failed_job_ids == std::vector<std::string>{"high-fails"} &&
           result.launched_job_ids == std::vector<std::string>{"normal-launches"} &&
           failed != nullptr && failed->status() == JobStatus::failed &&
           failed->started_at_utc() == std::optional<std::string>{"2026-08-06T21:20:00Z"} &&
           failed->finished_at_utc() == std::optional<std::string>{"2026-08-06T21:20:01Z"} &&
           launched != nullptr && launched->status() == JobStatus::preparing;
}

[[nodiscard]] bool launch_failure_recovery_contract() {
    std::vector<std::string> trace;
    FakeJobRepository repository{trace};
    repository.jobs = {
        make_job("job-unrecoverable", JobStatus::queued, JobPriority::high, "2026-08-06T19:00:00Z"),
    };
    repository.failed_update_calls.insert(2);
    UnusedIdGenerator ids;
    TraceClock clock{{"2026-08-06T21:30:00Z", "2026-08-06T21:30:01Z"}, trace};
    JobService service{repository, ids, clock};
    FakeWorkerSupervisor supervisor{trace};
    supervisor.failing_job_ids.insert("job-unrecoverable");
    PreparedJobTestStore prepared_jobs;
    JobScheduler scheduler{service, prepared_jobs, supervisor, 1U};

    try {
        static_cast<void>(scheduler.tick());
    } catch (const JobSchedulerError& error) {
        const Job* stored = repository.find_stored("job-unrecoverable");
        return error.code() == JobSchedulerErrorCode::launch_failure_recovery_failed &&
               error.job_id() == "job-unrecoverable" && stored != nullptr &&
               stored->status() == JobStatus::preparing;
    }
    return false;
}

[[nodiscard]] bool reentrant_tick_contract() {
    std::vector<std::string> trace;
    FakeJobRepository repository{trace};
    repository.jobs = {
        make_job("job-reentrant", JobStatus::queued, JobPriority::high, "2026-08-06T19:00:00Z"),
    };
    UnusedIdGenerator ids;
    TraceClock clock{{"2026-08-06T21:40:00Z"}, trace};
    JobService service{repository, ids, clock};
    ReentrantWorkerSupervisor supervisor;
    PreparedJobTestStore prepared_jobs;
    JobScheduler scheduler{service, prepared_jobs, supervisor, 1U};
    supervisor.scheduler = &scheduler;

    const auto first_result = scheduler.tick();
    const auto second_result = scheduler.tick();
    return supervisor.rejected_reentrant_tick &&
           first_result.launched_job_ids == std::vector<std::string>{"job-reentrant"} &&
           second_result.active_jobs_before == 1U && second_result.available_slots == 0U &&
           second_result.launched_job_ids.empty();
}

[[nodiscard]] bool no_capacity_contract() {
    std::vector<std::string> trace;
    FakeJobRepository repository{trace};
    repository.jobs = {
        make_job("active", JobStatus::running, JobPriority::normal, "2026-08-06T19:00:00Z"),
        make_job("queued", JobStatus::queued, JobPriority::high, "2026-08-06T19:01:00Z"),
    };
    UnusedIdGenerator ids;
    TraceClock clock{{}, trace};
    JobService service{repository, ids, clock};
    FakeWorkerSupervisor supervisor{trace};
    PreparedJobTestStore prepared_jobs;
    JobScheduler scheduler{service, prepared_jobs, supervisor, 1U};

    const auto result = scheduler.tick();
    return result.active_jobs_before == 1U && result.available_slots == 0U &&
           result.queued_jobs_seen == 1U && result.launched_job_ids.empty() &&
           supervisor.requests.empty() && trace == std::vector<std::string>{"list"};
}


[[nodiscard]] bool missing_prepared_execution_blocks_launch_contract() {
    std::vector<std::string> trace;
    FakeJobRepository repository{trace};
    repository.jobs = {make_job("job-unprepared", JobStatus::queued, JobPriority::high, "2026-08-06T19:00:00Z")};
    UnusedIdGenerator ids;
    TraceClock clock{{}, trace};
    JobService service{repository, ids, clock};
    FakeWorkerSupervisor supervisor{trace};
    PreparedJobTestStore prepared_jobs;
    prepared_jobs.missing_job_id = "job-unprepared";
    JobScheduler scheduler{service, prepared_jobs, supervisor, 1U};
    const auto result = scheduler.tick();
    const Job* stored = repository.find_stored("job-unprepared");
    return result.queued_jobs_seen == 1U && result.launched_job_ids.empty() &&
           result.skipped_job_ids == std::vector<std::string>{"job-unprepared"} &&
           supervisor.requests.empty() && stored != nullptr && stored->status() == JobStatus::queued &&
           trace == std::vector<std::string>{"list"};
}

[[nodiscard]] bool prepared_pipeline_identity_mismatch_blocks_launch_contract() {
    const auto verify = [](std::string pipeline_id, std::string pipeline_version) {
        std::vector<std::string> trace;
        FakeJobRepository repository{trace};
        repository.jobs = {
            make_job(
                "job-mismatched-prepared",
                JobStatus::queued,
                JobPriority::high,
                "2026-08-06T19:00:00Z"
            )
        };
        UnusedIdGenerator ids;
        TraceClock clock{{}, trace};
        JobService service{repository, ids, clock};
        FakeWorkerSupervisor supervisor{trace};
        PreparedJobTestStore prepared_jobs;
        prepared_jobs.pipeline_id = std::move(pipeline_id);
        prepared_jobs.pipeline_version = std::move(pipeline_version);
        JobScheduler scheduler{service, prepared_jobs, supervisor, 1U};
        const auto result = scheduler.tick();
        const Job* stored = repository.find_stored("job-mismatched-prepared");
        return result.queued_jobs_seen == 1U && result.launched_job_ids.empty() &&
               result.skipped_job_ids ==
                   std::vector<std::string>{"job-mismatched-prepared"} &&
               supervisor.requests.empty() && stored != nullptr &&
               stored->status() == JobStatus::queued &&
               trace == std::vector<std::string>{"list"};
    };

    return verify("org.biocore.other", "1.2.3") &&
           verify("org.biocore.pipeline", "9.9.9");
}

}  // namespace

int main() {
    if (!constructor_contract()) {
        std::cerr << "JobScheduler constructor contract failed\n";
        return EXIT_FAILURE;
    }
    if (!capacity_priority_and_metadata_contract()) {
        std::cerr << "JobScheduler capacity/priority contract failed\n";
        return EXIT_FAILURE;
    }
    if (!concurrency_skip_backfills_contract()) {
        std::cerr << "JobScheduler concurrency backfill contract failed\n";
        return EXIT_FAILURE;
    }
    if (!launch_failure_backfills_contract()) {
        std::cerr << "JobScheduler launch failure backfill contract failed\n";
        return EXIT_FAILURE;
    }
    if (!launch_failure_recovery_contract()) {
        std::cerr << "JobScheduler launch failure recovery contract failed\n";
        return EXIT_FAILURE;
    }
    if (!reentrant_tick_contract()) {
        std::cerr << "JobScheduler reentrant tick contract failed\n";
        return EXIT_FAILURE;
    }
    if (!no_capacity_contract()) {
        std::cerr << "JobScheduler no-capacity contract failed\n";
        return EXIT_FAILURE;
    }
    if (!missing_prepared_execution_blocks_launch_contract()) {
        std::cerr << "JobScheduler missing prepared execution contract failed\n";
        return EXIT_FAILURE;
    }
    if (!prepared_pipeline_identity_mismatch_blocks_launch_contract()) {
        std::cerr << "JobScheduler prepared pipeline identity mismatch contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
