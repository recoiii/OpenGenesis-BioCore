#pragma once

#include "biocore/domain/indel_candidate.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace biocore::domain::indel_detail {

struct SeedObservation final {
    VariantRecord variant;
    bool reverse_strand{false};
};

[[nodiscard]] bool consumes_read(AlignmentOperationKind kind) noexcept;
[[nodiscard]] bool consumes_reference(AlignmentOperationKind kind) noexcept;
[[nodiscard]] std::uint64_t reference_span(const ReadAlignment& alignment);
[[nodiscard]] std::vector<SeedObservation> extract_seeds(
    const ReadAlignment& alignment,
    const ReferenceGenome& reference,
    const IndelCandidateOptions& options
);
[[nodiscard]] std::uint32_t semiglobal_edit_distance(
    std::string_view query,
    std::string_view target
);
[[nodiscard]] std::string apply_variant_to_haplotype(
    std::string_view reference_haplotype,
    std::uint64_t haplotype_start,
    const VariantRecord& variant
);
[[nodiscard]] std::pair<std::uint32_t, std::optional<char>> homopolymer_context(
    const VariantRecord& variant,
    const ReferenceGenome& reference
);

}  // namespace biocore::domain::indel_detail
