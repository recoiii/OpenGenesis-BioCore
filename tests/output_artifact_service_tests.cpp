#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_output_artifact_inspector.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/output_artifact_service.hpp"
#include "biocore/application/output_artifact_service_error.hpp"

namespace {

using namespace biocore;

class IDs final : public application::IIdGenerator {
public:
    explicit IDs(std::vector<std::string> values) : values_{std::move(values)} {}
    std::string generate() override {
        ++calls;
        if (values_.empty()) throw std::runtime_error("no id");
        auto value = values_.front();
        values_.erase(values_.begin());
        return value;
    }
    int calls{0};
private:
    std::vector<std::string> values_;
};

class Clock final : public application::IUtcClock {
public:
    std::string now_utc_iso8601() override { ++calls; return "2026-08-07T08:00:00Z"; }
    int calls{0};
};

class Inspector final : public application::IOutputArtifactInspector {
public:
    application::InspectedOutputArtifact inspect_existing_output(std::string_view relative) override {
        ++calls;
        const auto slash = relative.find_last_of('/');
        const std::string display = std::string{relative.substr(slash == std::string_view::npos ? 0U : slash + 1U)};
        return application::InspectedOutputArtifact{
            .display_name = display,
            .managed_path = "/project/" + std::string{relative},
            .relative_project_path = std::string{relative},
            .size_bytes = 17,
            .modified_at_utc = std::nullopt,
            .checksum_algorithm = checksum_algorithm,
            .checksum_value = checksum_value,
        };
    }
    int calls{0};
    std::string checksum_algorithm{"sha256"};
    std::string checksum_value = std::string(64U, 'a');
};

class Repo final : public application::IManagedFileRepository {
public:
    bool add(const domain::ManagedFile& file) override { files.push_back(file); return true; }
    std::optional<domain::ManagedFile> find_by_id(std::string_view id) override {
        for (const auto& file : files) if (file.id() == id) return file;
        return std::nullopt;
    }
    std::optional<domain::ManagedFile> find_by_relative_project_path(std::string_view path) override {
        for (const auto& file : files) {
            if (file.relative_project_path().has_value() && *file.relative_project_path() == path) return file;
        }
        return std::nullopt;
    }
    std::vector<domain::ManagedFile> list() override { return files; }
    bool add_generated_output(
        const domain::ManagedFile& file,
        const application::GeneratedOutputProvenance& provenance
    ) override {
        const application::GeneratedOutputArtifact artifact{file, provenance};
        return add_generated_outputs_batch(std::span{&artifact, 1U});
    }
    bool add_generated_outputs_batch(
        std::span<const application::GeneratedOutputArtifact> batch
    ) override {
        ++generated_add_calls;
        if (force_conflict || batch.empty()) return false;
        for (const auto& artifact : batch) {
            const auto& file = artifact.file;
            const auto& provenance = artifact.provenance;
            if (find_by_id(file.id()).has_value() ||
                find_by_relative_project_path(provenance.relative_project_path).has_value() ||
                find_generated_output(
                    provenance.job_id, provenance.step_id, provenance.output_port
                ).has_value()) {
                return false;
            }
        }
        for (const auto& artifact : batch) {
            files.push_back(artifact.file);
            artifacts.push_back(artifact);
        }
        return true;
    }
    std::optional<application::GeneratedOutputArtifact> find_generated_output(
        std::string_view job, std::string_view step, std::string_view port
    ) override {
        ++generated_find_calls;
        for (const auto& artifact : artifacts) {
            const auto& p = artifact.provenance;
            if (p.job_id == job && p.step_id == step && p.output_port == port) return artifact;
        }
        return std::nullopt;
    }
    std::vector<application::GeneratedOutputArtifact> list_generated_outputs(std::string_view job) override {
        std::vector<application::GeneratedOutputArtifact> result;
        for (const auto& artifact : artifacts) if (artifact.provenance.job_id == job) result.push_back(artifact);
        return result;
    }

    std::vector<domain::ManagedFile> files;
    std::vector<application::GeneratedOutputArtifact> artifacts;
    bool force_conflict{false};
    int generated_add_calls{0};
    int generated_find_calls{0};
};

[[nodiscard]] application::RegisterGeneratedOutputRequest request() {
    return {
        .job_id = "job-1",
        .step_id = "copy",
        .output_port = "result",
        .plugin_id = "org.biocore.demo",
        .plugin_version = "0.1.0",
        .module_id = "org.biocore.demo.copy",
        .file_type = "txt",
        .relative_project_path = "outputs/job-1--copy--result.out",
    };
}

[[nodiscard]] application::RegisterGeneratedOutputRequest second_request() {
    auto value = request();
    value.output_port = "metrics";
    value.file_type = "json";
    value.relative_project_path = "outputs/job-1--copy--metrics.out";
    return value;
}

[[nodiscard]] bool success_and_idempotency_contract() {
    Repo repo; Inspector inspector; IDs ids{{"artifact-1"}}; Clock clock;
    application::OutputArtifactService service{repo, inspector, ids, clock};
    const auto first = service.register_generated_output(request());
    const auto second = service.register_generated_output(request());
    return first.file.id() == "artifact-1" &&
           first.file.storage_mode() == domain::StorageMode::generated_output &&
           first.file.size_bytes() == 17 && first.file.checksum_algorithm() == std::optional<std::string>{"sha256"} &&
           first.file.checksum_value() == std::optional<std::string>{std::string(64U, 'a')} &&
           first.provenance.output_port == "result" &&
           second.file.id() == first.file.id() && repo.files.size() == 1U &&
           repo.artifacts.size() == 1U && repo.generated_add_calls == 1 &&
           inspector.calls == 1 && ids.calls == 1 && clock.calls == 1;
}

[[nodiscard]] bool provenance_conflict_contract() {
    Repo repo; Inspector inspector; IDs ids{{"artifact-1"}}; Clock clock;
    application::OutputArtifactService service{repo, inspector, ids, clock};
    static_cast<void>(service.register_generated_output(request()));
    auto conflicting = request(); conflicting.plugin_version = "9.9.9";
    try {
        static_cast<void>(service.register_generated_output(conflicting));
    } catch (const application::OutputArtifactServiceError& error) {
        return error.code() == application::OutputArtifactServiceErrorCode::provenance_conflict &&
               inspector.calls == 1 && ids.calls == 1;
    }
    return false;
}

[[nodiscard]] bool identifier_retry_contract() {
    Repo repo;
    repo.files.push_back(domain::ManagedFile{
        "collision", "existing", domain::StorageMode::generated_output, std::nullopt,
        std::string{"/project/outputs/existing.out"}, std::string{"outputs/existing.out"},
        "txt", 1, std::nullopt, std::nullopt, std::nullopt, "c", "u"
    });
    Inspector inspector; IDs ids{{"collision", "artifact-2"}}; Clock clock;
    application::OutputArtifactService service{repo, inspector, ids, clock};
    const auto artifact = service.register_generated_output(request());
    return artifact.file.id() == "artifact-2" && ids.calls == 2 && inspector.calls == 1;
}

[[nodiscard]] bool step_batch_contract() {
    Repo repo; Inspector inspector; IDs ids{{"artifact-1", "artifact-2"}}; Clock clock;
    application::OutputArtifactService service{repo, inspector, ids, clock};
    const std::vector requests{request(), second_request()};
    const auto first = service.register_generated_outputs_batch(requests);
    const auto second = service.register_generated_outputs_batch(requests);
    return first.size() == 2U && second.size() == 2U &&
           first[0].file.id() == "artifact-1" && first[1].file.id() == "artifact-2" &&
           second[0].file.id() == "artifact-1" && second[1].file.id() == "artifact-2" &&
           repo.generated_add_calls == 1 && repo.artifacts.size() == 2U &&
           inspector.calls == 2 && ids.calls == 2 && clock.calls == 1;
}


[[nodiscard]] bool invalid_checksum_is_rejected_before_persistence() {
    Repo repo; Inspector inspector; IDs ids{{"artifact-1"}}; Clock clock;
    inspector.checksum_value = "not-a-sha256";
    application::OutputArtifactService service{repo, inspector, ids, clock};
    try {
        static_cast<void>(service.register_generated_output(request()));
    } catch (const application::OutputArtifactServiceError& error) {
        return error.code() == application::OutputArtifactServiceErrorCode::provenance_conflict &&
               inspector.calls == 1 && ids.calls == 0 && repo.generated_add_calls == 0;
    }
    return false;
}

[[nodiscard]] bool partial_existing_batch_is_rejected() {
    Repo repo; Inspector inspector; IDs ids{{"artifact-1", "artifact-2"}}; Clock clock;
    application::OutputArtifactService service{repo, inspector, ids, clock};
    static_cast<void>(service.register_generated_output(request()));
    try {
        static_cast<void>(service.register_generated_outputs_batch({request(), second_request()}));
    } catch (const application::OutputArtifactServiceError& error) {
        return error.code() == application::OutputArtifactServiceErrorCode::persistence_conflict &&
               repo.artifacts.size() == 1U && inspector.calls == 1;
    }
    return false;
}

}  // namespace

int main() {
    if (!success_and_idempotency_contract() || !provenance_conflict_contract() ||
        !identifier_retry_contract() || !step_batch_contract() ||
        !invalid_checksum_is_rejected_before_persistence() ||
        !partial_existing_batch_is_rejected()) {
        std::cerr << "Output artifact service tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
