#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>

#include "biocore/domain/project.hpp"

namespace biocore::bootstrap {

struct ProjectInitArguments final {
    std::filesystem::path project_root;
    std::string project_name;
    std::filesystem::path catalog_database_path;
};

[[nodiscard]] domain::Project initialize_project(
    const ProjectInitArguments& arguments,
    std::ostream& standard_output
);

}  // namespace biocore::bootstrap
