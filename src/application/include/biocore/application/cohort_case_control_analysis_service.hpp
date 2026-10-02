#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/i_cohort_analysis_snapshot_store.hpp"
#include "biocore/domain/case_control_association.hpp"
#include "biocore/domain/multi_sample_matrix.hpp"

namespace biocore::application {

enum class CohortCaseControlAnalysisErrorCode {
    analysis_not_found,
    invalid_snapshot,
    matrix_mismatch,
    association_failure
};

class CohortCaseControlAnalysisError final : public std::runtime_error {
public:
    CohortCaseControlAnalysisError(CohortCaseControlAnalysisErrorCode code, std::string message);
    [[nodiscard]] CohortCaseControlAnalysisErrorCode code() const noexcept;
private:
    CohortCaseControlAnalysisErrorCode code_;
};

struct CohortAssociationModelResult final {
    bool family_member{false};
    domain::AssociationContingencyTable table;
    domain::AssociationOddsRatio odds_ratio;
    std::optional<domain::AssociationConfidenceInterval95> odds_ratio_ci95;
    std::optional<double> fisher_two_sided_p;
    std::optional<double> bh_adjusted_q;
};

struct CohortAssociationVariantResult final {
    std::size_t ordinal{0U};
    std::string contig;
    std::uint64_t start{0U};
    std::uint64_t end{0U};
    std::string reference;
    std::string alternate;
    std::size_t total_cases{0U};
    std::size_t total_controls{0U};
    std::size_t case_unobserved{0U};
    std::size_t control_unobserved{0U};
    std::size_t case_no_calls{0U};
    std::size_t control_no_calls{0U};
    std::size_t case_partial_calls{0U};
    std::size_t control_partial_calls{0U};
    std::size_t effective_case_samples{0U};
    std::size_t effective_control_samples{0U};
    CohortAssociationModelResult allele;
    CohortAssociationModelResult carrier;
};

struct CohortCaseControlAnalysisResult final {
    std::string project_id;
    std::string analysis_id;
    std::string snapshot_digest;
    std::string association_contract_version;
    std::string test_filter_version;
    std::string case_group{"case"};
    std::string control_group{"control"};
    std::string no_call_policy{"complete-calls-only.v1"};
    std::string statistical_test{"probability-ordered-two-sided-fisher.v1"};
    std::string effect_measure{"odds-ratio-ad-over-bc.v1"};
    std::string confidence_interval_method{"woolf-log-or-95.v1"};
    std::string multiple_testing_method{"benjamini-hochberg-separate-by-model.v1"};
    std::size_t approved_case_samples{0U};
    std::size_t approved_control_samples{0U};
    std::size_t allele_family_size{0U};
    std::size_t carrier_family_size{0U};
    std::vector<CohortAssociationVariantResult> variants;
};

class CohortCaseControlAnalysisService final {
public:
    explicit CohortCaseControlAnalysisService(ICohortAnalysisSnapshotStore& snapshots) noexcept;

    [[nodiscard]] CohortCaseControlAnalysisResult analyze(
        std::string_view project_id,
        std::string_view analysis_id,
        const domain::MultiSampleMatrix& frozen_matrix
    );

private:
    ICohortAnalysisSnapshotStore& snapshots_;
};

}  // namespace biocore::application
