#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#include "biocore/application/generated_output_artifact.hpp"
#include "biocore/infrastructure/filesystem_result_artifact_reader.hpp"
#include "biocore/infrastructure/sha256.hpp"

namespace {
using namespace biocore;
namespace fs = std::filesystem;

struct Fixture final {
    fs::path root;
    fs::path output;
    std::optional<application::GeneratedOutputArtifact> artifact;

    explicit Fixture(const std::string& mode)
        : root{fs::temp_directory_path() / ("biocore-result-reader-" + mode)} {
        std::error_code error;
        fs::remove_all(root, error);
        fs::create_directories(root / "outputs");
        root = fs::canonical(root);
        output = fs::canonical(root / "outputs") / "artifact.tsv";
        const std::string text = "metric\tvalue\nreads\t10\n";
        std::ofstream stream{output, std::ios::binary};
        stream << text;
        stream.close();
        const auto checksum = infrastructure::sha256_file_hex(output);
        artifact.emplace(application::GeneratedOutputArtifact{
            domain::ManagedFile{
                "artifact-1", "summary.tsv", domain::StorageMode::generated_output,
                std::nullopt, output.generic_string(), std::string{"outputs/artifact.tsv"},
                "tsv", static_cast<std::int64_t>(text.size()), std::nullopt,
                std::string{"sha256"}, checksum, "created", "updated"
            },
            application::GeneratedOutputProvenance{
                .job_id="job-1", .step_id="qc", .output_port="table",
                .plugin_id="plugin", .plugin_version="0.1.0",
                .module_id="org.biocore.fastqqc.stats", .file_type="tsv",
                .relative_project_path="outputs/artifact.tsv", .step_progress=1.0,
                .registered_at_utc="registered"
            }
        });
    }

    ~Fixture() {
        std::error_code error;
        fs::remove_all(root, error);
    }
};

bool verified(const std::string& mode) {
    Fixture fixture{mode};
    infrastructure::FilesystemResultArtifactReader reader{fixture.root};
    const auto result = reader.read_verified_text(*fixture.artifact, 1024U);
    return result.status == application::ResultArtifactReadStatus::verified &&
           result.text == std::optional<std::string>{"metric\tvalue\nreads\t10\n"} &&
           result.verified_sha256 == fixture.artifact->file.checksum_value();
}

bool limit(const std::string& mode) {
    Fixture fixture{mode};
    infrastructure::FilesystemResultArtifactReader reader{fixture.root};
    const auto result = reader.read_verified_text(*fixture.artifact, 4U);
    return result.status == application::ResultArtifactReadStatus::too_large &&
           !result.text.has_value();
}

bool tamper(const std::string& mode) {
    Fixture fixture{mode};
    {
        std::ofstream stream{fixture.output, std::ios::binary | std::ios::trunc};
        stream << "metric\tvalue\nreads\t11\n";
    }
    infrastructure::FilesystemResultArtifactReader reader{fixture.root};
    const auto result = reader.read_verified_text(*fixture.artifact, 1024U);
    return result.status == application::ResultArtifactReadStatus::checksum_mismatch &&
           !result.text.has_value();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    const std::string mode = argv[1];
    bool ok = false;
    if (mode == "verified") ok = verified(mode);
    else if (mode == "limit") ok = limit(mode);
    else if (mode == "tamper") ok = tamper(mode);
    else return EXIT_FAILURE;
    if (!ok) {
        std::cerr << "Filesystem result artifact reader test failed: " << mode << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
