#include "biocore/domain/variant_annotation.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace biocore::domain {
namespace {

[[nodiscard]] AnnotationDatabaseProvenance make_provenance(const ReferenceDatabaseMetadata& metadata) {
    return AnnotationDatabaseProvenance{
        metadata.database_id,
        metadata.display_name,
        metadata.version,
        metadata.schema_version,
        metadata.assembly,
        metadata.source_uri,
        metadata.source_sha256
    };
}

[[nodiscard]] auto database_key(const ReferenceDatabase* database) {
    const auto& metadata = database->metadata();
    return std::tie(
        metadata.database_id,
        metadata.version,
        metadata.assembly.assembly,
        metadata.assembly.custom_id,
        metadata.schema_version,
        metadata.source_sha256
    );
}

[[nodiscard]] bool same_database_identity(
    const ReferenceDatabase* left,
    const ReferenceDatabase* right
) {
    const auto& lhs = left->metadata();
    const auto& rhs = right->metadata();
    return lhs.database_id == rhs.database_id &&
           lhs.version == rhs.version &&
           lhs.assembly == rhs.assembly;
}

void require_capacity(const std::size_t current, const std::size_t incoming, const std::size_t maximum, const char* what) {
    if (incoming > maximum || current > maximum - incoming) {
        throw std::length_error(what);
    }
}

}  // namespace

void validate_variant_annotation_options(const VariantAnnotationOptions& options) {
    if (options.include_allele_annotations && options.maximum_allele_annotations_per_alternate == 0U) {
        throw std::invalid_argument("maximum allele annotations must be positive when allele annotation is enabled");
    }
    if (options.include_feature_annotations && options.maximum_feature_annotations_per_alternate == 0U) {
        throw std::invalid_argument("maximum feature annotations must be positive when feature annotation is enabled");
    }
}

VariantAnnotationResult annotate_variant(
    const ReferenceAssemblyIdentity& assembly,
    const ContigTable& query_contigs,
    const VariantRecord& variant,
    const std::vector<const ReferenceDatabase*>& databases,
    const VariantAnnotationOptions& options
) {
    validate_reference_assembly_identity(assembly);
    validate_variant_annotation_options(options);
    if (const auto error = validate_variant_record(variant); error.has_value()) {
        throw std::invalid_argument("invalid variant for annotation: " + *error);
    }
    if (variant.alternates.empty()) {
        throw std::invalid_argument("annotation requires at least one alternate allele");
    }

    std::vector<const ReferenceDatabase*> ordered_databases = databases;
    for (const auto* database : ordered_databases) {
        if (database == nullptr) {
            throw std::invalid_argument("annotation database pointer is null");
        }
        validate_reference_database_metadata(database->metadata());
        if (!(database->metadata().assembly == assembly)) {
            throw std::invalid_argument("annotation database assembly does not match query assembly");
        }
    }
    std::stable_sort(ordered_databases.begin(), ordered_databases.end(), [](const auto* left, const auto* right) {
        return database_key(left) < database_key(right);
    });
    for (std::size_t index = 1U; index < ordered_databases.size(); ++index) {
        if (same_database_identity(ordered_databases[index - 1U], ordered_databases[index])) {
            throw std::invalid_argument("duplicate annotation database identity");
        }
    }

    VariantAnnotationResult result{
        .locus = variant.locus,
        .reference = variant.reference,
        .alternates = {}
    };
    result.alternates.reserve(variant.alternates.size());

    for (std::size_t alternate_index = 0U; alternate_index < variant.alternates.size(); ++alternate_index) {
        AlternateAnnotation annotated{
            .alternate_index = alternate_index,
            .alternate = variant.alternates[alternate_index],
            .allele_annotations = {},
            .feature_annotations = {}
        };

        for (const auto* database : ordered_databases) {
            const auto provenance = make_provenance(database->metadata());
            if (options.include_allele_annotations) {
                const auto matches = database->query_exact(assembly, query_contigs, variant, alternate_index);
                require_capacity(
                    annotated.allele_annotations.size(),
                    matches.size(),
                    options.maximum_allele_annotations_per_alternate,
                    "allele annotation limit exceeded"
                );
                for (const auto ordinal : matches) {
                    const auto& record = database->record(ordinal);
                    annotated.allele_annotations.push_back(AlleleAnnotation{
                        .provenance = provenance,
                        .record_id = record.record_id,
                        .attributes = record.attributes
                    });
                }
            }

            if (options.include_feature_annotations) {
                const auto matches = database->query_feature_overlap(
                    assembly,
                    query_contigs,
                    variant.locus.contig_id,
                    variant.locus.start,
                    variant.locus.end
                );
                require_capacity(
                    annotated.feature_annotations.size(),
                    matches.size(),
                    options.maximum_feature_annotations_per_alternate,
                    "feature annotation limit exceeded"
                );
                for (const auto ordinal : matches) {
                    const auto& feature = database->feature(ordinal);
                    annotated.feature_annotations.push_back(FeatureAnnotation{
                        .provenance = provenance,
                        .source = feature.source,
                        .feature_type = feature.feature_type,
                        .score = feature.score,
                        .strand = feature.strand,
                        .phase = feature.phase,
                        .source_ordinal = feature.source_ordinal,
                        .attributes = feature.attributes
                    });
                }
            }
        }
        result.alternates.push_back(std::move(annotated));
    }
    return result;
}

}  // namespace biocore::domain
