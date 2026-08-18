#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include "biocore/domain/job_status.hpp"

namespace {

using biocore::domain::JobStatus;

struct Transition final {
    JobStatus from;
    JobStatus to;
};

struct StatusName final {
    JobStatus status;
    std::string_view name;
};

constexpr std::array all_statuses{
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

constexpr std::array allowed_transitions{
    Transition{JobStatus::draft, JobStatus::queued},
    Transition{JobStatus::draft, JobStatus::cancelled},
    Transition{JobStatus::queued, JobStatus::preparing},
    Transition{JobStatus::queued, JobStatus::cancelling},
    Transition{JobStatus::queued, JobStatus::cancelled},
    Transition{JobStatus::preparing, JobStatus::running},
    Transition{JobStatus::preparing, JobStatus::cancelling},
    Transition{JobStatus::preparing, JobStatus::failed},
    Transition{JobStatus::preparing, JobStatus::interrupted},
    Transition{JobStatus::running, JobStatus::paused},
    Transition{JobStatus::running, JobStatus::cancelling},
    Transition{JobStatus::running, JobStatus::completed},
    Transition{JobStatus::running, JobStatus::failed},
    Transition{JobStatus::running, JobStatus::interrupted},
    Transition{JobStatus::paused, JobStatus::running},
    Transition{JobStatus::paused, JobStatus::cancelling},
    Transition{JobStatus::paused, JobStatus::interrupted},
    Transition{JobStatus::cancelling, JobStatus::cancelled},
    Transition{JobStatus::cancelling, JobStatus::failed},
    Transition{JobStatus::cancelling, JobStatus::interrupted},
    Transition{JobStatus::interrupted, JobStatus::queued},
    Transition{JobStatus::interrupted, JobStatus::cancelled},
};

constexpr std::array status_names{
    StatusName{JobStatus::draft, "draft"},
    StatusName{JobStatus::queued, "queued"},
    StatusName{JobStatus::preparing, "preparing"},
    StatusName{JobStatus::running, "running"},
    StatusName{JobStatus::paused, "paused"},
    StatusName{JobStatus::cancelling, "cancelling"},
    StatusName{JobStatus::cancelled, "cancelled"},
    StatusName{JobStatus::completed, "completed"},
    StatusName{JobStatus::failed, "failed"},
    StatusName{JobStatus::interrupted, "interrupted"},
};

[[nodiscard]] constexpr bool is_allowed(const JobStatus from, const JobStatus to) noexcept {
    for (const auto& transition : allowed_transitions) {
        if (transition.from == from && transition.to == to) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    for (const JobStatus from : all_statuses) {
        for (const JobStatus to : all_statuses) {
            const bool expected = is_allowed(from, to);
            const bool actual = biocore::domain::can_transition(from, to);
            if (actual != expected) {
                std::cerr << "Unexpected transition result: " << biocore::domain::to_string(from) << " -> "
                          << biocore::domain::to_string(to) << "; expected " << expected << " but received " << actual
                          << '\n';
                return EXIT_FAILURE;
            }
        }
    }

    for (const auto& [status, expected_name] : status_names) {
        if (biocore::domain::to_string(status) != expected_name) {
            std::cerr << "Unexpected status name for " << expected_name << '\n';
            return EXIT_FAILURE;
        }
    }

    for (const JobStatus status : all_statuses) {
        const bool expected = status == JobStatus::preparing || status == JobStatus::running ||
                              status == JobStatus::paused || status == JobStatus::cancelling;
        if (biocore::domain::occupies_worker_slot(status) != expected) {
            std::cerr << "Worker-slot classification is invalid for "
                      << biocore::domain::to_string(status) << '\n';
            return EXIT_FAILURE;
        }
    }

    if (!biocore::domain::is_terminal(JobStatus::completed) ||
        !biocore::domain::is_terminal(JobStatus::failed) ||
        !biocore::domain::is_terminal(JobStatus::cancelled) ||
        biocore::domain::is_terminal(JobStatus::draft) ||
        biocore::domain::is_terminal(JobStatus::queued) ||
        biocore::domain::is_terminal(JobStatus::preparing) ||
        biocore::domain::is_terminal(JobStatus::running) ||
        biocore::domain::is_terminal(JobStatus::paused) ||
        biocore::domain::is_terminal(JobStatus::cancelling) ||
        biocore::domain::is_terminal(JobStatus::interrupted)) {
        std::cerr << "Terminal state classification is invalid\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
