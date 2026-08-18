#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/project_service.hpp"
#include "biocore/application/project_service_error.hpp"
#include "biocore/infrastructure/filesystem_path_canonicalizer.hpp"
#include "biocore/infrastructure/filesystem_project_workspace.hpp"
#include "biocore/infrastructure/sqlite/catalog_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_project_repository.hpp"

namespace {

using biocore::application::CreateProjectRequest;
using biocore::application::DuplicateProjectRootError;
using biocore::application::IIdGenerator;
using biocore::application::IUtcClock;
using biocore::application::ProjectService;
using biocore::infrastructure::FilesystemPathCanonicalizer;
using biocore::infrastructure::FilesystemProjectWorkspace;
using biocore::infrastructure::sqlite::CatalogMigrationRunner;
using biocore::infrastructure::sqlite::SqliteConnection;
using biocore::infrastructure::sqlite::SqliteProjectRepository;

class FixedIdGenerator final : public IIdGenerator {
public:
    [[nodiscard]] std::string generate() override {
        return "project-service-integration";
    }
};

class FixedClock final : public IUtcClock {
public:
    [[nodiscard]] std::string now_utc_iso8601() override {
        return "2026-08-06T20:32:00Z";
    }
};

class TemporaryProjectRoot final {
public:
    TemporaryProjectRoot() {
        const auto unique_value = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("biocore-project-service-integration-" + std::to_string(unique_value));
        std::filesystem::create_directories(path_ / "nested");
    }

    ~TemporaryProjectRoot() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

[[nodiscard]] std::string to_utf8(const std::filesystem::path& path) {
    const std::u8string encoded = path.generic_u8string();
    std::string result;
    result.reserve(encoded.size());
    for (const char8_t character : encoded) {
        result.push_back(static_cast<char>(character));
    }
    return result;
}

}  // namespace

int main() {
    TemporaryProjectRoot project_root;
    SqliteConnection connection{std::filesystem::path{":memory:"}};
    CatalogMigrationRunner migrations{connection};
    migrations.apply_pending();

    SqliteProjectRepository repository{connection};
    FixedIdGenerator ids;
    FixedClock clock;
    FilesystemPathCanonicalizer paths;
    FilesystemProjectWorkspace workspace;
    ProjectService service{repository, ids, clock, paths, workspace};

    const std::filesystem::path alias = project_root.path() / "nested" / "..";
    const auto created = service.create(CreateProjectRequest{"Integrated project", to_utf8(alias)});
    const auto stored = repository.find_by_id(created.id());
    const auto stored_by_root = repository.find_by_root_path(created.root_path());
    if (!stored.has_value() || !stored_by_root.has_value() || stored->root_path() != created.root_path() ||
        stored->created_at_utc() != "2026-08-06T20:32:00Z" ||
        !std::filesystem::is_regular_file(project_root.path() / ".biocore" / "ownership.json") ||
        !std::filesystem::is_regular_file(project_root.path() / ".biocore" / "project.sqlite") ||
        !std::filesystem::is_directory(project_root.path() / "inputs") ||
        !std::filesystem::is_directory(project_root.path() / "work" / "jobs") ||
        !std::filesystem::is_directory(project_root.path() / "outputs") ||
        !std::filesystem::is_directory(project_root.path() / "reports") ||
        !std::filesystem::is_directory(project_root.path() / "logs")) {
        std::cerr << "ProjectService SQLite and workspace integration contract failed\n";
        return EXIT_FAILURE;
    }

    try {
        (void)service.create(CreateProjectRequest{"Alias duplicate", to_utf8(project_root.path())});
    } catch (const DuplicateProjectRootError&) {
        return EXIT_SUCCESS;
    }

    std::cerr << "ProjectService accepted an alias of an existing canonical root\n";
    return EXIT_FAILURE;
}
