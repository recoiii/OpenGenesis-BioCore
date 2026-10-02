#pragma once

#include "biocore/application/i_cohort_execution_store.hpp"

namespace biocore::infrastructure::sqlite {

class SqliteConnection;

class SqliteCohortExecutionStore final
    : public application::ICohortExecutionStore {
public:
    explicit SqliteCohortExecutionStore(SqliteConnection& connection) noexcept;

    application::CohortExecutionReserveResult reserve_initial(
        const application::CohortExecutionAttempt& attempt
    ) override;

    application::CohortExecutionReserveResult reserve_retry(
        const application::CohortExecutionAttempt& attempt,
        std::string_view expected_parent_attempt_id
    ) override;

    bool attach_job(
        std::string_view project_id,
        std::string_view analysis_id,
        std::string_view attempt_id,
        std::string_view job_id,
        std::string_view updated_at_utc
    ) override;

    [[nodiscard]] std::optional<application::CohortExecutionAttempt> find_attempt(
        std::string_view project_id,
        std::string_view analysis_id,
        std::string_view attempt_id
    ) override;

    [[nodiscard]] std::vector<application::CohortExecutionAttempt> list_attempts(
        std::string_view project_id,
        std::string_view analysis_id
    ) override;

    bool request_cancellation(
        std::string_view project_id,
        std::string_view analysis_id,
        std::string_view attempt_id,
        std::string_view updated_at_utc
    ) override;

    bool update_runtime_state(
        std::string_view project_id,
        std::string_view analysis_id,
        std::string_view attempt_id,
        application::CohortExecutionState state,
        std::optional<std::string_view> failure_message,
        std::string_view updated_at_utc
    ) override;

    bool record_completion(
        std::string_view project_id,
        std::string_view analysis_id,
        std::string_view attempt_id,
        std::string_view result_manifest_file_id,
        std::string_view result_manifest_sha256,
        std::string_view updated_at_utc
    ) override;

private:
    SqliteConnection& connection_;
};

}  // namespace biocore::infrastructure::sqlite
