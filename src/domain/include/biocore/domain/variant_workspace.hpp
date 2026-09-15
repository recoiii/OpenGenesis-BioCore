#pragma once

#include "biocore/domain/case_control_association.hpp"
#include "biocore/domain/variant_annotation.hpp"
#include "biocore/domain/variant_index.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace biocore::domain {

enum class VariantWorkspaceSortKey : std::uint8_t {
    coordinate,
    quality,
    carrier_count,
    allele_p_value,
    carrier_p_value,
    allele_fdr_q,
    carrier_fdr_q
};

enum class VariantWorkspaceSortDirection : std::uint8_t {
    ascending,
    descending
};

struct VariantWorkspaceRecordMetadata final {
    std::optional<std::string> record_id;
    std::optional<double> quality;
    std::vector<std::string> filters;
    bool filters_applied{false};
};

struct VariantWorkspaceQuery final {
    std::optional<ContigId> contig_id;
    std::optional<std::uint64_t> start;
    std::optional<std::uint64_t> end;
    std::vector<VariantType> variant_types;
    std::optional<double> minimum_quality;
    std::optional<double> maximum_quality;
    bool pass_only{false};
    std::optional<std::string> filter_tag;
    std::optional<std::size_t> minimum_carrier_count;
    std::optional<std::size_t> maximum_carrier_count;
    std::optional<double> maximum_allele_p_value;
    std::optional<double> maximum_carrier_p_value;
    std::optional<double> maximum_allele_fdr_q;
    std::optional<double> maximum_carrier_fdr_q;
    std::string search_text;
    VariantWorkspaceSortKey sort_key{VariantWorkspaceSortKey::coordinate};
    VariantWorkspaceSortDirection sort_direction{VariantWorkspaceSortDirection::ascending};
    std::size_t offset{0U};
    std::size_t limit{50U};
    std::uint64_t query_generation_id{0U};
};

struct VariantWorkspaceRowDto final {
    std::size_t variant_index{0U};
    ContigId contig_id{0U};
    std::string contig_name;
    std::uint64_t start{0U};
    std::uint64_t end{0U};
    std::string reference_allele;
    std::string alternate_allele;
    VariantType variant_type{VariantType::unknown};
    bool symbolic{false};

    std::optional<std::string> record_id;
    std::optional<double> quality;
    std::string filter_status{"."};

    std::size_t carrier_sample_count{0U};
    std::size_t total_complete_calls{0U};

    bool annotation_available{false};
    std::size_t allele_annotation_count{0U};
    std::size_t feature_annotation_count{0U};
    std::optional<std::string> primary_annotation_id;
    std::optional<std::string> primary_feature_type;

    bool association_available{false};
    AssociationOddsRatio allele_odds_ratio;
    AssociationOddsRatio carrier_odds_ratio;
    std::optional<AssociationConfidenceInterval95> allele_odds_ratio_ci95;
    std::optional<AssociationConfidenceInterval95> carrier_odds_ratio_ci95;
    std::optional<double> allele_fisher_two_sided_p;
    std::optional<double> carrier_fisher_two_sided_p;
    std::optional<double> allele_bh_adjusted_q;
    std::optional<double> carrier_bh_adjusted_q;
};

struct VariantWorkspaceSampleCallDto final {
    std::string sample_id;
    MultiSampleCallState state{MultiSampleCallState::no_call};
    bool genotype_present{false};
    GenotypeCall genotype;
    std::uint32_t reference_dosage{0U};
    std::uint32_t alternate_dosage{0U};
    std::uint32_t other_alternate_dosage{0U};
    std::uint32_t missing_allele_count{0U};
    std::size_t source_alternate_index{0U};
    std::vector<DynamicVariantField> extra_format_fields;
    MultiSampleMatrixRecordProvenance provenance;
};

struct VariantWorkspaceDetailDto final {
    VariantWorkspaceRowDto row;
    std::vector<VariantWorkspaceSampleCallDto> sample_calls;
    std::optional<VariantAnnotationResult> annotation;
    std::optional<CaseControlVariantAssociation> association;
};

struct VariantWorkspaceBoundedQueryResult final {
    std::uint64_t total_matched_variants{0U};
    std::uint64_t total_unfiltered_variants{0U};
    std::size_t window_offset{0U};
    std::vector<VariantWorkspaceRowDto> rows;
    std::uint64_t query_generation_id{0U};
};

void validate_variant_workspace_query(const VariantWorkspaceQuery& query);

class VariantAnalysisWorkspace final {
public:
    VariantAnalysisWorkspace() = default;

    [[nodiscard]] static VariantAnalysisWorkspace build(
        const MultiSampleMatrix& matrix,
        const CaseControlAssociationResult* associations = nullptr,
        const std::vector<VariantAnnotationResult>* annotations = nullptr,
        const std::vector<VariantWorkspaceRecordMetadata>* metadata = nullptr
    );

    [[nodiscard]] std::size_t size() const noexcept { return rows_.size(); }
    [[nodiscard]] const ReferenceAssemblyIdentity& assembly() const {
        if (matrix_ == nullptr) {
            throw std::logic_error("variant workspace is not initialized");
        }
        return matrix_->assembly();
    }
    [[nodiscard]] const VariantWorkspaceRowDto& export_row(const std::size_t variant_index) const {
        if (variant_index >= rows_.size()) {
            throw std::out_of_range("variant workspace export index is out of range");
        }
        return rows_[variant_index];
    }
    [[nodiscard]] const VariantAnnotationResult* annotation(const std::size_t variant_index) const {
        if (variant_index >= rows_.size()) {
            throw std::out_of_range("variant workspace annotation index is out of range");
        }
        return annotations_ == nullptr ? nullptr : &annotations_->at(variant_index);
    }
    [[nodiscard]] VariantWorkspaceBoundedQueryResult query(const VariantWorkspaceQuery& request) const;
    [[nodiscard]] VariantWorkspaceDetailDto detail(std::size_t variant_index) const;

private:
    const MultiSampleMatrix* matrix_{nullptr};
    const CaseControlAssociationResult* associations_{nullptr};
    const std::vector<VariantAnnotationResult>* annotations_{nullptr};
    VariantCoordinateIndex coordinate_index_;
    std::vector<VariantWorkspaceRowDto> rows_;
};

}  // namespace biocore::domain
