#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/job_status.hpp"

namespace biocore::domain {

class Job final {
public:
    static constexpr std::size_t maximum_id_length = 128U;
    static constexpr std::size_t maximum_metadata_length = 200U;

    Job(
        std::string id,
        std::optional<std::string> analysis_id,
        std::optional<std::string> pipeline_id,
        std::optional<std::string> pipeline_version,
        JobStatus status,
        JobPriority priority,
        double progress,
        std::optional<std::string> active_step_id,
        std::string created_at_utc,
        std::string updated_at_utc,
        std::optional<std::string> started_at_utc,
        std::optional<std::string> finished_at_utc,
        std::int64_t revision
    );

    [[nodiscard]] std::string_view id() const noexcept;
    [[nodiscard]] const std::optional<std::string>& analysis_id() const noexcept;
    [[nodiscard]] const std::optional<std::string>& pipeline_id() const noexcept;
    [[nodiscard]] const std::optional<std::string>& pipeline_version() const noexcept;
    [[nodiscard]] JobStatus status() const noexcept;
    [[nodiscard]] JobPriority priority() const noexcept;
    [[nodiscard]] double progress() const noexcept;
    [[nodiscard]] const std::optional<std::string>& active_step_id() const noexcept;
    [[nodiscard]] std::string_view created_at_utc() const noexcept;
    [[nodiscard]] std::string_view updated_at_utc() const noexcept;
    [[nodiscard]] const std::optional<std::string>& started_at_utc() const noexcept;
    [[nodiscard]] const std::optional<std::string>& finished_at_utc() const noexcept;
    [[nodiscard]] std::int64_t revision() const noexcept;

    void update_progress(
        double progress,
        std::optional<std::string> active_step_id,
        std::string update_at_utc
    );

    void transition_to(
        JobStatus target,
        double progress,
        std::optional<std::string> active_step_id,
        std::string transition_at_utc
    );

private:
    std::string id_;
    std::optional<std::string> analysis_id_;
    std::optional<std::string> pipeline_id_;
    std::optional<std::string> pipeline_version_;
    JobStatus status_;
    JobPriority priority_;
    double progress_;
    std::optional<std::string> active_step_id_;
    std::string created_at_utc_;
    std::string updated_at_utc_;
    std::optional<std::string> started_at_utc_;
    std::optional<std::string> finished_at_utc_;
    std::int64_t revision_;
};

}  // namespace biocore::domain
