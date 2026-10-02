#include "biocore/application/cohort_results_service.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace biocore::application {
namespace {

[[nodiscard]] std::string lower_copy(const std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char c : value) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return result;
}

[[nodiscard]] std::string assembly_name(const domain::ReferenceAssembly value) {
    switch (value) {
        case domain::ReferenceAssembly::unspecified: return "unspecified";
        case domain::ReferenceAssembly::grch37: return "GRCh37";
        case domain::ReferenceAssembly::grch38: return "GRCh38";
        case domain::ReferenceAssembly::custom: return "custom";
    }
    return "unspecified";
}

void validate_analysis_against_snapshot(
    const CohortAnalysisSnapshot& snapshot,
    const CohortCaseControlAnalysisResult& analysis
) {
    if (analysis.project_id != snapshot.project_id ||
        analysis.analysis_id != snapshot.analysis_id ||
        analysis.snapshot_digest != snapshot.snapshot_digest ||
        analysis.association_contract_version != snapshot.association_contract_version ||
        analysis.test_filter_version != snapshot.test_filter_version ||
        analysis.approved_case_samples != snapshot.approved_case_samples ||
        analysis.approved_control_samples != snapshot.approved_control_samples ||
        analysis.allele_family_size != snapshot.allele_family_size ||
        analysis.carrier_family_size != snapshot.carrier_family_size ||
        analysis.variants.size() != snapshot.test_universe.size()) {
        throw std::invalid_argument(
            "cohort results require an association result bound to the exact approved snapshot"
        );
    }

    for (std::size_t index = 0U; index < analysis.variants.size(); ++index) {
        const auto& row = analysis.variants[index];
        const auto& frozen = snapshot.test_universe[index];
        if (row.ordinal != index || frozen.ordinal != index ||
            row.contig != frozen.contig || row.start != frozen.start ||
            row.end != frozen.end || row.reference != frozen.reference ||
            row.alternate != frozen.alternate ||
            row.total_cases != snapshot.approved_case_samples ||
            row.total_controls != snapshot.approved_control_samples ||
            row.case_unobserved != frozen.case_unobserved ||
            row.control_unobserved != frozen.control_unobserved ||
            row.case_no_calls != frozen.case_no_calls ||
            row.control_no_calls != frozen.control_no_calls ||
            row.case_partial_calls != frozen.case_partial_calls ||
            row.control_partial_calls != frozen.control_partial_calls ||
            row.effective_case_samples != frozen.case_complete_calls ||
            row.effective_control_samples != frozen.control_complete_calls ||
            row.allele.family_member != frozen.allele_family_member ||
            row.carrier.family_member != frozen.carrier_family_member ||
            row.allele.fisher_two_sided_p.has_value() != row.allele.family_member ||
            row.allele.bh_adjusted_q.has_value() != row.allele.family_member ||
            row.carrier.fisher_two_sided_p.has_value() != row.carrier.family_member ||
            row.carrier.bh_adjusted_q.has_value() != row.carrier.family_member) {
            throw std::invalid_argument(
                "cohort result row identity, denominators or frozen family membership drifted"
            );
        }
    }
}

[[nodiscard]] std::map<std::size_t, CohortResultAnnotationLink> annotation_map(
    const std::vector<CohortResultAnnotationLink>& annotations,
    const std::size_t variant_count
) {
    std::map<std::size_t, CohortResultAnnotationLink> result;
    for (const auto& annotation : annotations) {
        if (annotation.ordinal >= variant_count) {
            throw std::invalid_argument("cohort result annotation ordinal is outside the frozen test universe");
        }
        if (!result.emplace(annotation.ordinal, annotation).second) {
            throw std::invalid_argument("duplicate cohort result annotation ordinal");
        }
    }
    return result;
}

[[nodiscard]] bool view_matches(
    const CohortAssociationVariantResult& row,
    const CohortResultVariantView view
) {
    const bool cases = row.carrier.table.case_exposed > 0U;
    const bool controls = row.carrier.table.control_exposed > 0U;
    switch (view) {
        case CohortResultVariantView::all: return true;
        case CohortResultVariantView::shared: return cases && controls;
        case CohortResultVariantView::case_specific: return cases && !controls;
        case CohortResultVariantView::control_specific: return !cases && controls;
    }
    return true;
}

[[nodiscard]] bool at_most(
    const std::optional<double>& value,
    const std::optional<double>& maximum
) {
    return !maximum.has_value() ||
           (value.has_value() && *value <= *maximum);
}

[[nodiscard]] bool text_matches(
    const CohortAssociationVariantResult& row,
    const std::optional<CohortResultAnnotationLink>& annotation,
    const std::string& needle
) {
    if (needle.empty()) return true;
    auto contains = [&](const std::string_view value) {
        return lower_copy(value).find(needle) != std::string::npos;
    };
    if (contains(row.contig) || contains(row.reference) || contains(row.alternate)) {
        return true;
    }
    if (!annotation.has_value()) return false;
    if (annotation->primary_record_id.has_value() && contains(*annotation->primary_record_id)) {
        return true;
    }
    if (annotation->primary_feature_type.has_value() && contains(*annotation->primary_feature_type)) {
        return true;
    }
    for (const auto& database : annotation->database_ids) {
        if (contains(database)) return true;
    }
    return false;
}

[[nodiscard]] std::optional<double> sort_value(
    const CohortAssociationVariantResult& row,
    const CohortResultSortKey key
) {
    switch (key) {
        case CohortResultSortKey::coordinate: return std::nullopt;
        case CohortResultSortKey::allele_p_value: return row.allele.fisher_two_sided_p;
        case CohortResultSortKey::carrier_p_value: return row.carrier.fisher_two_sided_p;
        case CohortResultSortKey::allele_fdr_q: return row.allele.bh_adjusted_q;
        case CohortResultSortKey::carrier_fdr_q: return row.carrier.bh_adjusted_q;
    }
    return std::nullopt;
}

[[nodiscard]] bool coordinate_less(
    const CohortResultRow& left,
    const CohortResultRow& right
) {
    return std::tie(
        left.association.contig,
        left.association.start,
        left.association.end,
        left.association.reference,
        left.association.alternate,
        left.association.ordinal
    ) < std::tie(
        right.association.contig,
        right.association.start,
        right.association.end,
        right.association.reference,
        right.association.alternate,
        right.association.ordinal
    );
}

void sort_rows(std::vector<CohortResultRow>& rows, const CohortResultQuery& query) {
    std::stable_sort(rows.begin(), rows.end(), [&](const auto& left, const auto& right) {
        if (query.sort_key == CohortResultSortKey::coordinate) {
            const bool less = coordinate_less(left, right);
            if (query.sort_direction == CohortResultSortDirection::ascending) return less;
            return !less && !coordinate_less(right, left) ? false : coordinate_less(right, left);
        }

        const auto l = sort_value(left.association, query.sort_key);
        const auto r = sort_value(right.association, query.sort_key);
        if (l.has_value() != r.has_value()) return l.has_value();
        if (!l.has_value()) return coordinate_less(left, right);
        if (*l == *r) return coordinate_less(left, right);
        return query.sort_direction == CohortResultSortDirection::ascending
            ? *l < *r
            : *l > *r;
    });
}

[[nodiscard]] std::vector<CohortResultRow> filtered_rows(
    const CohortCaseControlAnalysisResult& analysis,
    const CohortResultQuery& query,
    const std::map<std::size_t, CohortResultAnnotationLink>& annotations
) {
    const auto needle = lower_copy(query.search_text);
    std::vector<CohortResultRow> rows;
    rows.reserve(analysis.variants.size());
    for (const auto& value : analysis.variants) {
        const auto found = annotations.find(value.ordinal);
        std::optional<CohortResultAnnotationLink> annotation;
        if (found != annotations.end()) annotation = found->second;

        if (query.contig.has_value() && value.contig != *query.contig) continue;
        if (!view_matches(value, query.variant_view)) continue;
        if (query.annotated_only && !annotation.has_value()) continue;
        if (!at_most(value.allele.fisher_two_sided_p, query.maximum_allele_p_value)) continue;
        if (!at_most(value.carrier.fisher_two_sided_p, query.maximum_carrier_p_value)) continue;
        if (!at_most(value.allele.bh_adjusted_q, query.maximum_allele_fdr_q)) continue;
        if (!at_most(value.carrier.bh_adjusted_q, query.maximum_carrier_fdr_q)) continue;
        if (!text_matches(value, annotation, needle)) continue;
        rows.push_back({value, std::move(annotation)});
    }
    sort_rows(rows, query);
    return rows;
}

[[nodiscard]] std::size_t excluded_count(const CohortAnalysisSnapshot& snapshot) {
    return static_cast<std::size_t>(std::count_if(
        snapshot.samples.begin(), snapshot.samples.end(), [](const auto& sample) {
            return sample.analysis_disposition == CohortAnalysisDisposition::excluded;
        }
    ));
}

void require_package_text(const std::string_view value, const char* name) {
    if (value.empty() || value.size() > 256U || value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(std::string{name} + " is invalid");
    }
}

}  // namespace

CohortResultsService::CohortResultsService(
    ICohortAnalysisSnapshotStore& snapshots
) noexcept : snapshots_{snapshots} {}

CohortResultPage CohortResultsService::query(
    const std::string_view project_id,
    const std::string_view analysis_id,
    const CohortCaseControlAnalysisResult& analysis,
    const CohortResultQuery& request,
    const std::vector<CohortResultAnnotationLink>& annotations
) {
    validate_cohort_result_query(request);
    const auto snapshot = snapshots_.find(project_id, analysis_id);
    if (!snapshot.has_value()) {
        throw std::invalid_argument("approved cohort analysis snapshot was not found");
    }
    validate_analysis_against_snapshot(*snapshot, analysis);
    const auto mapped = annotation_map(annotations, analysis.variants.size());
    auto matches = filtered_rows(analysis, request, mapped);

    CohortResultPage page{
        .project_id = std::string{project_id},
        .analysis_id = std::string{analysis_id},
        .snapshot_digest = snapshot->snapshot_digest,
        .filter_definition = canonical_cohort_result_filter(request),
        .total_unfiltered_variants = analysis.variants.size(),
        .total_matched_variants = matches.size(),
        .offset = request.offset,
        .limit = request.limit,
        .rows = {},
    };
    if (request.offset >= matches.size()) return page;

    const auto count = std::min(request.limit, matches.size() - request.offset);
    page.rows.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        page.rows.push_back(std::move(matches[request.offset + index]));
    }
    return page;
}

CohortResultPackage CohortResultsService::build_package(
    const std::string_view project_id,
    const std::string_view analysis_id,
    const CohortCaseControlAnalysisResult& analysis,
    const CohortResultQuery& filter,
    std::string producer_version,
    std::string generated_at_utc,
    const std::vector<CohortResultAnnotationLink>& annotations,
    const std::size_t maximum_rows
) {
    validate_cohort_result_query(filter);
    require_package_text(producer_version, "Producer version");
    require_package_text(generated_at_utc, "Generated timestamp");
    if (maximum_rows == 0U || maximum_rows > maximum_export_rows) {
        throw std::invalid_argument("cohort result export row bound is invalid");
    }

    const auto snapshot = snapshots_.find(project_id, analysis_id);
    if (!snapshot.has_value()) {
        throw std::invalid_argument("approved cohort analysis snapshot was not found");
    }
    validate_analysis_against_snapshot(*snapshot, analysis);
    const auto mapped = annotation_map(annotations, analysis.variants.size());
    auto matches = filtered_rows(analysis, filter, mapped);
    if (matches.size() > maximum_rows) {
        throw std::length_error(
            "cohort result export exceeds the bounded server-side export row limit"
        );
    }

    return {
        .schema_version = 1U,
        .producer_name = "OpenGenesis-BioCore",
        .producer_version = std::move(producer_version),
        .generated_at_utc = std::move(generated_at_utc),
        .project_id = snapshot->project_id,
        .cohort_id = snapshot->cohort_id,
        .cohort_revision = snapshot->cohort_revision,
        .analysis_id = snapshot->analysis_id,
        .snapshot_digest = snapshot->snapshot_digest,
        .filter_definition = canonical_cohort_result_filter(filter),
        .reference_file_id = snapshot->reference.managed_file_id,
        .reference_sha256 = snapshot->reference.sha256,
        .reference_assembly = assembly_name(snapshot->reference.assembly),
        .approved_case_samples = snapshot->approved_case_samples,
        .approved_control_samples = snapshot->approved_control_samples,
        .excluded_samples = excluded_count(*snapshot),
        .statistical_test = analysis.statistical_test,
        .effect_measure = analysis.effect_measure,
        .confidence_interval_method = analysis.confidence_interval_method,
        .multiple_testing_method = analysis.multiple_testing_method,
        .total_unfiltered_variants = analysis.variants.size(),
        .total_matched_variants = matches.size(),
        .rows = std::move(matches),
    };
}

}  // namespace biocore::application
