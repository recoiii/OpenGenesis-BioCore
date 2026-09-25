#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "biocore/infrastructure/filesystem_workflow_template_catalog.hpp"
#include "biocore/presentation/frontend_asset_store.hpp"

namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string read_text(const fs::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("Unable to open frontend source asset");
    return {
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}
    };
}

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

[[nodiscard]] fs::path frontend_root() {
    return fs::path{BIOCORE_SOURCE_ROOT} / "frontend";
}

void surface_contract() {
    const std::string html = read_text(frontend_root() / "index.html");
    const std::string js = read_text(frontend_root() / "assets" / "app.js");
    const std::string css = read_text(frontend_root() / "assets" / "app.css");

    require(
        html.find("data-view=\"builder\"") != std::string::npos,
        "builder navigation must be present"
    );
    require(
        html.find("id=\"workflow-builder-panel\"") != std::string::npos &&
        html.find("id=\"builder-node-list\"") != std::string::npos &&
        html.find("id=\"builder-edge-list\"") != std::string::npos,
        "builder graph surface must be present"
    );
    require(
        html.find("id=\"builder-validate\"") != std::string::npos &&
        html.find("id=\"builder-export\"") != std::string::npos,
        "builder validate/export controls must be present"
    );
    require(
        js.find("/api/v1/workflow-templates") != std::string::npos &&
        js.find("/api/v1/workflows/validate") != std::string::npos,
        "builder must consume exact template and validation APIs"
    );
    require(
        js.find("parseBuilderInputs") != std::string::npos &&
        js.find("parseBuilderOutputs") != std::string::npos &&
        js.find("parseBuilderParameters") != std::string::npos,
        "builder node contract editors must be wired"
    );
    require(
        css.find(".builder-layout") != std::string::npos &&
        css.find(".builder-node-card") != std::string::npos &&
        css.find(".builder-edge-row") != std::string::npos,
        "builder styling contract must be present"
    );
}

void safe_rendering_contract() {
    const std::string js = read_text(frontend_root() / "assets" / "app.js");
    const auto begin = js.find("const setBuilderMessage");
    const auto end = js.find("const probeSession", begin);
    require(
        begin != std::string::npos && end != std::string::npos && end > begin,
        "builder implementation region must be identifiable"
    );
    const std::string builder = js.substr(begin, end - begin);

    require(
        builder.find("title.textContent = node.label") != std::string::npos &&
        builder.find("label.textContent =") != std::string::npos,
        "user-authored graph labels must use textContent"
    );
    require(
        builder.find(".innerHTML") == std::string::npos &&
        builder.find("insertAdjacentHTML") == std::string::npos,
        "builder must not inject authored values as HTML"
    );
    require(
        builder.find("JSON.stringify(builderState.workflow)") != std::string::npos,
        "validation must serialize the explicit draft object"
    );
    require(
        builder.find("validatedCanonical") != std::string::npos &&
        builder.find("new Blob") != std::string::npos,
        "export must use server-validated canonical workflow state"
    );
}

void scope_contract() {
    const std::string js = read_text(frontend_root() / "assets" / "app.js");
    const auto begin = js.find("const setBuilderMessage");
    const auto end = js.find("const probeSession", begin);
    require(
        begin != std::string::npos && end != std::string::npos,
        "builder scope region"
    );
    const std::string builder = js.substr(begin, end - begin);

    require(
        builder.find("/api/v1/jobs") == std::string::npos &&
        builder.find("WebSocket") == std::string::npos &&
        builder.find("worker.lifecycle") == std::string::npos,
        "Iteration 076 builder must not execute or observe workflows"
    );
    require(
        builder.find("/api/v1/workflow-templates") != std::string::npos &&
        builder.find("/api/v1/workflows/validate") != std::string::npos,
        "Iteration 076 builder scope must remain authoring and validation"
    );
}

void asset_store_contract() {
    biocore::presentation::FrontendAssetStore store{frontend_root()};
    const auto index = store.find("/");
    const auto javascript = store.find("/assets/app.js");
    const auto css = store.find("/assets/app.css");
    require(index.has_value() && javascript.has_value() && css.has_value(),
            "actual builder frontend assets must load through FrontendAssetStore");
    require(
        index->body.find("workflow-builder-panel") != std::string::npos &&
        javascript->body.find("validateBuilderWorkflow") != std::string::npos &&
        css->body.find(".builder-canvas") != std::string::npos,
        "served frontend assets must include builder implementation"
    );
}

void bundled_template_contract() {
    const fs::path pipeline_root = fs::canonical(
        fs::path{BIOCORE_SOURCE_ROOT} / "pipelines"
    );
    biocore::infrastructure::FilesystemWorkflowTemplateCatalog catalog{
        pipeline_root
    };
    const auto value = catalog.find(
        "org.biocore.template.fastq-trim-qc", "1.0.0"
    );
    require(value.has_value(), "bundled exact workflow template must be discoverable");
    require(
        value->blueprint().nodes().size() == 2U &&
        value->blueprint().edges().size() == 1U,
        "bundled workflow template graph contract"
    );
    require(
        value->blueprint().nodes()[0].module_id() ==
            "org.biocore.fastqqc.trim-single" &&
        value->blueprint().nodes()[0].plugin_version() == "0.1.0",
        "bundled workflow template must pin exact plugin module version"
    );
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    const std::string_view mode{argv[1]};
    if (mode == "surface") surface_contract();
    else if (mode == "safe-rendering") safe_rendering_contract();
    else if (mode == "scope") scope_contract();
    else if (mode == "asset-store") asset_store_contract();
    else if (mode == "bundled-template") bundled_template_contract();
    else return EXIT_FAILURE;

    std::cout << "Workflow builder frontend contract passed: " << mode << '\n';
    return EXIT_SUCCESS;
}
