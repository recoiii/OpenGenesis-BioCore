#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/cohort_analysis_snapshot.hpp"
#include "biocore/application/cohort_execution_service.hpp"
#include "biocore/application/i_cohort_analysis_snapshot_store.hpp"
#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_job_repository.hpp"
#include "biocore/application/i_job_submitter.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/domain/job.hpp"
#include "biocore/domain/job_status.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_cohort_execution_store.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"

namespace {
using namespace biocore;

constexpr const char* project_id = "p-095";
constexpr const char* analysis_id = "a-095";
constexpr const char* snapshot_digest =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr const char* payload_digest =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
constexpr const char* other_payload =
    "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
constexpr const char* manifest_hash =
    "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";

void check(const bool value, const char* message) {
    if (!value) throw std::runtime_error{message};
}

template<class Exception, class Function>
void rejects(Function function) {
    try { function(); } catch (const Exception&) { return; }
    throw std::runtime_error{"Expected exception was not thrown"};
}

class Clock final : public application::IUtcClock {
public:
    std::string now_utc_iso8601() override {
        ++tick;
        return "2026-10-02T15:30:" + (tick < 10 ? std::string{"0"} : std::string{}) +
               std::to_string(tick) + "Z";
    }
    int tick{0};
};

class Ids final : public application::IIdGenerator {
public:
    std::string generate() override {
        if (values.empty()) throw std::runtime_error{"identifier fixture exhausted"};
        auto value = values.front();
        values.erase(values.begin());
        return value;
    }
    std::vector<std::string> values{"attempt-1", "attempt-2", "attempt-3"};
};

class SnapshotStore final : public application::ICohortAnalysisSnapshotStore {
public:
    application::CohortAnalysisStoreResult create(
        const application::CohortAnalysisSnapshot& value
    ) override {
        snapshot = value;
        return application::CohortAnalysisStoreResult::stored;
    }

    std::optional<application::CohortAnalysisSnapshot> find(
        std::string_view project,
        std::string_view analysis
    ) override {
        if (!snapshot.has_value() ||
            snapshot->project_id != project ||
            snapshot->analysis_id != analysis) {
            return std::nullopt;
        }
        return snapshot;
    }

    std::optional<application::CohortAnalysisSnapshot> snapshot;
};

class Verifier final : public application::ICohortExecutionInputVerifier {
public:
    bool verify(const application::CohortAnalysisSnapshot&) override {
        ++calls;
        return valid;
    }
    bool valid{true};
    int calls{0};
};

class Files final : public application::IManagedFileRepository {
public:
    bool add(const domain::ManagedFile& file) override {
        return files.emplace(std::string{file.id()}, file).second;
    }

    std::optional<domain::ManagedFile> find_by_id(
        const std::string_view id
    ) override {
        const auto it = files.find(std::string{id});
        return it == files.end() ? std::nullopt
                                 : std::optional<domain::ManagedFile>{it->second};
    }

    std::optional<domain::ManagedFile> find_by_relative_project_path(
        const std::string_view path
    ) override {
        for (const auto& [id, file] : files) {
            (void)id;
            if (file.relative_project_path() ==
                std::optional<std::string>{std::string{path}}) return file;
        }
        return std::nullopt;
    }

    std::vector<domain::ManagedFile> list() override {
        std::vector<domain::ManagedFile> result;
        for (const auto& [id, file] : files) {
            (void)id;
            result.push_back(file);
        }
        return result;
    }

    bool add_generated_output(
        const domain::ManagedFile& file,
        const application::GeneratedOutputProvenance& provenance
    ) override {
        const application::GeneratedOutputArtifact artifact{file, provenance};
        return add_generated_outputs_batch(std::span{&artifact, 1U});
    }

    bool add_generated_outputs_batch(
        const std::span<const application::GeneratedOutputArtifact> values
    ) override {
        if (values.empty()) return false;
        for (const auto& value : values) {
            if (!files.emplace(std::string{value.file.id()}, value.file).second) {
                return false;
            }
            artifacts.push_back(value);
        }
        return true;
    }

    std::optional<application::GeneratedOutputArtifact> find_generated_output(
        const std::string_view job,
        const std::string_view step,
        const std::string_view port
    ) override {
        for (const auto& value : artifacts) {
            if (value.provenance.job_id == job &&
                value.provenance.step_id == step &&
                value.provenance.output_port == port) return value;
        }
        return std::nullopt;
    }

    std::vector<application::GeneratedOutputArtifact> list_generated_outputs(
        const std::string_view job
    ) override {
        std::vector<application::GeneratedOutputArtifact> result;
        for (const auto& value : artifacts) {
            if (value.provenance.job_id == job) result.push_back(value);
        }
        return result;
    }

    std::map<std::string, domain::ManagedFile, std::less<>> files;
    std::vector<application::GeneratedOutputArtifact> artifacts;
};

class Jobs final : public application::IJobRepository {
public:
    bool add(const domain::Job& job) override {
        return values.emplace(std::string{job.id()}, job).second;
    }

    std::optional<domain::Job> find_by_id(const std::string_view id) override {
        const auto it = values.find(std::string{id});
        return it == values.end() ? std::nullopt
                                  : std::optional<domain::Job>{it->second};
    }

    std::vector<domain::Job> list() override {
        std::vector<domain::Job> result;
        for (const auto& [id, job] : values) {
            (void)id;
            result.push_back(job);
        }
        return result;
    }

    bool update_runtime_state(
        const domain::Job& job,
        const std::int64_t expected_revision
    ) override {
        const auto it = values.find(std::string{job.id()});
        if (it == values.end() || it->second.revision() != expected_revision) return false;
        it->second = job;
        return true;
    }

    std::map<std::string, domain::Job, std::less<>> values;
};

class Submitter final : public application::IJobSubmitter {
public:
    Submitter(Jobs& jobs, infrastructure::sqlite::SqliteConnection& connection)
        : jobs_{jobs}, connection_{connection} {}

    domain::Job submit(const application::SubmitJobRequest& request) override {
        ++calls;
        if (fail) throw std::runtime_error{"synthetic scheduler failure"};
        const std::string id = "job-" + std::to_string(calls);
        domain::Job job{
            id,
            request.analysis_id,
            request.pipeline_id,
            request.pipeline_version,
            domain::JobStatus::queued,
            request.priority,
            0.0,
            std::nullopt,
            "2026-10-02T15:31:00Z",
            "2026-10-02T15:31:00Z",
            std::nullopt,
            std::nullopt,
            1,
        };
        check(jobs_.add(job), "job fixture id collision");
        connection_.execute(
            "INSERT INTO jobs(id) VALUES('" + id + "');"
        );
        return job;
    }

    Jobs& jobs_;
    infrastructure::sqlite::SqliteConnection& connection_;
    int calls{0};
    bool fail{false};
};

void bootstrap_v17(infrastructure::sqlite::SqliteConnection& connection) {
    connection.execute(R"sql(
        PRAGMA foreign_keys=ON;
        CREATE TABLE schema_migrations (
            version INTEGER PRIMARY KEY NOT NULL,
            name TEXT NOT NULL,
            applied_at_utc TEXT NOT NULL
        );
        INSERT INTO schema_migrations(version,name,applied_at_utc)
        VALUES(17,'fixture-v17','2026-10-02T00:00:00Z');

        CREATE TABLE jobs (
            id TEXT PRIMARY KEY NOT NULL
        );
        CREATE TABLE managed_files (
            id TEXT PRIMARY KEY NOT NULL
        );
        CREATE TABLE cohort_analysis_snapshots (
            project_id TEXT NOT NULL,
            analysis_id TEXT NOT NULL,
            sealed INTEGER NOT NULL,
            snapshot_digest TEXT NOT NULL,
            PRIMARY KEY(project_id,analysis_id)
        );
        INSERT INTO cohort_analysis_snapshots(
            project_id,analysis_id,sealed,snapshot_digest
        ) VALUES(
            'p-095','a-095',1,
            'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'
        );
    )sql");
}

application::CohortExecutionAttempt attempt(
    std::int64_t number,
    std::string id,
    std::optional<std::string> parent,
    std::string key,
    std::string payload = payload_digest
) {
    return {
        .project_id = project_id,
        .analysis_id = analysis_id,
        .attempt_number = number,
        .attempt_id = std::move(id),
        .parent_attempt_id = std::move(parent),
        .job_id = std::nullopt,
        .snapshot_digest = snapshot_digest,
        .idempotency_key = std::move(key),
        .payload_digest = std::move(payload),
        .state = application::CohortExecutionState::queued,
        .cancellation_requested = false,
        .created_at_utc = "2026-10-02T15:30:00Z",
        .updated_at_utc = "2026-10-02T15:30:00Z",
        .failure_message = std::nullopt,
        .result_manifest_file_id = std::nullopt,
        .result_manifest_sha256 = std::nullopt,
    };
}

struct Harness final {
    infrastructure::sqlite::SqliteConnection connection{":memory:"};
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    infrastructure::sqlite::SqliteCohortExecutionStore executions{connection};
    SnapshotStore snapshots;
    Verifier verifier;
    Files files;
    Jobs job_repository;
    Clock clock;
    Ids ids;
    application::JobService jobs{job_repository, ids, clock};
    Submitter submitter{job_repository, connection};
    application::CohortExecutionService service{
        snapshots, executions, verifier, submitter, files, jobs, ids, clock
    };

    Harness() {
        bootstrap_v17(connection);
        migrations.apply_pending();
        application::CohortAnalysisSnapshot snapshot;
        snapshot.project_id = project_id;
        snapshot.analysis_id = analysis_id;
        snapshot.snapshot_digest = snapshot_digest;
        snapshots.snapshot = snapshot;
    }
};

application::SubmitCohortExecutionRequest submit_request(
    std::string key = "submit-key",
    std::string payload = payload_digest
) {
    return {
        .project_id = project_id,
        .analysis_id = analysis_id,
        .expected_snapshot_digest = snapshot_digest,
        .idempotency_key = std::move(key),
        .payload_digest = std::move(payload),
        .priority = domain::JobPriority::normal,
    };
}

void store_test() {
    infrastructure::sqlite::SqliteConnection connection{":memory:"};
    bootstrap_v17(connection);
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    check(migrations.current_version() == 18, "schema advances to v18");

    infrastructure::sqlite::SqliteCohortExecutionStore store{connection};
    auto first = attempt(1, "attempt-1", std::nullopt, "key-1");
    check(store.reserve_initial(first) ==
          application::CohortExecutionReserveResult::created,
          "initial reservation created");
    check(store.reserve_initial(first) ==
          application::CohortExecutionReserveResult::replayed,
          "same key/payload replays");
    auto conflict = first;
    conflict.payload_digest = other_payload;
    check(store.reserve_initial(conflict) ==
          application::CohortExecutionReserveResult::idempotency_conflict,
          "same key different payload conflicts");
    auto second_initial = first;
    second_initial.idempotency_key = "other-key";
    second_initial.attempt_id = "attempt-other";
    check(store.reserve_initial(second_initial) ==
          application::CohortExecutionReserveResult::analysis_already_submitted,
          "second logical initial execution rejected");

    check(store.update_runtime_state(
              project_id, analysis_id, "attempt-1",
              application::CohortExecutionState::interrupted,
              "synthetic crash", "2026-10-02T15:31:00Z"),
          "initial attempt interrupted");

    auto child = attempt(2, "attempt-2", "attempt-1", "key-1");
    check(store.reserve_retry(child, "attempt-1") ==
          application::CohortExecutionReserveResult::created,
          "retry child reserved");
    const auto values = store.list_attempts(project_id, analysis_id);
    check(values.size() == 2U, "two attempts retained");
    check(values[1].parent_attempt_id == std::optional<std::string>{"attempt-1"},
          "retry parent lineage retained");
}

void duplicate_test() {
    Harness h;
    const auto first = h.service.submit(submit_request());
    check(first.job_id == std::optional<std::string>{"job-1"}, "first handoff attached");
    const auto replay = h.service.submit(submit_request());
    check(replay.attempt_id == first.attempt_id, "duplicate returns same attempt");
    check(h.submitter.calls == 1, "duplicate does not submit second job");

    auto conflict = submit_request("submit-key", other_payload);
    rejects<application::CohortExecutionError>([&] { (void)h.service.submit(conflict); });
    check(h.submitter.calls == 1, "conflict does not submit job");
}

void drift_test() {
    Harness h;
    h.verifier.valid = false;
    rejects<application::CohortExecutionError>([&] {
        (void)h.service.submit(submit_request());
    });
    check(h.submitter.calls == 0, "changed input blocks scheduler handoff");
    check(h.executions.list_attempts(project_id, analysis_id).empty(),
          "changed input does not reserve attempt");
}

void recovery_test() {
    Harness h;
    auto reserved = attempt(1, "orphan-attempt", std::nullopt, "orphan-key");
    check(h.executions.reserve_initial(reserved) ==
          application::CohortExecutionReserveResult::created,
          "orphan reservation stored before simulated crash");

    const auto history = h.service.reconcile(project_id, analysis_id);
    check(history.attempts.size() == 1U, "recovery returns orphan attempt");
    check(history.attempts[0].state ==
          application::CohortExecutionState::interrupted,
          "missing handoff is interrupted not completed");
    check(history.attempts[0].failure_message.has_value(),
          "recovery records failure evidence");
}

void retry_test() {
    Harness h;
    const auto first = h.service.submit(submit_request());
    check(first.job_id.has_value(), "initial job attached");

    auto job = h.job_repository.find_by_id(*first.job_id);
    check(job.has_value(), "initial job available");
    (void)h.jobs.transition(
        job->id(), domain::JobStatus::preparing, 0.0, std::nullopt
    );
    (void)h.jobs.transition(
        job->id(), domain::JobStatus::interrupted, 0.0, std::nullopt,
        application::JobFailureContext{
            .kind = domain::JobFailureKind::startup_recovery,
            .message = "synthetic crash",
            .exit_code = std::nullopt,
            .worker_timestamp_utc = std::nullopt,
        }
    );

    const auto child = h.service.retry({
        .project_id = project_id,
        .analysis_id = analysis_id,
        .expected_parent_attempt_id = first.attempt_id,
        .idempotency_key = "submit-key",
        .payload_digest = payload_digest,
        .priority = domain::JobPriority::normal,
    });
    check(child.attempt_number == 2, "retry advances attempt number");
    check(child.parent_attempt_id == std::optional<std::string>{first.attempt_id},
          "retry links parent attempt");
    check(child.job_id == std::optional<std::string>{"job-2"},
          "retry gets new job id");

    const auto replay = h.service.retry({
        .project_id = project_id,
        .analysis_id = analysis_id,
        .expected_parent_attempt_id = first.attempt_id,
        .idempotency_key = "submit-key",
        .payload_digest = payload_digest,
        .priority = domain::JobPriority::normal,
    });
    check(replay.attempt_id == child.attempt_id, "duplicate retry replays child");
    check(h.submitter.calls == 2, "duplicate retry does not create third job");
}

void cancel_test() {
    Harness h;
    const auto first = h.service.submit(submit_request());
    const auto cancelled = h.service.cancel(
        project_id, analysis_id, first.attempt_id
    );
    check(cancelled.cancellation_requested, "cancellation persisted first");

    const auto job = h.job_repository.find_by_id(*first.job_id);
    check(job.has_value() && job->status() == domain::JobStatus::cancelling,
          "existing job receives cancelling transition");

    const auto again = h.service.cancel(
        project_id, analysis_id, first.attempt_id
    );
    check(again.cancellation_requested, "cancellation request is idempotent");
}

void completion_test() {
    Harness h;
    const auto first = h.service.submit(submit_request());
    auto job = h.job_repository.find_by_id(*first.job_id);
    check(job.has_value(), "completion job exists");

    (void)h.jobs.transition(
        job->id(), domain::JobStatus::preparing, 0.0, std::nullopt
    );
    (void)h.jobs.transition(
        job->id(), domain::JobStatus::running, 0.2, std::string{"analysis"}
    );
    (void)h.jobs.transition(
        job->id(), domain::JobStatus::completed, 1.0, std::nullopt
    );

    const auto before = h.service.reconcile(project_id, analysis_id);
    check(before.attempts[0].state ==
          application::CohortExecutionState::interrupted,
          "job completion alone is never cohort completion");

    Harness h2;
    const auto second = h2.service.submit(submit_request());
    auto job2 = h2.job_repository.find_by_id(*second.job_id);
    check(job2.has_value(), "verified completion job exists");
    (void)h2.jobs.transition(
        job2->id(), domain::JobStatus::preparing, 0.0, std::nullopt
    );
    (void)h2.jobs.transition(
        job2->id(), domain::JobStatus::running, 0.5, std::string{"analysis"}
    );
    (void)h2.jobs.transition(
        job2->id(), domain::JobStatus::completed, 1.0, std::nullopt
    );
    h2.connection.execute("INSERT INTO managed_files(id) VALUES('manifest-1');");
    const domain::ManagedFile manifest{
        "manifest-1",
        "cohort-result-manifest.json",
        domain::StorageMode::generated_output,
        std::nullopt,
        std::string{"/managed/project/outputs/manifest-1.json"},
        std::string{"outputs/manifest-1.json"},
        "json",
        128,
        std::nullopt,
        std::string{"sha256"},
        std::string{manifest_hash},
        "2026-10-02T15:32:00Z",
        "2026-10-02T15:32:00Z"
    };
    check(h2.files.add_generated_output(
        manifest,
        application::GeneratedOutputProvenance{
            .job_id = *second.job_id,
            .step_id = "analysis",
            .output_port = "manifest",
            .plugin_id = "org.biocore.cohort",
            .plugin_version = "1.0.0",
            .module_id = "org.biocore.cohort.analysis",
            .file_type = "json",
            .relative_project_path = "outputs/manifest-1.json",
            .step_progress = 1.0,
            .registered_at_utc = "2026-10-02T15:32:00Z",
        }),
        "verified manifest fixture registered"
    );
    const auto completed = h2.service.complete({
        .project_id = project_id,
        .analysis_id = analysis_id,
        .attempt_id = second.attempt_id,
        .result_manifest_file_id = "manifest-1",
        .result_manifest_sha256 = manifest_hash,
    });
    check(completed.state == application::CohortExecutionState::completed,
          "verified manifest commits completed state");
    check(completed.result_manifest_sha256 ==
          std::optional<std::string>{manifest_hash},
          "manifest hash retained");

    rejects<application::CohortExecutionError>([&] {
        (void)h2.service.retry({
            .project_id = project_id,
            .analysis_id = analysis_id,
            .expected_parent_attempt_id = second.attempt_id,
            .idempotency_key = "illegal-retry",
            .payload_digest = payload_digest,
            .priority = domain::JobPriority::normal,
        });
    });
}

void persistence_failure_test() {
    Harness h;
    h.connection.execute(
        "CREATE TRIGGER reject_attempt_reservation BEFORE INSERT ON cohort_analysis_attempts "
        "BEGIN SELECT RAISE(ABORT,'simulated durable storage failure'); END;"
    );
    rejects<infrastructure::sqlite::SqliteError>([&] {
        (void)h.service.submit(submit_request());
    });
    check(h.submitter.calls == 0,
          "failed durable reservation must precede scheduler handoff");
    check(h.executions.list_attempts(project_id, analysis_id).empty(),
          "failed durable reservation leaves no attempt row");

    Harness h2;
    const auto started = h2.service.submit(submit_request());
    auto job = h2.job_repository.find_by_id(*started.job_id);
    check(job.has_value(), "persistence failure completion job exists");
    (void)h2.jobs.transition(
        job->id(), domain::JobStatus::preparing, 0.0, std::nullopt
    );
    (void)h2.jobs.transition(
        job->id(), domain::JobStatus::running, 0.5, std::string{"analysis"}
    );
    (void)h2.jobs.transition(
        job->id(), domain::JobStatus::completed, 1.0, std::nullopt
    );
    h2.connection.execute("INSERT INTO managed_files(id) VALUES('manifest-fail');");
    const domain::ManagedFile manifest{
        "manifest-fail",
        "cohort-result-manifest.json",
        domain::StorageMode::generated_output,
        std::nullopt,
        std::string{"/managed/project/outputs/manifest-fail.json"},
        std::string{"outputs/manifest-fail.json"},
        "json",
        128,
        std::nullopt,
        std::string{"sha256"},
        std::string{manifest_hash},
        "2026-10-02T15:33:00Z",
        "2026-10-02T15:33:00Z"
    };
    check(h2.files.add_generated_output(
        manifest,
        application::GeneratedOutputProvenance{
            .job_id = *started.job_id,
            .step_id = "analysis",
            .output_port = "manifest",
            .plugin_id = "org.biocore.cohort",
            .plugin_version = "1.0.0",
            .module_id = "org.biocore.cohort.analysis",
            .file_type = "json",
            .relative_project_path = "outputs/manifest-fail.json",
            .step_progress = 1.0,
            .registered_at_utc = "2026-10-02T15:33:00Z",
        }),
        "failure manifest fixture registered"
    );
    h2.connection.execute(
        "CREATE TRIGGER reject_completion_commit BEFORE UPDATE ON cohort_analysis_attempts "
        "WHEN NEW.state='completed' "
        "BEGIN SELECT RAISE(ABORT,'simulated durable completion failure'); END;"
    );
    rejects<infrastructure::sqlite::SqliteError>([&] {
        (void)h2.service.complete({
            .project_id = project_id,
            .analysis_id = analysis_id,
            .attempt_id = started.attempt_id,
            .result_manifest_file_id = "manifest-fail",
            .result_manifest_sha256 = manifest_hash,
        });
    });
    const auto persisted = h2.executions.find_attempt(
        project_id, analysis_id, started.attempt_id
    );
    check(persisted.has_value() &&
          persisted->state != application::CohortExecutionState::completed &&
          !persisted->result_manifest_file_id.has_value(),
          "failed completion commit cannot create false completion");
}

void migration_test() {
    infrastructure::sqlite::SqliteConnection connection{":memory:"};
    bootstrap_v17(connection);
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    check(migrations.current_version() == 18, "migration version is 18");

    infrastructure::sqlite::SqliteCohortExecutionStore store{connection};
    auto first = attempt(1, "immutable-attempt", std::nullopt, "immutable-key");
    check(store.reserve_initial(first) ==
          application::CohortExecutionReserveResult::created,
          "migration ledger accepts valid reservation");

    rejects<infrastructure::sqlite::SqliteError>([&] {
        connection.execute(
            "UPDATE cohort_analysis_attempts SET snapshot_digest="
            "'eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee'"
            " WHERE attempt_id='immutable-attempt';"
        );
    });
    rejects<infrastructure::sqlite::SqliteError>([&] {
        connection.execute(
            "DELETE FROM cohort_analysis_attempts "
            "WHERE attempt_id='immutable-attempt';"
        );
    });
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::invalid_argument{"mode required"};
        const std::string mode{argv[1]};
        if (mode == "store") store_test();
        else if (mode == "duplicate") duplicate_test();
        else if (mode == "drift") drift_test();
        else if (mode == "recovery") recovery_test();
        else if (mode == "retry") retry_test();
        else if (mode == "cancel") cancel_test();
        else if (mode == "completion") completion_test();
        else if (mode == "persistence-failure") persistence_failure_test();
        else if (mode == "migration") migration_test();
        else throw std::invalid_argument{"unknown mode"};
        std::cout << "PASS " << mode << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
