#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/i_worker_supervisor.hpp"
#include "biocore/application/job_scheduler.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/worker_launch_request.hpp"
#include "biocore/domain/job.hpp"
#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/job_status.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_job_repository.hpp"

#include "prepared_job_store_test_support.hpp"

namespace {

using biocore::application::IIdGenerator;
using biocore::application::IUtcClock;
using biocore::application::IWorkerSupervisor;
using biocore::application::JobScheduler;
using biocore::application::JobService;
using biocore::application::WorkerLaunchRequest;
using biocore::domain::Job;
using biocore::domain::JobPriority;
using biocore::domain::JobStatus;
using biocore::infrastructure::sqlite::ProjectMigrationRunner;
using biocore::infrastructure::sqlite::SqliteConnection;
using biocore::infrastructure::sqlite::SqliteJobRepository;

class UnusedIdGenerator final : public IIdGenerator {
public:
    std::string generate() override {
        throw std::runtime_error("Integration scheduler must not generate identifiers");
    }
};

class SequenceClock final : public IUtcClock {
public:
    explicit SequenceClock(std::deque<std::string> values) : values_{std::move(values)} {}

    std::string now_utc_iso8601() override {
        if (values_.empty()) {
            throw std::runtime_error("Integration scheduler clock exhausted");
        }
        std::string value = std::move(values_.front());
        values_.pop_front();
        return value;
    }

private:
    std::deque<std::string> values_;
};

class RecordingWorkerSupervisor final : public IWorkerSupervisor {
public:
    void launch(const WorkerLaunchRequest& request) override {
        requests.push_back(request);
    }

    std::vector<WorkerLaunchRequest> requests;
};

class TemporaryDatabase final {
public:
    TemporaryDatabase() {
        const auto unique_value = std::chrono::steady_clock::now().time_since_epoch().count();
        directory_ = std::filesystem::temp_directory_path() /
                     ("biocore-job-scheduler-integration-" + std::to_string(unique_value));
        std::filesystem::create_directory(directory_);
        path_ = directory_ / "project.sqlite";
    }

    ~TemporaryDatabase() {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path directory_;
    std::filesystem::path path_;
};

[[nodiscard]] Job queued_job(
    std::string id,
    const JobPriority priority,
    std::string created_at
) {
    return Job{
        std::move(id),
        std::string{"analysis-integration"},
        std::string{"org.biocore.scheduler"},
        std::string{"0.1.0"},
        JobStatus::queued,
        priority,
        0.0,
        std::nullopt,
        created_at,
        created_at,
        std::nullopt,
        std::nullopt,
        0,
    };
}

[[nodiscard]] Job running_job(std::string id, std::string timestamp) {
    return Job{
        std::move(id),
        std::nullopt,
        std::string{"org.biocore.scheduler"},
        std::string{"0.1.0"},
        JobStatus::running,
        JobPriority::normal,
        0.5,
        std::string{"step-1"},
        timestamp,
        timestamp,
        timestamp,
        std::nullopt,
        0,
    };
}

[[nodiscard]] bool scheduler_and_sqlite_compose() {
    TemporaryDatabase temporary;
    {
        SqliteConnection connection{temporary.path()};
        ProjectMigrationRunner migrations{connection};
        migrations.apply_pending();
        SqliteJobRepository repository{connection};
        if (!repository.add(queued_job("job-low", JobPriority::low, "2026-08-06T19:00:00Z")) ||
            !repository.add(queued_job("job-high", JobPriority::high, "2026-08-06T19:10:00Z")) ||
            !repository.add(queued_job("job-normal", JobPriority::normal, "2026-08-06T19:05:00Z")) ||
            !repository.add(running_job("job-active", "2026-08-06T18:00:00Z"))) {
            return false;
        }

        UnusedIdGenerator ids;
        SequenceClock clock{{"2026-08-06T22:00:00Z"}};
        JobService service{repository, ids, clock};
        RecordingWorkerSupervisor supervisor;
        PreparedJobTestStore prepared_jobs{"org.biocore.scheduler", "0.1.0"};
        JobScheduler scheduler{service, prepared_jobs, supervisor, 2U};
        const auto result = scheduler.tick();
        if (result.active_jobs_before != 1U || result.available_slots != 1U ||
            result.launched_job_ids != std::vector<std::string>{"job-high"} ||
            supervisor.requests.size() != 1U || supervisor.requests.front().job_id != "job-high") {
            return false;
        }
    }

    {
        SqliteConnection connection{temporary.path()};
        ProjectMigrationRunner migrations{connection};
        migrations.apply_pending();
        SqliteJobRepository repository{connection};
        const auto high = repository.find_by_id("job-high");
        const auto normal = repository.find_by_id("job-normal");
        const auto low = repository.find_by_id("job-low");
        return high.has_value() && high->status() == JobStatus::preparing &&
               high->revision() == 1 && high->started_at_utc().has_value() &&
               *high->started_at_utc() == "2026-08-06T22:00:00Z" &&
               normal.has_value() && normal->status() == JobStatus::queued &&
               low.has_value() && low->status() == JobStatus::queued;
    }
}

}  // namespace

int main() {
    if (!scheduler_and_sqlite_compose()) {
        std::cerr << "JobScheduler/SQLite integration contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
