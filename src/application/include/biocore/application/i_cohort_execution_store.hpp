#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "biocore/application/cohort_execution.hpp"

namespace biocore::application {

class ICohortExecutionStore {
public:
    virtual ~ICohortExecutionStore() = default;

    virtual CohortExecutionReserveResult reserve_initial(
        const CohortExecutionAttempt& attempt
    ) = 0;

    virtual CohortExecutionReserveResult reserve_retry(
        const CohortExecutionAttempt& attempt,
        std::string_view expected_parent_attempt_id
    ) = 0;

    virtual bool attach_job(
        std::string_view project_id,
        std::string_view analysis_id,
        std::string_view attempt_id,
        std::string_view job_id,
        std::string_view updated_at_utc
    ) = 0;

    [[nodiscard]] virtual std::optional<CohortExecutionAttempt> find_attempt(
        std::string_view project_id,
        std::string_view analysis_id,
        std::string_view attempt_id
    ) = 0;

    [[nodiscard]] virtual std::vector<CohortExecutionAttempt> list_attempts(
        std::string_view project_id,
        std::string_view analysis_id
    ) = 0;

    virtual bool request_cancellation(
        std::string_view project_id,
        std::string_view analysis_id,
        std::string_view attempt_id,
        std::string_view updated_at_utc
    ) = 0;

    virtual bool update_runtime_state(
        std::string_view project_id,
        std::string_view analysis_id,
        std::string_view attempt_id,
        CohortExecutionState state,
        std::optional<std::string_view> failure_message,
        std::string_view updated_at_utc
    ) = 0;

    virtual bool record_completion(
        std::string_view project_id,
        std::string_view analysis_id,
        std::string_view attempt_id,
        std::string_view result_manifest_file_id,
        std::string_view result_manifest_sha256,
        std::string_view updated_at_utc
    ) = 0;
};

}  // namespace biocore::application
