#include "biocore/domain/storage_mode.hpp"

namespace biocore::domain {

std::string_view to_string(const StorageMode mode) noexcept {
    switch (mode) {
        case StorageMode::managed_copy:
            return "managed_copy";
        case StorageMode::external_reference:
            return "external_reference";
        case StorageMode::managed_move:
            return "managed_move";
        case StorageMode::generated_output:
            return "generated_output";
        case StorageMode::temporary:
            return "temporary";
    }
    return "unknown";
}

std::optional<StorageMode> storage_mode_from_string(const std::string_view value) noexcept {
    if (value == "managed_copy") {
        return StorageMode::managed_copy;
    }
    if (value == "external_reference") {
        return StorageMode::external_reference;
    }
    if (value == "managed_move") {
        return StorageMode::managed_move;
    }
    if (value == "generated_output") {
        return StorageMode::generated_output;
    }
    if (value == "temporary") {
        return StorageMode::temporary;
    }
    return std::nullopt;
}

}  // namespace biocore::domain
