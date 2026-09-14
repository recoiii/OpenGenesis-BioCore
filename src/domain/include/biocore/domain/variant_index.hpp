#pragma once

#include "biocore/domain/variant_record.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace biocore::domain {

struct VariantIndexEntry final {
    GenomicInterval interval;
    std::size_t ordinal{0U};
};

class VariantCoordinateIndex final {
public:
    VariantCoordinateIndex() = default;

    [[nodiscard]] static VariantCoordinateIndex build(std::span<const VariantIndexEntry> entries);

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::vector<std::size_t> query(
        ContigId contig_id,
        std::uint64_t start,
        std::uint64_t end
    ) const;

private:
    struct ContigBlock final {
        ContigId contig_id{0U};
        std::vector<VariantIndexEntry> entries;
        std::vector<std::uint64_t> prefix_max_end;
    };

    std::vector<ContigBlock> blocks_;
    std::size_t size_{0U};
};

}  // namespace biocore::domain
