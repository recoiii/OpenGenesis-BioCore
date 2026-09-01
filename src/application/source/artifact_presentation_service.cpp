#include "biocore/application/artifact_presentation_service.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <utility>

#include "biocore/application/artifact_presentation_service_error.hpp"
#include "biocore/application/generated_output_artifact.hpp"
#include "biocore/application/i_artifact_content_access.hpp"
#include "biocore/application/i_job_repository.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_utc_clock.hpp"

namespace biocore::application {
namespace {

[[nodiscard]] bool is_blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
               return std::isspace(static_cast<unsigned char>(character)) != 0;
           });
}

void require_identifier(
    const std::string_view value,
    const std::string_view field,
    const std::size_t maximum_length
) {
    if (is_blank(value) || value.size() > maximum_length ||
        value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(std::string{field} + " is invalid");
    }
}

[[nodiscard]] ArtifactMetadata to_metadata(const GeneratedOutputArtifact& artifact) {
    return ArtifactMetadata{
        .managed_file_id = std::string{artifact.file.id()},
        .display_name = std::string{artifact.file.display_name()},
        .job_id = artifact.provenance.job_id,
        .step_id = artifact.provenance.step_id,
        .output_port = artifact.provenance.output_port,
        .plugin_id = artifact.provenance.plugin_id,
        .plugin_version = artifact.provenance.plugin_version,
        .module_id = artifact.provenance.module_id,
        .file_type = artifact.provenance.file_type,
        .relative_project_path = artifact.provenance.relative_project_path,
        .size_bytes = artifact.file.size_bytes(),
        .checksum_algorithm = artifact.file.checksum_algorithm(),
        .checksum_value = artifact.file.checksum_value(),
        .step_progress = artifact.provenance.step_progress,
        .registered_at_utc = artifact.provenance.registered_at_utc,
    };
}

void sort_metadata(std::vector<ArtifactMetadata>& artifacts) {
    std::ranges::sort(artifacts, [](const ArtifactMetadata& left, const ArtifactMetadata& right) {
        if (left.step_id != right.step_id) return left.step_id < right.step_id;
        if (left.output_port != right.output_port) return left.output_port < right.output_port;
        return left.managed_file_id < right.managed_file_id;
    });
}

[[noreturn]] void throw_job_not_found() {
    throw ArtifactPresentationError{
        ArtifactPresentationErrorCode::job_not_found,
        "Job was not found",
    };
}

[[noreturn]] void throw_content_error(const ArtifactContentStatus status) {
    switch (status) {
        case ArtifactContentStatus::missing:
            throw ArtifactPresentationError{
                ArtifactPresentationErrorCode::content_missing,
                "Artifact content is missing",
            };
        case ArtifactContentStatus::unsafe_path:
            throw ArtifactPresentationError{
                ArtifactPresentationErrorCode::unsafe_content,
                "Artifact content path is unsafe",
            };
        case ArtifactContentStatus::not_regular:
            throw ArtifactPresentationError{
                ArtifactPresentationErrorCode::content_not_regular,
                "Artifact content is not a regular file",
            };
        case ArtifactContentStatus::size_mismatch:
            throw ArtifactPresentationError{
                ArtifactPresentationErrorCode::size_mismatch,
                "Artifact content size differs from persisted metadata",
            };
        case ArtifactContentStatus::checksum_unavailable:
            throw ArtifactPresentationError{
                ArtifactPresentationErrorCode::checksum_unavailable,
                "Artifact has no supported persisted checksum",
            };
        case ArtifactContentStatus::checksum_mismatch:
            throw ArtifactPresentationError{
                ArtifactPresentationErrorCode::checksum_mismatch,
                "Artifact content SHA-256 differs from persisted metadata",
            };
        case ArtifactContentStatus::io_error:
            throw ArtifactPresentationError{
                ArtifactPresentationErrorCode::content_io_error,
                "Artifact content could not be verified",
            };
        case ArtifactContentStatus::verified:
            break;
    }
    throw ArtifactPresentationError{
        ArtifactPresentationErrorCode::content_io_error,
        "Artifact content verification returned an invalid result",
    };
}

}  // namespace

ArtifactPresentationService::ArtifactPresentationService(
    IManagedFileRepository& managed_files,
    IJobRepository& jobs,
    IArtifactContentAccess& content_access,
    IUtcClock& clock
) noexcept
    : managed_files_{managed_files}, jobs_{jobs}, content_access_{content_access}, clock_{clock} {}

std::vector<ArtifactMetadata> ArtifactPresentationService::list_for_job(
    const std::string_view job_id
) {
    require_identifier(job_id, "Artifact job id", 128U);
    if (!jobs_.find_by_id(job_id).has_value()) {
        throw_job_not_found();
    }
    const auto artifacts = managed_files_.list_generated_outputs(job_id);
    std::vector<ArtifactMetadata> result;
    result.reserve(artifacts.size());
    for (const auto& artifact : artifacts) {
        result.push_back(to_metadata(artifact));
    }
    sort_metadata(result);
    return result;
}

std::vector<ArtifactMetadata> ArtifactPresentationService::list_for_step(
    const std::string_view job_id,
    const std::string_view step_id
) {
    require_identifier(job_id, "Artifact job id", 128U);
    require_identifier(step_id, "Artifact step id", 200U);
    if (!jobs_.find_by_id(job_id).has_value()) {
        throw_job_not_found();
    }
    const auto artifacts = managed_files_.list_generated_outputs(job_id);
    std::vector<ArtifactMetadata> result;
    for (const auto& artifact : artifacts) {
        if (artifact.provenance.step_id == step_id) {
            result.push_back(to_metadata(artifact));
        }
    }
    sort_metadata(result);
    return result;
}

std::optional<ArtifactMetadata> ArtifactPresentationService::find_artifact(
    const std::string_view job_id,
    const std::string_view step_id,
    const std::string_view output_port
) {
    require_identifier(job_id, "Artifact job id", 128U);
    require_identifier(step_id, "Artifact step id", 200U);
    require_identifier(output_port, "Artifact output port", 200U);
    if (!jobs_.find_by_id(job_id).has_value()) {
        throw_job_not_found();
    }
    auto artifact = managed_files_.find_generated_output(job_id, step_id, output_port);
    if (!artifact.has_value()) {
        return std::nullopt;
    }
    return to_metadata(*artifact);
}

ArtifactDownloadDescriptor ArtifactPresentationService::prepare_download(
    const std::string_view job_id,
    const std::string_view step_id,
    const std::string_view output_port
) {
    require_identifier(job_id, "Artifact job id", 128U);
    require_identifier(step_id, "Artifact step id", 200U);
    require_identifier(output_port, "Artifact output port", 200U);
    if (!jobs_.find_by_id(job_id).has_value()) {
        throw_job_not_found();
    }
    auto artifact = managed_files_.find_generated_output(job_id, step_id, output_port);
    if (!artifact.has_value()) {
        throw ArtifactPresentationError{
            ArtifactPresentationErrorCode::artifact_not_found,
            "Artifact was not found",
        };
    }

    auto verification = content_access_.verify_for_download(*artifact);
    if (verification.status != ArtifactContentStatus::verified ||
        !verification.content_path.has_value() ||
        !verification.computed_sha256.has_value()) {
        throw_content_error(verification.status);
    }

    return ArtifactDownloadDescriptor{
        .metadata = to_metadata(*artifact),
        .content_path = std::move(*verification.content_path),
        .verified_sha256 = std::move(*verification.computed_sha256),
    };
}

PipelineExecutionReport ArtifactPresentationService::build_job_report(
    const std::string_view job_id
) {
    require_identifier(job_id, "Report job id", 128U);
    const auto job = jobs_.find_by_id(job_id);
    if (!job.has_value()) {
        throw_job_not_found();
    }

    return PipelineExecutionReport{
        .job_id = std::string{job->id()},
        .analysis_id = job->analysis_id(),
        .pipeline_id = job->pipeline_id(),
        .pipeline_version = job->pipeline_version(),
        .status = job->status(),
        .priority = job->priority(),
        .progress = job->progress(),
        .active_step_id = job->active_step_id(),
        .created_at_utc = std::string{job->created_at_utc()},
        .updated_at_utc = std::string{job->updated_at_utc()},
        .started_at_utc = job->started_at_utc(),
        .finished_at_utc = job->finished_at_utc(),
        .revision = job->revision(),
        .failure = job->failure(),
        .generated_at_utc = clock_.now_utc_iso8601(),
        .artifacts = list_for_job(job_id),
    };
}

}  // namespace biocore::application
