#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "biocore/application/artifact_presentation_service.hpp"
#include "biocore/presentation/artifact_report.hpp"

namespace {
using namespace biocore;

[[nodiscard]] application::ArtifactMetadata artifact() {
    return application::ArtifactMetadata{
        .managed_file_id = "artifact-1",
        .display_name = "result<unsafe>&\".txt",
        .job_id = "job-1",
        .step_id = "copy",
        .output_port = "result",
        .plugin_id = "org.biocore.demo",
        .plugin_version = "0.1.0",
        .module_id = "org.biocore.demo.copy",
        .file_type = "txt",
        .relative_project_path = "outputs/job-1--copy--result.out",
        .size_bytes = 6,
        .checksum_algorithm = std::string{"sha256"},
        .checksum_value = std::string{
            "b6a98d9ce9a2d9149288fa3df42d377c3e42737afdcdaf714e33c0a100b51060"},
        .step_progress = 1.0,
        .registered_at_utc = "2026-08-07T12:00:00Z",
    };
}

[[nodiscard]] application::PipelineExecutionReport report() {
    return application::PipelineExecutionReport{
        .job_id = "job-<script>alert(1)</script>",
        .analysis_id = std::string{"analysis-1"},
        .pipeline_id = std::string{"pipeline&one"},
        .pipeline_version = std::string{"1.0"},
        .status = domain::JobStatus::completed,
        .priority = domain::JobPriority::high,
        .progress = 1.0,
        .active_step_id = std::nullopt,
        .created_at_utc = "c",
        .updated_at_utc = "u",
        .started_at_utc = std::string{"s"},
        .finished_at_utc = std::string{"f"},
        .revision = 7,
        .failure = std::nullopt,
        .generated_at_utc = "g",
        .artifacts = {artifact()},
    };
}

[[nodiscard]] application::PipelineExecutionReport failure_report() {
    auto failed = report();
    failed.status = domain::JobStatus::failed;
    failed.progress = 0.4;
    failed.failure = domain::JobFailure{
        domain::JobFailureKind::worker_reported_failure,
        "malformed <FASTQ> & invalid read",
        17,
        std::string{"worker-time"},
        "recorded-time",
    };
    return failed;
}

[[nodiscard]] bool json_contract() {
    const auto json = presentation::render_pipeline_execution_report_json(report());
    const auto artifact_json = presentation::render_artifact_metadata_json(artifact());
    const auto failed_json = presentation::render_pipeline_execution_report_json(failure_report());
    return json.starts_with("{\"schemaVersion\":1") &&
           json.find("\"status\":\"completed\"") != std::string::npos &&
           json.find("\"failure\":null") != std::string::npos &&
           json.find("\"priority\":\"high\"") != std::string::npos &&
           json.find("b6a98d9ce9a2d914") != std::string::npos &&
           artifact_json.find("result<unsafe>&\\\".txt") != std::string::npos &&
           failed_json.find("\"kind\":\"worker_reported_failure\"") != std::string::npos &&
           failed_json.find("\"message\":\"malformed <FASTQ> & invalid read\"") !=
               std::string::npos &&
           failed_json.find("\"exitCode\":17") != std::string::npos;
}

[[nodiscard]] bool html_escaping_contract() {
    const auto html = presentation::render_pipeline_execution_report_html(report());
    const auto failed_html =
        presentation::render_pipeline_execution_report_html(failure_report());
    return html.find("<script>alert(1)</script>") == std::string::npos &&
           html.find("job-&lt;script&gt;alert(1)&lt;/script&gt;") != std::string::npos &&
           html.find("result&lt;unsafe&gt;&amp;&quot;.txt") != std::string::npos &&
           html.find("b6a98d9ce9a2d914") != std::string::npos &&
           failed_html.find("malformed &lt;FASTQ&gt; &amp; invalid read") != std::string::npos &&
           failed_html.find("malformed <FASTQ>") == std::string::npos;
}

}  // namespace

int main() {
    if (!json_contract() || !html_escaping_contract()) {
        std::cerr << "Artifact report presentation tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
