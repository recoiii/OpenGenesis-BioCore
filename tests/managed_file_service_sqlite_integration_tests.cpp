#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_monotonic_clock.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/managed_file_service.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/infrastructure/filesystem_input_file_storage.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_managed_file_repository.hpp"

namespace {

using biocore::application::IIdGenerator;
using biocore::application::IManagedFileRepository;
using biocore::application::IMonotonicClock;
using biocore::application::IUtcClock;
using biocore::application::ManagedFileService;
using biocore::domain::ManagedFile;
using biocore::infrastructure::FilesystemInputFileStorage;
using biocore::infrastructure::sqlite::ProjectMigrationRunner;
using biocore::infrastructure::sqlite::SqliteConnection;
using biocore::infrastructure::sqlite::SqliteManagedFileRepository;

class IDs final : public IIdGenerator {
public:
    explicit IDs(std::deque<std::string> values) : values_{std::move(values)} {}
    std::string generate() override { auto value = std::move(values_.front()); values_.pop_front(); return value; }
private:
    std::deque<std::string> values_;
};
class Clock final : public IUtcClock, public IMonotonicClock {
public:
    std::string now_utc_iso8601() override { return "2026-08-06T21:30:00Z"; }
    std::chrono::steady_clock::time_point now() override { return {}; }
};
class ThrowingRepository final : public IManagedFileRepository {
public:
    bool add(const ManagedFile&) override { throw std::runtime_error("persistence failed"); }
    std::optional<ManagedFile> find_by_id(std::string_view) override { return std::nullopt; }
    std::optional<ManagedFile> find_by_relative_project_path(std::string_view) override { return std::nullopt; }
    std::vector<ManagedFile> list() override { return {}; }
    bool add_generated_output(const ManagedFile&, const biocore::application::GeneratedOutputProvenance&) override { return false; }
    bool add_generated_outputs_batch(
        std::span<const biocore::application::GeneratedOutputArtifact>
    ) override { return false; }
    std::optional<biocore::application::GeneratedOutputArtifact> find_generated_output(std::string_view, std::string_view, std::string_view) override { return std::nullopt; }
    std::vector<biocore::application::GeneratedOutputArtifact> list_generated_outputs(std::string_view) override { return {}; }
};

[[nodiscard]] std::string utf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

void write(const std::filesystem::path& path, std::string_view content) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
}

[[nodiscard]] bool integration_contract() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("biocore-managed-file-integration-" + std::to_string(unique));
    const auto project = root / "project";
    const auto source_a_dir = root / "a";
    const auto source_b_dir = root / "b";
    std::filesystem::create_directories(project / "inputs");
    std::filesystem::create_directories(project / ".biocore");
    std::filesystem::create_directories(source_a_dir);
    std::filesystem::create_directories(source_b_dir);
    const auto source_a = source_a_dir / "same.fastq";
    const auto source_b = source_b_dir / "same.fastq";
    write(source_a, "AAAA\n");
    write(source_b, "CCCC\n");

    bool result = false;
    {
        SqliteConnection connection{project / ".biocore" / "project.sqlite"};
        ProjectMigrationRunner migrations{connection};
        migrations.apply_pending();
        SqliteManagedFileRepository repository{connection};
        FilesystemInputFileStorage storage{utf8(std::filesystem::canonical(project))};
        IDs ids{{"file-a", "file-b"}};
        Clock clock;
        ManagedFileService service{repository, storage, ids, clock, clock};
        const ManagedFile first = service.register_managed_copy({utf8(source_a), "fastq"});
        const ManagedFile second = service.register_managed_copy({utf8(source_b), "fastq"});
        result = first.relative_project_path() != second.relative_project_path() &&
                 std::filesystem::exists(*first.managed_path()) &&
                 std::filesystem::exists(*second.managed_path()) && service.list().size() == 2U;
    }

    if (result) {
        ThrowingRepository repository;
        FilesystemInputFileStorage storage{utf8(std::filesystem::canonical(project))};
        IDs ids{{"rollback-file"}};
        Clock clock;
        ManagedFileService service{repository, storage, ids, clock, clock};
        try {
            static_cast<void>(service.register_managed_copy({utf8(source_a), "fastq"}));
            result = false;
        } catch (const std::runtime_error&) {
            result = !std::filesystem::exists(project / "inputs" / "rollback-file");
        }
    }

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    return result;
}

}  // namespace

int main() {
    if (!integration_contract()) {
        std::cerr << "Managed file service SQLite/filesystem integration failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
