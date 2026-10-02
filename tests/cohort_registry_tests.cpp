#include <sqlite3.h>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/cohort_registry_service.hpp"
#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/domain/project.hpp"
#include "biocore/domain/project_sample.hpp"
#include "biocore/infrastructure/sqlite/project_database_guard.hpp"
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
constexpr const char* stamp2 = "2026-10-02T10:01:00Z";
constexpr const char* stamp3 = "2026-10-02T10:02:00Z";

void check(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

template<class Exception, class Function>
void rejects(Function function) {
    try { function(); } catch (const Exception&) { return; }
    throw std::runtime_error{"Expected rejection"};
}

class FixedIds final : public application::IIdGenerator {
public:
    explicit FixedIds(std::vector<std::string> values) : values_{std::move(values)} {}
    std::string generate() override {
        if (index_ >= values_.size()) throw std::runtime_error{"ID fixture exhausted"};
        return values_[index_++];
    }
private:
    std::vector<std::string> values_;
    std::size_t index_{0U};
};

class SequenceClock final : public application::IUtcClock {
public:
    explicit SequenceClock(std::vector<std::string> values) : values_{std::move(values)} {}
    std::string now_utc_iso8601() override {
        if (index_ >= values_.size()) throw std::runtime_error{"Clock fixture exhausted"};
        return values_[index_++];
    }
private:
    std::vector<std::string> values_;
    std::size_t index_{0U};
};

std::string scalar(SqliteConnection& connection, const char* sql) {
    sqlite3_stmt* statement = nullptr;
    check(sqlite3_prepare_v2(connection.native_handle(), sql, -1, &statement, nullptr) == SQLITE_OK,
          "prepare scalar");
    const int result = sqlite3_step(statement);
    check(result == SQLITE_ROW, "scalar row");
    const auto* value = sqlite3_column_text(statement, 0);
    std::string output = value == nullptr ? "" :
        std::string{reinterpret_cast<const char*>(value),
                    static_cast<std::size_t>(sqlite3_column_bytes(statement, 0))};
    sqlite3_finalize(statement);
    return output;
}

void initialize(SqliteConnection& connection) {
    ProjectDatabaseInitializer{connection}.initialize(
        domain::Project{"p-001", "Research", "/local/project", stamp1, stamp1});
    SqliteProjectSampleStore samples{connection};
    const std::vector<domain::ProjectSample> rows{
        {"p-001", "001", "Örnek α", "legacy-case"},
        {"p-001", "0007", "Kontrol β", "legacy-control"},
        {"p-001", "S-3", "Third", ""},
    };
    check(samples.add_batch("p-001", rows) == application::SampleBatchAddResult::added,
          "seed samples");
}

application::CohortMemberDraft member(
    std::string sample_id,
    application::CohortGroup group,
    application::CohortMemberDisposition disposition =
        application::CohortMemberDisposition::included,
    std::optional<std::string> reason = std::nullopt
) {
    const std::string biological_unit_id = "bio-" + sample_id;
    return application::CohortMemberDraft{
        .sample_id = std::move(sample_id),
        .biological_unit_id = biological_unit_id,
        .group = group,
        .disposition = disposition,
        .exclusion_reason = std::move(reason),
    };
}

void model_contract() {
    check(application::to_string(application::CohortGroup::case_group) == "case", "case token");
    check(application::to_string(application::CohortGroup::control) == "control", "control token");
    check(application::to_string(application::CohortGroup::unassigned) == "unassigned", "unassigned token");
    check(application::cohort_group_from_string("case") == application::CohortGroup::case_group,
          "case parse");
    check(!application::cohort_group_from_string("CASE"), "group token was case folded");
    const application::CohortMemberDraft default_group{};
    check(default_group.group == application::CohortGroup::unassigned,
          "missing group silently defaulted to control");
}

void create_contract() {
    SqliteConnection connection{":memory:"};
    initialize(connection);
    SqliteCohortRegistryStore store{connection};
    FixedIds ids{{"cohort-α"}};
    SequenceClock clock{{stamp2}};
    application::CohortRegistryService service{store, ids, clock};

    auto created = service.create({
        .project_id = "p-001",
        .name = "Case control registry",
        .members = {
            member("001", application::CohortGroup::case_group),
            member("0007", application::CohortGroup::control),
            member("S-3", application::CohortGroup::unassigned,
                   application::CohortMemberDisposition::excluded, "QC pending"),
        },
    });
    check(created.cohort_id == "cohort-α" && created.current_revision == 1U,
          "cohort identity/revision");
    check(created.revision.members.size() == 3U, "cohort members missing");
    check(created.revision.members[0].sample_id == "001" &&
          created.revision.members[1].sample_id == "0007", "sample IDs normalized");
    check(created.revision.members[0].sample_display_name == "Örnek α", "unicode snapshot lost");
    check(created.revision.members[2].exclusion_reason == "QC pending", "exclusion reason lost");
    check(scalar(connection, "SELECT sealed FROM cohort_revisions WHERE revision=1;") == "1",
          "revision was not sealed");
}

void validation_contract() {
    SqliteConnection connection{":memory:"};
    initialize(connection);
    SqliteCohortRegistryStore store{connection};
    FixedIds ids{{"c1", "c2", "c3"}};
    SequenceClock clock{{stamp2, stamp2, stamp2}};
    application::CohortRegistryService service{store, ids, clock};

    try {
        (void)service.create({"p-001", "dup", {
            member("001", application::CohortGroup::case_group),
            member("001", application::CohortGroup::control)}});
        throw std::runtime_error{"duplicate sample accepted"};
    } catch (const application::CohortRegistryError& error) {
        check(error.code() == application::CohortRegistryErrorCode::duplicate_sample,
              "duplicate sample wrong error");
    }
    check(scalar(connection, "SELECT COUNT(*) FROM cohort_definitions;") == "0",
          "duplicate write was partial");

    try {
        (void)service.create({"p-001", "missing", {
            member("does-not-exist", application::CohortGroup::case_group)}});
        throw std::runtime_error{"missing sample accepted"};
    } catch (const application::CohortRegistryError& error) {
        check(error.code() == application::CohortRegistryErrorCode::sample_not_found,
              "missing sample wrong error");
    }
    check(scalar(connection, "SELECT COUNT(*) FROM cohort_definitions;") == "0",
          "missing sample write was partial");

    try {
        (void)service.create({"wrong-project", "wrong", {
            member("001", application::CohortGroup::case_group)}});
        throw std::runtime_error{"wrong project accepted"};
    } catch (const application::CohortRegistryError& error) {
        check(error.code() == application::CohortRegistryErrorCode::project_not_found,
              "wrong project wrong error");
    }

    rejects<application::CohortRegistryError>([&] {
        (void)service.create({"p-001", "bad exclusion", {
            member("001", application::CohortGroup::case_group,
                   application::CohortMemberDisposition::excluded, std::nullopt)}});
    });
}

void revision_contract() {
    SqliteConnection connection{":memory:"};
    initialize(connection);
    SqliteCohortRegistryStore store{connection};
    FixedIds ids{{"c-rev"}};
    SequenceClock clock{{stamp1, stamp2, stamp3}};
    application::CohortRegistryService service{store, ids, clock};

    auto first = service.create({"p-001", "Revisioned", {
        member("001", application::CohortGroup::case_group),
        member("0007", application::CohortGroup::control)}});
    connection.execute(
        "UPDATE project_samples SET display_name='Metadata changed',group_label='changed' "
        "WHERE project_id='p-001' AND sample_id='001';");

    auto second = service.revise({"p-001", first.cohort_id, 1U, {
        member("001", application::CohortGroup::control),
        member("0007", application::CohortGroup::case_group)}});
    check(second.current_revision == 2U && second.revision.parent_revision == 1U,
          "revision lineage missing");
    check(second.revision.members[0].sample_display_name == "Metadata changed",
          "new revision did not snapshot current metadata");

    auto historical = service.find("p-001", first.cohort_id, 1U);
    check(historical.has_value(), "historical revision missing");
    check(historical->revision.members[0].sample_display_name == "Örnek α" &&
          historical->revision.members[0].sample_group_metadata == "legacy-case" &&
          historical->revision.members[0].group == application::CohortGroup::case_group,
          "historical snapshot changed after metadata/revision edit");

    try {
        (void)service.revise({"p-001", first.cohort_id, 1U, {
            member("001", application::CohortGroup::case_group)}});
        throw std::runtime_error{"stale revision accepted"};
    } catch (const application::CohortRegistryError& error) {
        check(error.code() == application::CohortRegistryErrorCode::stale_revision,
              "stale revision wrong error");
    }
    check(scalar(connection, "SELECT current_revision FROM cohort_definitions WHERE cohort_id='c-rev';") == "2",
          "stale revision changed current state");
}

void atomicity_contract() {
    SqliteConnection connection{":memory:"};
    initialize(connection);
    SqliteCohortRegistryStore store{connection};
    FixedIds ids{{"c-atomic-failed", "c-atomic"}};
    SequenceClock clock{{stamp1, stamp2, stamp3}};
    application::CohortRegistryService service{store, ids, clock};

    connection.execute(
        "CREATE TRIGGER reject_second_cohort_member BEFORE INSERT ON cohort_revision_members "
        "WHEN NEW.sample_id='0007' BEGIN SELECT RAISE(ABORT,'injected cohort write failure'); END;");
    rejects<SqliteError>([&] {
        (void)service.create({"p-001", "Atomic", {
            member("001", application::CohortGroup::case_group),
            member("0007", application::CohortGroup::control)}});
    });
    check(scalar(connection, "SELECT COUNT(*) FROM cohort_definitions;") == "0" &&
          scalar(connection, "SELECT COUNT(*) FROM cohort_revisions;") == "0" &&
          scalar(connection, "SELECT COUNT(*) FROM cohort_revision_members;") == "0",
          "failed create left partial cohort rows");
    connection.execute("DROP TRIGGER reject_second_cohort_member;");

    auto created = service.create({"p-001", "Atomic", {
        member("001", application::CohortGroup::case_group),
        member("0007", application::CohortGroup::control)}});
    connection.execute(
        "CREATE TRIGGER reject_revision_two_member BEFORE INSERT ON cohort_revision_members "
        "WHEN NEW.revision=2 AND NEW.sample_id='0007' "
        "BEGIN SELECT RAISE(ABORT,'injected revision failure'); END;");
    rejects<SqliteError>([&] {
        (void)service.revise({"p-001", created.cohort_id, 1U, {
            member("001", application::CohortGroup::control),
            member("0007", application::CohortGroup::case_group)}});
    });
    check(scalar(connection, "SELECT current_revision FROM cohort_definitions WHERE cohort_id='c-atomic';") == "1" &&
          scalar(connection, "SELECT COUNT(*) FROM cohort_revisions WHERE cohort_id='c-atomic';") == "1",
          "failed revision partially advanced cohort");
}

void immutability_contract() {
    SqliteConnection connection{":memory:"};
    initialize(connection);
    SqliteCohortRegistryStore store{connection};
    FixedIds ids{{"c-lock"}};
    SequenceClock clock{{stamp1}};
    application::CohortRegistryService service{store, ids, clock};
    (void)service.create({"p-001", "Locked", {
        member("001", application::CohortGroup::case_group),
        member("0007", application::CohortGroup::control)}});

    rejects<SqliteError>([&] {
        connection.execute("UPDATE cohort_revision_members SET group_token='control' WHERE sample_id='001';");
    });
    rejects<SqliteError>([&] {
        connection.execute("DELETE FROM cohort_revisions WHERE cohort_id='c-lock';");
    });
    rejects<SqliteError>([&] {
        connection.execute("DELETE FROM project_samples WHERE project_id='p-001' AND sample_id='001';");
    });
    check(scalar(connection, "SELECT group_token FROM cohort_revision_members WHERE sample_id='001';") == "case",
          "immutable member changed");
}

void drop_v17(SqliteConnection& connection) {
    connection.execute("DROP TABLE cohort_analysis_test_universe;");
    connection.execute("DROP TABLE cohort_analysis_reference_aliases;");
    connection.execute("DROP TABLE cohort_analysis_reference_contigs;");
    connection.execute("DROP TABLE cohort_analysis_sources;");
    connection.execute("DROP TABLE cohort_analysis_samples;");
    connection.execute("DROP TABLE cohort_analysis_snapshots;");
    connection.execute("DELETE FROM schema_migrations WHERE version=17;");
}

void migration_contract() {
    SqliteConnection connection{":memory:"};
    initialize(connection);
    drop_v17(connection);
    connection.execute("DROP TRIGGER cohort_revision_members_immutable_delete;");
    connection.execute("DROP TRIGGER cohort_revision_members_immutable_update;");
    connection.execute("DROP TRIGGER cohort_revision_members_require_open_revision;");
    connection.execute("DROP TRIGGER cohort_revisions_immutable_delete;");
    connection.execute("DROP TRIGGER cohort_revisions_validate_update;");
    connection.execute("DROP TRIGGER cohort_definitions_immutable_delete;");
    connection.execute("DROP TRIGGER cohort_definitions_validate_update;");
    connection.execute("DROP TABLE cohort_revision_members;");
    connection.execute("DROP TABLE cohort_revisions;");
    connection.execute("DROP TABLE cohort_definitions;");
    connection.execute("DELETE FROM schema_migrations WHERE version=16;");
    ProjectMigrationRunner runner{connection};
    check(runner.current_version() == 15, "v15 fixture setup failed");
    runner.apply_pending();
    ProjectDatabaseGuard{connection}.validate_current_schema();
    check(runner.current_version() == latest_project_schema_version,
          "v15->current migration missing");
    check(scalar(connection, "SELECT display_name FROM project_samples WHERE sample_id='001';") == "Örnek α",
          "migration changed existing sample metadata");
    check(scalar(connection,
        "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='cohort_revision_members';") == "1",
          "v16 cohort tables missing");
    check(scalar(connection,
        "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='cohort_analysis_snapshots';") == "1",
          "v17 analysis snapshot tables missing");
}

void rollback_contract() {
    SqliteConnection connection{":memory:"};
    initialize(connection);
    drop_v17(connection);
    connection.execute("DROP TRIGGER cohort_revision_members_immutable_delete;");
    connection.execute("DROP TRIGGER cohort_revision_members_immutable_update;");
    connection.execute("DROP TRIGGER cohort_revision_members_require_open_revision;");
    connection.execute("DROP TRIGGER cohort_revisions_immutable_delete;");
    connection.execute("DROP TRIGGER cohort_revisions_validate_update;");
    connection.execute("DROP TRIGGER cohort_definitions_immutable_delete;");
    connection.execute("DROP TRIGGER cohort_definitions_validate_update;");
    connection.execute("DROP TABLE cohort_revision_members;");
    connection.execute("DROP TABLE cohort_revisions;");
    connection.execute("DROP TABLE cohort_definitions;");
    connection.execute("DELETE FROM schema_migrations WHERE version=16;");
    connection.execute(
        "CREATE TRIGGER reject_v16 BEFORE INSERT ON schema_migrations "
        "WHEN NEW.version=16 BEGIN SELECT RAISE(ABORT,'injected v16 failure'); END;");
    rejects<SqliteError>([&] { ProjectMigrationRunner{connection}.apply_pending(); });
    check(ProjectMigrationRunner{connection}.current_version() == 15,
          "failed v16 migration advanced version");
    check(scalar(connection,
        "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='cohort_definitions';") == "0",
          "failed v16 migration left tables");
    connection.execute("DROP TRIGGER reject_v16;");
    ProjectMigrationRunner{connection}.apply_pending();
    ProjectDatabaseGuard{connection}.validate_current_schema();
    check(ProjectMigrationRunner{connection}.current_version() == latest_project_schema_version,
          "current migration retry failed");
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        if (argc != 2) return EXIT_FAILURE;
        const std::string_view mode{argv[1]};
        if (mode == "model") model_contract();
        else if (mode == "create") create_contract();
        else if (mode == "validation") validation_contract();
        else if (mode == "revision") revision_contract();
        else if (mode == "atomicity") atomicity_contract();
        else if (mode == "immutability") immutability_contract();
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
