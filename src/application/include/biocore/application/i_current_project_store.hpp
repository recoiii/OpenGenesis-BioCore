#pragma once

#include <optional>

#include "biocore/domain/project.hpp"

namespace biocore::application {

class ICurrentProjectStore {
public:
    virtual ~ICurrentProjectStore() = default;
    [[nodiscard]] virtual std::optional<domain::Project> find() = 0;
};

}  // namespace biocore::application
