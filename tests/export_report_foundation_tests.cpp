#include <cstdlib>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/artifact_presentation_service.hpp"
#include "biocore/application/artifact_presentation_service_error.hpp"
#include "biocore/application/generated_output_artifact.hpp"
#include "biocore/application/i_artifact_content_access.hpp"
#include "biocore/application/i_job_repository.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/domain/job.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/presentation/artifact_report.hpp"

namespace {
using namespace biocore;

constexpr std::string_view digest =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

application::GeneratedOutputArtifact make_artifact(std::string port, std::string step) {
    const std::string relative = "outputs/job-export--" + step + "--" + port + ".out";
    return {
        .file = domain::ManagedFile{
            "file-" + port, "result-" + port + ".txt", domain::StorageMode::generated_output,
            std::nullopt, "/private/project/" + relative, relative, "txt", 7,
            std::nullopt, std::string{"sha256"}, std::string{digest},
            "2026-09-01T09:00:00Z", "2026-09-01T09:00:00Z"
        },
        .provenance = {
            .job_id = "job-export", .step_id = std::move(step), .output_port = std::move(port),
            .plugin_id = "org.biocore.demo", .plugin_version = "0.1.0",
            .module_id = "org.biocore.demo.copy", .file_type = "txt",
            .relative_project_path = relative, .step_progress = 1.0,
            .registered_at_utc = "2026-09-01T09:00:00Z",
        },
    };
}

class Files final : public application::IManagedFileRepository {
public:
    bool add(const domain::ManagedFile&) override { return false; }
    std::optional<domain::ManagedFile> find_by_id(std::string_view) override { return std::nullopt; }
    std::optional<domain::ManagedFile> find_by_relative_project_path(std::string_view) override { return std::nullopt; }
    std::vector<domain::ManagedFile> list() override { return {}; }
    bool add_generated_output(const domain::ManagedFile&, const application::GeneratedOutputProvenance&) override { return false; }
    bool add_generated_outputs_batch(std::span<const application::GeneratedOutputArtifact>) override { return false; }
    std::optional<application::GeneratedOutputArtifact> find_generated_output(
        std::string_view job, std::string_view step, std::string_view port
    ) override {
        for (const auto& value : values) {
            if (value.provenance.job_id == job && value.provenance.step_id == step &&
                value.provenance.output_port == port) return value;
        }
        return std::nullopt;
    }
    std::vector<application::GeneratedOutputArtifact> list_generated_outputs(std::string_view job) override {
        std::vector<application::GeneratedOutputArtifact> result;
        for (const auto& value : values) if (value.provenance.job_id == job) result.push_back(value);
        return result;
    }
    std::vector<application::GeneratedOutputArtifact> values;
};

class Jobs final : public application::IJobRepository {
public:
    bool add(const domain::Job&) override { return false; }
    std::optional<domain::Job> find_by_id(std::string_view id) override {
        if (job.has_value() && job->id() == id) return job;
        return std::nullopt;
    }
    std::vector<domain::Job> list() override { return job.has_value() ? std::vector{*job} : std::vector<domain::Job>{}; }
    bool update_runtime_state(const domain::Job&, std::int64_t) override { return false; }
    std::optional<domain::Job> job;
};

class Content final : public application::IArtifactContentAccess {
public:
    application::ArtifactContentVerification verify_for_download(const application::GeneratedOutputArtifact& artifact) override {
        ++calls;
        if (mismatch) {
            return {.status = application::ArtifactContentStatus::checksum_mismatch,
                    .content_path = std::nullopt,
                    .computed_sha256 = std::string(64U, 'b'),
                    .actual_size_bytes = artifact.file.size_bytes()};
        }
        return {.status = application::ArtifactContentStatus::verified,
                .content_path = std::string{"/private/project/"} + artifact.provenance.relative_project_path,
                .computed_sha256 = std::string{digest},
                .actual_size_bytes = artifact.file.size_bytes()};
    }
    bool mismatch{false};
    int calls{0};
};

class Clock final : public application::IUtcClock {
public:
    std::string now_utc_iso8601() override { return "2026-09-01T09:30:00Z"; }
};

[[nodiscard]] domain::Job job() {
    return domain::Job{
        "job-export", std::string{"analysis-export"}, std::string{"org.biocore.demo.pipeline"},
        std::string{"1.0.0"}, domain::JobStatus::completed, domain::JobPriority::normal,
        1.0, std::nullopt, "2026-09-01T08:00:00Z", "2026-09-01T09:00:00Z",
        std::string{"2026-09-01T08:01:00Z"}, std::string{"2026-09-01T09:00:00Z"}, 7,
        std::nullopt, 3U
    };
}

[[nodiscard]] bool manifest_contract() {
    Files files;
    files.values.push_back(make_artifact("zeta", "step-b"));
    files.values.push_back(make_artifact("alpha", "step-a"));
    Jobs jobs;
    jobs.job = job();
    Content content;
    Clock clock;
    application::ArtifactPresentationService service{files, jobs, content, clock};

    const auto manifest = service.build_job_export_manifest("job-export");
    if (manifest.schema_version != 1U || manifest.producer_version != "0.2.0" ||
        !manifest.stable_snapshot || manifest.report.attempt_number != 3U ||
        manifest.artifacts.size() != 2U || content.calls != 2 ||
        manifest.artifacts[0].metadata.step_id != "step-a" ||
        manifest.artifacts[0].verified_sha256 != digest) {
        return false;
    }

    const std::string json = presentation::render_pipeline_export_manifest_json(manifest);
    return json.find("\"schemaVersion\":1") != std::string::npos &&
           json.find("\"version\":\"0.2.0\"") != std::string::npos &&
           json.find("\"stableSnapshot\":true") != std::string::npos &&
           json.find("\"artifactCount\":2") != std::string::npos &&
           json.find("\"attemptNumber\":3") != std::string::npos &&
           json.find("\"verifiedSha256\":\"aaaaaaaa") != std::string::npos &&
           json.find("/private/project/") == std::string::npos;
}

[[nodiscard]] bool integrity_failure_is_fail_closed() {
    Files files;
    files.values.push_back(make_artifact("result", "step-a"));
    Jobs jobs;
    jobs.job = job();
    Content content;
    content.mismatch = true;
    Clock clock;
    application::ArtifactPresentationService service{files, jobs, content, clock};
    try {
        static_cast<void>(service.build_job_export_manifest("job-export"));
    } catch (const application::ArtifactPresentationError& error) {
        return error.code() == application::ArtifactPresentationErrorCode::checksum_mismatch;
    }
    return false;
}

}  // namespace

int main() {
    if (!manifest_contract() || !integrity_failure_is_fail_closed()) {
        std::cerr << "Export/report foundation tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
