#pragma once

#include <string>
#include <string_view>

namespace biocore::application {

class IPathCanonicalizer {
public:
    virtual ~IPathCanonicalizer() = default;

    [[nodiscard]] virtual std::string canonicalize_existing_directory(std::string_view path) = 0;
};

}  // namespace biocore::application
