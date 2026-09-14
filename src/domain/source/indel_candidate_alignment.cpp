#include "biocore/domain/indel_candidate.hpp"
#include "indel_candidate_internal.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <string>

namespace biocore::domain {
namespace {

[[nodiscard]] std::optional<AlignmentOperationKind> cigar_kind(const char operation) noexcept {
    switch (operation) {
        case 'M': return AlignmentOperationKind::match;
        case 'I': return AlignmentOperationKind::insertion;
        case 'D': return AlignmentOperationKind::deletion;
        case 'N': return AlignmentOperationKind::reference_skip;
        case 'S': return AlignmentOperationKind::soft_clip;
        case 'H': return AlignmentOperationKind::hard_clip;
        case 'P': return AlignmentOperationKind::padding;
        case '=': return AlignmentOperationKind::sequence_match;
        case 'X': return AlignmentOperationKind::sequence_mismatch;
        default: return std::nullopt;
    }
}

[[nodiscard]] bool valid_read_base(const char base) noexcept {
    switch (base) {
        case 'A': case 'C': case 'G': case 'T': case 'N': return true;
        default: return false;
    }
}

}  // namespace

void validate_indel_candidate_options(const IndelCandidateOptions& options) {
    if (options.minimum_mapping_quality > 255U || options.minimum_base_quality > 93U ||
        options.minimum_seed_observations == 0U || options.minimum_realign_support == 0U ||
        options.minimum_score_improvement == 0U || options.realignment_flank > 4096U ||
        options.maximum_realign_read_bases == 0U || options.maximum_realign_read_bases > 16384U ||
        options.maximum_candidate_indel_bases == 0U || options.maximum_candidate_indel_bases > 16384U) {
        throw std::invalid_argument("Indel candidate options are outside supported bounds");
    }
}

std::vector<AlignmentOperation> parse_alignment_cigar(const std::string_view cigar) {
    if (cigar.empty() || cigar == "*") {
        throw std::invalid_argument("Mapped alignment CIGAR is empty");
    }
    std::vector<AlignmentOperation> result;
    std::uint64_t length = 0U;
    bool have_digit = false;
    for (const char value : cigar) {
        if (value >= '0' && value <= '9') {
            const std::uint64_t digit = static_cast<std::uint64_t>(value - '0');
            if (length > (std::numeric_limits<std::uint32_t>::max() - digit) / 10U) {
                throw std::invalid_argument("CIGAR operation length overflows uint32");
            }
            length = length * 10U + digit;
            have_digit = true;
            continue;
        }
        const auto kind = cigar_kind(value);
        if (!have_digit || length == 0U || !kind.has_value()) {
            throw std::invalid_argument("CIGAR is malformed or unsupported");
        }
        result.push_back({static_cast<std::uint32_t>(length), *kind});
        length = 0U;
        have_digit = false;
    }
    if (have_digit || result.empty()) {
        throw std::invalid_argument("CIGAR is malformed");
    }
    return result;
}

std::optional<std::string> validate_read_alignment(
    const ReadAlignment& alignment,
    const ReferenceGenome& reference,
    const IndelCandidateOptions& options
) {
    if (alignment.sequence.empty()) return "alignment sequence is empty";
    if (alignment.sequence.size() > options.maximum_realign_read_bases) return "alignment sequence exceeds realignment safety bound";
    if (alignment.base_qualities.size() != alignment.sequence.size()) return "alignment qualities do not match sequence length";
    if (alignment.mapping_quality > 255U) return "alignment mapping quality is outside 0..255";
    if (alignment.cigar.empty()) return "alignment CIGAR is empty";
    if (!reference.sequence(alignment.contig_id).has_value()) return "alignment contig is absent from reference";

    std::uint64_t read_bases = 0U;
    std::uint64_t ref_bases = 0U;
    for (const auto& operation : alignment.cigar) {
        if (operation.length == 0U) return "alignment CIGAR contains zero-length operation";
        if (indel_detail::consumes_read(operation.kind)) {
            if (read_bases > std::numeric_limits<std::uint64_t>::max() - operation.length) return "alignment read span overflows";
            read_bases += operation.length;
        }
        if (indel_detail::consumes_reference(operation.kind)) {
            if (ref_bases > std::numeric_limits<std::uint64_t>::max() - operation.length) return "alignment reference span overflows";
            ref_bases += operation.length;
        }
    }
    if (read_bases != alignment.sequence.size()) return "alignment CIGAR read span differs from sequence length";
    const auto contig = reference.sequence(alignment.contig_id);
    if (!contig.has_value() || alignment.reference_start > contig->size() ||
        ref_bases > contig->size() - alignment.reference_start) {
        return "alignment reference span exceeds contig bounds";
    }
    for (const char base : alignment.sequence) {
        if (!valid_read_base(base)) return "alignment sequence contains unsupported base";
    }
    for (const auto quality : alignment.base_qualities) {
        if (quality > 93U && quality != 0xFFU) return "alignment base quality is outside supported Phred range";
    }
    return std::nullopt;
}

}  // namespace biocore::domain

namespace biocore::domain::indel_detail {

bool consumes_read(const AlignmentOperationKind kind) noexcept {
    return kind == AlignmentOperationKind::match || kind == AlignmentOperationKind::insertion ||
        kind == AlignmentOperationKind::soft_clip || kind == AlignmentOperationKind::sequence_match ||
        kind == AlignmentOperationKind::sequence_mismatch;
}

bool consumes_reference(const AlignmentOperationKind kind) noexcept {
    return kind == AlignmentOperationKind::match || kind == AlignmentOperationKind::deletion ||
        kind == AlignmentOperationKind::reference_skip || kind == AlignmentOperationKind::sequence_match ||
        kind == AlignmentOperationKind::sequence_mismatch;
}

std::uint64_t reference_span(const ReadAlignment& alignment) {
    std::uint64_t result = 0U;
    for (const auto& operation : alignment.cigar) {
        if (!consumes_reference(operation.kind)) continue;
        if (result > std::numeric_limits<std::uint64_t>::max() - operation.length) {
            throw std::invalid_argument("alignment reference span overflows");
        }
        result += operation.length;
    }
    return result;
}

}  // namespace biocore::domain::indel_detail
