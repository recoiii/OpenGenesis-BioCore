#include "biocore/application/output_artifact_service.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_output_artifact_inspector.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/output_artifact_service_error.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/storage_mode.hpp"

namespace biocore::application {
namespace {

constexpr std::size_t maximum_batch_artifacts = 256U;

[[nodiscard]] bool is_blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
               return std::isspace(static_cast<unsigned char>(character)) != 0;
           });
}

void require_text(
    const std::string_view value,
    const std::string_view name,
    const std::size_t maximum_length
) {
    if (is_blank(value) || value.find('\0') != std::string_view::npos ||
        value.size() > maximum_length) {
        throw std::invalid_argument(std::string{name} + " is invalid");
    }
}


[[nodiscard]] bool is_lower_sha256(const std::string_view algorithm, const std::string_view value) {
    if (algorithm != "sha256" || value.size() != 64U) {
        return false;
    }
    return std::ranges::all_of(value, [](const char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
    });
}

void validate_request(const RegisterGeneratedOutputRequest& request) {
    require_text(request.job_id, "Generated output job id", 128U);
    require_text(request.step_id, "Generated output step id", 200U);
    require_text(request.output_port, "Generated output port", 200U);
    require_text(request.plugin_id, "Generated output plugin id", 200U);
    require_text(request.plugin_version, "Generated output plugin version", 200U);
    require_text(request.module_id, "Generated output module id", 200U);
    require_text(
        request.file_type,
        "Generated output file type",
        domain::ManagedFile::maximum_file_type_length
    );
    require_text(
        request.relative_project_path,
        "Generated output relative project path",
        domain::ManagedFile::maximum_metadata_length
    );
}

[[nodiscard]] bool provenance_matches(
    const GeneratedOutputArtifact& artifact,
    const RegisterGeneratedOutputRequest& request,
    const double step_progress
) {
    return artifact.file.storage_mode() == domain::StorageMode::generated_output &&
           artifact.file.file_type() == request.file_type &&
           artifact.file.relative_project_path().has_value() &&
           *artifact.file.relative_project_path() == request.relative_project_path &&
           artifact.provenance.job_id == request.job_id &&
           artifact.provenance.step_id == request.step_id &&
           artifact.provenance.output_port == request.output_port &&
           artifact.provenance.plugin_id == request.plugin_id &&
           artifact.provenance.plugin_version == request.plugin_version &&
           artifact.provenance.module_id == request.module_id &&
           artifact.provenance.file_type == request.file_type &&
           artifact.provenance.relative_project_path == request.relative_project_path &&
           artifact.provenance.step_progress == step_progress;
}

void validate_batch_shape(const std::vector<RegisterGeneratedOutputRequest>& requests) {
    if (requests.empty() || requests.size() > maximum_batch_artifacts) {
        throw std::invalid_argument("Generated output batch size is invalid");
    }

    const auto& first = requests.front();
    std::unordered_set<std::string_view> ports;
    std::unordered_set<std::string_view> paths;
    ports.reserve(requests.size());
    paths.reserve(requests.size());

    for (const auto& request : requests) {
        validate_request(request);
        if (request.job_id != first.job_id || request.step_id != first.step_id ||
            request.plugin_id != first.plugin_id ||
            request.plugin_version != first.plugin_version ||
            request.module_id != first.module_id) {
            throw std::invalid_argument(
                "Generated output batch must belong to one job step and plugin module"
            );
        }
        if (!ports.insert(request.output_port).second) {
            throw std::invalid_argument("Generated output batch contains duplicate output ports");
        }
        if (!paths.insert(request.relative_project_path).second) {
            throw std::invalid_argument("Generated output batch contains duplicate output paths");
        }
    }
}

[[nodiscard]] std::vector<GeneratedOutputArtifact> existing_batch_or_throw(
    IManagedFileRepository& repository,
    const std::vector<RegisterGeneratedOutputRequest>& requests,
    const double step_progress,
    const bool require_complete
) {
    std::vector<GeneratedOutputArtifact> existing;
    existing.reserve(requests.size());
    std::size_t found = 0U;
    for (const auto& request : requests) {
        auto artifact = repository.find_generated_output(
            request.job_id, request.step_id, request.output_port
        );
        if (!artifact.has_value()) {
            continue;
        }
        ++found;
        if (!provenance_matches(*artifact, request, step_progress)) {
            throw OutputArtifactServiceError{
                OutputArtifactServiceErrorCode::provenance_conflict,
                "Generated output provenance conflicts with an existing artifact",
            };
        }
        existing.push_back(std::move(*artifact));
    }

    if (found == 0U) {
        return {};
    }
    if (found != requests.size()) {
        throw OutputArtifactServiceError{
            OutputArtifactServiceErrorCode::persistence_conflict,
            "Generated output step has a partially registered artifact batch",
        };
    }
    if (!require_complete) {
        return existing;
    }

    // Preserve caller request order regardless of repository/list ordering.
    std::vector<GeneratedOutputArtifact> ordered;
    ordered.reserve(requests.size());
    for (const auto& request : requests) {
        auto iterator = std::ranges::find_if(existing, [&request](const auto& artifact) {
            return artifact.provenance.output_port == request.output_port;
        });
        if (iterator == existing.end()) {
            throw OutputArtifactServiceError{
                OutputArtifactServiceErrorCode::persistence_conflict,
                "Generated output batch changed while it was being read",
            };
        }
        ordered.push_back(*iterator);
    }
    return ordered;
}

}  // namespace

OutputArtifactService::OutputArtifactService(
    IManagedFileRepository& repository,
    IOutputArtifactInspector& inspector,
    IIdGenerator& id_generator,
    IUtcClock& clock
) noexcept
    : repository_{repository},
      inspector_{inspector},
      id_generator_{id_generator},
      clock_{clock} {}

GeneratedOutputArtifact OutputArtifactService::register_generated_output(
    const RegisterGeneratedOutputRequest& request,
    const double step_progress
) {
    auto artifacts = register_generated_outputs_batch({request}, step_progress);
    return std::move(artifacts.front());
}

std::vector<GeneratedOutputArtifact> OutputArtifactService::register_generated_outputs_batch(
    const std::vector<RegisterGeneratedOutputRequest>& requests,
    const double step_progress
) {
    validate_batch_shape(requests);
    if (!std::isfinite(step_progress) || step_progress < 0.0 || step_progress > 1.0) {
        throw std::invalid_argument("Generated output step progress must be finite and between 0 and 1");
    }

    if (auto existing = existing_batch_or_throw(repository_, requests, step_progress, true); !existing.empty()) {
        return existing;
    }

    std::vector<InspectedOutputArtifact> inspected;
    inspected.reserve(requests.size());
    for (const auto& request : requests) {
        auto value = inspector_.inspect_existing_output(request.relative_project_path);
        if (value.relative_project_path != request.relative_project_path) {
            throw OutputArtifactServiceError{
                OutputArtifactServiceErrorCode::provenance_conflict,
                "Generated output inspector returned a different project-relative path",
            };
        }
        if (!is_lower_sha256(value.checksum_algorithm, value.checksum_value)) {
            throw OutputArtifactServiceError{
                OutputArtifactServiceErrorCode::provenance_conflict,
                "Generated output inspector did not provide a valid SHA-256 checksum",
            };
        }
        inspected.push_back(std::move(value));
    }

    const std::string timestamp = clock_.now_utc_iso8601();
    for (int batch_attempt = 0; batch_attempt < maximum_identifier_attempts; ++batch_attempt) {
        std::vector<GeneratedOutputArtifact> artifacts;
        artifacts.reserve(requests.size());
        std::unordered_set<std::string> batch_ids;
        batch_ids.reserve(requests.size());

        bool retry_identifiers = false;
        for (std::size_t index = 0U; index < requests.size(); ++index) {
            const std::string id = id_generator_.generate();
            if (!batch_ids.insert(id).second || repository_.find_by_id(id).has_value()) {
                retry_identifiers = true;
                break;
            }

            const auto& request = requests[index];
            const auto& inspected_value = inspected[index];
            domain::ManagedFile file{
                id,
                inspected_value.display_name,
                domain::StorageMode::generated_output,
                std::nullopt,
                inspected_value.managed_path,
                inspected_value.relative_project_path,
                request.file_type,
                inspected_value.size_bytes,
                inspected_value.modified_at_utc,
                inspected_value.checksum_algorithm,
                inspected_value.checksum_value,
                timestamp,
                timestamp,
            };
            GeneratedOutputProvenance provenance{
                .job_id = request.job_id,
                .step_id = request.step_id,
                .output_port = request.output_port,
                .plugin_id = request.plugin_id,
                .plugin_version = request.plugin_version,
                .module_id = request.module_id,
                .file_type = request.file_type,
                .relative_project_path = request.relative_project_path,
                .step_progress = step_progress,
                .registered_at_utc = timestamp,
            };
            artifacts.push_back(GeneratedOutputArtifact{std::move(file), std::move(provenance)});
        }
        if (retry_identifiers) {
            continue;
        }

        if (repository_.add_generated_outputs_batch(artifacts)) {
            return artifacts;
        }

        if (auto existing = existing_batch_or_throw(repository_, requests, step_progress, true); !existing.empty()) {
            return existing;
        }
        for (const auto& request : requests) {
            if (repository_.find_by_relative_project_path(request.relative_project_path).has_value()) {
                throw OutputArtifactServiceError{
                    OutputArtifactServiceErrorCode::persistence_conflict,
                    "Generated output path is already registered to a different managed file",
                };
            }
        }
    }

    throw OutputArtifactServiceError{
        OutputArtifactServiceErrorCode::identifier_generation_exhausted,
        "Unable to generate unique identifiers for the generated-output batch",
    };
}

std::vector<GeneratedOutputArtifact> OutputArtifactService::list_for_job(
    const std::string_view job_id
) {
    require_text(job_id, "Generated output job id", 128U);
    return repository_.list_generated_outputs(job_id);
}

}  // namespace biocore::application
