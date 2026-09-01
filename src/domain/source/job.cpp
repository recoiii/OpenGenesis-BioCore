#include "biocore/domain/job.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace biocore::domain {
namespace {

[[nodiscard]] bool is_blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
               return std::isspace(static_cast<unsigned char>(character)) != 0;
           });
}

void require_text(
    const std::string_view value,
    const std::string_view field_name,
    const std::size_t maximum_length
) {
    if (is_blank(value)) {
        throw std::invalid_argument(std::string{field_name} + " must not be blank");
    }
    if (value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(std::string{field_name} + " must not contain NUL characters");
    }
    if (value.size() > maximum_length) {
        throw std::invalid_argument(std::string{field_name} + " exceeds the maximum length");
    }
}

void require_optional_text(
    const std::optional<std::string>& value,
    const std::string_view field_name,
    const std::size_t maximum_length
) {
    if (value.has_value()) {
        require_text(*value, field_name, maximum_length);
    }
}

void require_progress(const double progress) {
    if (!std::isfinite(progress) || progress < 0.0 || progress > 1.0) {
        throw std::invalid_argument("Job progress must be finite and between 0 and 1");
    }
}

}  // namespace

Job::Job(
    std::string id,
    std::optional<std::string> analysis_id,
    std::optional<std::string> pipeline_id,
    std::optional<std::string> pipeline_version,
    const JobStatus status,
    const JobPriority priority,
    const double progress,
    std::optional<std::string> active_step_id,
    std::string created_at_utc,
    std::string updated_at_utc,
    std::optional<std::string> started_at_utc,
    std::optional<std::string> finished_at_utc,
    const std::int64_t revision,
    std::optional<JobFailure> failure,
    const std::int64_t attempt_number
)
    : id_{std::move(id)},
      analysis_id_{std::move(analysis_id)},
      pipeline_id_{std::move(pipeline_id)},
      pipeline_version_{std::move(pipeline_version)},
      status_{status},
      priority_{priority},
      progress_{progress},
      active_step_id_{std::move(active_step_id)},
      created_at_utc_{std::move(created_at_utc)},
      updated_at_utc_{std::move(updated_at_utc)},
      started_at_utc_{std::move(started_at_utc)},
      finished_at_utc_{std::move(finished_at_utc)},
      revision_{revision},
      failure_{std::move(failure)},
      attempt_number_{attempt_number} {
    require_text(id_, "Job id", maximum_id_length);
    require_optional_text(analysis_id_, "Job analysis id", maximum_id_length);
    require_optional_text(pipeline_id_, "Job pipeline id", maximum_metadata_length);
    require_optional_text(pipeline_version_, "Job pipeline version", maximum_metadata_length);
    require_optional_text(active_step_id_, "Job active step id", maximum_metadata_length);
    require_text(created_at_utc_, "Job creation timestamp", maximum_metadata_length);
    require_text(updated_at_utc_, "Job update timestamp", maximum_metadata_length);
    require_optional_text(started_at_utc_, "Job start timestamp", maximum_metadata_length);
    require_optional_text(finished_at_utc_, "Job finish timestamp", maximum_metadata_length);
    require_progress(progress_);

    if (revision_ < 0) {
        throw std::invalid_argument("Job revision must not be negative");
    }
    if (attempt_number_ < 1) {
        throw std::invalid_argument("Job attempt number must be at least one");
    }
    if (status_ == JobStatus::completed && progress_ != 1.0) {
        throw std::invalid_argument("Completed jobs must have progress 1");
    }
    if (is_terminal(status_) && !finished_at_utc_.has_value()) {
        throw std::invalid_argument("Terminal jobs must have a finish timestamp");
    }
    if (!is_terminal(status_) && finished_at_utc_.has_value()) {
        throw std::invalid_argument("Non-terminal jobs must not have a finish timestamp");
    }
    if (is_terminal(status_) && active_step_id_.has_value()) {
        throw std::invalid_argument("Terminal jobs must not have an active step");
    }
    if (failure_.has_value() && status_ != JobStatus::failed && status_ != JobStatus::interrupted) {
        throw std::invalid_argument("Failure evidence is only valid for failed or interrupted jobs");
    }

    const bool requires_start_timestamp =
        status_ == JobStatus::preparing || status_ == JobStatus::running ||
        status_ == JobStatus::paused || status_ == JobStatus::cancelling ||
        status_ == JobStatus::completed || status_ == JobStatus::failed ||
        status_ == JobStatus::interrupted;
    if (requires_start_timestamp && !started_at_utc_.has_value()) {
        throw std::invalid_argument("Started jobs must have a start timestamp");
    }
}

std::string_view Job::id() const noexcept {
    return id_;
}

const std::optional<std::string>& Job::analysis_id() const noexcept {
    return analysis_id_;
}

const std::optional<std::string>& Job::pipeline_id() const noexcept {
    return pipeline_id_;
}

const std::optional<std::string>& Job::pipeline_version() const noexcept {
    return pipeline_version_;
}

JobStatus Job::status() const noexcept {
    return status_;
}

JobPriority Job::priority() const noexcept {
    return priority_;
}

double Job::progress() const noexcept {
    return progress_;
}

const std::optional<std::string>& Job::active_step_id() const noexcept {
    return active_step_id_;
}

std::string_view Job::created_at_utc() const noexcept {
    return created_at_utc_;
}

std::string_view Job::updated_at_utc() const noexcept {
    return updated_at_utc_;
}

const std::optional<std::string>& Job::started_at_utc() const noexcept {
    return started_at_utc_;
}

const std::optional<std::string>& Job::finished_at_utc() const noexcept {
    return finished_at_utc_;
}

std::int64_t Job::revision() const noexcept {
    return revision_;
}

const std::optional<JobFailure>& Job::failure() const noexcept {
    return failure_;
}

std::int64_t Job::attempt_number() const noexcept {
    return attempt_number_;
}

void Job::update_progress(
    const double progress,
    std::optional<std::string> active_step_id,
    std::string update_at_utc
) {
    if (status_ != JobStatus::running) {
        throw std::invalid_argument("Only running jobs may report progress");
    }
    require_progress(progress);
    if (progress < progress_) {
        throw std::invalid_argument("Job progress must not decrease");
    }
    require_optional_text(active_step_id, "Job active step id", maximum_metadata_length);
    require_text(update_at_utc, "Job progress timestamp", maximum_metadata_length);
    if (revision_ == std::numeric_limits<std::int64_t>::max()) {
        throw std::overflow_error("Job revision cannot be incremented");
    }

    progress_ = progress;
    active_step_id_ = std::move(active_step_id);
    updated_at_utc_ = std::move(update_at_utc);
    ++revision_;
}

void Job::transition_to(
    const JobStatus target,
    const double progress,
    std::optional<std::string> active_step_id,
    std::string transition_at_utc,
    std::optional<JobFailure> failure
) {
    if (!can_transition(status_, target)) {
        throw std::invalid_argument("Invalid job status transition");
    }
    require_progress(progress);
    require_optional_text(active_step_id, "Job active step id", maximum_metadata_length);
    require_text(transition_at_utc, "Job transition timestamp", maximum_metadata_length);

    if (target == JobStatus::completed && progress != 1.0) {
        throw std::invalid_argument("Completed jobs must have progress 1");
    }
    const bool failure_target = target == JobStatus::failed || target == JobStatus::interrupted;
    const bool retry_transition = status_ == JobStatus::interrupted && target == JobStatus::queued;
    if (retry_transition && (progress != 0.0 || active_step_id.has_value())) {
        throw std::invalid_argument("Retry transitions must restart from zero progress without an active step");
    }
    if (retry_transition && attempt_number_ == std::numeric_limits<std::int64_t>::max()) {
        throw std::overflow_error("Job attempt number cannot be incremented");
    }
    if (!failure_target && failure.has_value()) {
        throw std::invalid_argument("Failure evidence is only valid for failed or interrupted transitions");
    }
    if (failure_target && !failure.has_value()) {
        failure = JobFailure{
            JobFailureKind::unspecified_terminal_failure,
            "Terminal job state was persisted without a more specific runtime diagnostic.",
            std::nullopt,
            std::nullopt,
            transition_at_utc,
        };
    }
    if (revision_ == std::numeric_limits<std::int64_t>::max()) {
        throw std::overflow_error("Job revision cannot be incremented");
    }

    if (retry_transition) {
        started_at_utc_.reset();
        finished_at_utc_.reset();
        active_step_id_.reset();
        ++attempt_number_;
    } else {
        if (!started_at_utc_.has_value() &&
            (target == JobStatus::preparing || target == JobStatus::running ||
             target == JobStatus::paused || target == JobStatus::cancelling)) {
            started_at_utc_ = transition_at_utc;
        }

        if (is_terminal(target)) {
            finished_at_utc_ = transition_at_utc;
            active_step_id_.reset();
        } else {
            finished_at_utc_.reset();
            active_step_id_ = std::move(active_step_id);
        }
    }

    status_ = target;
    progress_ = progress;
    failure_ = failure_target ? std::move(failure) : std::nullopt;
    updated_at_utc_ = std::move(transition_at_utc);
    ++revision_;
}

}  // namespace biocore::domain
