#include "biocore/domain/multi_sample_matrix.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace biocore::domain {
namespace {

constexpr std::size_t maximum_identifier_bytes = 4096U;

struct VariantKey final {
    ContigId contig_id{0U};
    std::uint64_t start{0U};
    std::uint64_t end{0U};
    std::string reference;
    std::string alternate;
    bool alternate_symbolic{false};
    VariantType alternate_type{VariantType::unknown};

    friend bool operator<(const VariantKey& left, const VariantKey& right) noexcept {
        return std::tie(left.contig_id, left.start, left.end, left.reference, left.alternate,
                        left.alternate_symbolic, left.alternate_type)
             < std::tie(right.contig_id, right.start, right.end, right.reference, right.alternate,
                        right.alternate_symbolic, right.alternate_type);
    }
};

void validate_identifier(const std::string& value, const char* what) {
    if (value.empty() || value.size() > maximum_identifier_bytes) {
        throw std::invalid_argument(what);
    }
}

[[nodiscard]] VariantKey make_key(
    const ContigTable& source_contigs,
    const ContigTable& target_contigs,
    const VariantRecord& variant,
    const std::size_t alternate_index
) {
    if (alternate_index >= variant.alternates.size()) {
        throw std::out_of_range("matrix alternate index is out of range");
    }
    const auto source_name = source_contigs.canonical_name(variant.locus.contig_id);
    if (!source_name.has_value()) {
        throw std::invalid_argument("matrix source variant contig ID is not registered");
    }
    const auto target_id = target_contigs.resolve(*source_name);
    if (!target_id.has_value()) {
        throw std::invalid_argument("matrix target contig table does not contain a source variant contig");
    }
    const auto& alternate = variant.alternates[alternate_index];
    return VariantKey{
        *target_id,
        variant.locus.start,
        variant.locus.end,
        variant.reference.sequence,
        alternate.sequence,
        alternate.symbolic,
        alternate.type
    };
}

[[nodiscard]] MultiSampleCallState call_state(const bool genotype_present, const GenotypeCall& genotype) noexcept {
    if (!genotype_present || genotype.fully_missing()) {
        return MultiSampleCallState::no_call;
    }
    if (genotype.partially_missing()) {
        return MultiSampleCallState::partial_call;
    }
    return MultiSampleCallState::complete_call;
}

void compute_dosages(
    const GenotypeCall& genotype,
    const bool genotype_present,
    const std::size_t selected_allele_index,
    std::uint32_t& reference,
    std::uint32_t& alternate,
    std::uint32_t& other,
    std::uint32_t& missing
) {
    if (!genotype_present) {
        return;
    }
    if (genotype.ploidy() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::overflow_error("genotype ploidy exceeds matrix dosage representation");
    }
    if (selected_allele_index > static_cast<std::size_t>(std::numeric_limits<std::int16_t>::max())) {
        throw std::overflow_error("selected allele index exceeds genotype representation");
    }
    const auto selected = static_cast<std::int16_t>(selected_allele_index);
    for (const auto allele : genotype.allele_indices.values()) {
        if (allele == missing_allele_index) {
            ++missing;
        } else if (allele == 0) {
            ++reference;
        } else if (allele == selected) {
            ++alternate;
        } else {
            ++other;
        }
    }
}

void require_room(const std::size_t current, const std::size_t maximum, const char* what) {
    if (current >= maximum) {
        throw std::length_error(what);
    }
}

void require_capacity(
    const std::size_t current,
    const std::size_t incoming,
    const std::size_t maximum,
    const char* what
) {
    if (incoming > maximum || current > maximum - incoming) {
        throw std::length_error(what);
    }
}

[[nodiscard]] std::size_t record_observation_count(const CanonicalVariantRecord& record) {
    const auto sample_count = record.samples.size();
    const auto alternate_count = record.variant.alternates.size();
    if (alternate_count != 0U && sample_count > std::numeric_limits<std::size_t>::max() / alternate_count) {
        throw std::overflow_error("matrix record observation cardinality overflow");
    }
    return sample_count * alternate_count;
}

void validate_source_record(
    const CanonicalVariantRecord& record,
    const VcfIngestionResult& ingestion
) {
    if (record.samples.size() != ingestion.header.sample_names.size()) {
        throw std::invalid_argument("matrix VCF sample cardinality does not match header");
    }
    if (const auto error = validate_variant_record(record.variant); error.has_value()) {
        throw std::invalid_argument("invalid variant in matrix source: " + *error);
    }
}

}  // namespace

MultiSampleMatrix build_multi_sample_matrix(
    const ReferenceAssemblyIdentity& assembly,
    ContigTable matrix_contigs,
    const std::vector<MultiSampleMatrixSource>& sources,
    const MultiSampleMatrixOptions& options
) {
    validate_reference_assembly_identity(assembly);
    validate_multi_sample_matrix_options(options);
    if (matrix_contigs.assembly() != assembly.assembly) {
        throw std::invalid_argument("matrix contig table assembly does not match matrix assembly identity");
    }
    if (sources.empty()) {
        throw std::invalid_argument("multi-sample matrix requires at least one source");
    }
    if (sources.size() > options.maximum_sources) {
        throw std::length_error("multi-sample matrix source limit exceeded");
    }

    std::set<std::string> source_ids;
    std::map<std::string, std::string> sample_sources;
    std::vector<const MultiSampleMatrixSource*> ordered_sources;
    ordered_sources.reserve(sources.size());
    for (const auto& source : sources) {
        validate_identifier(source.source_id, "matrix source ID is invalid");
        if (!source_ids.insert(source.source_id).second) {
            throw std::invalid_argument("duplicate matrix source ID");
        }
        validate_reference_assembly_identity(source.assembly);
        if (!(source.assembly == assembly)) {
            throw std::invalid_argument("matrix source assembly does not match matrix assembly");
        }
        if (source.contigs == nullptr || source.variants == nullptr) {
            throw std::invalid_argument("matrix source pointers must not be null");
        }
        if (source.contigs->assembly() != source.assembly.assembly) {
            throw std::invalid_argument("matrix source contig table assembly does not match source assembly identity");
        }
        if (source.variants->header.sample_names.empty()) {
            throw std::invalid_argument("matrix source must declare at least one sample");
        }
        for (const auto& sample_id : source.variants->header.sample_names) {
            validate_identifier(sample_id, "matrix sample ID is invalid");
            if (sample_sources.find(sample_id) != sample_sources.end()) {
                throw std::invalid_argument("duplicate sample ID across matrix sources");
            }
            require_room(sample_sources.size(), options.maximum_samples, "multi-sample matrix sample limit exceeded");
            sample_sources.emplace(sample_id, source.source_id);
        }
        ordered_sources.push_back(&source);
    }
    std::sort(ordered_sources.begin(), ordered_sources.end(), [](const auto* left, const auto* right) {
        return left->source_id < right->source_id;
    });

    MultiSampleMatrix matrix;
    matrix.assembly_ = assembly;
    matrix.contigs_ = std::move(matrix_contigs);
    matrix.samples_.reserve(sample_sources.size());
    std::map<std::string, std::size_t> sample_indices;
    for (const auto& [sample_id, source_id] : sample_sources) {
        const std::size_t index = matrix.samples_.size();
        matrix.samples_.push_back(MultiSampleMatrixSample{sample_id, source_id});
        sample_indices.emplace(sample_id, index);
    }

    // First pass: establish the deterministic union of exact variant alleles without materializing cells.
    std::map<VariantKey, MultiSampleMatrixVariant> unique_variants;
    for (const auto* source : ordered_sources) {
        const auto& ingestion = *source->variants;
        for (const auto& record : ingestion.records) {
            validate_source_record(record, ingestion);
            for (std::size_t alternate_index = 0U; alternate_index < record.variant.alternates.size(); ++alternate_index) {
                const auto key = make_key(*source->contigs, matrix.contigs_, record.variant, alternate_index);
                if (unique_variants.find(key) == unique_variants.end()) {
                    require_room(unique_variants.size(), options.maximum_variant_alleles, "multi-sample matrix variant-allele limit exceeded");
                    unique_variants.emplace(key, MultiSampleMatrixVariant{
                        {key.contig_id, key.start, key.end},
                        record.variant.reference,
                        record.variant.alternates[alternate_index]
                    });
                }
            }
        }
    }

    matrix.variants_.reserve(unique_variants.size());
    std::map<VariantKey, std::size_t> variant_indices;
    for (auto& [key, value] : unique_variants) {
        const std::size_t index = matrix.variants_.size();
        matrix.variants_.push_back(std::move(value));
        variant_indices.emplace(key, index);
    }

    // Second pass: retain each source sample/record call once, then project it onto exact ALT columns.
    for (const auto* source : ordered_sources) {
        const auto& ingestion = *source->variants;
        for (std::size_t source_record_ordinal = 0U; source_record_ordinal < ingestion.records.size(); ++source_record_ordinal) {
            const auto& record = ingestion.records[source_record_ordinal];
            validate_source_record(record, ingestion);
            require_capacity(
                matrix.observations_.size(),
                record_observation_count(record),
                options.maximum_observations,
                "multi-sample matrix observation limit exceeded"
            );
            const std::size_t provenance_index = matrix.record_provenance_.size();
            matrix.record_provenance_.push_back(MultiSampleMatrixRecordProvenance{
                source->source_id,
                record.id,
                source_record_ordinal
            });

            std::vector<std::size_t> record_call_indices;
            record_call_indices.reserve(record.samples.size());
            for (std::size_t source_sample_index = 0U; source_sample_index < record.samples.size(); ++source_sample_index) {
                const auto& sample_id = ingestion.header.sample_names[source_sample_index];
                const auto sample_it = sample_indices.find(sample_id);
                if (sample_it == sample_indices.end()) {
                    throw std::logic_error("matrix sample index was not registered");
                }
                const auto& sample_data = record.samples[source_sample_index];
                if (sample_data.genotype_present) {
                    if (const auto error = validate_genotype_call(sample_data.genotype, record.variant.alternates.size() + 1U); error.has_value()) {
                        throw std::invalid_argument("invalid genotype in matrix source: " + *error);
                    }
                }
                const std::size_t call_index = matrix.calls_.size();
                matrix.calls_.push_back(MultiSampleMatrixCall{
                    sample_it->second,
                    provenance_index,
                    call_state(sample_data.genotype_present, sample_data.genotype),
                    sample_data.genotype_present,
                    sample_data.genotype,
                    sample_data.extra_format_fields
                });
                record_call_indices.push_back(call_index);
            }

            for (std::size_t alternate_index = 0U; alternate_index < record.variant.alternates.size(); ++alternate_index) {
                const auto key = make_key(*source->contigs, matrix.contigs_, record.variant, alternate_index);
                const auto variant_it = variant_indices.find(key);
                if (variant_it == variant_indices.end()) {
                    throw std::logic_error("matrix variant index was not registered");
                }
                const std::size_t selected_genotype_allele = alternate_index + 1U;
                for (std::size_t source_sample_index = 0U; source_sample_index < record.samples.size(); ++source_sample_index) {
                    require_room(matrix.observations_.size(), options.maximum_observations, "multi-sample matrix observation limit exceeded");
                    const auto call_index = record_call_indices[source_sample_index];
                    const auto& call = matrix.calls_[call_index];
                    MultiSampleMatrixObservation item;
                    item.sample_index = call.sample_index;
                    item.variant_index = variant_it->second;
                    item.call_index = call_index;
                    compute_dosages(
                        call.genotype,
                        call.genotype_present,
                        selected_genotype_allele,
                        item.reference_dosage,
                        item.alternate_dosage,
                        item.other_alternate_dosage,
                        item.missing_allele_count
                    );
                    item.source_alternate_index = alternate_index;
                    matrix.observations_.push_back(std::move(item));
                }
            }
        }
    }

    std::sort(matrix.observations_.begin(), matrix.observations_.end(), [](const auto& left, const auto& right) {
        return std::tie(left.sample_index, left.variant_index) < std::tie(right.sample_index, right.variant_index);
    });
    for (std::size_t index = 1U; index < matrix.observations_.size(); ++index) {
        const auto& previous = matrix.observations_[index - 1U];
        const auto& current = matrix.observations_[index];
        if (previous.sample_index == current.sample_index && previous.variant_index == current.variant_index) {
            throw std::invalid_argument("duplicate sample/variant observation in multi-sample matrix");
        }
    }

    matrix.row_offsets_.assign(matrix.samples_.size() + 1U, 0U);
    for (const auto& item : matrix.observations_) {
        ++matrix.row_offsets_[item.sample_index + 1U];
    }
    for (std::size_t index = 1U; index < matrix.row_offsets_.size(); ++index) {
        matrix.row_offsets_[index] += matrix.row_offsets_[index - 1U];
    }

    matrix.column_offsets_.assign(matrix.variants_.size() + 1U, 0U);
    for (const auto& item : matrix.observations_) {
        ++matrix.column_offsets_[item.variant_index + 1U];
    }
    for (std::size_t index = 1U; index < matrix.column_offsets_.size(); ++index) {
        matrix.column_offsets_[index] += matrix.column_offsets_[index - 1U];
    }
    matrix.column_observation_ordinals_.resize(matrix.observations_.size());
    auto next = matrix.column_offsets_;
    for (std::size_t ordinal = 0U; ordinal < matrix.observations_.size(); ++ordinal) {
        const auto variant_index_value = matrix.observations_[ordinal].variant_index;
        matrix.column_observation_ordinals_[next[variant_index_value]++] = ordinal;
    }

    return matrix;
}

}  // namespace biocore::domain
