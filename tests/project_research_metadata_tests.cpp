#include <sqlite3.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include "biocore/domain/project.hpp"
#include "biocore/domain/project_research_metadata.hpp"
#include "biocore/domain/workflow_checkpoint.hpp"
#include "biocore/infrastructure/sqlite/project_database_guard.hpp"
#include "biocore/infrastructure/sqlite/project_database_initializer.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"
#include "biocore/infrastructure/sqlite/sqlite_project_research_metadata_store.hpp"
#include "biocore/infrastructure/sqlite/sqlite_workflow_state_store.hpp"
#include "biocore/pipeline_protocol/workflow_document_codec.hpp"

namespace {
using namespace biocore;
using namespace biocore::infrastructure::sqlite;
constexpr const char* stamp = "2026-09-27T18:00:00Z";
void check(const bool ok, const char* message) { if (!ok) throw std::runtime_error{message}; }
template<class Exception, class Fn> void rejects(Fn fn) {
    try { fn(); } catch (const Exception&) { return; }
    throw std::runtime_error{"Expected rejection"};
}
class Temp final {
public:
    Temp() : root{std::filesystem::temp_directory_path() /
        ("biocore-research-079-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))} {
        std::filesystem::create_directory(root);
    }
    ~Temp() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
    std::filesystem::path root;
};
std::string scalar(SqliteConnection& c, const char* sql) {
    sqlite3_stmt* q = nullptr;
    check(sqlite3_prepare_v2(c.native_handle(), sql, -1, &q, nullptr) == SQLITE_OK, "prepare");
    const int rc = sqlite3_step(q);
    std::string value;
    if (rc == SQLITE_ROW && sqlite3_column_text(q, 0)) {
        value.assign(reinterpret_cast<const char*>(sqlite3_column_text(q, 0)),
                     static_cast<std::size_t>(sqlite3_column_bytes(q, 0)));
    }
    sqlite3_finalize(q);
    check(rc == SQLITE_ROW, "scalar row");
    return value;
}
void initialize(SqliteConnection& c) {
    ProjectDatabaseInitializer{c}.initialize(domain::Project{"p-001", "Research", "/local/project", stamp, stamp});
}
void legacy(SqliteConnection& c) {
    std::ifstream input{std::filesystem::path{BIOCORE_SOURCE_ROOT} / "tests/fixtures/project-schema-v9.sql"};
    check(input.good(), "legacy fixture missing");
    const std::string sql{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    c.execute(sql);
    c.execute("INSERT INTO project_metadata VALUES(1,'p-001','Research','/local/project','2026-09-27T18:00:00Z','2026-09-27T18:00:00Z');");
    c.execute("INSERT INTO jobs(id,status,priority,progress,created_at_utc,updated_at_utc) VALUES('old-job','queued','normal',0.25,'t','t');");
    c.execute("INSERT INTO managed_files(id,display_name,storage_mode,original_path,file_type,size_bytes,created_at_utc,updated_at_utc) VALUES('old-file','reads','external_reference','/reads.fastq','fastq',100,'t','t');");
    const auto workflow = pipeline_protocol::parse_workflow_document(R"({
        "schemaVersion":1,"id":"old-workflow","name":"Retained","description":"v9",
        "nodes":[{"id":"n","label":"N","moduleId":"org.biocore.test.source","pluginVersion":"1.0.0",
        "inputs":[],"outputs":[{"name":"result","artifactType":"txt"}],"parameters":{}}],"edges":[]})");
    application::PersistedWorkflowState state{
        .workflow=workflow,
        .checkpoint=domain::WorkflowCheckpointManifest{
            1,domain::WorkflowId{"old-workflow"},{{domain::WorkflowNodeId{"n"},
            domain::WorkflowCheckpointNodeState::pending,0U,1U,{},std::nullopt}}},
        .branch_decisions=std::nullopt,.revision=0,.updated_at_utc=stamp};
    check(SqliteWorkflowStateStore{c}.create(state), "legacy workflow creation");
}
void domain_contract() {
    const domain::ProjectResearchMetadata empty{"p-001", "", "", 0, stamp};
    check(empty.description().empty(), "empty optional metadata");
    for (const auto field : {0,1,2,3}) {
        rejects<std::invalid_argument>([&] {
            const domain::ProjectResearchMetadata bad{
                field==0 ? " " : "p", field==1 ? std::string(4097,'x') : "",
                field==2 ? std::string(257,'x') : "", field==3 ? -1 : 0, stamp};
            (void)bad;
        });
    }
    rejects<std::invalid_argument>([] { (void)domain::ProjectResearchMetadata{"p",std::string{"a\0b",3},"",0,stamp}; });
    rejects<std::invalid_argument>([] { (void)domain::ProjectResearchMetadata{"p","","",0,"\t"}; });
    const domain::ProjectResearchMetadata bounds{"p",std::string(4096,'x'),std::string(256,'x'),0,stamp};
    check(bounds.description().size()==4096, "byte bound");
}
void persistence_contract() {
    Temp temp;
    const auto path = temp.root / "project.sqlite";
    {
        SqliteConnection c{path}; initialize(c);
        SqliteProjectResearchMetadataStore store{c};
        const auto snapshot = store.find("p-001");
        check(snapshot && snapshot->revision()==0 && snapshot->updated_at_utc()==stamp, "initial metadata");
        check(!store.find("other"), "wrong owner read");
        const domain::ProjectResearchMetadata edited{"p-001","Açıklama; 'quoted'","Homo sapiens",1,stamp};
        check(store.update(edited,0), "update");
        check(!store.update(edited,0), "stale update");
        check(!store.update(domain::ProjectResearchMetadata{"other","","",1,stamp},0), "wrong owner write");
        check(snapshot->revision()==0 && snapshot->description().empty(), "value snapshot mutated");
        rejects<std::invalid_argument>([&] { (void)store.update(edited,-1); });
        rejects<std::invalid_argument>([&] { (void)store.update(edited,std::numeric_limits<std::int64_t>::max()); });
        rejects<std::invalid_argument>([&] { (void)store.update(edited,1); });
        rejects<SqliteError>([&] { c.execute("UPDATE project_metadata SET project_id='other';"); });
        rejects<SqliteError>([&] { c.execute("UPDATE project_metadata SET research_description='unversioned';"); });
        rejects<SqliteError>([&] { c.execute("UPDATE project_metadata SET research_updated_at_utc='unversioned';"); });
        rejects<SqliteError>([&] { c.execute("UPDATE project_metadata SET research_description=CAST(zeroblob(4097) AS TEXT),research_revision=2;"); });
        rejects<SqliteError>([&] { c.execute("UPDATE project_metadata SET research_revision=2.5;"); });
        // Failed SQL must preserve both revision and content.
        c.execute("CREATE TRIGGER reject_research_update BEFORE UPDATE ON project_metadata BEGIN SELECT RAISE(ABORT,'injected'); END;");
        rejects<SqliteError>([&] { (void)store.update(domain::ProjectResearchMetadata{"p-001","lost","",2,stamp},1); });
        check(store.find("p-001")->revision()==1, "failed update advanced revision");
        check(scalar(c,"SELECT name FROM project_metadata;")=="Research", "name changed");
        c.execute("DROP TRIGGER reject_research_update;");
        ProjectDatabaseGuard{c}.validate_current_schema();
    }
    SqliteConnection reopened{path};
    SqliteProjectResearchMetadataStore store{reopened};
    check(store.find("p-001")->description()=="Açıklama; 'quoted'", "reopen content");
    check(store.update(domain::ProjectResearchMetadata{"p-001","","",2,stamp},1), "clear optional fields");
}
void conflict_contract() {
    Temp temp; const auto path=temp.root/"project.sqlite";
    SqliteConnection first{path}; initialize(first);
    SqliteConnection second{path};
    SqliteProjectResearchMetadataStore a{first}, b{second};
    const auto observed=b.find("p-001");
    check(a.update(domain::ProjectResearchMetadata{"p-001","winner","",1,stamp},0), "first writer");
    check(!b.update(domain::ProjectResearchMetadata{"p-001","stale","",1,stamp},observed->revision()), "lost update");
    check(b.find("p-001")->description()=="winner", "winner lost");
}
void migration_contract() {
    Temp temp; const auto old_path=temp.root/"v9.sqlite";
    { SqliteConnection old{old_path}; legacy(old); }
    const auto path=temp.root/"copy.sqlite";
    std::filesystem::copy_file(old_path,path);
    {
        SqliteConnection c{path};
        const auto before=scalar(c,"SELECT workflow_document_json FROM workflow_states;");
        ProjectDatabaseGuard{c}.validate_before_migration();
        ProjectMigrationRunner migrations{c}; migrations.apply_pending(); migrations.apply_pending();
        ProjectDatabaseGuard{c}.validate_current_schema();
        check(migrations.current_version()==latest_project_schema_version,"latest schema");
        check(scalar(c,"SELECT workflow_document_json FROM workflow_states;")==before,"workflow bytes changed");
        check(scalar(c,"SELECT COUNT(*) FROM jobs WHERE id='old-job' AND progress=0.25;")=="1","job lost");
        check(scalar(c,"SELECT COUNT(*) FROM managed_files WHERE id='old-file';")=="1","file lost");
        check(SqliteProjectResearchMetadataStore{c}.find("p-001")->revision()==0,"legacy default");
    }
    SqliteConnection reopened{path};
    const auto retained=SqliteWorkflowStateStore{reopened}.find_by_workflow_id("old-workflow");
    check(retained && retained->checkpoint.nodes().size()==1 && retained->revision==0,"old workflow not readable");
    SqliteConnection original{old_path};
    check(ProjectMigrationRunner{original}.current_version()==9,"original copy mutated");
}
void rollback_contract() {
    SqliteConnection c{":memory:"}; legacy(c);
    c.execute("CREATE TRIGGER reject_v10 BEFORE INSERT ON schema_migrations WHEN NEW.version=10 BEGIN SELECT RAISE(ABORT,'injected migration failure'); END;");
    rejects<SqliteError>([&] { ProjectMigrationRunner{c}.apply_pending(); });
    check(ProjectMigrationRunner{c}.current_version()==9,"rollback version");
    check(scalar(c,"SELECT COUNT(*) FROM pragma_table_info('project_metadata') WHERE name LIKE 'research_%';")=="0","partial DDL survived");
    check(SqliteWorkflowStateStore{c}.find_by_workflow_id("old-workflow").has_value(),"rollback lost workflow");
    c.execute("DROP TRIGGER reject_v10;");
    ProjectMigrationRunner{c}.apply_pending(); ProjectDatabaseGuard{c}.validate_current_schema();
    c.execute("DROP TRIGGER project_metadata_identity_immutable;");
    rejects<SqliteError>([&] { ProjectDatabaseGuard{c}.validate_current_schema(); });
}
}
int main(const int argc, char** argv) {
    try {
        if(argc!=2) return EXIT_FAILURE;
        const std::string_view mode{argv[1]};
        if(mode=="domain") domain_contract();
        else if(mode=="persistence") persistence_contract();
        else if(mode=="conflict") conflict_contract();
        else if(mode=="migration") migration_contract();
        else if(mode=="rollback") rollback_contract();
        else return EXIT_FAILURE;
        std::cout << mode << " PASS\n"; return EXIT_SUCCESS;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return EXIT_FAILURE; }
}
