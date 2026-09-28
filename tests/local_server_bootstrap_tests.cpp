#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "local_server_bootstrap.hpp"
#include "project_init_bootstrap.hpp"
#include "biocore/application/project_service_error.hpp"
#include "biocore/domain/job.hpp"
#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/job_status.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_job_repository.hpp"
#include "biocore/presentation/local_api.hpp"
#include "biocore/presentation/frontend_asset_store.hpp"
#include "biocore/presentation/local_web_server.hpp"
#include "biocore/presentation/worker_lifecycle_event_broadcast.hpp"

namespace {

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

[[nodiscard]] std::string response_header(
    const biocore::presentation::LocalHttpResponse& response,
    const std::string_view name
) {
    const auto found = std::ranges::find_if(
        response.headers,
        [name](const auto& header) { return header.first == name; }
    );
    return found == response.headers.end() ? std::string{} : found->second;
}

[[nodiscard]] std::string path_tail(const std::string_view value) {
    const auto slash = value.find_last_of('/');
    return std::string{slash == std::string_view::npos ? value : value.substr(slash + 1U)};
}

class FakeServer final : public biocore::presentation::ILocalWebServer {
public:
    bool available() const noexcept override { return true; }
    std::string_view backend_name() const noexcept override { return "fake"; }
    void run(
        biocore::presentation::LocalApiController& api,
        biocore::presentation::WorkerLifecycleEventBroadcastHub& lifecycle_events,
        const biocore::presentation::LocalWebServerConfig& config
    ) override {
        called = true;
        seen_config = config;
        biocore::presentation::FrontendAssetStore frontend{config.frontend_root};
        const auto frontend_index = frontend.find("/");
        require(frontend_index.has_value() &&
                    frontend_index->body.find("OpenGenesis-BioCore") != std::string::npos,
                "frontend assets must be available before server run");
        std::mutex lifecycle_mutex;
        std::vector<std::string> lifecycle_messages;
        const auto lifecycle_subscription = lifecycle_events.subscribe_paused(
            [&](const std::string_view message) {
                std::scoped_lock lock{lifecycle_mutex};
                lifecycle_messages.emplace_back(message);
            }
        );
        require(lifecycle_events.activate(lifecycle_subscription),
                "bootstrap lifecycle subscription activation");
        const std::string authorization = "Bearer " + std::string{api.bootstrap_token()};
        const std::string browser_origin =
            config.port == 80U ? "http://127.0.0.1" :
            "http://127.0.0.1:" + std::to_string(config.port);
        const auto browser_exchange = api.handle({
            .method = biocore::presentation::HttpMethod::post,
            .target = "/api/v1/session",
            .authorization = {},
            .browser_session = {},
            .origin = browser_origin,
            .body = std::string{"{\"bootstrapToken\":\""} +
                    std::string{api.bootstrap_token()} + "\"}",
        });
        require(browser_exchange.status == 200,
                "composition root browser-session exchange");
        const auto cookie = std::ranges::find_if(
            browser_exchange.headers,
            [](const auto& header) { return header.first == "Set-Cookie"; }
        );
        require(cookie != browser_exchange.headers.end(),
                "composition root must issue browser session cookie");
        require(cookie->second.find(std::string{api.bootstrap_token()}) == std::string::npos,
                "composition root must not reuse bearer as browser-session secret");

        constexpr std::string_view cookie_prefix = "biocore_session=";
        const auto token_begin = cookie->second.find(cookie_prefix);
        require(token_begin != std::string::npos, "composition root session cookie name");
        const auto token_value_begin = token_begin + cookie_prefix.size();
        const auto token_end = cookie->second.find(';', token_value_begin);
        const std::string browser_token = cookie->second.substr(
            token_value_begin, token_end == std::string::npos ? std::string::npos
                                                             : token_end - token_value_begin
        );
        require(!browser_token.empty(), "composition root browser session token extraction");

        const auto workspace_initial = api.handle({
            .method = biocore::presentation::HttpMethod::get,
            .target = "/api/v1/project-workspace",
            .authorization = {},
            .browser_session = browser_token,
            .origin = {},
            .body = {},
        });
        require(workspace_initial.status == 200 &&
                    workspace_initial.body.find("\"id\":\"workspace-project\"") != std::string::npos,
                "project workspace snapshot must expose current project");

        const std::string sample_csv =
            "sample_id,display_name,group\nS1,Workspace Sample,case\n";
        const auto sample_preview = api.handle({
            .method = biocore::presentation::HttpMethod::post,
            .target = "/api/v1/project-workspace/samples/import/csv/preview",
            .authorization = {},
            .browser_session = browser_token,
            .origin = browser_origin,
            .content_type = "text/csv",
            .body = sample_csv,
        });
        require(sample_preview.status == 200 &&
                    sample_preview.body.find("\"valid\":true") != std::string::npos &&
                    sample_preview.body.find("\"sampleId\":\"S1\"") != std::string::npos,
                "project workspace sample import preview");
        const auto sample_commit = api.handle({
            .method = biocore::presentation::HttpMethod::post,
            .target = "/api/v1/project-workspace/samples/import/csv/commit",
            .authorization = {},
            .browser_session = browser_token,
            .origin = browser_origin,
            .content_type = "text/csv",
            .body = sample_csv,
        });
        require(sample_commit.status == 201 &&
                    sample_commit.body.find("\"valid\":true") != std::string::npos,
                "project workspace sample import commit");

        const std::string fastq =
            "@read-1\nACGTACGTACGTACGTACGTACGT\n+\nIIIIIIIIIIIIIIIIIIIIIIII\n";
        const auto upload_start = api.handle({
            .method = biocore::presentation::HttpMethod::post,
            .target = "/api/v1/files/uploads",
            .authorization = {},
            .browser_session = browser_token,
            .origin = browser_origin,
            .content_type = "application/json",
            .body = std::string{"{\"displayName\":\"S1.fastq\",\"fileType\":\"fastq\",\"sizeBytes\":"} +
                    std::to_string(fastq.size()) + "}",
        });
        require(upload_start.status == 201, "project workspace FASTQ upload start");
        const std::string upload_location = response_header(upload_start, "Location");
        const std::string upload_id = path_tail(upload_location);
        require(!upload_id.empty(), "project workspace upload id");
        const auto upload_chunk = api.handle({
            .method = biocore::presentation::HttpMethod::post,
            .target = "/api/v1/files/uploads/" + upload_id + "/chunks",
            .authorization = {},
            .browser_session = browser_token,
            .origin = browser_origin,
            .content_type = "application/octet-stream",
            .upload_offset = "0",
            .body = fastq,
        });
        require(upload_chunk.status == 200, "project workspace FASTQ upload chunk");
        const auto upload_complete = api.handle({
            .method = biocore::presentation::HttpMethod::post,
            .target = "/api/v1/files/uploads/" + upload_id + "/complete",
            .authorization = {},
            .browser_session = browser_token,
            .origin = browser_origin,
            .body = {},
        });
        require(upload_complete.status == 201, "project workspace FASTQ upload completion");
        const std::string file_id = path_tail(response_header(upload_complete, "Location"));
        require(!file_id.empty(), "project workspace uploaded managed file id");

        const std::string orphan_fastq =
            "@orphan\nACGTACGTACGTACGTACGTACGT\n+\nIIIIIIIIIIIIIIIIIIIIIIII\n";
        const auto orphan_start = api.handle({
            .method = biocore::presentation::HttpMethod::post,
            .target = "/api/v1/files/uploads",
            .authorization = {},
            .browser_session = browser_token,
            .origin = browser_origin,
            .content_type = "application/json",
            .body = std::string{"{\"displayName\":\"orphan.fastq\",\"fileType\":\"fastq\",\"sizeBytes\":"} +
                    std::to_string(orphan_fastq.size()) + "}",
        });
        require(orphan_start.status == 201, "project workspace orphan upload start");
        const std::string orphan_id = path_tail(response_header(orphan_start, "Location"));
        const auto orphan_chunk = api.handle({
            .method = biocore::presentation::HttpMethod::post,
            .target = "/api/v1/files/uploads/" + orphan_id + "/chunks",
            .authorization = {},
            .browser_session = browser_token,
            .origin = browser_origin,
            .content_type = "application/octet-stream",
            .upload_offset = "0",
            .body = orphan_fastq,
        });
        require(orphan_chunk.status == 200, "project workspace orphan upload chunk");
        const auto orphan_complete = api.handle({
            .method = biocore::presentation::HttpMethod::post,
            .target = "/api/v1/files/uploads/" + orphan_id + "/complete",
            .authorization = {},
            .browser_session = browser_token,
            .origin = browser_origin,
            .body = {},
        });
        require(orphan_complete.status == 201, "project workspace orphan upload completion");

        const std::string binding_body =
            std::string{"{\"layout\":\"single_fastq\",\"primaryFileId\":\""} +
            file_id + "\"}";
        const auto binding_preview = api.handle({
            .method = biocore::presentation::HttpMethod::post,
            .target = "/api/v1/project-workspace/samples/S1/binding/preview",
            .authorization = {},
            .browser_session = browser_token,
            .origin = browser_origin,
            .content_type = "application/json",
            .body = binding_body,
        });
        require(binding_preview.status == 200 &&
                    binding_preview.body.find("\"valid\":true") != std::string::npos,
                "project workspace binding preview");
        const auto binding_commit = api.handle({
            .method = biocore::presentation::HttpMethod::post,
            .target = "/api/v1/project-workspace/samples/S1/binding/commit",
            .authorization = {},
            .browser_session = browser_token,
            .origin = browser_origin,
            .content_type = "application/json",
            .body = binding_body,
        });
        require(binding_commit.status == 200 &&
                    binding_commit.body.find("\"valid\":true") != std::string::npos,
                "project workspace binding commit");

        const auto workspace_bound = api.handle({
            .method = biocore::presentation::HttpMethod::get,
            .target = "/api/v1/project-workspace",
            .authorization = {},
            .browser_session = browser_token,
            .origin = {},
            .body = {},
        });
        require(workspace_bound.status == 200 &&
                    workspace_bound.body.find("\"sampleId\":\"S1\"") != std::string::npos &&
                    workspace_bound.body.find("\"bindingComplete\":true") != std::string::npos &&
                    workspace_bound.body.find("\"displayName\":\"orphan.fastq\"") != std::string::npos &&
                    workspace_bound.body.find("\"orphaned\":true") != std::string::npos,
                "project workspace must reconcile committed bindings and orphan files");

        const std::string batch_preview_body =
            R"({"planId":"workspace-batch","templateId":"org.biocore.template.workspace-e2e","templateVersion":"1.0.0","sampleIds":["S1"],"inputAssignments":[{"nodeId":"stats","inputPort":"source","role":"primary"}]})";
        const auto batch_preview = api.handle({
            .method = biocore::presentation::HttpMethod::post,
            .target = "/api/v1/project-workspace/batches/preview",
            .authorization = {},
            .browser_session = browser_token,
            .origin = browser_origin,
            .content_type = "application/json",
            .body = batch_preview_body,
        });
        require(batch_preview.status == 200 &&
                    batch_preview.body.find("\"globallyValid\":true") != std::string::npos &&
                    batch_preview.body.find("\"sampleId\":\"S1\"") != std::string::npos &&
                    batch_preview.body.find("\"valid\":true") != std::string::npos,
                "project workspace batch preview");

        const auto batch_approve = api.handle({
            .method = biocore::presentation::HttpMethod::post,
            .target = "/api/v1/project-workspace/batches/workspace-batch/approve",
            .authorization = {},
            .browser_session = browser_token,
            .origin = browser_origin,
            .content_type = "application/json",
            .body = R"({"excludedSampleIds":[]})",
        });
        require(batch_approve.status == 201 &&
                    batch_approve.body.find("\"planId\":\"workspace-batch\"") != std::string::npos,
                "project workspace batch approval");

        const auto batch_submit = api.handle({
            .method = biocore::presentation::HttpMethod::post,
            .target = "/api/v1/project-workspace/batches/workspace-batch/submit",
            .authorization = {},
            .browser_session = browser_token,
            .origin = browser_origin,
            .content_type = "application/json",
            .body = R"({"maximumConcurrentJobs":1,"priority":"normal"})",
        });
        require(batch_submit.status == 202 &&
                    batch_submit.body.find("\"planId\":\"workspace-batch\"") != std::string::npos,
                "project workspace batch submission");

        bool batch_completed = false;
        std::string last_batch_state_body;
        for (int attempt = 0; attempt < 900; ++attempt) {
            const auto batch_state = api.handle({
                .method = biocore::presentation::HttpMethod::get,
                .target = "/api/v1/project-workspace/batches/workspace-batch",
                .authorization = {},
                .browser_session = browser_token,
                .origin = {},
                .body = {},
            });
            last_batch_state_body = batch_state.body;
            if (batch_state.status == 200 &&
                batch_state.body.find("\"state\":\"completed\"") != std::string::npos &&
                batch_state.body.find("\"completedCount\":1") != std::string::npos) {
                batch_completed = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        if (!batch_completed) {
            std::cerr << "Workspace batch final state: " << last_batch_state_body << '\n';
        }
        require(batch_completed, "project workspace batch must complete through real scheduler/runtime");

        const auto batch_results = api.handle({
            .method = biocore::presentation::HttpMethod::get,
            .target = "/api/v1/project-workspace/batches/workspace-batch/results",
            .authorization = {},
            .browser_session = browser_token,
            .origin = {},
            .body = {},
        });
        require(batch_results.status == 200 &&
                    batch_results.body.find("\"planId\":\"workspace-batch\"") != std::string::npos &&
                    batch_results.body.find("\"sampleId\":\"S1\"") != std::string::npos,
                "project workspace batch results");

        const auto batch_manifest = api.handle({
            .method = biocore::presentation::HttpMethod::get,
            .target = "/api/v1/batches/workspace-batch/export-manifest.json",
            .authorization = {},
            .browser_session = browser_token,
            .origin = {},
            .body = {},
        });
        require(batch_manifest.status == 200 &&
                    batch_manifest.body.find("\"id\":\"workspace-batch\"") != std::string::npos &&
                    batch_manifest.body.find("/project/") == std::string::npos,
                "project workspace export manifest bridge");
        const auto batch_report = api.handle({
            .method = biocore::presentation::HttpMethod::get,
            .target = "/api/v1/batches/workspace-batch/report.html",
            .authorization = {},
            .browser_session = browser_token,
            .origin = {},
            .body = {},
        });
        require(batch_report.status == 200 &&
                    batch_report.content_type == "text/html; charset=utf-8" &&
                    batch_report.body.find("workspace-batch") != std::string::npos &&
                    batch_report.body.find("/project/") == std::string::npos,
                "project workspace HTML report bridge");

        {
            biocore::infrastructure::sqlite::SqliteConnection drift_connection{
                project_root / ".biocore" / "project.sqlite"
            };
            drift_connection.execute("PRAGMA foreign_keys=OFF;");
            drift_connection.execute(
                "DELETE FROM managed_files WHERE id='" + file_id + "';"
            );
            drift_connection.execute("PRAGMA foreign_keys=ON;");
        }
        const auto workspace_broken = api.handle({
            .method = biocore::presentation::HttpMethod::get,
            .target = "/api/v1/project-workspace",
            .authorization = {},
            .browser_session = browser_token,
            .origin = {},
            .body = {},
        });
        require(workspace_broken.status == 200 &&
                    workspace_broken.body.find("\"sampleId\":\"S1\"") != std::string::npos &&
                    workspace_broken.body.find("\"bindingComplete\":false") != std::string::npos &&
                    workspace_broken.body.find("\"code\":\"missing_primary_file\"") != std::string::npos,
                "project workspace must explain a broken managed-file binding after external drift");

        const auto response = api.handle({
            .method = biocore::presentation::HttpMethod::get,
            .target = "/api/v1/jobs/stale-job",
            .authorization = authorization,
            .body = {},
        });
        require(response.status == 200, "recovered job must be available before server run");
        require(response.body.find("\"status\":\"interrupted\"") != std::string::npos,
                "startup recovery must run before server run");

        std::vector<std::string> locations;
        constexpr int submitted_jobs = 6;
        for (int index = 0; index < submitted_jobs; ++index) {
            const auto created = api.handle({
                .method = biocore::presentation::HttpMethod::post,
                .target = "/api/v1/jobs",
                .authorization = authorization,
                .body = R"({"pipelineId":"org.biocore.demo.validation","pipelineVersion":"0.1.0","priority":"high"})",
            });
            require(created.status == 201, "REST submission must publish a prepared queued job");
            require(created.body.find("\"status\":\"queued\"") != std::string::npos,
                    "REST submission response must be queued only after preparation");
            std::string location;
            for (const auto& [name, value] : created.headers) {
                if (name == "Location") location = value;
            }
            require(!location.empty(), "prepared job response must include Location");
            locations.push_back(std::move(location));
        }

        std::vector<bool> completed(locations.size(), false);
        for (int attempt = 0; attempt < 900; ++attempt) {
            bool all_completed = true;
            for (std::size_t index = 0; index < locations.size(); ++index) {
                if (completed[index]) continue;
                const auto current = api.handle({
                    .method = biocore::presentation::HttpMethod::get,
                    .target = locations[index],
                    .authorization = authorization,
                    .body = {},
                });
                if (current.status == 200 &&
                    current.body.find("\"status\":\"completed\"") != std::string::npos) {
                    completed[index] = true;
                }
                all_completed = all_completed && completed[index];
            }
            if (all_completed) break;
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        for (const bool job_completed : completed) {
            require(job_completed, "background WorkerRuntime must execute every REST-created prepared job");
        }
        bool completed_event_seen = false;
        for (int attempt = 0; attempt < 100 && !completed_event_seen; ++attempt) {
            {
                std::scoped_lock lock{lifecycle_mutex};
                for (const std::string& message : lifecycle_messages) {
                    if (message.find("\"type\":\"worker.lifecycle\"") != std::string::npos &&
                        message.find("\"eventType\":\"completed\"") != std::string::npos) {
                        completed_event_seen = true;
                        break;
                    }
                }
            }
            if (!completed_event_seen) {
                std::this_thread::sleep_for(std::chrono::milliseconds{5});
            }
        }
        lifecycle_events.unsubscribe(lifecycle_subscription);
        require(completed_event_seen,
                "composition root must broadcast completed WorkerLifecycleEvent");
    }
    bool called{false};
    std::filesystem::path project_root;
    biocore::presentation::LocalWebServerConfig seen_config{};
};

class UnavailableServer final : public biocore::presentation::ILocalWebServer {
public:
    bool available() const noexcept override { return false; }
    std::string_view backend_name() const noexcept override { return "unavailable-test"; }
    void run(
        biocore::presentation::LocalApiController&,
        biocore::presentation::WorkerLifecycleEventBroadcastHub&,
        const biocore::presentation::LocalWebServerConfig&
    ) override {
        run_called = true;
    }
    bool run_called{false};
};

}  // namespace

int main(const int argc, const char* const argv[]) {
    require(argc == 4, "expected worker, pipeline root, and plugin root arguments");

    {
        const auto base = std::filesystem::temp_directory_path() /
                          "biocore-project-init-bootstrap-test";
        std::error_code cleanup_error;
        std::filesystem::remove_all(base, cleanup_error);
        std::filesystem::create_directories(base);
        const auto project_root = base / "project-a";
        const auto catalog = base / "catalog" / "catalog.sqlite";
        std::ostringstream init_output;
        const auto project = biocore::bootstrap::initialize_project(
            {
                .project_root = project_root,
                .project_name = "Release Project",
                .catalog_database_path = catalog,
            },
            init_output
        );
        require(project.name() == "Release Project", "project init name");
        require(std::filesystem::is_regular_file(project_root / ".biocore" / "project.sqlite"),
                "project init database");
        require(std::filesystem::is_regular_file(project_root / ".biocore" / "ownership.json"),
                "project init ownership metadata");
        require(std::filesystem::is_directory(project_root / "inputs") &&
                    std::filesystem::is_directory(project_root / "outputs") &&
                    std::filesystem::is_directory(project_root / "reports") &&
                    std::filesystem::is_directory(project_root / "logs"),
                "project init workspace tree");
        require(std::filesystem::is_regular_file(catalog), "project init catalog");
        require(biocore::bootstrap::validated_project_root(project_root) ==
                    std::filesystem::canonical(project_root),
                "new project must be immediately serveable");
        require(init_output.str().find("Project ID: ") != std::string::npos,
                "project init output must include project ID");

        try {
            std::ostringstream duplicate_output;
            static_cast<void>(biocore::bootstrap::initialize_project(
                {
                    .project_root = project_root,
                    .project_name = "Duplicate",
                    .catalog_database_path = catalog,
                },
                duplicate_output
            ));
            require(false, "duplicate catalog project root must fail");
        } catch (const biocore::application::DuplicateProjectRootError&) {
        }

        const auto unsafe_root = base / "unsafe-project";
        try {
            std::ostringstream unsafe_output;
            static_cast<void>(biocore::bootstrap::initialize_project(
                {
                    .project_root = unsafe_root,
                    .project_name = "Unsafe",
                    .catalog_database_path = unsafe_root / "catalog.sqlite",
                },
                unsafe_output
            ));
            require(false, "catalog inside project workspace must fail");
        } catch (const std::invalid_argument&) {
            require(!std::filesystem::exists(unsafe_root),
                    "failed initialization must remove a newly created empty root");
        }

        std::filesystem::remove_all(base, cleanup_error);
    }
    const auto worker = std::filesystem::canonical(argv[1]);
    const auto pipeline_root = std::filesystem::canonical(argv[2]);
    const auto plugin_root = std::filesystem::canonical(argv[3]);
    {
        UnavailableServer unavailable;
        std::ostringstream unavailable_out;
        std::ostringstream unavailable_err;
        const int unavailable_result = biocore::bootstrap::run_local_server(
            {.project_root = "/definitely/missing/biocore-project", .port = 8421U},
            unavailable, unavailable_out, unavailable_err
        );
        require(unavailable_result == 3, "unavailable backend must fail closed with exit 3");
        require(!unavailable.run_called, "unavailable backend must not run");
        require(unavailable_out.str().empty(), "unavailable backend must not announce a token");
        require(unavailable_err.str().find("does not include Drogon") != std::string::npos,
                "unavailable backend must explain missing Drogon");
    }

    const auto root = std::filesystem::temp_directory_path() / "biocore-local-bootstrap-test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / ".biocore" / "runtime");
    std::filesystem::create_directories(root / "inputs");
    std::filesystem::create_directories(root / "outputs");
    const auto runtime_pipeline_root = root / "pipeline-fixture";
    std::filesystem::copy(
        pipeline_root, runtime_pipeline_root,
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing
    );
    {
        std::ofstream template_file{
            runtime_pipeline_root / "workspace-e2e.workflow-template.json", std::ios::binary
        };
        template_file << R"({
  "schemaVersion": 1,
  "id": "org.biocore.template.workspace-e2e",
  "version": "1.0.0",
  "name": "Workspace E2E FASTQ QC",
  "description": "Single-node test workflow used by the project workspace composition-root contract.",
  "workflowDocument": "{\"schemaVersion\":1,\"id\":\"org.biocore.template.workspace-e2e\",\"name\":\"Workspace E2E FASTQ QC\",\"description\":\"Run FASTQ QC for one project sample.\",\"nodes\":[{\"id\":\"stats\",\"label\":\"FASTQ QC\",\"moduleId\":\"org.biocore.fastqqc.stats\",\"pluginVersion\":\"0.1.0\",\"inputs\":[{\"name\":\"source\",\"artifactType\":\"fastq\",\"required\":true}],\"outputs\":[{\"name\":\"summary\",\"artifactType\":\"json\"},{\"name\":\"table\",\"artifactType\":\"tsv\"}],\"parameters\":{}}],\"edges\":[]}"
})";
    }
    const auto frontend_root = root / "frontend-fixture";
    std::filesystem::create_directories(frontend_root / "assets");
    {
        std::ofstream html{frontend_root / "index.html", std::ios::binary};
        html << "<!doctype html><title>OpenGenesis-BioCore</title>";
        std::ofstream css{frontend_root / "assets/app.css", std::ios::binary};
        css << "body{background:#07100d}";
        std::ofstream js{frontend_root / "assets/app.js", std::ios::binary};
        js << "fetch(\"/api/v1/health\")";
    }
    {
        biocore::infrastructure::sqlite::SqliteConnection connection{root / ".biocore" / "project.sqlite"};
        biocore::infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
        migrations.apply_pending();
        connection.execute(
            "INSERT INTO project_metadata(singleton,project_id,name,root_path,created_at_utc,updated_at_utc) "
            "VALUES(1,'workspace-project','Workspace Project','" +
            root.generic_string() +
            "','2026-09-28T07:00:00Z','2026-09-28T07:00:00Z');"
        );
        biocore::infrastructure::sqlite::SqliteJobRepository jobs{connection};
        const biocore::domain::Job stale{
            "stale-job", std::nullopt, "pipe", "1.0", biocore::domain::JobStatus::running,
            biocore::domain::JobPriority::normal, 0.25, "step-a",
            "2026-01-01T00:00:00Z", "2026-01-01T00:00:01Z",
            "2026-01-01T00:00:01Z", std::nullopt, 2
        };
        require(jobs.add(stale), "stale job fixture insert");
    }

    FakeServer server;
    server.project_root = root;
    std::ostringstream out;
    std::ostringstream err;
    const int result = biocore::bootstrap::run_local_server(
        {
            .project_root = root,
            .port = 9123U,
            .executable_path = {},
            .pipeline_root = runtime_pipeline_root,
            .plugin_root = plugin_root,
            .worker_executable = worker,
            .frontend_root = frontend_root,
            .maximum_concurrent_jobs = 1U,
        }, server, out, err
    );
    require(result == 0, "fake server bootstrap should succeed");
    require(server.called, "server run must be invoked");
    require(server.seen_config.bind_address == "127.0.0.1", "composition root must force loopback bind");
    require(server.seen_config.port == 9123U, "configured port must be forwarded");
    require(server.seen_config.worker_threads == 1U, "Core 0.1 HTTP adapter must remain single-threaded");
    require(server.seen_config.frontend_root == std::filesystem::canonical(frontend_root),
            "frontend root must be canonicalized and forwarded");
    require(out.str().find("1 stale job(s) interrupted") != std::string::npos, "recovery summary");
    require(out.str().find("Bootstrap bearer token: ") != std::string::npos, "bootstrap token announcement");
    require(out.str().find("OpenGenesis-BioCore UI: http://127.0.0.1:9123/") != std::string::npos,
            "frontend URL announcement");
    require(err.str().empty(), "successful bootstrap should not write stderr");

    std::filesystem::remove_all(root, error);
    std::cout << "Local server bootstrap tests passed\n";
    return 0;
}
