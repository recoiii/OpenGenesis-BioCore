#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "biocore/domain/storage_mode.hpp"

namespace biocore::domain {

class ManagedFile final {
public:
    static constexpr std::size_t maximum_id_length = 128U;
    static constexpr std::size_t maximum_display_name_length = 255U;
    static constexpr std::size_t maximum_file_type_length = 128U;
    static constexpr std::size_t maximum_metadata_length = 4096U;

    ManagedFile(
        std::string id,
        std::string display_name,
        StorageMode storage_mode,
        std::optional<std::string> original_path,
        std::optional<std::string> managed_path,
        std::optional<std::string> relative_project_path,
        std::string file_type,
        std::int64_t size_bytes,
        std::optional<std::string> modified_at_utc,
        std::optional<std::string> checksum_algorithm,
        std::optional<std::string> checksum_value,
        std::string created_at_utc,
        std::string updated_at_utc
    );

    [[nodiscard]] std::string_view id() const noexcept;
    [[nodiscard]] std::string_view display_name() const noexcept;
    [[nodiscard]] StorageMode storage_mode() const noexcept;
    [[nodiscard]] const std::optional<std::string>& original_path() const noexcept;
    [[nodiscard]] const std::optional<std::string>& managed_path() const noexcept;
    [[nodiscard]] const std::optional<std::string>& relative_project_path() const noexcept;
    [[nodiscard]] std::string_view file_type() const noexcept;
    [[nodiscard]] std::int64_t size_bytes() const noexcept;
    [[nodiscard]] const std::optional<std::string>& modified_at_utc() const noexcept;
    [[nodiscard]] const std::optional<std::string>& checksum_algorithm() const noexcept;
    [[nodiscard]] const std::optional<std::string>& checksum_value() const noexcept;
    [[nodiscard]] std::string_view created_at_utc() const noexcept;
    [[nodiscard]] std::string_view updated_at_utc() const noexcept;

private:
    std::string id_;
    std::string display_name_;
    StorageMode storage_mode_;
    std::optional<std::string> original_path_;
    std::optional<std::string> managed_path_;
    std::optional<std::string> relative_project_path_;
    std::string file_type_;
    std::int64_t size_bytes_;
    std::optional<std::string> modified_at_utc_;
    std::optional<std::string> checksum_algorithm_;
    std::optional<std::string> checksum_value_;
    std::string created_at_utc_;
    std::string updated_at_utc_;
};

}  // namespace biocore::domain
