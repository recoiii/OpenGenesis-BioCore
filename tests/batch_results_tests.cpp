#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/batch_results_service.hpp"
#include "biocore/application/i_batch_execution_store.hpp"
#include "biocore/application/i_batch_plan_store.hpp"
#include "biocore/application/i_job_repository.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_result_artifact_reader.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/reference_genome.hpp"

namespace {
using namespace biocore;

class PlanStore final : public application::IBatchPlanStore {
public:
    bool add(const application::ApprovedBatchPlan& value) override {
        plan = value;
        return true;
    }
    std::optional<application::ApprovedBatchPlan> find(std::string_view id) override {
        return plan.has_value() && plan->plan_id == id ? plan : std::nullopt;
    }
    std::optional<application::ApprovedBatchPlan> plan;
};

class ExecutionStore final : public application::IBatchExecutionStore {
public:
    application::AddBatchExecutionResult add(
        const application::BatchExecutionRecord& value,
        std::span<const application::BatchPreparedJob>
    ) override {
        record = value;
        return application::AddBatchExecutionResult::created;
    }
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
    application::AddBatchAttemptResult add_attempt(
        const application::BatchPreparedAttempt& item
    ) override {
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
    bool add(const domain::Job& job) override {
        return values.emplace(std::string{job.id()}, job).second;
    }
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
        values.insert_or_assign(std::string{job.id()}, job);
        return true;
    }
    std::map<std::string, domain::Job, std::less<>> values;
};

class ManagedFiles final : public application::IManagedFileRepository {
public:
    bool add(const domain::ManagedFile& file) override {
        files.insert_or_assign(std::string{file.id()}, file);
        return true;
    }
    std::optional<domain::ManagedFile> find_by_id(std::string_view id) override {
        const auto found = files.find(std::string{id});
        return found == files.end() ? std::nullopt : std::optional<domain::ManagedFile>{found->second};
    }
    std::optional<domain::ManagedFile> find_by_relative_project_path(std::string_view path) override {
        for (const auto& [id, file] : files) {
            static_cast<void>(id);
            if (file.relative_project_path() == std::optional<std::string>{std::string{path}}) return file;
        }
        return std::nullopt;
    }
    std::vector<domain::ManagedFile> list() override {
        std::vector<domain::ManagedFile> out;
        for (const auto& [id, file] : files) { static_cast<void>(id); out.push_back(file); }
        return out;
    }
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
        for (const auto& artifact : batch) {
            files.insert_or_assign(std::string{artifact.file.id()}, artifact.file);
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
        std::vector<application::GeneratedOutputArtifact> out;
        for (const auto& artifact : artifacts) if (artifact.provenance.job_id == job) out.push_back(artifact);
        return out;
    }
    std::map<std::string, domain::ManagedFile, std::less<>> files;
    std::vector<application::GeneratedOutputArtifact> artifacts;
};

class Reader final : public application::IResultArtifactReader {
public:
    application::ResultArtifactText read_verified_text(
        const application::GeneratedOutputArtifact& artifact,
        std::size_t maximum_bytes
    ) override {
        const auto found = texts.find(std::string{artifact.file.id()});
        if (found == texts.end()) return {application::ResultArtifactReadStatus::missing, std::nullopt, std::nullopt};
        if (found->second.size() > maximum_bytes) {
            return {application::ResultArtifactReadStatus::too_large, std::nullopt, std::nullopt};
        }
        return {
            application::ResultArtifactReadStatus::verified,
            found->second,
            std::string(64U, 'a')
        };
    }
    std::map<std::string, std::string, std::less<>> texts;
};

[[nodiscard]] domain::Job completed_job(std::string id, std::int64_t attempt = 1) {
    return domain::Job{
        std::move(id), std::nullopt, std::string{"org.biocore.batch.workflow"},
        std::string{"1.0.0"}, domain::JobStatus::completed, domain::JobPriority::normal,
        1.0, std::nullopt, "created", "finished", std::string{"started"},
        std::string{"finished"}, 3, std::nullopt, attempt
    };
}

[[nodiscard]] domain::ManagedFile output_file(
    std::string id,
    std::string type,
    std::string path,
    std::int64_t size
) {
    return domain::ManagedFile{
        std::move(id), "output", domain::StorageMode::generated_output,
        std::nullopt, std::string{"/project/"} + path, path, std::move(type), size,
        std::nullopt, std::string{"sha256"}, std::string(64U, 'a'), "created", "updated"
    };
}

void add_artifact(
    ManagedFiles& files,
    Reader& reader,
    std::string id,
    std::string job,
    std::string step,
    std::string port,
    std::string module,
    std::string type,
    std::string text
) {
    const auto path = "outputs/" + id + ".out";
    auto file = output_file(id, type, path, static_cast<std::int64_t>(text.size()));
    files.files.emplace(id, file);
    files.artifacts.push_back(application::GeneratedOutputArtifact{
        file,
        application::GeneratedOutputProvenance{
            .job_id = std::move(job),
            .step_id = std::move(step),
            .output_port = std::move(port),
            .plugin_id = "plugin",
            .plugin_version = "0.1.0",
            .module_id = std::move(module),
            .file_type = std::move(type),
            .relative_project_path = path,
            .step_progress = 1.0,
            .registered_at_utc = "registered",
        }
    });
    reader.texts.emplace(std::move(id), std::move(text));
}

[[nodiscard]] application::BatchPlanManagedFileSnapshot reference_snapshot(char fill) {
    return {
        .role = application::BatchInputFileRole::reference,
        .file_id = std::string{"ref-"} + fill,
        .file_type = "fasta",
        .size_bytes = 20,
        .sha256 = std::string(64U, fill),
    };
}

[[nodiscard]] application::BatchPlanNodeSnapshot fastq_qc_node(
    std::string value = {}
) {
    application::BatchPlanNodeSnapshot node{
        .node_id = "qc",
        .module_id = value.empty() ? "org.biocore.fastqqc.stats" : "org.biocore.fastqqc.trim-single",
        .plugin_version = "0.1.0",
        .parameters = {},
        .inputs = {},
        .outputs = {{"summary", "json"}, {"table", "tsv"}},
    };
    if (!value.empty()) {
        node.parameters.push_back({
            .name = "minimum-length",
            .type = domain::PluginParameterType::integer,
            .value = std::move(value),
            .source = application::BatchPlanParameterSource::workflow_parameter,
        });
    }
    return node;
}

[[nodiscard]] application::BatchPlanNodeSnapshot vcf_node(char reference_fill = 'a') {
    return application::BatchPlanNodeSnapshot{
        .node_id = "qc",
        .module_id = "org.biocore.vcfqc.filter",
        .plugin_version = "0.1.0",
        .parameters = {{
            .name = "min-depth",
