#pragma once

#include "biocore/domain/contig_table.hpp"
#include "biocore/domain/variant_index.hpp"
#include "biocore/domain/variant_record.hpp"

#include <cstddef>
#include <cstdint>
#include <istream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace biocore::domain {

struct ReferenceAssemblyIdentity final {
    ReferenceAssembly assembly{ReferenceAssembly::unspecified};
    std::string custom_id;

    friend bool operator==(const ReferenceAssemblyIdentity&, const ReferenceAssemblyIdentity&) = default;
};

struct ReferenceDatabaseMetadata final {
    std::string database_id;
    std::string display_name;
    std::string version;
    std::uint32_t schema_version{1U};
    ReferenceAssemblyIdentity assembly;
    std::string source_uri;
    std::string source_sha256;
};

struct ReferenceDatabaseAttribute final {
    std::string key;
    std::optional<std::string> value;

    friend bool operator==(const ReferenceDatabaseAttribute&, const ReferenceDatabaseAttribute&) = default;
};

struct ReferenceDatabaseRecord final {
    GenomicInterval locus;
    Allele reference;
    Allele alternate;
    std::string record_id;
    std::vector<ReferenceDatabaseAttribute> attributes;
};

struct ReferenceFeatureRecord final {
    GenomicInterval locus;
    std::string source;
    std::string feature_type;
    std::optional<double> score;
    char strand{'.'};
    std::optional<std::uint8_t> phase;
    std::size_t source_ordinal{0U};
    std::vector<ReferenceDatabaseAttribute> attributes;
};

void validate_reference_assembly_identity(const ReferenceAssemblyIdentity& identity);
void validate_reference_database_metadata(const ReferenceDatabaseMetadata& metadata);

class ReferenceDatabase final {
public:
    ReferenceDatabase() = default;

    [[nodiscard]] static ReferenceDatabase build(
        ReferenceDatabaseMetadata metadata,
        ContigTable contigs,
        std::vector<ReferenceDatabaseRecord> records,
        std::vector<ReferenceFeatureRecord> features = {}
    );

    [[nodiscard]] static ReferenceDatabase from_tsv(
        std::istream& input,
        ReferenceDatabaseMetadata metadata,
        ContigTable contigs
    );

    [[nodiscard]] static ReferenceDatabase from_gff3(
        std::istream& input,
        ReferenceDatabaseMetadata metadata,
        ContigTable contigs
    );

    [[nodiscard]] static ReferenceDatabase from_gtf(
        std::istream& input,
        ReferenceDatabaseMetadata metadata,
        ContigTable contigs
    );

    [[nodiscard]] const ReferenceDatabaseMetadata& metadata() const noexcept { return metadata_; }
    [[nodiscard]] const ContigTable& contigs() const noexcept { return contigs_; }
    [[nodiscard]] std::size_t size() const noexcept { return records_.size() + features_.size(); }
    [[nodiscard]] std::size_t variant_size() const noexcept { return records_.size(); }
    [[nodiscard]] std::size_t feature_size() const noexcept { return features_.size(); }
    [[nodiscard]] const ReferenceDatabaseRecord& record(std::size_t ordinal) const;
    [[nodiscard]] const ReferenceFeatureRecord& feature(std::size_t ordinal) const;

    [[nodiscard]] std::vector<std::size_t> query_overlap(
        const ReferenceAssemblyIdentity& assembly,
        const ContigTable& query_contigs,
        ContigId contig_id,
        std::uint64_t start,
        std::uint64_t end
    ) const;

    [[nodiscard]] std::vector<std::size_t> query_exact(
        const ReferenceAssemblyIdentity& assembly,
        const ContigTable& query_contigs,
        const VariantRecord& variant,
        std::size_t alternate_index
    ) const;

    [[nodiscard]] std::vector<std::size_t> query_feature_overlap(
        const ReferenceAssemblyIdentity& assembly,
        const ContigTable& query_contigs,
        ContigId contig_id,
        std::uint64_t start,
        std::uint64_t end
    ) const;

private:
    ReferenceDatabaseMetadata metadata_;
    ContigTable contigs_;
    std::vector<ReferenceDatabaseRecord> records_;
    std::vector<ReferenceFeatureRecord> features_;
    VariantCoordinateIndex coordinate_index_;
    VariantCoordinateIndex feature_coordinate_index_;
};

class ReferenceDatabaseRegistry final {
public:
    ReferenceDatabaseRegistry() = default;

    void register_database(ReferenceDatabase database);

    [[nodiscard]] const ReferenceDatabase* find(
        std::string_view database_id,
        std::string_view version,
        const ReferenceAssemblyIdentity& assembly
    ) const noexcept;

    [[nodiscard]] std::vector<ReferenceDatabaseMetadata> list_metadata() const;
    [[nodiscard]] std::size_t size() const noexcept { return databases_.size(); }

private:
    std::vector<std::unique_ptr<ReferenceDatabase>> databases_;
};

}  // namespace biocore::domain
