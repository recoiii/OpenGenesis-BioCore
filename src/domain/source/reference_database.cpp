#include "biocore/domain/reference_database.hpp"

#include "biocore/domain/variant_types.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace biocore::domain {
namespace {

constexpr std::size_t kMaximumMetadataTextBytes = 4096U;
constexpr std::size_t kMaximumIdentifierBytes = 1024U;
constexpr std::size_t kMaximumAttributesPerRecord = 1024U;
constexpr std::size_t kMaximumReferenceRecords = 50'000'000U;

[[nodiscard]] bool valid_identifier(const std::string_view value) noexcept {
    if (value.empty() || value.size() > kMaximumIdentifierBytes) {
        return false;
    }
    return std::ranges::all_of(value, [](const char character) {
        const bool alpha = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
        const bool digit = character >= '0' && character <= '9';
        return alpha || digit || character == '.' || character == '_' || character == '-';
    });
}

[[nodiscard]] bool valid_lower_sha256(const std::string_view value) noexcept {
    if (value.size() != 64U) {
        return false;
    }
    return std::ranges::all_of(value, [](const char character) {
        return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
    });
}

[[nodiscard]] bool valid_sequence_allele(const std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    return std::ranges::all_of(value, [](const char character) {
        return character == 'A' || character == 'C' || character == 'G' || character == 'T'
            || character == 'N';
    });
}

[[nodiscard]] bool valid_symbolic_alternate(const std::string_view value) noexcept {
    return value == "*" || (value.size() >= 3U && value.front() == '<' && value.back() == '>');
}

void validate_record(const ReferenceDatabaseRecord& record, const ContigTable& contigs) {
    if (record.record_id.empty() || record.record_id.size() > kMaximumIdentifierBytes) {
        throw std::invalid_argument("reference database record ID must be non-empty and bounded");
    }
    if (!record.locus.valid() || record.locus.start == record.locus.end) {
        throw std::invalid_argument("reference database record requires a non-empty valid interval");
    }
    if (!contigs.canonical_name(record.locus.contig_id).has_value()) {
        throw std::invalid_argument("reference database record uses an unknown contig ID");
    }
    if (!valid_sequence_allele(record.reference.sequence)) {
        throw std::invalid_argument("reference database REF must use uppercase A/C/G/T/N");
    }
    if (record.reference.symbolic || record.reference.type != VariantType::unknown) {
        throw std::invalid_argument("reference database REF must be non-symbolic UNKNOWN");
    }
    if (record.alternate.sequence == record.reference.sequence) {
        throw std::invalid_argument("reference database ALT must differ from REF");
    }
    if (record.alternate.symbolic) {
        if (!valid_symbolic_alternate(record.alternate.sequence)
            || record.alternate.type != VariantType::unknown) {
            throw std::invalid_argument("reference database symbolic ALT is malformed");
        }
    } else {
        if (!valid_sequence_allele(record.alternate.sequence)) {
            throw std::invalid_argument("reference database ALT must use uppercase A/C/G/T/N");
        }
        if (record.alternate.type
            != classify_variant(record.reference.sequence, record.alternate.sequence, false)) {
            throw std::invalid_argument("reference database ALT type does not match REF/ALT representation");
        }
    }

    VariantRecord variant;
    variant.locus = record.locus;
    variant.reference = record.reference;
    variant.alternates.push_back(record.alternate);
    if (const auto error = validate_variant_record(variant); error.has_value()) {
        throw std::invalid_argument("invalid reference database variant: " + *error);
    }

    if (record.attributes.size() > kMaximumAttributesPerRecord) {
        throw std::invalid_argument("reference database record has too many attributes");
    }
    std::unordered_set<std::string> keys;
    keys.reserve(record.attributes.size());
    for (const auto& attribute : record.attributes) {
        if (!valid_identifier(attribute.key)) {
            throw std::invalid_argument("reference database attribute key is invalid");
        }
        if (!keys.emplace(attribute.key).second) {
            throw std::invalid_argument("duplicate reference database attribute key: " + attribute.key);
        }
        if (attribute.value.has_value() && attribute.value->size() > kMaximumMetadataTextBytes) {
            throw std::invalid_argument("reference database attribute value is too large");
        }
    }
}


void validate_feature(const ReferenceFeatureRecord& feature, const ContigTable& contigs) {
    if (!feature.locus.valid() || feature.locus.start == feature.locus.end) {
        throw std::invalid_argument("reference feature requires a non-empty valid interval");
    }
    if (!contigs.canonical_name(feature.locus.contig_id).has_value()) {
        throw std::invalid_argument("reference feature uses an unknown contig ID");
    }
    if (feature.source.size() > kMaximumMetadataTextBytes || feature.feature_type.empty()
        || feature.feature_type.size() > kMaximumMetadataTextBytes) {
        throw std::invalid_argument("reference feature source/type is invalid");
    }
    if (feature.score.has_value() && !std::isfinite(*feature.score)) {
        throw std::invalid_argument("reference feature score must be finite");
    }
    if (feature.strand != '+' && feature.strand != '-' && feature.strand != '.' && feature.strand != '?') {
        throw std::invalid_argument("reference feature strand is invalid");
    }
    if (feature.phase.has_value() && *feature.phase > 2U) {
        throw std::invalid_argument("reference feature phase must be 0, 1 or 2");
    }
    if (feature.attributes.size() > 4096U) {
        throw std::invalid_argument("reference feature has too many attributes");
    }
    for (const auto& attribute : feature.attributes) {
        if (!valid_identifier(attribute.key)) {
            throw std::invalid_argument("reference feature attribute key is invalid");
        }
        if (attribute.value.has_value() && attribute.value->size() > kMaximumMetadataTextBytes) {
            throw std::invalid_argument("reference feature attribute value is too large");
        }
    }
}

[[nodiscard]] bool feature_less(
    const ReferenceFeatureRecord& left,
    const ReferenceFeatureRecord& right
) noexcept {
    if (left.locus.contig_id != right.locus.contig_id) return left.locus.contig_id < right.locus.contig_id;
    if (left.locus.start != right.locus.start) return left.locus.start < right.locus.start;
    if (left.locus.end != right.locus.end) return left.locus.end < right.locus.end;
    if (left.feature_type != right.feature_type) return left.feature_type < right.feature_type;
    if (left.source != right.source) return left.source < right.source;
    if (left.strand != right.strand) return left.strand < right.strand;
    return left.source_ordinal < right.source_ordinal;
}

[[nodiscard]] bool record_less(
    const ReferenceDatabaseRecord& left,
    const ReferenceDatabaseRecord& right
) noexcept {
    if (left.locus.contig_id != right.locus.contig_id) {
        return left.locus.contig_id < right.locus.contig_id;
    }
    if (left.locus.start != right.locus.start) {
        return left.locus.start < right.locus.start;
    }
    if (left.locus.end != right.locus.end) {
        return left.locus.end < right.locus.end;
    }
    if (left.reference.sequence != right.reference.sequence) {
        return left.reference.sequence < right.reference.sequence;
    }
    if (left.alternate.sequence != right.alternate.sequence) {
        return left.alternate.sequence < right.alternate.sequence;
    }
    return left.record_id < right.record_id;
}

void require_matching_assembly(
    const ReferenceAssemblyIdentity& expected,
    const ReferenceAssemblyIdentity& observed
) {
    validate_reference_assembly_identity(observed);
    if (expected != observed) {
        throw std::invalid_argument("reference database query assembly does not match database assembly");
    }
}

}  // namespace

void validate_reference_assembly_identity(const ReferenceAssemblyIdentity& identity) {
    if (identity.assembly == ReferenceAssembly::unspecified) {
        throw std::invalid_argument("reference database assembly must be specified");
    }
    if (identity.assembly == ReferenceAssembly::custom) {
        if (!valid_identifier(identity.custom_id)) {
            throw std::invalid_argument("custom reference assembly requires a stable custom ID");
        }
    } else if (!identity.custom_id.empty()) {
        throw std::invalid_argument("custom assembly ID is only valid for custom assemblies");
    }
}

void validate_reference_database_metadata(const ReferenceDatabaseMetadata& metadata) {
    if (!valid_identifier(metadata.database_id)) {
        throw std::invalid_argument("reference database ID is invalid");
    }
    if (metadata.display_name.empty() || metadata.display_name.size() > kMaximumMetadataTextBytes) {
        throw std::invalid_argument("reference database display name is invalid");
    }
    if (!valid_identifier(metadata.version)) {
        throw std::invalid_argument("reference database version is invalid");
    }
    if (metadata.schema_version == 0U) {
        throw std::invalid_argument("reference database schema version must be positive");
    }
    validate_reference_assembly_identity(metadata.assembly);
    if (metadata.source_uri.empty() || metadata.source_uri.size() > kMaximumMetadataTextBytes) {
        throw std::invalid_argument("reference database source URI is invalid");
    }
    if (!valid_lower_sha256(metadata.source_sha256)) {
        throw std::invalid_argument("reference database source checksum must be lowercase SHA-256");
    }
}

ReferenceDatabase ReferenceDatabase::build(
    ReferenceDatabaseMetadata metadata,
    ContigTable contigs,
    std::vector<ReferenceDatabaseRecord> records,
    std::vector<ReferenceFeatureRecord> features
) {
    validate_reference_database_metadata(metadata);
    if (contigs.assembly() != metadata.assembly.assembly) {
        throw std::invalid_argument("reference database contig table assembly does not match metadata");
    }
    if (records.size() > kMaximumReferenceRecords || features.size() > kMaximumReferenceRecords) {
        throw std::invalid_argument("reference database record count exceeds safety bound");
    }

    std::unordered_set<std::string> record_ids;
    record_ids.reserve(records.size());
    for (auto& record : records) {
        validate_record(record, contigs);
        if (!record_ids.emplace(record.record_id).second) {
            throw std::invalid_argument("duplicate reference database record ID: " + record.record_id);
        }
        std::ranges::sort(record.attributes, {}, &ReferenceDatabaseAttribute::key);
    }
    std::ranges::sort(records, record_less);
    for (auto& feature : features) {
        validate_feature(feature, contigs);
        std::stable_sort(
            feature.attributes.begin(),
            feature.attributes.end(),
            [](const ReferenceDatabaseAttribute& left, const ReferenceDatabaseAttribute& right) {
                if (left.key != right.key) return left.key < right.key;
                return left.value.value_or("") < right.value.value_or("");
            }
        );
    }
    std::ranges::sort(features, feature_less);

    std::vector<VariantIndexEntry> entries;
    entries.reserve(records.size());
    for (std::size_t ordinal = 0U; ordinal < records.size(); ++ordinal) {
        entries.push_back(VariantIndexEntry{records[ordinal].locus, ordinal});
    }

    std::vector<VariantIndexEntry> feature_entries;
    feature_entries.reserve(features.size());
    for (std::size_t ordinal = 0U; ordinal < features.size(); ++ordinal) {
        feature_entries.push_back(VariantIndexEntry{features[ordinal].locus, ordinal});
    }

    ReferenceDatabase database;
    database.metadata_ = std::move(metadata);
    database.contigs_ = std::move(contigs);
    database.records_ = std::move(records);
    database.features_ = std::move(features);
    database.coordinate_index_ = VariantCoordinateIndex::build(entries);
    database.feature_coordinate_index_ = VariantCoordinateIndex::build(feature_entries);
    return database;
}

const ReferenceDatabaseRecord& ReferenceDatabase::record(const std::size_t ordinal) const {
    if (ordinal >= records_.size()) {
        throw std::out_of_range("reference database record ordinal is out of range");
    }
    return records_[ordinal];
}

std::vector<std::size_t> ReferenceDatabase::query_overlap(
    const ReferenceAssemblyIdentity& assembly,
    const ContigTable& query_contigs,
    const ContigId contig_id,
    const std::uint64_t start,
    const std::uint64_t end
) const {
    require_matching_assembly(metadata_.assembly, assembly);
    if (query_contigs.assembly() != assembly.assembly) {
        throw std::invalid_argument("reference database query contig table assembly does not match query assembly");
    }
    const auto canonical_name = query_contigs.canonical_name(contig_id);
    if (!canonical_name.has_value()) {
        return {};
    }
    const auto database_contig_id = contigs_.resolve(*canonical_name);
    if (!database_contig_id.has_value()) {
        return {};
    }
    return coordinate_index_.query(*database_contig_id, start, end);
}

std::vector<std::size_t> ReferenceDatabase::query_exact(
    const ReferenceAssemblyIdentity& assembly,
    const ContigTable& query_contigs,
    const VariantRecord& variant,
    const std::size_t alternate_index
) const {
    require_matching_assembly(metadata_.assembly, assembly);
    if (query_contigs.assembly() != assembly.assembly) {
        throw std::invalid_argument("reference database query contig table assembly does not match query assembly");
    }
    if (const auto error = validate_variant_record(variant); error.has_value()) {
        throw std::invalid_argument("invalid variant database query: " + *error);
    }
    if (alternate_index >= variant.alternates.size()) {
        throw std::out_of_range("reference database query ALT index is out of range");
    }
    const auto canonical_name = query_contigs.canonical_name(variant.locus.contig_id);
    if (!canonical_name.has_value()) {
        return {};
    }
    const auto database_contig_id = contigs_.resolve(*canonical_name);
    if (!database_contig_id.has_value()) {
        return {};
    }
    const Allele& alternate = variant.alternates[alternate_index];
    std::vector<std::size_t> result;
    for (const std::size_t ordinal : coordinate_index_.query(
             *database_contig_id,
             variant.locus.start,
             variant.locus.end
         )) {
        const auto& candidate = records_[ordinal];
        if (candidate.locus.start == variant.locus.start
            && candidate.locus.end == variant.locus.end
            && candidate.reference.sequence == variant.reference.sequence
            && candidate.alternate.sequence == alternate.sequence
            && candidate.alternate.symbolic == alternate.symbolic) {
            result.push_back(ordinal);
        }
    }
    return result;
}

const ReferenceFeatureRecord& ReferenceDatabase::feature(const std::size_t ordinal) const {
    if (ordinal >= features_.size()) {
        throw std::out_of_range("reference feature ordinal is out of range");
    }
    return features_[ordinal];
}

std::vector<std::size_t> ReferenceDatabase::query_feature_overlap(
    const ReferenceAssemblyIdentity& assembly,
    const ContigTable& query_contigs,
    const ContigId contig_id,
    const std::uint64_t start,
    const std::uint64_t end
) const {
    require_matching_assembly(metadata_.assembly, assembly);
    if (query_contigs.assembly() != assembly.assembly) {
        throw std::invalid_argument("reference feature query contig table assembly does not match query assembly");
    }
    const auto canonical_name = query_contigs.canonical_name(contig_id);
    if (!canonical_name.has_value()) {
        return {};
    }
    const auto database_contig_id = contigs_.resolve(*canonical_name);
    if (!database_contig_id.has_value()) {
        return {};
    }
    return feature_coordinate_index_.query(*database_contig_id, start, end);
}

}  // namespace biocore::domain
