#pragma once

#include "biocore/domain/reference_database.hpp"
#include "biocore/domain/vcf_ingestion.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace biocore::domain {

enum class MultiSampleCallState : std::uint8_t {
    no_call,
    partial_call,
    complete_call
};

struct MultiSampleMatrixOptions final {
    std::size_t maximum_sources{4096U};
    std::size_t maximum_samples{100000U};
    std::size_t maximum_variant_alleles{10000000U};
    std::size_t maximum_observations{100000000U};
};

struct MultiSampleMatrixSource final {
    std::string source_id;
    ReferenceAssemblyIdentity assembly;
    const ContigTable* contigs{nullptr};
    const VcfIngestionResult* variants{nullptr};
};

struct MultiSampleMatrixSample final {
    std::string sample_id;
    std::string source_id;

    friend bool operator==(const MultiSampleMatrixSample&, const MultiSampleMatrixSample&) = default;
};

struct MultiSampleMatrixVariant final {
    GenomicInterval locus;
    Allele reference;
    Allele alternate;

};

struct MultiSampleMatrixRecordProvenance final {
    std::string source_id;
    std::string record_id;
    std::size_t source_record_ordinal{0U};

    friend bool operator==(const MultiSampleMatrixRecordProvenance&, const MultiSampleMatrixRecordProvenance&) = default;
};

struct MultiSampleMatrixCall final {
    std::size_t sample_index{0U};
    std::size_t record_provenance_index{0U};
    MultiSampleCallState state{MultiSampleCallState::no_call};
    bool genotype_present{false};
    GenotypeCall genotype;
    std::vector<DynamicVariantField> extra_format_fields;
};

struct MultiSampleMatrixObservation final {
    std::size_t sample_index{0U};
    std::size_t variant_index{0U};
    std::size_t call_index{0U};
    std::uint32_t reference_dosage{0U};
    std::uint32_t alternate_dosage{0U};
    std::uint32_t other_alternate_dosage{0U};
    std::uint32_t missing_allele_count{0U};
    std::size_t source_alternate_index{0U};
};

void validate_multi_sample_matrix_options(const MultiSampleMatrixOptions& options);

class MultiSampleMatrix final {
public:
    MultiSampleMatrix() = default;

    [[nodiscard]] const ReferenceAssemblyIdentity& assembly() const noexcept { return assembly_; }
    [[nodiscard]] const ContigTable& contigs() const noexcept { return contigs_; }
    [[nodiscard]] std::size_t sample_count() const noexcept { return samples_.size(); }
    [[nodiscard]] std::size_t variant_count() const noexcept { return variants_.size(); }
    [[nodiscard]] std::size_t observation_count() const noexcept { return observations_.size(); }
    [[nodiscard]] std::size_t call_count() const noexcept { return calls_.size(); }
    [[nodiscard]] std::size_t record_provenance_count() const noexcept { return record_provenance_.size(); }

    [[nodiscard]] const MultiSampleMatrixSample& sample(std::size_t index) const;
    [[nodiscard]] const MultiSampleMatrixVariant& variant(std::size_t index) const;
    [[nodiscard]] const MultiSampleMatrixObservation& observation(std::size_t index) const;
    [[nodiscard]] const MultiSampleMatrixCall& call(std::size_t index) const;
    [[nodiscard]] const MultiSampleMatrixRecordProvenance& record_provenance(std::size_t index) const;

    [[nodiscard]] std::optional<std::size_t> sample_index(std::string_view sample_id) const noexcept;
    [[nodiscard]] std::span<const MultiSampleMatrixObservation> sample_observations(std::size_t sample_index) const;
    [[nodiscard]] std::span<const std::size_t> variant_observation_ordinals(std::size_t variant_index) const;
    [[nodiscard]] const MultiSampleMatrixObservation* find_observation(
        std::size_t sample_index,
        std::size_t variant_index
    ) const;

    [[nodiscard]] VariantRecord variant_record(std::size_t variant_index) const;

private:
    friend MultiSampleMatrix build_multi_sample_matrix(
        const ReferenceAssemblyIdentity&,
        ContigTable,
        const std::vector<MultiSampleMatrixSource>&,
        const MultiSampleMatrixOptions&
    );

    ReferenceAssemblyIdentity assembly_;
    ContigTable contigs_;
    std::vector<MultiSampleMatrixSample> samples_;
    std::vector<MultiSampleMatrixVariant> variants_;
    std::vector<MultiSampleMatrixObservation> observations_;
    std::vector<MultiSampleMatrixCall> calls_;
    std::vector<MultiSampleMatrixRecordProvenance> record_provenance_;
    std::vector<std::size_t> row_offsets_;
    std::vector<std::size_t> column_offsets_;
    std::vector<std::size_t> column_observation_ordinals_;
};

[[nodiscard]] MultiSampleMatrix build_multi_sample_matrix(
    const ReferenceAssemblyIdentity& assembly,
    ContigTable matrix_contigs,
    const std::vector<MultiSampleMatrixSource>& sources,
    const MultiSampleMatrixOptions& options = {}
);

}  // namespace biocore::domain
