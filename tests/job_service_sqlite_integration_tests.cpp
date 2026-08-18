#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/job_status.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_job_repository.hpp"

namespace {

using biocore::application::CreateJobRequest;
using biocore::application::IIdGenerator;
using biocore::application::IUtcClock;
using biocore::application::JobService;
using biocore::domain::JobPriority;
using biocore::domain::JobStatus;
using biocore::infrastructure::sqlite::ProjectMigrationRunner;
using biocore::infrastructure::sqlite::SqliteConnection;
using biocore::infrastructure::sqlite::SqliteJobRepository;

class SequenceIdGenerator final : public IIdGenerator {
public:
    std::string generate() override {
        return "job-integration-1";
    }
};

class SequenceClock final : public IUtcClock {
public:
    explicit SequenceClock(std::deque<std::string> values) : values_{std::move(values)} {}

    std::string now_utc_iso8601() override {
        if (values_.empty()) {
            throw std::runtime_error("Integration clock exhausted");
        }
        std::string value = std::move(values_.front());
        values_.pop_front();
        return value;
    }

private:
    std::deque<std::string> values_;
};

class TemporaryDatabase final {
public:
    TemporaryDatabase() {
        const auto unique_value = std::chrono::steady_clock::now().time_since_epoch().count();
        directory_ = std::filesystem::temp_directory_path() /
                     ("biocore-job-service-integration-" + std::to_string(unique_value));
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

[[nodiscard]] bool service_and_sqlite_compose() {
    TemporaryDatabase temporary;
    {
        SqliteConnection connection{temporary.path()};
        ProjectMigrationRunner migrations{connection};
        migrations.apply_pending();
        SqliteJobRepository repository{connection};
        SequenceIdGenerator ids;
        SequenceClock clock{{
            "2026-08-06T20:30:00Z",
            "2026-08-06T20:31:00Z",
            "2026-08-06T20:32:00Z",
            "2026-08-06T20:33:00Z",
            "2026-08-06T20:34:00Z",
        }};
        JobService service{repository, ids, clock};

        const auto created = service.create(CreateJobRequest{
            .analysis_id = std::string{"analysis-integration"},
            .pipeline_id = std::string{"org.biocore.integration"},
            .pipeline_version = std::string{"0.1.0"},
            .priority = JobPriority::high,
        });
        if (created.id() != "job-integration-1" || created.status() != JobStatus::draft) {
            return false;
        }

        static_cast<void>(service.transition(created.id(), JobStatus::queued, 0.0, std::nullopt));
        static_cast<void>(service.transition(
            created.id(), JobStatus::preparing, 0.1, std::string{"validate"}
        ));
        static_cast<void>(service.transition(
            created.id(), JobStatus::running, 0.75, std::string{"scan"}
        ));
        const auto completed = service.transition(
            created.id(), JobStatus::completed, 1.0, std::string{"ignored"}
        );
        if (completed.revision() != 4 || !completed.finished_at_utc().has_value()) {
            return false;
        }
    }

    {
        SqliteConnection connection{temporary.path()};
        ProjectMigrationRunner migrations{connection};
        migrations.apply_pending();
        SqliteJobRepository repository{connection};
        const auto stored = repository.find_by_id("job-integration-1");
        return stored.has_value() && stored->status() == JobStatus::completed &&
               stored->progress() == 1.0 && stored->priority() == JobPriority::high &&
               stored->revision() == 4 && stored->analysis_id().has_value() &&
               *stored->analysis_id() == "analysis-integration" &&
               stored->pipeline_id().has_value() &&
               *stored->pipeline_id() == "org.biocore.integration" &&
               stored->started_at_utc().has_value() &&
               *stored->started_at_utc() == "2026-08-06T20:32:00Z" &&
               stored->finished_at_utc().has_value() &&
               *stored->finished_at_utc() == "2026-08-06T20:34:00Z";
    }
}

}  // namespace

int main() {
    if (!service_and_sqlite_compose()) {
        std::cerr << "JobService/SQLite integration contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
