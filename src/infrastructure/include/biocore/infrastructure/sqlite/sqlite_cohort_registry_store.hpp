#pragma once

#include "biocore/application/i_cohort_registry_store.hpp"

namespace biocore::infrastructure::sqlite {

class SqliteConnection;

class SqliteCohortRegistryStore final : public application::ICohortRegistryStore {
public:
    explicit SqliteCohortRegistryStore(SqliteConnection& connection) noexcept;

    application::CohortStoreWriteResult create(
        const application::CreateCohortStoreRequest& request
    ) override;

    application::CohortStoreWriteResult append_revision(
        const application::AppendCohortRevisionStoreRequest& request
    ) override;

    [[nodiscard]] std::optional<application::CohortDefinition> find(
        std::string_view project_id,
        std::string_view cohort_id,
        std::optional<std::uint32_t> revision = std::nullopt
    ) override;

    [[nodiscard]] std::vector<application::CohortDefinition> list(
        std::string_view project_id
    ) override;

private:
    SqliteConnection& connection_;
};

}  // namespace biocore::infrastructure::sqlite
