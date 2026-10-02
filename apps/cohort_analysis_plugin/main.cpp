#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include "biocore/application/build_info.hpp"
#include "biocore/application/cohort_analysis_selection_service.hpp"
#include "biocore/application/cohort_case_control_analysis_service.hpp"
#include "biocore/application/cohort_matrix_service.hpp"
#include "biocore/application/cohort_results_service.hpp"
#include "biocore/infrastructure/filesystem_input_file_storage.hpp"
#include "biocore/infrastructure/filesystem_reference_genome_reader.hpp"
#include "biocore/infrastructure/filesystem_reference_manifest_reader.hpp"
#include "biocore/infrastructure/filesystem_result_artifact_reader.hpp"
#include "biocore/infrastructure/sqlite/project_database_guard.hpp"
#include "biocore/infrastructure/sqlite/sqlite_batch_execution_store.hpp"
#include "biocore/infrastructure/sqlite/sqlite_batch_plan_store.hpp"
#include "biocore/infrastructure/sqlite/sqlite_cohort_analysis_snapshot_store.hpp"
#include "biocore/infrastructure/sqlite/sqlite_cohort_registry_store.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_job_repository.hpp"
#include "biocore/infrastructure/sqlite/sqlite_managed_file_repository.hpp"
#include "biocore/infrastructure/system_clock.hpp"
#include "biocore/plugin_protocol/plugin_document_codec.hpp"
#include "biocore/presentation/cohort_result_report.hpp"

namespace {
using namespace biocore;

[[nodiscard]] std::string read_invocation(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_regular_file(status)) {
        throw std::invalid_argument(
            "Cohort-analysis invocation must be a regular non-symlink file"
        );
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0U ||
        size > plugin_protocol::maximum_plugin_invocation_bytes) {
        throw std::invalid_argument("Cohort-analysis invocation size is invalid");
    }
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("Unable to open cohort-analysis invocation");
    std::string text{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}
    };
    if (input.bad() || text.size() != size) {
        throw std::runtime_error("Unable to read cohort-analysis invocation");
    }
    return text;
}

[[nodiscard]] std::string parameter(
    const plugin_protocol::PluginInvocationDocument& document,
    const std::string_view name
) {
    for (const auto& item : document.parameters) {
        if (item.name == name) {
            if (item.type != "string" || item.value.empty()) {
                throw std::invalid_argument(
                    "Cohort-analysis identity parameter is invalid"
                );
            }
            return item.value;
        }
    }
    throw std::invalid_argument("Cohort-analysis identity parameter is missing");
}

[[nodiscard]] const plugin_protocol::PluginInvocationOutputDocument& output(
    const plugin_protocol::PluginInvocationDocument& document,
    const std::string_view port
) {
    for (const auto& item : document.outputs) {
        if (item.port == port) return item;
    }
    throw std::invalid_argument("Cohort-analysis output binding is missing");
}

void validate_document(const plugin_protocol::PluginInvocationDocument& document) {
    if (document.module_id != "org.biocore.cohortanalysis.analysis") {
        throw std::invalid_argument("Unexpected cohort-analysis module id");
    }
    if (!document.inputs.empty()) {
        throw std::invalid_argument("Cohort-analysis plugin does not accept direct inputs");
    }
    if (document.parameters.size() != 3U || document.outputs.size() != 3U) {
        throw std::invalid_argument("Cohort-analysis invocation contract is incomplete");
    }
    static_cast<void>(parameter(document, "project_id"));
    static_cast<void>(parameter(document, "analysis_id"));
    static_cast<void>(parameter(document, "snapshot_digest"));
    if (output(document, "manifest").file_type != "json" ||
        output(document, "table").file_type != "tsv" ||
        output(document, "report").file_type != "html") {
        throw std::invalid_argument("Cohort-analysis output contract is invalid");
    }
}

[[nodiscard]] std::filesystem::path project_root_from_invocation(
    const std::filesystem::path& invocation
) {
    std::error_code error;
    const auto canonical = std::filesystem::canonical(invocation, error);
    if (error) throw std::invalid_argument("Invocation path could not be canonicalized");
    auto root = canonical.parent_path();
    for (int i = 0; i < 4; ++i) root = root.parent_path();
    const auto database = root / ".biocore" / "project.sqlite";
    const auto status = std::filesystem::symlink_status(database, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_regular_file(status)) {
        throw std::invalid_argument(
            "Cohort-analysis invocation is not inside an OpenGenesis-BioCore project"
        );
    }
    return root;
}

[[nodiscard]] application::CohortAnalysisSelectionRequest selection_from_snapshot(
    const application::CohortAnalysisSnapshot& snapshot
) {
    application::CohortAnalysisSelectionRequest request{
        .project_id = snapshot.project_id,
        .cohort_id = snapshot.cohort_id,
        .cohort_revision = snapshot.cohort_revision,
        .reference = {
            .managed_file_id = snapshot.reference.managed_file_id,
            .assembly = snapshot.reference.assembly,
            .custom_assembly_id = snapshot.reference.custom_assembly_id,
            .normalization_contract_version =
                snapshot.reference.normalization_contract_version,
            .aliases = snapshot.reference.aliases,
        },
        .selections = {},
    };
    request.selections.reserve(snapshot.sources.size());
    for (const auto& source : snapshot.sources) {
        request.selections.push_back({
            .project_sample_id = source.project_sample_id,
            .plan_id = source.plan_id,
            .attempt_number = source.attempt_number,
            .job_id = source.job_id,
            .step_id = source.step_id,
            .output_port = source.output_port,
            .managed_file_id = source.managed_file_id,
            .expected_sha256 = source.sha256,
            .vcf_sample_name = source.vcf_sample_name,
        });
    }
    return request;
}

void write_text(const std::filesystem::path& path, const std::string_view text) {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    if (!out) throw std::runtime_error("Unable to open cohort-analysis output");
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (!out) throw std::runtime_error("Unable to write cohort-analysis output");
}

int run(
    const std::filesystem::path& invocation_path,
    const std::string_view module_id,
    const std::string_view step_id
) {
    const auto document = plugin_protocol::parse_plugin_invocation_document(
        read_invocation(invocation_path)
    );
    validate_document(document);
    if (document.module_id != module_id || document.step_id != step_id) {
        throw std::invalid_argument("Cohort-analysis invocation identity mismatch");
    }

    const std::string project_id = parameter(document, "project_id");
    const std::string analysis_id = parameter(document, "analysis_id");
    const std::string snapshot_digest = parameter(document, "snapshot_digest");
    const auto project_root = project_root_from_invocation(invocation_path);

    infrastructure::sqlite::SqliteConnection connection{
        project_root / ".biocore" / "project.sqlite"
    };
    infrastructure::sqlite::ProjectDatabaseGuard{connection}.validate_current_schema();
    infrastructure::sqlite::SqliteCohortRegistryStore cohorts{connection};
    infrastructure::sqlite::SqliteBatchPlanStore plans{connection};
    infrastructure::sqlite::SqliteBatchExecutionStore executions{connection};
    infrastructure::sqlite::SqliteJobRepository jobs{connection};
    infrastructure::sqlite::SqliteManagedFileRepository files{connection};
    infrastructure::sqlite::SqliteCohortAnalysisSnapshotStore snapshots{connection};
    infrastructure::FilesystemInputFileStorage input_storage{project_root.string()};
    infrastructure::FilesystemResultArtifactReader artifact_reader{project_root};
    infrastructure::FilesystemReferenceManifestReader reference_manifest_reader{
        input_storage
    };
    infrastructure::FilesystemReferenceGenomeReader reference_genome_reader{
        input_storage
    };

    const auto snapshot = snapshots.find(project_id, analysis_id);
    if (!snapshot.has_value() || snapshot->snapshot_digest != snapshot_digest) {
        throw std::invalid_argument(
            "Cohort-analysis snapshot identity does not match the queued job"
        );
    }

    application::CohortAnalysisSelectionService selection_service{
        cohorts, plans, executions, jobs, files, input_storage, artifact_reader,
        reference_manifest_reader
    };
    application::CohortMatrixService matrix_service{
        selection_service, files, artifact_reader, reference_genome_reader
    };
    const auto matrix = matrix_service.build(selection_from_snapshot(*snapshot));
    application::CohortCaseControlAnalysisService association_service{snapshots};
    const auto association = association_service.analyze(
        project_id, analysis_id, matrix.matrix
    );

    application::CohortResultsService results{snapshots};
    infrastructure::SystemClock clock;
    const auto package = results.build_package(
        project_id,
        analysis_id,
        association,
        application::CohortResultQuery{},
        std::string{application::BuildInfo::version()},
        clock.now_utc_iso8601()
    );

    write_text(
        std::filesystem::path{output(document, "manifest").path},
        presentation::render_cohort_result_package_json(package)
    );
    write_text(
        std::filesystem::path{output(document, "table").path},
        presentation::render_cohort_result_package_tsv(package)
    );
    write_text(
        std::filesystem::path{output(document, "report").path},
        presentation::render_cohort_result_package_html(package)
    );
    return EXIT_SUCCESS;
}

}  // namespace

int main(const int argc, const char* const argv[]) {
    if (argc != 7 || std::string_view{argv[1]} != "--module-id" ||
        std::string_view{argv[3]} != "--step-id" ||
        std::string_view{argv[5]} != "--invocation") {
        std::cerr
            << "Usage: biocore-cohort-analysis-plugin --module-id <module-id> "
               "--step-id <step-id> --invocation <snapshot-path>\n";
        return 2;
    }
    try {
        return run(std::filesystem::path{argv[6]}, argv[2], argv[4]);
    } catch (const std::exception& error) {
        std::cerr << "Cohort analysis failed: " << error.what() << '\n';
        return 3;
    }
}
