#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "biocore/presentation/batch_result_package_report.hpp"

namespace {
using namespace biocore;

[[nodiscard]] application::BatchResultPackage package() {
    application::BatchResultArtifactLink link{
        .sample_id = "sample<&>", .job_id = "job-a", .attempt_number = 2,
        .attempt_mode = application::BatchAttemptMode::resume,
        .managed_file_id = "file-a", .step_id = "qc", .output_port = "table",
        .module_id = "org.biocore.fastqqc.stats", .plugin_version = "0.1.0",
        .file_type = "tsv", .relative_project_path = "outputs/job-a--qc--table.out",
        .size_bytes = 12, .sha256 = std::string(64U, 'a'),
    };
    application::BatchQcSummary qc{
        .sample_id = "sample<&>", .node_id = "qc", .module_id = "org.biocore.fastqqc.stats",
        .plugin_version = "0.1.0", .metric_schema_version = 1U,
        .parameter_signature = "none", .reference_signature = "not_applicable",
        .reference_evidence_complete = true, .stable = true, .source_artifact = link,
        .metrics = {{"read_count", application::BatchQcMetricValueKind::integer, std::string{"10"}},
                    {"missing", application::BatchQcMetricValueKind::null_value, std::nullopt}},
    };
    application::BatchSampleResults sample{
        .sample_id = "sample<&>", .disposition = application::BatchPlanSampleDisposition::included,
        .state = application::BatchResultSampleState::submitted,
        .latest_job_id = std::string{"job-a"},
        .latest_job_status = domain::JobStatus::completed,
        .latest_attempt_number = 2, .latest_attempt_mode = application::BatchAttemptMode::resume,
        .artifacts = {link}, .qc_summaries = {qc},
    };
    return {
        .schema_version = 1U, .producer_name = "OpenGenesis-BioCore",
        .producer_version = "0.5.0-dev", .generated_at_utc = "2026-09-28T10:00:00Z",
        .stable_snapshot = true, .complete_results = true,
        .overview = {
            .plan_id = "plan-1", .project_id = "project-1", .template_id = "template-1",
            .template_version = "1.0.0", .samples = {sample},
            .qc_comparisons = {{"qc", "org.biocore.fastqqc.stats",
                                application::BatchQcComparisonState::comparable,
                                {"sample<&>"}, "same contract"}},
            .issues = {{"sample<&>", "note", "<not html>"}},
        },
        .verified_artifacts = {{link, std::string(64U, 'a')}},
    };
}

[[nodiscard]] bool manifest_is_deterministic_and_safe() {
    const auto value = package();
    const auto first = presentation::render_batch_result_package_manifest_json(value);
    const auto second = presentation::render_batch_result_package_manifest_json(value);
    return first == second &&
           first.find("\"schemaVersion\":1") != std::string::npos &&
           first.find("\"verifiedSha256\":\"" + std::string(64U, 'a') + "\"") != std::string::npos &&
           first.find("\"value\":null") != std::string::npos &&
           first.find("/secret/") == std::string::npos;
}

[[nodiscard]] bool html_is_escaped_and_safe() {
    const auto html = presentation::render_batch_result_package_html(package());
    return html.find("sample&lt;&amp;&gt;") != std::string::npos &&
           html.find("&lt;not html&gt;") != std::string::npos &&
           html.find("outputs/job-a--qc--table.out") != std::string::npos &&
           html.find("/secret/") == std::string::npos &&
           html.find(std::string(64U, 'a')) != std::string::npos;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    const std::string_view mode = argv[1];
    const bool ok = mode == "json" ? manifest_is_deterministic_and_safe()
                  : mode == "html" ? html_is_escaped_and_safe()
                  : false;
    if (!ok) {
        std::cerr << "Batch result package report test failed: " << mode << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
