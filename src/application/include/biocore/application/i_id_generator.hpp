#pragma once

#include <string>

namespace biocore::application {

class IIdGenerator {
public:
    virtual ~IIdGenerator() = default;

    [[nodiscard]] virtual std::string generate() = 0;
};

}  // namespace biocore::application
