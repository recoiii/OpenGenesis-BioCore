#pragma once

#include <optional>
#include <string_view>

namespace biocore::domain {

enum class StorageMode {
    managed_copy,
    external_reference,
    managed_move,
    generated_output,
    temporary
};

[[nodiscard]] std::string_view to_string(StorageMode mode) noexcept;
[[nodiscard]] std::optional<StorageMode> storage_mode_from_string(std::string_view value) noexcept;

}  // namespace biocore::domain
