#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/cohort_analysis_selection_service.hpp"
#include "biocore/application/i_batch_execution_store.hpp"
#include "biocore/application/i_batch_plan_store.hpp"
#include "biocore/application/i_cohort_registry_store.hpp"
#include "biocore/application/i_input_file_storage.hpp"
#include "biocore/application/i_job_repository.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_reference_manifest_reader.hpp"
#include "biocore/application/i_result_artifact_reader.hpp"
#include "biocore/domain/job.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/infrastructure/filesystem_input_file_storage.hpp"
#include "biocore/infrastructure/filesystem_reference_manifest_reader.hpp"

namespace {
using namespace biocore;

constexpr const char* project_id = "p-001";
constexpr const char* cohort_id = "cohort-091";
constexpr const char* ref_hash =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
constexpr const char* vcf_hash =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

void check(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

[[nodiscard]] bool has_issue(
    const application::CohortAnalysisSelectionPreview& preview,
    const std::string_view code
) {
    return std::ranges::any_of(preview.issues, [code](const auto& issue) {
        return issue.code == code;
    });
}

class Cohorts final : public application::ICohortRegistryStore {
public:
    application::CohortStoreWriteResult create(
        const application::CreateCohortStoreRequest&
    ) override { return application::CohortStoreWriteResult::stored; }
    application::CohortStoreWriteResult append_revision(
        const application::AppendCohortRevisionStoreRequest&
    ) override { return application::CohortStoreWriteResult::stored; }
    std::optional<application::CohortDefinition> find(
        std::string_view project,
        std::string_view cohort,
        std::optional<std::uint32_t> revision = std::nullopt
    ) override {
        if (!value.has_value() || value->project_id != project || value->cohort_id != cohort) {
            return std::nullopt;
        }
        if (revision.has_value() && value->revision.revision != *revision) return std::nullopt;
        return value;
    }
    std::vector<application::CohortDefinition> list(std::string_view project) override {
        return value.has_value() && value->project_id == project
            ? std::vector<application::CohortDefinition>{*value}
            : std::vector<application::CohortDefinition>{};
    }
    std::optional<application::CohortDefinition> value;
};

class Plans final : public application::IBatchPlanStore {
public:
    bool add(const application::ApprovedBatchPlan& value) override {
        values.insert_or_assign(value.plan_id, value);
        return true;
    }
    std::optional<application::ApprovedBatchPlan> find(std::string_view id) override {
        const auto found = values.find(std::string{id});
        return found == values.end() ? std::nullopt
                                     : std::optional<application::ApprovedBatchPlan>{found->second};
    }
    std::vector<application::ApprovedBatchPlan> list() override {
        std::vector<application::ApprovedBatchPlan> result;
        for (const auto& [id, value] : values) {
            static_cast<void>(id);
            result.push_back(value);
        }
        return result;
    }
    std::map<std::string, application::ApprovedBatchPlan, std::less<>> values;
};

class Executions final : public application::IBatchExecutionStore {
public:
    application::AddBatchExecutionResult add(
        const application::BatchExecutionRecord& value,
        std::span<const application::BatchPreparedJob>
    ) override {
        records.insert_or_assign(value.plan_id, value);
        return application::AddBatchExecutionResult::created;
    }
    std::optional<application::BatchExecutionRecord> find(std::string_view id) override {
        const auto found = records.find(std::string{id});
        return found == records.end() ? std::nullopt
                                      : std::optional<application::BatchExecutionRecord>{found->second};
    }
    std::vector<application::BatchExecutionRecord> list() override {
        std::vector<application::BatchExecutionRecord> result;
        for (const auto& [id, value] : records) {
            static_cast<void>(id);
            result.push_back(value);
        }
        return result;
    }
    std::vector<application::BatchExecutionAttemptRecord> list_attempts(
        std::string_view plan_id
    ) override {
        std::vector<application::BatchExecutionAttemptRecord> result;
        for (const auto& value : attempts) {
            if (value.plan_id == plan_id) result.push_back(value);
        }
        return result;
    }
    application::AddBatchAttemptResult add_attempt(
        const application::BatchPreparedAttempt& value
    ) override {
        attempts.push_back(value.attempt);
        return application::AddBatchAttemptResult::created;
    }
    bool request_cancellation(std::string_view, std::string_view) override { return true; }
    std::optional<application::BatchSchedulingQuota> quota_for_job(
        std::string_view
    ) override { return std::nullopt; }

    std::map<std::string, application::BatchExecutionRecord, std::less<>> records;
    std::vector<application::BatchExecutionAttemptRecord> attempts;
};

class Jobs final : public application::IJobRepository {
public:
    bool add(const domain::Job& job) override {
        return values.emplace(std::string{job.id()}, job).second;
    }
    std::optional<domain::Job> find_by_id(std::string_view id) override {
        const auto found = values.find(std::string{id});
        return found == values.end() ? std::nullopt
                                     : std::optional<domain::Job>{found->second};
    }
    std::vector<domain::Job> list() override {
        std::vector<domain::Job> result;
        for (const auto& [id, value] : values) {
            static_cast<void>(id);
            result.push_back(value);
        }
        return result;
    }
    bool update_runtime_state(const domain::Job& job, std::int64_t) override {
        values.insert_or_assign(std::string{job.id()}, job);
        return true;
    }
    std::map<std::string, domain::Job, std::less<>> values;
};

class Files final : public application::IManagedFileRepository {
public:
    bool add(const domain::ManagedFile& file) override {
        files.insert_or_assign(std::string{file.id()}, file);
        return true;
    }
    std::optional<domain::ManagedFile> find_by_id(std::string_view id) override {
        const auto found = files.find(std::string{id});
        return found == files.end() ? std::nullopt
                                    : std::optional<domain::ManagedFile>{found->second};
    }
    std::optional<domain::ManagedFile> find_by_relative_project_path(
        std::string_view path
    ) override {
        for (const auto& [id, file] : files) {
            static_cast<void>(id);
            if (file.relative_project_path() ==
                std::optional<std::string>{std::string{path}}) return file;
        }
        return std::nullopt;
    }
    std::vector<domain::ManagedFile> list() override {
        std::vector<domain::ManagedFile> result;
        for (const auto& [id, value] : files) {
            static_cast<void>(id);
            result.push_back(value);
        }
        return result;
    }
    bool add_generated_output(
        const domain::ManagedFile& file,
        const application::GeneratedOutputProvenance& provenance
    ) override {
        const application::GeneratedOutputArtifact value{file, provenance};
        return add_generated_outputs_batch(std::span{&value, 1U});
    }
    bool add_generated_outputs_batch(
        std::span<const application::GeneratedOutputArtifact> values
    ) override {
        for (const auto& value : values) {
            files.insert_or_assign(std::string{value.file.id()}, value.file);
            artifacts.push_back(value);
        }
        return true;
    }
    std::optional<application::GeneratedOutputArtifact> find_generated_output(
        std::string_view job,
        std::string_view step,
        std::string_view port
    ) override {
        for (const auto& value : artifacts) {
            if (value.provenance.job_id == job &&
                value.provenance.step_id == step &&
                value.provenance.output_port == port) return value;
        }
        return std::nullopt;
    }
    std::vector<application::GeneratedOutputArtifact> list_generated_outputs(
        std::string_view job
    ) override {
        std::vector<application::GeneratedOutputArtifact> result;
        for (const auto& value : artifacts) {
            if (value.provenance.job_id == job) result.push_back(value);
        }
        return result;
    }

    std::map<std::string, domain::ManagedFile, std::less<>> files;
    std::vector<application::GeneratedOutputArtifact> artifacts;
};

class InputStorage final : public application::IInputFileStorage {
public:
    std::unique_ptr<application::IInputFileImportTransaction> prepare_managed_copy(
        std::string_view, std::string_view
    ) override { throw std::logic_error{"unused"}; }
    bool begin_browser_upload(std::string_view, std::string_view) override {
        throw std::logic_error{"unused"};
    }
    std::uint64_t append_browser_upload(
        std::string_view, std::uint64_t, std::string_view
    ) override { throw std::logic_error{"unused"}; }
    std::unique_ptr<application::IInputFileImportTransaction> prepare_browser_upload_commit(
        std::string_view, std::string_view
    ) override { throw std::logic_error{"unused"}; }
    void discard_browser_upload(std::string_view) noexcept override {}

    application::ManagedFileIntegrityResult verify_managed_file(
        const domain::ManagedFile& file
    ) const override {
        return {
            .status = status,
            .expected_size_bytes = file.size_bytes(),
            .observed_size_bytes = file.size_bytes(),
            .expected_sha256 = file.checksum_value(),
            .observed_sha256 = status == application::ManagedFileIntegrityStatus::verified
                ? file.checksum_value() : std::nullopt,
        };
    }
    application::ManagedFileIntegrityStatus status{
        application::ManagedFileIntegrityStatus::verified
    };
};

class ArtifactReader final : public application::IResultArtifactReader {
public:
    application::ResultArtifactText read_verified_text(
        const application::GeneratedOutputArtifact& artifact,
        std::size_t maximum_bytes
    ) override {
        ++reads[std::string{artifact.file.id()}];
        const auto found = texts.find(std::string{artifact.file.id()});
        if (found == texts.end()) {
            return {application::ResultArtifactReadStatus::missing, std::nullopt, std::nullopt};
        }
        if (artifact.file.size_bytes() < 0 ||
            static_cast<std::uint64_t>(artifact.file.size_bytes()) > maximum_bytes) {
            return {application::ResultArtifactReadStatus::too_large, std::nullopt, std::nullopt};
        }
        return {
            .status = status,
            .text = found->second,
            .verified_sha256 = artifact.file.checksum_value(),
        };
    }
    application::ResultArtifactReadStatus status{
        application::ResultArtifactReadStatus::verified
    };
    std::map<std::string, std::string, std::less<>> texts;
    std::map<std::string, std::size_t, std::less<>> reads;
};

class ReferenceReader final : public application::IReferenceManifestReader {
public:
    application::ReferenceManifestRead read_verified_manifest(
        const domain::ManagedFile& file,
        std::size_t
    ) override {
        return {
            .status = status,
            .contigs = {{"chr1", 100U}, {"chr2", 200U}},
            .verified_sha256 = status == application::ReferenceManifestReadStatus::verified
                ? file.checksum_value() : std::nullopt,
            .verified_size_bytes = file.size_bytes(),
        };
    }
    application::ReferenceManifestReadStatus status{
        application::ReferenceManifestReadStatus::verified
    };
};

[[nodiscard]] domain::ManagedFile reference_file() {
    return domain::ManagedFile{
        "ref", "reference.fa", domain::StorageMode::managed_copy,
        "/source/reference.fa", "/project/inputs/ref/reference.fa",
        "inputs/ref/reference.fa", "fasta", 100,
        std::nullopt, std::string{"sha256"}, std::string{ref_hash},
        "created", "updated"
    };
}

[[nodiscard]] domain::ManagedFile output_file(
    std::string id,
    const std::int64_t size,
    std::string hash = vcf_hash
) {
    const auto path = "outputs/" + id + ".vcf";
    return domain::ManagedFile{
        std::move(id), "variants.vcf", domain::StorageMode::generated_output,
        std::nullopt, "/project/" + path, path, "vcf", size,
        std::nullopt, std::string{"sha256"}, std::move(hash), "created", "updated"
    };
}

[[nodiscard]] domain::Job completed_job(std::string id, std::int64_t attempt = 1) {
    return domain::Job{
        std::move(id), std::nullopt, std::string{"org.biocore.batch.workflow"},
        std::string{"1.0.0"}, domain::JobStatus::completed, domain::JobPriority::normal,
        1.0, std::nullopt, "created", "finished", std::string{"started"},
        std::string{"finished"}, 3, std::nullopt, attempt
    };
}

[[nodiscard]] application::BatchPlanNodeSnapshot vcf_node(
    std::string reference_sha = ref_hash
) {
    return application::BatchPlanNodeSnapshot{
        .node_id = "variants",
        .module_id = "org.biocore.vcfqc.filter",
        .plugin_version = "0.1.0",
        .parameters = {},
        .inputs = {{
            .port_name = "reference",
            .artifact_type = "fasta",
            .source_kind = application::BatchPlanInputSourceKind::managed_file,
            .source_id = "ref",
            .source_port = {},
            .managed_file = application::BatchPlanManagedFileSnapshot{
                .role = application::BatchInputFileRole::reference,
                .file_id = "ref",
                .file_type = "fasta",
                .size_bytes = 100,
                .sha256 = std::move(reference_sha),
            },
        }},
        .outputs = {{"vcf", "vcf"}},
    };
}

[[nodiscard]] application::ApprovedBatchSamplePlan batch_sample(
    std::string sample_id,
    std::string reference_sha = ref_hash
) {
    return {
        .sample_id = std::move(sample_id),
        .disposition = application::BatchPlanSampleDisposition::included,
        .workflow_id = std::string{"workflow-1"},
        .nodes = {vcf_node(std::move(reference_sha))},
    };
}

[[nodiscard]] application::ApprovedBatchPlan batch_plan(
    std::vector<application::ApprovedBatchSamplePlan> samples
) {
    return {
        .plan_id = "plan-1",
        .project_id = project_id,
        .template_id = "template",
        .template_version = "1",
        .approved_at_utc = "approved",
        .samples = std::move(samples),
    };
}

[[nodiscard]] application::CohortMemberSnapshot cohort_member(
    std::string sample,
    std::string biological,
    application::CohortGroup group
) {
    return {
        .sample_id = std::move(sample),
        .sample_display_name = "display",
        .sample_group_metadata = "legacy",
        .biological_unit_id = std::move(biological),
        .group = group,
        .disposition = application::CohortMemberDisposition::included,
        .exclusion_reason = std::nullopt,
    };
}

[[nodiscard]] application::CohortDefinition cohort(
    std::vector<application::CohortMemberSnapshot> members
) {
    application::CohortRevision revision{
        .project_id = project_id,
        .cohort_id = cohort_id,
        .revision = 1U,
        .parent_revision = std::nullopt,
        .created_at_utc = "created",
        .members = std::move(members),
    };
    return {
        .project_id = project_id,
        .cohort_id = cohort_id,
        .name = "Cohort",
        .current_revision = 1U,
        .created_at_utc = "created",
        .updated_at_utc = "created",
        .revision = std::move(revision),
    };
}

void add_attempt(
    Executions& executions,
    Jobs& jobs,
    const std::string& producer_sample,
    const std::int64_t attempt,
    const std::string& job_id
) {
    executions.attempts.push_back({
        .plan_id = "plan-1",
        .sample_id = producer_sample,
        .attempt_number = attempt,
        .job_id = job_id,
        .parent_job_id = std::nullopt,
        .mode = attempt == 1 ? application::BatchAttemptMode::initial
                             : application::BatchAttemptMode::retry,
        .created_at_utc = "created",
        .execution_node_ids = {"variants"},
    });
    jobs.values.emplace(job_id, completed_job(job_id, attempt));
}

void add_artifact(
    Files& files,
    ArtifactReader& reader,
    std::string id,
    std::string job,
    std::string text,
    std::int64_t declared_size = -1
) {
    const std::int64_t size = declared_size >= 0
        ? declared_size : static_cast<std::int64_t>(text.size());
    auto file = output_file(id, size);
    files.files.emplace(std::string{file.id()}, file);
    files.artifacts.push_back({
        file,
        application::GeneratedOutputProvenance{
            .job_id = std::move(job),
            .step_id = "variants",
            .output_port = "vcf",
            .plugin_id = "plugin",
            .plugin_version = "0.1.0",
            .module_id = "org.biocore.vcfqc.filter",
            .file_type = "vcf",
            .relative_project_path = std::string{file.relative_project_path().value()},
            .step_progress = 1.0,
            .registered_at_utc = "registered",
        },
    });
    reader.texts.emplace(std::string{file.id()}, std::move(text));
}

[[nodiscard]] application::CohortVcfSelection selection(
    std::string project_sample,
    std::string artifact,
    std::string vcf_sample,
    std::string job = "job-1",
    std::int64_t attempt = 1,
    std::string expected = vcf_hash
) {
    return {
        .project_sample_id = std::move(project_sample),
        .plan_id = "plan-1",
        .attempt_number = attempt,
        .job_id = std::move(job),
        .step_id = "variants",
        .output_port = "vcf",
        .managed_file_id = std::move(artifact),
        .expected_sha256 = std::move(expected),
        .vcf_sample_name = std::move(vcf_sample),
    };
}

[[nodiscard]] application::CohortAnalysisSelectionRequest request(
    std::vector<application::CohortVcfSelection> selections
) {
    return {
        .project_id = project_id,
        .cohort_id = cohort_id,
        .cohort_revision = 1U,
        .reference = {
            .managed_file_id = "ref",
            .assembly = domain::ReferenceAssembly::grch38,
            .custom_assembly_id = std::nullopt,
            .normalization_contract_version =
                application::CohortAnalysisSelectionService::normalization_contract_v1,
            .aliases = {{"1", "chr1"}},
        },
        .selections = std::move(selections),
    };
}

struct Harness final {
    Cohorts cohorts;
    Plans plans;
    Executions executions;
    Jobs jobs;
    Files files;
    InputStorage input;
    ArtifactReader artifact_reader;
    ReferenceReader reference_reader;

    Harness() {
        files.files.emplace("ref", reference_file());
        cohorts.value = cohort({
            cohort_member("001", "bio-001", application::CohortGroup::case_group)
        });
        plans.values.emplace("plan-1", batch_plan({batch_sample("001")}));
        add_attempt(executions, jobs, "001", 1, "job-1");
    }

    application::CohortAnalysisSelectionService service() {
        return {
            cohorts, plans, executions, jobs, files, input, artifact_reader, reference_reader
        };
    }
};

[[nodiscard]] std::string vcf_one(
    const std::string_view sample,
    const std::string_view genotype = "0/1"
) {
    return
        "##fileformat=VCFv4.3\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\t" +
        std::string{sample} + "\n"
        "chr1\t10\t.\tA\tG\t.\tPASS\t.\tGT\t" +
        std::string{genotype} + "\n";
}

void mapping_contract() {
    Harness h;
    add_artifact(h.files, h.artifact_reader, "vcf-1", "job-1", vcf_one("001"));
    const auto preview = h.service().preview(request({selection("001", "vcf-1", "001")}));
    check(preview.ready, "valid explicit mapping rejected");
    check(preview.sources.size() == 1U && preview.sources[0].project_sample_id == "001",
          "sample id changed");
    check(preview.reference.has_value() && preview.reference->contigs.size() == 2U,
          "reference manifest was not pinned");
}

void multi_vcf_contract() {
    Harness h;
    h.cohorts.value = cohort({
        cohort_member("001", "bio-001", application::CohortGroup::case_group),
        cohort_member("Örnek-A", "bio-002", application::CohortGroup::control),
    });
    h.plans.values["plan-1"] = batch_plan({batch_sample("producer")});
    h.executions.attempts.clear();
    h.jobs.values.clear();
    add_attempt(h.executions, h.jobs, "producer", 1, "job-1");
    const std::string text =
        "##fileformat=VCFv4.3\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\t001\tÖrnek-A\n"
        "chr1\t10\t.\tA\tG\t.\tPASS\t.\tGT\t0/1\t1|1\n";
    add_artifact(h.files, h.artifact_reader, "shared", "job-1", text);
    const auto preview = h.service().preview(request({
        selection("001", "shared", "001"),
        selection("Örnek-A", "shared", "Örnek-A"),
    }));
    check(preview.ready && preview.sources.size() == 2U, "multi-sample VCF mapping failed");
    check(h.artifact_reader.reads["shared"] == 1U, "shared VCF was not read exactly once");
}

void attempt_contract() {
    Harness h;
    add_artifact(h.files, h.artifact_reader, "vcf-1", "job-1", vcf_one("001"));
    add_attempt(h.executions, h.jobs, "001", 2, "job-2");
    add_artifact(h.files, h.artifact_reader, "vcf-2", "job-2", vcf_one("001"));
    const auto preview = h.service().preview(request({
        selection("001", "vcf-1", "001", "job-1", 1),
        selection("001", "vcf-2", "001", "job-2", 2),
    }));
    check(!preview.ready && has_issue(preview, "mapping_conflict"),
          "same sample from two attempts was accepted");
}

void hash_contract() {
    Harness h;
    add_artifact(h.files, h.artifact_reader, "vcf-1", "job-1", vcf_one("001"));
    const auto preview = h.service().preview(request({
        selection("001", "vcf-1", "001", "job-1", 1,
                  "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc")
    }));
    check(!preview.ready && has_issue(preview, "artifact_changed"),
          "stale artifact hash was accepted");
}

void reference_contract() {
    Harness h;
    h.plans.values["plan-1"] = batch_plan({
        batch_sample("001",
            "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc")
    });
    add_artifact(h.files, h.artifact_reader, "vcf-1", "job-1", vcf_one("001"));
    const auto mismatch = h.service().preview(request({selection("001", "vcf-1", "001")}));
    check(!mismatch.ready && has_issue(mismatch, "reference_unverified"),
          "producing workflow reference mismatch was accepted");

    h.plans.values["plan-1"] = batch_plan({batch_sample("001")});
    h.input.status = application::ManagedFileIntegrityStatus::checksum_mismatch;
    const auto tampered = h.service().preview(request({selection("001", "vcf-1", "001")}));
    check(!tampered.ready && has_issue(tampered, "reference_unverified"),
          "reference byte mismatch was accepted");
}

void ploidy_contract() {
    Harness h;
    add_artifact(h.files, h.artifact_reader, "vcf-1", "job-1", vcf_one("001", "1"));
    const auto haploid = h.service().preview(request({selection("001", "vcf-1", "001")}));
    check(!haploid.ready && has_issue(haploid, "unsupported_ploidy"),
          "haploid GT was accepted");

    h.artifact_reader.texts["vcf-1"] = vcf_one("001", "0/1/1");
    const auto polyploid = h.service().preview(request({selection("001", "vcf-1", "001")}));
    check(!polyploid.ready && has_issue(polyploid, "unsupported_ploidy"),
          "polyploid GT was accepted");
}

void limits_contract() {
    Harness h;
    const auto too_large = static_cast<std::int64_t>(
        application::CohortAnalysisSelectionService::maximum_vcf_bytes) + 1;
    add_artifact(h.files, h.artifact_reader, "vcf-big", "job-1", vcf_one("001"), too_large);
    const auto file_limit = h.service().preview(
        request({selection("001", "vcf-big", "001")}));
    check(!file_limit.ready && has_issue(file_limit, "limit_exceeded"),
          "per-VCF admission limit was not enforced");

    auto many = request({});
    many.selections.resize(application::CohortAnalysisSelectionService::maximum_samples + 1U);
    const auto count_limit = h.service().preview(many);
    check(has_issue(count_limit, "limit_exceeded"), "sample admission limit was not enforced");
}

class Temp final {
public:
    Temp() : root{std::filesystem::temp_directory_path() /
        ("biocore-cohort-091-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()))} {
        std::filesystem::create_directories(root / "inputs");
        std::filesystem::create_directories(root / ".biocore" / "runtime");
    }
    ~Temp() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    std::filesystem::path root;
};

void manifest_contract() {
    Temp temp;
    const auto source = temp.root / "source.fa";
    {
        std::ofstream output{source, std::ios::binary};
        output << ">chr1\nAAAA\n>chr2 description\nCCCCCC\n";
    }
    const auto canonical_root = std::filesystem::canonical(temp.root).generic_string();
    infrastructure::FilesystemInputFileStorage storage{canonical_root};
    auto transaction = storage.prepare_managed_copy(source.generic_string(), "ref-manifest");
    const auto prepared = transaction->prepared_file();
    domain::ManagedFile managed{
        "ref-manifest", prepared.display_name, domain::StorageMode::managed_copy,
        prepared.original_path, prepared.managed_path, prepared.relative_project_path,
        "fasta", prepared.size_bytes, std::nullopt,
        prepared.checksum_algorithm, prepared.checksum_value, "created", "updated"
    };
    transaction->commit();

    infrastructure::FilesystemReferenceManifestReader reader{storage};
    const auto manifest = reader.read_verified_manifest(managed, 1024U);
    check(manifest.status == application::ReferenceManifestReadStatus::verified &&
          manifest.contigs.size() == 2U &&
          manifest.contigs[0].canonical_name == "chr1" &&
          manifest.contigs[0].length == 4U &&
          manifest.contigs[1].canonical_name == "chr2" &&
          manifest.contigs[1].length == 6U,
          "streaming FASTA manifest is incorrect");

    {
        std::ofstream output{*managed.managed_path(), std::ios::binary | std::ios::app};
        output << "A";
    }
    const auto changed = reader.read_verified_manifest(managed, 1024U);
    check(changed.status == application::ReferenceManifestReadStatus::integrity_unverified,
          "tampered FASTA remained verified");
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        if (argc != 2) return EXIT_FAILURE;
        const std::string_view mode{argv[1]};
        if (mode == "mapping") mapping_contract();
        else if (mode == "multi-vcf") multi_vcf_contract();
        else if (mode == "attempts") attempt_contract();
        else if (mode == "hash") hash_contract();
        else if (mode == "reference") reference_contract();
        else if (mode == "ploidy") ploidy_contract();
        else if (mode == "limits") limits_contract();
        else if (mode == "manifest") manifest_contract();
        else return EXIT_FAILURE;
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
