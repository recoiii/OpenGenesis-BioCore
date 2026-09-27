#pragma once

#include "biocore/application/i_project_sample_binding_store.hpp"

namespace biocore::infrastructure::sqlite {

class SqliteConnection;

class SqliteProjectSampleBindingStore final
    : public application::IProjectSampleBindingStore {
public:
    explicit SqliteProjectSampleBindingStore(SqliteConnection& connection) noexcept;

    [[nodiscard]] std::optional<domain::ProjectSampleBinding> find(
        std::string_view project_id,
        std::string_view sample_id
    ) override;

    [[nodiscard]] std::vector<domain::ProjectSampleBinding> list(
        std::string_view project_id
    ) override;

    void upsert(const domain::ProjectSampleBinding& binding) override;

private:
    SqliteConnection& connection_;
};

}  // namespace biocore::infrastructure::sqlite
