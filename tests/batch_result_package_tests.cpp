#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/batch_result_package_error.hpp"
#include "biocore/application/batch_result_package_service.hpp"
#include "biocore/application/batch_results_service.hpp"
#include "biocore/application/i_artifact_content_access.hpp"
#include "biocore/application/i_batch_execution_store.hpp"
#include "biocore/application/i_batch_plan_store.hpp"
#include "biocore/application/i_job_repository.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_result_artifact_reader.hpp"
#include "biocore/application/i_utc_clock.hpp"

namespace {
using namespace biocore;

class Plans final : public application::IBatchPlanStore {
public:
    bool add(const application::ApprovedBatchPlan& value) override { plan = value; return true; }
    std::optional<application::ApprovedBatchPlan> find(std::string_view id) override {
        return plan.has_value() && plan->plan_id == id ? plan : std::nullopt;
    }
    std::vector<application::ApprovedBatchPlan> list() override {
        return plan.has_value() ? std::vector<application::ApprovedBatchPlan>{*plan}
                                : std::vector<application::ApprovedBatchPlan>{};
    }
    std::optional<application::ApprovedBatchPlan> plan;
};

class Executions final : public application::IBatchExecutionStore {
public:
    application::AddBatchExecutionResult add(
        const application::BatchExecutionRecord& value,
        std::span<const application::BatchPreparedJob>
    ) override { record = value; return application::AddBatchExecutionResult::created; }
    std::optional<application::BatchExecutionRecord> find(std::string_view id) override {
        return record.has_value() && record->plan_id == id ? record : std::nullopt;
    }
    std::vector<application::BatchExecutionRecord> list() override {
        return record.has_value() ? std::vector<application::BatchExecutionRecord>{*record}
                                  : std::vector<application::BatchExecutionRecord>{};
    }
    std::vector<application::BatchExecutionAttemptRecord> list_attempts(std::string_view id) override {
        std::vector<application::BatchExecutionAttemptRecord> out;
        for (const auto& attempt : attempts) if (attempt.plan_id == id) out.push_back(attempt);
        return out;
    }
    application::AddBatchAttemptResult add_attempt(const application::BatchPreparedAttempt& item) override {
        attempts.push_back(item.attempt);
        return application::AddBatchAttemptResult::created;
    }
    bool request_cancellation(std::string_view, std::string_view) override { return true; }
    std::optional<application::BatchSchedulingQuota> quota_for_job(std::string_view) override {
        return std::nullopt;
    }
    std::optional<application::BatchExecutionRecord> record;
    std::vector<application::BatchExecutionAttemptRecord> attempts;
};

class Jobs final : public application::IJobRepository {
public:
    bool add(const domain::Job& job) override { values.insert_or_assign(std::string{job.id()}, job); return true; }
    std::optional<domain::Job> find_by_id(std::string_view id) override {
        const auto found = values.find(std::string{id});
        return found == values.end() ? std::nullopt : std::optional<domain::Job>{found->second};
    }
    std::vector<domain::Job> list() override {
        std::vector<domain::Job> out;
        for (const auto& [id, job] : values) { static_cast<void>(id); out.push_back(job); }
        return out;
    }
    bool update_runtime_state(const domain::Job& job, std::int64_t) override {
        values.insert_or_assign(std::string{job.id()}, job); return true;
    }
    std::map<std::string, domain::Job, std::less<>> values;
};

class Files final : public application::IManagedFileRepository {
public:
    bool add(const domain::ManagedFile& file) override { values.insert_or_assign(std::string{file.id()}, file); return true; }
    std::optional<domain::ManagedFile> find_by_id(std::string_view id) override {
        const auto found = values.find(std::string{id});
        return found == values.end() ? std::nullopt : std::optional<domain::ManagedFile>{found->second};
    }
    std::optional<domain::ManagedFile> find_by_relative_project_path(std::string_view path) override {
        for (const auto& [id, file] : values) {
            static_cast<void>(id);
            if (file.relative_project_path() == std::optional<std::string>{std::string{path}}) return file;
        }
        return std::nullopt;
    }
    std::vector<domain::ManagedFile> list() override {
        std::vector<domain::ManagedFile> out;
        for (const auto& [id, file] : values) { static_cast<void>(id); out.push_back(file); }
        return out;
    }
    bool add_generated_output(
        const domain::ManagedFile& file,
        const application::GeneratedOutputProvenance& provenance
    ) override {
        const application::GeneratedOutputArtifact artifact{file, provenance};
        return add_generated_outputs_batch(std::span{&artifact, 1U});
    }
    bool add_generated_outputs_batch(std::span<const application::GeneratedOutputArtifact> batch) override {
        for (const auto& artifact : batch) {
            values.insert_or_assign(std::string{artifact.file.id()}, artifact.file);
            artifacts.push_back(artifact);
        }
        return true;
    }
    std::optional<application::GeneratedOutputArtifact> find_generated_output(
        std::string_view job, std::string_view step, std::string_view port
    ) override {
        if (missing_on_find) return std::nullopt;
        for (const auto& artifact : artifacts) {
            if (artifact.provenance.job_id == job && artifact.provenance.step_id == step &&
                artifact.provenance.output_port == port) {
                if (mutate_on_find) {
                    auto changed = artifact;
                    changed.provenance.plugin_version = "9.9.9";
                    return changed;
                }
                return artifact;
            }
        }
        return std::nullopt;
    }
    std::vector<application::GeneratedOutputArtifact> list_generated_outputs(std::string_view job) override {
        std::vector<application::GeneratedOutputArtifact> out;
        for (const auto& artifact : artifacts) if (artifact.provenance.job_id == job) out.push_back(artifact);
        return out;
    }
    bool mutate_on_find{false};
    bool missing_on_find{false};
    std::map<std::string, domain::ManagedFile, std::less<>> values;
    std::vector<application::GeneratedOutputArtifact> artifacts;
};

class Reader final : public application::IResultArtifactReader {
public:
    application::ResultArtifactText read_verified_text(
        const application::GeneratedOutputArtifact&,
        std::size_t
    ) override {
        throw std::runtime_error{"Non-QC package test must not parse artifact payload"};
    }
};

class Content final : public application::IArtifactContentAccess {
public:
    application::ArtifactContentVerification verify_for_download(
        const application::GeneratedOutputArtifact& artifact
    ) override {
        ++calls;
        if (status != application::ArtifactContentStatus::verified) {
            return {.status = status, .content_path = std::nullopt,
                    .computed_sha256 = std::nullopt, .actual_size_bytes = 0};
        }
        return {
            .status = status,
            .content_path = std::string{"/secret/project/"} + artifact.provenance.relative_project_path,
            .computed_sha256 = std::string(64U, 'a'),
            .actual_size_bytes = artifact.file.size_bytes(),
        };
    }
    application::ArtifactContentStatus status{application::ArtifactContentStatus::verified};
    int calls{0};
};

class Clock final : public application::IUtcClock {
public:
    std::string now_utc_iso8601() override { return "2026-09-28T10:00:00Z"; }
};

[[nodiscard]] domain::Job completed_job(std::string id) {
    return domain::Job{
        std::move(id), std::nullopt, std::string{"org.biocore.batch.workflow"},
        std::string{"1.0.0"}, domain::JobStatus::completed, domain::JobPriority::normal,
        1.0, std::nullopt, "created", "finished", std::string{"started"},
        std::string{"finished"}, 3, std::nullopt, 1
    };
}

[[nodiscard]] application::ApprovedBatchPlan plan() {
    return {
        .plan_id = "plan-1", .project_id = "project-1", .template_id = "template-1",
        .template_version = "1.0.0", .approved_at_utc = "approved",
        .samples = {{
            .sample_id = "sample-a", .disposition = application::BatchPlanSampleDisposition::included,
            .workflow_id = std::string{"workflow"},
            .nodes = {{
                .node_id = "copy", .module_id = "org.biocore.demo.copy",
                .plugin_version = "1.0.0", .parameters = {}, .inputs = {},
                .outputs = {{"result", "txt"}},
            }},
        }},
    };
}

[[nodiscard]] application::GeneratedOutputArtifact artifact() {
    const std::string path = "outputs/job-a--copy--result.out";
    domain::ManagedFile file{
        "file-a", "result", domain::StorageMode::generated_output, std::nullopt,
        "/secret/project/" + path, path, "txt", 7, std::nullopt, std::string{"sha256"},
        std::string(64U, 'a'), "created", "updated"
    };
    return {
        file,
        application::GeneratedOutputProvenance{
            .job_id = "job-a", .step_id = "copy", .output_port = "result",
            .plugin_id = "org.biocore.demo", .plugin_version = "1.0.0",
            .module_id = "org.biocore.demo.copy", .file_type = "txt",
            .relative_project_path = path, .step_progress = 1.0,
            .registered_at_utc = "registered",
        }
    };
}

void seed(Plans& plans, Executions& executions, Jobs& jobs, Files& files) {
    plans.plan = plan();
    executions.record = application::BatchExecutionRecord{
        .plan_id = "plan-1", .maximum_concurrent_jobs = 1U, .cancellation_requested = false,
        .submitted_at_utc = "submitted", .updated_at_utc = "updated",
        .jobs = {{"sample-a", 0U, "job-a"}},
    };
    executions.attempts = {{
        .plan_id = "plan-1", .sample_id = "sample-a", .attempt_number = 1,
        .job_id = "job-a", .parent_job_id = std::nullopt,
        .mode = application::BatchAttemptMode::initial,
        .created_at_utc = "created", .execution_node_ids = {"copy"},
    }};
    jobs.values.emplace("job-a", completed_job("job-a"));
    const auto generated = artifact();
    files.values.emplace(std::string{generated.file.id()}, generated.file);
    files.artifacts.push_back(generated);
}

[[nodiscard]] bool verified_snapshot() {
    Plans plans; Executions executions; Jobs jobs; Files files; Reader reader; Content content; Clock clock;
    seed(plans, executions, jobs, files);
    application::BatchResultsService results{plans, executions, jobs, files, reader};
    application::BatchResultPackageService packages{results, files, content, clock};
    const auto value = packages.build("plan-1");
    return value.schema_version == 1U && value.stable_snapshot && value.complete_results &&
           value.overview.plan_id == "plan-1" && value.verified_artifacts.size() == 1U &&
           value.verified_artifacts.front().verified_sha256 == std::string(64U, 'a') &&
           value.generated_at_utc == "2026-09-28T10:00:00Z" && content.calls == 1;
}


[[nodiscard]] bool disappeared_artifact_rejected() {
    Plans plans; Executions executions; Jobs jobs; Files files; Reader reader; Content content; Clock clock;
    seed(plans, executions, jobs, files);
    files.missing_on_find = true;
    application::BatchResultsService results{plans, executions, jobs, files, reader};
    application::BatchResultPackageService packages{results, files, content, clock};
    try {
        static_cast<void>(packages.build("plan-1"));
    } catch (const application::BatchResultPackageError& error) {
        return error.code() == application::BatchResultPackageErrorCode::artifact_missing &&
               content.calls == 0;
    }
    return false;
}

[[nodiscard]] bool identity_change_rejected() {
    Plans plans; Executions executions; Jobs jobs; Files files; Reader reader; Content content; Clock clock;
    seed(plans, executions, jobs, files);
    files.mutate_on_find = true;
    application::BatchResultsService results{plans, executions, jobs, files, reader};
    application::BatchResultPackageService packages{results, files, content, clock};
    try {
        static_cast<void>(packages.build("plan-1"));
    } catch (const application::BatchResultPackageError& error) {
        return error.code() == application::BatchResultPackageErrorCode::artifact_identity_mismatch &&
               content.calls == 0;
    }
    return false;
}

[[nodiscard]] bool integrity_failure_rejected() {
    Plans plans; Executions executions; Jobs jobs; Files files; Reader reader; Content content; Clock clock;
    seed(plans, executions, jobs, files);
    content.status = application::ArtifactContentStatus::checksum_mismatch;
    application::BatchResultsService results{plans, executions, jobs, files, reader};
    application::BatchResultPackageService packages{results, files, content, clock};
    try {
        static_cast<void>(packages.build("plan-1"));
    } catch (const application::BatchResultPackageError& error) {
        return error.code() == application::BatchResultPackageErrorCode::artifact_integrity_error &&
               content.calls == 1;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    const std::string_view mode = argv[1];
    const bool ok = mode == "verified" ? verified_snapshot()
                  : mode == "missing" ? disappeared_artifact_rejected()
                  : mode == "identity" ? identity_change_rejected()
                  : mode == "integrity" ? integrity_failure_rejected()
                  : false;
    if (!ok) {
        std::cerr << "Batch result package test failed: " << mode << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
