#include <sqlite3.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/storage_mode.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"
#include "biocore/infrastructure/sqlite/sqlite_managed_file_repository.hpp"

namespace {

using biocore::domain::ManagedFile;
using biocore::domain::StorageMode;
using biocore::infrastructure::sqlite::ProjectMigrationRunner;
using biocore::infrastructure::sqlite::SqliteConnection;
using biocore::infrastructure::sqlite::SqliteError;
using biocore::infrastructure::sqlite::SqliteManagedFileRepository;

constexpr std::string_view repository_checksum =
    "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";

[[nodiscard]] ManagedFile make_file(std::string id, std::string relative, std::string created) {
    return ManagedFile{
        std::move(id),
        "örnek-'α.fastq",
        StorageMode::managed_copy,
        std::string{"/kaynak/örnek-'α.fastq"},
        std::string{"/proje/"} + relative,
        relative,
        "fastq",
        123,
        std::string{"2026-08-06T21:00:00Z"},
        std::string{"sha256"},
        std::string{repository_checksum},
        created,
        created,
    };
}

[[nodiscard]] bool repository_contract() {
    SqliteConnection connection{std::filesystem::path{":memory:"}};
    ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    SqliteManagedFileRepository repository{connection};

    const ManagedFile second = make_file("file-b", "inputs/file-b/örnek.fastq", "2026-08-06T21:01:00Z");
    const ManagedFile first = make_file("file-'a", "inputs/file-a/örnek.fastq", "2026-08-06T21:00:00Z");
    if (!repository.add(second) || !repository.add(first) || repository.add(first)) return false;

    const auto fetched = repository.find_by_id(first.id());
    const auto by_path = repository.find_by_relative_project_path("inputs/file-a/örnek.fastq");
    if (!fetched.has_value() || !by_path.has_value() || fetched->display_name() != "örnek-'α.fastq" ||
        fetched->size_bytes() != 123 || !fetched->checksum_value().has_value() ||
        *fetched->checksum_value() != repository_checksum || by_path->id() != first.id()) {
        return false;
    }

    const auto files = repository.list();
    if (files.size() != 2U || files[0].id() != first.id() || files[1].id() != second.id()) return false;

    const ManagedFile path_conflict = make_file("different-id", "inputs/file-a/örnek.fastq", "2026-08-06T21:02:00Z");
    return !repository.add(path_conflict) && !repository.find_by_id("missing").has_value();
}

[[nodiscard]] ManagedFile make_mode_file(const StorageMode mode, std::string id) {
    std::optional<std::string> original;
    std::optional<std::string> managed;
    std::optional<std::string> relative;
    switch (mode) {
        case StorageMode::managed_copy:
        case StorageMode::managed_move:
            original = "/source/" + id;
            managed = "/project/inputs/" + id;
            relative = "inputs/" + id;
            break;
        case StorageMode::external_reference:
            original = "/external/" + id;
            break;
        case StorageMode::generated_output:
            managed = "/project/outputs/" + id;
            relative = "outputs/" + id;
            break;
        case StorageMode::temporary:
            managed = "/project/work/temporary/" + id;
            relative = "work/temporary/" + id;
            break;
    }
    const std::optional<std::string> checksum_algorithm =
        mode == StorageMode::generated_output ? std::optional<std::string>{"sha256"} : std::nullopt;
    const std::optional<std::string> checksum_value =
        mode == StorageMode::generated_output ? std::optional<std::string>{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"} : std::nullopt;
    return ManagedFile{
        std::move(id), "file", mode, std::move(original), std::move(managed),
        std::move(relative), "unknown", 0, std::nullopt, checksum_algorithm, checksum_value,
        "2026-08-06T21:10:00Z", "2026-08-06T21:10:00Z"};
}

[[nodiscard]] bool storage_modes_round_trip_contract() {
    SqliteConnection connection{std::filesystem::path{":memory:"}};
    ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    SqliteManagedFileRepository repository{connection};
    constexpr StorageMode modes[]{
        StorageMode::managed_copy,
        StorageMode::external_reference,
        StorageMode::managed_move,
        StorageMode::generated_output,
        StorageMode::temporary,
    };
    for (std::size_t index = 0; index < std::size(modes); ++index) {
        const std::string id = "mode-" + std::to_string(index);
        if (!repository.add(make_mode_file(modes[index], id))) return false;
        const auto stored = repository.find_by_id(id);
        if (!stored.has_value() || stored->storage_mode() != modes[index]) return false;
    }
    return true;
}

[[nodiscard]] bool corruption_contract() {
    SqliteConnection connection{std::filesystem::path{":memory:"}};
    ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    connection.execute("PRAGMA ignore_check_constraints = ON;");
    connection.execute(R"sql(
        INSERT INTO managed_files(
            id, display_name, storage_mode, original_path, file_type, size_bytes,
            created_at_utc, updated_at_utc
        ) VALUES ('corrupt', 'x', 'unsupported', '/x', 'unknown', 1, 'c', 'u');
    )sql");
    connection.execute("PRAGMA ignore_check_constraints = OFF;");
    SqliteManagedFileRepository repository{connection};
    try {
        static_cast<void>(repository.find_by_id("corrupt"));
    } catch (const SqliteError& error) {
        return error.result_code() == SQLITE_MISMATCH;
    }
    return false;
}

[[nodiscard]] bool disk_persistence_contract() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
                           ("biocore-managed-file-db-" + std::to_string(unique));
    std::filesystem::create_directory(directory);
    const auto database = directory / std::filesystem::path{u8"dosyalar.sqlite"};
    bool result = false;
    {
        SqliteConnection connection{database};
        ProjectMigrationRunner migrations{connection};
        migrations.apply_pending();
        SqliteManagedFileRepository repository{connection};
        result = repository.add(make_file("persistent", "inputs/persistent/x", "2026-08-06T21:03:00Z"));
    }
    if (result) {
        SqliteConnection connection{database};
        ProjectMigrationRunner migrations{connection};
        migrations.apply_pending();
        SqliteManagedFileRepository repository{connection};
        result = repository.find_by_id("persistent").has_value();
    }
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    return result;
}

}  // namespace

int main() {
    if (!repository_contract() || !storage_modes_round_trip_contract() ||
        !corruption_contract() || !disk_persistence_contract()) {
        std::cerr << "SqliteManagedFileRepository contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
