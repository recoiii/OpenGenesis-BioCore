#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "biocore/application/i_prepared_job_store.hpp"

class PreparedJobTestStore final : public biocore::application::IPreparedJobStore {
public:
    explicit PreparedJobTestStore(
        std::string configured_pipeline_id = "org.biocore.pipeline",
        std::string configured_pipeline_version = "1.2.3"
    )
        : pipeline_id{std::move(configured_pipeline_id)},
          pipeline_version{std::move(configured_pipeline_version)} {}

    bool add_prepared_job(
        const biocore::domain::Job&,
        const biocore::application::PreparedJobExecution&
    ) override { return true; }

    std::optional<biocore::application::PreparedJobExecution> find_execution(
        const std::string_view job_id
    ) override {
        if (missing_job_id.has_value() && *missing_job_id == job_id) return std::nullopt;
        return biocore::application::PreparedJobExecution{
            .job_id = std::string{job_id},
            .launch_revision = launch_revision,
            .pipeline_id = pipeline_id,
            .pipeline_version = pipeline_version,
            .execution_plan_path = execution_plan_path,
            .prepared_at_utc = "2026-08-07T00:00:00Z",
        };
    }

    std::int64_t launch_revision{1};
    std::string pipeline_id;
    std::string pipeline_version;
    std::string execution_plan_path{"/tmp/biocore-test-execution-plan.json"};
    std::optional<std::string> missing_job_id{};
};
