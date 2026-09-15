#include "biocore/domain/multi_sample_matrix.hpp"

#include <algorithm>
#include <stdexcept>

namespace biocore::domain {

void validate_multi_sample_matrix_options(const MultiSampleMatrixOptions& options) {
    if (options.maximum_sources == 0U) {
        throw std::invalid_argument("maximum matrix sources must be positive");
    }
    if (options.maximum_samples == 0U) {
        throw std::invalid_argument("maximum matrix samples must be positive");
    }
    if (options.maximum_variant_alleles == 0U) {
        throw std::invalid_argument("maximum matrix variant alleles must be positive");
    }
    if (options.maximum_observations == 0U) {
        throw std::invalid_argument("maximum matrix observations must be positive");
    }
}

const MultiSampleMatrixSample& MultiSampleMatrix::sample(const std::size_t index) const {
    if (index >= samples_.size()) {
        throw std::out_of_range("multi-sample matrix sample index is out of range");
    }
    return samples_[index];
}

const MultiSampleMatrixVariant& MultiSampleMatrix::variant(const std::size_t index) const {
    if (index >= variants_.size()) {
        throw std::out_of_range("multi-sample matrix variant index is out of range");
    }
    return variants_[index];
}

const MultiSampleMatrixObservation& MultiSampleMatrix::observation(const std::size_t index) const {
    if (index >= observations_.size()) {
        throw std::out_of_range("multi-sample matrix observation index is out of range");
    }
    return observations_[index];
}

const MultiSampleMatrixCall& MultiSampleMatrix::call(const std::size_t index) const {
    if (index >= calls_.size()) {
        throw std::out_of_range("multi-sample matrix call index is out of range");
    }
    return calls_[index];
}

const MultiSampleMatrixRecordProvenance& MultiSampleMatrix::record_provenance(const std::size_t index) const {
    if (index >= record_provenance_.size()) {
        throw std::out_of_range("multi-sample matrix record provenance index is out of range");
    }
    return record_provenance_[index];
}

std::optional<std::size_t> MultiSampleMatrix::sample_index(const std::string_view sample_id) const noexcept {
    const auto it = std::lower_bound(samples_.begin(), samples_.end(), sample_id, [](const auto& sample_value, const std::string_view key) {
        return sample_value.sample_id < key;
    });
    if (it == samples_.end() || it->sample_id != sample_id) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(samples_.begin(), it));
}

std::span<const MultiSampleMatrixObservation> MultiSampleMatrix::sample_observations(const std::size_t sample_index_value) const {
    if (sample_index_value >= samples_.size()) {
        throw std::out_of_range("multi-sample matrix sample index is out of range");
    }
    const auto begin = row_offsets_[sample_index_value];
    const auto end = row_offsets_[sample_index_value + 1U];
    const auto* data = observations_.empty() ? nullptr : observations_.data() + begin;
    return std::span<const MultiSampleMatrixObservation>{data, end - begin};
}

std::span<const std::size_t> MultiSampleMatrix::variant_observation_ordinals(const std::size_t variant_index_value) const {
    if (variant_index_value >= variants_.size()) {
        throw std::out_of_range("multi-sample matrix variant index is out of range");
    }
    const auto begin = column_offsets_[variant_index_value];
    const auto end = column_offsets_[variant_index_value + 1U];
    const auto* data = column_observation_ordinals_.empty() ? nullptr : column_observation_ordinals_.data() + begin;
    return std::span<const std::size_t>{data, end - begin};
}

const MultiSampleMatrixObservation* MultiSampleMatrix::find_observation(
    const std::size_t sample_index_value,
    const std::size_t variant_index_value
) const {
    if (sample_index_value >= samples_.size()) {
        throw std::out_of_range("multi-sample matrix sample index is out of range");
    }
    if (variant_index_value >= variants_.size()) {
        throw std::out_of_range("multi-sample matrix variant index is out of range");
    }
    const auto row = sample_observations(sample_index_value);
    const auto it = std::lower_bound(row.begin(), row.end(), variant_index_value, [](const auto& observation_value, const std::size_t key) {
        return observation_value.variant_index < key;
    });
    if (it == row.end() || it->variant_index != variant_index_value) {
        return nullptr;
    }
    return &*it;
}

VariantRecord MultiSampleMatrix::variant_record(const std::size_t variant_index_value) const {
    const auto& value = variant(variant_index_value);
    VariantRecord record;
    record.locus = value.locus;
    record.reference = value.reference;
    record.alternates.push_back(value.alternate);
    return record;
}

}  // namespace biocore::domain
