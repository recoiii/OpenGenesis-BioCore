#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "biocore/application/cohort_registry.hpp"

namespace biocore::application {

class ICohortRegistryStore {
public:
    virtual ~ICohortRegistryStore() = default;

    virtual CohortStoreWriteResult create(const CreateCohortStoreRequest& request) = 0;

    virtual CohortStoreWriteResult append_revision(
        const AppendCohortRevisionStoreRequest& request
    ) = 0;

    [[nodiscard]] virtual std::optional<CohortDefinition> find(
        std::string_view project_id,
        std::string_view cohort_id,
        std::optional<std::uint32_t> revision = std::nullopt
    ) = 0;

    [[nodiscard]] virtual std::vector<CohortDefinition> list(
        std::string_view project_id
    ) = 0;
};

}  // namespace biocore::application
