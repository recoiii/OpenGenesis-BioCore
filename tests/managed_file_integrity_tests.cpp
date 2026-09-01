#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/i_input_file_storage.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/storage_mode.hpp"
#include "biocore/infrastructure/filesystem_input_file_storage.hpp"
#include "biocore/infrastructure/sha256.hpp"

namespace {

using biocore::application::ManagedFileIntegrityStatus;
using biocore::domain::ManagedFile;
using biocore::domain::StorageMode;
using biocore::infrastructure::FilesystemInputFileStorage;

class TemporaryTree final {
public:
    TemporaryTree() {
        const auto value = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
               ("biocore-integrity-" + std::to_string(value));
        project = root / "project";
        source = root / "large-input.bin";
        std::filesystem::create_directories(project / "inputs");
        std::filesystem::create_directories(project / ".biocore" / "runtime" / "browser-uploads");
    }
    ~TemporaryTree() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    std::filesystem::path root;
    std::filesystem::path project;
    std::filesystem::path source;
};

[[nodiscard]] std::string utf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

void require(const bool value, const std::string_view message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void write_large_file(const std::filesystem::path& path) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) std::exit(EXIT_FAILURE);
    std::vector<char> block(biocore::infrastructure::file_stream_buffer_bytes, 'A');
    for (int index = 0; index < 96; ++index) {
        block[static_cast<std::size_t>(index) % block.size()] =
            static_cast<char>('A' + (index % 23));
        output.write(block.data(), static_cast<std::streamsize>(block.size()));
    }
    output.write("tail", 4);
    if (!output) std::exit(EXIT_FAILURE);
}

ManagedFile make_file(
    const biocore::application::PreparedManagedCopy& prepared,
    std::optional<std::string> algorithm,
    std::optional<std::string> value
) {
    return ManagedFile{
        "integrity-file", prepared.display_name, StorageMode::managed_copy,
        prepared.original_path, prepared.managed_path, prepared.relative_project_path,
        "binary", prepared.size_bytes, std::nullopt, std::move(algorithm), std::move(value),
        "created", "updated"
    };
}

}  // namespace

int main() {
    static_assert(biocore::infrastructure::file_stream_buffer_bytes == 64U * 1024U);

    TemporaryTree tree;
    write_large_file(tree.source);
    const std::string source_hash = biocore::infrastructure::sha256_file_hex(tree.source);

    FilesystemInputFileStorage storage{utf8(std::filesystem::canonical(tree.project))};
    auto transaction = storage.prepare_managed_copy(utf8(tree.source), "integrity-file");
    const auto prepared = transaction->prepared_file();
    require(prepared.size_bytes > static_cast<std::int64_t>(biocore::infrastructure::file_stream_buffer_bytes),
            "fixture must exceed the streaming buffer");
    require(prepared.checksum_algorithm == std::optional<std::string>{"sha256"},
            "managed import must record SHA-256 algorithm");
    require(prepared.checksum_value == std::optional<std::string>{source_hash},
            "managed streaming copy checksum must match source bytes");
    transaction->commit();
    transaction.reset();

    ManagedFile file = make_file(prepared, prepared.checksum_algorithm, prepared.checksum_value);
    auto verified = storage.verify_managed_file(file);
    require(verified.status == ManagedFileIntegrityStatus::verified,
            "fresh managed input must verify");
    require(verified.observed_size_bytes == std::optional<std::int64_t>{prepared.size_bytes},
            "integrity verification must report observed size");
    require(verified.observed_sha256 == prepared.checksum_value,
            "integrity verification must report observed SHA-256");

    const std::filesystem::path managed{*file.managed_path()};
    {
        std::fstream mutate{managed, std::ios::binary | std::ios::in | std::ios::out};
        require(static_cast<bool>(mutate), "managed file must open for tamper fixture");
        mutate.seekp(17);
        mutate.put('Z');
    }
    const auto mismatched = storage.verify_managed_file(file);
    require(mismatched.status == ManagedFileIntegrityStatus::checksum_mismatch,
            "same-size tamper must be detected by checksum");

    std::filesystem::resize_file(managed, static_cast<std::uintmax_t>(prepared.size_bytes - 1));
    const auto wrong_size = storage.verify_managed_file(file);
    require(wrong_size.status == ManagedFileIntegrityStatus::size_mismatch,
            "truncation must be detected before hashing");

    std::filesystem::remove(managed);
    const auto missing = storage.verify_managed_file(file);
    require(missing.status == ManagedFileIntegrityStatus::file_missing,
            "missing managed input must be reported");

    ManagedFile legacy = make_file(prepared, std::nullopt, std::nullopt);
    write_large_file(managed);
    const auto legacy_result = storage.verify_managed_file(legacy);
    require(legacy_result.status == ManagedFileIntegrityStatus::checksum_unavailable,
            "legacy checksum-less records must remain readable but explicitly unverified");

    ManagedFile unsafe{
        "unsafe-file", "outside.bin", StorageMode::managed_copy,
        utf8(tree.source), utf8(tree.source), std::string{"../large-input.bin"},
        "binary", static_cast<std::int64_t>(std::filesystem::file_size(tree.source)),
        std::nullopt, std::string{"sha256"}, source_hash, "created", "updated"
    };
    const auto unsafe_result = storage.verify_managed_file(unsafe);
    require(unsafe_result.status == ManagedFileIntegrityStatus::unsafe_path,
            "relative path escape must fail closed");

    std::cout << "Managed file integrity and large-file I/O tests passed\n";
    return EXIT_SUCCESS;
}
