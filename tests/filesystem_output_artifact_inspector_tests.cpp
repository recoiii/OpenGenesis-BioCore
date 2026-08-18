#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "biocore/infrastructure/filesystem_output_artifact_inspector.hpp"

namespace {
namespace fs = std::filesystem;

class Temp final {
public:
    Temp() {
        root = fs::temp_directory_path() /
               ("biocore-artifact-inspector-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root / "outputs");
    }
    ~Temp() { std::error_code error; fs::remove_all(root, error); }
    fs::path root;
};

template <typename Function>
[[nodiscard]] bool rejects(Function&& function) {
    try { static_cast<void>(function()); } catch (const std::invalid_argument&) { return true; }
    return false;
}

[[nodiscard]] bool contract() {
    Temp temp;
    const auto output = temp.root / "outputs" / "job--step--result.out";
    { std::ofstream stream{output, std::ios::binary}; stream << "alpha\n"; }
    biocore::infrastructure::FilesystemOutputArtifactInspector inspector{fs::canonical(temp.root)};
    const auto inspected = inspector.inspect_existing_output("outputs/job--step--result.out");
    if (inspected.size_bytes != 6 || inspected.display_name != "job--step--result.out" ||
        inspected.relative_project_path != "outputs/job--step--result.out" ||
        inspected.checksum_algorithm != "sha256" ||
        inspected.checksum_value != "b6a98d9ce9a2d9149288fa3df42d377c3e42737afdcdaf714e33c0a100b51060") return false;

    fs::create_directories(temp.root / "outputs" / "nested");
    { std::ofstream stream{temp.root / "outputs" / "nested" / "x"}; stream << "x"; }
    if (!rejects([&] { return inspector.inspect_existing_output("outputs/nested/x"); }) ||
        !rejects([&] { return inspector.inspect_existing_output("../escape"); }) ||
        !rejects([&] { return inspector.inspect_existing_output("outputs/missing.out"); })) return false;
#ifndef _WIN32
    const auto link = temp.root / "outputs" / "link.out";
    std::error_code error;
    fs::create_symlink(output, link, error);
    if (!error && !rejects([&] { return inspector.inspect_existing_output("outputs/link.out"); })) {
        return false;
    }
#endif
    return true;
}
}

int main() {
    if (!contract()) {
        std::cerr << "Filesystem output artifact inspector tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
