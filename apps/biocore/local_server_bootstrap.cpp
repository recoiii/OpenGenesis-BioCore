#include "local_server_bootstrap.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "biocore/application/artifact_presentation_service.hpp"
#include "biocore/application/job_scheduler.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/job_submission_service.hpp"
#include "biocore/application/managed_file_service.hpp"
#include "biocore/application/output_artifact_cleanup_service.hpp"
#include "biocore/application/output_artifact_service.hpp"
#include "biocore/application/pipeline_preparation_service.hpp"
#include "biocore/application/project_recovery_service.hpp"
#include "biocore/application/worker_runtime.hpp"
#include "biocore/infrastructure/filesystem_artifact_content_access.hpp"
#include "biocore/infrastructure/filesystem_input_file_storage.hpp"
#include "biocore/infrastructure/filesystem_output_artifact_inspector.hpp"
#include "biocore/infrastructure/filesystem_partial_output_cleaner.hpp"
#include "biocore/infrastructure/filesystem_pipeline_catalog.hpp"
#include "biocore/infrastructure/filesystem_plugin_registry.hpp"
#include "biocore/infrastructure/filesystem_quarantine_retention_store.hpp"
#include "biocore/infrastructure/json_execution_plan_store.hpp"
#include "biocore/infrastructure/monotonic_clock.hpp"
#include "biocore/infrastructure/platform_worker_supervisor.hpp"
#include "biocore/infrastructure/secure_token.hpp"
#include "biocore/infrastructure/sqlite/project_database_guard.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_job_repository.hpp"
#include "biocore/infrastructure/sqlite/sqlite_managed_file_repository.hpp"
#include "biocore/infrastructure/sqlite/sqlite_prepared_job_store.hpp"
#include "biocore/infrastructure/system_clock.hpp"
#include "biocore/infrastructure/uuid_v4_generator.hpp"
#include "biocore/presentation/local_api.hpp"
#include "biocore/presentation/local_browser_session.hpp"
#include "biocore/presentation/worker_lifecycle_event_broadcast.hpp"
#include "biocore/presentation/local_web_server.hpp"

namespace biocore::bootstrap {
namespace {

[[nodiscard]] std::filesystem::path require_directory(
    const std::filesystem::path& path,
    const char* description
) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status)) {
        throw std::invalid_argument(std::string{description} + " must be an existing non-symlink directory");
    }
    const auto canonical = std::filesystem::canonical(path, error);
    if (error) throw std::invalid_argument(std::string{description} + " could not be canonicalized");
    return canonical;
}

[[nodiscard]] std::filesystem::path require_regular(
    const std::filesystem::path& path,
    const char* description
) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
        throw std::invalid_argument(std::string{description} + " must be an existing non-symlink regular file");
    }
    const auto canonical = std::filesystem::canonical(path, error);
    if (error) throw std::invalid_argument(std::string{description} + " could not be canonicalized");
    return canonical;
}

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string encoded = path.generic_u8string();
    std::string result;
    result.reserve(encoded.size());
    for (const char8_t character : encoded) {
        result.push_back(static_cast<char>(character));
    }
    return result;
}

[[nodiscard]] std::filesystem::path worker_name() {
#ifdef _WIN32
    return "biocore-worker.exe";
#else
    return "biocore-worker";
#endif
}

[[nodiscard]] std::optional<RuntimeAssets> try_build_layout(const std::filesystem::path& executable) {
    const auto build_root = executable.parent_path().parent_path().parent_path();
    std::error_code error;
    const auto pipeline = build_root / "pipelines";
    const auto plugins = build_root / "plugins";
    const auto worker = build_root / "apps" / "worker" / worker_name();
    if (!std::filesystem::is_directory(pipeline, error) || error) return std::nullopt;
    error.clear();
    if (!std::filesystem::is_directory(plugins, error) || error) return std::nullopt;
    error.clear();
    if (!std::filesystem::is_regular_file(worker, error) || error) return std::nullopt;
    return RuntimeAssets{require_directory(pipeline, "Pipeline root"), require_directory(plugins, "Plugin root"), require_regular(worker, "Worker executable")};
}

[[nodiscard]] std::optional<RuntimeAssets> try_install_layout(const std::filesystem::path& executable) {
    const auto prefix = executable.parent_path().parent_path();
    const auto data = prefix / "share" / "biocore";
    const auto pipeline = data / "pipelines";
    const auto plugins = data / "plugins";
    const auto worker = executable.parent_path() / worker_name();
    std::error_code error;
    if (!std::filesystem::is_directory(pipeline, error) || error) return std::nullopt;
    error.clear();
    if (!std::filesystem::is_directory(plugins, error) || error) return std::nullopt;
    error.clear();
    if (!std::filesystem::is_regular_file(worker, error) || error) return std::nullopt;
    return RuntimeAssets{require_directory(pipeline, "Pipeline root"), require_directory(plugins, "Plugin root"), require_regular(worker, "Worker executable")};
}

class RuntimeGuard final {
public:
    explicit RuntimeGuard(application::WorkerRuntime& runtime) : runtime_{runtime} { runtime_.start(); }
    ~RuntimeGuard() { runtime_.stop(); }
    RuntimeGuard(const RuntimeGuard&) = delete;
    RuntimeGuard& operator=(const RuntimeGuard&) = delete;
private:
    application::WorkerRuntime& runtime_;
};

}  // namespace

std::filesystem::path validated_project_root(std::filesystem::path root) {
    root = require_directory(root, "Project root");
    const auto database_path = root / ".biocore" / "project.sqlite";
    static_cast<void>(require_regular(database_path, "Existing .biocore/project.sqlite"));
    return root;
}

[[nodiscard]] std::filesystem::path resolve_frontend_root(
    const ServeArguments& arguments
) {
    if (arguments.frontend_root.has_value()) {
        return require_directory(*arguments.frontend_root, "Frontend root");
    }
    if (arguments.executable_path.empty()) {
        throw std::invalid_argument(
            "Executable path or explicit frontend root is required to resolve OpenGenesis-BioCore frontend assets"
        );
    }

    const auto executable = require_regular(arguments.executable_path, "OpenGenesis-BioCore executable");
    const auto build_root = executable.parent_path().parent_path().parent_path();
    std::error_code error;
    const auto build_frontend = build_root / "frontend";
    if (std::filesystem::is_directory(build_frontend, error) && !error) {
        return require_directory(build_frontend, "Frontend root");
    }

    error.clear();
    const auto install_frontend =
        executable.parent_path().parent_path() / "share" / "biocore" / "frontend";
    if (std::filesystem::is_directory(install_frontend, error) && !error) {
        return require_directory(install_frontend, "Frontend root");
    }

    throw std::invalid_argument(
        "OpenGenesis-BioCore frontend assets could not be resolved beside the executable"
    );
}

RuntimeAssets resolve_runtime_assets(const ServeArguments& arguments) {
    if (arguments.pipeline_root.has_value() || arguments.plugin_root.has_value() ||
        arguments.worker_executable.has_value()) {
        if (!arguments.pipeline_root.has_value() || !arguments.plugin_root.has_value() ||
            !arguments.worker_executable.has_value()) {
            throw std::invalid_argument("Runtime asset overrides must provide pipeline, plugin, and worker paths together");
        }
        return RuntimeAssets{
            require_directory(*arguments.pipeline_root, "Pipeline root"),
            require_directory(*arguments.plugin_root, "Plugin root"),
            require_regular(*arguments.worker_executable, "Worker executable"),
        };
    }
    if (arguments.executable_path.empty()) {
        throw std::invalid_argument("Executable path is required to resolve OpenGenesis-BioCore runtime assets");
    }
    const auto executable = require_regular(arguments.executable_path, "OpenGenesis-BioCore executable");
    if (const auto build = try_build_layout(executable); build.has_value()) return *build;
    if (const auto installed = try_install_layout(executable); installed.has_value()) return *installed;
    throw std::invalid_argument("OpenGenesis-BioCore runtime assets could not be resolved beside the executable");
}

int run_local_server(
    const ServeArguments& arguments,
    presentation::ILocalWebServer& server,
    std::ostream& standard_output,
    std::ostream& standard_error
) {
    if (!server.available()) {
        standard_error << "OpenGenesis-BioCore local server unavailable: this build does not include Drogon.\n";
        return 3;
    }
    if (arguments.maximum_concurrent_jobs == 0U) {
        throw std::invalid_argument("Maximum concurrent jobs must be greater than zero");
    }

    const std::filesystem::path root = validated_project_root(arguments.project_root);
    const RuntimeAssets assets = resolve_runtime_assets(arguments);
    const std::filesystem::path frontend_root = resolve_frontend_root(arguments);
    const auto project_database_path = root / ".biocore" / "project.sqlite";
    {
        infrastructure::sqlite::SqliteConnection migration_connection{project_database_path};
        infrastructure::sqlite::ProjectDatabaseGuard database_guard{migration_connection};
        database_guard.validate_before_migration();
        infrastructure::sqlite::ProjectMigrationRunner migrations{migration_connection};
        migrations.apply_pending();
        database_guard.validate_current_schema();
    }

    // The HTTP adapter is intentionally single-threaded in Core 0.1 and owns a
    // dedicated SQLite connection. WorkerRuntime runs on its own thread and owns
    // a separate connection. SQLite FULLMUTEX protects individual calls, but a
    // shared connection would still allow multi-statement transactions from the
    // two execution contexts to interleave. Separate WAL connections preserve
    // transaction ownership across the HTTP/runtime boundary.
    infrastructure::sqlite::SqliteConnection api_connection{project_database_path};
    infrastructure::sqlite::SqliteConnection runtime_connection{project_database_path};

    infrastructure::sqlite::SqliteJobRepository api_job_repository{api_connection};
    infrastructure::sqlite::SqlitePreparedJobStore api_prepared_jobs{api_connection};
    infrastructure::sqlite::SqliteManagedFileRepository api_managed_files{api_connection};
    infrastructure::sqlite::SqliteJobRepository runtime_job_repository{runtime_connection};
    infrastructure::sqlite::SqlitePreparedJobStore runtime_prepared_jobs{runtime_connection};
    infrastructure::sqlite::SqliteManagedFileRepository runtime_managed_files{runtime_connection};

    infrastructure::SystemClock clock;
    infrastructure::MonotonicClock monotonic_clock;
    infrastructure::UuidV4Generator api_ids;
    infrastructure::UuidV4Generator runtime_ids;
    application::JobService api_jobs{api_job_repository, api_ids, clock};
    application::JobService runtime_jobs{runtime_job_repository, runtime_ids, clock};
    infrastructure::FilesystemInputFileStorage api_input_storage{path_to_utf8(root)};
    application::ManagedFileService managed_files{
        api_managed_files, api_input_storage, api_ids, clock, monotonic_clock
    };

    infrastructure::FilesystemArtifactContentAccess content_access{root};
    application::ArtifactPresentationService artifacts{api_managed_files, api_job_repository, content_access, clock};
    infrastructure::FilesystemPartialOutputCleaner partial_cleaner{root};
    application::OutputArtifactCleanupService api_cleanup{api_managed_files, partial_cleaner};
    application::OutputArtifactCleanupService runtime_cleanup{runtime_managed_files, partial_cleaner};
    infrastructure::FilesystemQuarantineRetentionStore retention{root};
    application::ProjectRecoveryService recovery{api_jobs, api_managed_files, &api_cleanup, &retention};
    const auto recovery_result = recovery.recover();

    infrastructure::FilesystemPipelineCatalog pipelines{assets.pipeline_root};
    const auto pipeline_report = pipelines.refresh();
    if (pipeline_report.loaded_pipelines == 0U) {
        throw std::runtime_error("No valid pipelines are available to the local server");
    }
    infrastructure::FilesystemPluginRegistry plugins{{assets.plugin_root}};
    const auto plugin_report = plugins.refresh();
    if (plugin_report.loaded_modules == 0U) {
        throw std::runtime_error("No valid plugin modules are available to the local server");
    }
    infrastructure::JsonExecutionPlanStore execution_plans{root};
    application::PipelinePreparationService preparation{execution_plans, plugins, api_managed_files};
    application::JobSubmissionService submissions{api_prepared_jobs, pipelines, preparation, execution_plans, api_ids, clock};

    infrastructure::PlatformWorkerSupervisor supervisor{assets.worker_executable, root};
    application::JobScheduler scheduler{runtime_jobs, runtime_prepared_jobs, supervisor, arguments.maximum_concurrent_jobs};
    infrastructure::FilesystemOutputArtifactInspector output_inspector{root};
    application::OutputArtifactService output_artifacts{runtime_managed_files, output_inspector, runtime_ids, clock};
    presentation::WorkerLifecycleEventBroadcastHub lifecycle_events;
    application::WorkerRuntime runtime{
        scheduler, runtime_jobs, supervisor, monotonic_clock, application::WorkerRuntimePolicy{},
        &output_artifacts, &runtime_cleanup, &lifecycle_events
    };

    const std::string token = infrastructure::generate_secure_token_hex();
    presentation::LocalBrowserSession browser_session{
        arguments.port, infrastructure::generate_secure_token_hex()
    };
    presentation::LocalApiController api{
        api_jobs, submissions, managed_files, artifacts, clock, token, browser_session
    };
    standard_output << "OpenGenesis-BioCore project recovery: " << recovery_result.recovered_jobs.size()
                    << " stale job(s) interrupted, " << recovery_result.issues.size() << " issue(s).\n";
    standard_output << "OpenGenesis-BioCore pipelines: " << pipeline_report.loaded_pipelines
                    << ", plugin modules: " << plugin_report.loaded_modules << ".\n";
    standard_output << "OpenGenesis-BioCore local API: http://127.0.0.1:" << arguments.port << "/api/v1/health\n";
    standard_output << "OpenGenesis-BioCore UI: http://127.0.0.1:" << arguments.port << "/\n";
    standard_output << "Bootstrap bearer token: " << token << '\n';
    standard_output.flush();

    RuntimeGuard runtime_guard{runtime};
    server.run(api, lifecycle_events, presentation::LocalWebServerConfig{
        .bind_address = "127.0.0.1",
        .port = arguments.port,
        .worker_threads = 1U,
        .frontend_root = frontend_root,
    });
    return 0;
}

}  // namespace biocore::bootstrap
