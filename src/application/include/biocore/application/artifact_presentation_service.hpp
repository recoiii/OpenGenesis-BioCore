#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/domain/job_failure.hpp"
#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/job_status.hpp"

namespace biocore::application {

class IArtifactContentAccess;
class IJobRepository;
class IManagedFileRepository;
class IUtcClock;

struct ArtifactMetadata final {
    std::string managed_file_id;
    std::string display_name;
    std::string job_id;
    std::string step_id;
    std::string output_port;
    std::string plugin_id;
    std::string plugin_version;
    std::string module_id;
    std::string file_type;
    std::string relative_project_path;
    std::int64_t size_bytes{0};
    std::optional<std::string> checksum_algorithm;
    std::optional<std::string> checksum_value;
    double step_progress{0.0};
    std::string registered_at_utc;
};

struct ArtifactDownloadDescriptor final {
    ArtifactMetadata metadata;
    std::string content_path;
    std::string verified_sha256;
};

struct ArtifactExportEntry final {
    ArtifactMetadata metadata;
    std::string verified_sha256;
};

struct PipelineExecutionReport final {
    std::string job_id;
    std::optional<std::string> analysis_id;
    std::optional<std::string> pipeline_id;
    std::optional<std::string> pipeline_version;
    domain::JobStatus status{domain::JobStatus::draft};
    domain::JobPriority priority{domain::JobPriority::normal};
    double progress{0.0};
    std::optional<std::string> active_step_id;
    std::string created_at_utc;
    std::string updated_at_utc;
    std::optional<std::string> started_at_utc;
    std::optional<std::string> finished_at_utc;
    std::int64_t revision{0};
    std::int64_t attempt_number{1};
    std::optional<domain::JobFailure> failure;
    std::string generated_at_utc;
    std::vector<ArtifactMetadata> artifacts;
};

struct PipelineExportManifest final {
    static constexpr std::uint32_t current_schema_version = 1U;
    std::uint32_t schema_version{current_schema_version};
    std::string producer_version;
    bool stable_snapshot{false};
    PipelineExecutionReport report;
    std::vector<ArtifactExportEntry> artifacts;
};

class ArtifactPresentationService final {
public:
    ArtifactPresentationService(
        IManagedFileRepository& managed_files,
        IJobRepository& jobs,
        IArtifactContentAccess& content_access,
        IUtcClock& clock
    ) noexcept;

    [[nodiscard]] std::vector<ArtifactMetadata> list_for_job(std::string_view job_id);
    [[nodiscard]] std::vector<ArtifactMetadata> list_for_step(
        std::string_view job_id,
        std::string_view step_id
    );
    [[nodiscard]] std::optional<ArtifactMetadata> find_artifact(
        std::string_view job_id,
        std::string_view step_id,
        std::string_view output_port
    );
    [[nodiscard]] ArtifactDownloadDescriptor prepare_download(
        std::string_view job_id,
        std::string_view step_id,
        std::string_view output_port
    );
    [[nodiscard]] PipelineExecutionReport build_job_report(std::string_view job_id);
    [[nodiscard]] PipelineExportManifest build_job_export_manifest(std::string_view job_id);

private:
    IManagedFileRepository& managed_files_;
    IJobRepository& jobs_;
    IArtifactContentAccess& content_access_;
    IUtcClock& clock_;
};

}  // namespace biocore::application
