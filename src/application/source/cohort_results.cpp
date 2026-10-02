#include "biocore/application/cohort_results.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace biocore::application {
namespace {

void validate_probability(const std::optional<double>& value, const char* name) {
    if (value.has_value() &&
        (!std::isfinite(*value) || *value < 0.0 || *value > 1.0)) {
        throw std::invalid_argument(std::string{name} + " must be finite and in [0,1]");
    }
}

[[nodiscard]] std::string optional_number(const std::optional<double>& value) {
    if (!value.has_value()) return "null";
    std::ostringstream stream;
    stream << std::setprecision(17) << *value;
    return stream.str();
}

}  // namespace

std::string_view to_string(const CohortResultVariantView value) noexcept {
    switch (value) {
        case CohortResultVariantView::all: return "all";
        case CohortResultVariantView::shared: return "shared";
        case CohortResultVariantView::case_specific: return "case-specific";
        case CohortResultVariantView::control_specific: return "control-specific";
    }
    return "all";
}

std::string_view to_string(const CohortResultSortKey value) noexcept {
    switch (value) {
        case CohortResultSortKey::coordinate: return "coordinate";
        case CohortResultSortKey::allele_p_value: return "allele-p";
        case CohortResultSortKey::carrier_p_value: return "carrier-p";
        case CohortResultSortKey::allele_fdr_q: return "allele-q";
        case CohortResultSortKey::carrier_fdr_q: return "carrier-q";
    }
    return "coordinate";
}

std::string_view to_string(const CohortResultSortDirection value) noexcept {
    switch (value) {
        case CohortResultSortDirection::ascending: return "ascending";
        case CohortResultSortDirection::descending: return "descending";
    }
    return "ascending";
}

void validate_cohort_result_query(const CohortResultQuery& query) {
    constexpr std::size_t maximum_page_size = 200U;
    constexpr std::size_t maximum_offset = 10000000U;
    constexpr std::size_t maximum_search_bytes = 512U;
    if (query.limit == 0U || query.limit > maximum_page_size) {
        throw std::invalid_argument("cohort result page limit must be in [1,200]");
    }
    if (query.offset > maximum_offset) {
        throw std::invalid_argument("cohort result page offset exceeds the supported bound");
    }
    if (query.search_text.size() > maximum_search_bytes ||
        query.search_text.find('\0') != std::string::npos) {
        throw std::invalid_argument("cohort result search text is invalid");
    }
    if (query.contig.has_value() &&
        (query.contig->empty() || query.contig->size() > 256U ||
         query.contig->find('\0') != std::string::npos)) {
        throw std::invalid_argument("cohort result contig filter is invalid");
    }
    validate_probability(query.maximum_allele_p_value, "maximum allele p-value");
    validate_probability(query.maximum_carrier_p_value, "maximum carrier p-value");
    validate_probability(query.maximum_allele_fdr_q, "maximum allele FDR q-value");
    validate_probability(query.maximum_carrier_fdr_q, "maximum carrier FDR q-value");
}

std::string canonical_cohort_result_filter(const CohortResultQuery& query) {
    validate_cohort_result_query(query);
    return "view=" + std::string{to_string(query.variant_view)} +
           ";contig=" + (query.contig.has_value() ? *query.contig : "*") +
           ";annotated=" + std::string{query.annotated_only ? "1" : "0"} +
           ";allele_p=" + optional_number(query.maximum_allele_p_value) +
           ";carrier_p=" + optional_number(query.maximum_carrier_p_value) +
           ";allele_q=" + optional_number(query.maximum_allele_fdr_q) +
           ";carrier_q=" + optional_number(query.maximum_carrier_fdr_q) +
           ";search=" + query.search_text +
           ";sort=" + std::string{to_string(query.sort_key)} +
           ";direction=" + std::string{to_string(query.sort_direction)};
}

CohortResultAnnotationLink make_cohort_result_annotation_link(
    const std::size_t ordinal,
    const domain::VariantAnnotationResult& annotation
) {
    CohortResultAnnotationLink result;
    result.ordinal = ordinal;
    std::set<std::string, std::less<>> databases;
    for (const auto& alternate : annotation.alternates) {
        for (const auto& value : alternate.allele_annotations) {
            databases.emplace(value.provenance.database_id);
            if (!result.primary_record_id.has_value() && !value.record_id.empty()) {
                result.primary_record_id = value.record_id;
            }
        }
        for (const auto& value : alternate.feature_annotations) {
            databases.emplace(value.provenance.database_id);
            if (!result.primary_feature_type.has_value() && !value.feature_type.empty()) {
                result.primary_feature_type = value.feature_type;
            }
        }
    }
    result.database_ids.assign(databases.begin(), databases.end());
    return result;
}

}  // namespace biocore::application
