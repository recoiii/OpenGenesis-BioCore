#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/artifact_presentation_service.hpp"
#include "biocore/application/artifact_presentation_service_error.hpp"
#include "biocore/application/i_artifact_content_access.hpp"
#include "biocore/application/i_job_repository.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/domain/job.hpp"
#include "biocore/domain/managed_file.hpp"

namespace {
using namespace biocore;

constexpr std::string_view checksum =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

[[nodiscard]] application::GeneratedOutputArtifact make_artifact(
    std::string port = "result",
    std::string step = "copy",
    std::string relative = "outputs/job-1--copy--result.out"
) {
    domain::ManagedFile file{
        "artifact-" + port,
        "artifact-" + port + ".out",
        domain::StorageMode::generated_output,
        std::nullopt,
        std::string{"/project/"} + relative,
        relative,
        "txt",
        17,
        std::nullopt,
        std::string{"sha256"},
        std::string{checksum},
        "2026-08-07T12:00:00Z",
        "2026-08-07T12:00:00Z",
    };
    application::GeneratedOutputProvenance provenance{
        .job_id = "job-1",
        .step_id = std::move(step),
        .output_port = std::move(port),
        .plugin_id = "org.biocore.demo",
        .plugin_version = "0.1.0",
        .module_id = "org.biocore.demo.copy",
        .file_type = "txt",
        .relative_project_path = std::move(relative),
        .step_progress = 0.75,
        .registered_at_utc = "2026-08-07T12:00:00Z",
    };
    return {std::move(file), std::move(provenance)};
}

class Files final : public application::IManagedFileRepository {
public:
    bool add(const domain::ManagedFile&) override { return false; }
    std::optional<domain::ManagedFile> find_by_id(std::string_view) override { return std::nullopt; }
    std::optional<domain::ManagedFile> find_by_relative_project_path(std::string_view) override {
        return std::nullopt;
    }
    std::vector<domain::ManagedFile> list() override { return {}; }
    bool add_generated_output(
        const domain::ManagedFile&,
        const application::GeneratedOutputProvenance&
    ) override { return false; }
    bool add_generated_outputs_batch(
        std::span<const application::GeneratedOutputArtifact>
    ) override { return false; }
    std::optional<application::GeneratedOutputArtifact> find_generated_output(
        std::string_view job,
        std::string_view step,
        std::string_view port
    ) override {
        for (const auto& artifact : artifacts) {
            if (artifact.provenance.job_id == job && artifact.provenance.step_id == step &&
                artifact.provenance.output_port == port) {
                return artifact;
            }
        }
        return std::nullopt;
    }
    std::vector<application::GeneratedOutputArtifact> list_generated_outputs(
        std::string_view job
    ) override {
        std::vector<application::GeneratedOutputArtifact> result;
        for (const auto& artifact : artifacts) {
            if (artifact.provenance.job_id == job) result.push_back(artifact);
        }
        return result;
    }
    std::vector<application::GeneratedOutputArtifact> artifacts;
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
    application::ArtifactContentVerification verify_for_download(
        const application::GeneratedOutputArtifact&
    ) override {
        ++calls;
        return verification;
    }
    application::ArtifactContentVerification verification{
        .status = application::ArtifactContentStatus::verified,
        .content_path = std::string{"/project/outputs/job-1--copy--result.out"},
        .computed_sha256 = std::string{checksum},
        .actual_size_bytes = 17,
    };
    int calls{0};
};

class Clock final : public application::IUtcClock {
public:
    std::string now_utc_iso8601() override { return "2026-08-07T12:30:00Z"; }
};

[[nodiscard]] domain::Job make_job() {
    return domain::Job{
        "job-1",
        std::string{"analysis-1"},
        std::string{"pipeline-1"},
        std::string{"1.0.0"},
        domain::JobStatus::completed,
        domain::JobPriority::high,
        1.0,
        std::nullopt,
        "2026-08-07T11:00:00Z",
        "2026-08-07T12:00:00Z",
        std::string{"2026-08-07T11:05:00Z"},
        std::string{"2026-08-07T12:00:00Z"},
        7,
    };
}

[[nodiscard]] bool query_and_report_contract() {
    Files files;
    files.artifacts.push_back(make_artifact(
        "metrics", "report", "outputs/job-1--report--metrics.out"
    ));
    files.artifacts.push_back(make_artifact());
    Jobs jobs;
    jobs.job = make_job();
    Content content;
    Clock clock;
    application::ArtifactPresentationService service{files, jobs, content, clock};

    const auto listed = service.list_for_job("job-1");
    const auto step = service.list_for_step("job-1", "copy");
    const auto found = service.find_artifact("job-1", "copy", "result");
    const auto report = service.build_job_report("job-1");
    if (listed.size() != 2U || listed[0].step_id != "copy" || listed[1].step_id != "report" ||
        step.size() != 1U || !found.has_value() ||
        found->checksum_algorithm != std::optional<std::string>{"sha256"} ||
        found->checksum_value != std::optional<std::string>{std::string{checksum}} ||
        report.job_id != "job-1" || report.status != domain::JobStatus::completed ||
        report.priority != domain::JobPriority::high || report.attempt_number != 1U ||
        report.artifacts.size() != 2U ||
        report.generated_at_utc != "2026-08-07T12:30:00Z") {
        return false;
    }

    const auto download = service.prepare_download("job-1", "copy", "result");
    return download.metadata.managed_file_id == "artifact-result" &&
           download.verified_sha256 == checksum && content.calls == 1;
}

[[nodiscard]] bool integrity_errors_are_typed() {
    Files files;
    files.artifacts.push_back(make_artifact());
    Jobs jobs;
    jobs.job = make_job();
    Content content;
    Clock clock;
    application::ArtifactPresentationService service{files, jobs, content, clock};

    content.verification = {
        .status = application::ArtifactContentStatus::checksum_mismatch,
        .content_path = std::nullopt,
        .computed_sha256 = std::string(64U, 'b'),
        .actual_size_bytes = 17,
    };
    try {
        static_cast<void>(service.prepare_download("job-1", "copy", "result"));
        return false;
    } catch (const application::ArtifactPresentationError& error) {
        if (error.code() != application::ArtifactPresentationErrorCode::checksum_mismatch) {
            return false;
        }
    }

    try {
        static_cast<void>(service.prepare_download("job-1", "copy", "missing"));
        return false;
    } catch (const application::ArtifactPresentationError& error) {
        if (error.code() != application::ArtifactPresentationErrorCode::artifact_not_found) {
            return false;
        }
    }

    try {
        static_cast<void>(service.list_for_job("missing"));
        return false;
    } catch (const application::ArtifactPresentationError& error) {
        if (error.code() != application::ArtifactPresentationErrorCode::job_not_found) return false;
    }

    try {
        static_cast<void>(service.build_job_report("missing"));
    } catch (const application::ArtifactPresentationError& error) {
        return error.code() == application::ArtifactPresentationErrorCode::job_not_found;
    }
    return false;
}

}  // namespace

int main() {
    if (!query_and_report_contract() || !integrity_errors_are_typed()) {
        std::cerr << "Artifact presentation service tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
