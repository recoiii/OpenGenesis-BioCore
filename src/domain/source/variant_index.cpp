#include "biocore/domain/variant_index.hpp"
#include "biocore/domain/variant_workspace.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

namespace biocore::domain {

VariantCoordinateIndex VariantCoordinateIndex::build(const std::span<const VariantIndexEntry> entries) {
    VariantCoordinateIndex index;
    std::vector<VariantIndexEntry> sorted{entries.begin(), entries.end()};
    for (const auto& entry : sorted) {
        if (!entry.interval.valid() || entry.interval.start == entry.interval.end) {
            throw std::invalid_argument("variant index requires non-empty valid half-open intervals");
        }
    }

    std::ranges::sort(sorted, [](const VariantIndexEntry& left, const VariantIndexEntry& right) {
        if (left.interval.contig_id != right.interval.contig_id) {
            return left.interval.contig_id < right.interval.contig_id;
        }
        if (left.interval.start != right.interval.start) {
            return left.interval.start < right.interval.start;
        }
        if (left.interval.end != right.interval.end) {
            return left.interval.end < right.interval.end;
        }
        return left.ordinal < right.ordinal;
    });

    index.size_ = sorted.size();
    for (const auto& entry : sorted) {
        if (index.blocks_.empty() || index.blocks_.back().contig_id != entry.interval.contig_id) {
            index.blocks_.push_back(ContigBlock{entry.interval.contig_id, {}, {}});
        }
        auto& block = index.blocks_.back();
        block.entries.push_back(entry);
        const std::uint64_t previous_max = block.prefix_max_end.empty() ? 0U : block.prefix_max_end.back();
        block.prefix_max_end.push_back(std::max(previous_max, entry.interval.end));
    }
    return index;
}

std::vector<std::size_t> VariantCoordinateIndex::query(
    const ContigId contig_id,
    const std::uint64_t start,
    const std::uint64_t end
) const {
    if (start > end) {
        throw std::invalid_argument("variant index query end precedes start");
    }
    if (start == end) {
        return {};
    }

    const auto block_it = std::lower_bound(
        blocks_.begin(),
        blocks_.end(),
        contig_id,
        [](const ContigBlock& block, const ContigId id) { return block.contig_id < id; }
    );
    if (block_it == blocks_.end() || block_it->contig_id != contig_id) {
        return {};
    }

    const auto first_it = std::upper_bound(
        block_it->prefix_max_end.begin(),
        block_it->prefix_max_end.end(),
        start
    );
    std::size_t index = static_cast<std::size_t>(std::distance(block_it->prefix_max_end.begin(), first_it));
    std::vector<std::size_t> result;
    while (index < block_it->entries.size()) {
        const auto& entry = block_it->entries[index];
        if (entry.interval.start >= end) {
            break;
        }
        if (entry.interval.end > start) {
            result.push_back(entry.ordinal);
        }
        ++index;
    }
    return result;
}

namespace {

constexpr std::size_t maximum_workspace_window = 250U;
constexpr std::size_t maximum_workspace_search_bytes = 1024U;
constexpr std::size_t maximum_workspace_filter_tag_bytes = 1024U;

bool valid_variant_type(const VariantType type) noexcept {
    switch (type) {
        case VariantType::snv:
        case VariantType::insertion:
        case VariantType::deletion:
        case VariantType::mnv:
        case VariantType::delins_complex:
        case VariantType::unknown:
            return true;
    }
    return false;
}

bool valid_workspace_sort_key(const VariantWorkspaceSortKey key) noexcept {
    switch (key) {
        case VariantWorkspaceSortKey::coordinate:
        case VariantWorkspaceSortKey::quality:
        case VariantWorkspaceSortKey::carrier_count:
        case VariantWorkspaceSortKey::allele_p_value:
        case VariantWorkspaceSortKey::carrier_p_value:
        case VariantWorkspaceSortKey::allele_fdr_q:
        case VariantWorkspaceSortKey::carrier_fdr_q:
            return true;
    }
    return false;
}

bool valid_workspace_sort_direction(const VariantWorkspaceSortDirection direction) noexcept {
    switch (direction) {
        case VariantWorkspaceSortDirection::ascending:
        case VariantWorkspaceSortDirection::descending:
            return true;
    }
    return false;
}

void validate_probability_threshold(const std::optional<double>& value, const char* message) {
    if (value.has_value() && (!std::isfinite(*value) || *value < 0.0 || *value > 1.0)) {
        throw std::invalid_argument(message);
    }
}

std::string workspace_filter_status(const VariantWorkspaceRecordMetadata* metadata) {
    if (metadata == nullptr || !metadata->filters_applied) {
        return ".";
    }
    if (metadata->filters.empty()) {
        return "PASS";
    }
    std::string value;
    for (std::size_t index = 0U; index < metadata->filters.size(); ++index) {
        if (index != 0U) {
            value.push_back(';');
        }
        value += metadata->filters[index];
    }
    return value;
}

bool filter_status_contains(const std::string_view status, const std::string_view tag) {
    if (status == tag) {
        return true;
    }
    std::size_t start = 0U;
    while (start < status.size()) {
        const std::size_t end = status.find(';', start);
        const std::size_t length = end == std::string_view::npos ? status.size() - start : end - start;
        if (status.substr(start, length) == tag) {
            return true;
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1U;
    }
    return false;
}

std::string ascii_lower(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        result.push_back(static_cast<char>(std::tolower(byte)));
    }
    return result;
}

bool contains_lowered(const std::string_view value, const std::string& lowered_needle) {
    if (lowered_needle.empty()) {
        return true;
    }
    return ascii_lower(value).find(lowered_needle) != std::string::npos;
}

bool annotation_matches_search(const VariantAnnotationResult& annotation, const std::string& lowered_needle) {
    const auto attribute_matches = [&](const ReferenceDatabaseAttribute& attribute) {
        return contains_lowered(attribute.key, lowered_needle)
            || (attribute.value.has_value() && contains_lowered(*attribute.value, lowered_needle));
    };
    for (const auto& alternate : annotation.alternates) {
        for (const auto& allele : alternate.allele_annotations) {
            if (contains_lowered(allele.record_id, lowered_needle)
                || contains_lowered(allele.provenance.database_id, lowered_needle)
                || contains_lowered(allele.provenance.display_name, lowered_needle)
                || std::any_of(allele.attributes.begin(), allele.attributes.end(), attribute_matches)) {
                return true;
            }
        }
        for (const auto& feature : alternate.feature_annotations) {
            if (contains_lowered(feature.source, lowered_needle)
                || contains_lowered(feature.feature_type, lowered_needle)
                || contains_lowered(feature.provenance.database_id, lowered_needle)
                || contains_lowered(feature.provenance.display_name, lowered_needle)
                || std::any_of(feature.attributes.begin(), feature.attributes.end(), attribute_matches)) {
                return true;
            }
        }
    }
    return false;
}

bool same_allele(const Allele& left, const Allele& right) noexcept {
    return left.sequence == right.sequence && left.type == right.type && left.symbolic == right.symbolic;
}

bool same_interval(const GenomicInterval& left, const GenomicInterval& right) noexcept {
    return left.contig_id == right.contig_id && left.start == right.start && left.end == right.end;
}

bool row_coordinate_less(const VariantWorkspaceRowDto& left, const VariantWorkspaceRowDto& right) {
    return std::tie(left.contig_id, left.start, left.end, left.reference_allele, left.alternate_allele, left.variant_index)
        < std::tie(right.contig_id, right.start, right.end, right.reference_allele, right.alternate_allele, right.variant_index);
}

template <typename T>
bool nullable_less_with_direction(
    const std::optional<T>& left,
    const std::optional<T>& right,
    const VariantWorkspaceSortDirection direction,
    const VariantWorkspaceRowDto& left_row,
    const VariantWorkspaceRowDto& right_row
) {
    if (left.has_value() != right.has_value()) {
        return left.has_value();
    }
    if (left.has_value() && *left != *right) {
        return direction == VariantWorkspaceSortDirection::ascending ? *left < *right : *left > *right;
    }
    return row_coordinate_less(left_row, right_row);
}

std::size_t annotation_allele_count(const VariantAnnotationResult& annotation) {
    std::size_t count = 0U;
    for (const auto& alternate : annotation.alternates) {
        count += alternate.allele_annotations.size();
    }
    return count;
}

std::size_t annotation_feature_count(const VariantAnnotationResult& annotation) {
    std::size_t count = 0U;
    for (const auto& alternate : annotation.alternates) {
        count += alternate.feature_annotations.size();
    }
    return count;
}

std::optional<std::string> primary_annotation_id(const VariantAnnotationResult& annotation) {
    for (const auto& alternate : annotation.alternates) {
        if (!alternate.allele_annotations.empty()) {
            return alternate.allele_annotations.front().record_id;
        }
    }
    return std::nullopt;
}

std::optional<std::string> primary_feature_type(const VariantAnnotationResult& annotation) {
    for (const auto& alternate : annotation.alternates) {
        if (!alternate.feature_annotations.empty()) {
            return alternate.feature_annotations.front().feature_type;
        }
    }
    return std::nullopt;
}

}  // namespace

void validate_variant_workspace_query(const VariantWorkspaceQuery& query) {
    if (query.limit == 0U || query.limit > maximum_workspace_window) {
        throw std::invalid_argument("variant workspace query limit must be in [1,250]");
    }
    if (query.start.has_value() != query.end.has_value()) {
        throw std::invalid_argument("variant workspace genomic range requires both start and end");
    }
    if ((query.start.has_value() || query.end.has_value()) && !query.contig_id.has_value()) {
        throw std::invalid_argument("variant workspace genomic range requires a contig");
    }
    if (query.start.has_value() && *query.start >= *query.end) {
        throw std::invalid_argument("variant workspace genomic range must be a non-empty half-open interval");
    }
    if (query.minimum_quality.has_value()
        && (!std::isfinite(*query.minimum_quality) || *query.minimum_quality < 0.0)) {
        throw std::invalid_argument("variant workspace minimum quality is invalid");
    }
    if (query.maximum_quality.has_value()
        && (!std::isfinite(*query.maximum_quality) || *query.maximum_quality < 0.0)) {
        throw std::invalid_argument("variant workspace maximum quality is invalid");
    }
    if (query.minimum_quality.has_value() && query.maximum_quality.has_value()
        && *query.minimum_quality > *query.maximum_quality) {
        throw std::invalid_argument("variant workspace quality range is inverted");
    }
    if (query.minimum_carrier_count.has_value() && query.maximum_carrier_count.has_value()
        && *query.minimum_carrier_count > *query.maximum_carrier_count) {
        throw std::invalid_argument("variant workspace carrier-count range is inverted");
    }
    validate_probability_threshold(query.maximum_allele_p_value, "variant workspace allele p-value threshold is invalid");
    validate_probability_threshold(query.maximum_carrier_p_value, "variant workspace carrier p-value threshold is invalid");
    validate_probability_threshold(query.maximum_allele_fdr_q, "variant workspace allele FDR threshold is invalid");
    validate_probability_threshold(query.maximum_carrier_fdr_q, "variant workspace carrier FDR threshold is invalid");
    if (query.search_text.size() > maximum_workspace_search_bytes) {
        throw std::invalid_argument("variant workspace search text exceeds maximum length");
    }
    if (query.filter_tag.has_value()
        && (query.filter_tag->empty() || query.filter_tag->size() > maximum_workspace_filter_tag_bytes)) {
        throw std::invalid_argument("variant workspace filter tag is invalid");
    }
    for (const auto type : query.variant_types) {
        if (!valid_variant_type(type)) {
            throw std::invalid_argument("variant workspace variant type is invalid");
        }
    }
    if (!valid_workspace_sort_key(query.sort_key)) {
        throw std::invalid_argument("variant workspace sort key is invalid");
    }
    if (!valid_workspace_sort_direction(query.sort_direction)) {
        throw std::invalid_argument("variant workspace sort direction is invalid");
    }
}

VariantAnalysisWorkspace VariantAnalysisWorkspace::build(
    const MultiSampleMatrix& matrix,
    const CaseControlAssociationResult* associations,
    const std::vector<VariantAnnotationResult>* annotations,
    const std::vector<VariantWorkspaceRecordMetadata>* metadata
) {
    if (associations != nullptr && associations->variants.size() != matrix.variant_count()) {
        throw std::invalid_argument("variant workspace association cardinality does not match matrix variants");
    }
    if (annotations != nullptr && annotations->size() != matrix.variant_count()) {
        throw std::invalid_argument("variant workspace annotation cardinality does not match matrix variants");
    }
    if (metadata != nullptr && metadata->size() != matrix.variant_count()) {
        throw std::invalid_argument("variant workspace metadata cardinality does not match matrix variants");
    }

    VariantAnalysisWorkspace workspace;
    workspace.matrix_ = &matrix;
    workspace.associations_ = associations;
    workspace.annotations_ = annotations;
    workspace.rows_.reserve(matrix.variant_count());
    std::vector<VariantIndexEntry> index_entries;
    index_entries.reserve(matrix.variant_count());

    for (std::size_t variant_index = 0U; variant_index < matrix.variant_count(); ++variant_index) {
        const auto& variant = matrix.variant(variant_index);
        const auto contig_name = matrix.contigs().canonical_name(variant.locus.contig_id);
        if (!contig_name.has_value()) {
            throw std::logic_error("variant workspace matrix variant references an unknown contig");
        }
        if (!variant.locus.valid() || variant.locus.start == variant.locus.end) {
            throw std::logic_error("variant workspace matrix variant has an invalid interval");
        }

        VariantWorkspaceRowDto row;
        row.variant_index = variant_index;
        row.contig_id = variant.locus.contig_id;
        row.contig_name = std::string{*contig_name};
        row.start = variant.locus.start;
        row.end = variant.locus.end;
        row.reference_allele = variant.reference.sequence;
        row.alternate_allele = variant.alternate.sequence;
        row.variant_type = variant.alternate.type;
        row.symbolic = variant.alternate.symbolic;

        if (metadata != nullptr) {
            const auto& item = (*metadata)[variant_index];
            if (item.quality.has_value() && (!std::isfinite(*item.quality) || *item.quality < 0.0)) {
                throw std::invalid_argument("variant workspace metadata quality is invalid");
            }
            for (const auto& filter : item.filters) {
                if (filter.empty() || filter.size() > maximum_workspace_filter_tag_bytes) {
                    throw std::invalid_argument("variant workspace metadata filter tag is invalid");
                }
            }
            row.record_id = item.record_id;
            row.quality = item.quality;
            row.filter_status = workspace_filter_status(&item);
        }

        for (const auto ordinal : matrix.variant_observation_ordinals(variant_index)) {
            const auto& observation = matrix.observation(ordinal);
            const auto& call = matrix.call(observation.call_index);
            if (call.state == MultiSampleCallState::complete_call) {
                ++row.total_complete_calls;
                if (observation.alternate_dosage > 0U) {
                    ++row.carrier_sample_count;
                }
            }
        }

        if (annotations != nullptr) {
            const auto& annotation = (*annotations)[variant_index];
            if (!same_interval(annotation.locus, variant.locus)
                || !same_allele(annotation.reference, variant.reference)
                || annotation.alternates.size() != 1U
                || annotation.alternates.front().alternate_index != 0U
                || !same_allele(annotation.alternates.front().alternate, variant.alternate)) {
                throw std::invalid_argument("variant workspace annotation is not aligned to its matrix variant");
            }
            row.annotation_available = true;
            row.allele_annotation_count = annotation_allele_count(annotation);
            row.feature_annotation_count = annotation_feature_count(annotation);
            row.primary_annotation_id = primary_annotation_id(annotation);
            row.primary_feature_type = primary_feature_type(annotation);
        }

        if (associations != nullptr) {
            const auto& association = associations->variants[variant_index];
            if (association.variant_index != variant_index) {
                throw std::invalid_argument("variant workspace association ordering does not match matrix variants");
            }
            row.association_available = true;
            row.allele_odds_ratio = association.allele_odds_ratio;
            row.carrier_odds_ratio = association.carrier_odds_ratio;
            row.allele_odds_ratio_ci95 = association.allele_odds_ratio_ci95;
            row.carrier_odds_ratio_ci95 = association.carrier_odds_ratio_ci95;
            row.allele_fisher_two_sided_p = association.allele_fisher_two_sided_p;
            row.carrier_fisher_two_sided_p = association.carrier_fisher_two_sided_p;
            row.allele_bh_adjusted_q = association.allele_bh_adjusted_q;
            row.carrier_bh_adjusted_q = association.carrier_bh_adjusted_q;
        }

        index_entries.push_back({variant.locus, variant_index});
        workspace.rows_.push_back(std::move(row));
    }
    workspace.coordinate_index_ = VariantCoordinateIndex::build(index_entries);
    return workspace;
}

VariantWorkspaceBoundedQueryResult VariantAnalysisWorkspace::query(const VariantWorkspaceQuery& request) const {
    validate_variant_workspace_query(request);
    if (matrix_ == nullptr) {
        throw std::logic_error("variant workspace is not initialized");
    }
    if (request.contig_id.has_value() && !matrix_->contigs().canonical_name(*request.contig_id).has_value()) {
        throw std::invalid_argument("variant workspace query references an unknown contig");
    }

    std::vector<std::size_t> candidates;
    if (request.contig_id.has_value() && request.start.has_value()) {
        candidates = coordinate_index_.query(*request.contig_id, *request.start, *request.end);
    } else {
        candidates.reserve(rows_.size());
        for (std::size_t index = 0U; index < rows_.size(); ++index) {
            candidates.push_back(index);
        }
    }

    const std::string lowered_search = ascii_lower(request.search_text);
    std::vector<std::size_t> matched;
    matched.reserve(candidates.size());
    for (const auto index : candidates) {
        const auto& row = rows_.at(index);
        if (request.contig_id.has_value() && row.contig_id != *request.contig_id) {
            continue;
        }
        if (!request.variant_types.empty()
            && std::find(request.variant_types.begin(), request.variant_types.end(), row.variant_type)
                == request.variant_types.end()) {
            continue;
        }
        if (request.minimum_quality.has_value()
            && (!row.quality.has_value() || *row.quality < *request.minimum_quality)) {
            continue;
        }
        if (request.maximum_quality.has_value()
            && (!row.quality.has_value() || *row.quality > *request.maximum_quality)) {
            continue;
        }
        if (request.pass_only && row.filter_status != "PASS") {
            continue;
        }
        if (request.filter_tag.has_value() && !filter_status_contains(row.filter_status, *request.filter_tag)) {
            continue;
        }
        if (request.minimum_carrier_count.has_value() && row.carrier_sample_count < *request.minimum_carrier_count) {
            continue;
        }
        if (request.maximum_carrier_count.has_value() && row.carrier_sample_count > *request.maximum_carrier_count) {
            continue;
        }
        if (request.maximum_allele_p_value.has_value()
            && (!row.allele_fisher_two_sided_p.has_value()
                || *row.allele_fisher_two_sided_p > *request.maximum_allele_p_value)) {
            continue;
        }
        if (request.maximum_carrier_p_value.has_value()
            && (!row.carrier_fisher_two_sided_p.has_value()
                || *row.carrier_fisher_two_sided_p > *request.maximum_carrier_p_value)) {
            continue;
        }
        if (request.maximum_allele_fdr_q.has_value()
            && (!row.allele_bh_adjusted_q.has_value() || *row.allele_bh_adjusted_q > *request.maximum_allele_fdr_q)) {
            continue;
        }
        if (request.maximum_carrier_fdr_q.has_value()
            && (!row.carrier_bh_adjusted_q.has_value() || *row.carrier_bh_adjusted_q > *request.maximum_carrier_fdr_q)) {
            continue;
        }
        if (!lowered_search.empty()) {
            bool text_match = contains_lowered(row.contig_name, lowered_search)
                || contains_lowered(row.reference_allele, lowered_search)
                || contains_lowered(row.alternate_allele, lowered_search)
                || contains_lowered(to_string(row.variant_type), lowered_search)
                || (row.record_id.has_value() && contains_lowered(*row.record_id, lowered_search));
            if (!text_match && annotations_ != nullptr) {
                text_match = annotation_matches_search((*annotations_)[row.variant_index], lowered_search);
            }
            if (!text_match) {
                continue;
            }
        }
        matched.push_back(index);
    }

    const auto direction = request.sort_direction;
    const auto tie_less = [&](const std::size_t left_index, const std::size_t right_index) {
        const auto& left = rows_[left_index];
        const auto& right = rows_[right_index];
        switch (request.sort_key) {
            case VariantWorkspaceSortKey::coordinate:
                return direction == VariantWorkspaceSortDirection::ascending
                    ? row_coordinate_less(left, right)
                    : row_coordinate_less(right, left);
            case VariantWorkspaceSortKey::quality:
                return nullable_less_with_direction(left.quality, right.quality, direction, left, right);
            case VariantWorkspaceSortKey::carrier_count:
                if (left.carrier_sample_count != right.carrier_sample_count) {
                    return direction == VariantWorkspaceSortDirection::ascending
                        ? left.carrier_sample_count < right.carrier_sample_count
                        : left.carrier_sample_count > right.carrier_sample_count;
                }
                return row_coordinate_less(left, right);
            case VariantWorkspaceSortKey::allele_p_value:
                return nullable_less_with_direction(left.allele_fisher_two_sided_p, right.allele_fisher_two_sided_p, direction, left, right);
            case VariantWorkspaceSortKey::carrier_p_value:
                return nullable_less_with_direction(left.carrier_fisher_two_sided_p, right.carrier_fisher_two_sided_p, direction, left, right);
            case VariantWorkspaceSortKey::allele_fdr_q:
                return nullable_less_with_direction(left.allele_bh_adjusted_q, right.allele_bh_adjusted_q, direction, left, right);
            case VariantWorkspaceSortKey::carrier_fdr_q:
                return nullable_less_with_direction(left.carrier_bh_adjusted_q, right.carrier_bh_adjusted_q, direction, left, right);
        }
        return row_coordinate_less(left, right);
    };
    std::sort(matched.begin(), matched.end(), tie_less);

    VariantWorkspaceBoundedQueryResult result;
    result.total_unfiltered_variants = static_cast<std::uint64_t>(rows_.size());
    result.total_matched_variants = static_cast<std::uint64_t>(matched.size());
    result.window_offset = request.offset;
    result.query_generation_id = request.query_generation_id;
    if (request.offset >= matched.size()) {
        return result;
    }
    const std::size_t available = matched.size() - request.offset;
    const std::size_t count = std::min(request.limit, available);
    result.rows.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        result.rows.push_back(rows_[matched[request.offset + index]]);
    }
    return result;
}

VariantWorkspaceDetailDto VariantAnalysisWorkspace::detail(const std::size_t variant_index) const {
    if (matrix_ == nullptr) {
        throw std::logic_error("variant workspace is not initialized");
    }
    if (variant_index >= rows_.size()) {
        throw std::out_of_range("variant workspace detail index is out of range");
    }

    VariantWorkspaceDetailDto result;
    result.row = rows_[variant_index];
    const auto ordinals = matrix_->variant_observation_ordinals(variant_index);
    result.sample_calls.reserve(ordinals.size());
    for (const auto ordinal : ordinals) {
        const auto& observation = matrix_->observation(ordinal);
        const auto& sample = matrix_->sample(observation.sample_index);
        const auto& call = matrix_->call(observation.call_index);
        VariantWorkspaceSampleCallDto item;
        item.sample_id = sample.sample_id;
        item.state = call.state;
        item.genotype_present = call.genotype_present;
        item.genotype = call.genotype;
        item.reference_dosage = observation.reference_dosage;
        item.alternate_dosage = observation.alternate_dosage;
        item.other_alternate_dosage = observation.other_alternate_dosage;
        item.missing_allele_count = observation.missing_allele_count;
        item.source_alternate_index = observation.source_alternate_index;
        item.extra_format_fields = call.extra_format_fields;
        item.provenance = matrix_->record_provenance(call.record_provenance_index);
        result.sample_calls.push_back(std::move(item));
    }
    if (annotations_ != nullptr) {
        result.annotation = (*annotations_)[variant_index];
    }
    if (associations_ != nullptr) {
        result.association = associations_->variants[variant_index];
    }
    return result;
}

}  // namespace biocore::domain
