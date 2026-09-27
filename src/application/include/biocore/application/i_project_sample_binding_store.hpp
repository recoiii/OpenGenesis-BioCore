#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "biocore/domain/project_sample_binding.hpp"

namespace biocore::application {

class IProjectSampleBindingStore {
public:
    virtual ~IProjectSampleBindingStore() = default;

    [[nodiscard]] virtual std::optional<domain::ProjectSampleBinding> find(
        std::string_view project_id,
        std::string_view sample_id
    ) = 0;

    [[nodiscard]] virtual std::vector<domain::ProjectSampleBinding> list(
        std::string_view project_id
    ) = 0;

    virtual void upsert(const domain::ProjectSampleBinding& binding) = 0;
};

}  // namespace biocore::application
