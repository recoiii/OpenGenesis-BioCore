from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text, encoding="utf-8")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one replacement, found {count}\nANCHOR:\n{old[:300]}")
    write(path, text.replace(old, new, 1))


# 1) Application storage contract: durable checksum evidence + integrity inspection.
write("src/application/include/biocore/application/i_input_file_storage.hpp", r'''#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "biocore/domain/managed_file.hpp"

namespace biocore::application {

struct PreparedManagedCopy final {
    std::string display_name;
    std::string original_path;
    std::string managed_path;
    std::string relative_project_path;
    std::int64_t size_bytes;
    std::optional<std::string> checksum_algorithm{};
    std::optional<std::string> checksum_value{};
};

enum class ManagedFileIntegrityStatus {
    verified,
    checksum_unavailable,
    file_missing,
    unsafe_path,
    size_mismatch,
    checksum_mismatch,
    changed_during_verification,
};

[[nodiscard]] constexpr std::string_view to_string(
    const ManagedFileIntegrityStatus status
) noexcept {
    switch (status) {
        case ManagedFileIntegrityStatus::verified: return "verified";
        case ManagedFileIntegrityStatus::checksum_unavailable: return "checksum_unavailable";
        case ManagedFileIntegrityStatus::file_missing: return "file_missing";
        case ManagedFileIntegrityStatus::unsafe_path: return "unsafe_path";
        case ManagedFileIntegrityStatus::size_mismatch: return "size_mismatch";
        case ManagedFileIntegrityStatus::checksum_mismatch: return "checksum_mismatch";
        case ManagedFileIntegrityStatus::changed_during_verification:
            return "changed_during_verification";
    }
    return "checksum_unavailable";
}

struct ManagedFileIntegrityResult final {
    ManagedFileIntegrityStatus status{ManagedFileIntegrityStatus::checksum_unavailable};
    std::int64_t expected_size_bytes{0};
    std::optional<std::int64_t> observed_size_bytes{};
    std::optional<std::string> expected_sha256{};
    std::optional<std::string> observed_sha256{};
};

class IInputFileImportTransaction {
public:
    virtual ~IInputFileImportTransaction() = default;

    [[nodiscard]] virtual const PreparedManagedCopy& prepared_file() const noexcept = 0;
    virtual void commit() noexcept = 0;
};

class IInputFileStorage {
public:
    virtual ~IInputFileStorage() = default;

    [[nodiscard]] virtual std::unique_ptr<IInputFileImportTransaction> prepare_managed_copy(
        std::string_view source_path,
        std::string_view managed_file_id
    ) = 0;

    [[nodiscard]] virtual bool begin_browser_upload(
        std::string_view upload_id,
        std::string_view display_name
    ) = 0;

    [[nodiscard]] virtual std::uint64_t append_browser_upload(
        std::string_view upload_id,
        std::uint64_t expected_offset,
        std::string_view bytes
    ) = 0;

    [[nodiscard]] virtual std::unique_ptr<IInputFileImportTransaction>
    prepare_browser_upload_commit(
        std::string_view upload_id,
        std::string_view managed_file_id
    ) = 0;

    virtual void discard_browser_upload(std::string_view upload_id) noexcept = 0;

    [[nodiscard]] virtual ManagedFileIntegrityResult verify_managed_file(
        const domain::ManagedFile& file
    ) const {
        ManagedFileIntegrityResult result{
            .status = ManagedFileIntegrityStatus::checksum_unavailable,
            .expected_size_bytes = file.size_bytes(),
        };
        if (file.checksum_algorithm().has_value() && file.checksum_value().has_value() &&
            *file.checksum_algorithm() == "sha256") {
            result.expected_sha256 = *file.checksum_value();
        }
        return result;
    }
};

}  // namespace biocore::application
''')

# 2) Make bounded-memory SHA/file-copy behavior an explicit public invariant.
write("src/infrastructure/include/biocore/infrastructure/sha256.hpp", r'''#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace biocore::infrastructure {

inline constexpr std::size_t file_stream_buffer_bytes = 64U * 1024U;

struct FileCopySha256Result final {
    std::uint64_t bytes_copied{0U};
    std::string sha256;
};

[[nodiscard]] std::string sha256_hex(std::span<const std::byte> bytes);
[[nodiscard]] std::string sha256_file_hex(const std::filesystem::path& path);
[[nodiscard]] FileCopySha256Result copy_file_with_sha256(
    const std::filesystem::path& source,
    const std::filesystem::path& destination
);

}  // namespace biocore::infrastructure
''')

replace_once(
    "src/infrastructure/source/sha256.cpp",
    '#include <fstream>\n#include <stdexcept>\n#include <string>\n',
    '#include <fstream>\n#include <limits>\n#include <stdexcept>\n#include <string>\n'
)
replace_once(
    "src/infrastructure/source/sha256.cpp",
    '    std::array<char, 64U * 1024U> buffer{};\n',
    '    std::array<char, file_stream_buffer_bytes> buffer{};\n'
)
replace_once(
    "src/infrastructure/source/sha256.cpp",
    '''    return digest_to_hex(sha.finish());\n}\n\n}  // namespace biocore::infrastructure\n''',
    '''    return digest_to_hex(sha.finish());\n}\n\nFileCopySha256Result copy_file_with_sha256(\n    const std::filesystem::path& source,\n    const std::filesystem::path& destination\n) {\n    if (source == destination) {\n        throw std::invalid_argument("SHA-256 copy source and destination must differ");\n    }\n\n    std::ifstream input{source, std::ios::binary};\n    if (!input.is_open()) {\n        throw std::runtime_error("Unable to open source file for managed streaming copy");\n    }\n    std::ofstream output{destination, std::ios::binary | std::ios::trunc};\n    if (!output.is_open()) {\n        throw std::runtime_error("Unable to open destination file for managed streaming copy");\n    }\n\n    Sha256 sha;\n    std::array<char, file_stream_buffer_bytes> buffer{};\n    std::uint64_t total = 0U;\n    while (input) {\n        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));\n        const std::streamsize count = input.gcount();\n        if (count <= 0) continue;\n        const auto unsigned_count = static_cast<std::uint64_t>(count);\n        if (unsigned_count > std::numeric_limits<std::uint64_t>::max() - total) {\n            throw std::overflow_error("Managed streaming copy size overflow");\n        }\n        output.write(buffer.data(), count);\n        if (!output) {\n            throw std::runtime_error("Unable to write managed streaming copy");\n        }\n        sha.update(std::span<const std::byte>{\n            reinterpret_cast<const std::byte*>(buffer.data()),\n            static_cast<std::size_t>(count),\n        });\n        total += unsigned_count;\n    }\n    if (!input.eof()) {\n        throw std::runtime_error("Unable to read source during managed streaming copy");\n    }\n    output.flush();\n    if (!output) {\n        throw std::runtime_error("Unable to flush managed streaming copy");\n    }\n    output.close();\n    if (output.fail()) {\n        throw std::runtime_error("Unable to close managed streaming copy");\n    }\n\n    return FileCopySha256Result{\n        .bytes_copied = total,\n        .sha256 = digest_to_hex(sha.finish()),\n    };\n}\n\n}  // namespace biocore::infrastructure\n'''
)

# 3) Filesystem input storage records and verifies SHA-256 without loading whole files.
replace_once(
    "src/infrastructure/include/biocore/infrastructure/filesystem_input_file_storage.hpp",
    '''    void discard_browser_upload(std::string_view upload_id) noexcept override;\n\nprivate:\n''',
    '''    void discard_browser_upload(std::string_view upload_id) noexcept override;\n\n    [[nodiscard]] application::ManagedFileIntegrityResult verify_managed_file(\n        const domain::ManagedFile& file\n    ) const override;\n\nprivate:\n'''
)
replace_once(
    "src/infrastructure/source/filesystem_input_file_storage.cpp",
    '#include <vector>\n\nnamespace biocore::infrastructure {\n',
    '#include <vector>\n\n#include "biocore/domain/storage_mode.hpp"\n#include "biocore/infrastructure/sha256.hpp"\n\nnamespace biocore::infrastructure {\n'
)
replace_once(
    "src/infrastructure/source/filesystem_input_file_storage.cpp",
    '''        created_paths.push_back(temporary_path);\n        std::error_code copy_error;\n        const bool copied = std::filesystem::copy_file(\n            source,\n            temporary_path,\n            std::filesystem::copy_options::none,\n            copy_error\n        );\n        if (copy_error || !copied) {\n            throw std::runtime_error(\n                "Unable to copy input file: " + copy_error.message()\n            );\n        }\n\n''',
    '''        created_paths.push_back(temporary_path);\n        const FileCopySha256Result copy = copy_file_with_sha256(source, temporary_path);\n        if (copy.bytes_copied != size_before) {\n            throw std::runtime_error(\n                "Managed streaming copy byte count does not match the source size"\n            );\n        }\n\n'''
)
replace_once(
    "src/infrastructure/source/filesystem_input_file_storage.cpp",
    '''            .relative_project_path = path_to_utf8(relative),\n            .size_bytes = static_cast<std::int64_t>(size_before),\n        };\n''',
    '''            .relative_project_path = path_to_utf8(relative),\n            .size_bytes = static_cast<std::int64_t>(size_before),\n            .checksum_algorithm = std::string{"sha256"},\n            .checksum_value = copy.sha256,\n        };\n'''
)
replace_once(
    "src/infrastructure/source/filesystem_input_file_storage.cpp",
    '''    if (size_error ||\n        size > static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max())) {\n        throw std::runtime_error("Browser upload has an unsupported final size");\n    }\n\n    const std::filesystem::path destination_directory =\n''',
    '''    if (size_error ||\n        size > static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max())) {\n        throw std::runtime_error("Browser upload has an unsupported final size");\n    }\n    std::error_code timestamp_error;\n    const auto modified_before = std::filesystem::last_write_time(staged_path, timestamp_error);\n    if (timestamp_error) {\n        throw std::runtime_error("Unable to inspect browser upload modification time");\n    }\n    const std::string checksum = sha256_file_hex(staged_path);\n    size_error.clear();\n    const std::uintmax_t size_after_hash = std::filesystem::file_size(staged_path, size_error);\n    timestamp_error.clear();\n    const auto modified_after = std::filesystem::last_write_time(staged_path, timestamp_error);\n    if (size_error || timestamp_error || size_after_hash != size || modified_after != modified_before) {\n        throw std::runtime_error("Browser upload changed while SHA-256 was being calculated");\n    }\n\n    const std::filesystem::path destination_directory =\n'''
)
replace_once(
    "src/infrastructure/source/filesystem_input_file_storage.cpp",
    '''        .relative_project_path = path_to_utf8(relative),\n        .size_bytes = static_cast<std::int64_t>(size),\n    };\n''',
    '''        .relative_project_path = path_to_utf8(relative),\n        .size_bytes = static_cast<std::int64_t>(size),\n        .checksum_algorithm = std::string{"sha256"},\n        .checksum_value = checksum,\n    };\n'''
)
replace_once(
    "src/infrastructure/source/filesystem_input_file_storage.cpp",
    '''void FilesystemInputFileStorage::discard_browser_upload(\n    const std::string_view upload_id\n) noexcept {\n''',
    r'''application::ManagedFileIntegrityResult FilesystemInputFileStorage::verify_managed_file(
    const domain::ManagedFile& file
) const {
    application::ManagedFileIntegrityResult result{
        .status = application::ManagedFileIntegrityStatus::checksum_unavailable,
        .expected_size_bytes = file.size_bytes(),
    };
    if (file.checksum_algorithm().has_value() && file.checksum_value().has_value() &&
        *file.checksum_algorithm() == "sha256") {
        result.expected_sha256 = *file.checksum_value();
    }

    if (file.storage_mode() != domain::StorageMode::managed_copy &&
        file.storage_mode() != domain::StorageMode::managed_move) {
        return result;
    }
    if (!file.relative_project_path().has_value() || !file.managed_path().has_value()) {
        result.status = application::ManagedFileIntegrityStatus::unsafe_path;
        return result;
    }

    const std::filesystem::path relative = path_from_utf8(*file.relative_project_path());
    if (relative.empty() || relative.is_absolute() || relative.has_root_path()) {
        result.status = application::ManagedFileIntegrityStatus::unsafe_path;
        return result;
    }
    for (const auto& component : relative) {
        if (component == "." || component == "..") {
            result.status = application::ManagedFileIntegrityStatus::unsafe_path;
            return result;
        }
    }

    const std::filesystem::path candidate = project_root_ / relative;
    if (candidate.lexically_normal() != candidate ||
        path_from_utf8(*file.managed_path()) != candidate) {
        result.status = application::ManagedFileIntegrityStatus::unsafe_path;
        return result;
    }

    std::error_code status_error;
    const auto status = std::filesystem::symlink_status(candidate, status_error);
    if (status_error == std::errc::no_such_file_or_directory ||
        (!status_error && status.type() == std::filesystem::file_type::not_found)) {
        result.status = application::ManagedFileIntegrityStatus::file_missing;
        return result;
    }
    if (status_error || status.type() != std::filesystem::file_type::regular) {
        result.status = application::ManagedFileIntegrityStatus::unsafe_path;
        return result;
    }
    std::error_code canonical_error;
    const auto canonical = std::filesystem::canonical(candidate, canonical_error);
    if (canonical_error || canonical != candidate) {
        result.status = application::ManagedFileIntegrityStatus::unsafe_path;
        return result;
    }

    std::error_code size_error;
    const std::uintmax_t size_before = std::filesystem::file_size(candidate, size_error);
    if (size_error ||
        size_before > static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max())) {
        result.status = application::ManagedFileIntegrityStatus::size_mismatch;
        return result;
    }
    result.observed_size_bytes = static_cast<std::int64_t>(size_before);
    if (*result.observed_size_bytes != file.size_bytes()) {
        result.status = application::ManagedFileIntegrityStatus::size_mismatch;
        return result;
    }
    if (!result.expected_sha256.has_value()) {
        result.status = application::ManagedFileIntegrityStatus::checksum_unavailable;
        return result;
    }

    std::error_code timestamp_error;
    const auto modified_before = std::filesystem::last_write_time(candidate, timestamp_error);
    if (timestamp_error) {
        throw std::runtime_error("Unable to inspect managed file modification time");
    }
    result.observed_sha256 = sha256_file_hex(candidate);
    size_error.clear();
    const std::uintmax_t size_after = std::filesystem::file_size(candidate, size_error);
    timestamp_error.clear();
    const auto modified_after = std::filesystem::last_write_time(candidate, timestamp_error);
    if (size_error || timestamp_error || size_after != size_before || modified_after != modified_before) {
        result.status = application::ManagedFileIntegrityStatus::changed_during_verification;
        return result;
    }

    result.status = *result.observed_sha256 == *result.expected_sha256
        ? application::ManagedFileIntegrityStatus::verified
        : application::ManagedFileIntegrityStatus::checksum_mismatch;
    return result;
}

void FilesystemInputFileStorage::discard_browser_upload(
    const std::string_view upload_id
) noexcept {
'''
)

# 4) Persist checksum evidence and expose verification from the managed-file service.
replace_once(
    "src/application/include/biocore/application/managed_file_service.hpp",
    '''    [[nodiscard]] std::optional<domain::ManagedFile> find_by_id(std::string_view id);\n\n    [[nodiscard]] ManagedFileUploadSession begin_upload(\n''',
    '''    [[nodiscard]] std::optional<domain::ManagedFile> find_by_id(std::string_view id);\n    [[nodiscard]] std::optional<ManagedFileIntegrityResult> verify_integrity(\n        std::string_view id\n    );\n\n    [[nodiscard]] ManagedFileUploadSession begin_upload(\n'''
)
replace_once(
    "src/application/source/managed_file_service.cpp",
    '''        prepared.size_bytes,\n        std::nullopt,\n        std::nullopt,\n        std::nullopt,\n        timestamp,\n''',
    '''        prepared.size_bytes,\n        std::nullopt,\n        prepared.checksum_algorithm,\n        prepared.checksum_value,\n        timestamp,\n'''
)
replace_once(
    "src/application/source/managed_file_service.cpp",
    '''std::optional<domain::ManagedFile> ManagedFileService::find_by_id(\n    const std::string_view id\n) {\n    return repository_.find_by_id(id);\n}\n\nManagedFileUploadSession ManagedFileService::begin_upload(\n''',
    '''std::optional<domain::ManagedFile> ManagedFileService::find_by_id(\n    const std::string_view id\n) {\n    return repository_.find_by_id(id);\n}\n\nstd::optional<ManagedFileIntegrityResult> ManagedFileService::verify_integrity(\n    const std::string_view id\n) {\n    const auto file = repository_.find_by_id(id);\n    if (!file.has_value() || file->storage_mode() != domain::StorageMode::managed_copy) {\n        return std::nullopt;\n    }\n    return input_storage_.verify_managed_file(*file);\n}\n\nManagedFileUploadSession ManagedFileService::begin_upload(\n'''
)

# 5) Managed-file domain validates SHA-256 shape for managed inputs when evidence exists;
# legacy checksum-less records remain readable.
replace_once(
    "src/domain/source/managed_file.cpp",
    '''    if (storage_mode_ == StorageMode::generated_output && checksum_algorithm_.has_value() &&\n        (*checksum_algorithm_ != "sha256" || !is_lower_sha256(*checksum_value_))) {\n        throw std::invalid_argument("Generated output checksum must be lowercase SHA-256");\n    }\n''',
    '''    if ((storage_mode_ == StorageMode::managed_copy ||\n         storage_mode_ == StorageMode::managed_move ||\n         storage_mode_ == StorageMode::generated_output) &&\n        checksum_algorithm_.has_value() &&\n        (*checksum_algorithm_ != "sha256" || !is_lower_sha256(*checksum_value_))) {\n        throw std::invalid_argument("Managed file checksum must be lowercase SHA-256");\n    }\n'''
)

# 6) Local API exposes durable checksum evidence and on-demand verification.
replace_once(
    "src/presentation/source/local_api.cpp",
    '''           ",\\\"fileType\\\":" + quote(file.file_type()) +\n           ",\\\"sizeBytes\\\":" + std::to_string(file.size_bytes()) +\n           ",\\\"createdAtUtc\\\":" + quote(file.created_at_utc()) + "}";\n}\n\n[[nodiscard]] std::string render_managed_inputs(\n''',
    '''           ",\\\"fileType\\\":" + quote(file.file_type()) +\n           ",\\\"sizeBytes\\\":" + std::to_string(file.size_bytes()) +\n           ",\\\"checksumAlgorithm\\\":" + optional_json(file.checksum_algorithm()) +\n           ",\\\"checksumValue\\\":" + optional_json(file.checksum_value()) +\n           ",\\\"createdAtUtc\\\":" + quote(file.created_at_utc()) + "}";\n}\n\n[[nodiscard]] std::string render_integrity(\n    const application::ManagedFileIntegrityResult& result\n) {\n    return "{" + std::string{"\\\"status\\\":"} + quote(application::to_string(result.status)) +\n           ",\\\"expectedSizeBytes\\\":" + std::to_string(result.expected_size_bytes) +\n           ",\\\"observedSizeBytes\\\":" +\n           (result.observed_size_bytes.has_value()\n                ? std::to_string(*result.observed_size_bytes) : "null") +\n           ",\\\"expectedSha256\\\":" + optional_json(result.expected_sha256) +\n           ",\\\"observedSha256\\\":" + optional_json(result.observed_sha256) + "}";\n}\n\n[[nodiscard]] std::string render_managed_inputs(\n'''
)
replace_once(
    "src/presentation/source/local_api.cpp",
    '''if (path.size() == 4U && path[2] == "files" && safe_path_atom(path[3]) &&\n    request.method == HttpMethod::get) {\n    const auto file = managed_files_.find_by_id(path[3]);\n''',
    '''if (path.size() == 5U && path[2] == "files" && safe_path_atom(path[3]) &&\n    path[4] == "integrity" && request.method == HttpMethod::get) {\n    const auto integrity = managed_files_.verify_integrity(path[3]);\n    if (!integrity.has_value()) {\n        return error_response(404, "managed_file_not_found", "Managed input file was not found");\n    }\n    return json_response(200, render_integrity(*integrity));\n}\nif (path.size() == 4U && path[2] == "files" && safe_path_atom(path[3]) &&\n    request.method == HttpMethod::get) {\n    const auto file = managed_files_.find_by_id(path[3]);\n'''
)

# 7) Existing tests gain checksum expectations without changing legacy-fixture semantics.
for anchor in [
    '''            .relative_project_path = "inputs/" + std::string{id} + "/örnek.fastq",\n            .size_bytes = 12,\n''',
    '''            .relative_project_path = "inputs/" + std::string{id} + "/" +\n                iterator->second.display_name,\n            .size_bytes = static_cast<std::int64_t>(iterator->second.bytes.size()) +\n                reported_upload_size_delta,\n'''
]:
    if "örnek.fastq" in anchor:
        replacement = anchor + '''            .checksum_algorithm = std::string{"sha256"},\n            .checksum_value = std::string(64U, 'a'),\n'''
    else:
        replacement = anchor + '''            .checksum_algorithm = std::string{"sha256"},\n            .checksum_value = std::string(64U, 'b'),\n'''
    replace_once("tests/managed_file_service_tests.cpp", anchor, replacement)
replace_once(
    "tests/managed_file_service_tests.cpp",
    '''    return file.id() == "file-1" && file.display_name() == "örnek.fastq" &&\n           file.size_bytes() == 12 && file.created_at_utc() == "2026-08-06T21:00:00Z" &&\n''',
    '''    return file.id() == "file-1" && file.display_name() == "örnek.fastq" &&\n           file.size_bytes() == 12 && file.checksum_algorithm() == std::optional<std::string>{"sha256"} &&\n           file.checksum_value() == std::optional<std::string>{std::string(64U, 'a')} &&\n           file.created_at_utc() == "2026-08-06T21:00:00Z" &&\n'''
)
replace_once(
    "tests/managed_file_service_tests.cpp",
    '''    return file.id() == "file-1" && file.display_name() == "genome.fa" &&\n           file.file_type() == "fasta" && file.size_bytes() == 4 &&\n''',
    '''    return file.id() == "file-1" && file.display_name() == "genome.fa" &&\n           file.file_type() == "fasta" && file.size_bytes() == 4 &&\n           file.checksum_algorithm() == std::optional<std::string>{"sha256"} &&\n           file.checksum_value() == std::optional<std::string>{std::string(64U, 'b')} &&\n'''
)

replace_once(
    "tests/filesystem_input_file_storage_tests.cpp",
    '#include "biocore/infrastructure/filesystem_input_file_storage.hpp"\n',
    '#include "biocore/infrastructure/filesystem_input_file_storage.hpp"\n#include "biocore/infrastructure/sha256.hpp"\n'
)
replace_once(
    "tests/filesystem_input_file_storage_tests.cpp",
    '''        first_prepared.relative_project_path != "inputs/file-1/örnek.fastq" ||\n        first_prepared.size_bytes != static_cast<std::int64_t>(first_bytes.size())) {\n''',
    '''        first_prepared.relative_project_path != "inputs/file-1/örnek.fastq" ||\n        first_prepared.size_bytes != static_cast<std::int64_t>(first_bytes.size()) ||\n        first_prepared.checksum_algorithm != std::optional<std::string>{"sha256"} ||\n        first_prepared.checksum_value !=\n            std::optional<std::string>{biocore::infrastructure::sha256_file_hex(first_managed)}) {\n'''
)
replace_once(
    "tests/filesystem_input_file_storage_tests.cpp",
    '''        if (!std::filesystem::exists(final_path) ||\n            transaction->prepared_file().relative_project_path != "inputs/file-1/genome.fa") {\n''',
    '''        if (!std::filesystem::exists(final_path) ||\n            transaction->prepared_file().relative_project_path != "inputs/file-1/genome.fa" ||\n            transaction->prepared_file().checksum_algorithm != std::optional<std::string>{"sha256"} ||\n            transaction->prepared_file().checksum_value !=\n                std::optional<std::string>{biocore::infrastructure::sha256_file_hex(final_path)}) {\n'''
)

# 8) Dedicated large-file + tamper integrity contract.
write("tests/managed_file_integrity_tests.cpp", r'''#include <chrono>
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
''')

# 9) Register the 71st test.
replace_once(
    "CMakeLists.txt",
    '''    add_test(\n        NAME integration.job_retry_semantics\n        COMMAND biocore-job-retry-semantics-tests\n    )\nendif()\n''',
    '''    add_test(\n        NAME integration.job_retry_semantics\n        COMMAND biocore-job-retry-semantics-tests\n    )\n\n    add_executable(\n        biocore-managed-file-integrity-tests\n        tests/managed_file_integrity_tests.cpp\n    )\n    target_link_libraries(\n        biocore-managed-file-integrity-tests\n        PRIVATE BioCore::infrastructure BioCore::project_warnings BioCore::sanitizers\n    )\n    add_test(\n        NAME integration.managed_file_integrity\n        COMMAND biocore-managed-file-integrity-tests\n    )\nendif()\n'''
)

# 10) Acceptance and scope documentation. Schema intentionally remains v8.
write("docs/development/ITERATION-048-ACCEPTANCE.md", '''# Iteration 048 Acceptance Record\n\n## Status\n\nACCEPTED & FROZEN\n\n## Exact candidate\n\n- Commit: `f762462d864779b02379b6096198a4135add0cae`\n- Frozen reference: `accepted/iteration-048`\n- GitHub Actions run: `33495208914`\n- Gemini review artifact: `OpenGenesis-BioCore-iteration-048-GEMINI-review`\n- Artifact digest: `sha256:0effbaf8e15e7c2e9977d95eafc6f73b6a0557b0660b325207e0e856a7235b4f`\n\n## Validation\n\n- GCC Debug: 70/70 PASS\n- GCC Release: 70/70 PASS\n- Clang Debug: 70/70 PASS\n- GCC ASan+UBSan: 70/70 PASS\n- Total Linux matrix: 280/280 PASS\n- `biocore --version`: `0.2.0-dev`\n- Project database schema: v8\n- Worker Protocol: v2\n\n## Independent review\n\nGemini verdict: `ACCEPT` with 100% confidence.\n\nBlocking findings: NONE.\n\nNon-blocking findings: NONE.\n\nIteration 048 is immutable at the exact commit above. Subsequent development must use the frozen reference as its baseline.\n''')

write("docs/development/ITERATION-049.md", '''# OpenGenesis-BioCore v0.2.0-dev — Iteration 049\n\n## Title\n\nManaged-File Integrity & Large-File I/O\n\n## Goal\n\nMake newly imported managed inputs content-addressable by durable SHA-256 evidence, keep large-file work bounded in memory, and provide a fail-closed integrity inspection path that detects deletion, size changes, same-size content tampering, unsafe path redirection, and files changing during verification.\n\nProject database schema remains v8 because checksum metadata columns already exist. Worker Protocol remains v2.\n\n## Intended changes\n\n- replace managed input `copy_file` with an explicit fixed-buffer streaming copy that computes SHA-256 while bytes are copied;\n- keep the file hashing read buffer fixed at 64 KiB and expose that bound as a testable infrastructure constant;\n- calculate SHA-256 for finalized browser uploads before publishing them into managed inputs;\n- persist `checksum_algorithm=sha256` and the lowercase digest on every managed copy created by the application after this iteration;\n- preserve pre-existing checksum-less records as readable legacy data rather than inventing or backfilling unverifiable digests;\n- add filesystem integrity inspection that validates project-relative path containment, non-symlink regular-file identity, byte size, SHA-256, and file stability while hashing;\n- expose managed-file checksum evidence in existing file JSON and add `GET /api/v1/files/{id}/integrity`;\n- add a large-file/tamper integration contract and raise the active CTest floor from 70 to 71.\n\n## Explicit non-goals\n\nIteration 049 must not:\n\n- change biological algorithms, thresholds, formats, or scientific outputs;\n- change project database schema version 8;\n- rewrite historical checksum-less managed-file records;\n- automatically mutate, repair, or delete a file that fails integrity verification;\n- change Worker Protocol v2, plugin manifests, pipeline definitions, or immutable execution-plan snapshots;\n- add remote/object/cloud storage;\n- change localhost/browser security boundaries or process-tree cancellation ownership;\n- make frontend operational-visibility changes reserved for Iteration 052.\n\n## Acceptance criteria\n\n1. Managed copy import uses a fixed-size streaming buffer and does not read the whole source into process memory.\n2. The streamed copy records a lowercase SHA-256 digest matching the copied byte sequence.\n3. Browser upload finalization records a SHA-256 digest and detects staging mutation during hashing.\n4. `ManagedFileService` persists checksum algorithm/value for newly created managed copies.\n5. Legacy managed-copy records without checksum evidence remain readable and report `checksum_unavailable`.\n6. Integrity inspection returns `verified` for an unchanged managed input.\n7. Same-size byte tampering returns `checksum_mismatch`.\n8. Truncation/size changes return `size_mismatch` before digest comparison.\n9. Missing files return `file_missing`; symlink/path escape or metadata path redirection returns `unsafe_path`.\n10. A file that changes while verification is hashing it returns `changed_during_verification`.\n11. File JSON exposes persisted checksum evidence and `GET /api/v1/files/{id}/integrity` exposes only bounded integrity metadata, not file contents.\n12. Active CTest floor is at least 71 and GCC Debug, GCC Release, Clang Debug, and GCC ASan+UBSan all pass.\n13. The exact candidate is packaged in the standard four-part Gemini review format and remains open until independent `ACCEPT`.\n\n## Freeze rule\n\nDo not create `accepted/iteration-049` until Gemini returns exact `VERDICT: ACCEPT`.\n''')

print("Iteration 049 transformations applied")
