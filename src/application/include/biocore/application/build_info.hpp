#pragma once

#include <string_view>

namespace biocore::application {

class BuildInfo final {
public:
    [[nodiscard]] static std::string_view version() noexcept;
};

}  // namespace biocore::application
