#pragma once

#include <cstddef>
#include <string_view>

namespace biocore::domain {

[[nodiscard]] bool is_namespaced_identifier(
    std::string_view value,
    std::size_t maximum_length
) noexcept;

[[nodiscard]] bool is_semantic_version(
    std::string_view value,
    std::size_t maximum_length
) noexcept;

}  // namespace biocore::domain
