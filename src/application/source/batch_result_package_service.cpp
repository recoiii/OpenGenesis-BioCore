#include "biocore/application/batch_result_package_service.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include "biocore/application/batch_result_package_error.hpp"
#include "biocore/application/batch_results_service.hpp"
#include "biocore/application/build_info.hpp"
#include "biocore/application/i_artifact_content_access.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/domain/job_status.hpp"

namespace biocore::application {
namespace {

[[nodiscard]] bool same_identity(
    const BatchResultArtifactLink& link,
    const GeneratedOutputArtifact& artifact
) {
    return artifact.file.id() == link.managed_file_id &&
           artifact.provenance.job_id == link.job_id &&
           artifact.provenance.step_id == link.step_id &&
           artifact.provenance.output_port == link.output_port &&
           artifact.provenance.module_id == link.module_id &&
           artifact.provenance.plugin_version == link.plugin_version &&
           artifact.provenance.file_type == link.file_type &&
           artifact.provenance.relative_project_path == link.relative_project_path &&
           artifact.file.relative_project_path() ==
               std::optional<std::string>{link.relative_project_path} &&
           artifact.file.size_bytes() == link.size_bytes &&
           artifact.file.checksum_algorithm() == std::optional<std::string>{"sha256"} &&
           artifact.file.checksum_value() == link.sha256;
}

[[nodiscard]] const char* integrity_message(
    const ArtifactContentStatus status
) noexcept {
    switch (status) {
        case ArtifactContentStatus::verified:
            return "Batch result artifact content is verified";
        case ArtifactContentStatus::missing:
            return "Batch result artifact content is missing";
        case ArtifactContentStatus::unsafe_path:
            return "Batch result artifact path is unsafe";
        case ArtifactContentStatus::not_regular:
            return "Batch result artifact is not a regular file";
        case ArtifactContentStatus::size_mismatch:
            return "Batch result artifact size does not match persisted metadata";
        case ArtifactContentStatus::checksum_unavailable:
            return "Batch result artifact has no usable SHA-256 checksum";
        case ArtifactContentStatus::checksum_mismatch:
            return "Batch result artifact SHA-256 does not match persisted metadata";
        case ArtifactContentStatus::io_error:
            return "Batch result artifact could not be verified";
    }
    return "Batch result artifact could not be verified";
}

[[nodiscard]] bool stable_sample(const BatchSampleResults& sample) noexcept {
    if (sample.disposition == BatchPlanSampleDisposition::excluded) return true;
    return sample.state == BatchResultSampleState::submitted &&
           sample.latest_job_status.has_value() &&
           domain::is_terminal(*sample.latest_job_status);
}

[[nodiscard]] bool complete_sample(const BatchSampleResults& sample) noexcept {
    if (sample.disposition == BatchPlanSampleDisposition::excluded) return true;
    return sample.state == BatchResultSampleState::submitted &&
           sample.latest_job_status == std::optional<domain::JobStatus>{
               domain::JobStatus::completed
           };
}

}  // namespace

BatchResultPackageService::BatchResultPackageService(
    BatchResultsService& results,
    IManagedFileRepository& managed_files,
    IArtifactContentAccess& content_access,
    IUtcClock& clock
) noexcept
    : results_{results},
      managed_files_{managed_files},
      content_access_{content_access},
      clock_{clock} {}

BatchResultPackage BatchResultPackageService::build(const std::string_view plan_id) {
    auto overview = results_.overview(plan_id);

    BatchResultPackage package{
        .schema_version = 1U,
        .producer_name = "OpenGenesis-BioCore",
        .producer_version = std::string{BuildInfo::version()},
        .generated_at_utc = clock_.now_utc_iso8601(),
        .stable_snapshot = std::ranges::all_of(overview.samples, stable_sample),
        .complete_results = std::ranges::all_of(overview.samples, complete_sample),
        .overview = std::move(overview),
        .verified_artifacts = {},
    };

    for (const auto& sample : package.overview.samples) {
        for (const auto& link : sample.artifacts) {
            const auto artifact = managed_files_.find_generated_output(
                link.job_id, link.step_id, link.output_port
            );
            if (!artifact.has_value()) {
                throw BatchResultPackageError{
                    BatchResultPackageErrorCode::artifact_missing,
                    "Batch result artifact disappeared from persistence"
                };
            }
            if (!same_identity(link, *artifact)) {
                throw BatchResultPackageError{
                    BatchResultPackageErrorCode::artifact_identity_mismatch,
                    "Batch result artifact identity no longer matches the current result snapshot"
                };
            }

            const auto verification = content_access_.verify_for_download(*artifact);
            if (verification.status != ArtifactContentStatus::verified ||
                !verification.computed_sha256.has_value() ||
                verification.actual_size_bytes != link.size_bytes ||
                link.sha256 != verification.computed_sha256) {
                throw BatchResultPackageError{
                    BatchResultPackageErrorCode::artifact_integrity_error,
                    integrity_message(verification.status)
                };
            }

            package.verified_artifacts.push_back(BatchResultPackageArtifact{
                .artifact = link,
                .verified_sha256 = *verification.computed_sha256,
            });
        }
    }

    std::ranges::sort(
        package.verified_artifacts,
        [](const auto& left, const auto& right) {
            return std::tie(
                       left.artifact.sample_id,
                       left.artifact.step_id,
                       left.artifact.output_port,
                       left.artifact.job_id,
                       left.artifact.managed_file_id
                   ) <
                   std::tie(
                       right.artifact.sample_id,
                       right.artifact.step_id,
                       right.artifact.output_port,
                       right.artifact.job_id,
                       right.artifact.managed_file_id
                   );
        }
    );
    return package;
}

}  // namespace biocore::application
