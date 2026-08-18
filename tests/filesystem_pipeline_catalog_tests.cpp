#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

#include "biocore/infrastructure/filesystem_pipeline_catalog.hpp"

namespace {
void require(bool value, std::string_view message) { if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); } }
const char* document = R"({"schemaVersion":1,"id":"org.biocore.demo.validation","name":"Demo","version":"0.1.0","steps":[{"id":"validate","module":"org.biocore.demo.validate","dependsOn":[],"weight":1.0}]})";
}

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("biocore-pipeline-catalog-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    { std::ofstream out{root / "demo.json"}; out << document; }
    biocore::infrastructure::FilesystemPipelineCatalog catalog{fs::canonical(root)};
    auto report = catalog.refresh();
    require(report.loaded_pipelines == 1U && report.rejected.empty(), "single valid pipeline must load");
    require(catalog.find("org.biocore.demo.validation", "0.1.0").has_value(), "exact id/version lookup");
    require(!catalog.find("org.biocore.demo.validation", "9.9.9").has_value(), "wrong version must not resolve");

    { std::ofstream out{root / "duplicate.json"}; out << document; }
    report = catalog.refresh();
    require(report.loaded_pipelines == 0U && report.rejected.size() == 2U, "duplicate id/version candidates must both be rejected");
    require(!catalog.find("org.biocore.demo.validation", "0.1.0").has_value(), "conflicted pipeline must not remain registered");
    std::error_code error; fs::remove_all(root, error);
    std::cout << "Filesystem pipeline catalog tests passed\n";
    return 0;
}
