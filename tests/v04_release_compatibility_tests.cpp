#include <sqlite3.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>

#include "biocore/infrastructure/filesystem_workflow_template_catalog.hpp"
#include "biocore/infrastructure/sqlite/project_database_guard.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"

namespace {

using biocore::infrastructure::FilesystemWorkflowTemplateCatalog;
using biocore::infrastructure::sqlite::ProjectDatabaseGuard;
using biocore::infrastructure::sqlite::ProjectMigrationRunner;
using biocore::infrastructure::sqlite::SqliteConnection;
using biocore::infrastructure::sqlite::latest_project_schema_version;
namespace fs = std::filesystem;

[[nodiscard]] bool table_exists(
    SqliteConnection& connection,
    const std::string_view name
) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(
            connection.native_handle(),
            "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name=?;",
            -1,
            &statement,
            nullptr
        ) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(
        statement,
        1,
        name.data(),
        static_cast<int>(name.size()),
        SQLITE_TRANSIENT
    );
    const int step = sqlite3_step(statement);
    const bool exists =
        step == SQLITE_ROW && sqlite3_column_int(statement, 0) == 1;
    sqlite3_finalize(statement);
    return exists;
}

[[nodiscard]] int scalar_int(
    SqliteConnection& connection,
    const char* sql
) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(
            connection.native_handle(), sql, -1, &statement, nullptr
        ) != SQLITE_OK) {
        return -1;
    }
    const int step = sqlite3_step(statement);
    const int value =
        step == SQLITE_ROW ? sqlite3_column_int(statement, 0) : -1;
    sqlite3_finalize(statement);
    return value;
}

[[nodiscard]] bool v03_schema_upgrade_contract() {
    SqliteConnection connection{fs::path{":memory:"}};
    ProjectMigrationRunner initial{connection};
    initial.apply_pending();
    if (initial.current_version() != latest_project_schema_version ||
        latest_project_schema_version != 9) {
        return false;
    }

    connection.execute(R"sql(
        INSERT INTO jobs(
            id, status, priority, progress, created_at_utc, updated_at_utc
        ) VALUES (
            'v03-retained-job', 'queued', 'normal', 0.25,
            '2026-09-15T12:00:00Z', '2026-09-15T12:01:00Z'
        );

        DROP TRIGGER IF EXISTS workflow_states_prevent_branch_schema_clear;
        DROP TRIGGER IF EXISTS workflow_branch_decisions_require_snapshot_insert;
        DROP TABLE workflow_branch_decisions;
        DROP TABLE workflow_checkpoint_artifacts;
        DROP TABLE workflow_node_checkpoints;
        DROP TABLE workflow_states;
        DELETE FROM schema_migrations WHERE version = 9;
    )sql");

    ProjectMigrationRunner legacy{connection};
    if (legacy.current_version() != 8 ||
        table_exists(connection, "workflow_states")) {
        return false;
    }

    ProjectDatabaseGuard before{connection};
    before.validate_before_migration();

    legacy.apply_pending();

    ProjectDatabaseGuard after{connection};
    after.validate_current_schema();

    return legacy.current_version() == 9 &&
           table_exists(connection, "workflow_states") &&
           table_exists(connection, "workflow_node_checkpoints") &&
           table_exists(connection, "workflow_checkpoint_artifacts") &&
           table_exists(connection, "workflow_branch_decisions") &&
           scalar_int(
               connection,
               "SELECT COUNT(*) FROM jobs "
               "WHERE id='v03-retained-job' AND status='queued' "
               "AND progress=0.25 AND attempt_number=1;"
           ) == 1 &&
           scalar_int(
               connection,
               "SELECT COUNT(*) FROM workflow_states;"
           ) == 0 &&
           scalar_int(
               connection,
               "SELECT COUNT(*) FROM schema_migrations WHERE version=9 "
               "AND name='persist_workflow_state_and_recovery';"
           ) == 1;
}

[[nodiscard]] bool v03_asset_compatibility_contract() {
    const fs::path pipeline_root =
        fs::path{BIOCORE_SOURCE_ROOT} / "pipelines";

    constexpr std::array<std::string_view, 12> retained_pipelines{{
        "alignment-paired.biocore-pipeline.json",
        "alignment-qc.biocore-pipeline.json",
        "alignment-single.biocore-pipeline.json",
        "demo.biocore-pipeline.json",
        "fasta-qc.biocore-pipeline.json",
        "fastq-paired-qc.biocore-pipeline.json",
        "fastq-qc.biocore-pipeline.json",
        "fastq-trim-paired.biocore-pipeline.json",
        "fastq-trim-single.biocore-pipeline.json",
        "variant-annotation-local.biocore-pipeline.json",
        "variant-call-snv.biocore-pipeline.json",
        "vcf-qc-filter.biocore-pipeline.json",
    }};

    std::set<std::string, std::less<>> actual_pipelines;
    std::size_t template_count = 0U;
    for (const auto& entry : fs::directory_iterator{pipeline_root}) {
        if (!entry.is_regular_file()) continue;
        const std::string name = entry.path().filename().string();
        if (name.ends_with(".biocore-pipeline.json")) {
            actual_pipelines.emplace(name);
        } else if (name.ends_with(".workflow-template.json")) {
            ++template_count;
        }
    }

    if (actual_pipelines.size() != retained_pipelines.size() ||
        template_count != 1U) {
        return false;
    }
    for (const std::string_view name : retained_pipelines) {
        if (!actual_pipelines.contains(name)) return false;
    }

    FilesystemWorkflowTemplateCatalog catalog{fs::canonical(pipeline_root)};
    const auto workflow_template = catalog.find(
        "org.biocore.template.fastq-trim-qc", "1.0.0"
    );
    return workflow_template.has_value() &&
           workflow_template->blueprint().nodes().size() == 2U &&
           workflow_template->blueprint().edges().size() == 1U;
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    const std::string_view mode{argv[1]};
    if (mode == "v03-schema-upgrade") {
        return v03_schema_upgrade_contract() ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (mode == "v03-assets") {
        return v03_asset_compatibility_contract() ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    return EXIT_FAILURE;
}
