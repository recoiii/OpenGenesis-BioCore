#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "biocore/domain/project.hpp"

namespace {

using biocore::domain::Project;

[[nodiscard]] bool throws_invalid_argument(const std::function<void()>& operation) {
    try {
        operation();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

}  // namespace

int main() {
    const Project project{
        "project-001",
        "Reference cohort",
        "/data/biocore/reference-cohort",
        "2026-08-06T17:00:00Z",
        "2026-08-06T17:00:00Z",
    };

    if (project.id() != "project-001" || project.name() != "Reference cohort" ||
        project.root_path() != "/data/biocore/reference-cohort" ||
        project.created_at_utc() != "2026-08-06T17:00:00Z" ||
        project.updated_at_utc() != "2026-08-06T17:00:00Z") {
        std::cerr << "Project fields were not preserved\n";
        return EXIT_FAILURE;
    }

    const auto make_project = [](
                                  std::string id,
                                  std::string name,
                                  std::string root,
                                  std::string created,
                                  std::string updated
                              ) {
        return Project{std::move(id), std::move(name), std::move(root), std::move(created), std::move(updated)};
    };

    if (!throws_invalid_argument([&] { (void)make_project("", "name", "/root", "created", "updated"); }) ||
        !throws_invalid_argument([&] { (void)make_project("id", "   ", "/root", "created", "updated"); }) ||
        !throws_invalid_argument([&] { (void)make_project("id", "name", "", "created", "updated"); }) ||
        !throws_invalid_argument([&] { (void)make_project("id", "name", "/root", "", "updated"); }) ||
        !throws_invalid_argument([&] { (void)make_project("id", "name", "/root", "created", ""); }) ||
        !throws_invalid_argument([&] {
            (void)make_project(
                std::string(Project::maximum_id_length + 1U, 'i'), "name", "/root", "created", "updated");
        }) ||
        !throws_invalid_argument([&] {
            (void)make_project(
                "id", std::string(Project::maximum_name_length + 1U, 'n'), "/root", "created", "updated");
        }) ||
        !throws_invalid_argument([&] {
            (void)make_project(std::string{"id\0hidden", 9U}, "name", "/root", "created", "updated");
        })) {
        std::cerr << "Project validation accepted invalid input\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
