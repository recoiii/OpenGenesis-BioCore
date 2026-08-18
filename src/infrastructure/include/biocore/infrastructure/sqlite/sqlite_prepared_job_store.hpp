#pragma once

#include "biocore/application/i_prepared_job_store.hpp"

namespace biocore::infrastructure::sqlite {

class SqliteConnection;

class SqlitePreparedJobStore final : public application::IPreparedJobStore {
public:
    explicit SqlitePreparedJobStore(SqliteConnection& connection) noexcept;

    bool add_prepared_job(
        const domain::Job& queued_job,
        const application::PreparedJobExecution& execution
    ) override;

    [[nodiscard]] std::optional<application::PreparedJobExecution> find_execution(
        std::string_view job_id
    ) override;

private:
    SqliteConnection& connection_;
};

}  // namespace biocore::infrastructure::sqlite
