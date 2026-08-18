#include <cstdlib>
#include <deque>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_job_repository.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/job_service_error.hpp"
#include "biocore/domain/job.hpp"
#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/job_status.hpp"

namespace {

using biocore::application::CreateJobRequest;
using biocore::application::IIdGenerator;
using biocore::application::IJobRepository;
using biocore::application::IUtcClock;
using biocore::application::JobService;
using biocore::application::JobServiceError;
using biocore::application::JobServiceErrorCode;
using biocore::domain::Job;
using biocore::domain::JobPriority;
using biocore::domain::JobStatus;

class FakeIdGenerator final : public IIdGenerator {
public:
    explicit FakeIdGenerator(std::deque<std::string> values) : values_{std::move(values)} {}

    std::string generate() override {
        ++calls;
        if (values_.empty()) {
            throw std::runtime_error("No fake identifier available");
        }
        std::string value = std::move(values_.front());
        values_.pop_front();
        return value;
    }

    int calls{0};

private:
    std::deque<std::string> values_;
};

class FakeClock final : public IUtcClock {
public:
    explicit FakeClock(std::deque<std::string> values) : values_{std::move(values)} {}

    std::string now_utc_iso8601() override {
        ++calls;
        if (values_.empty()) {
            throw std::runtime_error("No fake timestamp available");
        }
        std::string value = std::move(values_.front());
        values_.pop_front();
        return value;
    }

    int calls{0};

private:
    std::deque<std::string> values_;
};

class FakeJobRepository final : public IJobRepository {
public:
    bool add(const Job& job) override {
        ++add_calls;
        if (force_add_collision) {
            return false;
        }
        if (find_index(job.id()).has_value()) {
            return false;
        }
        jobs.push_back(job);
        return true;
    }

    std::optional<Job> find_by_id(const std::string_view job_id) override {
        ++find_calls;
        const auto index = find_index(job_id);
        if (!index.has_value()) {
            return std::nullopt;
        }
        return jobs[*index];
    }

    std::vector<Job> list() override {
        ++list_calls;
        return jobs;
    }

    bool update_runtime_state(const Job& job, const std::int64_t expected_revision) override {
        ++update_calls;
        if (force_concurrent_update) {
            return false;
        }
        const auto index = find_index(job.id());
        if (!index.has_value() || jobs[*index].revision() != expected_revision) {
            return false;
        }
        jobs[*index] = job;
        return true;
    }

    std::vector<Job> jobs;
    bool force_add_collision{false};
    bool force_concurrent_update{false};
    int add_calls{0};
    int find_calls{0};
    int list_calls{0};
    int update_calls{0};

private:
    [[nodiscard]] std::optional<std::size_t> find_index(const std::string_view id) const {
        for (std::size_t index = 0; index < jobs.size(); ++index) {
            if (jobs[index].id() == id) {
                return index;
            }
        }
        return std::nullopt;
    }
};

[[nodiscard]] Job make_job(const std::string& id = "job-existing") {
    return Job{
        id,
        std::string{"analysis-1"},
        std::string{"pipeline-1"},
        std::string{"1.0.0"},
        JobStatus::draft,
        JobPriority::normal,
        0.0,
        std::nullopt,
        "2026-08-06T20:00:00Z",
        "2026-08-06T20:00:00Z",
        std::nullopt,
        std::nullopt,
        0,
    };
}

[[nodiscard]] bool create_contract() {
    FakeJobRepository repository;
    FakeIdGenerator ids{{"job-new"}};
    FakeClock clock{{"2026-08-06T20:01:00Z"}};
    JobService service{repository, ids, clock};

    const Job job = service.create(CreateJobRequest{
        .analysis_id = std::string{"analysis-'quoted"},
        .pipeline_id = std::string{"org.biocore.demo"},
        .pipeline_version = std::string{"0.1.0"},
        .priority = JobPriority::high,
    });

    return job.id() == "job-new" && job.status() == JobStatus::draft &&
           job.priority() == JobPriority::high && job.progress() == 0.0 &&
           job.analysis_id().has_value() && *job.analysis_id() == "analysis-'quoted" &&
           job.pipeline_id().has_value() && *job.pipeline_id() == "org.biocore.demo" &&
           job.pipeline_version().has_value() && *job.pipeline_version() == "0.1.0" &&
           job.created_at_utc() == "2026-08-06T20:01:00Z" &&
           job.updated_at_utc() == "2026-08-06T20:01:00Z" && job.revision() == 0 &&
           repository.jobs.size() == 1U && repository.add_calls == 1 && ids.calls == 1 &&
           clock.calls == 1;
}

[[nodiscard]] bool identifier_collision_contract() {
    FakeJobRepository repository;
    repository.jobs.push_back(make_job("collision"));
    FakeIdGenerator ids{{"collision", "job-unique"}};
    FakeClock clock{{"2026-08-06T20:02:00Z"}};
    JobService service{repository, ids, clock};

    const Job job = service.create(CreateJobRequest{});
    if (job.id() != "job-unique" || repository.add_calls != 2 || ids.calls != 2 ||
        clock.calls != 1) {
        return false;
    }

    FakeJobRepository failing_repository;
    failing_repository.force_add_collision = true;
    FakeIdGenerator failing_ids{{
        "1", "2", "3", "4", "5", "6", "7", "8",
    }};
    FakeClock failing_clock{{"2026-08-06T20:03:00Z"}};
    JobService failing_service{failing_repository, failing_ids, failing_clock};
    try {
        static_cast<void>(failing_service.create(CreateJobRequest{}));
    } catch (const JobServiceError& error) {
        return error.code() == JobServiceErrorCode::identifier_generation_exhausted &&
               failing_repository.add_calls == JobService::maximum_identifier_attempts &&
               failing_ids.calls == JobService::maximum_identifier_attempts &&
               failing_clock.calls == 1;
    }
    return false;
}

[[nodiscard]] bool transition_contract() {
    FakeJobRepository repository;
    repository.jobs.push_back(make_job());
    FakeIdGenerator ids{{"unused"}};
    FakeClock clock{{
        "2026-08-06T20:04:00Z",
        "2026-08-06T20:05:00Z",
        "2026-08-06T20:06:00Z",
        "2026-08-06T20:07:00Z",
    }};
    JobService service{repository, ids, clock};

    Job job = service.transition("job-existing", JobStatus::queued, 0.0, std::nullopt);
    job = service.transition("job-existing", JobStatus::preparing, 0.1, std::string{"prepare"});
    job = service.transition("job-existing", JobStatus::running, 0.7, std::string{"align"});
    job = service.transition("job-existing", JobStatus::completed, 1.0, std::string{"ignored"});

    return job.status() == JobStatus::completed && job.revision() == 4 &&
           job.started_at_utc().has_value() &&
           *job.started_at_utc() == "2026-08-06T20:05:00Z" &&
           job.finished_at_utc().has_value() &&
           *job.finished_at_utc() == "2026-08-06T20:07:00Z" &&
           job.active_step_id().has_value() == false && repository.update_calls == 4 &&
           clock.calls == 4;
}

[[nodiscard]] bool failure_contract() {
    FakeJobRepository repository;
    FakeIdGenerator ids{{"unused"}};
    FakeClock clock{{"should-not-be-consumed", "transition-time"}};
    JobService service{repository, ids, clock};

    try {
        static_cast<void>(service.transition("missing", JobStatus::queued, 0.0, std::nullopt));
        return false;
    } catch (const JobServiceError& error) {
        if (error.code() != JobServiceErrorCode::job_not_found || clock.calls != 0) {
            return false;
        }
    }

    repository.jobs.push_back(make_job());
    try {
        static_cast<void>(service.transition("job-existing", JobStatus::running, 0.5, std::nullopt));
        return false;
    } catch (const std::invalid_argument&) {
        if (clock.calls != 0 || repository.update_calls != 0 ||
            repository.jobs.front().status() != JobStatus::draft) {
            return false;
        }
    }

    repository.force_concurrent_update = true;
    try {
        static_cast<void>(service.transition("job-existing", JobStatus::queued, 0.0, std::nullopt));
    } catch (const JobServiceError& error) {
        return error.code() == JobServiceErrorCode::concurrent_update && clock.calls == 1 &&
               repository.jobs.front().status() == JobStatus::draft;
    }
    return false;
}

[[nodiscard]] bool query_contract() {
    FakeJobRepository repository;
    repository.jobs.push_back(make_job("job-a"));
    repository.jobs.push_back(make_job("job-b"));
    FakeIdGenerator ids{{"unused"}};
    FakeClock clock{{"unused"}};
    JobService service{repository, ids, clock};

    const auto found = service.find_by_id("job-b");
    const auto jobs = service.list();
    return found.has_value() && found->id() == "job-b" && jobs.size() == 2U &&
           repository.find_calls == 1 && repository.list_calls == 1;
}

}  // namespace

int main() {
    if (!create_contract()) {
        std::cerr << "JobService create contract failed\n";
        return EXIT_FAILURE;
    }
    if (!identifier_collision_contract()) {
        std::cerr << "JobService identifier collision contract failed\n";
        return EXIT_FAILURE;
    }
    if (!transition_contract()) {
        std::cerr << "JobService transition contract failed\n";
        return EXIT_FAILURE;
    }
    if (!failure_contract()) {
        std::cerr << "JobService failure contract failed\n";
        return EXIT_FAILURE;
    }
    if (!query_contract()) {
        std::cerr << "JobService query contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
