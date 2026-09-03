#include "project_init_bootstrap.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

#include "biocore/application/project_service.hpp"
#include "biocore/infrastructure/filesystem_path_canonicalizer.hpp"
#include "biocore/infrastructure/filesystem_project_workspace.hpp"
#include "biocore/infrastructure/sqlite/catalog_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_project_repository.hpp"
#include "biocore/infrastructure/system_clock.hpp"
#include "biocore/infrastructure/uuid_v4_generator.hpp"

namespace biocore::bootstrap {
namespace {

void trace_project_init(const char* message) {
    std::cerr << "[project-init-probe] " << message << '\n' << std::flush;
}

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] bool path_is_within(
    const std::filesystem::path& parent,
    const std::filesystem::path& child
) {
    const auto relative = child.lexically_relative(parent);
    if (relative.empty() || relative.is_absolute()) return false;
    const auto first = relative.begin();
    return first != relative.end() && *first != "..";
}

[[nodiscard]] std::filesystem::path prepare_project_root(
    const std::filesystem::path& requested,
    bool& created
) {
    if (requested.empty()) {
        throw std::invalid_argument("Project root must not be empty");
    }

    std::error_code error;
    const auto absolute = std::filesystem::absolute(requested, error).lexically_normal();
    if (error) {
        throw std::invalid_argument("Project root could not be resolved");
    }

    const auto status = std::filesystem::symlink_status(absolute, error);
    if (!error && std::filesystem::exists(status)) {
        if (std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status)) {
            throw std::invalid_argument(
                "Project root must be an existing non-symlink directory or a new directory path"
            );
        }
        created = false;
    } else {
        error.clear();
        const auto parent = absolute.parent_path();
        const auto parent_status = std::filesystem::symlink_status(parent, error);
        if (error || !std::filesystem::is_directory(parent_status) ||
            std::filesystem::is_symlink(parent_status)) {
            throw std::invalid_argument(
                "Project root parent must be an existing non-symlink directory"
            );
        }
        if (!std::filesystem::create_directory(absolute, error) || error) {
            throw std::runtime_error("Unable to create project root directory");
        }
        created = true;
    }

    const auto canonical = std::filesystem::canonical(absolute, error);
    if (error || canonical != absolute) {
        throw std::invalid_argument("Project root must resolve without aliases");
    }
    return canonical;
}

[[nodiscard]] std::filesystem::path prepare_catalog_path(
    const std::filesystem::path& requested,
    const std::filesystem::path& project_root
) {
    if (requested.empty()) {
        throw std::invalid_argument("Catalog database path must not be empty");
    }

    std::error_code error;
    const auto absolute = std::filesystem::absolute(requested, error).lexically_normal();
    if (error || absolute.filename().empty()) {
        throw std::invalid_argument("Catalog database path is invalid");
    }
    if (path_is_within(project_root, absolute) || absolute == project_root) {
        throw std::invalid_argument("Catalog database must remain outside the project workspace");
    }

    const auto parent = absolute.parent_path();
    if (parent.empty()) {
        throw std::invalid_argument("Catalog database parent directory is invalid");
    }
    std::filesystem::create_directories(parent, error);
    if (error) {
        throw std::runtime_error("Unable to create catalog database directory");
    }
    const auto parent_status = std::filesystem::symlink_status(parent, error);
    if (error || !std::filesystem::is_directory(parent_status) ||
        std::filesystem::is_symlink(parent_status)) {
        throw std::invalid_argument("Catalog parent must be a non-symlink directory");
    }
    const auto canonical_parent = std::filesystem::canonical(parent, error);
    if (error || canonical_parent != parent) {
        throw std::invalid_argument("Catalog parent must resolve without aliases");
    }

    const auto status = std::filesystem::symlink_status(absolute, error);
    if (!error && std::filesystem::exists(status) &&
        (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))) {
        throw std::invalid_argument("Existing catalog path must be a non-symlink regular file");
    }
    return absolute;
}

}  // namespace

domain::Project initialize_project(
    const ProjectInitArguments& arguments,
    std::ostream& standard_output
) {
    trace_project_init("initialize: begin");
    bool created_root = false;
    trace_project_init("initialize: before prepare root");
    const auto root = prepare_project_root(arguments.project_root, created_root);
    trace_project_init("initialize: after prepare root");

    try {
        trace_project_init("initialize: before prepare catalog");
        const auto catalog_path = prepare_catalog_path(arguments.catalog_database_path, root);
        trace_project_init("initialize: after prepare catalog");
        infrastructure::sqlite::SqliteConnection catalog_connection{catalog_path};
        trace_project_init("initialize: catalog sqlite opened");
        infrastructure::sqlite::CatalogMigrationRunner catalog_migrations{catalog_connection};
        catalog_migrations.apply_pending();
        trace_project_init("initialize: catalog migrations done");
        infrastructure::sqlite::SqliteProjectRepository projects{catalog_connection};
        infrastructure::UuidV4Generator ids;
        infrastructure::SystemClock clock;
        infrastructure::FilesystemPathCanonicalizer canonicalizer;
        infrastructure::FilesystemProjectWorkspace workspace;
        application::ProjectService service{
            projects, ids, clock, canonicalizer, workspace
        };

        trace_project_init("initialize: before service create");
        const domain::Project project = service.create({
            .name = arguments.project_name,
            .root_path = path_to_utf8(root),
        });
        trace_project_init("initialize: after service create");
        standard_output << "OpenGenesis-BioCore project created: " << project.name() << '\n'
                        << "Project ID: " << project.id() << '\n'
                        << "Project root: " << project.root_path() << '\n'
                        << "Catalog: " << path_to_utf8(catalog_path) << '\n';
        trace_project_init("initialize: returning");
        return project;
    } catch (...) {
        trace_project_init("initialize: caught exception");
        if (created_root) {
            std::error_code ignored;
            static_cast<void>(std::filesystem::remove(root, ignored));
            trace_project_init("initialize: created root cleanup attempted");
        }
        trace_project_init("initialize: rethrowing");
        throw;
    }
}

}  // namespace biocore::bootstrap
