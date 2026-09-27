#include <sqlite3.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/sample_registry_import.hpp"
#include "biocore/domain/project.hpp"
#include "biocore/domain/project_sample.hpp"
#include "biocore/infrastructure/sqlite/project_database_guard.hpp"
#include "biocore/infrastructure/sqlite/project_database_initializer.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"
#include "biocore/infrastructure/sqlite/sqlite_project_sample_store.hpp"

namespace {
using namespace biocore;
using namespace biocore::infrastructure::sqlite;

constexpr const char* stamp = "2026-09-27T19:00:00Z";

void check(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

template<class Exception, class Function>
void rejects(Function function) {
    try { function(); } catch (const Exception&) { return; }
    throw std::runtime_error{"Expected rejection"};
}

class Temp final {
public:
    Temp() : root{std::filesystem::temp_directory_path() /
        ("biocore-samples-080-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()))} {
        std::filesystem::create_directory(root);
    }
    ~Temp() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    std::filesystem::path root;
};

std::string scalar(SqliteConnection& connection, const char* sql) {
    sqlite3_stmt* statement = nullptr;
    check(sqlite3_prepare_v2(connection.native_handle(), sql, -1, &statement, nullptr) == SQLITE_OK,
          "prepare");
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
        domain::Project{"p-001", "Research", "/local/project", stamp, stamp});
}

void load_v10(SqliteConnection& connection) {
    std::ifstream input{std::filesystem::path{BIOCORE_SOURCE_ROOT} /
        "tests/fixtures/project-schema-v10.sql"};
    check(input.good(), "v10 fixture missing");
    const std::string sql{std::istreambuf_iterator<char>{input},
                          std::istreambuf_iterator<char>{}};
    connection.execute(sql);
    connection.execute(
        "INSERT INTO project_metadata(singleton,project_id,name,root_path,created_at_utc,"
        "updated_at_utc,research_description,research_organism,research_revision,"
        "research_updated_at_utc) VALUES(1,'p-001','Research','/local/project','t','t',"
        "'legacy research','Homo sapiens',2,'t2');");
}

void domain_contract() {
    const domain::ProjectSample sample{"p-001", "001", "Örnek α", ""};
    check(sample.sample_id() == "001", "leading zero lost");
    check(sample.display_name() == "Örnek α", "unicode lost");
    check(sample.group_label().empty(), "empty metadata changed");
    rejects<std::invalid_argument>([] {
        (void)domain::ProjectSample{"p", "   ", "", ""};
    });
    rejects<std::invalid_argument>([] {
        (void)domain::ProjectSample{"p", std::string(129U, 'x'), "", ""};
    });
    rejects<std::invalid_argument>([] {
        (void)domain::ProjectSample{"p", "id", std::string{"a\0b", 3U}, ""};
    });
}

void preview_contract() {
    SqliteConnection connection{":memory:"};
    initialize(connection);
    SqliteProjectSampleStore store{connection};
    application::SampleRegistryImportService service{store};

    const auto invalid = service.preview(
        "p-001",
        "sample_id,display_name,group\n001,Alpha,case\n001,Beta,control\n,Missing,case\n",
        application::SampleTableFormat::csv);
    check(!invalid.valid(), "invalid preview accepted");
    check(invalid.issues().size() == 2U, "row diagnostics missing");
    check(invalid.issues()[0].line == 3U && invalid.issues()[1].line == 4U,
          "wrong diagnostic lines");
    rejects<std::invalid_argument>([&] { service.commit(invalid); });
    check(store.list("p-001")->empty(), "invalid preview wrote rows");

    const auto malformed = service.preview(
        "p-001", "sample_id\tdisplay_name\tgroup\n002\t\"bad\tx\n",
        application::SampleTableFormat::tsv);
    check(!malformed.valid() && malformed.issues().front().line == 2U,
          "malformed quote not diagnosed");
}

void persistence_contract() {
    SqliteConnection connection{":memory:"};
    initialize(connection);
    SqliteProjectSampleStore store{connection};
    const std::vector<domain::ProjectSample> first{
        {"p-001", "001", "One", ""},
        {"p-001", "002", "Two", "case"},
    };
    check(store.add_batch("p-001", first) == application::SampleBatchAddResult::added,
          "initial batch failed");
    const std::vector<domain::ProjectSample> conflicting{
        {"p-001", "003", "Three", ""},
        {"p-001", "002", "Duplicate", ""},
    };
    check(store.add_batch("p-001", conflicting) ==
              application::SampleBatchAddResult::duplicate_sample,
          "duplicate batch accepted");
    const auto rows = store.list("p-001");
    check(rows && rows->size() == 2U, "duplicate batch partially wrote");

    connection.execute(
        "CREATE TRIGGER reject_sample_004 BEFORE INSERT ON project_samples "
        "WHEN NEW.sample_id='004' BEGIN SELECT RAISE(ABORT,'injected sample failure'); END;");
    const std::vector<domain::ProjectSample> failing{
        {"p-001", "003", "Three", ""},
        {"p-001", "004", "Four", ""},
    };
    rejects<SqliteError>([&] { (void)store.add_batch("p-001", failing); });
    check(store.list("p-001")->size() == 2U, "SQLite failure partially wrote batch");
    connection.execute("DROP TRIGGER reject_sample_004;");

    check(!store.list("wrong").has_value(), "wrong project list accepted");
}

void import_contract() {
    SqliteConnection connection{":memory:"};
    initialize(connection);
    SqliteProjectSampleStore store{connection};
    application::SampleRegistryImportService service{store};

    auto preview = service.preview(
        "p-001",
        "sample_id,display_name,group\n001,\"Çelik, Recep\",case\n0007,,\n",
        application::SampleTableFormat::csv);
    check(preview.valid() && preview.samples().size() == 2U, "valid preview rejected");
    service.commit(preview);
    const auto rows = store.list("p-001");
    check(rows && rows->size() == 2U && rows->front().sample_id() == "0007",
          "deterministic registry order");
    check(rows->front().display_name().empty() && rows->front().group_label().empty(),
          "empty metadata not preserved");

    const auto duplicate = service.preview(
        "p-001", "sample_id,display_name,group\n001,again,case\n",
        application::SampleTableFormat::csv);
    check(!duplicate.valid() && duplicate.issues().front().code == "sample_already_exists",
          "existing duplicate not previewed");

    const auto stale = service.preview(
        "p-001", "sample_id,display_name,group\n009,Nine,case\n",
        application::SampleTableFormat::csv);
    check(stale.valid(), "stale setup preview failed");
    const std::vector<domain::ProjectSample> concurrent{
        {"p-001", "009", "Concurrent", "control"},
    };
    check(store.add_batch("p-001", concurrent) == application::SampleBatchAddResult::added,
          "stale setup insert failed");
    rejects<std::runtime_error>([&] { service.commit(stale); });
    check(store.list("p-001")->size() == 3U, "stale preview partially wrote");
}

void roundtrip_contract() {
    SqliteConnection source{":memory:"};
    initialize(source);
    SqliteProjectSampleStore source_store{source};
    application::SampleRegistryImportService source_service{source_store};
    const std::vector<domain::ProjectSample> values{
        {"p-001", "001", "Örnek α", ""},
        {"p-001", "A-2", "quoted \"name\"", "case,one"},
    };
    check(source_store.add_batch("p-001", values) == application::SampleBatchAddResult::added,
          "seed export registry");

    const std::string csv =
        source_service.export_table("p-001", application::SampleTableFormat::csv);

    SqliteConnection target{":memory:"};
    initialize(target);
    SqliteProjectSampleStore target_store{target};
    application::SampleRegistryImportService target_service{target_store};
    const auto preview =
        target_service.preview("p-001", csv, application::SampleTableFormat::csv);
    check(preview.valid(), "round-trip preview failed");
    target_service.commit(preview);
    check(target_store.list("p-001") == source_store.list("p-001"),
          "CSV round-trip changed registry values");

    const std::string tsv =
        source_service.export_table("p-001", application::SampleTableFormat::tsv);
    SqliteConnection tsv_target{":memory:"};
    initialize(tsv_target);
    SqliteProjectSampleStore tsv_store{tsv_target};
    application::SampleRegistryImportService tsv_service{tsv_store};
    const auto tsv_preview =
        tsv_service.preview("p-001", tsv, application::SampleTableFormat::tsv);
    check(tsv_preview.valid(), "TSV round-trip preview failed");
    tsv_service.commit(tsv_preview);
    check(tsv_store.list("p-001") == source_store.list("p-001"),
          "TSV round-trip changed registry values");
}

void migration_contract() {
    Temp temp;
    const auto path = temp.root / "v10.sqlite";
    {
        SqliteConnection connection{path};
        load_v10(connection);
    }

    SqliteConnection connection{path};
    ProjectMigrationRunner migrations{connection};
    check(migrations.current_version() == 10, "fixture is not v10");
    migrations.apply_pending();
    ProjectDatabaseGuard{connection}.validate_current_schema();
    check(migrations.current_version() == latest_project_schema_version, "latest migration missing");
    check(scalar(connection,
        "SELECT research_description FROM project_metadata WHERE project_id='p-001';") ==
        "legacy research", "research metadata changed");
    check(scalar(connection, "SELECT COUNT(*) FROM project_samples;") == "0",
          "migration created sample rows");
}

void rollback_contract() {
    SqliteConnection connection{":memory:"};
    load_v10(connection);
    connection.execute(
        "CREATE TRIGGER reject_v11 BEFORE INSERT ON schema_migrations "
        "WHEN NEW.version=11 BEGIN SELECT RAISE(ABORT,'injected v11 failure'); END;");
    rejects<SqliteError>([&] { ProjectMigrationRunner{connection}.apply_pending(); });
    check(ProjectMigrationRunner{connection}.current_version() == 10,
          "failed migration advanced version");
    check(scalar(connection,
        "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='project_samples';") ==
        "0", "failed migration left table");
    connection.execute("DROP TRIGGER reject_v11;");
    ProjectMigrationRunner{connection}.apply_pending();
    ProjectDatabaseGuard{connection}.validate_current_schema();
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        if (argc != 2) return EXIT_FAILURE;
        const std::string_view mode{argv[1]};
        if (mode == "domain") domain_contract();
        else if (mode == "preview") preview_contract();
        else if (mode == "persistence") persistence_contract();
        else if (mode == "import") import_contract();
        else if (mode == "roundtrip") roundtrip_contract();
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
