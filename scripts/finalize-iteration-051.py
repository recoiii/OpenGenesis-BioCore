#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (root / path).read_text(encoding="utf-8")


def write(path: str, content: str) -> None:
    target = root / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(content, encoding="utf-8", newline="\n")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one match, found {count}: {old[:100]!r}")
    write(path, text.replace(old, new, 1))


# Application report/export model.
replace_once(
    "src/application/include/biocore/application/artifact_presentation_service.hpp",
    "    std::int64_t revision{0};\n    std::optional<domain::JobFailure> failure;\n",
    "    std::int64_t revision{0};\n    std::uint64_t attempt_number{1U};\n    std::optional<domain::JobFailure> failure;\n",
)
replace_once(
    "src/application/include/biocore/application/artifact_presentation_service.hpp",
    "struct PipelineExecutionReport final {\n",
    "struct ArtifactExportEntry final {\n"
    "    ArtifactMetadata metadata;\n"
    "    std::string verified_sha256;\n"
    "};\n\n"
    "struct PipelineExecutionReport final {\n",
)
replace_once(
    "src/application/include/biocore/application/artifact_presentation_service.hpp",
    "};\n\nclass ArtifactPresentationService final {\n",
    "};\n\n"
    "struct PipelineExportManifest final {\n"
    "    static constexpr std::uint32_t current_schema_version = 1U;\n"
    "    std::uint32_t schema_version{current_schema_version};\n"
    "    std::string producer_version;\n"
    "    bool stable_snapshot{false};\n"
    "    PipelineExecutionReport report;\n"
    "    std::vector<ArtifactExportEntry> artifacts;\n"
    "};\n\n"
    "class ArtifactPresentationService final {\n",
)
replace_once(
    "src/application/include/biocore/application/artifact_presentation_service.hpp",
    "    [[nodiscard]] PipelineExecutionReport build_job_report(std::string_view job_id);\n",
    "    [[nodiscard]] PipelineExecutionReport build_job_report(std::string_view job_id);\n"
    "    [[nodiscard]] PipelineExportManifest build_job_export_manifest(std::string_view job_id);\n",
)

# Service: report attempt identity and fail-closed export verification.
replace_once(
    "src/application/source/artifact_presentation_service.cpp",
    '#include "biocore/application/artifact_presentation_service_error.hpp"\n',
    '#include "biocore/application/artifact_presentation_service_error.hpp"\n#include "biocore/application/build_info.hpp"\n',
)
replace_once(
    "src/application/source/artifact_presentation_service.cpp",
    "[[noreturn]] void throw_job_not_found() {\n",
    "[[nodiscard]] bool stable_export_status(const domain::JobStatus status) noexcept {\n"
    "    return status == domain::JobStatus::completed || status == domain::JobStatus::failed ||\n"
    "           status == domain::JobStatus::cancelled || status == domain::JobStatus::interrupted;\n"
    "}\n\n"
    "[[noreturn]] void throw_job_not_found() {\n",
)
replace_once(
    "src/application/source/artifact_presentation_service.cpp",
    "        .revision = job->revision(),\n        .failure = job->failure(),\n",
    "        .revision = job->revision(),\n        .attempt_number = job->attempt_number(),\n        .failure = job->failure(),\n",
)
replace_once(
    "src/application/source/artifact_presentation_service.cpp",
    "}\n\n}  // namespace biocore::application\n",
    "}\n\n"
    "PipelineExportManifest ArtifactPresentationService::build_job_export_manifest(\n"
    "    const std::string_view job_id\n"
    ") {\n"
    "    PipelineExecutionReport report = build_job_report(job_id);\n"
    "    std::vector<ArtifactExportEntry> entries;\n"
    "    entries.reserve(report.artifacts.size());\n"
    "    for (const auto& metadata : report.artifacts) {\n"
    "        auto descriptor = prepare_download(job_id, metadata.step_id, metadata.output_port);\n"
    "        if (descriptor.metadata.managed_file_id != metadata.managed_file_id ||\n"
    "            descriptor.metadata.relative_project_path != metadata.relative_project_path) {\n"
    "            throw std::runtime_error(\"Artifact identity changed while building export manifest\");\n"
    "        }\n"
    "        entries.push_back(ArtifactExportEntry{\n"
    "            .metadata = std::move(descriptor.metadata),\n"
    "            .verified_sha256 = std::move(descriptor.verified_sha256),\n"
    "        });\n"
    "    }\n"
    "    return PipelineExportManifest{\n"
    "        .schema_version = PipelineExportManifest::current_schema_version,\n"
    "        .producer_version = std::string{BuildInfo::version()},\n"
    "        .stable_snapshot = stable_export_status(report.status),\n"
    "        .report = std::move(report),\n"
    "        .artifacts = std::move(entries),\n"
    "    };\n"
    "}\n\n"
    "}  // namespace biocore::application\n",
)

# Presentation renderers: report schema v2 + portable export manifest.
replace_once(
    "src/presentation/include/biocore/presentation/artifact_report.hpp",
    "[[nodiscard]] std::string render_pipeline_execution_report_html(\n    const application::PipelineExecutionReport& report\n);\n",
    "[[nodiscard]] std::string render_pipeline_execution_report_html(\n    const application::PipelineExecutionReport& report\n);\n"
    "[[nodiscard]] std::string render_pipeline_export_manifest_json(\n"
    "    const application::PipelineExportManifest& manifest\n"
    ");\n",
)
replace_once(
    "src/presentation/source/artifact_report.cpp",
    'std::string{"\\\"schemaVersion\\\":1,\\\"jobId\\\":"}',
    'std::string{"\\\"schemaVersion\\\":2,\\\"jobId\\\":"}',
)
replace_once(
    "src/presentation/source/artifact_report.cpp",
    '           ",\\\"revision\\\":" + std::to_string(report.revision) +\n           ",\\\"failure\\\":" + failure_json(report.failure) +',
    '           ",\\\"revision\\\":" + std::to_string(report.revision) +\n'
    '           ",\\\"attemptNumber\\\":" + std::to_string(report.attempt_number) +\n'
    '           ",\\\"failure\\\":" + failure_json(report.failure) +',
)
replace_once(
    "src/presentation/source/artifact_report.cpp",
    '           optional_html(report.pipeline_id) + "</dd>" + failure_html(report.failure) +\n',
    '           optional_html(report.pipeline_id) + "</dd><dt>Attempt</dt><dd>" +\n'
    '           std::to_string(report.attempt_number) + "</dd>" + failure_html(report.failure) +\n',
)
replace_once(
    "src/presentation/source/artifact_report.cpp",
    "std::string render_pipeline_execution_report_html(\n",
    "std::string render_pipeline_export_manifest_json(\n"
    "    const application::PipelineExportManifest& manifest\n"
    ") {\n"
    "    std::string artifacts{\"[\"};\n"
    "    for (std::size_t index = 0U; index < manifest.artifacts.size(); ++index) {\n"
    "        if (index != 0U) artifacts += ',';\n"
    "        const auto& entry = manifest.artifacts[index];\n"
    "        artifacts += \"{\\\"metadata\\\":\" + render_artifact_metadata_json(entry.metadata) +\n"
    "                     \",\\\"verifiedSha256\\\":\" + quote_json(entry.verified_sha256) + \"}\";\n"
    "    }\n"
    "    artifacts += ']';\n"
    "    return \"{\" +\n"
    "           std::string{\"\\\"schemaVersion\\\":\"} + std::to_string(manifest.schema_version) +\n"
    "           \",\\\"producer\\\":{\\\"name\\\":\\\"OpenGenesis-BioCore\\\",\\\"version\\\":\" +\n"
    "           quote_json(manifest.producer_version) + \"}\" +\n"
    "           \",\\\"stableSnapshot\\\":\" + (manifest.stable_snapshot ? \"true\" : \"false\") +\n"
    "           \",\\\"artifactCount\\\":\" + std::to_string(manifest.artifacts.size()) +\n"
    "           \",\\\"report\\\":\" + render_pipeline_execution_report_json(manifest.report) +\n"
    "           \",\\\"artifacts\\\":\" + artifacts + \"}\";\n"
    "}\n\n"
    "std::string render_pipeline_execution_report_html(\n",
)

# API route for the manifest; it inherits existing artifact integrity error mapping.
replace_once(
    "src/presentation/source/local_api.cpp",
    "            if (path.size() == 5U && path[4] == \"report.json\" && request.method == HttpMethod::get) {\n"
    "                return json_response(200, render_pipeline_execution_report_json(artifacts_.build_job_report(job_id)));\n"
    "            }\n",
    "            if (path.size() == 5U && path[4] == \"report.json\" && request.method == HttpMethod::get) {\n"
    "                return json_response(200, render_pipeline_execution_report_json(artifacts_.build_job_report(job_id)));\n"
    "            }\n"
    "            if (path.size() == 5U && path[4] == \"export-manifest.json\" && request.method == HttpMethod::get) {\n"
    "                return json_response(200, render_pipeline_export_manifest_json(artifacts_.build_job_export_manifest(job_id)));\n"
    "            }\n",
)

# Existing test fixtures reflect report schema v2 and attempt identity.
replace_once(
    "tests/artifact_report_tests.cpp",
    "        .revision = 7,\n        .failure = std::nullopt,\n",
    "        .revision = 7,\n        .attempt_number = 2U,\n        .failure = std::nullopt,\n",
)
replace_once(
    "tests/artifact_report_tests.cpp",
    '    return json.starts_with("{\\\"schemaVersion\\\":1") &&\n',
    '    return json.starts_with("{\\\"schemaVersion\\\":2") &&\n'
    '           json.find("\\\"attemptNumber\\\":2") != std::string::npos &&\n',
)
replace_once(
    "tests/artifact_report_tests.cpp",
    '           html.find("b6a98d9ce9a2d914") != std::string::npos &&\n',
    '           html.find("b6a98d9ce9a2d914") != std::string::npos &&\n'
    '           html.find("<dt>Attempt</dt><dd>2</dd>") != std::string::npos &&\n',
)
replace_once(
    "tests/artifact_presentation_service_tests.cpp",
    '        report.priority != domain::JobPriority::high || report.artifacts.size() != 2U ||\n',
    '        report.priority != domain::JobPriority::high || report.attempt_number != 1U ||\n'
    '        report.artifacts.size() != 2U ||\n',
)

# API contract assertions.
replace_once(
    "tests/local_api_tests.cpp",
    '    require(report_json.status == 200 && report_json.body.find("\\\"schemaVersion\\\":1") != std::string::npos, "JSON report route");\n',
    '    require(report_json.status == 200 && report_json.body.find("\\\"schemaVersion\\\":2") != std::string::npos, "JSON report route");\n'
    '    require(report_json.body.find("\\\"attemptNumber\\\":1") != std::string::npos, "report attempt identity");\n',
)
replace_once(
    "tests/local_api_tests.cpp",
    '    require(report_json.body.find("/secret/project") == std::string::npos, "report must not expose absolute content path");\n\n'
    '    const auto download = api.handle',
    '    require(report_json.body.find("/secret/project") == std::string::npos, "report must not expose absolute content path");\n\n'
    '    const auto export_manifest = api.handle({.method = biocore::presentation::HttpMethod::get, .target = "/api/v1/jobs/job-a/export-manifest.json", .authorization = auth, .body = {}});\n'
    '    require(export_manifest.status == 200, "export manifest route");\n'
    '    require(export_manifest.body.find("\\\"stableSnapshot\\\":true") != std::string::npos, "completed export must be stable");\n'
    '    require(export_manifest.body.find("\\\"verifiedSha256\\\":\\\"aaaaaaaa") != std::string::npos, "export must contain verified digest");\n'
    '    require(export_manifest.body.find("/secret/project") == std::string::npos, "export manifest must not expose absolute content path");\n\n'
    '    const auto download = api.handle',
)

# Dedicated 051 contract test.
write("tests/export_report_foundation_tests.cpp", r'''#include <cstdlib>
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
    if (manifest.schema_version != 1U || manifest.producer_version != "0.2.0-dev" ||
        !manifest.stable_snapshot || manifest.report.attempt_number != 3U ||
        manifest.artifacts.size() != 2U || content.calls != 2 ||
        manifest.artifacts[0].metadata.step_id != "step-a" ||
        manifest.artifacts[0].verified_sha256 != digest) {
        return false;
    }

    const std::string json = presentation::render_pipeline_export_manifest_json(manifest);
    return json.find("\"schemaVersion\":1") != std::string::npos &&
           json.find("\"version\":\"0.2.0-dev\"") != std::string::npos &&
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
''')

replace_once(
    "CMakeLists.txt",
    "    add_test(\n        NAME integration.plugin_pipeline_contract_hardening\n        COMMAND biocore-plugin-pipeline-contract-hardening-tests\n    )\n",
    "    add_test(\n        NAME integration.plugin_pipeline_contract_hardening\n        COMMAND biocore-plugin-pipeline-contract-hardening-tests\n    )\n\n"
    "    add_executable(\n"
    "        biocore-export-report-foundation-tests\n"
    "        tests/export_report_foundation_tests.cpp\n"
    "    )\n"
    "    target_link_libraries(\n"
    "        biocore-export-report-foundation-tests\n"
    "        PRIVATE BioCore::application BioCore::presentation BioCore::project_warnings BioCore::sanitizers\n"
    "    )\n"
    "    add_test(\n"
    "        NAME integration.export_report_foundation\n"
    "        COMMAND biocore-export-report-foundation-tests\n"
    "    )\n",
)

write("docs/development/ITERATION-050-ACCEPTANCE.md", """# Iteration 050 — Acceptance Record\n\n- Status: ACCEPTED & FROZEN\n- Gemini verdict: ACCEPT\n- Gemini confidence: 100%\n- Accepted SHA: `e4dceeaf80b3f42bb26064cbf592129576f03fce`\n- Validation run: `33500658317`\n- Linux matrix: 72/72 PASS in GCC Debug, GCC Release, Clang Debug, and GCC ASan+UBSan (288/288 total)\n- Frozen ref: `accepted/iteration-050`\n\nBlocking findings: NONE. Non-blocking findings: NONE.\n""")

write("docs/development/ITERATION-051.md", """# OpenGenesis-BioCore v0.2.0-dev — Iteration 051\n\n## Export & Report Foundation\n\nIteration 051 establishes a portable, integrity-verified export contract without introducing an archive-format dependency or changing scientific output.\n\n### Changes\n\n- pipeline execution report JSON advances to schema v2 and records durable `attemptNumber`;\n- HTML reports expose the execution attempt;\n- a new export manifest schema v1 records producer version, stable-snapshot status, the complete report, artifact metadata, and a freshly verified SHA-256 for every generated artifact;\n- export manifest generation reuses the existing artifact integrity verifier and fails closed on missing, unsafe, size-mismatched, checksum-less, or checksum-mismatched content;\n- absolute local filesystem paths are intentionally excluded from JSON report/export surfaces;\n- `GET /api/v1/jobs/{id}/export-manifest.json` exposes the portable manifest through the existing authenticated local API;\n- artifact ordering remains deterministic by step, output port, and managed-file identity;\n- a dedicated integration test raises the active test floor from 72 to 73.\n\n### Compatibility boundaries\n\n- project database schema remains v8;\n- Worker Protocol remains v2;\n- Pipeline Definition remains schema v2;\n- Execution Plan remains schema v4;\n- no biological algorithm, threshold, output format, scheduler, retry, security, process-supervision, or managed-file storage semantics are changed;\n- no ZIP/TAR writer is introduced in this iteration. The manifest is the format-neutral foundation for later packaging/export UI work.\n\n### Acceptance criteria\n\n1. Report schema v2 contains attempt identity and remains safe for HTML/JSON rendering.\n2. Export manifest schema v1 contains producer identity, stable-snapshot status, report, deterministic artifact order, and verified SHA-256 digests.\n3. Export construction fails closed when any artifact fails integrity verification.\n4. Export/report JSON never exposes internal absolute content paths.\n5. The authenticated local API exposes `export-manifest.json`.\n6. All four Linux validation configurations pass at least 73 CTests with no sanitizer findings.\n""")

Path(__file__).unlink()
print("Iteration 051 transformation complete")
