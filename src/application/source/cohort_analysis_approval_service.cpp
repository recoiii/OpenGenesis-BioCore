#include "biocore/application/cohort_analysis_approval_service.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "biocore/application/generated_output_artifact.hpp"
#include "biocore/application/i_cohort_analysis_digester.hpp"
#include "biocore/application/i_cohort_analysis_snapshot_store.hpp"
#include "biocore/application/i_cohort_matrix_builder.hpp"
#include "biocore/application/i_cohort_registry_store.hpp"
#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_result_artifact_reader.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/domain/multi_sample_matrix.hpp"

namespace biocore::application {
namespace {

[[nodiscard]] bool blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
        return std::isspace(static_cast<unsigned char>(character)) != 0;
    });
}

[[nodiscard]] bool valid_sha256(const std::string_view value) noexcept {
    return value.size() == 64U && std::ranges::all_of(value, [](const char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
    });
}

void add_issue(
    CohortAnalysisApprovalPreview& preview,
    std::string code,
    std::string sample_id,
    const bool blocking,
    std::string message
) {
    preview.issues.push_back({
        .code = std::move(code),
        .sample_id = std::move(sample_id),
        .blocking = blocking,
        .message = std::move(message),
    });
}

[[nodiscard]] const CohortPinnedVcfSource* source_for_sample(
    const std::vector<CohortPinnedVcfSource>& sources,
    const std::string_view sample_id
) {
    const auto found = std::ranges::find_if(sources, [sample_id](const auto& source) {
        return source.project_sample_id == sample_id;
    });
    return found == sources.end() ? nullptr : &*found;
}

[[nodiscard]] CohortQcArtifactEvidence qc_evidence(
    IManagedFileRepository& files,
    IResultArtifactReader& reader,
    const CohortPinnedVcfSource& source,
    const std::size_t maximum_bytes
) {
    CohortQcArtifactEvidence evidence{
        .state = CohortQcEvidenceState::unavailable,
        .reason = {},
        .managed_file_id = std::nullopt,
        .job_id = std::nullopt,
        .step_id = std::nullopt,
        .output_port = std::nullopt,
        .module_id = std::nullopt,
        .plugin_version = std::nullopt,
        .size_bytes = std::nullopt,
        .sha256 = std::nullopt,
    };

    if (source.module_id != "org.biocore.vcfqc.filter") {
        evidence.reason = "Selected VCF producer does not expose the cohort-v1 VCF QC summary contract";
        return evidence;
    }

    const auto artifact = files.find_generated_output(
        source.job_id, source.step_id, "summary"
    );
    if (!artifact.has_value()) {
        evidence.reason = "Selected VCF attempt has no registered VCF QC summary";
        return evidence;
    }
    if (artifact->provenance.module_id != source.module_id ||
        artifact->provenance.plugin_version != source.plugin_version ||
        artifact->provenance.file_type != "json" ||
        artifact->file.checksum_algorithm() != std::optional<std::string>{"sha256"} ||
        !artifact->file.checksum_value().has_value() ||
        !valid_sha256(*artifact->file.checksum_value()) ||
        artifact->file.size_bytes() < 0) {
        evidence.reason = "VCF QC summary provenance or checksum contract is invalid";
        return evidence;
    }

    const auto read = reader.read_verified_text(*artifact, maximum_bytes);
    if (read.status != ResultArtifactReadStatus::verified ||
        !read.text.has_value() ||
        !read.verified_sha256.has_value() ||
        *read.verified_sha256 != *artifact->file.checksum_value()) {
        evidence.reason = "VCF QC summary bytes could not be verified";
        return evidence;
    }
    if (read.text->empty() ||
        read.text->find('\0') != std::string::npos ||
        read.text->find("\"module\":\"org.biocore.vcfqc.filter\"") == std::string::npos ||
        read.text->find("\"schemaVersion\":1") == std::string::npos ||
        read.text->find("\"metrics\"") == std::string::npos) {
        evidence.reason = "VCF QC summary content does not match schema v1 identity";
        return evidence;
    }

    evidence.state = CohortQcEvidenceState::verified;
    evidence.reason = "Verified VCF QC summary";
    evidence.managed_file_id = std::string{artifact->file.id()};
    evidence.job_id = source.job_id;
    evidence.step_id = source.step_id;
    evidence.output_port = artifact->provenance.output_port;
    evidence.module_id = artifact->provenance.module_id;
    evidence.plugin_version = artifact->provenance.plugin_version;
    evidence.size_bytes = artifact->file.size_bytes();
    evidence.sha256 = *read.verified_sha256;
    return evidence;
}

struct GroupTally final {
    std::size_t no_call{0U};
    std::size_t partial_call{0U};
    std::size_t complete_call{0U};
};

[[nodiscard]] CohortAnalysisVariantUniverse variant_universe(
    const domain::MultiSampleMatrix& matrix,
    const std::size_t variant_index,
    const std::map<std::string, CohortGroup, std::less<>>& approved_groups,
    const std::size_t case_total,
    const std::size_t control_total,
    const CohortAssociationApprovalOptions& options
) {
    GroupTally cases;
    GroupTally controls;

    for (const auto ordinal : matrix.variant_observation_ordinals(variant_index)) {
        const auto& observation = matrix.observation(ordinal);
        if (observation.sample_index >= matrix.sample_count()) {
            throw std::logic_error{"Cohort matrix observation sample index is invalid"};
        }
        const auto& sample = matrix.sample(observation.sample_index);
        const auto group = approved_groups.find(sample.sample_id);
        if (group == approved_groups.end()) continue;

        const auto& call = matrix.call(observation.call_index);
        if (call.sample_index != observation.sample_index) {
            throw std::logic_error{"Cohort matrix call/sample association is inconsistent"};
        }
        auto& tally = group->second == CohortGroup::case_group ? cases : controls;
        switch (call.state) {
            case domain::MultiSampleCallState::no_call:
                ++tally.no_call;
                break;
            case domain::MultiSampleCallState::partial_call:
                ++tally.partial_call;
                break;
            case domain::MultiSampleCallState::complete_call:
                ++tally.complete_call;
                break;
        }
    }

    const auto case_observed = cases.no_call + cases.partial_call + cases.complete_call;
    const auto control_observed =
        controls.no_call + controls.partial_call + controls.complete_call;
    if (case_observed > case_total || control_observed > control_total) {
        throw std::logic_error{"Cohort matrix has more group observations than approved samples"};
    }

    const auto& variant = matrix.variant(variant_index);
    const auto contig = matrix.contigs().canonical_name(variant.locus.contig_id);
    if (!contig.has_value()) {
        throw std::logic_error{"Cohort matrix variant contig cannot be resolved"};
    }
    const bool testable =
        cases.complete_call >= options.minimum_complete_case_calls &&
        controls.complete_call >= options.minimum_complete_control_calls;

    return {
        .ordinal = variant_index,
        .contig = std::string{*contig},
        .start = variant.locus.start,
        .end = variant.locus.end,
        .reference = variant.reference.sequence,
        .alternate = variant.alternate.sequence,
        .case_unobserved = case_total - case_observed,
        .control_unobserved = control_total - control_observed,
        .case_no_calls = cases.no_call,
        .control_no_calls = controls.no_call,
        .case_partial_calls = cases.partial_call,
        .control_partial_calls = controls.partial_call,
        .case_complete_calls = cases.complete_call,
        .control_complete_calls = controls.complete_call,
        .allele_family_member = testable,
        .carrier_family_member = testable,
    };
}

[[nodiscard]] bool blocking_issues(
    const std::vector<CohortAnalysisApprovalIssue>& issues
) {
    return std::ranges::any_of(issues, [](const auto& issue) { return issue.blocking; });
}

void require_options(const CohortAssociationApprovalOptions& options) {
    if (options.minimum_complete_case_calls == 0U ||
        options.minimum_complete_control_calls == 0U ||
        options.minimum_complete_case_calls > 100U ||
        options.minimum_complete_control_calls > 100U ||
        options.maximum_fisher_table_states == 0U ||
        options.maximum_fisher_table_states >
            CohortAnalysisApprovalService::maximum_fisher_table_states) {
        throw CohortAnalysisApprovalError{
            CohortAnalysisApprovalErrorCode::invalid_request,
            "Cohort association approval options exceed the v0.6 contract"
        };
    }
}

}  // namespace

CohortAnalysisApprovalError::CohortAnalysisApprovalError(
    const CohortAnalysisApprovalErrorCode code,
    std::string message
) : std::runtime_error{std::move(message)}, code_{code} {}

CohortAnalysisApprovalErrorCode CohortAnalysisApprovalError::code() const noexcept {
    return code_;
}

CohortAnalysisApprovalService::CohortAnalysisApprovalService(
    ICohortMatrixBuilder& matrix_builder,
    ICohortRegistryStore& cohorts,
    IManagedFileRepository& files,
    IResultArtifactReader& artifact_reader,
    ICohortAnalysisSnapshotStore& snapshots,
    ICohortAnalysisDigester& digester,
    IIdGenerator& ids,
    IUtcClock& clock
) noexcept
    : matrix_builder_{matrix_builder},
      cohorts_{cohorts},
      files_{files},
      artifact_reader_{artifact_reader},
      snapshots_{snapshots},
      digester_{digester},
      ids_{ids},
      clock_{clock} {}

CohortAnalysisApprovalPreview CohortAnalysisApprovalService::preview(
    const CohortAnalysisApprovalRequest& request
) {
    require_options(request.association_options);

    CohortAnalysisApprovalPreview preview{
        .ready = false,
        .preview_digest = {},
        .draft = CohortAnalysisSnapshot{},
        .issues = {},
    };
    preview.draft.project_id = request.selection.project_id;
    preview.draft.cohort_id = request.selection.cohort_id;
    preview.draft.cohort_revision = request.selection.cohort_revision;
    preview.draft.association_options = request.association_options;

    const auto current = cohorts_.find(
        request.selection.project_id, request.selection.cohort_id
    );
    if (!current.has_value()) {
        add_issue(preview, "cohort_not_found", {}, true,
                  "Cohort does not exist in the selected project");
        return preview;
    }
    if (current->current_revision != request.selection.cohort_revision) {
        add_issue(preview, "stale_revision", {}, true,
                  "Analysis preview does not target the current cohort revision");
        return preview;
    }
    const auto cohort = cohorts_.find(
        request.selection.project_id,
        request.selection.cohort_id,
        request.selection.cohort_revision
    );
    if (!cohort.has_value()) {
        add_issue(preview, "cohort_not_found", {}, true,
                  "Requested cohort revision does not exist");
        return preview;
    }
    if (cohort->revision.members.empty()) {
        add_issue(preview, "empty_cohort", {}, true,
                  "Empty cohort cannot be approved for association");
        return preview;
    }

    std::set<std::string, std::less<>> cohort_included;
    for (const auto& member : cohort->revision.members) {
        if (member.disposition == CohortMemberDisposition::included) {
            cohort_included.emplace(member.sample_id);
        }
    }
    if (cohort_included.empty()) {
        add_issue(preview, "all_excluded", {}, true,
                  "Cohort revision has no included samples");
        return preview;
    }

    std::map<std::string, CohortQcDecisionDraft, std::less<>> decisions;
    for (const auto& decision : request.qc_decisions) {
        if (blank(decision.sample_id) ||
            !cohort_included.contains(decision.sample_id) ||
            !decisions.emplace(decision.sample_id, decision).second) {
            add_issue(preview, "invalid_qc_decision", decision.sample_id, true,
                      "QC decisions must map exactly once to cohort-included samples");
        }
    }
    for (const auto& sample_id : cohort_included) {
        if (!decisions.contains(sample_id)) {
            add_issue(preview, "missing_qc_decision", sample_id, true,
                      "Every cohort-included sample requires an explicit QC inclusion decision");
        }
    }
    if (blocking_issues(preview.issues)) return preview;

    CohortMatrixBuildResult matrix_build;
    try {
        matrix_build = matrix_builder_.build(request.selection);
    } catch (const std::exception& error) {
        add_issue(preview, "matrix_not_ready", {}, true,
                  std::string{"Cohort matrix could not be revalidated: "} + error.what());
        return preview;
    }
    if (!matrix_build.selection.ready ||
        !matrix_build.selection.reference.has_value()) {
        add_issue(preview, "matrix_not_ready", {}, true,
                  "Cohort matrix selection is not ready");
        return preview;
    }

    preview.draft.reference = *matrix_build.selection.reference;
    preview.draft.sources = matrix_build.selection.sources;
    preview.draft.samples.reserve(cohort->revision.members.size());

    std::map<std::string, CohortGroup, std::less<>> approved_groups;
    std::size_t case_count = 0U;
    std::size_t control_count = 0U;

    for (std::size_t ordinal = 0U; ordinal < cohort->revision.members.size(); ++ordinal) {
        const auto& member = cohort->revision.members[ordinal];
        CohortAnalysisSampleSnapshot sample{
            .ordinal = ordinal,
            .sample_id = member.sample_id,
            .sample_display_name = member.sample_display_name,
            .sample_group_metadata = member.sample_group_metadata,
            .biological_unit_id = member.biological_unit_id,
            .group = member.group,
            .cohort_disposition = member.disposition,
            .cohort_exclusion_reason = member.exclusion_reason,
            .analysis_disposition = CohortAnalysisDisposition::excluded,
            .analysis_reason = member.exclusion_reason,
            .qc = CohortQcArtifactEvidence{
                .state = CohortQcEvidenceState::not_applicable,
                .reason = "Cohort member was excluded before analysis selection",
                .managed_file_id = std::nullopt,
                .job_id = std::nullopt,
                .step_id = std::nullopt,
                .output_port = std::nullopt,
                .module_id = std::nullopt,
                .plugin_version = std::nullopt,
                .size_bytes = std::nullopt,
                .sha256 = std::nullopt,
            },
        };

        if (member.disposition == CohortMemberDisposition::included) {
            const auto decision_it = decisions.find(member.sample_id);
            if (decision_it == decisions.end()) {
                throw std::logic_error{"Validated QC decision disappeared"};
            }
            const auto* source = source_for_sample(
                matrix_build.selection.sources, member.sample_id
            );
            if (source == nullptr) {
                add_issue(preview, "mapping_conflict", member.sample_id, true,
                          "Included sample has no revalidated source mapping");
                preview.draft.samples.push_back(std::move(sample));
                continue;
            }
            sample.qc = qc_evidence(
                files_, artifact_reader_, *source, maximum_qc_summary_bytes
            );
            sample.analysis_disposition = decision_it->second.disposition;
            sample.analysis_reason = decision_it->second.reason;

            if (decision_it->second.disposition == CohortAnalysisDisposition::excluded) {
                if (!decision_it->second.reason.has_value() ||
                    blank(*decision_it->second.reason) ||
                    decision_it->second.reason->size() > 512U) {
                    add_issue(preview, "qc_exclusion_reason_required", member.sample_id, true,
                              "QC-excluded sample requires an explicit reason");
                }
            } else {
                if (member.group == CohortGroup::unassigned) {
                    add_issue(preview, "unassigned_group", member.sample_id, true,
                              "Unassigned cohort member cannot enter an association snapshot");
                }
                if (sample.qc.state != CohortQcEvidenceState::verified) {
                    add_issue(preview, "qc_unavailable", member.sample_id, false,
                              sample.qc.reason);
                    if (!decision_it->second.reason.has_value() ||
                        blank(*decision_it->second.reason) ||
                        decision_it->second.reason->size() > 512U) {
                        add_issue(preview, "qc_acknowledgement_required", member.sample_id, true,
                                  "Including a sample without verified QC requires an explicit rationale");
                    }
                } else if (decision_it->second.reason.has_value() &&
                           (blank(*decision_it->second.reason) ||
                            decision_it->second.reason->size() > 512U)) {
                    add_issue(preview, "invalid_qc_reason", member.sample_id, true,
                              "QC approval rationale is invalid");
                }

                if (member.group == CohortGroup::case_group) {
                    ++case_count;
                    approved_groups.emplace(member.sample_id, member.group);
                } else if (member.group == CohortGroup::control) {
                    ++control_count;
                    approved_groups.emplace(member.sample_id, member.group);
                }
            }
        }
        preview.draft.samples.push_back(std::move(sample));
    }

    if (case_count + control_count == 0U) {
        add_issue(preview, "all_excluded", {}, true,
                  "QC decisions exclude every sample from analysis");
    }
    if (case_count == 0U) {
        add_issue(preview, "missing_case_group", {}, true,
                  "Approved analysis requires at least one case sample");
    }
    if (control_count == 0U) {
        add_issue(preview, "missing_control_group", {}, true,
                  "Approved analysis requires at least one control sample");
    }

    preview.draft.approved_case_samples = case_count;
    preview.draft.approved_control_samples = control_count;

    if (!blocking_issues(preview.issues)) {
        preview.draft.test_universe.reserve(matrix_build.matrix.variant_count());
        for (std::size_t index = 0U; index < matrix_build.matrix.variant_count(); ++index) {
            auto item = variant_universe(
                matrix_build.matrix,
                index,
                approved_groups,
                case_count,
                control_count,
                request.association_options
            );
            if (item.allele_family_member) ++preview.draft.allele_family_size;
            if (item.carrier_family_member) ++preview.draft.carrier_family_size;
            preview.draft.test_universe.push_back(std::move(item));
        }
    }

    preview.preview_digest = digester_.sha256(
        canonical_cohort_analysis_snapshot(preview.draft, false)
    );
    if (!valid_sha256(preview.preview_digest)) {
        throw CohortAnalysisApprovalError{
            CohortAnalysisApprovalErrorCode::invalid_request,
            "Cohort analysis digester returned an invalid SHA-256"
        };
    }
    preview.draft.preview_digest = preview.preview_digest;
    preview.ready = !blocking_issues(preview.issues);
    return preview;
}

CohortAnalysisSnapshot CohortAnalysisApprovalService::approve(
    const ApproveCohortAnalysisRequest& request
) {
    if (!valid_sha256(request.expected_preview_digest)) {
        throw CohortAnalysisApprovalError{
            CohortAnalysisApprovalErrorCode::invalid_request,
            "Expected preview digest must be lowercase SHA-256"
        };
    }

    auto checked = preview(request.preview_request);
    if (!checked.ready) {
        throw CohortAnalysisApprovalError{
            CohortAnalysisApprovalErrorCode::preview_not_ready,
            "Cohort analysis preview has blocking eligibility issues"
        };
    }
    if (checked.preview_digest != request.expected_preview_digest) {
        throw CohortAnalysisApprovalError{
            CohortAnalysisApprovalErrorCode::stale_preview,
            "Cohort analysis preview changed and must be reviewed again"
        };
    }

    const std::string approved_at = clock_.now_utc_iso8601();
    if (blank(approved_at) || approved_at.size() > 200U ||
        approved_at.find('\0') != std::string::npos) {
        throw CohortAnalysisApprovalError{
            CohortAnalysisApprovalErrorCode::invalid_request,
            "Approval timestamp is invalid"
        };
    }

    for (std::size_t attempt = 0U; attempt < maximum_id_generation_attempts; ++attempt) {
        auto snapshot = checked.draft;
        snapshot.analysis_id = ids_.generate();
        snapshot.approved_at_utc = approved_at;
        if (blank(snapshot.analysis_id) || snapshot.analysis_id.size() > 128U ||
            snapshot.analysis_id.find('\0') != std::string::npos) {
            throw CohortAnalysisApprovalError{
                CohortAnalysisApprovalErrorCode::invalid_request,
                "Generated analysis id is invalid"
            };
        }
        snapshot.snapshot_digest = digester_.sha256(
            canonical_cohort_analysis_snapshot(snapshot, true)
        );
        if (!valid_sha256(snapshot.snapshot_digest)) {
            throw CohortAnalysisApprovalError{
                CohortAnalysisApprovalErrorCode::invalid_request,
                "Cohort analysis snapshot digester returned an invalid SHA-256"
            };
        }

        const auto stored = snapshots_.create(snapshot);
        if (stored == CohortAnalysisStoreResult::analysis_id_conflict) continue;
        if (stored != CohortAnalysisStoreResult::stored) {
            throw CohortAnalysisApprovalError{
                CohortAnalysisApprovalErrorCode::persistence_conflict,
                "Immutable cohort analysis snapshot could not be persisted"
            };
        }
        const auto reloaded = snapshots_.find(snapshot.project_id, snapshot.analysis_id);
        if (!reloaded.has_value() || reloaded->snapshot_digest != snapshot.snapshot_digest) {
            throw CohortAnalysisApprovalError{
                CohortAnalysisApprovalErrorCode::persistence_conflict,
                "Persisted cohort analysis snapshot could not be verified"
            };
        }
        return *reloaded;
    }

    throw CohortAnalysisApprovalError{
        CohortAnalysisApprovalErrorCode::analysis_id_exhausted,
        "Unable to allocate a unique cohort analysis id"
    };
}

}  // namespace biocore::application
