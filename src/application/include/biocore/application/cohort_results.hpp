#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/cohort_analysis_snapshot.hpp"
#include "biocore/application/cohort_case_control_analysis_service.hpp"
#include "biocore/domain/variant_annotation.hpp"

namespace biocore::application {

enum class CohortResultVariantView : std::uint8_t {
    all,
    shared,
    case_specific,
    control_specific
};

enum class CohortResultSortKey : std::uint8_t {
    coordinate,
    allele_p_value,
    carrier_p_value,
    allele_fdr_q,
    carrier_fdr_q
};

enum class CohortResultSortDirection : std::uint8_t {
    ascending,
    descending
};

struct CohortResultQuery final {
    std::optional<std::string> contig;
    CohortResultVariantView variant_view{CohortResultVariantView::all};
    bool annotated_only{false};
    std::optional<double> maximum_allele_p_value;
    std::optional<double> maximum_carrier_p_value;
    std::optional<double> maximum_allele_fdr_q;
    std::optional<double> maximum_carrier_fdr_q;
    std::string search_text;
    CohortResultSortKey sort_key{CohortResultSortKey::coordinate};
    CohortResultSortDirection sort_direction{CohortResultSortDirection::ascending};
    std::size_t offset{0U};
    std::size_t limit{50U};
};

struct CohortResultAnnotationLink final {
    std::size_t ordinal{0U};
    std::vector<std::string> database_ids;
    std::optional<std::string> primary_record_id;
    std::optional<std::string> primary_feature_type;

    friend bool operator==(const CohortResultAnnotationLink&, const CohortResultAnnotationLink&) = default;
};

struct CohortResultRow final {
    CohortAssociationVariantResult association;
    std::optional<CohortResultAnnotationLink> annotation;
};

struct CohortResultPage final {
    std::string project_id;
    std::string analysis_id;
    std::string snapshot_digest;
    std::string filter_definition;
    std::size_t total_unfiltered_variants{0U};
    std::size_t total_matched_variants{0U};
    std::size_t offset{0U};
    std::size_t limit{0U};
    std::vector<CohortResultRow> rows;
};

struct CohortResultExclusion final {
    std::string sample_id;
    std::optional<std::string> reason;

    friend bool operator==(const CohortResultExclusion&, const CohortResultExclusion&) = default;
};

struct CohortResultPackage final {
    std::uint32_t schema_version{1U};
    std::string producer_name{"OpenGenesis-BioCore"};
    std::string producer_version;
    std::string generated_at_utc;
    std::string project_id;
    std::string cohort_id;
    std::uint32_t cohort_revision{0U};
    std::string analysis_id;
    std::string snapshot_digest;
    std::string filter_definition;
    std::string reference_file_id;
    std::string reference_sha256;
    std::string reference_assembly;
    std::size_t approved_case_samples{0U};
    std::size_t approved_control_samples{0U};
    std::size_t excluded_samples{0U};
    std::vector<CohortResultExclusion> exclusions;
    std::string association_contract_version;
    std::string test_filter_version;
    std::string no_call_policy;
    std::size_t allele_family_size{0U};
    std::size_t carrier_family_size{0U};
    std::string statistical_test;
    std::string effect_measure;
    std::string confidence_interval_method;
    std::string multiple_testing_method;
    std::size_t total_unfiltered_variants{0U};
    std::size_t total_matched_variants{0U};
    std::vector<CohortResultRow> rows;
};

[[nodiscard]] std::string_view to_string(CohortResultVariantView value) noexcept;
[[nodiscard]] std::string_view to_string(CohortResultSortKey value) noexcept;
[[nodiscard]] std::string_view to_string(CohortResultSortDirection value) noexcept;

void validate_cohort_result_query(const CohortResultQuery& query);

[[nodiscard]] std::string canonical_cohort_result_filter(const CohortResultQuery& query);

[[nodiscard]] CohortResultAnnotationLink make_cohort_result_annotation_link(
    std::size_t ordinal,
    const domain::VariantAnnotationResult& annotation
);

}  // namespace biocore::application
