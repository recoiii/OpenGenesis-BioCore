#include "biocore/domain/variant_prioritization.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>

namespace biocore::domain {

void rank_variant_priorities(std::vector<RankedVariantPriority>& priorities) {
    for (const auto& item : priorities) {
        if (!std::isfinite(item.prioritization.score)) {
            throw std::invalid_argument("cannot rank a non-finite prioritization score");
        }
    }
    std::sort(priorities.begin(), priorities.end(), [](const RankedVariantPriority& left, const RankedVariantPriority& right) {
        if (left.prioritization.score != right.prioritization.score) {
            return left.prioritization.score > right.prioritization.score;
        }
        return std::tie(
            left.key.contig_id,
            left.key.start,
            left.key.end,
            left.key.reference,
            left.key.alternate,
            left.key.alternate_index,
            left.input_ordinal
        ) < std::tie(
            right.key.contig_id,
            right.key.start,
            right.key.end,
            right.key.reference,
            right.key.alternate,
            right.key.alternate_index,
            right.input_ordinal
        );
    });
}

}  // namespace biocore::domain
