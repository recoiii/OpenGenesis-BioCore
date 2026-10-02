#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/cohort_analysis_approval_service.hpp"
#include "biocore/application/i_cohort_analysis_snapshot_store.hpp"
#include "biocore/application/i_cohort_matrix_builder.hpp"
#include "biocore/application/i_cohort_registry_store.hpp"
#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_result_artifact_reader.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/generated_output_artifact.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/reference_genome.hpp"
#include "biocore/domain/vcf_ingestion.hpp"
#include "biocore/infrastructure/sha256_cohort_analysis_digester.hpp"

namespace {
using namespace biocore;

constexpr const char* project_id = "p-001";
constexpr const char* cohort_id = "c-093";
constexpr const char* ref_hash =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
constexpr const char* vcf_hash =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr const char* qc_hash =
    "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

[[nodiscard]] bool has_issue(
    const application::CohortAnalysisApprovalPreview& preview,
    const std::string_view code,
    const std::optional<bool> blocking = std::nullopt
) {
    for (const auto& issue : preview.issues) {
        if (issue.code == code &&
            (!blocking.has_value() || issue.blocking == *blocking)) return true;
    }
    return false;
}

template<class Exception, class Function>
void rejects(Function function) {
    try { function(); } catch (const Exception&) { return; }
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
        if (!value.has_value() || value->project_id != project || value->cohort_id != cohort) {
            return std::nullopt;
        }
        if (revision.has_value() && value->revision.revision != *revision) return std::nullopt;
        return value;
    }

    std::vector<application::CohortDefinition> list(std::string_view) override {
        return value.has_value()
            ? std::vector<application::CohortDefinition>{*value}
            : std::vector<application::CohortDefinition>{};
    }

    std::optional<application::CohortDefinition> value;
};

class MatrixBuilder final : public application::ICohortMatrixBuilder {
public:
    application::CohortMatrixBuildResult build(
        const application::CohortAnalysisSelectionRequest&
    ) override {
        ++calls;
        if (failure.has_value()) throw std::runtime_error{*failure};
        return result;
    }

    application::CohortMatrixBuildResult result;
    std::optional<std::string> failure;
    std::size_t calls{0U};
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
        std::string_view
    ) override { return std::nullopt; }
    std::vector<domain::ManagedFile> list() override { return {}; }

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

class Reader final : public application::IResultArtifactReader {
public:
    application::ResultArtifactText read_verified_text(
        const application::GeneratedOutputArtifact& artifact,
        std::size_t maximum_bytes
    ) override {
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
};

class Snapshots final : public application::ICohortAnalysisSnapshotStore {
public:
    application::CohortAnalysisStoreResult create(
        const application::CohortAnalysisSnapshot& snapshot
    ) override {
        if (conflicts_remaining > 0U) {
            --conflicts_remaining;
            return application::CohortAnalysisStoreResult::analysis_id_conflict;
        }
        const auto key = snapshot.project_id + "/" + snapshot.analysis_id;
        if (values.contains(key)) {
            return application::CohortAnalysisStoreResult::analysis_id_conflict;
        }
        values.emplace(key, snapshot);
        return application::CohortAnalysisStoreResult::stored;
    }

    std::optional<application::CohortAnalysisSnapshot> find(
        std::string_view project,
        std::string_view analysis
    ) override {
        const auto found = values.find(std::string{project} + "/" + std::string{analysis});
        return found == values.end() ? std::nullopt
                                     : std::optional<application::CohortAnalysisSnapshot>{found->second};
    }

    std::map<std::string, application::CohortAnalysisSnapshot, std::less<>> values;
    std::size_t conflicts_remaining{0U};
};

class Ids final : public application::IIdGenerator {
public:
    explicit Ids(std::vector<std::string> values) : values_{std::move(values)} {}
    std::string generate() override {
        if (index_ >= values_.size()) throw std::runtime_error{"ID fixture exhausted"};
        return values_[index_++];
    }
private:
    std::vector<std::string> values_;
    std::size_t index_{0U};
};

class Clock final : public application::IUtcClock {
public:
    std::string now_utc_iso8601() override { return "2026-10-02T15:45:00Z"; }
};

[[nodiscard]] application::CohortMemberSnapshot member(
    std::string id,
    application::CohortGroup group,
    application::CohortMemberDisposition disposition =
        application::CohortMemberDisposition::included,
    std::optional<std::string> reason = std::nullopt
) {
    return {
        .sample_id = id,
        .sample_display_name = "display-" + id,
        .sample_group_metadata = "legacy",
        .biological_unit_id = "bio-" + id,
        .group = group,
        .disposition = disposition,
        .exclusion_reason = std::move(reason),
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
        .created_at_utc = "2026-10-02T15:00:00Z",
        .members = std::move(members),
    };
    return {
        .project_id = project_id,
        .cohort_id = cohort_id,
        .name = "Cohort 093",
        .current_revision = 1U,
        .created_at_utc = "2026-10-02T15:00:00Z",
        .updated_at_utc = "2026-10-02T15:00:00Z",
        .revision = std::move(revision),
    };
}

[[nodiscard]] domain::ManagedFile qc_file(std::string id, std::int64_t size) {
    const auto path = "outputs/" + id + ".json";
    return {
        std::move(id), "summary.json", domain::StorageMode::generated_output,
        std::nullopt, "/project/" + path, path, "json", size, std::nullopt,
        std::string{"sha256"}, std::string{qc_hash}, "created", "updated"
    };
}

void add_qc(
    Files& files,
    Reader& reader,
    const std::string& job,
    const std::string& id
) {
    const std::string text =
        "{\"schemaVersion\":1,\"module\":\"org.biocore.vcfqc.filter\","
        "\"metrics\":{\"totalRecords\":2}}";
    auto file = qc_file(id, static_cast<std::int64_t>(text.size()));
    files.files.emplace(std::string{file.id()}, file);
    files.artifacts.push_back({
        file,
        application::GeneratedOutputProvenance{
            .job_id = job,
            .step_id = "variants",
            .output_port = "summary",
            .plugin_id = "plugin",
            .plugin_version = "0.1.0",
            .module_id = "org.biocore.vcfqc.filter",
            .file_type = "json",
            .relative_project_path = std::string{file.relative_project_path().value()},
            .step_progress = 1.0,
            .registered_at_utc = "registered",
        }
    });
    reader.texts.emplace(id, text);
}

[[nodiscard]] domain::MultiSampleMatrix matrix_fixture() {
    std::istringstream fasta{
        ">chr1\n"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
    };
    auto genome = domain::ReferenceGenome::from_fasta(
        fasta, domain::ReferenceAssembly::grch38
    );
    const domain::ReferenceAssemblyIdentity assembly{
        domain::ReferenceAssembly::grch38, {}
    };

    std::istringstream case_vcf{
        "##fileformat=VCFv4.3\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tcase-1\n"
        "chr1\t10\tv1\tA\tG\t.\tPASS\t.\tGT\t0/1\n"
        "chr1\t20\tv2\tA\tT\t.\tPASS\t.\tGT\t./.\n"
    };
    std::istringstream control_vcf{
        "##fileformat=VCFv4.3\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tcontrol-1\n"
        "chr1\t10\tv1c\tA\tG\t.\tPASS\t.\tGT\t0/0\n"
        "chr1\t30\tv3\tA\tC\t.\tPASS\t.\tGT\t0/1\n"
    };
    auto case_data = domain::ingest_vcf(case_vcf, genome);
    auto control_data = domain::ingest_vcf(control_vcf, genome);
    domain::ContigTableBuilder target{domain::ReferenceAssembly::grch38};
    target.add_contig("1", {"chr1"});

    return domain::build_multi_sample_matrix(
        assembly,
        target.build(),
        {
            {"case-source", assembly, &genome.contigs(), &case_data},
            {"control-source", assembly, &genome.contigs(), &control_data},
        },
        {
            .maximum_sources = 100U,
            .maximum_samples = 100U,
            .maximum_variant_alleles = 10000U,
            .maximum_observations = 1000000U,
        }
    );
}

[[nodiscard]] application::CohortPinnedVcfSource source(
    std::string sample,
    std::string job,
    std::string file
) {
    return {
        .project_sample_id = sample,
        .biological_unit_id = "bio-" + sample,
        .plan_id = "plan-1",
        .workflow_id = "workflow-1",
        .producer_sample_id = sample,
        .attempt_number = 1,
        .job_id = std::move(job),
        .step_id = "variants",
        .output_port = "vcf",
        .module_id = "org.biocore.vcfqc.filter",
        .plugin_version = "0.1.0",
        .managed_file_id = std::move(file),
        .size_bytes = 100,
        .sha256 = vcf_hash,
        .vcf_sample_name = sample,
    };
}

[[nodiscard]] application::CohortMatrixBuildResult matrix_build() {
    application::CohortAnalysisSelectionPreview selection{
        .project_id = project_id,
        .cohort_id = cohort_id,
        .cohort_revision = 1U,
        .ready = true,
        .reference = application::CohortPinnedReference{
            .managed_file_id = "ref",
            .file_type = "fasta",
            .size_bytes = 100,
            .sha256 = ref_hash,
            .assembly = domain::ReferenceAssembly::grch38,
            .custom_assembly_id = std::nullopt,
            .normalization_contract_version = "biocore.normalized-allele.v1",
            .contigs = {{"chr1", 64U}},
            .aliases = {{"chr1", "1"}},
        },
        .sources = {
            source("case-1", "job-case", "vcf-case"),
            source("control-1", "job-control", "vcf-control"),
        },
        .issues = {},
        .total_vcf_bytes = 200U,
    };
    application::CohortMatrixDispatchPlan dispatch{
        .project_id = project_id,
        .cohort_id = cohort_id,
        .cohort_revision = 1U,
        .stage_id = "matrix",
        .native_module_id = "org.biocore.cohort.matrix",
        .normalization_contract_version = "biocore.normalized-allele.v1",
        .reference_file_id = "ref",
        .reference_sha256 = ref_hash,
        .sources = {
            {"case-1", "vcf-case", vcf_hash, "case-1"},
            {"control-1", "vcf-control", vcf_hash, "control-1"},
        },
        .maximum_samples = 100U,
        .maximum_sources = 100U,
        .maximum_normalized_alleles = 10000U,
        .maximum_observations = 1000000U,
    };
    return {
        .selection = std::move(selection),
        .dispatch = std::move(dispatch),
        .matrix = matrix_fixture(),
        .parsed_vcf_artifacts = 2U,
    };
}

[[nodiscard]] application::CohortAnalysisSelectionRequest selection_request() {
    return {
        .project_id = project_id,
        .cohort_id = cohort_id,
        .cohort_revision = 1U,
        .reference = {
            .managed_file_id = "ref",
            .assembly = domain::ReferenceAssembly::grch38,
            .custom_assembly_id = std::nullopt,
            .normalization_contract_version = "biocore.normalized-allele.v1",
            .aliases = {{"chr1", "1"}},
        },
        .selections = {},
    };
}

[[nodiscard]] application::CohortAnalysisApprovalRequest valid_request() {
    return {
        .selection = selection_request(),
        .qc_decisions = {
            {"case-1", application::CohortAnalysisDisposition::included, std::nullopt},
            {"control-1", application::CohortAnalysisDisposition::included, std::nullopt},
        },
        .association_options = {
            .minimum_complete_case_calls = 1U,
            .minimum_complete_control_calls = 1U,
            .maximum_fisher_table_states = 100000U,
        },
    };
}

struct Harness final {
    Cohorts cohorts;
    MatrixBuilder matrix;
    Files files;
    Reader reader;
    Snapshots snapshots;
    infrastructure::Sha256CohortAnalysisDigester digester;
    Ids ids{{"analysis-1", "analysis-2", "analysis-3"}};
    Clock clock;

    Harness() {
        cohorts.value = cohort({
            member("case-1", application::CohortGroup::case_group),
            member("control-1", application::CohortGroup::control),
        });
        matrix.result = matrix_build();
        add_qc(files, reader, "job-case", "qc-case");
        add_qc(files, reader, "job-control", "qc-control");
    }

    application::CohortAnalysisApprovalService service() {
        return {
            matrix, cohorts, files, reader, snapshots, digester, ids, clock
        };
    }
};

void valid_contract() {
    Harness h;
    auto service = h.service();
    const auto preview = service.preview(valid_request());
    check(preview.ready, "valid analysis preview rejected");
    check(preview.draft.approved_case_samples == 1U &&
          preview.draft.approved_control_samples == 1U,
          "approved group totals wrong");
    check(preview.draft.test_universe.size() == 3U,
          "test universe size wrong");
    check(preview.draft.allele_family_size == 1U &&
          preview.draft.carrier_family_size == 1U,
          "complete-call family size wrong");
    check(preview.draft.test_universe[1].case_no_calls == 1U &&
          preview.draft.test_universe[1].control_unobserved == 1U,
          "no-call/unobserved denominator semantics changed");

    const auto approved = service.approve({
        .preview_request = valid_request(),
        .expected_preview_digest = preview.preview_digest,
    });
    check(approved.analysis_id == "analysis-1", "analysis id changed");
    check(approved.snapshot_digest.size() == 64U &&
          approved.preview_digest == preview.preview_digest,
          "approval digests missing");
    check(h.snapshots.values.size() == 1U, "approved snapshot not persisted");

    h.matrix.result.dispatch.sources[0].sha256 =
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    const auto drifted_dispatch = service.preview(valid_request());
    check(!drifted_dispatch.ready &&
          has_issue(drifted_dispatch, "matrix_dispatch_mismatch", true),
          "drifted matrix dispatch source was accepted");
}

void empty_contract() {
    Harness h;
    h.cohorts.value = cohort({});
    auto service = h.service();
    const auto preview = service.preview(valid_request());
    check(!preview.ready && has_issue(preview, "empty_cohort", true),
          "empty cohort was not blocked");
    check(h.matrix.calls == 0U, "empty cohort unnecessarily built a matrix");
}

void all_registry_excluded_contract() {
    Harness h;
    h.cohorts.value = cohort({
        member("case-1", application::CohortGroup::case_group,
               application::CohortMemberDisposition::excluded, "registry QC"),
        member("control-1", application::CohortGroup::control,
               application::CohortMemberDisposition::excluded, "registry QC"),
    });
    auto request = valid_request();
    request.qc_decisions.clear();
    auto service = h.service();
    const auto preview = service.preview(request);
    check(!preview.ready && has_issue(preview, "all_excluded", true),
          "all-registry-excluded cohort was not blocked");
    check(h.matrix.calls == 0U, "all-excluded cohort unnecessarily built a matrix");
}

void missing_qc_contract() {
    Harness h;
    h.files.artifacts.erase(
        h.files.artifacts.begin(),
        h.files.artifacts.begin() + 1
    );
    h.reader.texts.erase("qc-case");
    auto service = h.service();

    auto request = valid_request();
    auto blocked = service.preview(request);
    check(!blocked.ready &&
          has_issue(blocked, "qc_unavailable", false) &&
          has_issue(blocked, "qc_acknowledgement_required", true),
          "missing QC was hidden or silently accepted");

    request.qc_decisions[0].reason = "QC unavailable reviewed before inclusion";
    auto acknowledged = service.preview(request);
    check(acknowledged.ready &&
          has_issue(acknowledged, "qc_unavailable", false) &&
          acknowledged.draft.samples[0].qc.state ==
              application::CohortQcEvidenceState::unavailable,
          "explicit missing-QC acknowledgement did not preserve unavailable state");

    add_qc(h.files, h.reader, "job-case", "qc-case");
    h.reader.texts["qc-case"] =
        "{\"schemaVersion\":1,\"module\":\"org.biocore.vcfqc.filter\","
        "\"metrics\":{";
    auto malformed_request = valid_request();
    const auto malformed = service.preview(malformed_request);
    check(!malformed.ready &&
          has_issue(malformed, "qc_unavailable", false) &&
          has_issue(malformed, "qc_acknowledgement_required", true),
          "malformed QC JSON was treated as verified evidence");
}

void group_contract() {
    Harness h;
    h.cohorts.value = cohort({
        member("case-1", application::CohortGroup::case_group),
        member("control-1", application::CohortGroup::case_group),
    });
    auto service = h.service();
    const auto no_control = service.preview(valid_request());
    check(!no_control.ready && has_issue(no_control, "missing_control_group", true),
          "missing control denominator was not blocked");

    h.cohorts.value = cohort({
        member("case-1", application::CohortGroup::case_group),
        member("control-1", application::CohortGroup::unassigned),
    });
    const auto unassigned = service.preview(valid_request());
    check(!unassigned.ready &&
          has_issue(unassigned, "unassigned_group", true) &&
          has_issue(unassigned, "missing_control_group", true),
          "unassigned group entered association denominator");
}

void qc_all_excluded_contract() {
    Harness h;
    auto request = valid_request();
    request.qc_decisions[0] = {
        "case-1", application::CohortAnalysisDisposition::excluded,
        std::string{"QC exclusion"}
    };
    request.qc_decisions[1] = {
        "control-1", application::CohortAnalysisDisposition::excluded,
        std::string{"QC exclusion"}
    };
    auto service = h.service();
    const auto preview = service.preview(request);
    check(!preview.ready &&
          has_issue(preview, "all_excluded", true) &&
          has_issue(preview, "missing_case_group", true) &&
          has_issue(preview, "missing_control_group", true),
          "QC all-excluded state was not blocked");
}

void stale_digest_contract() {
    Harness h;
    auto service = h.service();
    const auto first = service.preview(valid_request());
    check(first.ready, "initial preview not ready");

    auto changed = valid_request();
    changed.qc_decisions[0].reason = "Additional manual review note";
    try {
        static_cast<void>(service.approve({
            .preview_request = changed,
            .expected_preview_digest = first.preview_digest,
        }));
        throw std::runtime_error{"stale preview digest accepted"};
    } catch (const application::CohortAnalysisApprovalError& error) {
        check(error.code() == application::CohortAnalysisApprovalErrorCode::stale_preview,
              "stale preview returned wrong error");
    }
    check(h.snapshots.values.empty(), "stale approval wrote a snapshot");
}

void deterministic_digest_contract() {
    Harness h;
    auto service = h.service();
    auto first_request = valid_request();
    auto second_request = valid_request();
    std::reverse(second_request.qc_decisions.begin(), second_request.qc_decisions.end());
    const auto first = service.preview(first_request);
    const auto second = service.preview(second_request);
    check(first.ready && second.ready &&
          first.preview_digest == second.preview_digest,
          "preview digest depends on QC decision input order");
}

void family_gate_contract() {
    Harness h;
    auto request = valid_request();
    request.association_options.minimum_complete_case_calls = 2U;
    request.association_options.minimum_complete_control_calls = 2U;
    auto service = h.service();
    const auto preview = service.preview(request);
    check(preview.ready &&
          preview.draft.allele_family_size == 0U &&
          preview.draft.carrier_family_size == 0U,
          "minimum-complete-call gate did not freeze an empty test family");
}

void id_retry_contract() {
    Harness h;
    h.snapshots.conflicts_remaining = 1U;
    auto service = h.service();
    const auto preview = service.preview(valid_request());
    const auto approved = service.approve({
        .preview_request = valid_request(),
        .expected_preview_digest = preview.preview_digest,
    });
    check(approved.analysis_id == "analysis-2" &&
          h.snapshots.values.size() == 1U,
          "analysis id conflict did not retry deterministically");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) return EXIT_FAILURE;
        const std::string_view mode{argv[1]};
        if (mode == "valid") valid_contract();
        else if (mode == "empty") empty_contract();
        else if (mode == "registry-excluded") all_registry_excluded_contract();
        else if (mode == "missing-qc") missing_qc_contract();
        else if (mode == "groups") group_contract();
        else if (mode == "qc-excluded") qc_all_excluded_contract();
        else if (mode == "stale-digest") stale_digest_contract();
        else if (mode == "deterministic-digest") deterministic_digest_contract();
        else if (mode == "family-gate") family_gate_contract();
        else if (mode == "id-retry") id_retry_contract();
        else return EXIT_FAILURE;
        std::cout << mode << " PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
