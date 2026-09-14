#include "biocore/domain/variant_index.hpp"

#include <algorithm>
#include <stdexcept>

namespace biocore::domain {

VariantCoordinateIndex VariantCoordinateIndex::build(const std::span<const VariantIndexEntry> entries) {
    VariantCoordinateIndex index;
    std::vector<VariantIndexEntry> sorted{entries.begin(), entries.end()};
    for (const auto& entry : sorted) {
        if (!entry.interval.valid() || entry.interval.start == entry.interval.end) {
            throw std::invalid_argument("variant index requires non-empty valid half-open intervals");
        }
    }

    std::ranges::sort(sorted, [](const VariantIndexEntry& left, const VariantIndexEntry& right) {
        if (left.interval.contig_id != right.interval.contig_id) {
            return left.interval.contig_id < right.interval.contig_id;
        }
        if (left.interval.start != right.interval.start) {
            return left.interval.start < right.interval.start;
        }
        if (left.interval.end != right.interval.end) {
            return left.interval.end < right.interval.end;
        }
        return left.ordinal < right.ordinal;
    });

    index.size_ = sorted.size();
    for (const auto& entry : sorted) {
        if (index.blocks_.empty() || index.blocks_.back().contig_id != entry.interval.contig_id) {
            index.blocks_.push_back(ContigBlock{entry.interval.contig_id, {}, {}});
        }
        auto& block = index.blocks_.back();
        block.entries.push_back(entry);
        const std::uint64_t previous_max = block.prefix_max_end.empty() ? 0U : block.prefix_max_end.back();
        block.prefix_max_end.push_back(std::max(previous_max, entry.interval.end));
    }
    return index;
}

std::vector<std::size_t> VariantCoordinateIndex::query(
    const ContigId contig_id,
    const std::uint64_t start,
    const std::uint64_t end
) const {
    if (start > end) {
        throw std::invalid_argument("variant index query end precedes start");
    }
    if (start == end) {
        return {};
    }

    const auto block_it = std::lower_bound(
        blocks_.begin(),
        blocks_.end(),
        contig_id,
        [](const ContigBlock& block, const ContigId id) { return block.contig_id < id; }
    );
    if (block_it == blocks_.end() || block_it->contig_id != contig_id) {
        return {};
    }

    const auto first_it = std::upper_bound(
        block_it->prefix_max_end.begin(),
        block_it->prefix_max_end.end(),
        start
    );
    std::size_t index = static_cast<std::size_t>(std::distance(block_it->prefix_max_end.begin(), first_it));
    std::vector<std::size_t> result;
    while (index < block_it->entries.size()) {
        const auto& entry = block_it->entries[index];
        if (entry.interval.start >= end) {
            break;
        }
        if (entry.interval.end > start) {
            result.push_back(entry.ordinal);
        }
        ++index;
    }
    return result;
}

}  // namespace biocore::domain
