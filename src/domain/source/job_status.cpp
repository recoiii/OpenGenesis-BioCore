#include "biocore/domain/job_status.hpp"

namespace biocore::domain {

bool can_transition(const JobStatus from, const JobStatus to) noexcept {
    if (from == to) {
        return false;
    }

    switch (from) {
        case JobStatus::draft:
            return to == JobStatus::queued || to == JobStatus::cancelled;
        case JobStatus::queued:
            return to == JobStatus::preparing || to == JobStatus::cancelling || to == JobStatus::cancelled;
        case JobStatus::preparing:
            return to == JobStatus::running || to == JobStatus::cancelling || to == JobStatus::failed ||
                   to == JobStatus::interrupted;
        case JobStatus::running:
            return to == JobStatus::paused || to == JobStatus::cancelling || to == JobStatus::completed ||
                   to == JobStatus::failed || to == JobStatus::interrupted;
        case JobStatus::paused:
            return to == JobStatus::running || to == JobStatus::cancelling || to == JobStatus::interrupted;
        case JobStatus::cancelling:
            return to == JobStatus::cancelled || to == JobStatus::failed || to == JobStatus::interrupted;
        case JobStatus::interrupted:
            return to == JobStatus::queued || to == JobStatus::cancelled;
        case JobStatus::cancelled:
        case JobStatus::completed:
        case JobStatus::failed:
            return false;
    }

    return false;
}

bool occupies_worker_slot(const JobStatus status) noexcept {
    return status == JobStatus::preparing || status == JobStatus::running ||
           status == JobStatus::paused || status == JobStatus::cancelling;
}

std::string_view to_string(const JobStatus status) noexcept {
    switch (status) {
        case JobStatus::draft:
            return "draft";
        case JobStatus::queued:
            return "queued";
        case JobStatus::preparing:
            return "preparing";
        case JobStatus::running:
            return "running";
        case JobStatus::paused:
            return "paused";
        case JobStatus::cancelling:
            return "cancelling";
        case JobStatus::cancelled:
            return "cancelled";
        case JobStatus::completed:
            return "completed";
        case JobStatus::failed:
            return "failed";
        case JobStatus::interrupted:
            return "interrupted";
    }

    return "unknown";
}

std::optional<JobStatus> job_status_from_string(const std::string_view value) noexcept {
    if (value == "draft") {
        return JobStatus::draft;
    }
    if (value == "queued") {
        return JobStatus::queued;
    }
    if (value == "preparing") {
        return JobStatus::preparing;
    }
    if (value == "running") {
        return JobStatus::running;
    }
    if (value == "paused") {
        return JobStatus::paused;
    }
    if (value == "cancelling") {
        return JobStatus::cancelling;
    }
    if (value == "cancelled") {
        return JobStatus::cancelled;
    }
    if (value == "completed") {
        return JobStatus::completed;
    }
    if (value == "failed") {
        return JobStatus::failed;
    }
    if (value == "interrupted") {
        return JobStatus::interrupted;
    }
    return std::nullopt;
}

}  // namespace biocore::domain
