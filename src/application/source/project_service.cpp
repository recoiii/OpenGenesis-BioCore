#include "biocore/application/project_service.hpp"

#include <utility>

#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_path_canonicalizer.hpp"
#include "biocore/application/i_project_repository.hpp"
#include "biocore/application/i_project_workspace.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/project_service_error.hpp"
#include "biocore/application/project_workspace_error.hpp"

namespace biocore::application {

ProjectService::ProjectService(
    IProjectRepository& repository,
    IIdGenerator& id_generator,
    IUtcClock& clock,
    IPathCanonicalizer& path_canonicalizer,
    IProjectWorkspace& project_workspace
) noexcept
    : repository_{repository},
      id_generator_{id_generator},
      clock_{clock},
      path_canonicalizer_{path_canonicalizer},
      project_workspace_{project_workspace} {}

domain::Project ProjectService::create(CreateProjectRequest request) {
    const std::string canonical_root = path_canonicalizer_.canonicalize_existing_directory(request.root_path);
    if (repository_.find_by_root_path(canonical_root).has_value()) {
        throw DuplicateProjectRootError{};
    }

    std::string project_id;
    for (std::size_t attempt = 0; attempt < maximum_id_generation_attempts; ++attempt) {
        project_id = id_generator_.generate();
        if (!repository_.find_by_id(project_id).has_value()) {
            const std::string timestamp = clock_.now_utc_iso8601();
            domain::Project project{
                std::move(project_id),
                std::move(request.name),
                canonical_root,
                timestamp,
                timestamp,
            };
            auto workspace_transaction = project_workspace_.initialize(project);
            if (!workspace_transaction) {
                throw ProjectWorkspaceInitializationError{
                    "Project workspace initializer returned a null transaction"};
            }

            repository_.save(project);
            workspace_transaction->commit();
            return project;
        }
    }

    throw ProjectIdGenerationError{};
}

}  // namespace biocore::application
