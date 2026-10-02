#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/cohort_analysis_selection.hpp"
#include "biocore/application/cohort_registry.hpp"

namespace biocore::application {

enum class CohortAnalysisDisposition {
    included,
    excluded
};

enum class CohortQcEvidenceState {
    verified,
    unavailable,
    not_applicable
};

[[nodiscard]] std::string_view to_string(CohortAnalysisDisposition value) noexcept;
[[nodiscard]] std::optional<CohortAnalysisDisposition>
cohort_analysis_disposition_from_string(std::string_view value) noexcept;

[[nodiscard]] std::string_view to_string(CohortQcEvidenceState value) noexcept;
[[nodiscard]] std::optional<CohortQcEvidenceState>
cohort_qc_evidence_state_from_string(std::string_view value) noexcept;

struct CohortQcDecisionDraft final {
    std::string sample_id;
    CohortAnalysisDisposition disposition{CohortAnalysisDisposition::included};
    std::optional<std::string> reason;

    friend bool operator==(const CohortQcDecisionDraft&, const CohortQcDecisionDraft&) = default;
};

struct CohortQcArtifactEvidence final {
    CohortQcEvidenceState state{CohortQcEvidenceState::unavailable};
    std::string reason;
    std::optional<std::string> managed_file_id;
    std::optional<std::string> job_id;
    std::optional<std::string> step_id;
    std::optional<std::string> output_port;
    std::optional<std::string> module_id;
    std::optional<std::string> plugin_version;
    std::optional<std::int64_t> size_bytes;
    std::optional<std::string> sha256;

    friend bool operator==(const CohortQcArtifactEvidence&, const CohortQcArtifactEvidence&) = default;
};

struct CohortAnalysisSampleSnapshot final {
    std::size_t ordinal{0U};
    std::string sample_id;
    std::string sample_display_name;
    std::string sample_group_metadata;
    std::string biological_unit_id;
    CohortGroup group{CohortGroup::unassigned};
    CohortMemberDisposition cohort_disposition{CohortMemberDisposition::included};
    std::optional<std::string> cohort_exclusion_reason;
    CohortAnalysisDisposition analysis_disposition{CohortAnalysisDisposition::excluded};
    std::optional<std::string> analysis_exclusion_reason;
    CohortQcArtifactEvidence qc;

    friend bool operator==(const CohortAnalysisSampleSnapshot&, const CohortAnalysisSampleSnapshot&) = default;
};

struct CohortAssociationApprovalOptions final {
    std::size_t minimum_complete_case_calls{1U};
    std::size_t minimum_complete_control_calls{1U};
    std::size_t maximum_fisher_table_states{100000U};

    friend bool operator==(const CohortAssociationApprovalOptions&, const CohortAssociationApprovalOptions&) = default;
};

struct CohortAnalysisVariantUniverse final {
    std::size_t ordinal{0U};
    std::string contig;
    std::uint64_t start{0U};
    std::uint64_t end{0U};
    std::string reference;
    std::string alternate;
    std::size_t case_unobserved{0U};
    std::size_t control_unobserved{0U};
    std::size_t case_no_calls{0U};
    std::size_t control_no_calls{0U};
    std::size_t case_partial_calls{0U};
    std::size_t control_partial_calls{0U};
    std::size_t case_complete_calls{0U};
    std::size_t control_complete_calls{0U};
    bool allele_family_member{false};
    bool carrier_family_member{false};

    friend bool operator==(const CohortAnalysisVariantUniverse&, const CohortAnalysisVariantUniverse&) = default;
};

struct CohortAnalysisSnapshot final {
    std::string analysis_id;
    std::string project_id;
    std::string cohort_id;
    std::uint32_t cohort_revision{0U};
    std::string contract_version{"biocore.cohort-analysis.v1"};
    std::string preview_digest;
    std::string snapshot_digest;
    std::string approved_at_utc;
    CohortPinnedReference reference;
    std::vector<CohortPinnedVcfSource> sources;
    std::vector<CohortAnalysisSampleSnapshot> samples;
    CohortAssociationApprovalOptions association_options;
    std::string association_contract_version{"biocore.case-control.v1"};
    std::string test_filter_version{"biocore.complete-call-gate.v1"};
    std::size_t approved_case_samples{0U};
    std::size_t approved_control_samples{0U};
    std::size_t allele_family_size{0U};
    std::size_t carrier_family_size{0U};
    std::vector<CohortAnalysisVariantUniverse> test_universe;

    friend bool operator==(const CohortAnalysisSnapshot&, const CohortAnalysisSnapshot&) = default;
};

struct CohortAnalysisApprovalIssue final {
    std::string code;
    std::string sample_id;
    bool blocking{true};
    std::string message;

    friend bool operator==(const CohortAnalysisApprovalIssue&, const CohortAnalysisApprovalIssue&) = default;
};

struct CohortAnalysisApprovalPreview final {
    bool ready{false};
    std::string preview_digest;
    CohortAnalysisSnapshot draft;
    std::vector<CohortAnalysisApprovalIssue> issues;
};

struct CohortAnalysisApprovalRequest final {
    CohortAnalysisSelectionRequest selection;
    std::vector<CohortQcDecisionDraft> qc_decisions;
    CohortAssociationApprovalOptions association_options;
};

struct ApproveCohortAnalysisRequest final {
    CohortAnalysisApprovalRequest preview_request;
    std::string expected_preview_digest;
};

[[nodiscard]] std::string canonical_cohort_analysis_snapshot(
    const CohortAnalysisSnapshot& snapshot,
    bool include_final_identity
);

}  // namespace biocore::application
