#include "biocore/domain/multi_sample_matrix.hpp"

#include <algorithm>
#include <map>
#include <set>
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

MultiSampleMatrix project_multi_sample_matrix_samples(
    const MultiSampleMatrix& matrix,
    const std::vector<std::string>& sample_ids
) {
    if (sample_ids.empty()) {
        throw std::invalid_argument("matrix sample projection requires at least one sample");
    }
    const std::set<std::string, std::less<>> requested{sample_ids.begin(), sample_ids.end()};
    if (requested.size() != sample_ids.size()) {
        throw std::invalid_argument("matrix sample projection contains duplicate sample IDs");
    }

    MultiSampleMatrix result;
    result.assembly_ = matrix.assembly_;
    result.contigs_ = matrix.contigs_;
    result.variants_ = matrix.variants_;
    result.row_offsets_.push_back(0U);

    std::vector<std::vector<std::size_t>> column_ordinals(matrix.variant_count());
    std::map<std::size_t, std::size_t> projected_provenance;
    std::map<std::size_t, std::size_t> projected_calls;
    std::size_t matched = 0U;

    for (std::size_t old_sample_index = 0U; old_sample_index < matrix.sample_count(); ++old_sample_index) {
        const auto& old_sample = matrix.sample(old_sample_index);
        if (!requested.contains(old_sample.sample_id)) continue;

        const std::size_t new_sample_index = result.samples_.size();
        result.samples_.push_back(old_sample);
        ++matched;

        for (const auto& old_observation : matrix.sample_observations(old_sample_index)) {
            if (old_observation.sample_index != old_sample_index) {
                throw std::logic_error("matrix projection encountered inconsistent sample observation");
            }
            const auto& old_call = matrix.call(old_observation.call_index);
            if (old_call.sample_index != old_sample_index) {
                throw std::logic_error("matrix projection encountered inconsistent call ownership");
            }

            std::size_t new_call_index = 0U;
            const auto existing = projected_calls.find(old_observation.call_index);
            if (existing == projected_calls.end()) {
                if (old_call.record_provenance_index >= matrix.record_provenance_count()) {
                    throw std::logic_error("matrix projection encountered invalid record provenance");
                }
                std::size_t new_provenance_index = 0U;
                const auto provenance = projected_provenance.find(old_call.record_provenance_index);
                if (provenance == projected_provenance.end()) {
                    new_provenance_index = result.record_provenance_.size();
                    result.record_provenance_.push_back(
                        matrix.record_provenance(old_call.record_provenance_index)
                    );
                    projected_provenance.emplace(
                        old_call.record_provenance_index, new_provenance_index
                    );
                } else {
                    new_provenance_index = provenance->second;
                }
                auto new_call = old_call;
                new_call.sample_index = new_sample_index;
                new_call.record_provenance_index = new_provenance_index;
                new_call_index = result.calls_.size();
                result.calls_.push_back(std::move(new_call));
                projected_calls.emplace(old_observation.call_index, new_call_index);
            } else {
                new_call_index = existing->second;
            }

            auto new_observation = old_observation;
            new_observation.sample_index = new_sample_index;
            new_observation.call_index = new_call_index;
            const std::size_t new_ordinal = result.observations_.size();
            result.observations_.push_back(std::move(new_observation));
            if (old_observation.variant_index >= column_ordinals.size()) {
                throw std::logic_error("matrix projection encountered invalid variant index");
            }
            column_ordinals[old_observation.variant_index].push_back(new_ordinal);
        }
        result.row_offsets_.push_back(result.observations_.size());
    }

    if (matched != requested.size()) {
        throw std::invalid_argument("matrix sample projection references an unknown sample ID");
    }

    result.column_offsets_.reserve(result.variant_count() + 1U);
    result.column_offsets_.push_back(0U);
    for (const auto& ordinals : column_ordinals) {
        result.column_observation_ordinals_.insert(
            result.column_observation_ordinals_.end(), ordinals.begin(), ordinals.end()
        );
        result.column_offsets_.push_back(result.column_observation_ordinals_.size());
    }
    return result;
}

}  // namespace biocore::domain
