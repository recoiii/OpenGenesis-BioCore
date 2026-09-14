#include "vcf_ingestion_internal.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace biocore::domain::vcf_detail {

[[nodiscard]] GenotypeCall parse_gt(const std::string_view raw, const std::size_t allele_count) {
    GenotypeCall call;
    if (raw.empty()) {
        throw std::invalid_argument("VCF GT is empty");
    }

    std::size_t begin = 0U;
    for (;;) {
        std::size_t end = begin;
        while (end < raw.size() && raw[end] != '/' && raw[end] != '|') {
            ++end;
        }
        const std::string_view allele = raw.substr(begin, end - begin);
        if (allele == ".") {
            call.allele_indices.push_back(missing_allele_index);
        } else {
            const std::uint64_t parsed = parse_u64(allele, "VCF GT allele index");
            if (parsed > static_cast<std::uint64_t>(std::numeric_limits<std::int16_t>::max())) {
                throw std::invalid_argument("VCF GT allele index exceeds internal capacity");
            }
            call.allele_indices.push_back(static_cast<std::int16_t>(parsed));
        }
        if (end == raw.size()) {
            break;
        }
        call.phased_separators.push_back(raw[end] == '|' ? 1U : 0U);
        begin = end + 1U;
        if (begin == raw.size()) {
            throw std::invalid_argument("VCF GT ends with a separator");
        }
    }

    if (const auto error = validate_genotype_call(call, allele_count); error.has_value()) {
        throw std::invalid_argument("VCF GT is invalid: " + *error);
    }
    return call;
}

void store_extra_format(SampleVariantData& sample, std::string key, VariantFieldValue value) {
    sample.extra_format_fields.push_back(DynamicVariantField{std::move(key), std::move(value)});
}

void assign_format_value(
    SampleVariantData& sample,
    const std::string& key,
    VariantFieldValue value
) {
    if (key == "DP" || key == "GQ") {
        if (const auto* scalar = std::get_if<std::int32_t>(&value)) {
            if (*scalar < 0) {
                throw std::invalid_argument("VCF FORMAT " + key + " must not be negative");
            }
            if (key == "DP") sample.genotype.depth = static_cast<std::uint32_t>(*scalar);
            else sample.genotype.genotype_quality = static_cast<std::uint32_t>(*scalar);
            return;
        }
        store_extra_format(sample, key, std::move(value));
        return;
    }
    if (key == "AD" || key == "PL") {
        if (const auto* values = std::get_if<NullableVariantList<std::int32_t>>(&value); values != nullptr) {
            for (const auto& item : *values) {
                if (item.has_value() && *item < 0) {
                    throw std::invalid_argument("VCF FORMAT " + key + " values must not be negative");
                }
            }
            if (all_present(*values)) {
                if (key == "AD") {
                    for (const auto& item : *values) sample.genotype.allele_depths.push_back(static_cast<std::uint32_t>(*item));
                } else {
                    for (const auto& item : *values) sample.genotype.phred_likelihoods.push_back(static_cast<std::uint32_t>(*item));
                    normalize_phred_likelihoods(sample.genotype);
                }
                return;
            }
        }
        store_extra_format(sample, key, std::move(value));
        return;
    }
    store_extra_format(sample, key, std::move(value));
}

[[nodiscard]] std::vector<SampleVariantData> parse_samples(
    const std::vector<std::string_view>& fields,
    const VcfHeader& header,
    const std::size_t alt_count
) {
    if (header.sample_names.empty()) {
        if (fields.size() != 8U) {
            throw std::invalid_argument("VCF record has FORMAT/sample columns but the header declares no samples");
        }
        return {};
    }
    if (fields.size() != 9U + header.sample_names.size()) {
        throw std::invalid_argument("VCF record sample column count does not match the header");
    }

    const auto keys = split(fields[8U], ':');
    if (keys.empty()) {
        throw std::invalid_argument("VCF FORMAT column is empty");
    }
    std::unordered_set<std::string> seen;
    for (const auto key : keys) {
        if (!safe_identifier(key) || !seen.insert(std::string{key}).second) {
            throw std::invalid_argument("VCF FORMAT keys are invalid or duplicated");
        }
    }
    const auto gt_it = std::find(keys.begin(), keys.end(), std::string_view{"GT"});
    if (gt_it != keys.end() && gt_it != keys.begin()) {
        throw std::invalid_argument("VCF GT must be the first FORMAT field when present");
    }

    std::vector<SampleVariantData> samples;
    samples.reserve(header.sample_names.size());
    for (std::size_t sample_index = 0U; sample_index < header.sample_names.size(); ++sample_index) {
        SampleVariantData sample;
        const auto values = split(fields[9U + sample_index], ':');
        if (values.size() > keys.size()) {
            throw std::invalid_argument("VCF sample has more values than FORMAT keys");
        }
        for (std::size_t key_index = 0U; key_index < keys.size(); ++key_index) {
            const std::string key{keys[key_index]};
            const std::string_view raw = key_index < values.size() ? values[key_index] : std::string_view{"."};
            if (key == "GT") {
                sample.genotype = parse_gt(raw, alt_count + 1U);
                sample.genotype_present = true;
                continue;
            }
            VcfFieldDefinition fallback;
            const auto& definition = definition_for(header.format_definitions, key, true, fallback);
            std::optional<std::size_t> ploidy;
            if (sample.genotype_present) {
                ploidy = sample.genotype.ploidy();
            }
            VariantFieldValue parsed = parse_typed_value(
                raw,
                definition,
                alt_count,
                ploidy,
                "VCF FORMAT " + key
            );
            assign_format_value(sample, key, std::move(parsed));
        }
        if (sample.genotype_present) {
            if (const auto error = validate_genotype_call(sample.genotype, alt_count + 1U); error.has_value()) {
                throw std::invalid_argument("VCF sample genotype metrics are invalid: " + *error);
            }
        }
        samples.push_back(std::move(sample));
    }
    return samples;
}

}  // namespace biocore::domain::vcf_detail
