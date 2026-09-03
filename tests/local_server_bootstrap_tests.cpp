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

void trace(const std::string_view message) {
    std::cerr << "[local-server-probe] " << message << '\n' << std::flush;
}

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
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
        trace("fake server: entered");
        called = true;
        seen_config = config;
        biocore::presentation::FrontendAssetStore frontend{config.frontend_root};
        const auto frontend_index = frontend.find("/");
        require(frontend_index.has_value() &&
                    frontend_index->body.find("OpenGenesis-BioCore") != std::string::npos,
                "frontend assets must be available before server run");
        trace("fake server: frontend verified");
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
        trace("fake server: lifecycle activated");
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
        trace("fake server: browser session verified");
        const auto response = api.handle({
            .method = biocore::presentation::HttpMethod::get,
            .target = "/api/v1/jobs/stale-job",
            .authorization = authorization,
            .body = {},
        });
        require(response.status == 200, "recovered job must be available before server run");
        require(response.body.find("\"status\":\"interrupted\"") != std::string::npos,
                "startup recovery must run before server run");
        trace("fake server: stale recovery verified");

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
        trace("fake server: six jobs submitted");

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
        trace("fake server: all jobs completed");
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
        trace("fake server: before lifecycle unsubscribe");
        lifecycle_events.unsubscribe(lifecycle_subscription);
        trace("fake server: after lifecycle unsubscribe");
        require(completed_event_seen,
                "composition root must broadcast completed WorkerLifecycleEvent");
        trace("fake server: returning");
    }
    bool called{false};
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
    std::ostringstream out;
    std::ostringstream err;
    trace("main: before run_local_server");
    const int result = biocore::bootstrap::run_local_server(
        {
            .project_root = root,
            .port = 9123U,
            .executable_path = {},
            .pipeline_root = pipeline_root,
            .plugin_root = plugin_root,
            .worker_executable = worker,
            .frontend_root = frontend_root,
            .maximum_concurrent_jobs = 1U,
        }, server, out, err
    );
    trace("main: after run_local_server");
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
