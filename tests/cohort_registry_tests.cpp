#include <sqlite3.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/i_cohort_registry_store.hpp"
#include "biocore/domain/cohort.hpp"
#include "biocore/domain/project.hpp"
#include "biocore/domain/project_sample.hpp"
#include "biocore/infrastructure/sqlite/project_database_initializer.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_cohort_registry_store.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"
#include "biocore/infrastructure/sqlite/sqlite_project_sample_store.hpp"

namespace {
using namespace biocore;
using namespace biocore::infrastructure::sqlite;

constexpr const char* stamp1 = "2026-10-02T10:00:00Z";
constexpr const char* stamp2 = "2026-10-02T11:00:00Z";
constexpr const char* stamp3 = "2026-10-02T12:00:00Z";

void check(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

template<class Exception, class Function>
void rejects(Function function) {
    try { function(); } catch (const Exception&) { return; }
    throw std::runtime_error{"Expected rejection"};
}

int scalar_integer(SqliteConnection& connection, const char* sql) {
    sqlite3_stmt* statement = nullptr;
    check(sqlite3_prepare_v2(connection.native_handle(), sql, -1, &statement, nullptr) == SQLITE_OK,
          "prepare scalar");
    const int result = sqlite3_step(statement);
    check(result == SQLITE_ROW, "scalar row");
    const int value = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);
    return value;
}

void initialize(SqliteConnection& connection) {
    ProjectDatabaseInitializer{connection}.initialize(
        domain::Project{"p-001", "Cohort project", "/local/cohort", stamp1, stamp1});
}

void seed_samples(SqliteConnection& connection) {
    SqliteProjectSampleStore samples{connection};
    const std::vector<domain::ProjectSample> rows{
        {"p-001", "001", "Örnek α", "legacy-case"},
        {"p-001", "ctrl-01", "Control one", ""},
        {"p-001", "EX", "Excluded", "metadata-only"},
        {"p-001", "FAIL", "Injected failure", ""},
    };
    check(samples.add_batch("p-001", rows) == application::SampleBatchAddResult::added,
          "sample seed failed");
}

std::vector<domain::CohortMemberDraft> initial_members() {
    return {
        {"001", "person-001", domain::CohortGroup::case_sample, true, ""},
        {"ctrl-01", "person-ctrl-01", domain::CohortGroup::control, true, ""},
        {"EX", "person-ex", domain::CohortGroup::unassigned, false, "QC exclusion"},
    };
}

void token_contract() {
    check(domain::to_string(domain::CohortGroup::case_sample) == "case", "case token");
    check(domain::to_string(domain::CohortGroup::control) == "control", "control token");
    check(domain::to_string(domain::CohortGroup::unassigned) == "unassigned",
          "unassigned token");
    check(domain::cohort_group_from_string("case") == domain::CohortGroup::case_sample,
          "case parse");
    check(domain::cohort_group_from_string("control") == domain::CohortGroup::control,
          "control parse");
    check(domain::cohort_group_from_string("unassigned") == domain::CohortGroup::unassigned,
          "unassigned parse");
    check(!domain::cohort_group_from_string("legacy-case"), "free-text metadata became phenotype");

    domain::CohortMemberDraft default_member;
    check(default_member.group == domain::CohortGroup::unassigned,
          "missing group defaulted to control");
}

void persistence_contract() {
    SqliteConnection connection{":memory:"};
    initialize(connection);
    seed_samples(connection);
    SqliteCohortRegistryStore store{connection};

    const auto members = initial_members();
    check(store.create("p-001", "cohort-α", "Discovery cohort", stamp1, members) ==
              application::CohortWriteResult::created,
          "cohort creation failed");

    const auto definition = store.get("p-001", "cohort-α");
    check(definition && definition->current_revision == 1 &&
              definition->name == "Discovery cohort",
          "definition round-trip failed");

    const auto revision = store.get_revision("p-001", "cohort-α", 1);
    check(revision && revision->members.size() == 3U, "revision round-trip failed");
    check(revision->members[0].sample_id == "001" &&
              revision->members[0].display_name == "Örnek α" &&
              revision->members[0].group == domain::CohortGroup::case_sample,
          "Unicode/leading-zero member changed");
    check(!revision->members[2].included &&
              revision->members[2].group == domain::CohortGroup::unassigned &&
              revision->members[2].exclusion_reason == "QC exclusion",
          "excluded unassigned member changed");

    const std::vector<domain::CohortMemberDraft> duplicate{
        {"001", "bio-1", domain::CohortGroup::case_sample, true, ""},
        {"001", "bio-2", domain::CohortGroup::control, true, ""},
    };
    check(store.create("p-001", "duplicate", "Duplicate", stamp1, duplicate) ==
              application::CohortWriteResult::duplicate_sample,
          "duplicate sample accepted");
    check(!store.get("p-001", "duplicate"), "duplicate cohort partially persisted");

    const std::vector<domain::CohortMemberDraft> missing{
        {"not-in-project", "bio-x", domain::CohortGroup::case_sample, true, ""},
    };
    check(store.create("p-001", "wrong-project", "Wrong project", stamp1, missing) ==
              application::CohortWriteResult::sample_not_found,
          "unknown/wrong-project sample accepted");
    check(!store.get("p-001", "wrong-project"), "failed cohort partially persisted");

    const std::vector<domain::CohortMemberDraft> invalid_exclusion{
        {"001", "bio-1", domain::CohortGroup::case_sample, false, ""},
    };
    check(store.create("p-001", "bad-exclusion", "Bad exclusion", stamp1, invalid_exclusion) ==
              application::CohortWriteResult::invalid_request,
          "excluded member without reason accepted");

    rejects<SqliteError>([&] {
        connection.execute(
            "UPDATE cohort_revision_members SET group_token='control' "
            "WHERE project_id='p-001' AND cohort_id='cohort-α' AND revision=1 AND sample_id='001';");
    });
    rejects<SqliteError>([&] {
        connection.execute(
            "DELETE FROM project_samples WHERE project_id='p-001' AND sample_id='001';");
    });
}

void revision_contract() {
    SqliteConnection connection{":memory:"};
    initialize(connection);
    seed_samples(connection);
    SqliteCohortRegistryStore store{connection};
    const auto members = initial_members();
    check(store.create("p-001", "c1", "Original", stamp1, members) ==
              application::CohortWriteResult::created,
          "setup create");

    const std::vector<domain::CohortMemberDraft> revision_two{
        {"001", "person-001", domain::CohortGroup::case_sample, true, ""},
        {"ctrl-01", "person-ctrl-01", domain::CohortGroup::control, false, "manual exclusion"},
    };
    check(store.revise("p-001", "c1", 1, "Revised", stamp2, revision_two) ==
              application::CohortWriteResult::revised,
          "revision two failed");
    check(store.get("p-001", "c1")->current_revision == 2, "current revision did not advance");

    const auto old_revision = store.get_revision("p-001", "c1", 1);
    const auto new_revision = store.get_revision("p-001", "c1", 2);
    check(old_revision && old_revision->members.size() == 3U, "historical revision changed");
    check(new_revision && new_revision->parent_revision == 1 &&
              new_revision->members.size() == 2U &&
              !new_revision->members[1].included,
          "revision two content invalid");

    check(store.revise("p-001", "c1", 1, "Stale", stamp3, revision_two) ==
              application::CohortWriteResult::stale_revision,
          "stale revision accepted");
    check(store.get("p-001", "c1")->current_revision == 2, "stale write advanced revision");

    connection.execute(
        "CREATE TRIGGER reject_fail_member BEFORE INSERT ON cohort_revision_members "
        "WHEN NEW.sample_id='FAIL' BEGIN SELECT RAISE(ABORT,'injected cohort failure'); END;");
    const std::vector<domain::CohortMemberDraft> failing{
        {"001", "person-001", domain::CohortGroup::case_sample, true, ""},
        {"FAIL", "person-fail", domain::CohortGroup::control, true, ""},
    };
    rejects<SqliteError>([&] {
        (void)store.revise("p-001", "c1", 2, "Should rollback", stamp3, failing);
    });
    check(store.get("p-001", "c1")->current_revision == 2, "failed revision advanced pointer");
    check(!store.get_revision("p-001", "c1", 3), "failed revision survived rollback");
}

void create_v15_fixture(SqliteConnection& connection) {
    connection.execute(R"sql(
        CREATE TABLE schema_migrations(
            version INTEGER PRIMARY KEY NOT NULL,
            name TEXT NOT NULL,
            applied_at_utc TEXT NOT NULL
        );
        INSERT INTO schema_migrations(version,name,applied_at_utc)
        VALUES(15,'add_batch_recovery_attempt_lineage','legacy');

        CREATE TABLE project_metadata(
            singleton INTEGER PRIMARY KEY NOT NULL CHECK(singleton=1),
            project_id TEXT UNIQUE NOT NULL,
            name TEXT NOT NULL,
            root_path TEXT UNIQUE NOT NULL,
            created_at_utc TEXT NOT NULL,
            updated_at_utc TEXT NOT NULL
        );
        INSERT INTO project_metadata(singleton,project_id,name,root_path,created_at_utc,updated_at_utc)
        VALUES(1,'p-001','Legacy','/legacy','c','u');

        CREATE TABLE project_samples(
            project_id TEXT NOT NULL,
            sample_id TEXT NOT NULL,
            display_name TEXT NOT NULL DEFAULT '',
            group_label TEXT NOT NULL DEFAULT '',
            PRIMARY KEY(project_id,sample_id),
            FOREIGN KEY(project_id) REFERENCES project_metadata(project_id) ON DELETE CASCADE
        );
        INSERT INTO project_samples(project_id,sample_id,display_name,group_label)
        VALUES('p-001','001','Legacy sample','old metadata');

        CREATE TABLE jobs(id TEXT PRIMARY KEY NOT NULL, status TEXT NOT NULL);
        INSERT INTO jobs(id,status) VALUES('legacy-job','completed');
    )sql");
}

void migration_contract() {
    {
        SqliteConnection connection{":memory:"};
        create_v15_fixture(connection);
        ProjectMigrationRunner migrations{connection};
        check(migrations.current_version() == 15, "fixture not v15");
        migrations.apply_pending();
        check(migrations.current_version() == 16, "v16 not applied");
        check(scalar_integer(connection,
            "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='cohort_definitions';") == 1,
            "cohort definitions table missing");
        check(scalar_integer(connection,
            "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='cohort_revisions';") == 1,
            "cohort revisions table missing");
        check(scalar_integer(connection,
            "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='cohort_revision_members';") == 1,
            "cohort members table missing");
        check(scalar_integer(connection, "SELECT COUNT(*) FROM jobs WHERE id='legacy-job';") == 1,
            "legacy job changed");
        check(scalar_integer(connection,
            "SELECT COUNT(*) FROM project_samples WHERE sample_id='001';") == 1,
            "legacy sample changed");
    }

    {
        SqliteConnection connection{":memory:"};
        create_v15_fixture(connection);
        connection.execute(
            "CREATE TRIGGER reject_v16 BEFORE INSERT ON schema_migrations "
            "WHEN NEW.version=16 BEGIN SELECT RAISE(ABORT,'injected v16 failure'); END;");
        rejects<SqliteError>([&] { ProjectMigrationRunner{connection}.apply_pending(); });
        check(ProjectMigrationRunner{connection}.current_version() == 15,
              "failed v16 advanced schema");
        check(scalar_integer(connection,
            "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='cohort_definitions';") == 0,
            "failed v16 left cohort table");
        check(scalar_integer(connection, "SELECT COUNT(*) FROM jobs WHERE id='legacy-job';") == 1,
              "failed v16 changed legacy row");
        connection.execute("DROP TRIGGER reject_v16;");
        ProjectMigrationRunner{connection}.apply_pending();
        check(ProjectMigrationRunner{connection}.current_version() == 16,
              "v16 retry failed");
    }
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        if (argc != 2) return EXIT_FAILURE;
        const std::string_view mode{argv[1]};
        if (mode == "tokens") token_contract();
        else if (mode == "persistence") persistence_contract();
        else if (mode == "revision") revision_contract();
        else if (mode == "migration") migration_contract();
        else return EXIT_FAILURE;
        std::cout << mode << " PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
