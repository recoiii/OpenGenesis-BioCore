#include "biocore/application/output_artifact_cleanup_service.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/domain/job.hpp"

namespace biocore::application {
namespace {

[[nodiscard]] bool is_blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
               return std::isspace(static_cast<unsigned char>(character)) != 0;
           });
}

void validate_job_id(const std::string_view job_id) {
    if (is_blank(job_id) || job_id.find('\0') != std::string_view::npos ||
        job_id.size() > domain::Job::maximum_id_length) {
        throw std::invalid_argument("Partial-output cleanup job id is invalid");
    }
}

}  // namespace

OutputArtifactCleanupService::OutputArtifactCleanupService(
    IManagedFileRepository& repository,
    IPartialOutputCleaner& cleaner
) noexcept
    : repository_{repository}, cleaner_{cleaner} {}

PartialOutputCleanupResult OutputArtifactCleanupService::quarantine_unregistered_for_job(
    const std::string_view job_id
) {
    validate_job_id(job_id);
    const auto registered = repository_.list_generated_outputs(job_id);
    std::vector<std::string> protected_paths;
    protected_paths.reserve(registered.size());
    for (const auto& artifact : registered) {
        protected_paths.push_back(artifact.provenance.relative_project_path);
    }
    return cleaner_.quarantine_unregistered_outputs(job_id, protected_paths);
}

}  // namespace biocore::application
