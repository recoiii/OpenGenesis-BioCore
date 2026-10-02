#include <algorithm>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/cohort_analysis_selection_service.hpp"
#include "biocore/application/cohort_matrix_service.hpp"
#include "biocore/application/i_batch_execution_store.hpp"
#include "biocore/application/i_batch_plan_store.hpp"
#include "biocore/application/i_cohort_registry_store.hpp"
#include "biocore/application/i_input_file_storage.hpp"
#include "biocore/application/i_job_repository.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_reference_genome_reader.hpp"
#include "biocore/application/i_reference_manifest_reader.hpp"
#include "biocore/application/i_result_artifact_reader.hpp"
#include "biocore/domain/job.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/infrastructure/filesystem_input_file_storage.hpp"
#include "biocore/infrastructure/filesystem_reference_genome_reader.hpp"

namespace {
using namespace biocore;

constexpr const char* project_id = "p-001";
constexpr const char* cohort_id = "cohort-092";
constexpr const char* ref_hash =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
constexpr const char* vcf_hash =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

template<class Exception, class Function>
void rejects(Function function) {
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error{"Expected rejection"};
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
        if (!value.has_value() || value->project_id != project ||
            value->cohort_id != cohort ||
            (revision.has_value() && value->revision.revision != *revision)) {
            return std::nullopt;
        }
        return value;
    }

    std::vector<application::CohortDefinition> list(std::string_view) override {
        return value.has_value()
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
        const application::BatchExecutionRecord&,
        std::span<const application::BatchPreparedJob>
    ) override {
        return application::AddBatchExecutionResult::created;
    }

    std::optional<application::BatchExecutionRecord> find(std::string_view) override {
        return std::nullopt;
    }

    std::vector<application::BatchExecutionRecord> list() override { return {}; }

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

    std::unique_ptr<application::IInputFileImportTransaction>
    prepare_browser_upload_commit(std::string_view, std::string_view) override {
        throw std::logic_error{"unused"};
    }

    void discard_browser_upload(std::string_view) noexcept override {}

    application::ManagedFileIntegrityResult verify_managed_file(
        const domain::ManagedFile& file
    ) const override {
        return {
            .status = application::ManagedFileIntegrityStatus::verified,
            .expected_size_bytes = file.size_bytes(),
            .observed_size_bytes = file.size_bytes(),
            .expected_sha256 = file.checksum_value(),
            .observed_sha256 = file.checksum_value(),
        };
    }
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
            return {
                application::ResultArtifactReadStatus::missing,
                std::nullopt,
                std::nullopt
            };
        }
        if (artifact.file.size_bytes() < 0 ||
            static_cast<std::uint64_t>(artifact.file.size_bytes()) > maximum_bytes) {
            return {
                application::ResultArtifactReadStatus::too_large,
                std::nullopt,
                std::nullopt
            };
        }
        return {
            .status = application::ResultArtifactReadStatus::verified,
            .text = found->second,
            .verified_sha256 = artifact.file.checksum_value(),
        };
    }

    std::map<std::string, std::string, std::less<>> texts;
    std::map<std::string, std::size_t, std::less<>> reads;
};

class ManifestReader final : public application::IReferenceManifestReader {
public:
    application::ReferenceManifestRead read_verified_manifest(
        const domain::ManagedFile& file,
        std::size_t
    ) override {
        return {
            .status = application::ReferenceManifestReadStatus::verified,
            .contigs = {{contig_name, contig_length}},
            .verified_sha256 = file.checksum_value(),
            .verified_size_bytes = file.size_bytes(),
        };
    }

    std::string contig_name{"chr1"};
    std::uint64_t contig_length{256U};
};

class GenomeReader final : public application::IReferenceGenomeReader {
public:
    application::ReferenceGenomeRead read_verified_genome(
        const domain::ManagedFile& file,
        domain::ReferenceAssembly assembly,
        std::size_t
    ) override {
        std::istringstream input{fasta};
        auto genome = domain::ReferenceGenome::from_fasta(input, assembly);
        return {
            .status = application::ReferenceGenomeReadStatus::verified,
            .genome = std::move(genome),
            .verified_sha256 = drift
                ? std::optional<std::string>{
                    "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"}
                : file.checksum_value(),
            .verified_size_bytes = file.size_bytes(),
        };
    }

    std::string fasta;
    bool drift{false};
};

[[nodiscard]] domain::Job completed_job(std::string id) {
    return domain::Job{
        std::move(id), std::nullopt, std::string{"org.biocore.batch.workflow"},
        std::string{"1.0.0"}, domain::JobStatus::completed, domain::JobPriority::normal,
        1.0, std::nullopt, "created", "finished", std::string{"started"},
        std::string{"finished"}, 3, std::nullopt, 1
    };
}

[[nodiscard]] application::CohortMemberSnapshot member(
    std::string id,
    std::string biological,
    application::CohortGroup group
) {
    return {
        .sample_id = std::move(id),
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

[[nodiscard]] application::BatchPlanNodeSnapshot vcf_node(
    std::int64_t reference_size
) {
    return {
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
                .size_bytes = reference_size,
                .sha256 = ref_hash,
            },
        }},
        .outputs = {{"vcf", "vcf"}},
    };
}

[[nodiscard]] application::ApprovedBatchPlan plan(
    std::string producer_sample,
    std::int64_t reference_size
) {
    return {
        .plan_id = "plan-1",
        .project_id = project_id,
        .template_id = "template",
        .template_version = "1",
        .approved_at_utc = "approved",
        .samples = {{
            .sample_id = std::move(producer_sample),
            .disposition = application::BatchPlanSampleDisposition::included,
            .workflow_id = std::string{"workflow-1"},
            .nodes = {vcf_node(reference_size)},
        }},
    };
}

[[nodiscard]] domain::ManagedFile reference_file(
    std::int64_t size
) {
    return {
        "ref", "reference.fa", domain::StorageMode::managed_copy,
        "/source/reference.fa", "/project/inputs/ref/reference.fa",
        "inputs/ref/reference.fa", "fasta", size, std::nullopt,
        std::string{"sha256"}, std::string{ref_hash}, "created", "updated"
    };
}

[[nodiscard]] domain::ManagedFile output_file(
    std::string id,
    std::int64_t size
) {
    const auto path = "outputs/" + id + ".vcf";
    return {
        std::move(id), "variants.vcf", domain::StorageMode::generated_output,
        std::nullopt, "/project/" + path, path, "vcf", size, std::nullopt,
        std::string{"sha256"}, std::string{vcf_hash}, "created", "updated"
    };
}

struct Harness final {
    Cohorts cohorts;
    Plans plans;
    Executions executions;
    Jobs jobs;
    Files files;
    InputStorage input;
    ArtifactReader artifacts;
    ManifestReader manifest;
    GenomeReader genome;

    std::string producer{"producer"};
    std::string fasta{
        ">chr1\n"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
    };

    Harness() {
        genome.fasta = fasta;
        const auto size = static_cast<std::int64_t>(fasta.size());
        files.files.emplace("ref", reference_file(size));
        cohorts.value = cohort({
            member("001", "bio-001", application::CohortGroup::case_group)
        });
        plans.values.emplace("plan-1", plan(producer, size));
        executions.attempts.push_back({
            .plan_id = "plan-1",
            .sample_id = producer,
            .attempt_number = 1,
            .job_id = "job-1",
            .parent_job_id = std::nullopt,
            .mode = application::BatchAttemptMode::initial,
            .created_at_utc = "created",
            .execution_node_ids = {"variants"},
        });
        jobs.values.emplace("job-1", completed_job("job-1"));
    }

    void set_reference(
        std::string name,
        std::string sequence,
        domain::ReferenceAssembly assembly
    ) {
        fasta = ">" + name + "\n" + sequence + "\n";
        genome.fasta = fasta;
        manifest.contig_name = name;
        manifest.contig_length = sequence.size();

        const auto size = static_cast<std::int64_t>(fasta.size());
        files.files.insert_or_assign("ref", reference_file(size));
        plans.values["plan-1"] = plan(producer, size);
        selected_assembly = assembly;
    }

    void add_vcf(std::string id, std::string text) {
        auto file = output_file(id, static_cast<std::int64_t>(text.size()));
        files.files.emplace(std::string{file.id()}, file);
        files.artifacts.push_back({
            file,
            application::GeneratedOutputProvenance{
                .job_id = "job-1",
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
        artifacts.texts.emplace(std::string{file.id()}, std::move(text));
    }

    application::CohortAnalysisSelectionRequest request(
        std::vector<application::CohortVcfSelection> selections,
        std::vector<application::CohortReferenceAlias> aliases = {},
        std::optional<std::string> custom_id = std::nullopt
    ) const {
        return {
            .project_id = project_id,
            .cohort_id = cohort_id,
            .cohort_revision = 1U,
            .reference = {
                .managed_file_id = "ref",
                .assembly = selected_assembly,
                .custom_assembly_id = std::move(custom_id),
                .normalization_contract_version =
                    application::CohortAnalysisSelectionService::normalization_contract_v1,
                .aliases = std::move(aliases),
            },
            .selections = std::move(selections),
        };
    }

    application::CohortVcfSelection selection(
        std::string sample,
        std::string artifact,
        std::string column
    ) const {
        return {
            .project_sample_id = std::move(sample),
            .plan_id = "plan-1",
            .attempt_number = 1,
            .job_id = "job-1",
            .step_id = "variants",
            .output_port = "vcf",
            .managed_file_id = std::move(artifact),
            .expected_sha256 = vcf_hash,
            .vcf_sample_name = std::move(column),
        };
    }

    application::CohortMatrixBuildResult build(
        application::CohortAnalysisSelectionRequest request
    ) {
        application::CohortAnalysisSelectionService selection_service{
            cohorts, plans, executions, jobs, files, input, artifacts, manifest
        };
        application::CohortMatrixService service{
            selection_service, files, artifacts, genome
        };
        return service.build(request);
    }

    domain::ReferenceAssembly selected_assembly{domain::ReferenceAssembly::grch38};
};

[[nodiscard]] std::string shared_vcf(bool reverse_records = false) {
    const std::string first =
        "chr1\t20\tb\tA\tG\t.\tPASS\t.\tGT\t0/1\t1/1\n";
    const std::string second =
        "chr1\t10\ta\tA\tT\t.\tPASS\t.\tGT\t0/0\t0/1\n";
    return
        "##fileformat=VCFv4.3\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tSRC-A\tSRC-B\n" +
        (reverse_records ? first + second : second + first);
}

void projection_contract() {
    Harness h;
    h.cohorts.value = cohort({
        member("001", "bio-001", application::CohortGroup::case_group),
        member("Örnek-A", "bio-002", application::CohortGroup::control),
    });
    h.add_vcf("shared", shared_vcf());

    const auto result = h.build(h.request({
        h.selection("Örnek-A", "shared", "SRC-B"),
        h.selection("001", "shared", "SRC-A"),
    }));

    check(result.parsed_vcf_artifacts == 1U, "shared VCF was parsed more than once");
    check(result.matrix.sample_count() == 2U, "projected sample count mismatch");
    check(result.matrix.sample(0U).sample_id == "001", "leading-zero sample ID changed");
    check(result.matrix.sample(1U).sample_id == "Örnek-A", "Unicode sample ID changed");
    check(result.matrix.variant_count() == 2U, "projected variant count mismatch");

    const auto first_variant = result.matrix.variant(0U);
    check(first_variant.locus.start == 9U, "canonical locus ordering failed");
    const auto a = result.matrix.sample_index("001");
    const auto b = result.matrix.sample_index("Örnek-A");
    check(a.has_value() && b.has_value(), "projected sample index missing");
    check(result.matrix.find_observation(*a, 0U)->alternate_dosage == 0U,
          "selected SRC-A genotype was not projected");
    check(result.matrix.find_observation(*b, 0U)->alternate_dosage == 1U,
          "selected SRC-B genotype was not projected");
}

void absence_contract() {
    Harness h;
    h.cohorts.value = cohort({
        member("001", "bio-001", application::CohortGroup::case_group),
        member("002", "bio-002", application::CohortGroup::control),
    });
    h.add_vcf(
        "one",
        "##fileformat=VCFv4.3\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tA\n"
        "chr1\t10\ta\tA\tG\t.\tPASS\t.\tGT\t0/1\n"
    );
    auto second = output_file("two", 0);
    const std::string second_text =
        "##fileformat=VCFv4.3\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tB\n"
        "chr1\t20\tb\tA\tT\t.\tPASS\t.\tGT\t1/1\n";
    second = output_file("two", static_cast<std::int64_t>(second_text.size()));
    h.files.files.emplace("two", second);
    h.files.artifacts.push_back({
        second,
        application::GeneratedOutputProvenance{
            .job_id = "job-1",
            .step_id = "variants",
            .output_port = "vcf-two",
            .plugin_id = "plugin",
            .plugin_version = "0.1.0",
            .module_id = "org.biocore.vcfqc.filter",
            .file_type = "vcf",
            .relative_project_path = std::string{second.relative_project_path().value()},
            .step_progress = 1.0,
            .registered_at_utc = "registered",
        }
    });
    h.artifacts.texts.emplace("two", second_text);
    h.plans.values["plan-1"].samples[0].nodes[0].outputs.push_back({"vcf-two", "vcf"});

    auto s2 = h.selection("002", "two", "B");
    s2.output_port = "vcf-two";
    const auto result = h.build(h.request({
        h.selection("001", "one", "A"),
        std::move(s2),
    }));

    check(result.matrix.variant_count() == 2U, "sparse union size mismatch");
    const auto one = result.matrix.sample_index("001");
    const auto two = result.matrix.sample_index("002");
    check(one.has_value() && two.has_value(), "sparse sample missing");
    check(result.matrix.find_observation(*one, 1U) == nullptr,
          "absent record was converted into a genotype observation");
    check(result.matrix.find_observation(*two, 0U) == nullptr,
          "absent record was converted into a genotype observation");
}

void ordering_contract() {
    Harness h;
    h.cohorts.value = cohort({
        member("B", "bio-b", application::CohortGroup::case_group),
        member("A", "bio-a", application::CohortGroup::control),
    });
    h.add_vcf("shared", shared_vcf(true));
    const auto first = h.build(h.request({
        h.selection("B", "shared", "SRC-B"),
        h.selection("A", "shared", "SRC-A"),
    }));
    const auto second = h.build(h.request({
        h.selection("A", "shared", "SRC-A"),
        h.selection("B", "shared", "SRC-B"),
    }));
    check(first.matrix.sample(0U).sample_id == "A" &&
          second.matrix.sample(0U).sample_id == "A",
          "matrix sample ordering depends on request order");
    check(first.matrix.variant(0U).locus.start == 9U &&
          second.matrix.variant(0U).locus.start == 9U,
          "matrix allele ordering depends on VCF/request order");
}

void allele_gate_contract() {
    Harness h;
    h.add_vcf(
        "mnv",
        "##fileformat=VCFv4.3\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tSRC\n"
        "chr1\t10\tm\tAA\tGG\t.\tPASS\t.\tGT\t0/1\n"
    );
    rejects<std::invalid_argument>([&] {
        static_cast<void>(h.build(h.request({h.selection("001", "mnv", "SRC")})));
    });

    const std::string long_alt = "A" + std::string(51U, 'C');
    h.files.artifacts.clear();
    h.files.files.erase("mnv");
    h.artifacts.texts.clear();
    h.add_vcf(
        "long",
        "##fileformat=VCFv4.3\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tSRC\n"
        "chr1\t30\tl\tA\t" + long_alt + "\t.\tPASS\t.\tGT\t0/1\n"
    );
    rejects<std::invalid_argument>([&] {
        static_cast<void>(h.build(h.request({h.selection("001", "long", "SRC")})));
    });
}

void budget_contract() {
    application::validate_cohort_matrix_budget(100U, 100U, 10000U, 1000000U);
    rejects<std::length_error>([] {
        application::validate_cohort_matrix_budget(101U, 100U, 10000U, 1000000U);
    });
    rejects<std::length_error>([] {
        application::validate_cohort_matrix_budget(100U, 101U, 10000U, 1000000U);
    });
    rejects<std::length_error>([] {
        application::validate_cohort_matrix_budget(100U, 100U, 10001U, 1000000U);
    });
    rejects<std::length_error>([] {
        application::validate_cohort_matrix_budget(100U, 100U, 10000U, 1000001U);
    });
}

void alias_contract() {
    Harness h;
    h.set_reference("chrCustom", std::string(256U, 'A'), domain::ReferenceAssembly::custom);
    h.add_vcf(
        "custom",
        "##fileformat=VCFv4.3\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tSRC\n"
        "alias1\t10\ta\tA\tG\t.\tPASS\t.\tGT\t0/1\n"
    );
    const auto result = h.build(h.request(
        {h.selection("001", "custom", "SRC")},
        {{"alias1", "chrCustom"}},
        std::string{"custom-assembly"}
    ));
    check(result.matrix.variant_count() == 1U,
          "explicit custom contig alias was not applied");
}

void reference_drift_contract() {
    Harness h;
    h.add_vcf(
        "one",
        "##fileformat=VCFv4.3\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tSRC\n"
        "chr1\t10\ta\tA\tG\t.\tPASS\t.\tGT\t0/1\n"
    );
    h.genome.drift = true;
    rejects<std::runtime_error>([&] {
        static_cast<void>(h.build(h.request({h.selection("001", "one", "SRC")})));
    });
}

class TempProject final {
public:
    TempProject()
        : root{std::filesystem::temp_directory_path() /
               ("biocore-cohort-092-" + std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count()))} {
        std::filesystem::create_directories(root / "inputs");
        std::filesystem::create_directories(root / ".biocore" / "runtime");
    }

    ~TempProject() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    std::filesystem::path root;
};

void genome_reader_contract() {
    TempProject temp;
    const auto source = temp.root / "source.fa";
    {
        std::ofstream output{source, std::ios::binary};
        output << ">custom\nACGTACGT\n";
    }

    const auto canonical_root = std::filesystem::canonical(temp.root).generic_string();
    infrastructure::FilesystemInputFileStorage storage{canonical_root};
    auto transaction = storage.prepare_managed_copy(
        source.generic_string(), "ref-092"
    );
    const auto prepared = transaction->prepared_file();
    domain::ManagedFile managed{
        "ref-092", prepared.display_name, domain::StorageMode::managed_copy,
        prepared.original_path, prepared.managed_path, prepared.relative_project_path,
        "fasta", prepared.size_bytes, std::nullopt,
        prepared.checksum_algorithm, prepared.checksum_value, "created", "updated"
    };
    transaction->commit();

    infrastructure::FilesystemReferenceGenomeReader reader{storage};
    const auto loaded = reader.read_verified_genome(
        managed, domain::ReferenceAssembly::custom, 1024U
    );
    check(loaded.status == application::ReferenceGenomeReadStatus::verified &&
          loaded.genome.has_value() &&
          loaded.verified_sha256 == managed.checksum_value(),
          "filesystem reference genome was not verified");
    const auto contig = loaded.genome->contigs().resolve("custom");
    check(contig.has_value() && loaded.genome->base(*contig, 2U) == std::optional<char>{'G'},
          "filesystem reference genome content is incorrect");

    {
        std::ofstream output{*managed.managed_path(), std::ios::binary | std::ios::app};
        output << "A";
    }
    const auto changed = reader.read_verified_genome(
        managed, domain::ReferenceAssembly::custom, 1024U
    );
    check(changed.status == application::ReferenceGenomeReadStatus::integrity_unverified,
          "tampered managed FASTA remained loadable");
}

void dispatch_contract() {
    Harness h;
    h.cohorts.value = cohort({
        member("B", "bio-b", application::CohortGroup::case_group),
        member("A", "bio-a", application::CohortGroup::control),
    });
    h.add_vcf("shared", shared_vcf());
    const auto result = h.build(h.request({
        h.selection("B", "shared", "SRC-B"),
        h.selection("A", "shared", "SRC-A"),
    }));
    check(result.dispatch.native_module_id == "org.biocore.cohort.matrix",
          "matrix native dispatch module identity changed");
    check(result.dispatch.stage_id == "matrix", "matrix dispatch stage identity changed");
    check(result.dispatch.sources.size() == 2U &&
          result.dispatch.sources[0].project_sample_id == "A" &&
          result.dispatch.sources[1].project_sample_id == "B",
          "matrix dispatch source order is nondeterministic");
    check(result.dispatch.maximum_normalized_alleles == 10000U &&
          result.dispatch.maximum_observations == 1000000U,
          "matrix dispatch resource contract drifted");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) return EXIT_FAILURE;
        const std::string_view mode{argv[1]};
        if (mode == "projection") projection_contract();
        else if (mode == "absence") absence_contract();
        else if (mode == "ordering") ordering_contract();
        else if (mode == "allele-gate") allele_gate_contract();
        else if (mode == "budget") budget_contract();
        else if (mode == "alias") alias_contract();
        else if (mode == "reference-drift") reference_drift_contract();
        else if (mode == "genome-reader") genome_reader_contract();
        else if (mode == "dispatch") dispatch_contract();
        else return EXIT_FAILURE;
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
