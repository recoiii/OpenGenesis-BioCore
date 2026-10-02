#include <sqlite3.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/batch_execution_service.hpp"
#include "biocore/application/execution_plan.hpp"
#include "biocore/application/i_execution_plan_store.hpp"
#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_input_file_storage.hpp"
#include "biocore/application/i_plugin_registry.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/i_worker_supervisor.hpp"
#include "biocore/application/job_scheduler.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/domain/job_failure.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/project.hpp"
#include "biocore/domain/storage_mode.hpp"
#include "biocore/infrastructure/sqlite/project_database_guard.hpp"
#include "biocore/infrastructure/sqlite/project_database_initializer.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_batch_execution_store.hpp"
#include "biocore/infrastructure/sqlite/sqlite_batch_plan_store.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"
#include "biocore/infrastructure/sqlite/sqlite_job_repository.hpp"
#include "biocore/infrastructure/sqlite/sqlite_managed_file_repository.hpp"
#include "biocore/infrastructure/sqlite/sqlite_prepared_job_store.hpp"

namespace {

using namespace biocore;
using namespace biocore::infrastructure::sqlite;

constexpr const char* stamp = "2026-09-27T21:00:00Z";
constexpr const char* project_id = "p-001";

void check(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

template<class Exception, class Function>
void rejects(Function function) {
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error{"Expected rejection"};
}

class SequenceIds final : public application::IIdGenerator {
public:
    explicit SequenceIds(std::vector<std::string> values)
        : values_{std::move(values)} {}

    std::string generate() override {
        if (index_ >= values_.size()) {
            throw std::runtime_error{"ID sequence exhausted"};
        }
        return values_[index_++];
    }

private:
    std::vector<std::string> values_;
    std::size_t index_{0U};
};

class Clock final : public application::IUtcClock {
public:
    std::string now_utc_iso8601() override {
        return "2026-09-27T21:00:" + two_digit(counter_++) + "Z";
    }

private:
    static std::string two_digit(const int value) {
        const int bounded = value % 60;
        return bounded < 10 ? "0" + std::to_string(bounded)
                            : std::to_string(bounded);
    }
    int counter_{0};
};

class InputStorage final : public application::IInputFileStorage {
public:
    std::unique_ptr<application::IInputFileImportTransaction> prepare_managed_copy(
        std::string_view, std::string_view
    ) override {
        throw std::logic_error{"unused"};
    }

    bool begin_browser_upload(std::string_view, std::string_view) override {
        throw std::logic_error{"unused"};
    }

    std::uint64_t append_browser_upload(
        std::string_view, std::uint64_t, std::string_view
    ) override {
        throw std::logic_error{"unused"};
    }

    std::unique_ptr<application::IInputFileImportTransaction>
    prepare_browser_upload_commit(
        std::string_view, std::string_view
    ) override {
        throw std::logic_error{"unused"};
    }

    void discard_browser_upload(std::string_view) noexcept override {}

    application::ManagedFileIntegrityResult verify_managed_file(
        const domain::ManagedFile& file
    ) const override {
        return {
            .status = application::ManagedFileIntegrityStatus::verified,
            .expected_size_bytes = file.size_bytes(),
            .observed_size_bytes = file.size_bytes(),
            .expected_sha256 = file.checksum_value(),
            .observed_sha256 = file.checksum_value(),
        };
    }
};

class PluginRegistry final : public application::IPluginRegistry {
public:
    std::optional<application::ResolvedPluginModule> find_module(
        const std::string_view module_id
    ) const override {
        if (module_id != "org.test.module") return std::nullopt;
        return application::ResolvedPluginModule{
            .plugin_id = "org.test",
            .plugin_version = "1.0.0",
            .plugin_manifest_version = 2U,
            .plugin_api_version = "1.0",
            .module_id = "org.test.module",
            .module_type = domain::PluginModuleType::process,
            .plugin_root_path = "/plugins/org.test",
            .executable_path = "/plugins/org.test/bin/module",
            .parameters = {},
            .inputs = {
                domain::PluginInputPortDefinition{"reads", true, {"fastq"}},
            },
            .outputs = {
                domain::PluginOutputPortDefinition{"report", "json"},
            },
        };
    }

    std::vector<application::RegisteredPlugin> list_plugins() const override {
        return {};
    }
};

class ExecutionPlanStore final : public application::IExecutionPlanStore {
public:
    std::string store(const application::ExecutionPlan& plan) override {
        plans.push_back(plan);
        return "/tmp/" + std::string{plan.job_id()} + ".json";
    }

    void discard(const std::string_view snapshot_path) override {
        discarded.emplace_back(snapshot_path);
    }

    std::vector<application::ExecutionPlan> plans;
    std::vector<std::string> discarded;
};

class Supervisor final : public application::IWorkerSupervisor {
public:
    void launch(const application::WorkerLaunchRequest& request) override {
        launches.push_back(request);
    }

    std::vector<application::WorkerLaunchRequest> launches;
};

class Temp final {
public:
    Temp()
        : root{std::filesystem::temp_directory_path() /
            ("biocore-batch-083-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()
            ))} {
        std::filesystem::create_directory(root);
    }
    ~Temp() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    std::filesystem::path root;
};

struct Harness final {
    SqliteConnection connection{":memory:"};
    SqliteBatchPlanStore plans{connection};
    SqliteBatchExecutionStore executions{connection};
    SqliteManagedFileRepository files{connection};
    SqliteJobRepository job_repository{connection};
    SqlitePreparedJobStore prepared_jobs{connection};
    SequenceIds ids;
    Clock clock;
    application::JobService jobs{job_repository, ids, clock};
    InputStorage input_storage;
    PluginRegistry plugins;
    ExecutionPlanStore execution_plans;
    Supervisor supervisor;
    application::JobScheduler scheduler{
        jobs, prepared_jobs, supervisor, 8U, &executions
    };
    application::BatchExecutionService service{
        plans, executions, plugins, files, input_storage, execution_plans,
        ids, clock, jobs, scheduler
    };

    explicit Harness(std::vector<std::string> id_values)
        : ids{std::move(id_values)} {
        ProjectDatabaseInitializer{connection}.initialize(
            domain::Project{project_id, "Research", "/local/project", stamp, stamp}
        );
    }
};

domain::ManagedFile managed_file(
    std::string id,
    const char checksum_character
) {
    const std::string relative = "inputs/" + id + ".fastq";
    return domain::ManagedFile{
        id,
        id + ".fastq",
        domain::StorageMode::managed_copy,
        std::string{"/original/"} + id + ".fastq",
        std::string{"/project/"} + relative,
        relative,
        "fastq",
        4,
        std::nullopt,
        std::string{"sha256"},
        std::string(64U, checksum_character),
        stamp,
        stamp,
    };
}

application::BatchPlanNodeSnapshot node_for(
    const std::string& file_id,
    const char checksum_character
) {
    return application::BatchPlanNodeSnapshot{
        .node_id = "qc",
        .module_id = "org.test.module",
        .plugin_version = "1.0.0",
        .parameters = {},
        .inputs = {
            application::BatchPlanInputSnapshot{
                .port_name = "reads",
                .artifact_type = "fastq",
                .source_kind = application::BatchPlanInputSourceKind::managed_file,
                .source_id = file_id,
                .source_port = "",
                .managed_file = application::BatchPlanManagedFileSnapshot{
                    .role = application::BatchInputFileRole::primary,
                    .file_id = file_id,
                    .file_type = "fastq",
                    .size_bytes = 4,
                    .sha256 = std::string(64U, checksum_character),
                },
            },
        },
        .outputs = {
            application::BatchPlanOutputSnapshot{
                .port_name = "report",
                .artifact_type = "json",
            },
        },
    };
}

application::ApprovedBatchPlan seed_plan(
    Harness& harness,
    const std::size_t sample_count
) {
    application::ApprovedBatchPlan plan{
        .plan_id = "batch.plan.083",
        .project_id = project_id,
        .template_id = "org.test.template",
        .template_version = "1.0.0",
        .approved_at_utc = stamp,
        .samples = {},
    };

    for (std::size_t index = 0U; index < sample_count; ++index) {
        const std::string sample = "S" + std::to_string(index + 1U);
        const std::string file_id = "file-" + std::to_string(index + 1U);
        const char checksum = static_cast<char>('a' + static_cast<int>(index));
        check(harness.files.add(managed_file(file_id, checksum)), "file seed failed");
        plan.samples.push_back(application::ApprovedBatchSamplePlan{
            .sample_id = sample,
            .disposition = application::BatchPlanSampleDisposition::included,
            .workflow_id = "batch.plan.083.sample.s" + std::to_string(index + 1U),
            .nodes = {node_for(file_id, checksum)},
        });
    }
    check(harness.plans.add(plan), "plan seed failed");
    return plan;
}

std::int64_t scalar(
    SqliteConnection& connection,
    const char* sql
) {
    sqlite3_stmt* statement = nullptr;
    check(
        sqlite3_prepare_v2(connection.native_handle(), sql, -1, &statement, nullptr)
            == SQLITE_OK,
        "scalar prepare"
    );
    check(sqlite3_step(statement) == SQLITE_ROW, "scalar row");
    const auto value = sqlite3_column_int64(statement, 0);
    sqlite3_finalize(statement);
    return value;
}

void submit_contract() {
    Harness harness{{"job-a", "job-b", "job-c", "job-d"}};
    seed_plan(harness, 2U);

    const auto first = harness.service.submit("batch.plan.083", 2U);
    check(first.samples.size() == 2U, "batch did not submit every included sample");
    check(first.samples[0].job_id == "job-a" &&
          first.samples[1].job_id == "job-b", "unexpected job identities");
    check(scalar(harness.connection, "SELECT COUNT(*) FROM jobs;") == 2,
          "wrong prepared job count");
    check(scalar(harness.connection, "SELECT COUNT(*) FROM batch_execution_jobs;") == 2,
          "wrong execution ledger count");
    check(scalar(harness.connection, "SELECT COUNT(*) FROM batch_execution_attempts;") == 2,
          "initial attempt lineage was not created");

    const auto second = harness.service.submit("batch.plan.083", 2U);
    check(second.samples[0].job_id == first.samples[0].job_id &&
          second.samples[1].job_id == first.samples[1].job_id,
          "repeated submit created different runs");
    check(scalar(harness.connection, "SELECT COUNT(*) FROM jobs;") == 2,
          "repeated submit created duplicate jobs");
    check(harness.execution_plans.plans.size() == 2U,
          "repeated submit regenerated execution snapshots");

    rejects<std::invalid_argument>([&] {
        static_cast<void>(harness.service.submit("batch.plan.083", 1U));
    });
}

void quota_contract() {
    Harness harness{{"job-a", "job-b", "job-c"}};
    seed_plan(harness, 3U);
    static_cast<void>(harness.service.submit("batch.plan.083", 1U));

    const auto first = harness.scheduler.tick();
    check(first.launched_job_ids.size() == 1U, "batch quota did not limit first tick");
    check(first.quota_deferred_job_ids.size() == 2U,
          "batch quota did not expose deferred jobs");
    check(harness.supervisor.launches.size() == 1U,
          "worker launch count ignored batch quota");

    const auto second = harness.scheduler.tick();
    check(second.launched_job_ids.empty(), "batch quota launched while one batch job active");
    check(second.quota_deferred_job_ids.size() == 2U,
          "remaining queued batch jobs were not quota-deferred");
}

void output_contract() {
    Harness harness{{"job-a", "job-b"}};
    seed_plan(harness, 2U);
    static_cast<void>(harness.service.submit("batch.plan.083", 2U));

    check(harness.execution_plans.plans.size() == 2U, "execution plans missing");
    const auto& first = harness.execution_plans.plans[0];
    const auto& second = harness.execution_plans.plans[1];
    check(first.steps().size() == 1U && second.steps().size() == 1U,
          "unexpected materialized step count");
    const std::string path_a = first.steps()[0].outputs[0].relative_project_path;
    const std::string path_b = second.steps()[0].outputs[0].relative_project_path;
    check(path_a != path_b, "sample output paths collided");
    check(path_a.find("job-a--qc--report.out") != std::string::npos &&
          path_b.find("job-b--qc--report.out") != std::string::npos,
          "output paths are not namespaced by job identity");
}

void fail_job(
    application::JobService& jobs,
    const std::string_view job_id
) {
    auto job = jobs.find_by_id(job_id);
    check(job.has_value(), "job missing before failure");
    static_cast<void>(jobs.transition(
        job_id, domain::JobStatus::preparing, job->progress(), std::nullopt
    ));
    job = jobs.find_by_id(job_id);
    static_cast<void>(jobs.transition(
        job_id,
        domain::JobStatus::failed,
        job->progress(),
        std::nullopt,
        application::JobFailureContext{
            .kind = domain::JobFailureKind::unspecified_terminal_failure,
            .message = "synthetic per-sample failure",
            .exit_code = std::nullopt,
            .worker_timestamp_utc = std::nullopt,
        }
    ));
}

void complete_job(
    application::JobService& jobs,
    const std::string_view job_id
) {
    auto job = jobs.find_by_id(job_id);
    check(job.has_value(), "job missing before completion");
    static_cast<void>(jobs.transition(
        job_id, domain::JobStatus::preparing, 0.0, std::nullopt
    ));
    static_cast<void>(jobs.transition(
        job_id, domain::JobStatus::running, 0.1, std::string{"qc"}
    ));
    static_cast<void>(jobs.transition(
        job_id, domain::JobStatus::completed, 1.0, std::nullopt
    ));
}

void partial_failure_contract() {
    Harness harness{{"job-a", "job-b"}};
    seed_plan(harness, 2U);
    static_cast<void>(harness.service.submit("batch.plan.083", 2U));

    fail_job(harness.jobs, "job-a");
    auto snapshot = harness.service.find("batch.plan.083");
    check(snapshot.has_value() &&
          snapshot->failed_count == 1U &&
          snapshot->active_count == 1U &&
          snapshot->state == application::BatchExecutionState::active,
          "one sample failure corrupted sibling execution state");
    check(harness.jobs.find_by_id("job-b")->status() == domain::JobStatus::queued,
          "sibling sample was changed by another sample failure");

    complete_job(harness.jobs, "job-b");
    snapshot = harness.service.find("batch.plan.083");
    check(snapshot.has_value() &&
          snapshot->failed_count == 1U &&
          snapshot->completed_count == 1U &&
          snapshot->state == application::BatchExecutionState::partial_failure,
          "partial failure terminal state is incorrect");
}

void cancel_contract() {
    Harness harness{{"job-a", "job-b"}};
    seed_plan(harness, 2U);
    static_cast<void>(harness.service.submit("batch.plan.083", 2U));

    static_cast<void>(harness.jobs.transition(
        "job-a", domain::JobStatus::preparing, 0.0, std::nullopt
    ));
    auto cancelled = harness.service.cancel("batch.plan.083");
    check(cancelled.cancellation_requested, "batch cancellation flag not persisted");
    check(harness.jobs.find_by_id("job-a")->status() == domain::JobStatus::cancelling,
          "active job did not enter cancelling");
    check(harness.jobs.find_by_id("job-b")->status() == domain::JobStatus::cancelled,
          "queued job was not cancelled immediately");

    cancelled = harness.service.cancel("batch.plan.083");
    check(cancelled.cancellation_requested, "repeated batch cancel is not idempotent");
}

void persistence_contract() {
    Harness harness{{"job-a"}};
    seed_plan(harness, 1U);
    static_cast<void>(harness.service.submit("batch.plan.083", 1U));

    const auto execution = harness.executions.find("batch.plan.083");
    check(execution.has_value() && execution->jobs.size() == 1U,
          "sealed execution ledger did not round-trip");
    const auto attempts = harness.executions.list_attempts("batch.plan.083");
    check(attempts.size() == 1U && attempts[0].attempt_number == 1 &&
          attempts[0].mode == application::BatchAttemptMode::initial &&
          !attempts[0].parent_job_id.has_value() &&
          attempts[0].execution_node_ids == std::vector<std::string>{"qc"},
          "initial attempt lineage did not round-trip");
    const auto quota = harness.executions.quota_for_job("job-a");
    check(quota.has_value() &&
          quota->group_id == "batch.plan.083" &&
          quota->maximum_concurrent_jobs == 1U,
          "scheduler quota ledger did not round-trip");

    rejects<SqliteError>([&] {
        harness.connection.execute(
            "UPDATE batch_executions SET maximum_concurrent_jobs=2 "
            "WHERE plan_id='batch.plan.083';"
        );
    });
    rejects<SqliteError>([&] {
        harness.connection.execute(
            "DELETE FROM batch_executions WHERE plan_id='batch.plan.083';"
        );
    });
    rejects<SqliteError>([&] {
        harness.connection.execute(
            "INSERT INTO batch_execution_jobs(plan_id,sample_id,ordinal,job_id) "
            "VALUES('batch.plan.083','S1',99,'job-extra');"
        );
    });
}

void load_v13(SqliteConnection& connection) {
    std::ifstream input{
        std::filesystem::path{BIOCORE_SOURCE_ROOT} /
        "tests/fixtures/project-schema-v13.sql"
    };
    check(input.good(), "v13 fixture missing");
    const std::string sql{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}
    };
    connection.execute(sql);
    connection.execute(R"sql(
        INSERT INTO project_metadata(
            singleton,project_id,name,root_path,created_at_utc,updated_at_utc,
            research_description,research_organism,research_revision,
            research_updated_at_utc
        ) VALUES(
            1,'p-001','Research','/local/project','t','t','','',0,'t'
        );

        INSERT INTO batch_plans(
            plan_id,project_id,template_id,template_version,approved_at_utc,sealed
        ) VALUES('legacy.plan','p-001','org.test.template','1.0.0','t',0);

        INSERT INTO batch_plan_samples(
            plan_id,sample_id,ordinal,disposition,workflow_id
        ) VALUES('legacy.plan','S1',0,'included','legacy.plan.sample.s1');

        INSERT INTO batch_plan_nodes(
            plan_id,sample_id,ordinal,node_id,module_id,plugin_version
        ) VALUES('legacy.plan','S1',0,'qc','org.test.module','1.0.0');

        UPDATE batch_plans SET sealed=1 WHERE plan_id='legacy.plan';
    )sql");
}

void migration_contract() {
    SqliteConnection connection{":memory:"};
    load_v13(connection);
    ProjectMigrationRunner migrations{connection};
    check(migrations.current_version() == 13, "fixture is not v13");
    migrations.apply_pending();
    ProjectDatabaseGuard{connection}.validate_current_schema();
    check(migrations.current_version() == latest_project_schema_version, "latest migration missing");
    check(scalar(connection, "SELECT COUNT(*) FROM batch_plans;") == 1,
          "v13 approved plan was not preserved");
    check(scalar(connection, "SELECT COUNT(*) FROM batch_executions;") == 0,
          "migration invented batch execution state");
}

void load_v14(SqliteConnection& connection) {
    std::ifstream input{
        std::filesystem::path{BIOCORE_SOURCE_ROOT} /
        "tests/fixtures/project-schema-v14.sql"
    };
    check(input.good(), "v14 fixture missing");
    const std::string sql{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}
    };
    connection.execute(sql);
    connection.execute(R"sql(
        INSERT INTO project_metadata(
            singleton,project_id,name,root_path,created_at_utc,updated_at_utc,
            research_description,research_organism,research_revision,
            research_updated_at_utc
        ) VALUES(1,'p-001','Research','/local/project','t','t','','',0,'t');

        INSERT INTO batch_plans(
            plan_id,project_id,template_id,template_version,approved_at_utc,sealed
        ) VALUES('legacy.plan','p-001','org.test.template','1.0.0','t',0);
        INSERT INTO batch_plan_samples(
            plan_id,sample_id,ordinal,disposition,workflow_id
        ) VALUES('legacy.plan','S1',0,'included','legacy.plan.sample.s1');
        INSERT INTO batch_plan_nodes(
            plan_id,sample_id,ordinal,node_id,module_id,plugin_version
        ) VALUES('legacy.plan','S1',0,'qc','org.test.module','1.0.0');
        UPDATE batch_plans SET sealed=1 WHERE plan_id='legacy.plan';

        INSERT INTO jobs(
            id,analysis_id,pipeline_id,pipeline_version,status,priority,progress,
            active_step_id,created_at_utc,updated_at_utc,started_at_utc,
            finished_at_utc,revision,attempt_number
        ) VALUES(
            'legacy-job','legacy.plan','org.biocore.batch.workflow','1.0.0',
            'completed','normal',1.0,NULL,'t','t','t','t',1,1
        );
        INSERT INTO batch_executions(
            plan_id,maximum_concurrent_jobs,cancellation_requested,
            submitted_at_utc,updated_at_utc,sealed
        ) VALUES('legacy.plan',1,0,'t','t',0);
        INSERT INTO batch_execution_jobs(plan_id,sample_id,ordinal,job_id)
        VALUES('legacy.plan','S1',0,'legacy-job');
        UPDATE batch_executions SET sealed=1 WHERE plan_id='legacy.plan';
    )sql");
}

void rollback_contract() {
    SqliteConnection connection{":memory:"};
    load_v14(connection);
    check(ProjectMigrationRunner{connection}.current_version() == 14,
          "fixture is not v14");
    connection.execute(
        "CREATE TRIGGER reject_v15 BEFORE INSERT ON schema_migrations "
        "WHEN NEW.version=15 BEGIN SELECT RAISE(ABORT,'injected v15 failure'); END;"
    );
    rejects<SqliteError>([&] {
        ProjectMigrationRunner{connection}.apply_pending();
    });
    check(ProjectMigrationRunner{connection}.current_version() == 14,
          "failed v15 migration advanced version");
    check(scalar(
        connection,
        "SELECT COUNT(*) FROM sqlite_master "
        "WHERE type='table' AND name='batch_execution_attempts';"
    ) == 0, "failed v15 migration left attempt table");
    check(scalar(connection, "SELECT COUNT(*) FROM batch_executions;") == 1,
          "failed v15 migration damaged v14 execution state");

    connection.execute("DROP TRIGGER reject_v15;");
    ProjectMigrationRunner{connection}.apply_pending();
    ProjectDatabaseGuard{connection}.validate_current_schema();
    check(scalar(connection, "SELECT COUNT(*) FROM batch_execution_attempts;") == 1,
          "v15 migration did not backfill initial attempt lineage");
    check(scalar(connection, "SELECT COUNT(*) FROM batch_execution_attempt_nodes;") == 1,
          "v15 migration did not backfill initial attempt nodes");
}


}  // namespace

int main(const int argc, char** argv) {
    try {
        if (argc != 2) return EXIT_FAILURE;
        const std::string_view mode{argv[1]};
        if (mode == "submit") submit_contract();
        else if (mode == "quota") quota_contract();
        else if (mode == "outputs") output_contract();
        else if (mode == "partial-failure") partial_failure_contract();
        else if (mode == "cancel") cancel_contract();
        else if (mode == "persistence") persistence_contract();
        else if (mode == "migration") migration_contract();
        else if (mode == "rollback") rollback_contract();
        else return EXIT_FAILURE;

        std::cout << mode << " PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
