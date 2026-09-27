#include <sqlite3.h>

#include <algorithm>
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
#include <unordered_map>
#include <vector>

#include "biocore/application/batch_planning_service.hpp"
#include "biocore/application/i_input_file_storage.hpp"
#include "biocore/application/i_plugin_registry.hpp"
#include "biocore/application/i_reference_compatibility_inspector.hpp"
#include "biocore/application/i_workflow_template_catalog.hpp"
#include "biocore/application/sample_binding_service.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/project.hpp"
#include "biocore/domain/project_sample.hpp"
#include "biocore/domain/project_sample_binding.hpp"
#include "biocore/domain/storage_mode.hpp"
#include "biocore/domain/workflow.hpp"
#include "biocore/domain/workflow_template.hpp"
#include "biocore/infrastructure/sqlite/project_database_guard.hpp"
#include "biocore/infrastructure/sqlite/project_database_initializer.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_batch_plan_store.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"
#include "biocore/infrastructure/sqlite/sqlite_managed_file_repository.hpp"
#include "biocore/infrastructure/sqlite/sqlite_project_sample_binding_store.hpp"
#include "biocore/infrastructure/sqlite/sqlite_project_sample_store.hpp"

namespace {

using namespace biocore;
using namespace biocore::infrastructure::sqlite;

constexpr const char* stamp = "2026-09-27T22:00:00Z";
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

class FakeInputStorage final : public application::IInputFileStorage {
public:
    std::unordered_map<std::string, application::ManagedFileIntegrityStatus> status;

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
    prepare_browser_upload_commit(std::string_view, std::string_view) override {
        throw std::logic_error{"unused"};
    }

    void discard_browser_upload(std::string_view) noexcept override {}

    application::ManagedFileIntegrityResult verify_managed_file(
        const domain::ManagedFile& file
    ) const override {
        const auto found = status.find(std::string{file.id()});
        const auto value = found == status.end()
            ? application::ManagedFileIntegrityStatus::verified
            : found->second;
        return application::ManagedFileIntegrityResult{
            .status = value,
            .expected_size_bytes = file.size_bytes(),
            .observed_size_bytes =
                value == application::ManagedFileIntegrityStatus::file_missing
                    ? std::nullopt
                    : std::optional<std::int64_t>{file.size_bytes()},
            .expected_sha256 = file.checksum_value(),
            .observed_sha256 =
                value == application::ManagedFileIntegrityStatus::verified
                    ? file.checksum_value()
                    : std::nullopt,
        };
    }
};

class FakeReferenceInspector final
    : public application::IReferenceCompatibilityInspector {
public:
    application::ReferenceCompatibilityResult inspect(
        const domain::ManagedFile& input,
        const domain::ManagedFile&
    ) const override {
        if (input.file_type() == "fastq") {
            return {
                application::ReferenceCompatibilityStatus::not_declared_by_format,
                "FASTQ does not declare an assembly"
            };
        }
        return {
            application::ReferenceCompatibilityStatus::verified,
            "verified"
        };
    }
};

class Catalog final : public application::IWorkflowTemplateCatalog {
public:
    int revision{1};

    std::optional<domain::WorkflowTemplate> find(
        const std::string_view id,
        const std::string_view version
    ) const override {
        if (id != "org.biocore.template.batch-test" || version != "1.0.0") {
            return std::nullopt;
        }

        const std::string prep_version =
            revision == 1 ? "1.0.0" : "2.0.0";
        return domain::WorkflowTemplate{
            1U,
            "org.biocore.template.batch-test",
            "1.0.0",
            "Batch test",
            "Reusable batch planning fixture",
            domain::Workflow{
                1U,
                domain::WorkflowId{"org.biocore.template.batch-test"},
                "Batch blueprint",
                "",
                {
                    domain::WorkflowNode{
                        domain::WorkflowNodeId{"prep"},
                        "Prepare",
                        "org.biocore.test.prep",
                        prep_version,
                        {
                            domain::WorkflowInputDeclaration{
                                "reads", "fastq", true
                            },
                            domain::WorkflowInputDeclaration{
                                "reference", "fasta", true
                            },
                        },
                        {
                            domain::WorkflowOutputDeclaration{
                                "alignment", "bam"
                            },
                        },
                        {{"threads", "4"}}
                    },
                    domain::WorkflowNode{
                        domain::WorkflowNodeId{"qc"},
                        "QC",
                        "org.biocore.test.qc",
                        "1.0.0",
                        {
                            domain::WorkflowInputDeclaration{
                                "alignment", "bam", true
                            },
                        },
                        {
                            domain::WorkflowOutputDeclaration{
                                "report", "json"
                            },
                        },
                        {}
                    },
                },
                {
                    domain::WorkflowEdge{
                        domain::WorkflowNodeId{"prep"},
                        "alignment",
                        domain::WorkflowNodeId{"qc"},
                        "alignment"
                    },
                }
            }
        };
    }

    std::vector<application::RegisteredWorkflowTemplate> list() const override {
        return {{
            "org.biocore.template.batch-test",
            "1.0.0",
            "Batch test",
            "Reusable batch planning fixture"
        }};
    }
};

class Registry final : public application::IPluginRegistry {
public:
    std::optional<application::ResolvedPluginModule> find_module(
        const std::string_view id
    ) const override {
        if (id == "org.biocore.test.prep") {
            return application::ResolvedPluginModule{
                .plugin_id = "org.biocore.test",
                .plugin_version = "1.0.0",
                .module_id = std::string{id},
                .plugin_root_path = {},
                .executable_path = {},
                .parameters = {
                    domain::PluginParameterDefinition{
                        "threads",
                        domain::PluginParameterType::integer,
                        true,
                        domain::PluginParameterValue{std::int64_t{2}},
                        1.0,
                        64.0
                    },
                },
                .inputs = {
                    domain::PluginInputPortDefinition{
                        "reads", true, {"fastq"}
                    },
                    domain::PluginInputPortDefinition{
                        "reference", true, {"fasta"}
                    },
                },
                .outputs = {
                    domain::PluginOutputPortDefinition{
                        "alignment", "bam"
                    },
                },
            };
        }
        if (id == "org.biocore.test.qc") {
            return application::ResolvedPluginModule{
                .plugin_id = "org.biocore.test",
                .plugin_version = "1.0.0",
                .module_id = std::string{id},
                .plugin_root_path = {},
                .executable_path = {},
                .inputs = {
                    domain::PluginInputPortDefinition{
                        "alignment", true, {"bam"}
                    },
                },
                .outputs = {
                    domain::PluginOutputPortDefinition{
                        "report", "json"
                    },
                },
            };
        }
        return std::nullopt;
    }

    std::vector<application::RegisteredPlugin> list_plugins() const override {
        return {};
    }
};

[[nodiscard]] domain::ManagedFile managed_file(
    std::string id,
    std::string type,
    std::string relative,
    const char hash_character
) {
    return domain::ManagedFile{
        std::move(id),
        relative,
        domain::StorageMode::managed_copy,
        "/original/" + relative,
        "/project/" + relative,
        relative,
        std::move(type),
        16,
        std::nullopt,
        std::string{"sha256"},
        std::string(64U, hash_character),
        stamp,
        stamp,
    };
}

class Environment final {
public:
    Environment()
        : samples{connection},
          bindings{connection},
          files{connection},
          plans{connection},
          binding_validation{
              samples, bindings, files, storage, reference_inspector
          },
          planning{
              samples,
              bindings,
              files,
              binding_validation,
              catalog,
              registry,
              plans
          } {
        ProjectDatabaseInitializer{connection}.initialize(
            domain::Project{
                project_id, "Batch research", "/local/project", stamp, stamp
            }
        );

        const std::array<domain::ProjectSample, 2> seed_samples{{
            {project_id, "001", "One", "case"},
            {project_id, "002", "Two", "control"},
        }};
        check(
            samples.add_batch(project_id, seed_samples) ==
                application::SampleBatchAddResult::added,
            "sample seed failed"
        );

        check(
            files.add(managed_file("r1", "fastq", "inputs/r1.fastq", 'a')),
            "read seed failed"
        );
        check(
            files.add(managed_file("ref", "fasta", "inputs/ref.fa", 'b')),
            "reference seed failed"
        );

        bindings.upsert(domain::ProjectSampleBinding{
            project_id,
            "001",
            domain::SampleInputLayout::single_fastq,
            "r1",
            std::nullopt,
            std::string{"ref"},
        });
    }

    SqliteConnection connection{":memory:"};
    SqliteProjectSampleStore samples;
    SqliteProjectSampleBindingStore bindings;
    SqliteManagedFileRepository files;
    SqliteBatchPlanStore plans;
    FakeInputStorage storage;
    FakeReferenceInspector reference_inspector;
    application::SampleBindingService binding_validation;
    Catalog catalog;
    Registry registry;
    application::BatchPlanningService planning;
};

[[nodiscard]] application::BatchPlanRequest request(
    std::vector<std::string> samples = {"002", "001"},
    bool reverse_assignments = false
) {
    std::vector<application::BatchWorkflowInputAssignment> assignments{
        {
            domain::WorkflowNodeId{"prep"},
            "reads",
            application::BatchInputFileRole::primary
        },
        {
            domain::WorkflowNodeId{"prep"},
            "reference",
            application::BatchInputFileRole::reference
        },
    };
    if (reverse_assignments) std::ranges::reverse(assignments);

    return application::BatchPlanRequest{
        .plan_id = "batch.iteration-082",
        .project_id = project_id,
        .template_id = "org.biocore.template.batch-test",
        .template_version = "1.0.0",
        .sample_ids = std::move(samples),
        .parameter_overrides = {
            {
                domain::WorkflowNodeId{"prep"},
                "threads",
                "8"
            },
        },
        .input_assignments = std::move(assignments),
    };
}

[[nodiscard]] const application::BatchSamplePlanPreview& sample(
    const application::BatchPlanPreview& preview,
    const std::string_view id
) {
    const auto iterator = std::ranges::find_if(
        preview.samples,
        [id](const auto& value) { return value.sample_id == id; }
    );
    if (iterator == preview.samples.end()) {
        throw std::runtime_error{"sample preview missing"};
    }
    return *iterator;
}

void deterministic_contract() {
    Environment env;
    const auto first = env.planning.preview(
        request({"002", "001"}, true)
    );
    const auto second = env.planning.preview(
        request({"001", "002"}, false)
    );

    check(first == second, "semantic request order changed batch preview");
    check(
        first.samples.size() == 2U &&
        first.samples[0].sample_id == "001" &&
        first.samples[0].workflow_id == "batch.iteration-082.sample.s1" &&
        first.samples[1].sample_id == "002" &&
        first.samples[1].workflow_id == "batch.iteration-082.sample.s2",
        "deterministic sample/workflow ordering failed"
    );
}

void preview_contract() {
    Environment env;
    const auto preview = env.planning.preview(request());

    const auto& valid = sample(preview, "001");
    check(valid.valid(), "valid sample plan rejected");
    check(
        std::ranges::any_of(valid.issues, [](const auto& issue) {
            return issue.severity ==
                       application::BatchPlanIssueSeverity::warning &&
                   issue.code ==
                       "binding_reference_not_declared_by_format";
        }),
        "reference uncertainty warning missing"
    );
    check(valid.nodes.size() == 2U, "expected workflow nodes missing");

    const auto& prep = valid.nodes[0];
    check(
        prep.node_id == "prep" &&
        prep.module_id == "org.biocore.test.prep" &&
        prep.plugin_version == "1.0.0" &&
        prep.parameters.size() == 1U &&
        prep.parameters[0].name == "threads" &&
        prep.parameters[0].value == "8" &&
        prep.parameters[0].type ==
            domain::PluginParameterType::integer,
        "resolved parameter preview is incomplete"
    );
    check(
        prep.inputs.size() == 2U &&
        prep.outputs.size() == 1U &&
        prep.outputs[0].port_name == "alignment" &&
        prep.outputs[0].artifact_type == "bam",
        "input/output preview is incomplete"
    );
    check(
        std::ranges::all_of(prep.inputs, [](const auto& input) {
            return input.source_kind ==
                       application::BatchPlanInputSourceKind::managed_file &&
                   input.managed_file.has_value() &&
                   input.managed_file->sha256.size() == 64U;
        }),
        "managed input content identities are missing"
    );

    const auto& qc = valid.nodes[1];
    check(
        qc.inputs.size() == 1U &&
        qc.inputs[0].source_kind ==
            application::BatchPlanInputSourceKind::node_output &&
        qc.inputs[0].source_id == "prep" &&
        qc.inputs[0].source_port == "alignment" &&
        qc.outputs.size() == 1U &&
        qc.outputs[0].artifact_type == "json",
        "internal dependency or expected output preview is incomplete"
    );

    const auto& invalid = sample(preview, "002");
    check(
        !invalid.valid() &&
        std::ranges::any_of(invalid.issues, [](const auto& issue) {
            return issue.code == "sample_binding_missing";
        }),
        "invalid selected sample disappeared from preview"
    );
}

void exclusion_contract() {
    Environment env;
    const auto preview = env.planning.preview(request());

    rejects<std::invalid_argument>([&] {
        static_cast<void>(
            env.planning.approve(preview, {}, stamp)
        );
    });

    const auto approved =
        env.planning.approve(preview, {"002"}, stamp);
    check(
        approved.samples.size() == 2U &&
        approved.samples[0].sample_id == "001" &&
        approved.samples[0].disposition ==
            application::BatchPlanSampleDisposition::included &&
        approved.samples[1].sample_id == "002" &&
        approved.samples[1].disposition ==
            application::BatchPlanSampleDisposition::excluded,
        "explicit exclusion was not frozen into the plan"
    );

    const auto stored = env.plans.find("batch.iteration-082");
    check(stored.has_value() && *stored == approved,
          "approved batch plan did not round-trip");
}

void stale_contract() {
    Environment env;
    const auto preview =
        env.planning.preview(request({"001"}));

    env.storage.status["r1"] =
        application::ManagedFileIntegrityStatus::checksum_mismatch;

    rejects<std::runtime_error>([&] {
        static_cast<void>(
            env.planning.approve(preview, {}, stamp)
        );
    });
    check(
        !env.plans.find("batch.iteration-082").has_value(),
        "stale batch preview was persisted"
    );
}

void template_freeze_contract() {
    Environment env;
    const auto preview =
        env.planning.preview(request({"001"}));
    check(
        sample(preview, "001").nodes[0].plugin_version == "1.0.0",
        "template setup failed"
    );

    env.catalog.revision = 2;
    const auto approved =
        env.planning.approve(preview, {}, stamp);
    const auto stored = env.plans.find(approved.plan_id);

    check(
        stored.has_value() &&
        stored->samples[0].nodes[0].plugin_version == "1.0.0",
        "template mutation rewrote the approved preview snapshot"
    );
}

void persistence_contract() {
    Environment env;
    const auto preview =
        env.planning.preview(request({"001"}));
    static_cast<void>(
        env.planning.approve(preview, {}, stamp)
    );

    rejects<SqliteError>([&] {
        env.connection.execute(
            "UPDATE batch_plans SET template_version='9.9.9' "
            "WHERE plan_id='batch.iteration-082';"
        );
    });
    rejects<SqliteError>([&] {
        env.connection.execute(
            "INSERT INTO batch_plan_outputs("
            "plan_id,sample_id,node_id,ordinal,port_name,artifact_type"
            ") VALUES("
            "'batch.iteration-082','001','prep',99,'late','json');"
        );
    });
    rejects<SqliteError>([&] {
        env.connection.execute(
            "DELETE FROM batch_plans WHERE plan_id='batch.iteration-082';"
        );
    });

    const auto stored = env.plans.find("batch.iteration-082");
    check(
        stored.has_value() &&
        stored->template_version == "1.0.0" &&
        stored->samples[0].nodes[0].outputs.size() == 1U,
        "sealed batch plan changed after mutation attempts"
    );
}

void load_v12(SqliteConnection& connection) {
    std::ifstream input{
        std::filesystem::path{BIOCORE_SOURCE_ROOT} /
        "tests/fixtures/project-schema-v12.sql"
    };
    check(input.good(), "v12 fixture missing");
    const std::string sql{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}
    };
    connection.execute(sql);
    connection.execute(
        "INSERT INTO project_metadata("
        "singleton,project_id,name,root_path,created_at_utc,updated_at_utc,"
        "research_description,research_organism,research_revision,"
        "research_updated_at_utc"
        ") VALUES("
        "1,'p-001','Research','/local/project','t','t',"
        "'legacy research','Homo sapiens',2,'t2');"
        "INSERT INTO project_samples("
        "project_id,sample_id,display_name,group_label"
        ") VALUES('p-001','001','Sample','case');"
        "INSERT INTO managed_files("
        "id,display_name,storage_mode,original_path,managed_path,"
        "relative_project_path,file_type,size_bytes,checksum_algorithm,"
        "checksum_value,created_at_utc,updated_at_utc"
        ") VALUES("
        "'r1','r1.fastq','managed_copy','/o/r1','/m/r1','inputs/r1',"
        "'fastq',4,'sha256',"
        "'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa',"
        "'t','t');"
        "INSERT INTO project_sample_bindings("
        "project_id,sample_id,input_layout,primary_file_id,"
        "secondary_file_id,reference_file_id"
        ") VALUES('p-001','001','single_fastq','r1',NULL,NULL);"
    );
}

[[nodiscard]] std::int64_t scalar(
    SqliteConnection& connection,
    const char* sql
) {
    sqlite3_stmt* statement = nullptr;
    check(
        sqlite3_prepare_v2(
            connection.native_handle(), sql, -1, &statement, nullptr
        ) == SQLITE_OK,
        "scalar prepare failed"
    );
    check(sqlite3_step(statement) == SQLITE_ROW, "scalar row missing");
    const auto value = sqlite3_column_int64(statement, 0);
    sqlite3_finalize(statement);
    return value;
}

void migration_contract() {
    SqliteConnection connection{":memory:"};
    load_v12(connection);
    ProjectMigrationRunner migrations{connection};
    check(migrations.current_version() == 12, "fixture is not v12");

    migrations.apply_pending();
    ProjectDatabaseGuard{connection}.validate_current_schema();

    check(
        migrations.current_version() == 13 &&
        scalar(connection, "SELECT COUNT(*) FROM project_samples;") == 1 &&
        scalar(connection, "SELECT COUNT(*) FROM project_sample_bindings;") == 1 &&
        scalar(connection, "SELECT COUNT(*) FROM managed_files;") == 1 &&
        scalar(connection, "SELECT COUNT(*) FROM batch_plans;") == 0,
        "v12 to v13 migration did not preserve legacy project data"
    );
}

void rollback_contract() {
    SqliteConnection connection{":memory:"};
    load_v12(connection);
    connection.execute(
        "CREATE TRIGGER reject_v13 BEFORE INSERT ON schema_migrations "
        "WHEN NEW.version=13 "
        "BEGIN SELECT RAISE(ABORT,'injected v13 failure'); END;"
    );

    rejects<SqliteError>([&] {
        ProjectMigrationRunner{connection}.apply_pending();
    });
    check(
        ProjectMigrationRunner{connection}.current_version() == 12 &&
        scalar(
            connection,
            "SELECT COUNT(*) FROM sqlite_master "
            "WHERE type='table' AND name='batch_plans';"
        ) == 0 &&
        scalar(connection, "SELECT COUNT(*) FROM project_sample_bindings;") == 1,
        "failed v13 migration left partial schema or damaged v12 data"
    );

    connection.execute("DROP TRIGGER reject_v13;");
    ProjectMigrationRunner{connection}.apply_pending();
    ProjectDatabaseGuard{connection}.validate_current_schema();
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        if (argc != 2) return EXIT_FAILURE;
        const std::string_view mode{argv[1]};
        if (mode == "deterministic") deterministic_contract();
        else if (mode == "preview") preview_contract();
        else if (mode == "exclusion") exclusion_contract();
        else if (mode == "stale") stale_contract();
        else if (mode == "template-freeze") template_freeze_contract();
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
