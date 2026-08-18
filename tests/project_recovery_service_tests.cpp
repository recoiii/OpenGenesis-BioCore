#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_job_repository.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_partial_output_cleaner.hpp"
#include "biocore/application/i_quarantine_retention_store.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/output_artifact_cleanup_service.hpp"
#include "biocore/application/project_recovery_service.hpp"
#include "biocore/domain/job.hpp"

namespace {
using namespace biocore;

class JobRepo final : public application::IJobRepository {
public:
    bool add(const domain::Job& job) override {
        return jobs.emplace(std::string{job.id()}, job).second;
    }
    std::optional<domain::Job> find_by_id(const std::string_view id) override {
        const auto it = jobs.find(std::string{id});
        if (it == jobs.end()) return std::nullopt;
        return it->second;
    }
    std::vector<domain::Job> list() override {
        std::vector<domain::Job> values;
        for (const auto& [id, job] : jobs) {
            static_cast<void>(id);
            values.push_back(job);
        }
        return values;
    }
    bool update_runtime_state(const domain::Job& job, const std::int64_t expected) override {
        auto it = jobs.find(std::string{job.id()});
        if (it == jobs.end() || it->second.revision() != expected) return false;
        it->second = job;
        return true;
    }
    std::map<std::string, domain::Job> jobs;
};

class ManagedRepo final : public application::IManagedFileRepository {
public:
    bool add(const domain::ManagedFile&) override { return true; }
    std::optional<domain::ManagedFile> find_by_id(std::string_view) override { return std::nullopt; }
    std::optional<domain::ManagedFile> find_by_relative_project_path(std::string_view) override {
        return std::nullopt;
    }
    std::vector<domain::ManagedFile> list() override { return {}; }
    bool add_generated_output(
        const domain::ManagedFile&, const application::GeneratedOutputProvenance&
    ) override { return true; }
    bool add_generated_outputs_batch(
        std::span<const application::GeneratedOutputArtifact>
    ) override { return true; }
    std::optional<application::GeneratedOutputArtifact> find_generated_output(
        std::string_view, std::string_view, std::string_view
    ) override { return std::nullopt; }
    std::vector<application::GeneratedOutputArtifact> list_generated_outputs(
        const std::string_view job_id
    ) override {
        listed_jobs.push_back(std::string{job_id});
        return {};
    }
    std::optional<double> latest_generated_output_progress(const std::string_view job_id) override {
        if (throw_checkpoint_for == job_id) throw std::runtime_error("checkpoint unavailable");
        const auto it = checkpoints.find(std::string{job_id});
        return it == checkpoints.end() ? std::nullopt : std::optional<double>{it->second};
    }

    std::map<std::string, double> checkpoints;
    std::string throw_checkpoint_for;
    std::vector<std::string> listed_jobs;
};

class Cleaner final : public application::IPartialOutputCleaner {
public:
    application::PartialOutputCleanupResult quarantine_unregistered_outputs(
        const std::string_view job_id, std::span<const std::string> protected_paths
    ) override {
        static_cast<void>(protected_paths);
        if (throw_for == job_id) throw std::runtime_error("cleanup failed");
        calls.push_back(std::string{job_id});
        return {};
    }
    std::string throw_for;
    std::vector<std::string> calls;
};

class Retention final : public application::IQuarantineRetentionStore {
public:
    application::QuarantineRetentionResult purge_expired(const std::chrono::seconds age) override {
        observed_age = age;
        if (throw_failure) throw std::runtime_error("retention failed");
        return {.purged_relative_paths = {"old.partial"}, .skipped_relative_paths = {}};
    }
    bool throw_failure{false};
    std::chrono::seconds observed_age{0};
};

class IDs final : public application::IIdGenerator {
public:
    std::string generate() override { return "unused"; }
};

class Clock final : public application::IUtcClock {
public:
    std::string now_utc_iso8601() override { return "2026-08-07T10:30:00Z"; }
};

[[nodiscard]] domain::Job job(
    std::string id, const domain::JobStatus status, const double progress, const std::int64_t revision
) {
    const bool started = status == domain::JobStatus::preparing || status == domain::JobStatus::running ||
                         status == domain::JobStatus::paused || status == domain::JobStatus::cancelling ||
                         status == domain::JobStatus::interrupted;
    const bool interrupted = status == domain::JobStatus::interrupted;
    return domain::Job{
        std::move(id), std::nullopt, std::string{"pipeline"}, std::string{"1"}, status,
        domain::JobPriority::normal, progress,
        (status == domain::JobStatus::running && !interrupted) ? std::optional<std::string>{"scan"} : std::nullopt,
        "2026-08-07T09:00:00Z", "2026-08-07T09:10:00Z",
        started ? std::optional<std::string>{"2026-08-07T09:05:00Z"} : std::nullopt,
        std::nullopt,
        revision,
    };
}

[[nodiscard]] bool recovers_checkpoint_and_is_idempotent() {
    JobRepo jobs_repo;
    jobs_repo.add(job("running", domain::JobStatus::running, 0.2, 4));
    jobs_repo.add(job("queued", domain::JobStatus::queued, 0.0, 1));
    jobs_repo.add(job("old-interrupted", domain::JobStatus::interrupted, 0.4, 3));
    ManagedRepo files;
    files.checkpoints["running"] = 0.65;
    Cleaner cleaner;
    Retention retention;
    IDs ids;
    Clock clock;
    application::JobService jobs{jobs_repo, ids, clock};
    application::OutputArtifactCleanupService cleanup{files, cleaner};
    application::ProjectRecoveryService recovery{jobs, files, &cleanup, &retention};

    const auto first = recovery.recover();
    const auto running = jobs_repo.find_by_id("running");
    const auto queued = jobs_repo.find_by_id("queued");
    if (first.recovered_jobs.size() != 1U || !running.has_value() ||
        running->status() != domain::JobStatus::interrupted || running->progress() != 0.65 ||
        !queued.has_value() || queued->status() != domain::JobStatus::queued ||
        cleaner.calls != std::vector<std::string>({"old-interrupted", "running"}) ||
        !first.retention.has_value() || first.retention->purged_relative_paths.size() != 1U ||
        retention.observed_age != std::chrono::hours{24 * 30} || !first.issues.empty()) {
        return false;
    }

    cleaner.calls.clear();
    const auto second = recovery.recover();
    return second.recovered_jobs.empty() &&
           cleaner.calls == std::vector<std::string>({"old-interrupted", "running"}) &&
           jobs_repo.find_by_id("running")->revision() == running->revision();
}

[[nodiscard]] bool checkpoint_failure_isolated() {
    JobRepo jobs_repo;
    jobs_repo.add(job("bad", domain::JobStatus::running, 0.25, 2));
    jobs_repo.add(job("good", domain::JobStatus::running, 0.3, 2));
    ManagedRepo files;
    files.throw_checkpoint_for = "bad";
    files.checkpoints["good"] = 0.5;
    Cleaner cleaner;
    Retention retention;
    IDs ids;
    Clock clock;
    application::JobService jobs{jobs_repo, ids, clock};
    application::OutputArtifactCleanupService cleanup{files, cleaner};
    application::ProjectRecoveryService recovery{jobs, files, &cleanup, &retention};

    const auto result = recovery.recover();
    return result.recovered_jobs.size() == 1U && result.recovered_jobs.front().job_id == "good" &&
           jobs_repo.find_by_id("bad")->status() == domain::JobStatus::running &&
           jobs_repo.find_by_id("good")->status() == domain::JobStatus::interrupted &&
           result.issues.size() == 1U &&
           result.issues.front().stage == application::ProjectRecoveryIssueStage::checkpoint_read;
}

[[nodiscard]] bool policy_validation() {
    JobRepo jobs_repo;
    ManagedRepo files;
    IDs ids;
    Clock clock;
    application::JobService jobs{jobs_repo, ids, clock};
    try {
        application::ProjectRecoveryService invalid{
            jobs, files, nullptr, nullptr,
            application::ProjectRecoveryPolicy{
                .cleanup_interrupted_jobs = false,
                .purge_expired_quarantine = true,
                .quarantine_retention = std::chrono::seconds{0},
            }
        };
        static_cast<void>(invalid);
        return false;
    } catch (const std::invalid_argument&) {
        return true;
    }
}

}  // namespace

int main() {
    if (!recovers_checkpoint_and_is_idempotent() || !checkpoint_failure_isolated() ||
        !policy_validation()) {
        std::cerr << "Project recovery service tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
