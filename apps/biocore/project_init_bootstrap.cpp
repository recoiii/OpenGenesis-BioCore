#include "project_init_bootstrap.hpp"

#include <cwchar>
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

[[nodiscard]] bool native_paths_equal(
    const std::filesystem::path& left,
    const std::filesystem::path& right
) noexcept {
#ifdef _WIN32
    trace_project_init("native paths equal: entry");
    trace_project_init("native paths equal: before left.native");
    const auto& left_native = left.native();
    trace_project_init("native paths equal: after left.native");
    std::cerr << "[project-init-probe] native paths equal: left native length="
              << left_native.size() << '\n' << std::flush;
    trace_project_init("native paths equal: before right.native");
    const auto& right_native = right.native();
    trace_project_init("native paths equal: after right.native");
    std::cerr << "[project-init-probe] native paths equal: right native length="
              << right_native.size() << '\n' << std::flush;
    trace_project_init("native paths equal: before _wcsicmp");
    const int comparison = _wcsicmp(left_native.c_str(), right_native.c_str());
    trace_project_init("native paths equal: after _wcsicmp");
    return comparison == 0;
#else
    return left.native() == right.native();
#endif
}

[[nodiscard]] bool path_is_within(
    const std::filesystem::path& parent,
    const std::filesystem::path& child
) {
#ifdef _WIN32
    const auto& parent_native = parent.native();
    const auto& child_native = child.native();
    if (child_native.size() <= parent_native.size()) return false;
    if (_wcsnicmp(child_native.c_str(), parent_native.c_str(), parent_native.size()) != 0) {
        return false;
    }
    const wchar_t boundary = child_native[parent_native.size()];
    return boundary == L'\\' || boundary == L'/';
#else
    const auto& parent_native = parent.native();
    const auto& child_native = child.native();
    if (child_native.size() <= parent_native.size()) return false;
    if (child_native.compare(0, parent_native.size(), parent_native) != 0) return false;
    return child_native[parent_native.size()] == '/';
#endif
}

[[nodiscard]] std::filesystem::path prepare_project_root(
    const std::filesystem::path& requested,
    bool& created
) {
    trace_project_init("prepare root: entry");
    if (requested.empty()) {
        throw std::invalid_argument("Project root must not be empty");
    }

    std::error_code error;
    trace_project_init("prepare root: before absolute");
    const auto absolute = std::filesystem::absolute(requested, error).lexically_normal();
    trace_project_init("prepare root: after absolute");
    if (error) {
        throw std::invalid_argument("Project root could not be resolved");
    }

    trace_project_init("prepare root: before target status");
    const auto status = std::filesystem::symlink_status(absolute, error);
    trace_project_init("prepare root: after target status");
    if (!error && std::filesystem::exists(status)) {
        trace_project_init("prepare root: existing target branch");
        if (std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status)) {
            throw std::invalid_argument(
                "Project root must be an existing non-symlink directory or a new directory path"
            );
        }
        created = false;
    } else {
        trace_project_init("prepare root: new target branch");
        error.clear();
        const auto parent = absolute.parent_path();
        trace_project_init("prepare root: before parent status");
        const auto parent_status = std::filesystem::symlink_status(parent, error);
        trace_project_init("prepare root: after parent status");
        if (error || !std::filesystem::is_directory(parent_status) ||
            std::filesystem::is_symlink(parent_status)) {
            throw std::invalid_argument(
                "Project root parent must be an existing non-symlink directory"
            );
        }
        trace_project_init("prepare root: before create directory");
        if (!std::filesystem::create_directory(absolute, error) || error) {
            throw std::runtime_error("Unable to create project root directory");
        }
        trace_project_init("prepare root: after create directory");
        created = true;
    }

    trace_project_init("prepare root: before canonical");
    const auto canonical = std::filesystem::canonical(absolute, error);
    trace_project_init("prepare root: after canonical");
    if (error) {
        throw std::invalid_argument("Project root must resolve without aliases");
    }
    trace_project_init("prepare root: before canonical equality");
    if (!native_paths_equal(canonical, absolute)) {
        throw std::invalid_argument("Project root must resolve without aliases");
    }
    trace_project_init("prepare root: after canonical equality");
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
    if (path_is_within(project_root, absolute) || native_paths_equal(absolute, project_root)) {
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
    if (error || !native_paths_equal(canonical_parent, parent)) {
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
