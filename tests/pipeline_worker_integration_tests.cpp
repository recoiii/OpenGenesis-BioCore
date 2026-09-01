#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "biocore/application/pipeline_planner.hpp"
#include "biocore/application/worker_launch_request.hpp"
#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/pipeline_definition.hpp"
#include "biocore/infrastructure/filesystem_plugin_registry.hpp"
#include "biocore/infrastructure/json_execution_plan_store.hpp"
#include "biocore/infrastructure/platform_worker_supervisor.hpp"

namespace {

namespace fs = std::filesystem;
using biocore::application::PipelinePlanner;
using biocore::application::WorkerLaunchRequest;
using biocore::application::WorkerLifecycleEventType;
using biocore::domain::PipelineDefinition;
using biocore::domain::PipelineStep;
using biocore::infrastructure::FilesystemPluginRegistry;
using biocore::infrastructure::JsonExecutionPlanStore;
using biocore::infrastructure::PlatformWorkerSupervisor;
using biocore::infrastructure::WorkerSupervisorError;
using biocore::infrastructure::WorkerSupervisorErrorCode;

[[nodiscard]] fs::path path_from_utf8(const std::string_view value) {
    std::u8string utf8;
    utf8.reserve(value.size());
    for (const char character : value) {
        utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
    }
    return fs::path{utf8};
}

class TemporaryProject final {
public:
    TemporaryProject() {
        path_ = fs::temp_directory_path() /
                ("biocore-pipeline-worker-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()
                ));
        fs::create_directories(path_ / ".biocore" / "runtime");
        fs::create_directories(path_ / "outputs");
        path_ = fs::canonical(path_);
    }
    ~TemporaryProject() {
        std::error_code error;
        fs::remove_all(path_, error);
    }
    [[nodiscard]] const fs::path& path() const noexcept { return path_; }
private:
    fs::path path_;
};

class StaticRegistry final : public biocore::application::IPluginRegistry {
public:
    explicit StaticRegistry(biocore::application::ResolvedPluginModule module)
        : module_{std::move(module)} {}

    [[nodiscard]] std::optional<biocore::application::ResolvedPluginModule> find_module(
        const std::string_view module_id
    ) const override {
        if (module_.module_id == module_id) return module_;
        return std::nullopt;
    }

    [[nodiscard]] std::vector<biocore::application::RegisteredPlugin> list_plugins() const override {
        return {};
    }

private:
    biocore::application::ResolvedPluginModule module_;
};

[[nodiscard]] std::vector<biocore::application::WorkerProcessExit> await_exit(
    PlatformWorkerSupervisor& supervisor
) {
    for (int attempt = 0; attempt < 500; ++attempt) {
        auto exits = supervisor.reap_exited();
        if (!exits.empty()) return exits;
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return {};
}

[[nodiscard]] WorkerLaunchRequest request(
    std::string job_id,
    const std::int64_t revision,
    std::string snapshot
) {
    return WorkerLaunchRequest{
        .job_id = std::move(job_id),
        .analysis_id = std::nullopt,
        .pipeline_id = std::string{"org.biocore.demo"},
        .pipeline_version = std::string{"1.0.0"},
        .priority = biocore::domain::JobPriority::normal,
        .job_revision = revision,
        .execution_plan_path = std::move(snapshot),
    };
}

[[nodiscard]] PipelineDefinition successful_definition() {
    return PipelineDefinition{
        2U,
        "org.biocore.demo",
        "Demo",
        "1.0.0",
        {
            PipelineStep{"validate", "org.biocore.demo.validate", "0.1.0", {}, 2.0},
            PipelineStep{"scan", "org.biocore.demo.scan", "0.1.0", {"validate"}, 6.0},
            PipelineStep{"report", "org.biocore.demo.report", "0.1.0", {"scan"}, 2.0},
        },
    };
}

[[nodiscard]] bool successful_execution_contract(
    const fs::path& worker,
    const biocore::application::IPluginRegistry& registry
) {
    TemporaryProject project;
    JsonExecutionPlanStore store{project.path()};
    const auto plan = PipelinePlanner::create_execution_plan(
        successful_definition(), "job-success", 2, registry
    );
    const std::string snapshot = store.store(plan);
    PlatformWorkerSupervisor supervisor{worker, project.path()};
    supervisor.launch(request("job-success", 2, snapshot));
    const auto exits = await_exit(supervisor);
    if (exits.size() != 1U || exits.front().exit_code != 0 ||
        !exits.front().protocol_issues.empty() || exits.front().events.size() != 9U) {
        return false;
    }
    const auto& events = exits.front().events;
    if (events.front().type != WorkerLifecycleEventType::ready ||
        events.back().type != WorkerLifecycleEventType::completed) {
        return false;
    }
    std::vector<double> progress;
    for (const auto& event : events) {
        if (event.type == WorkerLifecycleEventType::progress && event.progress.has_value()) {
            progress.push_back(*event.progress);
        }
    }
    return progress == std::vector<double>{0.2, 0.8, 1.0};
}

[[nodiscard]] bool plugin_failure_contract(
    const fs::path& worker,
    const biocore::application::IPluginRegistry& registry
) {
    TemporaryProject project;
    const PipelineDefinition definition{
        2U,
        "org.biocore.failure",
        "Failure",
        "1.0.0",
        {PipelineStep{"bad", "org.biocore.demo.fail", "0.1.0", {}, 1.0}},
    };
    JsonExecutionPlanStore store{project.path()};
    const auto plan = PipelinePlanner::create_execution_plan(
        definition, "job-failed", 0, registry
    );
    const std::string snapshot = store.store(plan);
    PlatformWorkerSupervisor supervisor{worker, project.path()};
    supervisor.launch(request("job-failed", 0, snapshot));
    const auto exits = await_exit(supervisor);
    const bool artifact_emitted = exits.size() == 1U && std::ranges::any_of(
        exits.front().events,
        [](const biocore::application::WorkerLifecycleEvent& event) {
            return event.type == WorkerLifecycleEventType::artifact;
        }
    );
    return exits.size() == 1U && exits.front().exit_code == 3 &&
           exits.front().events.size() == 4U &&
           exits.front().events.front().type == WorkerLifecycleEventType::ready &&
           exits.front().events[0].sequence == 1U &&
           exits.front().events[1].sequence == 2U &&
           exits.front().events[2].sequence == 3U &&
           exits.front().events[3].sequence == 4U &&
           exits.front().events.back().type == WorkerLifecycleEventType::failed &&
           exits.front().events.back().exit_code == std::optional<std::int64_t>{3} &&
           !artifact_emitted;
}

[[nodiscard]] bool identity_mismatch_failure_contract(
    const fs::path& worker,
    const biocore::application::IPluginRegistry& registry
) {
    TemporaryProject project;
    JsonExecutionPlanStore store{project.path()};
    const auto plan = PipelinePlanner::create_execution_plan(
        successful_definition(), "job-in-plan", 5, registry
    );
    const std::string snapshot = store.store(plan);
    PlatformWorkerSupervisor supervisor{worker, project.path()};
    supervisor.launch(request("job-at-launch", 5, snapshot));
    const auto exits = await_exit(supervisor);
    return exits.size() == 1U && exits.front().exit_code == 3 &&
           exits.front().events.size() == 1U &&
           exits.front().events.front().type == WorkerLifecycleEventType::failed;
}

[[nodiscard]] std::string path_to_utf8(const fs::path& path) {
    const std::u8string value = path.u8string();
    return std::string{reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] bool long_running_plugin_heartbeat_contract(
    const fs::path& worker,
    const fs::path& slow_probe
) {
    TemporaryProject project;
    const fs::path plugin_root = project.path() / "slow-plugin";
#if defined(_WIN32)
    const fs::path relative_executable = fs::path{"bin"} / "windows-x64" / "slow-plugin.exe";
    constexpr std::string_view platform = "windows-x64";
#else
    const fs::path relative_executable = fs::path{"bin"} / "linux-x64" / "slow-plugin";
    constexpr std::string_view platform = "linux-x64";
#endif
    const fs::path executable = plugin_root / relative_executable;
    fs::create_directories(executable.parent_path());
    fs::copy_file(slow_probe, executable, fs::copy_options::overwrite_existing);
#if !defined(_WIN32)
    fs::permissions(
        executable,
        fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
            fs::perms::group_read | fs::perms::group_exec |
            fs::perms::others_read | fs::perms::others_exec,
        fs::perm_options::replace
    );
#endif
    const std::string manifest =
        "{\n"
        "  \"manifestVersion\": 2,\n"
        "  \"id\": \"org.biocore.test.slow\",\n"
        "  \"name\": \"BioCore Slow Plugin Probe\",\n"
        "  \"version\": \"0.1.0\",\n"
        "  \"apiVersion\": \"1.0\",\n"
        "  \"publisher\": \"BioCore Tests\",\n"
        "  \"modules\": [{\n"
        "    \"id\": \"org.biocore.test.slow.run\",\n"
        "    \"type\": \"process\",\n"
        "    \"entrypoints\": {\"" + std::string{platform} + "\": \"" +
            relative_executable.generic_string() + "\"},\n"
        "    \"parameters\": [], \"inputs\": [], \"outputs\": []\n"
        "  }]\n"
        "}\n";
    {
        std::ofstream output{plugin_root / "plugin.json", std::ios::binary | std::ios::trunc};
        if (!output) return false;
        output << manifest;
        output.close();
        if (!output) return false;
    }

    const StaticRegistry registry{biocore::application::ResolvedPluginModule{
        .plugin_id = "org.biocore.test.slow",
        .plugin_version = "0.1.0",
        .module_id = "org.biocore.test.slow.run",
        .module_type = biocore::domain::PluginModuleType::process,
        .plugin_root_path = path_to_utf8(fs::canonical(plugin_root)),
        .executable_path = path_to_utf8(fs::canonical(executable)),
        .parameters = {},
        .inputs = {},
        .outputs = {},
    }};
    const PipelineDefinition definition{
        2U,
        "org.biocore.test.slow.pipeline",
        "Slow plugin heartbeat probe",
        "0.1.0",
        {PipelineStep{"slow", "org.biocore.test.slow.run", "0.1.0", {}, 1.0}},
    };
    JsonExecutionPlanStore store{project.path()};
    const auto plan = PipelinePlanner::create_execution_plan(
        definition, "job-long-heartbeat", 1, registry
    );
    const std::string snapshot = store.store(plan);
    PlatformWorkerSupervisor supervisor{worker, project.path()};
    supervisor.launch(request("job-long-heartbeat", 1, snapshot));
    const auto exits = await_exit(supervisor);
    if (exits.size() != 1U || exits.front().exit_code != 0 ||
        !exits.front().protocol_issues.empty()) {
        return false;
    }
    const auto& events = exits.front().events;
    if (events.size() < 7U || events.front().type != WorkerLifecycleEventType::ready ||
        events.back().type != WorkerLifecycleEventType::completed) {
        return false;
    }
    std::size_t heartbeat_count = 0U;
    for (std::size_t index = 0U; index < events.size(); ++index) {
        if (events[index].sequence != index + 1U) return false;
        if (events[index].type == WorkerLifecycleEventType::heartbeat) ++heartbeat_count;
    }
    return heartbeat_count >= 3U &&
           std::ranges::any_of(events, [](const auto& event) {
               return event.type == WorkerLifecycleEventType::progress;
           });
}

[[nodiscard]] bool project_containment_contract(
    const fs::path& worker,
    const biocore::application::IPluginRegistry& registry
) {
    TemporaryProject project;
    TemporaryProject outside_project;
    JsonExecutionPlanStore outside_store{outside_project.path()};
    const auto plan = PipelinePlanner::create_execution_plan(
        successful_definition(), "job-outside", 0, registry
    );
    const std::string outside_snapshot = outside_store.store(plan);
    PlatformWorkerSupervisor supervisor{worker, project.path()};
    try {
        supervisor.launch(request("job-outside", 0, outside_snapshot));
    } catch (const WorkerSupervisorError& error) {
        return error.code() == WorkerSupervisorErrorCode::invalid_launch_request &&
               supervisor.tracked_processes().empty();
    }
    return false;
}

}  // namespace

int main(const int argc, const char* const argv[]) {
    if (argc != 4) {
        std::cerr << "Expected biocore-worker, plugin-root, and slow-plugin probe paths\n";
        return EXIT_FAILURE;
    }
    const fs::path worker = fs::canonical(path_from_utf8(argv[1]));
    const fs::path plugin_root = fs::canonical(path_from_utf8(argv[2]));
    const fs::path slow_probe = fs::canonical(path_from_utf8(argv[3]));
    FilesystemPluginRegistry registry{{plugin_root}};
    const auto report = registry.refresh();
    const auto known_module = registry.find_module("org.biocore.demo.validate");
    const auto fasta_module = registry.find_module("org.biocore.fastaqc.stats");
    const auto fastq_module = registry.find_module("org.biocore.fastqqc.stats");
    const bool discovery_ok = report.loaded_plugins == 8U &&
                              report.loaded_modules == 17U &&
                              report.rejected.empty() && known_module.has_value() &&
                              fasta_module.has_value() &&
                              fastq_module.has_value();
    const bool success_ok = discovery_ok && successful_execution_contract(worker, registry);
    const bool failure_ok = discovery_ok &&
                            plugin_failure_contract(worker, registry);
    const bool identity_ok = discovery_ok &&
                             identity_mismatch_failure_contract(worker, registry);
    const bool containment_ok = discovery_ok &&
                                project_containment_contract(worker, registry);
    const bool heartbeat_ok = long_running_plugin_heartbeat_contract(worker, slow_probe);
    if (!discovery_ok || !success_ok || !failure_ok || !identity_ok || !containment_ok ||
        !heartbeat_ok) {
        std::cerr << "Pipeline worker integration tests failed"
                  << " discovery=" << discovery_ok
                  << " success=" << success_ok
                  << " failure=" << failure_ok
                  << " identity=" << identity_ok
                  << " containment=" << containment_ok
                  << " heartbeat=" << heartbeat_ok << '\n';
        for (const auto& issue : report.rejected) {
            std::cerr << issue.candidate_path << ": " << issue.message << '\n';
        }
        return EXIT_FAILURE;
    }
    std::cout << "Pipeline worker integration tests passed\n";
    return EXIT_SUCCESS;
}
