#pragma once

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
