#include <sqlite3.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>

#include "biocore/application/project_workspace_error.hpp"
#include "biocore/domain/project.hpp"
#include "biocore/infrastructure/filesystem_project_workspace.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"

namespace {

using biocore::application::ProjectWorkspaceConflictError;
using biocore::application::ProjectWorkspaceInitializationError;
using biocore::domain::Project;
using biocore::infrastructure::FilesystemProjectWorkspace;
using biocore::infrastructure::sqlite::SqliteConnection;

void trace(const std::string_view message) {
    std::cerr << "[workspace-probe] " << message << '\n' << std::flush;
}

class TemporaryDirectory final {
public:
    explicit TemporaryDirectory(std::string_view suffix = {}) {
        const auto unique_value = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("biocore-workspace-test-" + std::to_string(unique_value) + "-" + std::string{suffix});
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::cerr << "[workspace-probe] temp cleanup begin: " << path_ << '\n' << std::flush;
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
        std::cerr << "[workspace-probe] temp cleanup end: " << path_ << " error=" << ignored.message()
                  << '\n' << std::flush;
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

[[nodiscard]] Project make_project(const std::filesystem::path& root, std::string id = "project-workspace") {
    return Project{
        std::move(id),
        "Workspace project",
        to_utf8(root),
        "2026-08-06T20:47:00Z",
        "2026-08-06T20:47:00Z",
    };
}

[[nodiscard]] bool has_expected_tree(const std::filesystem::path& root) {
    constexpr std::array<std::string_view, 11> directories{
        ".biocore",
        ".biocore/locks",
        ".biocore/runtime",
        ".biocore/cache",
        "inputs",
        "work",
        "work/jobs",
        "work/temporary",
        "outputs",
        "reports",
        "logs",
    };
    for (const std::string_view relative_path : directories) {
        if (!std::filesystem::is_directory(root / relative_path)) {
            return false;
        }
    }
    const std::filesystem::path database_path = root / ".biocore" / "project.sqlite";
    std::filesystem::path wal_path = database_path;
    wal_path += "-wal";
    std::filesystem::path shared_memory_path = database_path;
    shared_memory_path += "-shm";

    return std::filesystem::is_regular_file(root / ".biocore" / "ownership.json") &&
           std::filesystem::is_regular_file(database_path) &&
           !std::filesystem::exists(wal_path) && !std::filesystem::exists(shared_memory_path) &&
           !std::filesystem::exists(root / ".biocore" / "ownership.json.tmp");
}

[[nodiscard]] bool project_database_matches(
    const std::filesystem::path& root,
    const Project& project
) {
    trace("project_database_matches: open");
    SqliteConnection connection{root / ".biocore" / "project.sqlite"};
    trace("project_database_matches: opened");
    sqlite3* const database = connection.native_handle();
    constexpr const char* sql = R"sql(
        SELECT project_id, name, root_path, created_at_utc, updated_at_utc
        FROM project_metadata
        WHERE singleton = 1;
    )sql";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK) {
        return false;
    }
    trace("project_database_matches: prepared");
    const int step_result = sqlite3_step(statement);
    trace("project_database_matches: stepped");
    if (step_result != SQLITE_ROW) {
        sqlite3_finalize(statement);
        return false;
    }

    const auto column_matches = [&](const int column, const std::string_view expected) {
        const auto* value = sqlite3_column_text(statement, column);
        const int size = sqlite3_column_bytes(statement, column);
        return value != nullptr &&
               std::string_view{reinterpret_cast<const char*>(value), static_cast<std::size_t>(size)} == expected;
    };

    const bool matches = column_matches(0, project.id()) && column_matches(1, project.name()) &&
                         column_matches(2, project.root_path()) &&
                         column_matches(3, project.created_at_utc()) &&
                         column_matches(4, project.updated_at_utc());
    const int finalize_result = sqlite3_finalize(statement);
    trace("project_database_matches: finalized");
    return matches && finalize_result == SQLITE_OK;
}

[[nodiscard]] bool no_reserved_entries_exist(const std::filesystem::path& root) {
    constexpr std::array<std::string_view, 6> entries{
        ".biocore", "inputs", "work", "outputs", "reports", "logs"};
    for (const std::string_view entry : entries) {
        if (std::filesystem::exists(root / entry) || std::filesystem::is_symlink(root / entry)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string read_all(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] bool commits_complete_workspace_and_preserves_unrelated_content() {
    trace("commit case: begin");
    TemporaryDirectory temporary{"commit"};
    const std::filesystem::path unrelated = temporary.path() / "research-notes.txt";
    {
        std::ofstream output{unrelated};
        output << "preserve me";
    }

    std::string project_id{"project-"};
    project_id.push_back('"');
    project_id.push_back('\\');
    project_id.push_back('\n');
    project_id.push_back('\x01');

    const Project project = make_project(temporary.path(), project_id);
    FilesystemProjectWorkspace workspace;
    trace("commit case: before initialize");
    auto transaction = workspace.initialize(project);
    trace("commit case: after initialize");
    if (!transaction || !has_expected_tree(temporary.path())) {
        return false;
    }
    trace("commit case: tree verified");
    if (!project_database_matches(temporary.path(), project)) {
        return false;
    }
    trace("commit case: database verified");

    const std::string metadata = read_all(temporary.path() / ".biocore" / "ownership.json");
    const std::string expected =
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"projectId\": \"project-\\\"\\\\\\n\\u0001\",\n"
        "  \"createdAtUtc\": \"2026-08-06T20:47:00Z\"\n"
        "}\n";
    if (metadata != expected) {
        return false;
    }
    trace("commit case: metadata verified");

    transaction->commit();
    trace("commit case: transaction committed");
    transaction.reset();
    trace("commit case: transaction reset");
    const bool result = has_expected_tree(temporary.path()) && std::filesystem::is_regular_file(unrelated) &&
                        read_all(unrelated) == "preserve me";
    trace("commit case: final checks complete");
    return result;
}

[[nodiscard]] bool rolls_back_uncommitted_workspace_only() {
    trace("rollback case: begin");
    TemporaryDirectory temporary{"rollback"};
    const std::filesystem::path unrelated = temporary.path() / "existing-data.tsv";
    {
        std::ofstream output{unrelated};
        output << "existing";
    }

    FilesystemProjectWorkspace workspace;
    {
        auto transaction = workspace.initialize(make_project(temporary.path()));
        if (!transaction || !has_expected_tree(temporary.path())) {
            return false;
        }
    }

    const bool result = no_reserved_entries_exist(temporary.path()) && std::filesystem::is_regular_file(unrelated) &&
                        read_all(unrelated) == "existing";
    trace("rollback case: final checks complete");
    return result;
}

[[nodiscard]] bool rollback_preserves_untracked_content() {
    trace("untracked case: begin");
    TemporaryDirectory temporary{"untracked"};
    FilesystemProjectWorkspace workspace;
    auto transaction = workspace.initialize(make_project(temporary.path()));
    if (!transaction) {
        return false;
    }

    const std::filesystem::path untracked = temporary.path() / "work" / "external-result.txt";
    {
        std::ofstream output{untracked};
        output << "do not delete";
    }

    transaction.reset();
    const bool result = std::filesystem::is_regular_file(untracked) && read_all(untracked) == "do not delete" &&
                        !std::filesystem::exists(temporary.path() / ".biocore") &&
                        !std::filesystem::exists(temporary.path() / "inputs") &&
                        !std::filesystem::exists(temporary.path() / "outputs") &&
                        !std::filesystem::exists(temporary.path() / "reports") &&
                        !std::filesystem::exists(temporary.path() / "logs");
    trace("untracked case: final checks complete");
    return result;
}

[[nodiscard]] bool rejects_every_reserved_top_level_entry_without_partial_creation() {
    trace("reserved-entry case: begin");
    constexpr std::array<std::string_view, 6> entries{
        ".biocore", "inputs", "work", "outputs", "reports", "logs"};

    for (const std::string_view entry : entries) {
        TemporaryDirectory temporary{entry == ".biocore" ? "hidden" : entry};
        std::filesystem::create_directory(temporary.path() / entry);

        FilesystemProjectWorkspace workspace;
        try {
            (void)workspace.initialize(make_project(temporary.path()));
        } catch (const ProjectWorkspaceConflictError&) {
            if (!std::filesystem::is_directory(temporary.path() / entry)) {
                return false;
            }
            for (const std::string_view other : entries) {
                if (other != entry && (std::filesystem::exists(temporary.path() / other) ||
                                       std::filesystem::is_symlink(temporary.path() / other))) {
                    return false;
                }
            }
            continue;
        }
        return false;
    }
    trace("reserved-entry case: complete");
    return true;
}

[[nodiscard]] bool rejects_missing_or_symlink_project_roots() {
    trace("invalid-root case: begin");
    TemporaryDirectory temporary{"invalid-root"};
    FilesystemProjectWorkspace workspace;

    try {
        (void)workspace.initialize(make_project(temporary.path() / "missing"));
        return false;
    } catch (const ProjectWorkspaceInitializationError&) {
    }

    try {
        (void)workspace.initialize(make_project(temporary.path().root_path()));
        return false;
    } catch (const ProjectWorkspaceInitializationError&) {
    }

    std::filesystem::create_directory(temporary.path() / "nested");
    try {
        (void)workspace.initialize(make_project(temporary.path() / "nested" / ".."));
        return false;
    } catch (const ProjectWorkspaceInitializationError&) {
    }

#ifndef _WIN32
    const std::filesystem::path target = temporary.path() / "target";
    const std::filesystem::path link = temporary.path() / "root-link";
    std::filesystem::create_directory(target);
    std::error_code error;
    std::filesystem::create_directory_symlink(target, link, error);
    if (!error) {
        try {
            (void)workspace.initialize(make_project(link));
            return false;
        } catch (const ProjectWorkspaceInitializationError&) {
        }
    }
#endif

    trace("invalid-root case: complete");
    return true;
}

}  // namespace

int main() {
    trace("test main: start commit case");
    if (!commits_complete_workspace_and_preserves_unrelated_content()) {
        std::cerr << "Committed filesystem workspace contract failed\n";
        return EXIT_FAILURE;
    }
    trace("test main: commit case passed");
    if (!rolls_back_uncommitted_workspace_only()) {
        std::cerr << "Filesystem workspace rollback contract failed\n";
        return EXIT_FAILURE;
    }
    trace("test main: rollback case passed");
    if (!rollback_preserves_untracked_content()) {
        std::cerr << "Filesystem workspace rollback deleted untracked content\n";
        return EXIT_FAILURE;
    }
    trace("test main: untracked case passed");
    if (!rejects_every_reserved_top_level_entry_without_partial_creation()) {
        std::cerr << "Filesystem workspace reserved-entry conflict contract failed\n";
        return EXIT_FAILURE;
    }
    trace("test main: reserved-entry case passed");
    if (!rejects_missing_or_symlink_project_roots()) {
        std::cerr << "Filesystem workspace root validation contract failed\n";
        return EXIT_FAILURE;
    }
    trace("test main: all cases passed");
    return EXIT_SUCCESS;
}
