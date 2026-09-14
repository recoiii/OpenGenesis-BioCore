#include "biocore/domain/variant_normalization.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string_view>

namespace biocore::domain {
namespace {

void uppercase_sequence(std::string& value) {
    for (char& character : value) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
}

[[nodiscard]] bool all_non_symbolic(const VariantRecord& record) noexcept {
    return std::all_of(
        record.alternates.values().begin(),
        record.alternates.values().end(),
        [](const Allele& allele) { return !allele.symbolic; }
    );
}

[[nodiscard]] bool common_last_base(const VariantRecord& record) noexcept {
    if (record.reference.sequence.size() <= 1U) {
        return false;
    }
    const char value = record.reference.sequence.back();
    for (const Allele& alternate : record.alternates.values()) {
        if (alternate.sequence.size() <= 1U || alternate.sequence.back() != value) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool common_first_base(const VariantRecord& record) noexcept {
    if (record.reference.sequence.size() <= 1U) {
        return false;
    }
    const char value = record.reference.sequence.front();
    for (const Allele& alternate : record.alternates.values()) {
        if (alternate.sequence.size() <= 1U || alternate.sequence.front() != value) {
            return false;
        }
    }
    return true;
}

void trim_common_suffix(VariantRecord& record) {
    while (common_last_base(record)) {
        record.reference.sequence.pop_back();
        for (Allele& alternate : record.alternates.values()) {
            alternate.sequence.pop_back();
        }
    }
}

void trim_common_prefix(VariantRecord& record) {
    while (common_first_base(record)) {
        record.reference.sequence.erase(record.reference.sequence.begin());
        for (Allele& alternate : record.alternates.values()) {
            alternate.sequence.erase(alternate.sequence.begin());
        }
        ++record.locus.start;
    }
}

void refresh_locus_and_types(VariantRecord& record) {
    record.locus.end = record.locus.start + record.reference.sequence.size();
    for (Allele& alternate : record.alternates.values()) {
        alternate.type = classify_variant(
            record.reference.sequence,
            alternate.sequence,
            alternate.symbolic
        );
    }
}

void minimal_trim(VariantRecord& record) {
    if (!all_non_symbolic(record)) {
        refresh_locus_and_types(record);
        return;
    }
    trim_common_suffix(record);
    trim_common_prefix(record);
    refresh_locus_and_types(record);
}

void left_align_simple_biallelic_indel(VariantRecord& record, const ReferenceGenome& reference) {
    if (record.alternates.size() != 1U || record.alternates[0U].symbolic) {
        return;
    }
    const VariantType type = record.alternates[0U].type;
    if (type != VariantType::insertion && type != VariantType::deletion) {
        return;
    }

    for (;;) {
        if (record.locus.start == 0U) {
            break;
        }
        const auto previous = reference.base(record.locus.contig_id, record.locus.start - 1U);
        if (!previous.has_value()) {
            break;
        }

        VariantRecord candidate = record;
        candidate.locus.start -= 1U;
        candidate.reference.sequence.insert(candidate.reference.sequence.begin(), *previous);
        candidate.alternates[0U].sequence.insert(candidate.alternates[0U].sequence.begin(), *previous);
        minimal_trim(candidate);
        if (candidate.locus.start >= record.locus.start) {
            break;
        }
        if (validate_reference_match(candidate, reference).has_value()) {
            break;
        }
        record = std::move(candidate);
    }
}

}  // namespace

std::optional<std::string> validate_reference_match(
    const VariantRecord& record,
    const ReferenceGenome& reference
) {
    const auto expected = reference.slice(
        record.locus.contig_id,
        record.locus.start,
        record.reference.sequence.size()
    );
    if (!expected.has_value()) {
        return "variant reference span is outside the loaded FASTA contig";
    }
    if (*expected != record.reference.sequence) {
        return "VCF REF does not match the loaded FASTA reference";
    }
    return std::nullopt;
}

void normalize_variant(VariantRecord& record, const ReferenceGenome& reference) {
    uppercase_sequence(record.reference.sequence);
    for (Allele& alternate : record.alternates.values()) {
        if (!alternate.symbolic) {
            uppercase_sequence(alternate.sequence);
        }
    }

    if (const auto domain_error = validate_variant_record(record); domain_error.has_value()) {
        throw std::invalid_argument("variant cannot be normalized: " + *domain_error);
    }
    if (const auto reference_error = validate_reference_match(record, reference); reference_error.has_value()) {
        throw std::invalid_argument(*reference_error);
    }

    minimal_trim(record);
    left_align_simple_biallelic_indel(record, reference);
    minimal_trim(record);

    if (const auto reference_error = validate_reference_match(record, reference); reference_error.has_value()) {
        throw std::logic_error("normalization changed the represented reference haplotype: " + *reference_error);
    }
    if (const auto domain_error = validate_variant_record(record); domain_error.has_value()) {
        throw std::logic_error("normalized variant violates the Iteration 055 domain model: " + *domain_error);
    }
}

}  // namespace biocore::domain
