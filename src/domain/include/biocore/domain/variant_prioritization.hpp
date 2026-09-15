#pragma once

#include "biocore/domain/variant_filtering.hpp"
#include "biocore/domain/variant_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace biocore::domain {

enum class PrioritizationFeature : std::uint8_t {
    depth,
    variant_allele_fraction,
    genotype_quality,
    mapping_quality,
    alternate_strand_fraction,
    homopolymer_run_length,
    filter_passed,
    variant_type
};

enum class PrioritizationOperator : std::uint8_t {
    equal,
    not_equal,
    less_than,
    less_or_equal,
    greater_than,
    greater_or_equal
};

enum class MissingFeatureBehavior : std::uint8_t {
    skip_rule,
    reject_evaluation
};

enum class PrioritizationRuleStatus : std::uint8_t {
    matched,
    not_matched,
    missing
};

using PrioritizationValue = std::variant<double, bool, VariantType>;

struct VariantPrioritizationContext final {
    std::size_t alternate_index{0U};
    std::optional<double> depth;
    std::optional<double> variant_allele_fraction;
    std::optional<double> genotype_quality;
    std::optional<double> mapping_quality;
    std::optional<double> alternate_strand_fraction;
    std::optional<double> homopolymer_run_length;
    bool filter_passed{false};
    VariantType variant_type{VariantType::unknown};
};

struct VariantPrioritizationRule final {
    std::string id;
    PrioritizationFeature feature{PrioritizationFeature::depth};
    PrioritizationOperator comparison{PrioritizationOperator::greater_or_equal};
    PrioritizationValue expected{0.0};
    double score_delta{0.0};
    MissingFeatureBehavior missing_behavior{MissingFeatureBehavior::skip_rule};
};

struct VariantPrioritizationPolicy final {
    double baseline_score{0.0};
    std::vector<VariantPrioritizationRule> rules;
};

struct VariantPrioritizationTrace final {
    std::string rule_id;
    PrioritizationRuleStatus status{PrioritizationRuleStatus::not_matched};
    std::optional<PrioritizationValue> observed;
    double applied_score_delta{0.0};
};

struct VariantPrioritizationResult final {
    double score{0.0};
    std::vector<VariantPrioritizationTrace> trace;
};

struct VariantPriorityKey final {
    ContigId contig_id{0U};
    std::uint64_t start{0U};
    std::uint64_t end{0U};
    std::string reference;
    std::string alternate;
    std::size_t alternate_index{0U};
};

struct RankedVariantPriority final {
    VariantPriorityKey key;
    VariantPrioritizationResult prioritization;
    std::size_t input_ordinal{0U};
};

void validate_variant_prioritization_policy(const VariantPrioritizationPolicy& policy);

[[nodiscard]] VariantPrioritizationContext make_variant_prioritization_context(
    const VariantRecord& variant,
    const VariantFilterEvidence& evidence,
    const VariantFilterDecision& filter_decision
);

[[nodiscard]] VariantPrioritizationResult evaluate_variant_prioritization(
    const VariantPrioritizationContext& context,
    const VariantPrioritizationPolicy& policy
);

[[nodiscard]] VariantPriorityKey make_variant_priority_key(
    const VariantRecord& variant,
    std::size_t alternate_index
);

void rank_variant_priorities(std::vector<RankedVariantPriority>& priorities);

[[nodiscard]] std::string_view prioritization_feature_name(PrioritizationFeature feature) noexcept;
[[nodiscard]] std::string_view prioritization_rule_status_name(PrioritizationRuleStatus status) noexcept;

}  // namespace biocore::domain
