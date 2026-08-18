#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_project_repository.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/project_service.hpp"
#include "biocore/domain/project.hpp"
#include "biocore/infrastructure/filesystem_path_canonicalizer.hpp"
#include "biocore/infrastructure/filesystem_project_workspace.hpp"

namespace {

using biocore::application::CreateProjectRequest;
using biocore::application::IIdGenerator;
using biocore::application::IProjectRepository;
using biocore::application::IUtcClock;
using biocore::application::ProjectService;
using biocore::domain::Project;
using biocore::infrastructure::FilesystemPathCanonicalizer;
using biocore::infrastructure::FilesystemProjectWorkspace;

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto unique_value = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("biocore-workspace-service-integration-" + std::to_string(unique_value));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

class ThrowingRepository final : public IProjectRepository {
public:
    void save(const Project&) override {
        ++save_calls;
        throw std::runtime_error{"simulated persistence failure"};
    }

    [[nodiscard]] std::optional<Project> find_by_id(std::string_view) override {
        return std::nullopt;
    }

    [[nodiscard]] std::optional<Project> find_by_root_path(std::string_view) override {
        return std::nullopt;
    }

    [[nodiscard]] std::vector<Project> list() override {
        return {};
    }

    bool remove(std::string_view) override {
        return false;
    }

    std::size_t save_calls{0U};
};

class FixedIdGenerator final : public IIdGenerator {
public:
    [[nodiscard]] std::string generate() override {
        return "workspace-rollback-integration";
    }
};

class FixedClock final : public IUtcClock {
public:
    [[nodiscard]] std::string now_utc_iso8601() override {
        return "2026-08-06T20:47:00Z";
    }
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

}  // namespace

int main() {
    TemporaryDirectory temporary;
    const std::filesystem::path unrelated = temporary.path() / "keep.txt";
    {
        std::ofstream output{unrelated};
        output << "keep";
    }

    ThrowingRepository repository;
    FixedIdGenerator ids;
    FixedClock clock;
    FilesystemPathCanonicalizer canonicalizer;
    FilesystemProjectWorkspace workspace;
    ProjectService service{repository, ids, clock, canonicalizer, workspace};

    try {
        (void)service.create(CreateProjectRequest{"Rollback integration", to_utf8(temporary.path())});
    } catch (const std::runtime_error&) {
        if (repository.save_calls == 1U && no_reserved_entries_exist(temporary.path()) &&
            std::filesystem::is_regular_file(unrelated)) {
            return EXIT_SUCCESS;
        }
    }

    std::cerr << "ProjectService did not roll back the real filesystem workspace after persistence failure\n";
    return EXIT_FAILURE;
}
