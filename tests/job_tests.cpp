#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "biocore/domain/job.hpp"
#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/job_status.hpp"

namespace {

using biocore::domain::Job;
using biocore::domain::JobPriority;
using biocore::domain::JobStatus;
using biocore::domain::job_priority_from_string;
using biocore::domain::job_status_from_string;
using biocore::domain::to_string;

template <typename Function>
[[nodiscard]] bool throws_invalid_argument(Function&& function) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

template <typename Function>
[[nodiscard]] bool throws_overflow_error(Function&& function) {
    try {
        function();
    } catch (const std::overflow_error&) {
        return true;
    }
    return false;
}

[[nodiscard]] Job make_draft() {
    return Job{
        "job-1",
        std::string{"analysis-1"},
        std::string{"org.biocore.demo"},
        std::string{"0.1.0"},
        JobStatus::draft,
        JobPriority::high,
        0.0,
        std::nullopt,
        "2026-08-06T19:00:00Z",
        "2026-08-06T19:00:00Z",
        std::nullopt,
        std::nullopt,
        0,
    };
}

[[nodiscard]] bool constructor_and_enum_contract() {
    const Job job = make_draft();
    if (job.id() != "job-1" || !job.analysis_id().has_value() ||
        *job.analysis_id() != "analysis-1" || !job.pipeline_id().has_value() ||
        *job.pipeline_id() != "org.biocore.demo" ||
        !job.pipeline_version().has_value() || *job.pipeline_version() != "0.1.0" ||
        job.status() != JobStatus::draft || job.priority() != JobPriority::high ||
        job.progress() != 0.0 || job.active_step_id().has_value() ||
        job.created_at_utc() != "2026-08-06T19:00:00Z" ||
        job.updated_at_utc() != "2026-08-06T19:00:00Z" ||
        job.started_at_utc().has_value() || job.finished_at_utc().has_value() ||
        job.revision() != 0) {
        return false;
    }

    constexpr std::array<JobPriority, 3> priorities{
        JobPriority::low,
        JobPriority::normal,
        JobPriority::high,
    };
    for (const JobPriority priority : priorities) {
        const auto parsed = job_priority_from_string(to_string(priority));
        if (!parsed.has_value() || *parsed != priority) {
            return false;
        }
    }

    constexpr std::array<JobStatus, 10> statuses{
        JobStatus::draft,
        JobStatus::queued,
        JobStatus::preparing,
        JobStatus::running,
        JobStatus::paused,
        JobStatus::cancelling,
        JobStatus::cancelled,
        JobStatus::completed,
        JobStatus::failed,
        JobStatus::interrupted,
    };
    for (const JobStatus status : statuses) {
        const auto parsed = job_status_from_string(to_string(status));
        if (!parsed.has_value() || *parsed != status) {
            return false;
        }
    }

    return !job_priority_from_string("urgent").has_value() &&
           !job_status_from_string("skipped").has_value();
}

[[nodiscard]] bool validation_contract() {
    if (!throws_invalid_argument([] {
            Job{
                " ", std::nullopt, std::nullopt, std::nullopt, JobStatus::draft,
                JobPriority::normal, 0.0, std::nullopt, "t", "t", std::nullopt,
                std::nullopt, 0};
        }) ||
        !throws_invalid_argument([] {
            Job{
                "job", std::string{""}, std::nullopt, std::nullopt, JobStatus::draft,
                JobPriority::normal, 0.0, std::nullopt, "t", "t", std::nullopt,
                std::nullopt, 0};
        }) ||
        !throws_invalid_argument([] {
            Job{
                "job", std::nullopt, std::nullopt, std::nullopt, JobStatus::draft,
                JobPriority::normal, std::numeric_limits<double>::quiet_NaN(), std::nullopt,
                "t", "t", std::nullopt, std::nullopt, 0};
        }) ||
        !throws_invalid_argument([] {
            Job{
                "job", std::nullopt, std::nullopt, std::nullopt, JobStatus::completed,
                JobPriority::normal, 0.9, std::nullopt, "t", "t", std::nullopt,
                std::string{"done"}, 0};
        }) ||
        !throws_invalid_argument([] {
            Job{
                "job", std::nullopt, std::nullopt, std::nullopt, JobStatus::failed,
                JobPriority::normal, 0.5, std::nullopt, "t", "t", std::nullopt,
                std::nullopt, 0};
        }) ||
        !throws_invalid_argument([] {
            Job{
                "job", std::nullopt, std::nullopt, std::nullopt, JobStatus::running,
                JobPriority::normal, 0.5, std::nullopt, "t", "t", std::string{"start"},
                std::string{"finish"}, 0};
        }) ||
        !throws_invalid_argument([] {
            Job{
                "job", std::nullopt, std::nullopt, std::nullopt, JobStatus::preparing,
                JobPriority::normal, 0.1, std::string{"step"}, "t", "t", std::nullopt,
                std::nullopt, 1};
        }) ||
        !throws_invalid_argument([] {
            Job{
                "job", std::nullopt, std::nullopt, std::nullopt, JobStatus::completed,
                JobPriority::normal, 1.0, std::string{"stale-step"}, "t", "t",
                std::string{"started"}, std::string{"finished"}, 1};
        }) ||
        !throws_invalid_argument([] {
            Job{
                "job", std::nullopt, std::nullopt, std::nullopt, JobStatus::draft,
                JobPriority::normal, std::numeric_limits<double>::infinity(), std::nullopt,
                "t", "t", std::nullopt, std::nullopt, 0};
        }) ||
        !throws_invalid_argument([] {
            Job{
                "job", std::nullopt, std::nullopt, std::nullopt, JobStatus::draft,
                JobPriority::normal, 0.0, std::nullopt, "t", "t", std::nullopt,
                std::nullopt, -1};
        })) {
        return false;
    }

    return true;
}

[[nodiscard]] bool transition_lifecycle_contract() {
    Job job = make_draft();

    job.transition_to(JobStatus::queued, 0.0, std::nullopt, "2026-08-06T19:01:00Z");
    if (job.status() != JobStatus::queued || job.revision() != 1 ||
        job.started_at_utc().has_value() || job.finished_at_utc().has_value()) {
        return false;
    }

    job.transition_to(
        JobStatus::preparing,
        0.1,
        std::string{"prepare"},
        "2026-08-06T19:02:00Z"
    );
    if (job.status() != JobStatus::preparing || job.revision() != 2 ||
        !job.started_at_utc().has_value() ||
        *job.started_at_utc() != "2026-08-06T19:02:00Z" ||
        !job.active_step_id().has_value() || *job.active_step_id() != "prepare") {
        return false;
    }

    job.transition_to(
        JobStatus::running,
        0.6,
        std::string{"scan"},
        "2026-08-06T19:03:00Z"
    );
    if (job.status() != JobStatus::running || job.revision() != 3 ||
        *job.started_at_utc() != "2026-08-06T19:02:00Z" ||
        !job.active_step_id().has_value() || *job.active_step_id() != "scan") {
        return false;
    }

    job.transition_to(JobStatus::completed, 1.0, std::string{"ignored"}, "2026-08-06T19:04:00Z");
    if (job.status() != JobStatus::completed || job.progress() != 1.0 ||
        job.revision() != 4 || job.active_step_id().has_value() ||
        !job.finished_at_utc().has_value() ||
        *job.finished_at_utc() != "2026-08-06T19:04:00Z" ||
        job.updated_at_utc() != "2026-08-06T19:04:00Z") {
        return false;
    }

    return throws_invalid_argument([&job] {
        job.transition_to(JobStatus::queued, 1.0, std::nullopt, "later");
    });
}

[[nodiscard]] bool transition_edge_contract() {
    Job cancelled = make_draft();
    cancelled.transition_to(JobStatus::cancelled, 0.0, std::nullopt, "cancelled-at");
    if (cancelled.started_at_utc().has_value() || !cancelled.finished_at_utc().has_value()) {
        return false;
    }

    Job invalid = make_draft();
    if (!throws_invalid_argument([&invalid] {
            invalid.transition_to(JobStatus::running, 0.5, std::nullopt, "t");
        }) ||
        invalid.status() != JobStatus::draft || invalid.revision() != 0) {
        return false;
    }

    Job completed = make_draft();
    completed.transition_to(JobStatus::queued, 0.0, std::nullopt, "q");
    completed.transition_to(JobStatus::preparing, 0.1, std::nullopt, "p");
    completed.transition_to(JobStatus::running, 0.9, std::nullopt, "r");
    if (!throws_invalid_argument([&completed] {
            completed.transition_to(JobStatus::completed, 0.99, std::nullopt, "c");
        })) {
        return false;
    }

    Job maximum_revision{
        "job-max", std::nullopt, std::nullopt, std::nullopt, JobStatus::queued,
        JobPriority::normal, 0.0, std::nullopt, "t", "t", std::nullopt, std::nullopt,
        std::numeric_limits<std::int64_t>::max()};
    return throws_overflow_error([&maximum_revision] {
        maximum_revision.transition_to(JobStatus::preparing, 0.1, std::nullopt, "next");
    });
}

}  // namespace

int main() {
    if (!constructor_and_enum_contract()) {
        std::cerr << "Job constructor/enum contract failed\n";
        return EXIT_FAILURE;
    }
    if (!validation_contract()) {
        std::cerr << "Job validation contract failed\n";
        return EXIT_FAILURE;
    }
    if (!transition_lifecycle_contract()) {
        std::cerr << "Job transition lifecycle contract failed\n";
        return EXIT_FAILURE;
    }
    if (!transition_edge_contract()) {
        std::cerr << "Job transition edge contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
