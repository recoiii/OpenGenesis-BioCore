#include "indel_candidate_internal.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

namespace biocore::domain::indel_detail {

std::uint32_t semiglobal_edit_distance(const std::string_view query, const std::string_view target) {
    if (query.empty()) return 0U;
    if (target.empty()) {
        if (query.size() > std::numeric_limits<std::uint32_t>::max()) throw std::overflow_error("edit distance overflows");
        return static_cast<std::uint32_t>(query.size());
    }
    if (query.size() > std::numeric_limits<std::uint32_t>::max() || target.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error("edit distance input is too large");
    }

    std::vector<std::uint32_t> previous(target.size() + 1U, 0U);
    std::vector<std::uint32_t> current(target.size() + 1U, 0U);
    for (std::size_t row = 1U; row <= query.size(); ++row) {
        current[0U] = static_cast<std::uint32_t>(row);
        for (std::size_t column = 1U; column <= target.size(); ++column) {
            const std::uint32_t substitution = previous[column - 1U] + (query[row - 1U] == target[column - 1U] ? 0U : 1U);
            const std::uint32_t deletion = previous[column] + 1U;
            const std::uint32_t insertion = current[column - 1U] + 1U;
            current[column] = std::min({substitution, deletion, insertion});
        }
        std::swap(previous, current);
    }
    return *std::min_element(previous.begin(), previous.end());
}

std::string apply_variant_to_haplotype(
    const std::string_view reference_haplotype,
    const std::uint64_t haplotype_start,
    const VariantRecord& variant
) {
    if (variant.alternates.size() != 1U || variant.alternates[0U].symbolic) {
        throw std::invalid_argument("local realignment requires one concrete alternate allele");
    }
    if (variant.locus.start < haplotype_start) throw std::invalid_argument("variant starts before realignment haplotype");
    const std::uint64_t relative = variant.locus.start - haplotype_start;
    if (relative > reference_haplotype.size() || variant.reference.sequence.size() > reference_haplotype.size() - relative) {
        throw std::invalid_argument("variant extends beyond realignment haplotype");
    }
    const auto observed = reference_haplotype.substr(
        static_cast<std::size_t>(relative), variant.reference.sequence.size()
    );
    if (observed != variant.reference.sequence) {
        throw std::logic_error("candidate reference does not match realignment haplotype");
    }
    std::string result;
    result.reserve(reference_haplotype.size() - variant.reference.sequence.size() + variant.alternates[0U].sequence.size());
    result.append(reference_haplotype.substr(0U, static_cast<std::size_t>(relative)));
    result.append(variant.alternates[0U].sequence);
    result.append(reference_haplotype.substr(static_cast<std::size_t>(relative) + variant.reference.sequence.size()));
    return result;
}

std::pair<std::uint32_t, std::optional<char>> homopolymer_context(
    const VariantRecord& variant,
    const ReferenceGenome& reference
) {
    const auto contig = reference.sequence(variant.locus.contig_id);
    if (!contig.has_value() || contig->empty()) return {0U, std::nullopt};

    std::uint32_t best_length = 0U;
    std::optional<char> best_base;
    const auto examine = [&](const std::uint64_t position) {
        if (position >= contig->size()) return;
        const char base = (*contig)[static_cast<std::size_t>(position)];
        std::uint64_t left = position;
        while (left > 0U && (*contig)[static_cast<std::size_t>(left - 1U)] == base) --left;
        std::uint64_t right = position + 1U;
        while (right < contig->size() && (*contig)[static_cast<std::size_t>(right)] == base) ++right;
        const std::uint64_t length = right - left;
        if (length > best_length) {
            best_length = length > std::numeric_limits<std::uint32_t>::max()
                ? std::numeric_limits<std::uint32_t>::max()
                : static_cast<std::uint32_t>(length);
            best_base = base;
        }
    };

    examine(variant.locus.start);
    if (variant.locus.start > 0U) examine(variant.locus.start - 1U);
    if (variant.locus.end > 0U) examine(variant.locus.end - 1U);
    if (variant.locus.end < contig->size()) examine(variant.locus.end);
    return {best_length, best_base};
}

}  // namespace biocore::domain::indel_detail
