#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/cohort_case_control_analysis_service.hpp"
#include "biocore/application/i_cohort_analysis_snapshot_store.hpp"
#include "biocore/domain/reference_genome.hpp"
#include "biocore/domain/vcf_ingestion.hpp"

namespace {
using namespace biocore;

void check(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

template<class Function>
void expect_code(
    Function function,
    const application::CohortCaseControlAnalysisErrorCode expected
) {
    try {
        function();
    } catch (const application::CohortCaseControlAnalysisError& error) {
        check(error.code() == expected, "unexpected cohort association error code");
        return;
    }
    throw std::runtime_error{"expected cohort association error"};
}

class Snapshots final : public application::ICohortAnalysisSnapshotStore {
public:
    application::CohortAnalysisStoreResult create(
        const application::CohortAnalysisSnapshot& snapshot
    ) override {
        values.insert_or_assign(snapshot.project_id + "/" + snapshot.analysis_id, snapshot);
        return application::CohortAnalysisStoreResult::stored;
    }

    std::optional<application::CohortAnalysisSnapshot> find(
        std::string_view project_id,
        std::string_view analysis_id
    ) override {
        const auto found = values.find(std::string{project_id} + "/" + std::string{analysis_id});
        return found == values.end() ? std::nullopt
                                     : std::optional<application::CohortAnalysisSnapshot>{found->second};
    }

    std::map<std::string, application::CohortAnalysisSnapshot, std::less<>> values;
};

[[nodiscard]] domain::VcfIngestionResult ingest_one(
    const std::string& sample_id,
    const std::vector<std::pair<std::uint64_t, std::string>>& calls,
    const domain::ReferenceGenome& genome,
    const bool drift = false
) {
    std::ostringstream vcf;
    vcf << "##fileformat=VCFv4.3\n"
           "##FORMAT=<ID=GT,Number=1,Type=String,Description=\"Genotype\">\n"
           "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\t"
        << sample_id << '\n';
    for (const auto& [position, genotype] : calls) {
        const char alternate = (drift && position == 10U) ? 'C' :
                               (position == 20U ? 'T' : (position == 30U ? 'C' : 'G'));
        vcf << "chr1\t" << position << "\tv" << position
            << "\tA\t" << alternate << "\t.\tPASS\t.\tGT\t" << genotype << '\n';
    }
    std::istringstream input{vcf.str()};
    return domain::ingest_vcf(input, genome);
}

[[nodiscard]] domain::MultiSampleMatrix matrix_fixture(const bool drift = false) {
    std::istringstream fasta{
        ">chr1\n"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
    };
    auto genome = domain::ReferenceGenome::from_fasta(fasta, domain::ReferenceAssembly::grch38);
    const domain::ReferenceAssemblyIdentity assembly{domain::ReferenceAssembly::grch38, {}};

    auto case1 = ingest_one("case-1", {{10U,"0/1"},{20U,"0/1"},{30U,"1/1"}}, genome, drift);
    auto case2 = ingest_one("case-2", {{10U,"1/1"},{20U,"./."},{30U,"1/1"}}, genome, drift);
    auto control1 = ingest_one("control-1", {{10U,"0/0"},{20U,"0/."},{30U,"0/0"}}, genome, drift);
    auto control2 = ingest_one("control-2", {{10U,"0/1"},{30U,"0/0"}}, genome, drift);

    domain::ContigTableBuilder target{domain::ReferenceAssembly::grch38};
    target.add_contig("1", {"chr1"});
    return domain::build_multi_sample_matrix(
        assembly,
        target.build(),
        {
            {"case-1", assembly, &genome.contigs(), &case1},
            {"case-2", assembly, &genome.contigs(), &case2},
            {"control-1", assembly, &genome.contigs(), &control1},
            {"control-2", assembly, &genome.contigs(), &control2},
        },
        {
            .maximum_sources = 100U,
            .maximum_samples = 100U,
            .maximum_variant_alleles = 10000U,
            .maximum_observations = 1000000U,
        }
    );
}

struct SampleConfig final {
    std::string id;
    application::CohortGroup group;
    application::CohortAnalysisDisposition disposition{application::CohortAnalysisDisposition::included};
};

[[nodiscard]] application::CohortAnalysisSnapshot snapshot_fixture(
    const domain::MultiSampleMatrix& matrix,
    std::vector<SampleConfig> samples = {
        {"case-1", application::CohortGroup::case_group},
        {"case-2", application::CohortGroup::case_group},
        {"control-1", application::CohortGroup::control},
        {"control-2", application::CohortGroup::control},
    },
    application::CohortAssociationApprovalOptions options = {}
) {
    application::CohortAnalysisSnapshot snapshot;
    snapshot.analysis_id = "analysis-094";
    snapshot.project_id = "p-001";
    snapshot.cohort_id = "c-094";
    snapshot.cohort_revision = 1U;
    snapshot.preview_digest = std::string(64U, 'a');
    snapshot.snapshot_digest = std::string(64U, 'b');
    snapshot.approved_at_utc = "2026-10-02T17:00:00Z";
    snapshot.reference.managed_file_id = "ref";
    snapshot.reference.file_type = "fasta";
    snapshot.reference.size_bytes = 64;
    snapshot.reference.sha256 = std::string(64U, 'c');
    snapshot.reference.assembly = domain::ReferenceAssembly::grch38;
    snapshot.reference.normalization_contract_version = "biocore.normalized-allele.v1";
    snapshot.reference.contigs = {{"1", 64U}};
    snapshot.association_options = options;

    std::vector<std::string> included_ids;
    std::vector<domain::CaseControlSampleLabel> labels;
    for (std::size_t index = 0U; index < samples.size(); ++index) {
        const auto& config = samples[index];
        snapshot.samples.push_back({
            .ordinal = index,
            .sample_id = config.id,
            .sample_display_name = "display-" + config.id,
            .sample_group_metadata = "metadata-not-used-as-phenotype",
            .biological_unit_id = "bio-" + config.id,
            .group = config.group,
            .cohort_disposition = application::CohortMemberDisposition::included,
            .cohort_exclusion_reason = std::nullopt,
            .analysis_disposition = config.disposition,
            .analysis_reason = config.disposition == application::CohortAnalysisDisposition::excluded
                ? std::optional<std::string>{"manual QC exclusion"} : std::nullopt,
            .qc = {
                .state = application::CohortQcEvidenceState::unavailable,
                .reason = "test fixture",
                .managed_file_id = std::nullopt,
                .job_id = std::nullopt,
                .step_id = std::nullopt,
                .output_port = std::nullopt,
                .module_id = std::nullopt,
                .plugin_version = std::nullopt,
                .size_bytes = std::nullopt,
                .sha256 = std::nullopt,
            },
        });
        snapshot.sources.push_back({
            .project_sample_id = config.id,
            .biological_unit_id = "bio-" + config.id,
            .plan_id = "plan",
            .workflow_id = "workflow",
            .producer_sample_id = config.id,
            .attempt_number = 1,
            .job_id = "job-" + config.id,
            .step_id = "variants",
            .output_port = "vcf",
            .module_id = "org.biocore.vcfqc.filter",
            .plugin_version = "0.1.0",
            .managed_file_id = "vcf-" + config.id,
            .size_bytes = 100,
            .sha256 = std::string(64U, 'd'),
            .vcf_sample_name = config.id,
        });
        if (config.disposition == application::CohortAnalysisDisposition::included) {
            included_ids.push_back(config.id);
            if (config.group == application::CohortGroup::case_group) {
                labels.push_back({config.id, domain::CaseControlPhenotype::case_sample});
                ++snapshot.approved_case_samples;
            } else if (config.group == application::CohortGroup::control) {
                labels.push_back({config.id, domain::CaseControlPhenotype::control});
                ++snapshot.approved_control_samples;
            }
        }
    }

    if (snapshot.approved_case_samples == 0U || snapshot.approved_control_samples == 0U) {
        return snapshot;
    }
    const auto projected = domain::project_multi_sample_matrix_samples(matrix, included_ids);
    const auto association = domain::analyze_case_control(
        projected,
        labels,
        {
            .maximum_labels = 100U,
            .maximum_variants = 10000U,
            .minimum_complete_case_calls = options.minimum_complete_case_calls,
            .minimum_complete_control_calls = options.minimum_complete_control_calls,
            .maximum_fisher_table_states = options.maximum_fisher_table_states,
        }
    );
    for (std::size_t index = 0U; index < matrix.variant_count(); ++index) {
        const auto& variant = matrix.variant(index);
        const auto contig = matrix.contigs().canonical_name(variant.locus.contig_id);
        const auto& value = association.variants[index];
        snapshot.test_universe.push_back({
            .ordinal = index,
            .contig = std::string{*contig},
            .start = variant.locus.start,
            .end = variant.locus.end,
            .reference = variant.reference.sequence,
            .alternate = variant.alternate.sequence,
            .case_unobserved = value.case_unobserved,
            .control_unobserved = value.control_unobserved,
            .case_no_calls = value.case_no_calls,
            .control_no_calls = value.control_no_calls,
            .case_partial_calls = value.case_partial_calls,
            .control_partial_calls = value.control_partial_calls,
            .case_complete_calls = value.case_complete_calls,
            .control_complete_calls = value.control_complete_calls,
            .allele_family_member = value.allele_fisher_two_sided_p.has_value(),
            .carrier_family_member = value.carrier_fisher_two_sided_p.has_value(),
        });
        if (value.allele_fisher_two_sided_p.has_value()) ++snapshot.allele_family_size;
        if (value.carrier_fisher_two_sided_p.has_value()) ++snapshot.carrier_family_size;
    }
    return snapshot;
}

[[nodiscard]] application::CohortCaseControlAnalysisResult analyze(
    const application::CohortAnalysisSnapshot& snapshot,
    const domain::MultiSampleMatrix& matrix
) {
    Snapshots store;
    store.create(snapshot);
    application::CohortCaseControlAnalysisService service{store};
    return service.analyze(snapshot.project_id, snapshot.analysis_id, matrix);
}

void fixed_contract() {
    const auto matrix = matrix_fixture();
    const auto result = analyze(snapshot_fixture(matrix), matrix);
    check(result.variants.size() == 3U, "094 variant cardinality");
    check(result.case_group == "case" && result.control_group == "control", "group direction metadata");
    check(result.statistical_test == "probability-ordered-two-sided-fisher.v1", "Fisher method metadata");
    check(result.multiple_testing_method == "benjamini-hochberg-separate-by-model.v1", "BH method metadata");
    const auto& v = result.variants[0];
    check(v.allele.table == domain::AssociationContingencyTable{3U,1U,1U,3U}, "hand-counted allele table");
    check(v.carrier.table == domain::AssociationContingencyTable{2U,0U,1U,1U}, "hand-counted carrier table");
    check(v.allele.odds_ratio.kind == domain::AssociationOddsRatioKind::finite &&
          std::abs(v.allele.odds_ratio.value - 9.0) < 1e-12, "hand-counted allele OR");
    check(v.effective_case_samples == 2U && v.effective_control_samples == 2U, "effective sample counts");
}

void missingness_contract() {
    const auto matrix = matrix_fixture();
    const auto result = analyze(snapshot_fixture(matrix), matrix);
    const auto& v = result.variants[1];
    check(v.case_unobserved == 0U && v.case_no_calls == 1U && v.case_partial_calls == 0U &&
          v.effective_case_samples == 1U, "case missingness decomposition");
    check(v.control_unobserved == 1U && v.control_no_calls == 0U && v.control_partial_calls == 1U &&
          v.effective_control_samples == 0U, "control missingness decomposition");
    check(!v.allele.fisher_two_sided_p.has_value() && !v.allele.bh_adjusted_q.has_value(),
          "non-testable missingness row invented significance");
    check(result.no_call_policy == "complete-calls-only.v1", "no-call policy metadata");
}

void zero_cell_contract() {
    const auto matrix = matrix_fixture();
    const auto result = analyze(snapshot_fixture(matrix), matrix);
    const auto& v = result.variants[2];
    check(v.allele.table == domain::AssociationContingencyTable{4U,0U,0U,4U}, "zero-cell table changed");
    check(v.allele.odds_ratio.kind == domain::AssociationOddsRatioKind::positive_infinity,
          "zero-cell OR was corrected or hidden");
    check(!v.allele.odds_ratio_ci95.has_value(), "zero-cell CI should be unavailable");
    check(v.allele.fisher_two_sided_p.has_value() && v.allele.bh_adjusted_q.has_value(),
          "testable zero-cell row lost Fisher/BH result");
}

void exclusion_contract() {
    const auto matrix = matrix_fixture();
    auto snapshot = snapshot_fixture(matrix, {
        {"case-1", application::CohortGroup::case_group},
        {"case-2", application::CohortGroup::case_group, application::CohortAnalysisDisposition::excluded},
        {"control-1", application::CohortGroup::control},
        {"control-2", application::CohortGroup::control},
    });
    const auto projected = domain::project_multi_sample_matrix_samples(
        matrix, {"case-1", "control-1", "control-2"}
    );
    check(projected.sample_count() == 3U && !projected.sample_index("case-2").has_value() &&
          projected.record_provenance_count() == 8U,
          "matrix projection retained excluded sample provenance");
    const auto result = analyze(snapshot, matrix);
    check(result.approved_case_samples == 1U && result.approved_control_samples == 2U,
          "QC exclusion did not change frozen denominators");
    check(result.variants[0].total_cases == 1U && result.variants[0].effective_case_samples == 1U,
          "excluded sample entered association matrix");
    check(result.variants[0].allele.table == domain::AssociationContingencyTable{1U,1U,1U,3U},
          "excluded case contributed allele counts");
}

void frozen_labels_contract() {
    const auto matrix = matrix_fixture();
    auto snapshot = snapshot_fixture(matrix, {
        {"case-1", application::CohortGroup::control},
        {"case-2", application::CohortGroup::control},
        {"control-1", application::CohortGroup::case_group},
        {"control-2", application::CohortGroup::case_group},
    });
    const auto result = analyze(snapshot, matrix);
    const auto& v = result.variants[0];
    check(v.allele.table == domain::AssociationContingencyTable{1U,3U,3U,1U},
          "association ignored frozen snapshot labels");
    check(v.allele.odds_ratio.kind == domain::AssociationOddsRatioKind::finite &&
          std::abs(v.allele.odds_ratio.value - (1.0/9.0)) < 1e-12,
          "frozen group direction not reflected in OR");
}

void family_contract() {
    const auto matrix = matrix_fixture();
    application::CohortAssociationApprovalOptions options;
    options.minimum_complete_case_calls = 3U;
    options.minimum_complete_control_calls = 3U;
    const auto snapshot = snapshot_fixture(matrix, {
        {"case-1", application::CohortGroup::case_group},
        {"case-2", application::CohortGroup::case_group},
        {"control-1", application::CohortGroup::control},
        {"control-2", application::CohortGroup::control},
    }, options);
    const auto result = analyze(snapshot, matrix);
    check(result.allele_family_size == 0U && result.carrier_family_size == 0U,
          "frozen empty family size changed");
    for (const auto& variant : result.variants) {
        check(!variant.allele.family_member && !variant.carrier.family_member &&
              !variant.allele.fisher_two_sided_p.has_value() &&
              !variant.carrier.fisher_two_sided_p.has_value() &&
              !variant.allele.bh_adjusted_q.has_value() &&
              !variant.carrier.bh_adjusted_q.has_value(),
              "display-time association recomputed outside frozen family");
    }
}

void drift_contract() {
    const auto matrix = matrix_fixture();
    const auto snapshot = snapshot_fixture(matrix);
    const auto drifted = matrix_fixture(true);
    expect_code([&] { static_cast<void>(analyze(snapshot, drifted)); },
                application::CohortCaseControlAnalysisErrorCode::matrix_mismatch);
}

void empty_group_contract() {
    const auto matrix = matrix_fixture();
    auto snapshot = snapshot_fixture(matrix, {
        {"case-1", application::CohortGroup::control},
        {"case-2", application::CohortGroup::control},
        {"control-1", application::CohortGroup::control},
        {"control-2", application::CohortGroup::control},
    });
    snapshot.approved_case_samples = 0U;
    expect_code([&] { static_cast<void>(analyze(snapshot, matrix)); },
                application::CohortCaseControlAnalysisErrorCode::invalid_snapshot);
}

void contract_version_contract() {
    const auto matrix = matrix_fixture();
    auto snapshot = snapshot_fixture(matrix);
    snapshot.association_contract_version = "biocore.case-control.v2";
    expect_code([&] { static_cast<void>(analyze(snapshot, matrix)); },
                application::CohortCaseControlAnalysisErrorCode::invalid_snapshot);
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        if (argc != 2) return EXIT_FAILURE;
        const std::string_view mode{argv[1]};
        if (mode == "fixed") fixed_contract();
        else if (mode == "missingness") missingness_contract();
        else if (mode == "zero-cell") zero_cell_contract();
        else if (mode == "exclusion") exclusion_contract();
        else if (mode == "frozen-labels") frozen_labels_contract();
        else if (mode == "family") family_contract();
        else if (mode == "drift") drift_contract();
        else if (mode == "empty-group") empty_group_contract();
        else if (mode == "contract-version") contract_version_contract();
        else return EXIT_FAILURE;
        return EXIT_SUCCESS;
    } catch (const std::exception&) {
        return EXIT_FAILURE;
    }
}
