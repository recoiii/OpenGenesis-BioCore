#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "biocore/application/pipeline_planner.hpp"
#include "biocore/domain/pipeline_definition.hpp"
#include "biocore/infrastructure/json_execution_plan_store.hpp"
#include "biocore/infrastructure/json_pipeline_definition_loader.hpp"
#include "biocore/pipeline_protocol/pipeline_document_codec.hpp"
#include "plugin_test_support.hpp"

namespace {

namespace fs = std::filesystem;
using biocore::application::PipelinePlanner;
using biocore::infrastructure::JsonExecutionPlanStore;
using biocore::infrastructure::JsonPipelineDefinitionLoader;
using biocore::tests::FakePluginRegistry;

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        path_ = fs::temp_directory_path() /
                ("biocore-pipeline-io-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()
                ));
        fs::create_directories(path_ / ".biocore" / "runtime");
        path_ = fs::canonical(path_);
    }
    ~TemporaryDirectory() {
        std::error_code error;
        fs::remove_all(path_, error);
    }
    [[nodiscard]] const fs::path& path() const noexcept { return path_; }
private:
    fs::path path_;
};

void write_file(const fs::path& path, const std::string_view content) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output) throw std::runtime_error("Unable to write test file");
}

[[nodiscard]] std::string read_file(const fs::path& path) {
    std::ifstream input{path, std::ios::binary};
    return std::string{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}
    };
}

template <typename Function>
[[nodiscard]] bool rejects(Function&& function) {
    try {
        function();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

[[nodiscard]] bool load_and_snapshot_contract() {
    TemporaryDirectory project;
    const fs::path definition_path = project.path() / "demo.biocore-pipeline.json";
    write_file(
        definition_path,
        R"({"schemaVersion":2,"id":"org.biocore.demo","name":"Demo","version":"1.0.0","steps":[{"id":"validate","module":"org.biocore.demo.validate","pluginVersion":"0.1.0","dependsOn":[],"weight":2},{"id":"scan","module":"org.biocore.demo.scan","pluginVersion":"0.1.0","dependsOn":["validate"],"weight":6},{"id":"report","module":"org.biocore.demo.report","pluginVersion":"0.1.0","dependsOn":["scan"],"weight":2}]})"
    );

    const JsonPipelineDefinitionLoader loader;
    const auto definition = loader.load(definition_path);
    const FakePluginRegistry registry;
    const auto plan = PipelinePlanner::create_execution_plan(
        definition, "job-plan-1", 4, registry
    );
    JsonExecutionPlanStore store{project.path()};
    const std::string snapshot = store.store(plan);
    const fs::path snapshot_path = fs::path{std::u8string{
        reinterpret_cast<const char8_t*>(snapshot.data()),
        reinterpret_cast<const char8_t*>(snapshot.data() + snapshot.size())
    }};
    if (!fs::is_regular_file(snapshot_path) ||
        snapshot_path.parent_path().filename() != "job-plan-1") {
        return false;
    }
    const auto document = biocore::pipeline_protocol::parse_execution_plan_document(
        read_file(snapshot_path)
    );
    if (document.job_id != "job-plan-1" || document.job_revision != 4 ||
        document.steps.size() != 3U ||
        document.steps[1].depends_on != std::vector<std::string>{"validate"} ||
        document.steps[0].plugin_id != "org.biocore.demo" ||
        document.steps[0].plugin_version != "0.1.0" ||
        document.steps[0].plugin_manifest_version != 2U ||
        document.steps[0].plugin_api_version != "1.0" ||
        document.steps[0].module_type != "process" ||
        document.steps[0].plugin_root_path.empty() ||
        document.steps[0].executable_path.empty()) {
        return false;
    }
    return rejects([&] { static_cast<void>(store.store(plan)); });
}

[[nodiscard]] bool invalid_storage_and_definition_contract() {
    TemporaryDirectory project;
    const JsonPipelineDefinitionLoader loader;
    const fs::path invalid = project.path() / "invalid.json";
    write_file(
        invalid,
        R"({"schemaVersion":1,"id":"p","name":"P","version":"1","steps":[{"id":"a","module":"m","dependsOn":["missing"],"weight":1}]})"
    );
    if (!rejects([&] { static_cast<void>(loader.load(invalid)); })) return false;

    const biocore::domain::PipelineDefinition definition{
        2U,
        "org.biocore.p",
        "P",
        "1.0.0",
        {biocore::domain::PipelineStep{"a", "org.biocore.demo.validate", "0.1.0", {}, 1.0}},
    };
    const FakePluginRegistry registry;
    const auto unsafe_plan = PipelinePlanner::create_execution_plan(
        definition, "../unsafe", 0, registry
    );
    JsonExecutionPlanStore store{project.path()};
    return rejects([&] { static_cast<void>(store.store(unsafe_plan)); });
}


[[nodiscard]] bool symlink_boundary_contract() {
#if defined(_WIN32)
    return true;
#else
    TemporaryDirectory project;
    const fs::path outside = project.path() / "outside";
    fs::create_directory(outside);
    fs::create_directory_symlink(outside, project.path() / ".biocore" / "runtime" / "jobs");

    const biocore::domain::PipelineDefinition definition{
        2U,
        "org.biocore.p",
        "P",
        "1.0.0",
        {biocore::domain::PipelineStep{"a", "org.biocore.demo.validate", "0.1.0", {}, 1.0}},
    };
    const FakePluginRegistry registry;
    const auto plan = PipelinePlanner::create_execution_plan(
        definition, "job-safe", 0, registry
    );
    JsonExecutionPlanStore store{project.path()};
    const bool rejected = rejects([&] { static_cast<void>(store.store(plan)); });
    return rejected && fs::is_empty(outside);
#endif
}

}  // namespace

int main() {
    if (!load_and_snapshot_contract() || !invalid_storage_and_definition_contract() ||
        !symlink_boundary_contract()) {
        std::cerr << "JSON pipeline IO tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "JSON pipeline IO tests passed\n";
    return EXIT_SUCCESS;
}
