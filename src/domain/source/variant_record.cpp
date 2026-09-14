#include "biocore/domain/variant_record.hpp"

#include <limits>
#include <stdexcept>

namespace biocore::domain {

std::optional<std::string> validate_variant_record(const VariantRecord& record) {
    if (!record.locus.valid()) {
        return "variant interval end precedes start";
    }
    if (record.reference.symbolic) {
        return "reference allele must not be symbolic";
    }
    if (record.reference.sequence.empty()) {
        return "reference allele must not be empty";
    }
    if (record.reference.type != VariantType::unknown) {
        return "reference allele type must remain UNKNOWN";
    }
    if (record.locus.length() != record.reference.sequence.size()) {
        return "half-open locus length must match the reference allele length";
    }
    if (record.alternates.empty()) {
        return "variant record must contain at least one alternate allele";
    }

    for (const Allele& alternate : record.alternates.values()) {
        if (alternate.sequence.empty()) {
            return "alternate allele must not be empty";
        }
        if (!alternate.symbolic) {
            const VariantType expected = classify_variant(record.reference.sequence, alternate.sequence, false);
            if (alternate.type != expected) {
                return "alternate allele type does not match REF/ALT representation";
            }
        } else if (alternate.type != VariantType::unknown) {
            return "symbolic alternate alleles must use UNKNOWN until a dedicated symbolic type model exists";
        }
    }

    if (record.info.allele_frequencies.has_value()
        && record.info.allele_frequencies->size() != record.alternates.size()) {
        return "AF cardinality must be Number=A";
    }
    if (record.info.allele_counts.has_value()
        && record.info.allele_counts->size() != record.alternates.size()) {
        return "AC cardinality must be Number=A";
    }
    if (record.info.allele_number.has_value() && *record.info.allele_number < 0) {
        return "AN must not be negative";
    }
    if (record.info.depth.has_value() && *record.info.depth < 0) {
        return "DP must not be negative";
    }
    return std::nullopt;
}

std::uint64_t vcf_position_to_internal_start(const std::uint64_t vcf_position) {
    if (vcf_position == 0U) {
        throw std::invalid_argument("VCF POS is 1-based and must be at least 1");
    }
    return vcf_position - 1U;
}

std::uint64_t internal_start_to_vcf_position(const std::uint64_t internal_start) {
    if (internal_start == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("internal coordinate cannot be represented as VCF POS");
    }
    return internal_start + 1U;
}

}  // namespace biocore::domain
