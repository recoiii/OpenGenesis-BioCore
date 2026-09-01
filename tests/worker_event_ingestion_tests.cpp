#include <cstdlib>
#include <deque>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_job_repository.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/worker_event_ingestion_error.hpp"
#include "biocore/application/worker_event_ingestion_session.hpp"
#include "biocore/application/worker_lifecycle_event.hpp"
#include "biocore/domain/job.hpp"

namespace {

using namespace biocore::application;
using biocore::domain::Job;
using biocore::domain::JobPriority;
using biocore::domain::JobStatus;

class UnusedIdGenerator final : public IIdGenerator {
public:
    std::string generate() override { throw std::runtime_error("unused"); }
};

class FakeClock final : public IUtcClock {
public:
    explicit FakeClock(std::deque<std::string> values) : values_{std::move(values)} {}
    std::string now_utc_iso8601() override {
        ++calls;
        if (values_.empty()) throw std::runtime_error("clock exhausted");
        std::string value = std::move(values_.front());
        values_.pop_front();
        return value;
    }
    int calls{0};
private:
    std::deque<std::string> values_;
};

class FakeJobRepository final : public IJobRepository {
public:
    bool add(const Job& job) override {
        if (stored.has_value()) return false;
        stored = job;
        return true;
    }
    std::optional<Job> find_by_id(const std::string_view id) override {
        if (stored.has_value() && stored->id() == id) return stored;
        return std::nullopt;
    }
    std::vector<Job> list() override {
        return stored.has_value() ? std::vector<Job>{*stored} : std::vector<Job>{};
    }
    bool update_runtime_state(const Job& job, const std::int64_t expected_revision) override {
        ++updates;
        if (force_concurrent || !stored.has_value() || stored->revision() != expected_revision) {
            return false;
        }
        stored = job;
        return true;
    }
    std::optional<Job> stored;
    bool force_concurrent{false};
    int updates{0};
};

[[nodiscard]] Job preparing_job() {
    return Job{
        "job-1", std::nullopt, std::string{"pipeline"}, std::string{"1.0"},
        JobStatus::preparing, JobPriority::normal, 0.0, std::string{"prepare"},
        "2026-08-06T20:00:00Z", "2026-08-06T20:01:00Z",
        std::string{"2026-08-06T20:01:00Z"}, std::nullopt, 3,
    };
}

[[nodiscard]] WorkerLifecycleEvent event(
    const WorkerLifecycleEventType type,
    const std::uint64_t sequence
) {
    return WorkerLifecycleEvent{
        .type = type,
        .job_id = "job-1",
        .launch_revision = 3,
        .sequence = sequence,
        .worker_timestamp_utc = "2026-08-06T20:02:00Z",
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

[[nodiscard]] bool lifecycle_contract() {
    FakeJobRepository repository;
    repository.stored = preparing_job();
    UnusedIdGenerator ids;
    FakeClock clock{{
        "2026-08-06T20:03:00Z", "2026-08-06T20:04:00Z", "2026-08-06T20:05:00Z"
    }};
    JobService jobs{repository, ids, clock};
    WorkerEventIngestionSession session{jobs, "job-1", 3};

    const auto ready = session.ingest(event(WorkerLifecycleEventType::ready, 1U));
    if (ready.action != WorkerEventIngestionAction::job_started ||
        !ready.persisted_job.has_value() || ready.persisted_job->status() != JobStatus::running) {
        return false;
    }

    const auto heartbeat = session.ingest(event(WorkerLifecycleEventType::heartbeat, 2U));
    if (heartbeat.action != WorkerEventIngestionAction::heartbeat_observed ||
        heartbeat.persisted_job.has_value() || clock.calls != 1) {
        return false;
    }

    auto progress = event(WorkerLifecycleEventType::progress, 3U);
    progress.progress = 0.4;
    progress.active_step_id = "align";
    const auto progress_result = session.ingest(progress);
    if (!progress_result.persisted_job.has_value() ||
        progress_result.persisted_job->progress() != 0.4 ||
        progress_result.persisted_job->active_step_id() != std::optional<std::string>{"align"}) {
        return false;
    }

    auto log = event(WorkerLifecycleEventType::log, 4U);
    log.log_level = WorkerLifecycleLogLevel::info;
    log.component = "worker";
    log.message = "processing";
    const auto log_result = session.ingest(log);
    if (log_result.action != WorkerEventIngestionAction::log_observed || clock.calls != 2) {
        return false;
    }

    auto completed = event(WorkerLifecycleEventType::completed, 5U);
    completed.exit_code = 0;
    const auto completed_result = session.ingest(completed);
    return completed_result.persisted_job.has_value() &&
           completed_result.persisted_job->status() == JobStatus::completed &&
           completed_result.persisted_job->progress() == 1.0 && session.last_sequence() == 5U &&
           session.ready_received() && session.terminal_received() && clock.calls == 3 &&
           session.finalize_process_exit(0).matched_terminal_event;
}

[[nodiscard]] bool failure_before_ready_contract() {
    FakeJobRepository repository;
    repository.stored = preparing_job();
    UnusedIdGenerator ids;
    FakeClock clock{{"2026-08-06T20:03:00Z"}};
    JobService jobs{repository, ids, clock};
    WorkerEventIngestionSession session{jobs, "job-1", 3};

    auto failed = event(WorkerLifecycleEventType::failed, 1U);
    failed.exit_code = 17;
    failed.message = "startup failed";
    const auto result = session.ingest(failed);
    return result.action == WorkerEventIngestionAction::job_failed &&
           result.persisted_job.has_value() && result.persisted_job->status() == JobStatus::failed &&
           result.persisted_job->failure().has_value() &&
           result.persisted_job->failure()->kind() ==
               biocore::domain::JobFailureKind::worker_reported_failure &&
           result.persisted_job->failure()->message() == "startup failed" &&
           result.persisted_job->failure()->exit_code() == std::optional<std::int64_t>{17} &&
           result.persisted_job->failure()->worker_timestamp_utc() ==
               std::optional<std::string>{"2026-08-06T20:02:00Z"} &&
           !session.ready_received() && session.terminal_received();
}

[[nodiscard]] bool missing_terminal_finalization_contract() {
    FakeJobRepository repository;
    repository.stored = preparing_job();
    UnusedIdGenerator ids;
    FakeClock clock{{"ready-time", "interrupt-time"}};
    JobService jobs{repository, ids, clock};
    WorkerEventIngestionSession session{jobs, "job-1", 3};

    static_cast<void>(session.ingest(event(WorkerLifecycleEventType::ready, 1U)));
    const auto finalized = session.finalize_process_exit(19);
    if (finalized.matched_terminal_event || !finalized.persisted_job.has_value() ||
        finalized.persisted_job->status() != JobStatus::interrupted ||
        !finalized.persisted_job->failure().has_value() ||
        finalized.persisted_job->failure()->kind() !=
            biocore::domain::JobFailureKind::process_exit_without_terminal ||
        finalized.persisted_job->failure()->exit_code() != std::optional<std::int64_t>{19} ||
        !session.terminal_received()) {
        return false;
    }
    try {
        static_cast<void>(session.ingest(event(WorkerLifecycleEventType::heartbeat, 2U)));
        return false;
    } catch (const WorkerEventIngestionError& error) {
        return error.code() == WorkerEventIngestionErrorCode::lifecycle_violation;
    }
}

[[nodiscard]] bool terminal_exit_mismatch_contract() {
    FakeJobRepository repository;
    repository.stored = preparing_job();
    UnusedIdGenerator ids;
    FakeClock clock{{"ready-time", "complete-time"}};
    JobService jobs{repository, ids, clock};
    WorkerEventIngestionSession session{jobs, "job-1", 3};
    static_cast<void>(session.ingest(event(WorkerLifecycleEventType::ready, 1U)));
    auto completed = event(WorkerLifecycleEventType::completed, 2U);
    completed.exit_code = 0;
    static_cast<void>(session.ingest(completed));
    try {
        static_cast<void>(session.finalize_process_exit(7));
        return false;
    } catch (const WorkerEventIngestionError& error) {
        if (error.code() != WorkerEventIngestionErrorCode::lifecycle_violation) return false;
    }
    if (!session.finalize_process_exit(0).matched_terminal_event) return false;
    try {
        static_cast<void>(session.finalize_process_exit(0));
        return false;
    } catch (const WorkerEventIngestionError& error) {
        return error.code() == WorkerEventIngestionErrorCode::lifecycle_violation;
    }
}

[[nodiscard]] bool rejection_contract() {
    FakeJobRepository repository;
    repository.stored = preparing_job();
    UnusedIdGenerator ids;
    FakeClock clock{{"t1", "t2", "t3"}};
    JobService jobs{repository, ids, clock};
    WorkerEventIngestionSession session{jobs, "job-1", 3};

    auto wrong = event(WorkerLifecycleEventType::ready, 1U);
    wrong.launch_revision = 4;
    try {
        static_cast<void>(session.ingest(wrong));
        return false;
    } catch (const WorkerEventIngestionError& error) {
        if (error.code() != WorkerEventIngestionErrorCode::identity_mismatch ||
            session.last_sequence() != 0U) return false;
    }

    auto out_of_order = event(WorkerLifecycleEventType::ready, 2U);
    try {
        static_cast<void>(session.ingest(out_of_order));
        return false;
    } catch (const WorkerEventIngestionError& error) {
        if (error.code() != WorkerEventIngestionErrorCode::sequence_mismatch) return false;
    }

    static_cast<void>(session.ingest(event(WorkerLifecycleEventType::ready, 1U)));
    auto stale = event(WorkerLifecycleEventType::progress, 2U);
    stale.progress = 0.5;
    static_cast<void>(session.ingest(stale));
    auto decreasing = event(WorkerLifecycleEventType::progress, 3U);
    decreasing.progress = 0.4;
    try {
        static_cast<void>(session.ingest(decreasing));
        return false;
    } catch (const WorkerEventIngestionError& error) {
        return error.code() == WorkerEventIngestionErrorCode::lifecycle_violation &&
               session.last_sequence() == 2U;
    }
}

[[nodiscard]] bool invalid_shape_contract() {
    FakeJobRepository repository;
    repository.stored = preparing_job();
    UnusedIdGenerator ids;
    FakeClock clock{{"unused"}};
    JobService jobs{repository, ids, clock};
    WorkerEventIngestionSession session{jobs, "job-1", 3};

    auto malformed_ready = event(WorkerLifecycleEventType::ready, 1U);
    malformed_ready.progress = 0.1;
    try {
        static_cast<void>(session.ingest(malformed_ready));
        return false;
    } catch (const WorkerEventIngestionError& error) {
        if (error.code() != WorkerEventIngestionErrorCode::invalid_event ||
            session.last_sequence() != 0U || clock.calls != 0) {
            return false;
        }
    }

    auto oversized_log = event(WorkerLifecycleEventType::log, 1U);
    oversized_log.log_level = WorkerLifecycleLogLevel::info;
    oversized_log.component = "worker";
    oversized_log.message = std::string(16U * 1024U + 1U, 'x');
    try {
        static_cast<void>(session.ingest(oversized_log));
        return false;
    } catch (const WorkerEventIngestionError& error) {
        return error.code() == WorkerEventIngestionErrorCode::invalid_event &&
               session.last_sequence() == 0U;
    }
}

[[nodiscard]] bool concurrency_retry_contract() {
    FakeJobRepository repository;
    repository.stored = preparing_job();
    repository.force_concurrent = true;
    UnusedIdGenerator ids;
    FakeClock clock{{"t1", "t2"}};
    JobService jobs{repository, ids, clock};
    WorkerEventIngestionSession session{jobs, "job-1", 3};

    try {
        static_cast<void>(session.ingest(event(WorkerLifecycleEventType::ready, 1U)));
        return false;
    } catch (const WorkerEventIngestionError& error) {
        if (error.code() != WorkerEventIngestionErrorCode::concurrent_update ||
            session.last_sequence() != 0U || session.ready_received()) return false;
    }
    repository.force_concurrent = false;
    const auto result = session.ingest(event(WorkerLifecycleEventType::ready, 1U));
    return result.action == WorkerEventIngestionAction::job_started && session.last_sequence() == 1U;
}

}  // namespace

int main() {
    if (!lifecycle_contract()) {
        std::cerr << "Worker event ingestion lifecycle contract failed\n";
        return EXIT_FAILURE;
    }
    if (!failure_before_ready_contract()) {
        std::cerr << "Worker event pre-ready failure contract failed\n";
        return EXIT_FAILURE;
    }
    if (!missing_terminal_finalization_contract()) {
        std::cerr << "Worker missing-terminal finalization contract failed\n";
        return EXIT_FAILURE;
    }
    if (!terminal_exit_mismatch_contract()) {
        std::cerr << "Worker terminal/exit consistency contract failed\n";
        return EXIT_FAILURE;
    }
    if (!rejection_contract()) {
        std::cerr << "Worker event rejection contract failed\n";
        return EXIT_FAILURE;
    }
    if (!invalid_shape_contract()) {
        std::cerr << "Worker event shape validation contract failed\n";
        return EXIT_FAILURE;
    }
    if (!concurrency_retry_contract()) {
        std::cerr << "Worker event concurrency retry contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
