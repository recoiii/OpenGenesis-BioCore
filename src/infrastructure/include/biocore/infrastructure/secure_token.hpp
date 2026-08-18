#pragma once

#include <cstddef>
#include <string>

namespace biocore::infrastructure {

[[nodiscard]] std::string generate_secure_token_hex(std::size_t byte_count = 32U);

}  // namespace biocore::infrastructure
