#pragma once

#include <cstddef>
#include <string>

#include "biocore/domain/project.hpp"

namespace biocore::application {

class IIdGenerator;
class IPathCanonicalizer;
class IProjectRepository;
class IProjectWorkspace;
class IUtcClock;

struct CreateProjectRequest final {
    std::string name;
    std::string root_path;
};

class ProjectService final {
public:
    static constexpr std::size_t maximum_id_generation_attempts = 8U;

    ProjectService(
        IProjectRepository& repository,
        IIdGenerator& id_generator,
        IUtcClock& clock,
        IPathCanonicalizer& path_canonicalizer,
        IProjectWorkspace& project_workspace
    ) noexcept;

    [[nodiscard]] domain::Project create(CreateProjectRequest request);

private:
    IProjectRepository& repository_;
    IIdGenerator& id_generator_;
    IUtcClock& clock_;
    IPathCanonicalizer& path_canonicalizer_;
    IProjectWorkspace& project_workspace_;
};

}  // namespace biocore::application
