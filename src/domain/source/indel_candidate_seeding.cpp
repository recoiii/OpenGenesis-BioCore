#include "indel_candidate_internal.hpp"
#include "biocore/domain/variant_normalization.hpp"

#include <algorithm>
#include <stdexcept>

namespace biocore::domain::indel_detail {
namespace {

[[nodiscard]] VariantRecord insertion_variant(
    const ReadAlignment& alignment,
    const ReferenceGenome& reference,
    const std::uint64_t reference_position,
    const std::string_view inserted
) {
    VariantRecord record;
    record.locus.contig_id = alignment.contig_id;
    const auto contig = reference.sequence(alignment.contig_id);
    if (!contig.has_value() || contig->empty()) throw std::logic_error("validated alignment reference disappeared");

    if (reference_position > 0U) {
        const std::uint64_t anchor = reference_position - 1U;
        const char base = (*contig)[static_cast<std::size_t>(anchor)];
        record.locus.start = anchor;
        record.locus.end = anchor + 1U;
        record.reference = {std::string(1U, base), VariantType::unknown, false};
        record.alternates.push_back({std::string(1U, base) + std::string{inserted}, VariantType::insertion, false});
    } else {
        const char base = contig->front();
        record.locus.start = 0U;
        record.locus.end = 1U;
        record.reference = {std::string(1U, base), VariantType::unknown, false};
        record.alternates.push_back({std::string{inserted} + std::string(1U, base), VariantType::insertion, false});
    }
    normalize_variant(record, reference);
    return record;
}

[[nodiscard]] VariantRecord deletion_variant(
    const ReadAlignment& alignment,
    const ReferenceGenome& reference,
    const std::uint64_t reference_position,
    const std::uint32_t deletion_length
) {
    VariantRecord record;
    record.locus.contig_id = alignment.contig_id;
    const auto contig = reference.sequence(alignment.contig_id);
    if (!contig.has_value()) throw std::logic_error("validated alignment reference disappeared");
    if (deletion_length == 0U || reference_position > contig->size() || deletion_length > contig->size() - reference_position) {
        throw std::invalid_argument("deletion seed exceeds reference bounds");
    }

    if (reference_position > 0U) {
        const std::uint64_t anchor = reference_position - 1U;
        const std::size_t length = static_cast<std::size_t>(deletion_length) + 1U;
        record.locus.start = anchor;
        record.locus.end = anchor + length;
        record.reference = {std::string{contig->substr(static_cast<std::size_t>(anchor), length)}, VariantType::unknown, false};
        record.alternates.push_back({std::string(1U, record.reference.sequence.front()), VariantType::deletion, false});
    } else {
        if (static_cast<std::uint64_t>(deletion_length) >= contig->size()) {
            throw std::invalid_argument("deletion at contig start requires a trailing anchor base");
        }
        const std::size_t length = static_cast<std::size_t>(deletion_length) + 1U;
        record.locus.start = 0U;
        record.locus.end = length;
        record.reference = {std::string{contig->substr(0U, length)}, VariantType::unknown, false};
        record.alternates.push_back({std::string(1U, record.reference.sequence.back()), VariantType::deletion, false});
    }
    normalize_variant(record, reference);
    return record;
}

[[nodiscard]] bool insertion_quality_ok(
    const ReadAlignment& alignment,
    const std::size_t read_position,
    const std::uint32_t length,
    const std::uint32_t minimum_quality
) {
    if (length > alignment.base_qualities.size() - read_position) return false;
    return std::all_of(
        alignment.base_qualities.begin() + static_cast<std::ptrdiff_t>(read_position),
        alignment.base_qualities.begin() + static_cast<std::ptrdiff_t>(read_position + length),
        [minimum_quality](const std::uint8_t value) { return value != 0xFFU && value >= minimum_quality; }
    );
}

[[nodiscard]] bool deletion_boundary_quality_ok(
    const ReadAlignment& alignment,
    const std::size_t read_position,
    const std::uint32_t minimum_quality
) {
    if (alignment.base_qualities.empty()) return false;
    bool saw = false;
    if (read_position > 0U) {
        const auto quality = alignment.base_qualities[read_position - 1U];
        if (quality == 0xFFU || quality < minimum_quality) return false;
        saw = true;
    }
    if (read_position < alignment.base_qualities.size()) {
        const auto quality = alignment.base_qualities[read_position];
        if (quality == 0xFFU || quality < minimum_quality) return false;
        saw = true;
    }
    return saw;
}

}  // namespace

std::vector<SeedObservation> extract_seeds(
    const ReadAlignment& alignment,
    const ReferenceGenome& reference,
    const IndelCandidateOptions& options
) {
    std::vector<SeedObservation> seeds;
    std::uint64_t reference_position = alignment.reference_start;
    std::size_t read_position = 0U;
    for (const auto& operation : alignment.cigar) {
        switch (operation.kind) {
            case AlignmentOperationKind::insertion: {
                if (operation.length <= options.maximum_candidate_indel_bases &&
                    insertion_quality_ok(alignment, read_position, operation.length, options.minimum_base_quality)) {
                    const auto inserted = std::string_view{alignment.sequence}.substr(read_position, operation.length);
                    seeds.push_back({insertion_variant(alignment, reference, reference_position, inserted), alignment.reverse_strand});
                }
                read_position += operation.length;
                break;
            }
            case AlignmentOperationKind::deletion: {
                if (operation.length <= options.maximum_candidate_indel_bases &&
                    deletion_boundary_quality_ok(alignment, read_position, options.minimum_base_quality)) {
                    seeds.push_back({deletion_variant(alignment, reference, reference_position, operation.length), alignment.reverse_strand});
                }
                reference_position += operation.length;
                break;
            }
            case AlignmentOperationKind::match:
            case AlignmentOperationKind::sequence_match:
            case AlignmentOperationKind::sequence_mismatch:
                read_position += operation.length;
                reference_position += operation.length;
                break;
            case AlignmentOperationKind::soft_clip:
                read_position += operation.length;
                break;
            case AlignmentOperationKind::reference_skip:
                reference_position += operation.length;
                break;
            case AlignmentOperationKind::hard_clip:
            case AlignmentOperationKind::padding:
                break;
        }
    }
    return seeds;
}

}  // namespace biocore::domain::indel_detail
