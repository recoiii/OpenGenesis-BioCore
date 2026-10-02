#include "biocore/application/cohort_case_control_analysis_service.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace biocore::application {
namespace {

[[noreturn]] void fail(
    const CohortCaseControlAnalysisErrorCode code,
    std::string message
) {
    throw CohortCaseControlAnalysisError{code, std::move(message)};
}

void validate_snapshot_identity(
    const CohortAnalysisSnapshot& snapshot,
    const std::string_view project_id,
    const std::string_view analysis_id
) {
    if (snapshot.project_id != project_id || snapshot.analysis_id != analysis_id) {
        fail(CohortCaseControlAnalysisErrorCode::invalid_snapshot,
             "cohort analysis snapshot identity does not match request");
    }
    if (snapshot.contract_version != "biocore.cohort-analysis.v1" ||
        snapshot.association_contract_version != "biocore.case-control.v1" ||
        snapshot.test_filter_version != "biocore.complete-call-gate.v1" ||
        snapshot.matrix_stage_id != "matrix" ||
        snapshot.matrix_module_id != "org.biocore.cohort.matrix") {
        fail(CohortCaseControlAnalysisErrorCode::invalid_snapshot,
             "cohort analysis snapshot uses an unsupported v0.6 contract");
    }
    if (snapshot.approved_case_samples == 0U || snapshot.approved_control_samples == 0U ||
        snapshot.approved_case_samples > snapshot.resource_limits.maximum_samples ||
        snapshot.approved_control_samples > snapshot.resource_limits.maximum_samples ||
        snapshot.test_universe.size() > snapshot.resource_limits.maximum_normalized_alleles ||
        snapshot.allele_family_size > snapshot.test_universe.size() ||
        snapshot.carrier_family_size > snapshot.test_universe.size()) {
        fail(CohortCaseControlAnalysisErrorCode::invalid_snapshot,
             "cohort analysis snapshot denominators or test-family limits are invalid");
    }
}

void validate_matrix_identity(
    const CohortAnalysisSnapshot& snapshot,
    const domain::MultiSampleMatrix& matrix
) {
    if (matrix.sample_count() > snapshot.resource_limits.maximum_samples ||
        matrix.variant_count() > snapshot.resource_limits.maximum_normalized_alleles ||
        matrix.observation_count() > snapshot.resource_limits.maximum_observations ||
        matrix.variant_count() != snapshot.test_universe.size()) {
        fail(CohortCaseControlAnalysisErrorCode::matrix_mismatch,
             "cohort matrix dimensions do not match the frozen analysis snapshot");
    }

    std::set<std::string, std::less<>> expected_samples;
    for (const auto& source : snapshot.sources) {
        if (!expected_samples.emplace(source.project_sample_id).second) {
            fail(CohortCaseControlAnalysisErrorCode::invalid_snapshot,
                 "cohort analysis snapshot contains duplicate matrix source samples");
        }
    }
    if (expected_samples.size() != matrix.sample_count()) {
        fail(CohortCaseControlAnalysisErrorCode::matrix_mismatch,
             "cohort matrix sample cardinality differs from frozen sources");
    }
    for (std::size_t index = 0U; index < matrix.sample_count(); ++index) {
        if (!expected_samples.contains(matrix.sample(index).sample_id)) {
            fail(CohortCaseControlAnalysisErrorCode::matrix_mismatch,
                 "cohort matrix contains a sample outside the frozen sources");
        }
    }

    for (std::size_t index = 0U; index < matrix.variant_count(); ++index) {
        const auto& variant = matrix.variant(index);
        const auto& frozen = snapshot.test_universe[index];
        const auto contig = matrix.contigs().canonical_name(variant.locus.contig_id);
        if (!contig.has_value() || frozen.ordinal != index || *contig != frozen.contig ||
            variant.locus.start != frozen.start || variant.locus.end != frozen.end ||
            variant.reference.sequence != frozen.reference ||
            variant.alternate.sequence != frozen.alternate) {
            fail(CohortCaseControlAnalysisErrorCode::matrix_mismatch,
                 "cohort matrix variant identity differs from frozen test universe");
        }
    }
}

struct FrozenLabels final {
    std::vector<std::string> included_sample_ids;
    std::vector<domain::CaseControlSampleLabel> labels;
};

[[nodiscard]] FrozenLabels frozen_labels(const CohortAnalysisSnapshot& snapshot) {
    FrozenLabels result;
    std::set<std::string, std::less<>> seen;
    std::size_t cases = 0U;
    std::size_t controls = 0U;

    for (const auto& sample : snapshot.samples) {
        if (!seen.emplace(sample.sample_id).second) {
            fail(CohortCaseControlAnalysisErrorCode::invalid_snapshot,
                 "cohort analysis snapshot contains duplicate samples");
        }
        if (sample.analysis_disposition != CohortAnalysisDisposition::included) continue;
        if (sample.cohort_disposition != CohortMemberDisposition::included) {
            fail(CohortCaseControlAnalysisErrorCode::invalid_snapshot,
                 "cohort analysis snapshot includes a registry-excluded sample");
        }

        domain::CaseControlPhenotype phenotype;
        if (sample.group == CohortGroup::case_group) {
            phenotype = domain::CaseControlPhenotype::case_sample;
            ++cases;
        } else if (sample.group == CohortGroup::control) {
            phenotype = domain::CaseControlPhenotype::control;
            ++controls;
        } else {
            fail(CohortCaseControlAnalysisErrorCode::invalid_snapshot,
                 "cohort analysis snapshot includes an unassigned phenotype");
        }
        result.included_sample_ids.push_back(sample.sample_id);
        result.labels.push_back({sample.sample_id, phenotype});
    }

    if (cases != snapshot.approved_case_samples || controls != snapshot.approved_control_samples ||
        cases == 0U || controls == 0U) {
        fail(CohortCaseControlAnalysisErrorCode::invalid_snapshot,
             "cohort analysis snapshot group denominators do not match included samples");
    }
    return result;
}

void validate_frozen_variant(
    const CohortAnalysisSnapshot& snapshot,
    const std::size_t index,
    const domain::CaseControlVariantAssociation& value
) {
    const auto& frozen = snapshot.test_universe[index];
    if (value.variant_index != index ||
        value.total_cases != snapshot.approved_case_samples ||
        value.total_controls != snapshot.approved_control_samples ||
        value.case_unobserved != frozen.case_unobserved ||
        value.control_unobserved != frozen.control_unobserved ||
        value.case_no_calls != frozen.case_no_calls ||
        value.control_no_calls != frozen.control_no_calls ||
        value.case_partial_calls != frozen.case_partial_calls ||
        value.control_partial_calls != frozen.control_partial_calls ||
        value.case_complete_calls != frozen.case_complete_calls ||
        value.control_complete_calls != frozen.control_complete_calls) {
        fail(CohortCaseControlAnalysisErrorCode::matrix_mismatch,
             "association effective denominators differ from frozen test universe");
    }
    if (value.allele_fisher_two_sided_p.has_value() != frozen.allele_family_member ||
        value.carrier_fisher_two_sided_p.has_value() != frozen.carrier_family_member ||
        value.allele_bh_adjusted_q.has_value() != value.allele_fisher_two_sided_p.has_value() ||
        value.carrier_bh_adjusted_q.has_value() != value.carrier_fisher_two_sided_p.has_value()) {
        fail(CohortCaseControlAnalysisErrorCode::matrix_mismatch,
             "association test-family membership differs from frozen approval");
    }
}

}  // namespace

CohortCaseControlAnalysisError::CohortCaseControlAnalysisError(
    const CohortCaseControlAnalysisErrorCode code,
    std::string message
) : std::runtime_error{std::move(message)}, code_{code} {}

CohortCaseControlAnalysisErrorCode CohortCaseControlAnalysisError::code() const noexcept {
    return code_;
}

CohortCaseControlAnalysisService::CohortCaseControlAnalysisService(
    ICohortAnalysisSnapshotStore& snapshots
) noexcept : snapshots_{snapshots} {}

CohortCaseControlAnalysisResult CohortCaseControlAnalysisService::analyze(
    const std::string_view project_id,
    const std::string_view analysis_id,
    const domain::MultiSampleMatrix& frozen_matrix
) {
    const auto snapshot = snapshots_.find(project_id, analysis_id);
    if (!snapshot.has_value()) {
        fail(CohortCaseControlAnalysisErrorCode::analysis_not_found,
             "approved cohort analysis snapshot was not found");
    }
    validate_snapshot_identity(*snapshot, project_id, analysis_id);
    validate_matrix_identity(*snapshot, frozen_matrix);
    const auto label_set = frozen_labels(*snapshot);

    domain::MultiSampleMatrix analysis_matrix;
    domain::CaseControlAssociationResult association;
    try {
        analysis_matrix = domain::project_multi_sample_matrix_samples(
            frozen_matrix, label_set.included_sample_ids
        );
        association = domain::analyze_case_control(
            analysis_matrix,
            label_set.labels,
            domain::CaseControlAssociationOptions{
                .maximum_labels = snapshot->resource_limits.maximum_samples,
                .maximum_variants = snapshot->resource_limits.maximum_normalized_alleles,
                .minimum_complete_case_calls =
                    snapshot->association_options.minimum_complete_case_calls,
                .minimum_complete_control_calls =
                    snapshot->association_options.minimum_complete_control_calls,
                .maximum_fisher_table_states =
                    snapshot->association_options.maximum_fisher_table_states,
            }
        );
    } catch (const CohortCaseControlAnalysisError&) {
        throw;
    } catch (const std::exception& error) {
        fail(CohortCaseControlAnalysisErrorCode::association_failure,
             std::string{"cohort case/control integration failed: "} + error.what());
    }

    if (association.variants.size() != snapshot->test_universe.size()) {
        fail(CohortCaseControlAnalysisErrorCode::matrix_mismatch,
             "association result cardinality differs from frozen test universe");
    }

    CohortCaseControlAnalysisResult result{
        .project_id = snapshot->project_id,
        .analysis_id = snapshot->analysis_id,
        .snapshot_digest = snapshot->snapshot_digest,
        .association_contract_version = snapshot->association_contract_version,
        .test_filter_version = snapshot->test_filter_version,
        .approved_case_samples = snapshot->approved_case_samples,
        .approved_control_samples = snapshot->approved_control_samples,
        .allele_family_size = snapshot->allele_family_size,
        .carrier_family_size = snapshot->carrier_family_size,
        .variants = {},
    };
    result.variants.reserve(association.variants.size());

    std::size_t allele_family_size = 0U;
    std::size_t carrier_family_size = 0U;
    for (std::size_t index = 0U; index < association.variants.size(); ++index) {
        const auto& value = association.variants[index];
        const auto& frozen = snapshot->test_universe[index];
        validate_frozen_variant(*snapshot, index, value);
        if (frozen.allele_family_member) ++allele_family_size;
        if (frozen.carrier_family_member) ++carrier_family_size;

        result.variants.push_back({
            .ordinal = frozen.ordinal,
            .contig = frozen.contig,
            .start = frozen.start,
            .end = frozen.end,
            .reference = frozen.reference,
            .alternate = frozen.alternate,
            .total_cases = value.total_cases,
            .total_controls = value.total_controls,
            .case_unobserved = value.case_unobserved,
            .control_unobserved = value.control_unobserved,
            .case_no_calls = value.case_no_calls,
            .control_no_calls = value.control_no_calls,
            .case_partial_calls = value.case_partial_calls,
            .control_partial_calls = value.control_partial_calls,
            .effective_case_samples = value.case_complete_calls,
            .effective_control_samples = value.control_complete_calls,
            .allele = {
                .family_member = frozen.allele_family_member,
                .table = value.allele_table,
                .odds_ratio = value.allele_odds_ratio,
                .odds_ratio_ci95 = value.allele_odds_ratio_ci95,
                .fisher_two_sided_p = value.allele_fisher_two_sided_p,
                .bh_adjusted_q = value.allele_bh_adjusted_q,
            },
            .carrier = {
                .family_member = frozen.carrier_family_member,
                .table = value.carrier_table,
                .odds_ratio = value.carrier_odds_ratio,
                .odds_ratio_ci95 = value.carrier_odds_ratio_ci95,
                .fisher_two_sided_p = value.carrier_fisher_two_sided_p,
                .bh_adjusted_q = value.carrier_bh_adjusted_q,
            },
        });
    }
    if (allele_family_size != snapshot->allele_family_size ||
        carrier_family_size != snapshot->carrier_family_size) {
        fail(CohortCaseControlAnalysisErrorCode::invalid_snapshot,
             "frozen association family sizes do not match test-universe membership");
    }
    return result;
}

}  // namespace biocore::application
