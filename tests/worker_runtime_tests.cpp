#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_job_repository.hpp"
#include "biocore/application/i_monotonic_clock.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/i_worker_supervisor.hpp"
#include "biocore/application/i_worker_lifecycle_event_sink.hpp"
#include "biocore/application/job_scheduler.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/worker_lifecycle_event.hpp"
#include "biocore/application/worker_process.hpp"
#include "biocore/application/worker_runtime.hpp"
#include "biocore/application/worker_runtime_error.hpp"
#include "biocore/domain/job.hpp"
#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/job_status.hpp"

#include "prepared_job_store_test_support.hpp"

namespace {

using namespace std::chrono_literals;
using biocore::application::IIdGenerator;
using biocore::application::IJobRepository;
using biocore::application::IMonotonicClock;
using biocore::application::IUtcClock;
using biocore::application::IWorkerSupervisor;
using biocore::application::JobScheduler;
using biocore::application::JobService;
using biocore::application::WorkerLifecycleEvent;
using biocore::application::WorkerLifecycleEventType;
using biocore::application::WorkerLifecycleLogLevel;
using biocore::application::WorkerProcessExit;
using biocore::application::WorkerProcessOutput;
using biocore::application::WorkerRuntime;
using biocore::application::WorkerRuntimeError;
using biocore::application::WorkerRuntimeErrorCode;
using biocore::application::WorkerRuntimeIssueStage;
using biocore::application::WorkerRuntimePolicy;
using biocore::application::WorkerTerminationResult;
using biocore::domain::Job;
using biocore::domain::JobFailureKind;
using biocore::domain::JobPriority;
using biocore::domain::JobStatus;

class UnusedIdGenerator final : public IIdGenerator {
public:
    std::string generate() override {
        throw std::runtime_error("Runtime must not generate identifiers");
    }
};

class ConstantUtcClock final : public IUtcClock {
public:
    std::string now_utc_iso8601() override {
        ++calls;
        return "2026-08-07T00:00:00Z";
    }
    int calls{0};
};

class FakeMonotonicClock final : public IMonotonicClock {
public:
    std::chrono::steady_clock::time_point now() override {
        return std::chrono::steady_clock::time_point{
            std::chrono::milliseconds{milliseconds.load(std::memory_order_acquire)}
        };
    }

    void advance(const std::chrono::milliseconds amount) {
        milliseconds.fetch_add(amount.count(), std::memory_order_acq_rel);
    }

    std::atomic<std::int64_t> milliseconds{0};
};

class FakeJobRepository final : public IJobRepository {
public:
    bool add(const Job& job) override {
        std::scoped_lock lock{mutex_};
        if (find_index(job.id()).has_value()) return false;
        jobs_.push_back(job);
        return true;
    }

    std::optional<Job> find_by_id(const std::string_view job_id) override {
        std::scoped_lock lock{mutex_};
        const auto index = find_index(job_id);
        return index.has_value() ? std::optional<Job>{jobs_[*index]} : std::nullopt;
    }

    std::vector<Job> list() override {
        std::scoped_lock lock{mutex_};
        return jobs_;
    }

    bool update_runtime_state(const Job& job, const std::int64_t expected_revision) override {
        std::scoped_lock lock{mutex_};
        const auto index = find_index(job.id());
        if (!index.has_value() || jobs_[*index].revision() != expected_revision) return false;
        jobs_[*index] = job;
        return true;
    }

    void seed(Job job) {
        std::scoped_lock lock{mutex_};
        jobs_.push_back(std::move(job));
    }

private:
    [[nodiscard]] std::optional<std::size_t> find_index(const std::string_view id) const {
        for (std::size_t index = 0; index < jobs_.size(); ++index) {
            if (jobs_[index].id() == id) return index;
        }
        return std::nullopt;
    }

    mutable std::mutex mutex_;
    std::vector<Job> jobs_;
};

class FakeWorkerSupervisor final : public IWorkerSupervisor {
public:
    void launch(const biocore::application::WorkerLaunchRequest& request) override {
        std::scoped_lock lock{mutex};
        launches.push_back(request);
    }

    std::vector<WorkerProcessOutput> poll_output() override {
        ++poll_calls;
        if (throw_poll_once.exchange(false, std::memory_order_acq_rel)) {
            throw std::runtime_error("Synthetic poll failure");
        }
        if (block_poll.load(std::memory_order_acquire)) {
            std::unique_lock lock{block_mutex};
            poll_entered = true;
            block_condition.notify_all();
            block_condition.wait(lock, [this] { return release_poll; });
        }
        std::scoped_lock lock{mutex};
        return std::exchange(outputs, {});
    }

    std::vector<WorkerProcessExit> reap_exited() override {
        std::scoped_lock lock{mutex};
        return std::exchange(exits, {});
    }

    WorkerTerminationResult terminate(const std::string_view job_id) override {
        std::scoped_lock lock{mutex};
        terminated_ids.emplace_back(job_id);
        if (termination_not_found.contains(std::string{job_id})) {
            return WorkerTerminationResult::not_found;
        }
        if (termination_already_exited.contains(std::string{job_id})) {
            return WorkerTerminationResult::already_exited;
        }
        return WorkerTerminationResult::requested;
    }

    std::mutex mutex;
    std::vector<biocore::application::WorkerLaunchRequest> launches;
    std::vector<WorkerProcessOutput> outputs;
    std::vector<WorkerProcessExit> exits;
    std::vector<std::string> terminated_ids;
    std::set<std::string> termination_not_found;
    std::set<std::string> termination_already_exited;
    std::atomic<bool> throw_poll_once{false};
    std::atomic<int> poll_calls{0};
    std::atomic<bool> block_poll{false};
    std::mutex block_mutex;
    std::condition_variable block_condition;
    bool poll_entered{false};
    bool release_poll{false};
};


class RecordingLifecycleSink final
    : public biocore::application::IWorkerLifecycleEventSink {
public:
    void publish(const WorkerLifecycleEvent& value) override {
        attempted_sequences.push_back(value.sequence);
        attempted_types.push_back(value.type);
        if (throw_on_sequence.has_value() && *throw_on_sequence == value.sequence) {
            throw std::runtime_error("Synthetic lifecycle broadcast failure");
        }
    }

    std::vector<std::uint64_t> attempted_sequences;
    std::vector<WorkerLifecycleEventType> attempted_types;
    std::optional<std::uint64_t> throw_on_sequence;
};

[[nodiscard]] Job queued_job(std::string id, std::string created = "2026-08-06T23:00:00Z") {
    return Job{
        std::move(id),
        std::string{"analysis"},
        std::string{"pipeline"},
        std::string{"1.0.0"},
        JobStatus::queued,
        JobPriority::normal,
        0.0,
        std::nullopt,
        created,
        created,
        std::nullopt,
        std::nullopt,
        0,
    };
}

[[nodiscard]] WorkerLifecycleEvent event(
    const WorkerLifecycleEventType type,
    std::string job_id,
    const std::int64_t revision,
    const std::uint64_t sequence
) {
    return WorkerLifecycleEvent{
        .type = type,
        .job_id = std::move(job_id),
        .launch_revision = revision,
        .sequence = sequence,
        .worker_timestamp_utc = "2026-08-07T00:00:00Z",
        .progress = std::nullopt,
        .active_step_id = std::nullopt,
        .log_level = std::nullopt,
        .component = std::nullopt,
        .message = std::nullopt,
        .artifact_step_id = std::nullopt,
        .artifact_output_port = std::nullopt,
        .artifact_plugin_id = std::nullopt,
        .artifact_plugin_version = std::nullopt,
        .artifact_module_id = std::nullopt,
        .artifact_file_type = std::nullopt,
        .artifact_relative_project_path = std::nullopt,
        .exit_code = std::nullopt,
    };
}

[[nodiscard]] bool policy_contract() {
    FakeJobRepository repository;
    UnusedIdGenerator ids;
    ConstantUtcClock utc;
    JobService jobs{repository, ids, utc};
    FakeWorkerSupervisor supervisor;
    PreparedJobTestStore prepared_jobs{"pipeline", "1.0.0"};
    JobScheduler scheduler{jobs, prepared_jobs, supervisor, 1U};
    FakeMonotonicClock monotonic;

    try {
        WorkerRuntime invalid{
            scheduler, jobs, supervisor, monotonic, WorkerRuntimePolicy{0ms, 10ms}
        };
        static_cast<void>(invalid);
        return false;
    } catch (const WorkerRuntimeError& error) {
        if (error.code() != WorkerRuntimeErrorCode::invalid_policy) return false;
    }

    try {
        WorkerRuntime invalid{
            scheduler, jobs, supervisor, monotonic, WorkerRuntimePolicy{20ms, 10ms}
        };
        static_cast<void>(invalid);
        return false;
    } catch (const WorkerRuntimeError& error) {
        if (error.code() != WorkerRuntimeErrorCode::invalid_policy) return false;
    }

    try {
        WorkerRuntime invalid{
            scheduler, jobs, supervisor, monotonic,
            WorkerRuntimePolicy{10ms, 20ms, 0U, 1U, 1U}
        };
        static_cast<void>(invalid);
        return false;
    } catch (const WorkerRuntimeError& error) {
        return error.code() == WorkerRuntimeErrorCode::invalid_policy;
    }
}

[[nodiscard]] bool lifecycle_and_exit_contract() {
    FakeJobRepository repository;
    repository.seed(queued_job("job-lifecycle"));
    UnusedIdGenerator ids;
    ConstantUtcClock utc;
    JobService jobs{repository, ids, utc};
    FakeWorkerSupervisor supervisor;
    PreparedJobTestStore prepared_jobs{"pipeline", "1.0.0"};
    JobScheduler scheduler{jobs, prepared_jobs, supervisor, 1U};
    FakeMonotonicClock monotonic;
    WorkerRuntime runtime{scheduler, jobs, supervisor, monotonic, {10ms, 5s}};

    const auto launch_cycle = runtime.run_cycle();
    if (launch_cycle.scheduler.launched_job_ids != std::vector<std::string>{"job-lifecycle"} ||
        runtime.active_session_count() != 1U || supervisor.launches.size() != 1U ||
        supervisor.launches.front().job_revision != 1) {
        return false;
    }

    auto ready = event(WorkerLifecycleEventType::ready, "job-lifecycle", 1, 1U);
    auto progress = event(WorkerLifecycleEventType::progress, "job-lifecycle", 1, 2U);
    progress.progress = 0.5;
    progress.active_step_id = "align";
    auto completed = event(WorkerLifecycleEventType::completed, "job-lifecycle", 1, 3U);
    completed.exit_code = 0;
    {
        std::scoped_lock lock{supervisor.mutex};
        supervisor.outputs.push_back(WorkerProcessOutput{
            .job_id = "job-lifecycle",
            .process_id = 42U,
            .events = {ready, progress},
            .diagnostics = {"diagnostic"},
            .protocol_issues = {},
        });
        supervisor.exits.push_back(WorkerProcessExit{
            .job_id = "job-lifecycle",
            .process_id = 42U,
            .exit_code = 0,
            .events = {completed},
            .diagnostics = {},
            .protocol_issues = {},
        });
    }

    const auto event_cycle = runtime.run_cycle();
    const auto stored = jobs.find_by_id("job-lifecycle");
    return stored.has_value() && stored->status() == JobStatus::completed &&
           stored->progress() == 1.0 && runtime.active_session_count() == 0U &&
           event_cycle.ingested_job_ids == std::vector<std::string>{"job-lifecycle", "job-lifecycle"} &&
           event_cycle.exited_job_ids == std::vector<std::string>{"job-lifecycle"} &&
           event_cycle.diagnostics == std::vector<std::string>{"diagnostic"} &&
           event_cycle.issues.empty();
}

[[nodiscard]] bool terminal_event_reserves_slot_until_native_exit_contract() {
    FakeJobRepository repository;
    repository.seed(queued_job("job-terminal-pending", "2026-08-06T22:00:00Z"));
    repository.seed(queued_job("job-after-terminal", "2026-08-06T23:00:00Z"));
    UnusedIdGenerator ids;
    ConstantUtcClock utc;
    JobService jobs{repository, ids, utc};
    FakeWorkerSupervisor supervisor;
    PreparedJobTestStore prepared_jobs{"pipeline", "1.0.0"};
    JobScheduler scheduler{jobs, prepared_jobs, supervisor, 1U};
    FakeMonotonicClock monotonic;
    WorkerRuntime runtime{scheduler, jobs, supervisor, monotonic, {10ms, 1s}};

    static_cast<void>(runtime.run_cycle());
    auto ready = event(WorkerLifecycleEventType::ready, "job-terminal-pending", 1, 1U);
    auto completed = event(WorkerLifecycleEventType::completed, "job-terminal-pending", 1, 2U);
    completed.exit_code = 0;
    {
        std::scoped_lock lock{supervisor.mutex};
        supervisor.outputs.push_back(WorkerProcessOutput{
            .job_id = "job-terminal-pending",
            .process_id = 70U,
            .events = {ready, completed},
            .diagnostics = {},
            .protocol_issues = {},
        });
    }
    const auto terminal_cycle = runtime.run_cycle();
    const auto completed_job = jobs.find_by_id("job-terminal-pending");
    if (!completed_job.has_value() || completed_job->status() != JobStatus::completed ||
        terminal_cycle.scheduler.reserved_slots != 1U ||
        !terminal_cycle.scheduler.launched_job_ids.empty()) {
        return false;
    }

    {
        std::scoped_lock lock{supervisor.mutex};
        supervisor.exits.push_back(WorkerProcessExit{
            .job_id = "job-terminal-pending",
            .process_id = 70U,
            .exit_code = 0,
            .events = {},
            .diagnostics = {},
            .protocol_issues = {},
        });
    }
    const auto exit_cycle = runtime.run_cycle();
    return exit_cycle.scheduler.reserved_slots == 0U &&
           exit_cycle.scheduler.launched_job_ids ==
               std::vector<std::string>{"job-after-terminal"};
}

[[nodiscard]] bool heartbeat_timeout_and_capacity_barrier_contract() {
    FakeJobRepository repository;
    repository.seed(queued_job("job-timeout", "2026-08-06T22:00:00Z"));
    repository.seed(queued_job("job-next", "2026-08-06T23:00:00Z"));
    UnusedIdGenerator ids;
    ConstantUtcClock utc;
    JobService jobs{repository, ids, utc};
    FakeWorkerSupervisor supervisor;
    PreparedJobTestStore prepared_jobs{"pipeline", "1.0.0"};
    JobScheduler scheduler{jobs, prepared_jobs, supervisor, 1U};
    FakeMonotonicClock monotonic;
    WorkerRuntime runtime{scheduler, jobs, supervisor, monotonic, {10ms, 100ms}};

    if (runtime.run_cycle().scheduler.launched_job_ids != std::vector<std::string>{"job-timeout"}) {
        return false;
    }
    {
        std::scoped_lock lock{supervisor.mutex};
        supervisor.outputs.push_back(WorkerProcessOutput{
            .job_id = "job-timeout",
            .process_id = 7U,
            .events = {event(WorkerLifecycleEventType::ready, "job-timeout", 1, 1U)},
            .diagnostics = {},
            .protocol_issues = {},
        });
    }
    monotonic.advance(50ms);
    if (!runtime.run_cycle().timed_out_job_ids.empty()) return false;

    monotonic.advance(101ms);
    const auto timeout_cycle = runtime.run_cycle();
    const auto timed_out = jobs.find_by_id("job-timeout");
    if (timeout_cycle.timed_out_job_ids != std::vector<std::string>{"job-timeout"} ||
        !timeout_cycle.scheduler.launched_job_ids.empty() ||
        !timed_out.has_value() || timed_out->status() != JobStatus::interrupted ||
        !timed_out->failure().has_value() ||
        timed_out->failure()->kind() != JobFailureKind::heartbeat_timeout ||
        timed_out->failure()->exit_code().has_value() ||
        supervisor.terminated_ids != std::vector<std::string>{"job-timeout"}) {
        return false;
    }

    const auto blocked_cycle = runtime.run_cycle();
    if (!blocked_cycle.scheduler.launched_job_ids.empty()) return false;

    {
        std::scoped_lock lock{supervisor.mutex};
        supervisor.exits.push_back(WorkerProcessExit{
            .job_id = "job-timeout",
            .process_id = 7U,
            .exit_code = 137,
            .events = {},
            .diagnostics = {},
            .protocol_issues = {},
        });
    }
    const auto reap_cycle = runtime.run_cycle();
    const auto next = jobs.find_by_id("job-next");
    return reap_cycle.exited_job_ids == std::vector<std::string>{"job-timeout"} &&
           reap_cycle.scheduler.launched_job_ids == std::vector<std::string>{"job-next"} &&
           runtime.active_session_count() == 1U && next.has_value() &&
           next->status() == JobStatus::preparing;
}

[[nodiscard]] bool activity_refresh_and_not_found_contract() {
    FakeJobRepository repository;
    repository.seed(queued_job("job-activity"));
    UnusedIdGenerator ids;
    ConstantUtcClock utc;
    JobService jobs{repository, ids, utc};
    FakeWorkerSupervisor supervisor;
    PreparedJobTestStore prepared_jobs{"pipeline", "1.0.0"};
    JobScheduler scheduler{jobs, prepared_jobs, supervisor, 1U};
    FakeMonotonicClock monotonic;
    WorkerRuntime runtime{scheduler, jobs, supervisor, monotonic, {10ms, 100ms}};

    static_cast<void>(runtime.run_cycle());
    monotonic.advance(90ms);
    auto ready = event(WorkerLifecycleEventType::ready, "job-activity", 1, 1U);
    {
        std::scoped_lock lock{supervisor.mutex};
        supervisor.outputs.push_back(WorkerProcessOutput{
            .job_id = "job-activity",
            .process_id = 8U,
            .events = {ready},
            .diagnostics = {},
            .protocol_issues = {},
        });
    }
    if (!runtime.run_cycle().timed_out_job_ids.empty()) return false;
    monotonic.advance(90ms);
    if (!runtime.run_cycle().timed_out_job_ids.empty()) return false;

    supervisor.termination_not_found.insert("job-activity");
    monotonic.advance(20ms);
    const auto missed = runtime.run_cycle();
    const auto stored = jobs.find_by_id("job-activity");
    return missed.timed_out_job_ids.empty() && !missed.issues.empty() &&
           missed.issues.front().stage == WorkerRuntimeIssueStage::termination &&
           stored.has_value() && stored->status() == JobStatus::running;
}

[[nodiscard]] bool log_spam_does_not_refresh_liveness_contract() {
    FakeJobRepository repository;
    repository.seed(queued_job("job-log-spam"));
    UnusedIdGenerator ids;
    ConstantUtcClock utc;
    JobService jobs{repository, ids, utc};
    FakeWorkerSupervisor supervisor;
    PreparedJobTestStore prepared_jobs{"pipeline", "1.0.0"};
    JobScheduler scheduler{jobs, prepared_jobs, supervisor, 1U};
    FakeMonotonicClock monotonic;
    WorkerRuntime runtime{scheduler, jobs, supervisor, monotonic, {10ms, 100ms}};

    static_cast<void>(runtime.run_cycle());
    {
        std::scoped_lock lock{supervisor.mutex};
        supervisor.outputs.push_back(WorkerProcessOutput{
            .job_id = "job-log-spam",
            .process_id = 81U,
            .events = {event(WorkerLifecycleEventType::ready, "job-log-spam", 1, 1U)},
            .diagnostics = {},
            .protocol_issues = {},
        });
    }
    static_cast<void>(runtime.run_cycle());

    monotonic.advance(90ms);
    auto log = event(WorkerLifecycleEventType::log, "job-log-spam", 1, 2U);
    log.log_level = WorkerLifecycleLogLevel::info;
    log.component = "noisy-plugin";
    log.message = "still noisy";
    {
        std::scoped_lock lock{supervisor.mutex};
        supervisor.outputs.push_back(WorkerProcessOutput{
            .job_id = "job-log-spam",
            .process_id = 81U,
            .events = {log},
            .diagnostics = {},
            .protocol_issues = {},
        });
    }
    if (!runtime.run_cycle().timed_out_job_ids.empty()) return false;

    monotonic.advance(11ms);
    const auto timed_out = runtime.run_cycle();
    const auto stored = jobs.find_by_id("job-log-spam");
    return timed_out.timed_out_job_ids == std::vector<std::string>{"job-log-spam"} &&
           stored.has_value() && stored->status() == JobStatus::interrupted &&
           stored->failure().has_value() &&
           stored->failure()->kind() == JobFailureKind::heartbeat_timeout &&
           supervisor.terminated_ids == std::vector<std::string>{"job-log-spam"};
}

[[nodiscard]] bool cancellation_contract() {
    FakeJobRepository repository;
    repository.seed(queued_job("job-cancel", "2026-08-06T22:00:00Z"));
    repository.seed(queued_job("job-after-cancel", "2026-08-06T23:00:00Z"));
    UnusedIdGenerator ids;
    ConstantUtcClock utc;
    JobService jobs{repository, ids, utc};
    FakeWorkerSupervisor supervisor;
    PreparedJobTestStore prepared_jobs{"pipeline", "1.0.0"};
    JobScheduler scheduler{jobs, prepared_jobs, supervisor, 1U};
    FakeMonotonicClock monotonic;
    WorkerRuntime runtime{scheduler, jobs, supervisor, monotonic, {10ms, 5s}};

    if (runtime.run_cycle().scheduler.launched_job_ids !=
        std::vector<std::string>{"job-cancel"}) {
        return false;
    }
    {
        std::scoped_lock lock{supervisor.mutex};
        supervisor.outputs.push_back(WorkerProcessOutput{
            .job_id = "job-cancel",
            .process_id = 88U,
            .events = {event(WorkerLifecycleEventType::ready, "job-cancel", 1, 1U)},
            .diagnostics = {},
            .protocol_issues = {},
        });
    }
    static_cast<void>(runtime.run_cycle());
    const auto running = jobs.find_by_id("job-cancel");
    if (!running.has_value() || running->status() != JobStatus::running) return false;
    static_cast<void>(jobs.transition(
        "job-cancel", JobStatus::cancelling, running->progress(), std::nullopt
    ));

    const auto request_cycle = runtime.run_cycle();
    if (!request_cycle.cancelled_job_ids.empty() ||
        supervisor.terminated_ids != std::vector<std::string>{"job-cancel"}) {
        return false;
    }
    const auto cancelling = jobs.find_by_id("job-cancel");
    if (!cancelling.has_value() || cancelling->status() != JobStatus::cancelling ||
        runtime.active_session_count() != 1U) {
        return false;
    }

    {
        std::scoped_lock lock{supervisor.mutex};
        supervisor.exits.push_back(WorkerProcessExit{
            .job_id = "job-cancel",
            .process_id = 88U,
            .exit_code = 137,
            .events = {},
            .diagnostics = {},
            .protocol_issues = {},
        });
    }
    const auto exit_cycle = runtime.run_cycle();
    const auto cancelled = jobs.find_by_id("job-cancel");
    const auto next = jobs.find_by_id("job-after-cancel");
    return exit_cycle.cancelled_job_ids == std::vector<std::string>{"job-cancel"} &&
           exit_cycle.exited_job_ids == std::vector<std::string>{"job-cancel"} &&
           exit_cycle.scheduler.launched_job_ids == std::vector<std::string>{"job-after-cancel"} &&
           cancelled.has_value() && cancelled->status() == JobStatus::cancelled &&
           next.has_value() && next->status() == JobStatus::preparing;
}

[[nodiscard]] bool bounded_background_retention_contract() {
    FakeJobRepository repository;
    UnusedIdGenerator ids;
    ConstantUtcClock utc;
    JobService jobs{repository, ids, utc};
    FakeWorkerSupervisor supervisor;
    {
        std::scoped_lock lock{supervisor.mutex};
        for (std::size_t index = 0U; index < 10U; ++index) {
            supervisor.outputs.push_back(WorkerProcessOutput{
                .job_id = "untracked-" + std::to_string(index),
                .process_id = 100U + index,
                .events = {event(
                    WorkerLifecycleEventType::ready,
                    "untracked-" + std::to_string(index),
                    1,
                    1U
                )},
                .diagnostics = {"diagnostic-" + std::to_string(index)},
                .protocol_issues = {{
                    "untracked-" + std::to_string(index),
                    "protocol-" + std::to_string(index)
                }},
            });
        }
    }
    PreparedJobTestStore prepared_jobs{"pipeline", "1.0.0"};
    JobScheduler scheduler{jobs, prepared_jobs, supervisor, 1U};
    FakeMonotonicClock monotonic;
    WorkerRuntime runtime{
        scheduler,
        jobs,
        supervisor,
        monotonic,
        WorkerRuntimePolicy{100ms, 1s, 3U, 3U, 3U}
    };

    runtime.start();
    std::this_thread::sleep_for(20ms);
    runtime.stop();

    const auto issues = runtime.drain_background_issues();
    const auto diagnostics = runtime.drain_background_diagnostics();
    const auto protocol = runtime.drain_background_protocol_issues();
    if (issues.size() != 4U || diagnostics.size() != 4U || protocol.size() != 4U) {
        return false;
    }
    if (issues.back().stage != WorkerRuntimeIssueStage::observation_retention ||
        issues.back().message.find("Dropped 7") == std::string::npos ||
        diagnostics.back().find("Dropped 7") == std::string::npos ||
        protocol.back().message.find("Dropped 7") == std::string::npos) {
        return false;
    }
    return diagnostics[0] == "diagnostic-7" && diagnostics[2] == "diagnostic-9" &&
           protocol[0].message == "protocol-7" && protocol[2].message == "protocol-9";
}

[[nodiscard]] bool already_exited_timeout_contract() {
    FakeJobRepository repository;
    repository.seed(queued_job("job-already-exited"));
    UnusedIdGenerator ids;
    ConstantUtcClock utc;
    JobService jobs{repository, ids, utc};
    FakeWorkerSupervisor supervisor;
    supervisor.termination_already_exited.insert("job-already-exited");
    PreparedJobTestStore prepared_jobs{"pipeline", "1.0.0"};
    JobScheduler scheduler{jobs, prepared_jobs, supervisor, 1U};
    FakeMonotonicClock monotonic;
    WorkerRuntime runtime{scheduler, jobs, supervisor, monotonic, {10ms, 50ms}};

    static_cast<void>(runtime.run_cycle());
    monotonic.advance(60ms);
    const auto timeout_check = runtime.run_cycle();
    const auto still_preparing = jobs.find_by_id("job-already-exited");
    if (!timeout_check.timed_out_job_ids.empty() || !still_preparing.has_value() ||
        still_preparing->status() != JobStatus::preparing) {
        return false;
    }

    {
        std::scoped_lock lock{supervisor.mutex};
        supervisor.exits.push_back(WorkerProcessExit{
            .job_id = "job-already-exited",
            .process_id = 91U,
            .exit_code = 0,
            .events = {},
            .diagnostics = {},
            .protocol_issues = {},
        });
    }
    const auto exit_cycle = runtime.run_cycle();
    const auto interrupted = jobs.find_by_id("job-already-exited");
    return exit_cycle.exited_job_ids == std::vector<std::string>{"job-already-exited"} &&
           interrupted.has_value() && interrupted->status() == JobStatus::interrupted &&
           interrupted->failure().has_value() &&
           interrupted->failure()->kind() == JobFailureKind::process_exit_without_terminal &&
           interrupted->failure()->exit_code() == std::optional<std::int64_t>{0};
}

[[nodiscard]] bool concurrent_cycle_guard_contract() {
    FakeJobRepository repository;
    UnusedIdGenerator ids;
    ConstantUtcClock utc;
    JobService jobs{repository, ids, utc};
    FakeWorkerSupervisor supervisor;
    supervisor.block_poll.store(true, std::memory_order_release);
    PreparedJobTestStore prepared_jobs{"pipeline", "1.0.0"};
    JobScheduler scheduler{jobs, prepared_jobs, supervisor, 1U};
    FakeMonotonicClock monotonic;
    WorkerRuntime runtime{scheduler, jobs, supervisor, monotonic, {10ms, 100ms}};

    std::thread first_cycle{[&runtime] { static_cast<void>(runtime.run_cycle()); }};
    {
        std::unique_lock lock{supervisor.block_mutex};
        supervisor.block_condition.wait(lock, [&supervisor] {
            return supervisor.poll_entered;
        });
    }

    bool rejected = false;
    try {
        static_cast<void>(runtime.run_cycle());
    } catch (const WorkerRuntimeError& error) {
        rejected = error.code() == WorkerRuntimeErrorCode::cycle_already_in_progress;
    }

    {
        std::scoped_lock lock{supervisor.block_mutex};
        supervisor.release_poll = true;
    }
    supervisor.block_condition.notify_all();
    first_cycle.join();
    return rejected && runtime.run_cycle().issues.empty();
}


[[nodiscard]] bool lifecycle_broadcast_contract() {
    {
        FakeJobRepository repository;
        repository.seed(queued_job("job-broadcast-filter"));
        UnusedIdGenerator ids;
        ConstantUtcClock utc;
        JobService jobs{repository, ids, utc};
        FakeWorkerSupervisor supervisor;
        PreparedJobTestStore prepared_jobs{"pipeline", "1.0.0"};
        JobScheduler scheduler{jobs, prepared_jobs, supervisor, 1U};
        FakeMonotonicClock monotonic;
        RecordingLifecycleSink sink;
        WorkerRuntime runtime{
            scheduler, jobs, supervisor, monotonic, {10ms, 5s},
            nullptr, nullptr, &sink
        };

        const auto launch = runtime.run_cycle();
        if (launch.scheduler.launched_job_ids !=
                std::vector<std::string>{"job-broadcast-filter"}) {
            return false;
        }

        auto ready = event(
            WorkerLifecycleEventType::ready, "job-broadcast-filter", 1, 1U
        );
        auto duplicate = event(
            WorkerLifecycleEventType::ready, "job-broadcast-filter", 1, 1U
        );
        auto progress = event(
            WorkerLifecycleEventType::progress, "job-broadcast-filter", 1, 2U
        );
        progress.progress = 0.25;
        progress.active_step_id = "align";
        {
            std::scoped_lock lock{supervisor.mutex};
            supervisor.outputs.push_back(WorkerProcessOutput{
                .job_id = "job-broadcast-filter",
                .process_id = 41U,
                .events = {ready, duplicate, progress},
                .diagnostics = {},
                .protocol_issues = {},
            });
        }

        const auto cycle = runtime.run_cycle();
        if (sink.attempted_sequences != std::vector<std::uint64_t>{1U, 2U}) {
            return false;
        }
        std::size_t rejected = 0U;
        for (const auto& item : cycle.issues) {
            if (item.stage == WorkerRuntimeIssueStage::event_ingestion) {
                ++rejected;
            }
        }
        if (rejected != 1U) {
            return false;
        }
    }

    {
        FakeJobRepository repository;
        repository.seed(queued_job("job-broadcast-failure"));
        UnusedIdGenerator ids;
        ConstantUtcClock utc;
        JobService jobs{repository, ids, utc};
        FakeWorkerSupervisor supervisor;
        PreparedJobTestStore prepared_jobs{"pipeline", "1.0.0"};
        JobScheduler scheduler{jobs, prepared_jobs, supervisor, 1U};
        FakeMonotonicClock monotonic;
        RecordingLifecycleSink sink;
        sink.throw_on_sequence = 1U;
        WorkerRuntime runtime{
            scheduler, jobs, supervisor, monotonic, {10ms, 5s},
            nullptr, nullptr, &sink
        };

        static_cast<void>(runtime.run_cycle());
        auto ready = event(
            WorkerLifecycleEventType::ready, "job-broadcast-failure", 1, 1U
        );
        auto progress = event(
            WorkerLifecycleEventType::progress, "job-broadcast-failure", 1, 2U
        );
        progress.progress = 0.25;
        progress.active_step_id = "align";
        {
            std::scoped_lock lock{supervisor.mutex};
            supervisor.outputs.push_back(WorkerProcessOutput{
                .job_id = "job-broadcast-failure",
                .process_id = 42U,
                .events = {ready, progress},
                .diagnostics = {},
                .protocol_issues = {},
            });
        }

        const auto cycle = runtime.run_cycle();
        if (sink.attempted_sequences != std::vector<std::uint64_t>{1U, 2U}) {
            return false;
        }
        bool broadcast_failure_seen = false;
        for (const auto& item : cycle.issues) {
            if (item.stage == WorkerRuntimeIssueStage::event_broadcast &&
                item.message == "Synthetic lifecycle broadcast failure") {
                broadcast_failure_seen = true;
            }
        }
        if (!broadcast_failure_seen) {
            return false;
        }
        const auto stored = repository.find_by_id("job-broadcast-failure");
        if (!stored.has_value() || stored->status() != JobStatus::running ||
            stored->progress() != 0.25 ||
            stored->active_step_id() != std::optional<std::string>{"align"}) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool background_loop_contract() {
    FakeJobRepository repository;
    UnusedIdGenerator ids;
    ConstantUtcClock utc;
    JobService jobs{repository, ids, utc};
    FakeWorkerSupervisor supervisor;
    supervisor.throw_poll_once.store(true, std::memory_order_release);
    {
        std::scoped_lock lock{supervisor.mutex};
        supervisor.outputs.push_back(WorkerProcessOutput{
            .job_id = "untracked-background",
            .process_id = 99U,
            .events = {},
            .diagnostics = {"background diagnostic"},
            .protocol_issues = {{"untracked-background", "background protocol issue"}},
        });
    }
    PreparedJobTestStore prepared_jobs{"pipeline", "1.0.0"};
    JobScheduler scheduler{jobs, prepared_jobs, supervisor, 1U};
    FakeMonotonicClock monotonic;
    WorkerRuntime runtime{scheduler, jobs, supervisor, monotonic, {5ms, 20ms}};

    runtime.start();
    if (!runtime.running()) return false;
    try {
        runtime.start();
        return false;
    } catch (const WorkerRuntimeError& error) {
        if (error.code() != WorkerRuntimeErrorCode::already_running) return false;
    }
    std::this_thread::sleep_for(35ms);
    runtime.stop();
    runtime.stop();
    const auto issues = runtime.drain_background_issues();
    const auto diagnostics = runtime.drain_background_diagnostics();
    const auto protocol_issues = runtime.drain_background_protocol_issues();
    return !runtime.running() && supervisor.poll_calls.load(std::memory_order_acquire) >= 2 &&
           diagnostics == std::vector<std::string>{"background diagnostic"} &&
           protocol_issues.size() == 1U &&
           protocol_issues.front().message == "background protocol issue" &&
           runtime.drain_background_diagnostics().empty() &&
           runtime.drain_background_protocol_issues().empty() &&
           std::ranges::any_of(issues, [](const auto& item) {
               return item.stage == WorkerRuntimeIssueStage::output_poll &&
                      item.message == "Synthetic poll failure";
           });
}

}  // namespace

int main() {
    const bool passed = policy_contract() && lifecycle_and_exit_contract() &&
                        terminal_event_reserves_slot_until_native_exit_contract() &&
                        heartbeat_timeout_and_capacity_barrier_contract() &&
                        activity_refresh_and_not_found_contract() &&
                        log_spam_does_not_refresh_liveness_contract() &&
                        cancellation_contract() &&
                        bounded_background_retention_contract() &&
                        already_exited_timeout_contract() &&
                        concurrent_cycle_guard_contract() &&
                        lifecycle_broadcast_contract() &&
                        background_loop_contract();
    if (!passed) {
        std::cerr << "Worker runtime tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Worker runtime tests passed\n";
    return EXIT_SUCCESS;
}
