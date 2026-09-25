#pragma once

#include "biocore/application/i_workflow_state_store.hpp"

namespace biocore::infrastructure::sqlite {

class SqliteConnection;

class SqliteWorkflowStateStore final
    : public application::IWorkflowStateStore {
public:
    explicit SqliteWorkflowStateStore(SqliteConnection& connection) noexcept;

    bool create(const application::PersistedWorkflowState& state) override;

    bool update(
        const application::PersistedWorkflowState& state,
        std::int64_t expected_revision
    ) override;

    [[nodiscard]] std::optional<application::PersistedWorkflowState>
    find_by_workflow_id(std::string_view workflow_id) override;

    [[nodiscard]] std::vector<application::PersistedWorkflowState> list() override;

private:
    SqliteConnection& connection_;
};

}  // namespace biocore::infrastructure::sqlite
