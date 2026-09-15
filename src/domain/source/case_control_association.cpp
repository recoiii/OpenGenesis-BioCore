#include "biocore/domain/case_control_association.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <string_view>

namespace biocore::domain {
namespace {

constexpr std::size_t maximum_identifier_length = 1024U;

void validate_sample_id(const std::string_view value) {
    if (value.empty() || value.size() > maximum_identifier_length) {
        throw std::invalid_argument("case/control sample ID is invalid");
    }
}

void checked_add(std::uint64_t& target, const std::uint64_t value, const char* message) {
    if (target > std::numeric_limits<std::uint64_t>::max() - value) {
        throw std::overflow_error(message);
    }
    target += value;
}

std::uint64_t checked_add_value(const std::uint64_t left, const std::uint64_t right, const char* message) {
    if (left > std::numeric_limits<std::uint64_t>::max() - right) {
        throw std::overflow_error(message);
    }
    return left + right;
}

long double log_choose(const std::uint64_t n, const std::uint64_t k) {
    if (k > n) {
        return -std::numeric_limits<long double>::infinity();
    }
    return std::lgammal(static_cast<long double>(n) + 1.0L)
        - std::lgammal(static_cast<long double>(k) + 1.0L)
        - std::lgammal(static_cast<long double>(n - k) + 1.0L);
}

long double log_add_exp(const long double left, const long double right) {
    if (std::isinf(left) && left < 0.0L) {
        return right;
    }
    if (std::isinf(right) && right < 0.0L) {
        return left;
    }
    const long double high = std::max(left, right);
    const long double low = std::min(left, right);
    return high + std::log1pl(std::exp(low - high));
}

struct GroupCounters final {
    std::size_t total{0U};
    std::size_t unobserved{0U};
    std::size_t no_call{0U};
    std::size_t partial_call{0U};
    std::size_t complete_call{0U};
    std::uint64_t alternate_alleles{0U};
    std::uint64_t other_alleles{0U};
    std::uint64_t carriers{0U};
    std::uint64_t noncarriers{0U};
};

void record_observation(
    GroupCounters& counters,
    const MultiSampleMatrix& matrix,
    const MultiSampleMatrixObservation& observation
) {
    const auto& call = matrix.call(observation.call_index);
    if (call.sample_index != observation.sample_index) {
        throw std::logic_error("matrix call/sample association is inconsistent");
    }
    switch (call.state) {
        case MultiSampleCallState::no_call:
            ++counters.no_call;
            return;
        case MultiSampleCallState::partial_call:
            ++counters.partial_call;
            return;
        case MultiSampleCallState::complete_call:
            break;
    }
    if (!call.genotype_present || call.genotype.ploidy() == 0U || observation.missing_allele_count != 0U) {
        throw std::logic_error("complete matrix call contains missing genotype state");
    }
    const std::uint64_t called_other = checked_add_value(
        observation.reference_dosage,
        observation.other_alternate_dosage,
        "case/control non-selected allele count overflow"
    );
    const std::uint64_t projected_ploidy = checked_add_value(
        observation.alternate_dosage,
        called_other,
        "case/control projected ploidy overflow"
    );
    if (projected_ploidy != call.genotype.ploidy()) {
        throw std::logic_error("matrix dosage projection does not match complete genotype ploidy");
    }
    ++counters.complete_call;
    checked_add(counters.alternate_alleles, observation.alternate_dosage, "case/control alternate allele count overflow");
    checked_add(counters.other_alleles, called_other, "case/control other allele count overflow");
    if (observation.alternate_dosage > 0U) {
        ++counters.carriers;
    } else {
        ++counters.noncarriers;
    }
}

}  // namespace

void validate_case_control_association_options(const CaseControlAssociationOptions& options) {
    if (options.maximum_labels == 0U) {
        throw std::invalid_argument("maximum case/control labels must be positive");
    }
    if (options.maximum_variants == 0U) {
        throw std::invalid_argument("maximum case/control variants must be positive");
    }
    if (options.minimum_complete_case_calls == 0U) {
        throw std::invalid_argument("minimum complete case calls must be positive");
    }
    if (options.minimum_complete_control_calls == 0U) {
        throw std::invalid_argument("minimum complete control calls must be positive");
    }
    if (options.maximum_fisher_table_states == 0U) {
        throw std::invalid_argument("maximum Fisher table states must be positive");
    }
}

AssociationOddsRatio association_odds_ratio(const AssociationContingencyTable& table) noexcept {
    const long double numerator = static_cast<long double>(table.case_exposed)
        * static_cast<long double>(table.control_unexposed);
    const long double denominator = static_cast<long double>(table.case_unexposed)
        * static_cast<long double>(table.control_exposed);
    if (numerator == 0.0L && denominator == 0.0L) {
        return {AssociationOddsRatioKind::undefined, 0.0};
    }
    if (numerator == 0.0L) {
        return {AssociationOddsRatioKind::zero, 0.0};
    }
    if (denominator == 0.0L) {
        return {AssociationOddsRatioKind::positive_infinity, 0.0};
    }
    const long double ratio = numerator / denominator;
    if (!std::isfinite(ratio) || ratio > static_cast<long double>(std::numeric_limits<double>::max())) {
        return {AssociationOddsRatioKind::positive_infinity, 0.0};
    }
    return {AssociationOddsRatioKind::finite, static_cast<double>(ratio)};
}

double fisher_exact_two_sided(
    const AssociationContingencyTable& table,
    const std::size_t maximum_states
) {
    if (maximum_states == 0U) {
        throw std::invalid_argument("maximum Fisher table states must be positive");
    }
    const std::uint64_t row_case = checked_add_value(table.case_exposed, table.case_unexposed, "Fisher row count overflow");
    const std::uint64_t row_control = checked_add_value(table.control_exposed, table.control_unexposed, "Fisher row count overflow");
    const std::uint64_t col_exposed = checked_add_value(table.case_exposed, table.control_exposed, "Fisher column count overflow");
    const std::uint64_t col_unexposed = checked_add_value(table.case_unexposed, table.control_unexposed, "Fisher column count overflow");
    const std::uint64_t total = checked_add_value(row_case, row_control, "Fisher total count overflow");
    if (checked_add_value(col_exposed, col_unexposed, "Fisher total count overflow") != total) {
        throw std::logic_error("Fisher contingency table margins are inconsistent");
    }
    if (total == 0U || row_case == 0U || row_control == 0U || col_exposed == 0U || col_unexposed == 0U) {
        return 1.0;
    }

    const std::uint64_t low = row_case > col_unexposed ? row_case - col_unexposed : 0U;
    const std::uint64_t high = std::min(row_case, col_exposed);
    if (table.case_exposed < low || table.case_exposed > high) {
        throw std::invalid_argument("Fisher observed table is outside its fixed margins");
    }
    const std::uint64_t support_span = high - low;
    if (support_span >= static_cast<std::uint64_t>(maximum_states)) {
        throw std::length_error("Fisher exact support exceeds configured state limit");
    }

    const long double denominator_log = log_choose(total, row_case);
    const auto probability_log = [&](const std::uint64_t exposed_cases) {
        return log_choose(col_exposed, exposed_cases)
            + log_choose(col_unexposed, row_case - exposed_cases)
            - denominator_log;
    };
    const long double observed_log = probability_log(table.case_exposed);
    constexpr long double log_tolerance = 1.0e-12L;
    long double selected_log_sum = -std::numeric_limits<long double>::infinity();
    for (std::uint64_t value = low;; ++value) {
        const long double current_log = probability_log(value);
        if (current_log <= observed_log + log_tolerance) {
            selected_log_sum = log_add_exp(selected_log_sum, current_log);
        }
        if (value == high) {
            break;
        }
    }
    long double p = std::exp(selected_log_sum);
    if (!std::isfinite(p)) {
        throw std::overflow_error("Fisher exact probability is not finite");
    }
    p = std::clamp(p, 0.0L, 1.0L);
    return static_cast<double>(p);
}

CaseControlAssociationResult analyze_case_control(
    const MultiSampleMatrix& matrix,
    const std::vector<CaseControlSampleLabel>& labels,
    const CaseControlAssociationOptions& options
) {
    validate_case_control_association_options(options);
    if (matrix.sample_count() == 0U) {
        throw std::invalid_argument("case/control association requires at least one matrix sample");
    }
    if (matrix.variant_count() > options.maximum_variants) {
        throw std::length_error("case/control variant limit exceeded");
    }
    if (labels.size() != matrix.sample_count()) {
        throw std::invalid_argument("case/control labels must cover every matrix sample exactly once");
    }
    if (labels.size() > options.maximum_labels) {
        throw std::length_error("case/control label limit exceeded");
    }

    std::map<std::string, CaseControlPhenotype> by_sample;
    for (const auto& label : labels) {
        validate_sample_id(label.sample_id);
        if (label.phenotype != CaseControlPhenotype::case_sample
            && label.phenotype != CaseControlPhenotype::control) {
            throw std::invalid_argument("case/control phenotype value is invalid");
        }
        if (!by_sample.emplace(label.sample_id, label.phenotype).second) {
            throw std::invalid_argument("duplicate case/control sample label");
        }
    }

    std::vector<CaseControlPhenotype> phenotypes(matrix.sample_count());
    std::size_t total_cases = 0U;
    std::size_t total_controls = 0U;
    for (std::size_t sample_index = 0U; sample_index < matrix.sample_count(); ++sample_index) {
        const auto& sample = matrix.sample(sample_index);
        const auto it = by_sample.find(sample.sample_id);
        if (it == by_sample.end()) {
            throw std::invalid_argument("case/control label is missing for a matrix sample");
        }
        phenotypes[sample_index] = it->second;
        if (it->second == CaseControlPhenotype::case_sample) {
            ++total_cases;
        } else {
            ++total_controls;
        }
    }
    if (total_cases == 0U || total_controls == 0U) {
        throw std::invalid_argument("case/control association requires both case and control samples");
    }

    CaseControlAssociationResult result;
    result.variants.reserve(matrix.variant_count());
    for (std::size_t variant_index = 0U; variant_index < matrix.variant_count(); ++variant_index) {
        GroupCounters cases;
        GroupCounters controls;
        cases.total = total_cases;
        controls.total = total_controls;

        const auto ordinals = matrix.variant_observation_ordinals(variant_index);
        for (const auto ordinal : ordinals) {
            const auto& observation = matrix.observation(ordinal);
            if (observation.variant_index != variant_index || observation.sample_index >= phenotypes.size()) {
                throw std::logic_error("matrix variant observation index is inconsistent");
            }
            auto& counters = phenotypes[observation.sample_index] == CaseControlPhenotype::case_sample ? cases : controls;
            record_observation(counters, matrix, observation);
        }
        const auto case_observed = cases.no_call + cases.partial_call + cases.complete_call;
        const auto control_observed = controls.no_call + controls.partial_call + controls.complete_call;
        if (case_observed > cases.total || control_observed > controls.total) {
            throw std::logic_error("matrix variant has more observations than labeled samples");
        }
        cases.unobserved = cases.total - case_observed;
        controls.unobserved = controls.total - control_observed;

        CaseControlVariantAssociation item;
        item.variant_index = variant_index;
        item.total_cases = cases.total;
        item.total_controls = controls.total;
        item.case_unobserved = cases.unobserved;
        item.control_unobserved = controls.unobserved;
        item.case_no_calls = cases.no_call;
        item.control_no_calls = controls.no_call;
        item.case_partial_calls = cases.partial_call;
        item.control_partial_calls = controls.partial_call;
        item.case_complete_calls = cases.complete_call;
        item.control_complete_calls = controls.complete_call;
        item.allele_table = {
            cases.alternate_alleles,
            cases.other_alleles,
            controls.alternate_alleles,
            controls.other_alleles
        };
        item.carrier_table = {
            cases.carriers,
            cases.noncarriers,
            controls.carriers,
            controls.noncarriers
        };
        item.allele_odds_ratio = association_odds_ratio(item.allele_table);
        item.carrier_odds_ratio = association_odds_ratio(item.carrier_table);
        if (cases.complete_call >= options.minimum_complete_case_calls
            && controls.complete_call >= options.minimum_complete_control_calls) {
            item.allele_fisher_two_sided_p = fisher_exact_two_sided(item.allele_table, options.maximum_fisher_table_states);
            item.carrier_fisher_two_sided_p = fisher_exact_two_sided(item.carrier_table, options.maximum_fisher_table_states);
        }
        result.variants.push_back(std::move(item));
    }
    return result;
}

}  // namespace biocore::domain
