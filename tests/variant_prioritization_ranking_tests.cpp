#include "biocore/domain/variant_prioritization.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
using namespace biocore::domain;

[[nodiscard]] bool require(const bool condition, const char* message) {
    if (!condition) std::cerr << "variant_prioritization_ranking_tests: " << message << '\n';
    return condition;
}

[[nodiscard]] RankedVariantPriority item(
    const double score,
    const ContigId contig,
    const std::uint64_t start,
    const std::string& ref,
    const std::string& alt,
    const std::size_t ordinal
) {
    RankedVariantPriority value;
    value.key = VariantPriorityKey{contig, start, start + ref.size(), ref, alt, 0U};
    value.prioritization.score = score;
    value.input_ordinal = ordinal;
    return value;
}

}  // namespace

int main() {
    using namespace biocore::domain;
    bool ok = true;

    std::vector<RankedVariantPriority> values{
        item(5.0, 1U, 20U, "A", "G", 4U),
        item(8.0, 2U, 5U, "C", "T", 1U),
        item(8.0, 1U, 30U, "A", "C", 2U),
        item(8.0, 1U, 30U, "A", "C", 1U),
        item(8.0, 1U, 10U, "G", "T", 9U)
    };
    rank_variant_priorities(values);

    ok = require(values[0].key.contig_id == 1U && values[0].key.start == 10U,
                 "ties must break by genomic key") && ok;
    ok = require(values[1].key.start == 30U && values[1].input_ordinal == 1U,
                 "identical canonical keys must use input ordinal as final deterministic tie-break") && ok;
    ok = require(values[2].key.start == 30U && values[2].input_ordinal == 2U,
                 "input ordinal tie-break order is incorrect") && ok;
    ok = require(values[3].key.contig_id == 2U && values[3].prioritization.score == 8.0,
                 "contig tie-break is incorrect") && ok;
    ok = require(values[4].prioritization.score == 5.0, "score ordering must be descending") && ok;

    const auto first_pass = values;
    rank_variant_priorities(values);
    ok = require(values.size() == first_pass.size(), "reranking changed cardinality") && ok;
    for (std::size_t i = 0; i < values.size(); ++i) {
        ok = require(values[i].input_ordinal == first_pass[i].input_ordinal,
                     "reranking must be idempotent and deterministic") && ok;
    }

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
