#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "biocore/domain/workflow_checkpoint.hpp"
#include "biocore/infrastructure/filesystem_workflow_checkpoint_artifact_verifier.hpp"

namespace {

using namespace biocore;

constexpr std::string_view abc_sha =
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

class TempRoot final {
public:
    explicit TempRoot(const std::string_view suffix) {
        root_ = std::filesystem::temp_directory_path() /
                ("biocore-073-checkpoint-" + std::string{suffix});
        std::error_code error;
        std::filesystem::remove_all(root_, error);
        std::filesystem::create_directories(root_ / "outputs");
        std::ofstream stream{root_ / "outputs" / "result.txt", std::ios::binary};
        stream << "abc";
        stream.close();
    }

    ~TempRoot() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return root_;
    }

private:
    std::filesystem::path root_;
};

[[nodiscard]] domain::WorkflowCheckpointArtifact base_artifact() {
    return {
        "result",
        "outputs/result.txt",
        3,
        std::string{abc_sha},
    };
}

[[nodiscard]] bool valid_contract() {
    TempRoot root{"valid"};
    infrastructure::FilesystemWorkflowCheckpointArtifactVerifier verifier{root.path()};
    return verifier.verify(base_artifact());
}

[[nodiscard]] bool hash_contract() {
    TempRoot root{"hash"};
    infrastructure::FilesystemWorkflowCheckpointArtifactVerifier verifier{root.path()};
    auto artifact = base_artifact();
    artifact.sha256 = std::string(64U, 'a');
    return !verifier.verify(artifact);
}

[[nodiscard]] bool size_contract() {
    TempRoot root{"size"};
    infrastructure::FilesystemWorkflowCheckpointArtifactVerifier verifier{root.path()};
    auto artifact = base_artifact();
    artifact.size_bytes = 4;
    return !verifier.verify(artifact);
}

[[nodiscard]] bool traversal_contract() {
    TempRoot root{"traversal"};
    infrastructure::FilesystemWorkflowCheckpointArtifactVerifier verifier{root.path()};
    auto artifact = base_artifact();
    artifact.relative_project_path = "../result.txt";
    return !verifier.verify(artifact);
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    const std::string_view name{argv[1]};
    const bool passed =
        (name == "valid" && valid_contract()) ||
        (name == "hash" && hash_contract()) ||
        (name == "size" && size_contract()) ||
        (name == "traversal" && traversal_contract());
    if (!passed) {
        std::cerr << "Checkpoint artifact verifier test failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Checkpoint artifact verifier test passed\n";
    return EXIT_SUCCESS;
}
