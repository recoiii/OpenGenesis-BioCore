#include "biocore/infrastructure/filesystem_project_workspace.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "biocore/application/project_workspace_error.hpp"
#include "biocore/infrastructure/sqlite/project_database_initializer.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"

namespace biocore::infrastructure {
namespace {

using application::IProjectWorkspaceTransaction;
using application::ProjectWorkspaceConflictError;
using application::ProjectWorkspaceInitializationError;

void workspace_probe(const std::string_view stage) {
    std::cerr << "[workspace-initialize-probe] " << stage << std::endl;
}

[[nodiscard]] std::filesystem::path path_from_utf8(const std::string_view value) {
#ifdef _WIN32
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
        encoded.push_back(static_cast<char8_t>(character));
    }
    return std::filesystem::path{encoded};
#else
    return std::filesystem::path{value};
#endif
}

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string encoded = path.generic_u8string();
    std::string result;
    result.reserve(encoded.size());
    for (const char8_t character : encoded) {
        result.push_back(static_cast<char>(character));
    }
    return result;
}

[[nodiscard]] std::string escape_json(const std::string_view value) {
    std::ostringstream escaped;
    escaped << std::hex << std::setfill('0');

    for (const char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
        switch (character) {
            case '"':
                escaped << "\\\"";
                break;
            case '\\':
                escaped << "\\\\";
                break;
            case '\b':
                escaped << "\\b";
                break;
            case '\f':
                escaped << "\\f";
                break;
            case '\n':
                escaped << "\\n";
                break;
            case '\r':
                escaped << "\\r";
                break;
            case '\t':
                escaped << "\\t";
                break;
            default:
                if (character < 0x20U) {
                    escaped << "\\u" << std::setw(4) << static_cast<unsigned int>(character);
                } else {
                    escaped << static_cast<char>(character);
                }
                break;
        }
    }

    return escaped.str();
}

[[nodiscard]] std::string ownership_document(const domain::Project& project) {
    return "{\n"
           "  \"schemaVersion\": " +
           std::to_string(FilesystemProjectWorkspace::ownership_schema_version) +
           ",\n  \"projectId\": \"" + escape_json(project.id()) +
           "\",\n  \"createdAtUtc\": \"" + escape_json(project.created_at_utc()) + "\"\n}\n";
}

[[nodiscard]] bool entry_exists(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::symlink_status(path, error);
    if (error && error != std::errc::no_such_file_or_directory) {
        throw ProjectWorkspaceInitializationError{
            "Unable to inspect reserved project entry '" + path_to_utf8(path) + "': " + error.message()};
    }
    return status.type() != std::filesystem::file_type::not_found;
}

void rollback_paths(std::vector<std::filesystem::path>& created_paths) noexcept {
    for (auto iterator = created_paths.rbegin(); iterator != created_paths.rend(); ++iterator) {
        std::error_code ignored;
        (void)std::filesystem::remove(*iterator, ignored);
    }
    created_paths.clear();
}

void create_directory(
    const std::filesystem::path& path,
    std::vector<std::filesystem::path>& created_paths
) {
    std::error_code error;
    const bool created = std::filesystem::create_directory(path, error);
    if (error) {
        throw ProjectWorkspaceInitializationError{
            "Unable to create project workspace directory '" + path_to_utf8(path) + "': " + error.message()};
    }
    if (!created) {
        throw ProjectWorkspaceConflictError{path.filename().string()};
    }
    created_paths.push_back(path);
}

void initialize_project_database(
    const std::filesystem::path& biocore_directory,
    const domain::Project& project,
    std::vector<std::filesystem::path>& created_paths
) {
    workspace_probe("database: begin");
    const std::filesystem::path database_path = biocore_directory / "project.sqlite";
    std::filesystem::path wal_path = database_path;
    wal_path += "-wal";
    std::filesystem::path shared_memory_path = database_path;
    shared_memory_path += "-shm";

    created_paths.push_back(database_path);
    created_paths.push_back(wal_path);
    created_paths.push_back(shared_memory_path);

    workspace_probe("database: before connection");
    sqlite::SqliteConnection connection{database_path};
    workspace_probe("database: after connection");
    sqlite::ProjectDatabaseInitializer initializer{connection};
    workspace_probe("database: before initializer.initialize");
    initializer.initialize(project);
    workspace_probe("database: after initializer.initialize");
}

void write_ownership_file(
    const std::filesystem::path& biocore_directory,
    const domain::Project& project,
    std::vector<std::filesystem::path>& created_paths
) {
    workspace_probe("ownership: begin");
    const std::filesystem::path temporary_path = biocore_directory / "ownership.json.tmp";
    const std::filesystem::path ownership_path = biocore_directory / "ownership.json";

    workspace_probe("ownership: before open");
    std::ofstream output{temporary_path, std::ios::binary | std::ios::trunc};
    if (!output.is_open()) {
        throw ProjectWorkspaceInitializationError{
            "Unable to create project ownership metadata: " + path_to_utf8(temporary_path)};
    }
    workspace_probe("ownership: after open");
    created_paths.push_back(temporary_path);

    workspace_probe("ownership: before serialize/write");
    output << ownership_document(project);
    workspace_probe("ownership: after serialize/write");
    output.flush();
    workspace_probe("ownership: after flush");
    if (!output) {
        throw ProjectWorkspaceInitializationError{
            "Unable to write project ownership metadata: " + path_to_utf8(temporary_path)};
    }
    output.close();
    workspace_probe("ownership: after close");
    if (!output) {
        throw ProjectWorkspaceInitializationError{
            "Unable to close project ownership metadata: " + path_to_utf8(temporary_path)};
    }

    std::error_code error;
    workspace_probe("ownership: before rename");
    std::filesystem::rename(temporary_path, ownership_path, error);
    workspace_probe("ownership: after rename");
    if (error) {
        throw ProjectWorkspaceInitializationError{
            "Unable to publish project ownership metadata: " + error.message()};
    }
    created_paths.back() = ownership_path;
}

class FilesystemWorkspaceTransaction final : public IProjectWorkspaceTransaction {
public:
    explicit FilesystemWorkspaceTransaction(std::vector<std::filesystem::path> created_paths)
        : created_paths_{std::move(created_paths)} {}

    ~FilesystemWorkspaceTransaction() override {
        if (!committed_) {
            rollback_paths(created_paths_);
        }
    }

    void commit() noexcept override {
        committed_ = true;
        created_paths_.clear();
    }

private:
    std::vector<std::filesystem::path> created_paths_;
    bool committed_{false};
};

}  // namespace

std::unique_ptr<application::IProjectWorkspaceTransaction> FilesystemProjectWorkspace::initialize(
    const domain::Project& project
) {
    workspace_probe("initialize: entry");
    const std::filesystem::path requested_root = path_from_utf8(project.root_path());
    workspace_probe("initialize: root path decoded");
    std::error_code canonical_error;
    workspace_probe("initialize: before canonical");
    const std::filesystem::path root = std::filesystem::canonical(requested_root, canonical_error);
    workspace_probe("initialize: after canonical");
    if (canonical_error) {
        throw ProjectWorkspaceInitializationError{
            "Project root is no longer available: " + canonical_error.message()};
    }
    if (path_to_utf8(root) != project.root_path()) {
        throw ProjectWorkspaceInitializationError{
            "Project workspace initialization requires a canonical root path"};
    }
    if (root.empty() || root == root.root_path()) {
        throw ProjectWorkspaceInitializationError{
            "A filesystem root cannot be initialized as an OpenGenesis-BioCore project workspace"};
    }
    workspace_probe("initialize: canonical root validated");

    std::error_code status_error;
    const std::filesystem::file_status root_status = std::filesystem::symlink_status(root, status_error);
    if (status_error || root_status.type() != std::filesystem::file_type::directory) {
        throw ProjectWorkspaceInitializationError{
            "Project root is no longer an existing non-symlink directory: " + path_to_utf8(root)};
    }
    workspace_probe("initialize: root status validated");

    constexpr std::array<std::string_view, 6> reserved_entries{
        ".biocore", "inputs", "work", "outputs", "reports", "logs"};
    for (const std::string_view entry : reserved_entries) {
        if (entry_exists(root / entry)) {
            throw ProjectWorkspaceConflictError{std::string{entry}};
        }
    }
    workspace_probe("initialize: reserved entries checked");

    std::vector<std::filesystem::path> created_paths;
    created_paths.reserve(15U);

    try {
        const std::filesystem::path biocore_directory = root / ".biocore";
        const std::filesystem::path work_directory = root / "work";

        workspace_probe("initialize: before directory creation");
        create_directory(biocore_directory, created_paths);
        create_directory(biocore_directory / "locks", created_paths);
        create_directory(biocore_directory / "runtime", created_paths);
        create_directory(biocore_directory / "cache", created_paths);
        create_directory(root / "inputs", created_paths);
        create_directory(work_directory, created_paths);
        create_directory(work_directory / "jobs", created_paths);
        create_directory(work_directory / "temporary", created_paths);
        create_directory(root / "outputs", created_paths);
        create_directory(root / "reports", created_paths);
        create_directory(root / "logs", created_paths);
        workspace_probe("initialize: directories created");
        initialize_project_database(biocore_directory, project, created_paths);
        workspace_probe("initialize: database helper returned");
        write_ownership_file(biocore_directory, project, created_paths);
        workspace_probe("initialize: ownership helper returned");
    } catch (...) {
        workspace_probe("initialize: exception, rolling back");
        rollback_paths(created_paths);
        throw;
    }

    workspace_probe("initialize: before transaction return");
    return std::make_unique<FilesystemWorkspaceTransaction>(std::move(created_paths));
}

}  // namespace biocore::infrastructure
