#pragma once

#include "biocore/domain/reference_database.hpp"
#include "biocore/domain/variant_record.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace biocore::domain {

struct AnnotationDatabaseProvenance final {
    std::string database_id;
    std::string display_name;
    std::string version;
    std::uint32_t schema_version{0U};
    ReferenceAssemblyIdentity assembly;
    std::string source_uri;
    std::string source_sha256;

    friend bool operator==(const AnnotationDatabaseProvenance&, const AnnotationDatabaseProvenance&) = default;
};

struct AlleleAnnotation final {
    AnnotationDatabaseProvenance provenance;
    std::string record_id;
    std::vector<ReferenceDatabaseAttribute> attributes;
};

struct FeatureAnnotation final {
    AnnotationDatabaseProvenance provenance;
    std::string source;
    std::string feature_type;
    std::optional<double> score;
    char strand{'.'};
    std::optional<std::uint8_t> phase;
    std::size_t source_ordinal{0U};
    std::vector<ReferenceDatabaseAttribute> attributes;
};

struct AlternateAnnotation final {
    std::size_t alternate_index{0U};
    Allele alternate;
    std::vector<AlleleAnnotation> allele_annotations;
    std::vector<FeatureAnnotation> feature_annotations;
};

struct VariantAnnotationResult final {
    GenomicInterval locus;
    Allele reference;
    std::vector<AlternateAnnotation> alternates;
};

struct VariantAnnotationOptions final {
    bool include_allele_annotations{true};
    bool include_feature_annotations{true};
    std::size_t maximum_allele_annotations_per_alternate{4096U};
    std::size_t maximum_feature_annotations_per_alternate{16384U};
};

void validate_variant_annotation_options(const VariantAnnotationOptions& options);

[[nodiscard]] VariantAnnotationResult annotate_variant(
    const ReferenceAssemblyIdentity& assembly,
    const ContigTable& query_contigs,
    const VariantRecord& variant,
    const std::vector<const ReferenceDatabase*>& databases,
    const VariantAnnotationOptions& options = {}
);

}  // namespace biocore::domain
