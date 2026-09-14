#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace biocore::domain {

enum class VariantType : std::uint8_t {
    snv,
    insertion,
    deletion,
    mnv,
    delins_complex,
    unknown
};

[[nodiscard]] std::string_view to_string(VariantType type) noexcept;
[[nodiscard]] std::optional<VariantType> variant_type_from_string(std::string_view value) noexcept;
[[nodiscard]] VariantType classify_variant(
    std::string_view reference,
    std::string_view alternate,
    bool symbolic = false
) noexcept;

}  // namespace biocore::domain
