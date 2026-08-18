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
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_output_artifact_inspector.hpp"
#include "biocore/application/i_partial_output_cleaner.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/output_artifact_cleanup_service.hpp"
#include "biocore/application/output_artifact_service.hpp"
#include "biocore/application/output_artifact_service_error.hpp"
#include "biocore/application/worker_event_ingestion_error.hpp"
#include "biocore/application/worker_event_ingestion_session.hpp"

namespace {
using namespace biocore;

class JobRepo final : public application::IJobRepository {
public:
    bool add(const domain::Job& job) override { stored = job; return true; }
    std::optional<domain::Job> find_by_id(std::string_view id) override {
        return stored.has_value() && stored->id() == id ? stored : std::nullopt;
    }
    std::vector<domain::Job> list() override {
        return stored.has_value() ? std::vector<domain::Job>{*stored} : std::vector<domain::Job>{};
    }
    bool update_runtime_state(const domain::Job& job, std::int64_t expected_revision) override {
        if (!stored.has_value() || stored->revision() != expected_revision) return false;
        stored = job;
        return true;
    }
    std::optional<domain::Job> stored;
};

class ManagedRepo final : public application::IManagedFileRepository {
public:
    bool add(const domain::ManagedFile& file) override { files.push_back(file); return true; }
    std::optional<domain::ManagedFile> find_by_id(std::string_view id) override {
        for (const auto& file : files) if (file.id() == id) return file;
        return std::nullopt;
    }
    std::optional<domain::ManagedFile> find_by_relative_project_path(std::string_view path) override {
        for (const auto& file : files) {
            if (file.relative_project_path().has_value() && *file.relative_project_path() == path) return file;
        }
        return std::nullopt;
    }
    std::vector<domain::ManagedFile> list() override { return files; }
    bool add_generated_output(
        const domain::ManagedFile& file,
        const application::GeneratedOutputProvenance& provenance
    ) override {
        const application::GeneratedOutputArtifact artifact{file, provenance};
        return add_generated_outputs_batch(std::span{&artifact, 1U});
    }
    bool add_generated_outputs_batch(
        std::span<const application::GeneratedOutputArtifact> batch
    ) override {
        ++batch_calls;
        if (batch.empty() || force_batch_failure) return false;
        for (const auto& artifact : batch) {
            files.push_back(artifact.file);
            artifacts.push_back(artifact);
        }
        return true;
    }
    std::optional<application::GeneratedOutputArtifact> find_generated_output(
        std::string_view job, std::string_view step, std::string_view port
    ) override {
        for (const auto& artifact : artifacts) {
            if (artifact.provenance.job_id == job && artifact.provenance.step_id == step &&
                artifact.provenance.output_port == port) return artifact;
        }
        return std::nullopt;
    }
    std::vector<application::GeneratedOutputArtifact> list_generated_outputs(std::string_view job) override {
        std::vector<application::GeneratedOutputArtifact> result;
        for (const auto& artifact : artifacts) if (artifact.provenance.job_id == job) result.push_back(artifact);
        return result;
    }

    int batch_calls{0};
    bool force_batch_failure{false};
    std::vector<domain::ManagedFile> files;
    std::vector<application::GeneratedOutputArtifact> artifacts;
};

class Cleaner final : public application::IPartialOutputCleaner {
public:
    application::PartialOutputCleanupResult quarantine_unregistered_outputs(
        const std::string_view job_id,
        const std::span<const std::string> protected_relative_paths
    ) override {
        ++calls;
        last_job_id = std::string{job_id};
        protected_count = protected_relative_paths.size();
        return application::PartialOutputCleanupResult{
            .quarantined = {application::QuarantinedPartialOutput{
                .original_relative_path = "outputs/job-1--bad--partial.out",
                .quarantine_relative_path =
                    ".biocore/quarantine/outputs/job-1/job-1--bad--partial.out.partial",
            }},
            .skipped_relative_paths = {},
        };
    }

    int calls{0};
    std::string last_job_id;
    std::size_t protected_count{0U};
};

class IDs final : public application::IIdGenerator {
public:
    explicit IDs(std::deque<std::string> values) : values_{std::move(values)} {}
    std::string generate() override {
        if (values_.empty()) throw std::runtime_error("id exhausted");
        auto value = std::move(values_.front());
        values_.pop_front();
        return value;
    }
private:
    std::deque<std::string> values_;
};

class Clock final : public application::IUtcClock {
public:
    explicit Clock(std::string value) : value_{std::move(value)} {}
    std::string now_utc_iso8601() override { return value_; }
private:
    std::string value_;
};

class Inspector final : public application::IOutputArtifactInspector {
public:
    application::InspectedOutputArtifact inspect_existing_output(std::string_view relative) override {
        const auto slash = relative.find_last_of('/');
        return {
            .display_name = std::string{relative.substr(slash + 1U)},
            .managed_path = "/project/" + std::string{relative},
            .relative_project_path = std::string{relative},
            .size_bytes = 7,
            .modified_at_utc = std::nullopt,
            .checksum_algorithm = "sha256",
            .checksum_value = std::string(64U, 'a'),
        };
    }
};

[[nodiscard]] domain::Job preparing_job() {
    return domain::Job{
        "job-1", std::nullopt, std::string{"pipeline"}, std::string{"1"},
        domain::JobStatus::preparing, domain::JobPriority::normal, 0.0, std::nullopt,
        "created", "updated", std::string{"started"}, std::nullopt, 3,
    };
}

[[nodiscard]] application::WorkerLifecycleEvent base(
    application::WorkerLifecycleEventType type,
    std::uint64_t sequence
) {
    return {
        .type = type,
        .job_id = "job-1",
        .launch_revision = 3,
        .sequence = sequence,
        .worker_timestamp_utc = "worker-time",
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

[[nodiscard]] application::WorkerLifecycleEvent artifact(
    std::uint64_t sequence,
    std::string port,
    std::string path,
    std::string step = "copy"
) {
    auto value = base(application::WorkerLifecycleEventType::artifact, sequence);
    value.artifact_step_id = std::move(step);
    value.artifact_output_port = std::move(port);
    value.artifact_plugin_id = "org.biocore.demo";
    value.artifact_plugin_version = "0.1.0";
    value.artifact_module_id = "org.biocore.demo.multi";
    value.artifact_file_type = "txt";
    value.artifact_relative_project_path = std::move(path);
    return value;
}

[[nodiscard]] bool batch_commits_at_progress_boundary() {
    JobRepo jobs_repo;
    jobs_repo.stored = preparing_job();
    ManagedRepo files;
    IDs job_ids{{"unused"}};
    IDs artifact_ids{{"artifact-1", "artifact-2"}};
    Clock job_clock{"job-time"};
    Clock artifact_clock{"artifact-time"};
    Inspector inspector;
    application::JobService jobs{jobs_repo, job_ids, job_clock};
    application::OutputArtifactService artifact_service{files, inspector, artifact_ids, artifact_clock};
    application::WorkerEventIngestionSession session{jobs, "job-1", 3, &artifact_service};

    static_cast<void>(session.ingest(base(application::WorkerLifecycleEventType::ready, 1U)));
    const auto first = session.ingest(artifact(
        2U, "left", "outputs/job-1--copy--left.out"
    ));
    const auto second = session.ingest(artifact(
        3U, "right", "outputs/job-1--copy--right.out"
    ));
    if (first.action != application::WorkerEventIngestionAction::artifact_buffered ||
        second.action != application::WorkerEventIngestionAction::artifact_buffered ||
        files.batch_calls != 0 || !files.artifacts.empty() || session.pending_artifact_count() != 2U) {
        return false;
    }

    auto progress = base(application::WorkerLifecycleEventType::progress, 4U);
    progress.progress = 0.5;
    progress.active_step_id = "copy";
    const auto committed = session.ingest(progress);
    return committed.action == application::WorkerEventIngestionAction::progress_persisted &&
           committed.registered_artifacts.size() == 2U && files.batch_calls == 1 &&
           files.artifacts.size() == 2U && session.pending_artifact_count() == 0U &&
           committed.persisted_job.has_value() && committed.persisted_job->progress() == 0.5;
}

[[nodiscard]] bool batch_failure_does_not_persist_progress() {
    JobRepo jobs_repo;
    jobs_repo.stored = preparing_job();
    ManagedRepo files;
    files.force_batch_failure = true;
    IDs job_ids{{"unused"}};
    IDs artifact_ids{{
        "a1", "a2", "b1", "b2", "c1", "c2", "d1", "d2",
        "e1", "e2", "f1", "f2", "g1", "g2", "h1", "h2",
    }};
    Clock job_clock{"job-time"};
    Clock artifact_clock{"artifact-time"};
    Inspector inspector;
    application::JobService jobs{jobs_repo, job_ids, job_clock};
    application::OutputArtifactService artifact_service{files, inspector, artifact_ids, artifact_clock};
    application::WorkerEventIngestionSession session{jobs, "job-1", 3, &artifact_service};

    static_cast<void>(session.ingest(base(application::WorkerLifecycleEventType::ready, 1U)));
    static_cast<void>(session.ingest(artifact(
        2U, "left", "outputs/job-1--copy--left.out"
    )));
    static_cast<void>(session.ingest(artifact(
        3U, "right", "outputs/job-1--copy--right.out"
    )));

    auto progress = base(application::WorkerLifecycleEventType::progress, 4U);
    progress.progress = 0.5;
    progress.active_step_id = "copy";
    try {
        static_cast<void>(session.ingest(progress));
        return false;
    } catch (const application::OutputArtifactServiceError& error) {
        const auto current = jobs_repo.stored;
        return error.code() ==
                   application::OutputArtifactServiceErrorCode::identifier_generation_exhausted &&
               current.has_value() && current->status() == domain::JobStatus::running &&
               current->progress() == 0.0 && files.artifacts.empty() && files.batch_calls == 8 &&
               session.last_sequence() == 3U && session.pending_artifact_count() == 2U;
    }
}

[[nodiscard]] bool interrupted_exit_invokes_cleanup() {
    JobRepo jobs_repo;
    jobs_repo.stored = preparing_job();
    ManagedRepo files;
    IDs job_ids{{"unused"}};
    Clock job_clock{"job-time"};
    Cleaner cleaner;
    application::JobService jobs{jobs_repo, job_ids, job_clock};
    application::OutputArtifactCleanupService cleanup{files, cleaner};
    application::WorkerEventIngestionSession session{
        jobs, "job-1", 3, nullptr, &cleanup
    };

    static_cast<void>(session.ingest(base(application::WorkerLifecycleEventType::ready, 1U)));
    const auto finalized = session.finalize_process_exit(137);
    return !finalized.matched_terminal_event && finalized.persisted_job.has_value() &&
           finalized.persisted_job->status() == domain::JobStatus::interrupted &&
           finalized.cleanup_result.has_value() &&
           finalized.cleanup_result->quarantined.size() == 1U &&
           !finalized.cleanup_error.has_value() && cleaner.calls == 1 &&
           cleaner.last_job_id == "job-1" && cleaner.protected_count == 0U;
}

[[nodiscard]] bool interleaved_steps_are_rejected() {
    JobRepo jobs_repo;
    jobs_repo.stored = preparing_job();
    ManagedRepo files;
    IDs job_ids{{"unused"}};
    IDs artifact_ids{{"artifact-1", "artifact-2"}};
    Clock job_clock{"job-time"};
    Clock artifact_clock{"artifact-time"};
    Inspector inspector;
    application::JobService jobs{jobs_repo, job_ids, job_clock};
    application::OutputArtifactService artifact_service{files, inspector, artifact_ids, artifact_clock};
    application::WorkerEventIngestionSession session{jobs, "job-1", 3, &artifact_service};

    static_cast<void>(session.ingest(base(application::WorkerLifecycleEventType::ready, 1U)));
    static_cast<void>(session.ingest(artifact(
        2U, "left", "outputs/job-1--copy--left.out"
    )));
    try {
        static_cast<void>(session.ingest(artifact(
            3U, "other", "outputs/job-1--next--other.out", "next"
        )));
        return false;
    } catch (const application::WorkerEventIngestionError& error) {
        return error.code() == application::WorkerEventIngestionErrorCode::lifecycle_violation &&
               session.last_sequence() == 2U && session.pending_artifact_count() == 1U &&
               files.batch_calls == 0;
    }
}

}  // namespace

int main() {
    if (!batch_commits_at_progress_boundary() ||
        !batch_failure_does_not_persist_progress() ||
        !interrupted_exit_invokes_cleanup() ||
        !interleaved_steps_are_rejected()) {
        std::cerr << "Worker artifact batch ingestion tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
