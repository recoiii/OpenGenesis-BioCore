#include "vcf_ingestion_internal.hpp"

#include "biocore/domain/variant_normalization.hpp"
#include "biocore/domain/variant_types.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace biocore::domain::vcf_detail {
namespace {
constexpr std::size_t maximum_alt_alleles = 4096U;
}

CanonicalVariantRecord parse_record(
    const std::vector<std::string_view>& fields,
    const VcfHeader& header,
    const ReferenceGenome& reference
) {
if (fields.size() < 8U) {
    throw std::invalid_argument("VCF record has fewer than eight mandatory columns");
}
const auto contig_id = reference.contigs().resolve(fields[0]);
if (!contig_id.has_value()) {
    throw std::invalid_argument("VCF CHROM cannot be resolved against the loaded FASTA: " + std::string{fields[0]});
}
const std::uint64_t position = parse_u64(fields[1], "VCF POS");
const std::uint64_t start = vcf_position_to_internal_start(position);

if (fields[3].empty() || fields[3] == ".") {
    throw std::invalid_argument("VCF REF must contain a sequence");
}
std::string ref{fields[3]};
uppercase_allele(ref);
if (!valid_sequence_allele(ref)) {
    throw std::invalid_argument("VCF REF contains a base outside A,C,G,T,N");
}
if (ref.size() > std::numeric_limits<std::uint64_t>::max() - start) {
    throw std::overflow_error("VCF REF span overflows internal coordinates");
}

const auto alt_tokens = split(fields[4], ',');
if (fields[4] == "." || alt_tokens.empty() || alt_tokens.size() > maximum_alt_alleles) {
    throw std::invalid_argument("VCF ALT is empty or exceeds the supported allele count");
}

CanonicalVariantRecord record;
record.variant.locus = {*contig_id, start, start + ref.size()};
record.variant.reference = Allele{std::move(ref), VariantType::unknown, false};
for (const auto alt_token : alt_tokens) {
    if (alt_token.empty() || alt_token == ".") {
        throw std::invalid_argument("VCF ALT contains an empty allele");
    }
    std::string alt{alt_token};
    const bool symbolic = symbolic_alt(alt);
    if (!symbolic) {
        uppercase_allele(alt);
        if (!valid_sequence_allele(alt)) {
            throw std::invalid_argument("VCF ALT contains a base outside A,C,G,T,N");
        }
    }
    const VariantType type = classify_variant(record.variant.reference.sequence, alt, symbolic);
    record.variant.alternates.push_back(Allele{
        std::move(alt),
        type,
        symbolic,
    });
}

record.id = fields[2] == "." ? std::string{} : std::string{fields[2]};
if (fields[5] != ".") {
    const double quality = parse_double(fields[5], "VCF QUAL");
    if (quality < 0.0) {
        throw std::invalid_argument("VCF QUAL must not be negative");
    }
    record.quality = quality;
}
record.filters = parse_filters(fields[6], record.filters_applied);
parse_info_fields(fields[7], header, record.variant);
record.samples = parse_samples(fields, header, record.variant.alternates.size());

normalize_variant(record.variant, reference);
for (const auto& sample : record.samples) {
    if (sample.genotype_present) {
        if (const auto error = validate_genotype_call(
                sample.genotype,
                record.variant.alternates.size() + 1U
            ); error.has_value()) {
            throw std::logic_error("normalization invalidated genotype allele semantics: " + *error);
        }
    }
}
return record;
}

}  // namespace biocore::domain::vcf_detail
