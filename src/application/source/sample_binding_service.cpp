#include "biocore/application/sample_binding_service.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/i_input_file_storage.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_project_sample_binding_store.hpp"
#include "biocore/application/i_project_sample_store.hpp"
#include "biocore/domain/managed_file.hpp"

namespace biocore::application {
namespace {

void issue(
    SampleBindingPreview& preview,
    const SampleBindingIssueSeverity severity,
    std::string code,
    std::string message
) {
    preview.issues.push_back(SampleBindingIssue{
        severity, std::move(code), std::move(message)
    });
}

[[nodiscard]] bool sample_exists(
    IProjectSampleStore& store,
    const std::string_view project_id,
    const std::string_view sample_id
) {
    const auto samples = store.list(project_id);
    if (!samples.has_value()) return false;
    return std::ranges::any_of(*samples, [sample_id](const auto& sample) {
        return sample.sample_id() == sample_id;
    });
}

[[nodiscard]] bool accepted_type(
    const domain::SampleInputLayout layout,
    const std::string_view type
) noexcept {
    switch (layout) {
        case domain::SampleInputLayout::single_fastq:
        case domain::SampleInputLayout::paired_fastq:
            return type == "fastq";
        case domain::SampleInputLayout::alignment:
            return type == "sam" || type == "bam";
        case domain::SampleInputLayout::variants:
            return type == "vcf";
    }
    return false;
}

[[nodiscard]] bool integrity_verified(
    SampleBindingPreview& preview,
    const IInputFileStorage& storage,
    const domain::ManagedFile& file,
    const std::string_view role
) {
    const auto result = storage.verify_managed_file(file);
    if (result.status == ManagedFileIntegrityStatus::verified) return true;

    issue(
        preview,
        SampleBindingIssueSeverity::blocker,
        std::string{role} + "_integrity_" + std::string{to_string(result.status)},
        std::string{role} + " file is not integrity-verified: " +
            std::string{to_string(result.status)}
    );
    return false;
}

[[nodiscard]] SampleBindingReferenceStatus map_reference_status(
    const ReferenceCompatibilityStatus status
) noexcept {
    switch (status) {
        case ReferenceCompatibilityStatus::not_declared_by_format:
            return SampleBindingReferenceStatus::not_declared_by_format;
        case ReferenceCompatibilityStatus::verified:
            return SampleBindingReferenceStatus::verified;
        case ReferenceCompatibilityStatus::mismatch:
            return SampleBindingReferenceStatus::mismatch;
        case ReferenceCompatibilityStatus::evidence_unavailable:
            return SampleBindingReferenceStatus::evidence_unavailable;
    }
    return SampleBindingReferenceStatus::evidence_unavailable;
}

}  // namespace

bool SampleBindingPreview::valid() const noexcept {
    return std::ranges::none_of(issues, [](const SampleBindingIssue& item) {
        return item.severity == SampleBindingIssueSeverity::blocker;
    });
}

SampleBindingService::SampleBindingService(
    IProjectSampleStore& samples,
    IProjectSampleBindingStore& bindings,
    IManagedFileRepository& managed_files,
    const IInputFileStorage& input_storage,
    const IReferenceCompatibilityInspector& reference_inspector
) noexcept
    : samples_{samples},
      bindings_{bindings},
      managed_files_{managed_files},
      input_storage_{input_storage},
      reference_inspector_{reference_inspector} {}

SampleBindingPreview SampleBindingService::preview(
    const domain::ProjectSampleBinding& binding
) {
    SampleBindingPreview result{binding, SampleBindingReferenceStatus::not_requested, {}};

    if (!sample_exists(samples_, binding.project_id(), binding.sample_id())) {
        issue(
            result,
            SampleBindingIssueSeverity::blocker,
            "sample_not_found",
            "Sample binding references a sample outside the requested project"
        );
        return result;
    }

    const auto primary = managed_files_.find_by_id(binding.primary_file_id());
    bool primary_integrity = false;
    if (!primary.has_value()) {
        issue(
            result,
            SampleBindingIssueSeverity::blocker,
            "primary_file_missing",
            "Primary sample input file is missing"
        );
    } else {
        if (!accepted_type(binding.layout(), primary->file_type())) {
            issue(
                result,
                SampleBindingIssueSeverity::blocker,
                "primary_file_role_mismatch",
                "Primary file type is incompatible with the selected sample input role"
            );
        }
        primary_integrity = integrity_verified(
            result, input_storage_, *primary, "primary"
        );
    }

    if (binding.layout() == domain::SampleInputLayout::paired_fastq) {
        const auto secondary = managed_files_.find_by_id(*binding.secondary_file_id());
        if (!secondary.has_value()) {
            issue(
                result,
                SampleBindingIssueSeverity::blocker,
                "read2_file_missing",
                "Paired FASTQ binding is missing its read2 file"
            );
        } else {
            if (secondary->file_type() != "fastq") {
                issue(
                    result,
                    SampleBindingIssueSeverity::blocker,
                    "read2_file_role_mismatch",
                    "Paired FASTQ read2 must reference a FASTQ managed file"
                );
            }
            static_cast<void>(
                integrity_verified(result, input_storage_, *secondary, "read2")
            );
        }
    }

    if (!binding.reference_file_id().has_value()) {
        result.reference_status = SampleBindingReferenceStatus::not_requested;
        return result;
    }

    const auto reference = managed_files_.find_by_id(*binding.reference_file_id());
    bool reference_integrity = false;
    if (!reference.has_value()) {
        issue(
            result,
            SampleBindingIssueSeverity::blocker,
            "reference_file_missing",
            "Selected reference file is missing"
        );
    } else {
        if (reference->file_type() != "fasta") {
            issue(
                result,
                SampleBindingIssueSeverity::blocker,
                "reference_file_role_mismatch",
                "Selected reference must be a FASTA managed file"
            );
        }
        reference_integrity = integrity_verified(
            result, input_storage_, *reference, "reference"
        );
    }

    if (!primary.has_value() || !reference.has_value() ||
        !primary_integrity || !reference_integrity ||
        !accepted_type(binding.layout(), primary->file_type()) ||
        reference->file_type() != "fasta") {
        result.reference_status = SampleBindingReferenceStatus::evidence_unavailable;
        return result;
    }

    const auto compatibility = reference_inspector_.inspect(*primary, *reference);
    result.reference_status = map_reference_status(compatibility.status);
    switch (compatibility.status) {
        case ReferenceCompatibilityStatus::verified:
            break;
        case ReferenceCompatibilityStatus::not_declared_by_format:
            issue(
                result,
                SampleBindingIssueSeverity::warning,
                "reference_not_declared_by_format",
                compatibility.detail.empty()
                    ? "Input format does not declare reference identity; compatibility is not verified"
                    : compatibility.detail
            );
            break;
        case ReferenceCompatibilityStatus::mismatch:
            issue(
                result,
                SampleBindingIssueSeverity::blocker,
                "reference_mismatch",
                compatibility.detail.empty()
                    ? "Input reference declaration is incompatible with the selected FASTA"
                    : compatibility.detail
            );
            break;
        case ReferenceCompatibilityStatus::evidence_unavailable:
            issue(
                result,
                SampleBindingIssueSeverity::blocker,
                "reference_evidence_unavailable",
                compatibility.detail.empty()
                    ? "Reference compatibility evidence could not be read safely"
                    : compatibility.detail
            );
            break;
    }

    return result;
}

SampleBindingPreview SampleBindingService::commit(
    const domain::ProjectSampleBinding& binding
) {
    SampleBindingPreview result = preview(binding);
    if (result.valid()) {
        bindings_.upsert(binding);
    }
    return result;
}

std::optional<domain::ProjectSampleBinding> SampleBindingService::find(
    const std::string_view project_id,
    const std::string_view sample_id
) {
    return bindings_.find(project_id, sample_id);
}

}  // namespace biocore::application
