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
            .type = domain::PluginParameterType::integer,
            .value = "3",
            .source = application::BatchPlanParameterSource::workflow_parameter,
        }},
        .inputs = {{
            .port_name = "reference-evidence",
            .artifact_type = "fasta",
            .source_kind = application::BatchPlanInputSourceKind::managed_file,
            .source_id = std::string{"ref-"} + reference_fill,
            .source_port = {},
            .managed_file = reference_snapshot(reference_fill),
        }},
        .outputs = {{"filtered", "vcf"}, {"summary", "json"}, {"table", "tsv"}},
    };
}

[[nodiscard]] application::ApprovedBatchPlan make_plan(
    std::vector<application::ApprovedBatchSamplePlan> samples
) {
    return {
        .plan_id = "plan-1",
        .project_id = "project-1",
        .template_id = "template-1",
        .template_version = "1",
        .approved_at_utc = "approved",
        .samples = std::move(samples),
    };
}

[[nodiscard]] application::ApprovedBatchSamplePlan sample_plan(
    std::string id,
    application::BatchPlanNodeSnapshot node
) {
    return {
        .sample_id = std::move(id),
        .disposition = application::BatchPlanSampleDisposition::included,
        .workflow_id = std::string{"workflow"},
        .nodes = {std::move(node)},
    };
}

void seed_execution(ExecutionStore& executions, const std::vector<std::pair<std::string, std::string>>& samples) {
    application::BatchExecutionRecord record{
        .plan_id = "plan-1", .maximum_concurrent_jobs = 2U,
        .cancellation_requested = false, .submitted_at_utc = "submitted", .updated_at_utc = "submitted", .jobs = {}
    };
    std::size_t ordinal = 0U;
    for (const auto& [sample, job] : samples) {
        record.jobs.push_back({sample, ordinal++, job});
        executions.attempts.push_back({
            .plan_id = "plan-1", .sample_id = sample, .attempt_number = 1,
            .job_id = job, .parent_job_id = std::nullopt,
            .mode = application::BatchAttemptMode::initial,
            .created_at_utc = "created", .execution_node_ids = {"qc"}
        });
    }
    executions.record = std::move(record);
}

[[nodiscard]] application::BatchResultsOverview comparable_overview(bool missing_metric = false) {
    PlanStore plans;
    plans.plan = make_plan({sample_plan("sample-a", fastq_qc_node()), sample_plan("sample-b", fastq_qc_node())});
    ExecutionStore executions;
    seed_execution(executions, {{"sample-a", "job-a"}, {"sample-b", "job-b"}});
    Jobs jobs;
    jobs.values.emplace("job-a", completed_job("job-a"));
    jobs.values.emplace("job-b", completed_job("job-b"));
    ManagedFiles files;
    Reader reader;
    add_artifact(files, reader, "a-table", "job-a", "qc", "table", "org.biocore.fastqqc.stats", "tsv",
                 "metric\tvalue\nread_count\t10\nq30_percent\t90.0\n");
    add_artifact(files, reader, "b-table", "job-b", "qc", "table", "org.biocore.fastqqc.stats", "tsv",
                 missing_metric ? "metric\tvalue\nread_count\t12\n"
                                : "metric\tvalue\nread_count\t12\nq30_percent\t88.0\n");
    application::BatchResultsService service{plans, executions, jobs, files, reader};
    return service.overview("plan-1");
}

[[nodiscard]] bool test_comparable() {
    const auto result = comparable_overview(false);
    return result.samples.size() == 2U && result.qc_comparisons.size() == 1U &&
           result.qc_comparisons.front().state == application::BatchQcComparisonState::comparable &&
           result.samples.front().qc_summaries.front().metrics.size() == 2U;
}

[[nodiscard]] bool test_missing_not_zero() {
    const auto result = comparable_overview(true);
    if (result.qc_comparisons.size() != 1U ||
        result.qc_comparisons.front().state != application::BatchQcComparisonState::incomplete) return false;
    const auto& metrics = result.samples[1].qc_summaries.front().metrics;
    return metrics.size() == 1U && metrics.front().key == "read_count";
}

[[nodiscard]] bool test_contract_mismatch() {
    PlanStore plans;
    plans.plan = make_plan({sample_plan("sample-a", fastq_qc_node("20")), sample_plan("sample-b", fastq_qc_node("25"))});
    ExecutionStore executions;
    seed_execution(executions, {{"sample-a", "job-a"}, {"sample-b", "job-b"}});
    Jobs jobs;
    jobs.values.emplace("job-a", completed_job("job-a"));
    jobs.values.emplace("job-b", completed_job("job-b"));
    ManagedFiles files;
    Reader reader;
    const std::string table = "metric\tvalue\nminimum_length\t20\ninput_reads\t10\nkept_reads\t8\n";
    add_artifact(files, reader, "a-table", "job-a", "qc", "table", "org.biocore.fastqqc.trim-single", "tsv", table);
    add_artifact(files, reader, "b-table", "job-b", "qc", "table", "org.biocore.fastqqc.trim-single", "tsv", table);
    application::BatchResultsService service{plans, executions, jobs, files, reader};
    const auto result = service.overview("plan-1");
    return result.qc_comparisons.size() == 1U &&
           result.qc_comparisons.front().state == application::BatchQcComparisonState::incompatible &&
           result.samples[0].qc_summaries[0].metrics.size() == 2U;
}


[[nodiscard]] bool test_reference_mismatch() {
    PlanStore plans;
    plans.plan = make_plan({sample_plan("sample-a", vcf_node('a')), sample_plan("sample-b", vcf_node('b'))});
    ExecutionStore executions;
    seed_execution(executions, {{"sample-a", "job-a"}, {"sample-b", "job-b"}});
    Jobs jobs;
    jobs.values.emplace("job-a", completed_job("job-a"));
    jobs.values.emplace("job-b", completed_job("job-b"));
    ManagedFiles files;
    Reader reader;
    const std::string summary =
        "{\"schemaVersion\":1,\"module\":\"org.biocore.vcfqc.filter\","
        "\"metrics\":{\"totalRecords\":10,\"tiTvRatio\":null}}\n";
    add_artifact(files, reader, "a-summary", "job-a", "qc", "summary", "org.biocore.vcfqc.filter", "json", summary);
    add_artifact(files, reader, "b-summary", "job-b", "qc", "summary", "org.biocore.vcfqc.filter", "json", summary);
    application::BatchResultsService service{plans, executions, jobs, files, reader};
    const auto result = service.overview("plan-1");
    if (result.qc_comparisons.size() != 1U ||
        result.qc_comparisons.front().state != application::BatchQcComparisonState::incompatible) return false;
    const auto& metrics = result.samples[0].qc_summaries[0].metrics;
    const auto null_metric = std::ranges::find_if(metrics, [](const auto& metric) {
        return metric.key == "tiTvRatio";
    });
    return null_metric != metrics.end() &&
           null_metric->kind == application::BatchQcMetricValueKind::null_value &&
           !null_metric->value.has_value();
}

[[nodiscard]] bool test_lineage_boundary() {
    PlanStore plans;
    plans.plan = make_plan({sample_plan("sample-a", fastq_qc_node())});
    ExecutionStore executions;
    seed_execution(executions, {{"sample-a", "job-1"}});
    executions.attempts.push_back({
        .plan_id="plan-1", .sample_id="sample-a", .attempt_number=2, .job_id="job-2",
        .parent_job_id=std::string{"job-1"}, .mode=application::BatchAttemptMode::resume,
        .created_at_utc="resume", .execution_node_ids={}
    });
    Jobs jobs;
    jobs.values.emplace("job-1", completed_job("job-1"));
    jobs.values.emplace("job-2", completed_job("job-2", 2));
    ManagedFiles files;
    Reader reader;
    add_artifact(files, reader, "old-table", "job-1", "qc", "table", "org.biocore.fastqqc.stats", "tsv",
                 "metric\tvalue\nread_count\t10\n");
    application::BatchResultsService service{plans, executions, jobs, files, reader};
    const auto resumed = service.overview("plan-1");
    if (resumed.samples[0].artifacts.size() != 1U || resumed.samples[0].artifacts[0].job_id != "job-1") return false;
    executions.attempts.back().execution_node_ids = {"qc"};
    const auto recomputing = service.overview("plan-1");
    if (!recomputing.samples[0].artifacts.empty()) return false;
    executions.attempts.back().mode = application::BatchAttemptMode::retry;
    const auto retried = service.overview("plan-1");
    return retried.samples[0].artifacts.empty() && retried.samples[0].qc_summaries.empty();
}

[[nodiscard]] std::string vcf_for(std::string_view sample, std::string_view genotype) {
    return "##fileformat=VCFv4.3\n"
           "##FORMAT=<ID=GT,Number=1,Type=String,Description=\"Genotype\">\n"
           "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\t" + std::string{sample} + "\n"
           "chr1\t10\trs1\tA\tG\t.\tPASS\t.\tGT\t" + std::string{genotype} + "\n";
}

[[nodiscard]] bool test_matrix() {
    PlanStore plans;
    plans.plan = make_plan({sample_plan("sample-a", vcf_node()), sample_plan("sample-b", vcf_node())});
    ExecutionStore executions;
    seed_execution(executions, {{"sample-a", "job-a"}, {"sample-b", "job-b"}});
    Jobs jobs;
    jobs.values.emplace("job-a", completed_job("job-a"));
    jobs.values.emplace("job-b", completed_job("job-b"));
    ManagedFiles files;
    Reader reader;
    add_artifact(files, reader, "a-vcf", "job-a", "qc", "filtered", "org.biocore.vcfqc.filter", "vcf", vcf_for("sample-a", "0/1"));
    add_artifact(files, reader, "b-vcf", "job-b", "qc", "filtered", "org.biocore.vcfqc.filter", "vcf", vcf_for("sample-b", "1/1"));
    application::BatchResultsService service{plans, executions, jobs, files, reader};
    std::istringstream fasta{">chr1\nAAAAAAAAAAAAAAAAAAAA\n"};
    const auto reference = domain::ReferenceGenome::from_fasta(fasta, domain::ReferenceAssembly::grch38);
    const auto built = service.build_variant_matrix(
        "plan-1", {domain::ReferenceAssembly::grch38, {}}, reference
    );
    const auto a = built.matrix.sample_index("sample-a");
    const auto b = built.matrix.sample_index("sample-b");
    return built.preview.ready && built.matrix.sample_count() == 2U && built.matrix.variant_count() == 1U &&
           a.has_value() && b.has_value() &&
           built.matrix.find_observation(*a, 0U)->alternate_dosage == 1U &&
           built.matrix.find_observation(*b, 0U)->alternate_dosage == 2U;
}

[[nodiscard]] bool test_matrix_rejects_sample_mismatch() {
    PlanStore plans;
    plans.plan = make_plan({sample_plan("sample-a", vcf_node())});
    ExecutionStore executions;
    seed_execution(executions, {{"sample-a", "job-a"}});
    Jobs jobs;
    jobs.values.emplace("job-a", completed_job("job-a"));
    ManagedFiles files;
    Reader reader;
    add_artifact(files, reader, "a-vcf", "job-a", "qc", "filtered", "org.biocore.vcfqc.filter", "vcf", vcf_for("wrong-sample", "0/1"));
    application::BatchResultsService service{plans, executions, jobs, files, reader};
    std::istringstream fasta{">chr1\nAAAAAAAAAAAAAAAAAAAA\n"};
    const auto reference = domain::ReferenceGenome::from_fasta(fasta, domain::ReferenceAssembly::grch38);
    try {
        static_cast<void>(service.build_variant_matrix(
            "plan-1", {domain::ReferenceAssembly::grch38, {}}, reference
        ));
        return false;
    } catch (const std::invalid_argument&) {
        return true;
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    const std::string mode = argv[1];
    bool ok = false;
    if (mode == "comparable") ok = test_comparable();
    else if (mode == "missing") ok = test_missing_not_zero();
    else if (mode == "contract") ok = test_contract_mismatch();
    else if (mode == "reference") ok = test_reference_mismatch();
    else if (mode == "lineage") ok = test_lineage_boundary();
    else if (mode == "matrix") ok = test_matrix();
    else if (mode == "matrix-reject") ok = test_matrix_rejects_sample_mismatch();
    else return EXIT_FAILURE;
    if (!ok) {
        std::cerr << "Batch results test failed: " << mode << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
