#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#ifndef BIOCORE_SOURCE_ROOT
#error "BIOCORE_SOURCE_ROOT is required"
#endif

namespace {
std::string load(const std::string& relative) {
    std::ifstream input{std::string{BIOCORE_SOURCE_ROOT} + "/" + relative, std::ios::binary};
    if (!input) return {};
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

bool has(const std::string& text, const std::string_view needle) {
    return text.find(needle) != std::string::npos;
}
}

int main() {
    const std::string html = load("frontend/index.html");
    const std::string js = load("frontend/assets/app.js");
    if (html.empty() || js.empty()) return EXIT_FAILURE;

    const bool html_contract =
        has(html, "OPENGENESIS · CORE 0.2 DEV") &&
        !has(html, "PROJECT GENESIS · CORE 0.1") &&
        has(html, "id=\"detail-attempt\"") &&
        has(html, "id=\"detail-revision\"") &&
        has(html, "id=\"export-status\"") &&
        has(html, "id=\"export-manifest-link\"") &&
        has(html, "id=\"verify-export\"") &&
        has(html, "id=\"retry-job\"");

    const bool js_contract =
        has(js, "canRetryJob(status)") &&
        has(js, "normalizeExportManifest(manifest)") &&
        has(js, "artifactVerificationState(artifact, manifest)") &&
        has(js, "attemptNumber:") &&
        has(js, "/export-manifest.json") &&
        has(js, "/retry") &&
        has(js, "state.exportManifests.delete(updated.id)");

    if (!html_contract || !js_contract) {
        std::cerr << "Frontend operational visibility contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
