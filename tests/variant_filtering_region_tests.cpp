#include "biocore/domain/variant_filtering.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
using namespace biocore::domain;

[[nodiscard]] bool require(const bool condition, const char* message) {
    if (!condition) std::cerr << "variant_filtering_region_tests: " << message << '\n';
    return condition;
}

[[nodiscard]] VariantRecord make_variant(const ContigId contig, const std::uint64_t start) {
    VariantRecord variant;
    variant.locus = GenomicInterval{contig, start, start + 1U};
    variant.reference = Allele{"A", VariantType::unknown, false};
    variant.alternates.push_back(Allele{"G", VariantType::snv, false});
    return variant;
}

[[nodiscard]] bool has_reason(const VariantFilterDecision& decision, const VariantFilterReason reason) {
    for (const auto observed : decision.reasons) if (observed == reason) return true;
    return false;
}

}  // namespace

int main() {
    using namespace biocore::domain;
    bool ok = true;

    const VariantRegionSet merged({
        GenomicInterval{0U, 10U, 20U},
        GenomicInterval{0U, 20U, 30U},
        GenomicInterval{0U, 15U, 25U},
        GenomicInterval{1U, 5U, 8U}
    });
    ok = require(merged.intervals().size() == 2U, "overlapping/adjacent regional masks must merge deterministically") && ok;
    ok = require(merged.intervals()[0U].start == 10U && merged.intervals()[0U].end == 30U,
                 "merged region boundaries are incorrect") && ok;
    ok = require(merged.overlaps(GenomicInterval{0U, 29U, 31U}), "overlap at right edge was missed") && ok;
    ok = require(!merged.overlaps(GenomicInterval{0U, 30U, 31U}), "half-open adjacent region must not overlap") && ok;
    ok = require(!merged.overlaps(GenomicInterval{2U, 10U, 11U}), "different contig region incorrectly overlapped") && ok;

    const VariantRecord inside = make_variant(0U, 12U);
    const VariantRecord outside = make_variant(0U, 40U);
    VariantFilterEvidence evidence;

    AdvancedVariantFilterPolicy required;
    required.required_regions = VariantRegionSet({GenomicInterval{0U, 10U, 30U}});
    ok = require(evaluate_variant_filter(inside, evidence, 0U, required).passed,
                 "variant inside required region did not pass") && ok;
    const auto outside_result = evaluate_variant_filter(outside, evidence, 0U, required);
    ok = require(has_reason(outside_result, VariantFilterReason::outside_required_region),
                 "variant outside required region was not filtered") && ok;

    AdvancedVariantFilterPolicy excluded;
    excluded.excluded_regions = VariantRegionSet({GenomicInterval{0U, 10U, 30U}});
    const auto excluded_result = evaluate_variant_filter(inside, evidence, 0U, excluded);
    ok = require(has_reason(excluded_result, VariantFilterReason::excluded_region),
                 "variant overlapping excluded region was not filtered") && ok;
    ok = require(evaluate_variant_filter(outside, evidence, 0U, excluded).passed,
                 "variant outside excluded region should pass") && ok;

    bool zero_length_rejected = false;
    try {
        static_cast<void>(VariantRegionSet({GenomicInterval{0U, 5U, 5U}}));
    } catch (const std::invalid_argument&) {
        zero_length_rejected = true;
    }
    ok = require(zero_length_rejected, "zero-length region was accepted") && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
