#pragma once

#include "biocore/domain/multi_sample_matrix.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace biocore::domain {

enum class CaseControlPhenotype : std::uint8_t {
    control,
    case_sample
};

struct CaseControlSampleLabel final {
    std::string sample_id;
    CaseControlPhenotype phenotype{CaseControlPhenotype::control};
};

struct CaseControlAssociationOptions final {
    std::size_t maximum_labels{100000U};
    std::size_t maximum_variants{10000000U};
    std::size_t minimum_complete_case_calls{1U};
    std::size_t minimum_complete_control_calls{1U};
    std::size_t maximum_fisher_table_states{1000000U};
};

struct AssociationContingencyTable final {
    std::uint64_t case_exposed{0U};
    std::uint64_t case_unexposed{0U};
    std::uint64_t control_exposed{0U};
    std::uint64_t control_unexposed{0U};

    friend bool operator==(const AssociationContingencyTable&, const AssociationContingencyTable&) = default;
};

enum class AssociationOddsRatioKind : std::uint8_t {
    undefined,
    zero,
    finite,
    positive_infinity
};

struct AssociationOddsRatio final {
    AssociationOddsRatioKind kind{AssociationOddsRatioKind::undefined};
    double value{0.0};
};

struct CaseControlVariantAssociation final {
    std::size_t variant_index{0U};

    std::size_t total_cases{0U};
    std::size_t total_controls{0U};
    std::size_t case_unobserved{0U};
    std::size_t control_unobserved{0U};
    std::size_t case_no_calls{0U};
    std::size_t control_no_calls{0U};
    std::size_t case_partial_calls{0U};
    std::size_t control_partial_calls{0U};
    std::size_t case_complete_calls{0U};
    std::size_t control_complete_calls{0U};

    AssociationContingencyTable allele_table;
    AssociationContingencyTable carrier_table;

    AssociationOddsRatio allele_odds_ratio;
    AssociationOddsRatio carrier_odds_ratio;
    std::optional<double> allele_fisher_two_sided_p;
    std::optional<double> carrier_fisher_two_sided_p;
};

struct CaseControlAssociationResult final {
    std::vector<CaseControlVariantAssociation> variants;
};

void validate_case_control_association_options(const CaseControlAssociationOptions& options);

[[nodiscard]] AssociationOddsRatio association_odds_ratio(const AssociationContingencyTable& table) noexcept;
[[nodiscard]] double fisher_exact_two_sided(
    const AssociationContingencyTable& table,
    std::size_t maximum_states = 1000000U
);

[[nodiscard]] CaseControlAssociationResult analyze_case_control(
    const MultiSampleMatrix& matrix,
    const std::vector<CaseControlSampleLabel>& labels,
    const CaseControlAssociationOptions& options = {}
);

}  // namespace biocore::domain
