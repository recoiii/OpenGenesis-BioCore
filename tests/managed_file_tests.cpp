#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/storage_mode.hpp"

namespace {

using biocore::domain::ManagedFile;
using biocore::domain::StorageMode;

[[nodiscard]] ManagedFile make_file(
    StorageMode mode = StorageMode::managed_copy,
    std::optional<std::string> original = std::string{"/source/input.fastq"},
    std::optional<std::string> managed = std::string{"/project/inputs/id/input.fastq"},
    std::optional<std::string> relative = std::string{"inputs/id/input.fastq"},
    std::optional<std::string> algorithm = std::nullopt,
    std::optional<std::string> checksum = std::nullopt
) {
    return ManagedFile{
        "file-1",
        "input.fastq",
        mode,
        std::move(original),
        std::move(managed),
        std::move(relative),
        "fastq",
        42,
        std::nullopt,
        std::move(algorithm),
        std::move(checksum),
        "2026-08-06T20:00:00Z",
        "2026-08-06T20:00:00Z",
    };
}

[[nodiscard]] bool valid_contract() {
    const ManagedFile file = make_file(
        StorageMode::managed_copy,
        std::string{"/kaynak/örnek.fastq"},
        std::string{"/proje/inputs/file-1/örnek.fastq"},
        std::string{"inputs/file-1/örnek.fastq"},
        std::string{"sha256"},
        std::string{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}
    );
    return file.id() == "file-1" && file.display_name() == "input.fastq" &&
           file.storage_mode() == StorageMode::managed_copy && file.size_bytes() == 42 &&
           file.original_path().has_value() && file.managed_path().has_value() &&
           file.relative_project_path().has_value() && file.file_type() == "fastq" &&
           file.checksum_algorithm().has_value() && file.checksum_value().has_value();
}

template <typename Factory>
[[nodiscard]] bool throws_invalid_argument(Factory&& factory) {
    try {
        static_cast<void>(factory());
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

[[nodiscard]] bool invalid_contract() {
    return throws_invalid_argument([] { return ManagedFile{"", "x", StorageMode::external_reference, "/x", std::nullopt, std::nullopt, "unknown", 0, std::nullopt, std::nullopt, std::nullopt, "c", "u"}; }) &&
           throws_invalid_argument([] { return ManagedFile{"id", "   ", StorageMode::external_reference, "/x", std::nullopt, std::nullopt, "unknown", 0, std::nullopt, std::nullopt, std::nullopt, "c", "u"}; }) &&
           throws_invalid_argument([] { return ManagedFile{"id", "x", StorageMode::managed_copy, "/x", std::nullopt, "inputs/x", "fastq", 1, std::nullopt, std::nullopt, std::nullopt, "c", "u"}; }) &&
           throws_invalid_argument([] { return ManagedFile{"id", "x", StorageMode::external_reference, std::nullopt, std::nullopt, std::nullopt, "unknown", 1, std::nullopt, std::nullopt, std::nullopt, "c", "u"}; }) &&
           throws_invalid_argument([] { return ManagedFile{"id", "x", StorageMode::generated_output, std::nullopt, std::nullopt, std::nullopt, "report", 1, std::nullopt, std::nullopt, std::nullopt, "c", "u"}; }) &&
           throws_invalid_argument([] { return ManagedFile{"id", "x", StorageMode::external_reference, "/x", std::nullopt, std::nullopt, "unknown", -1, std::nullopt, std::nullopt, std::nullopt, "c", "u"}; }) &&
           throws_invalid_argument([] { return make_file(StorageMode::managed_copy, "/x", "/y", "inputs/y", "sha256", std::nullopt); }) &&
           throws_invalid_argument([] { return make_file(StorageMode::managed_copy, "/x", "/y", "inputs/y", std::nullopt, "abc"); }) &&
           throws_invalid_argument([] { return make_file(StorageMode::generated_output, std::nullopt, "/project/outputs/x", "outputs/x", "sha256", "ABC"); });
}

[[nodiscard]] bool storage_mode_contract() {
    constexpr StorageMode modes[]{
        StorageMode::managed_copy,
        StorageMode::external_reference,
        StorageMode::managed_move,
        StorageMode::generated_output,
        StorageMode::temporary,
    };
    constexpr std::string_view names[]{
        "managed_copy", "external_reference", "managed_move", "generated_output", "temporary"};
    for (std::size_t index = 0; index < std::size(modes); ++index) {
        if (biocore::domain::to_string(modes[index]) != names[index] ||
            biocore::domain::storage_mode_from_string(names[index]) != modes[index]) {
            return false;
        }
    }
    return !biocore::domain::storage_mode_from_string("unsupported").has_value();
}

}  // namespace

int main() {
    if (!valid_contract() || !invalid_contract() || !storage_mode_contract()) {
        std::cerr << "ManagedFile domain contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
