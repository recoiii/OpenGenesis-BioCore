#pragma once

#include "biocore/application/i_cohort_registry_store.hpp"

namespace biocore::infrastructure::sqlite {

class SqliteConnection;

class SqliteCohortRegistryStore final : public application::ICohortRegistryStore {
public:
    explicit SqliteCohortRegistryStore(SqliteConnection& connection) noexcept;

    [[nodiscard]] std::optional<std::vector<domain::CohortDefinition>> list(
        std::string_view project_id) override;

    [[nodiscard]] std::optional<domain::CohortDefinition> get(
        std::string_view project_id,
        std::string_view cohort_id) override;

    [[nodiscard]] std::optional<domain::CohortRevision> get_revision(
        std::string_view project_id,
        std::string_view cohort_id,
        std::int64_t revision) override;

    application::CohortWriteResult create(
        std::string_view project_id,
        std::string_view cohort_id,
        std::string_view name,
        std::string_view created_at_utc,
        std::span<const domain::CohortMemberDraft> members) override;

    application::CohortWriteResult revise(
        std::string_view project_id,
        std::string_view cohort_id,
        std::int64_t expected_revision,
        std::string_view name,
        std::string_view created_at_utc,
        std::span<const domain::CohortMemberDraft> members) override;

private:
    SqliteConnection& connection_;
};

}  // namespace biocore::infrastructure::sqlite
