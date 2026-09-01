#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/domain/job.hpp"
#include "biocore/domain/job_failure.hpp"
#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/job_status.hpp"

namespace biocore::application {

class IIdGenerator;
class IJobRepository;
class IUtcClock;

struct CreateJobRequest final {
    std::optional<std::string> analysis_id;
    std::optional<std::string> pipeline_id;
    std::optional<std::string> pipeline_version;
    domain::JobPriority priority{domain::JobPriority::normal};
};

struct JobFailureContext final {
    domain::JobFailureKind kind{domain::JobFailureKind::unspecified_terminal_failure};
    std::string message;
    std::optional<std::int64_t> exit_code;
    std::optional<std::string> worker_timestamp_utc;
};

class JobService final {
public:
    static constexpr int maximum_identifier_attempts = 8;

    JobService(IJobRepository& repository, IIdGenerator& id_generator, IUtcClock& clock) noexcept;

    [[nodiscard]] domain::Job create(const CreateJobRequest& request);
    [[nodiscard]] domain::Job transition(
        std::string_view job_id,
        domain::JobStatus target,
        double progress,
        std::optional<std::string> active_step_id,
        std::optional<JobFailureContext> failure = std::nullopt
    );
    [[nodiscard]] domain::Job update_progress(
        std::string_view job_id,
        double progress,
        std::optional<std::string> active_step_id
    );
    [[nodiscard]] std::optional<domain::Job> find_by_id(std::string_view job_id);
    [[nodiscard]] std::vector<domain::Job> list();

private:
    IJobRepository& repository_;
    IIdGenerator& id_generator_;
    IUtcClock& clock_;
};

}  // namespace biocore::application
