#include "biocore/presentation/batch_result_package_report.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "biocore/application/batch_execution.hpp"
#include "biocore/application/batch_plan.hpp"
#include "biocore/domain/job_status.hpp"

namespace biocore::presentation {
namespace {

[[nodiscard]] std::string escape_json(const std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    constexpr std::array hexadecimal{
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
    };
    for (const char raw : value) {
        const auto character = static_cast<unsigned char>(raw);
        switch (character) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (character < 0x20U) {
                    escaped += "\\u00";
                    escaped += hexadecimal[(character >> 4U) & 0x0fU];
                    escaped += hexadecimal[character & 0x0fU];
                } else {
                    escaped += static_cast<char>(character);
                }
                break;
        }
    }
    return escaped;
}

[[nodiscard]] std::string quote_json(const std::string_view value) {
    return "\"" + escape_json(value) + "\"";
}

[[nodiscard]] std::string optional_json(const std::optional<std::string>& value) {
    return value.has_value() ? quote_json(*value) : "null";
}

[[nodiscard]] std::string escape_html(const std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += "&quot;"; break;
            case '\'': escaped += "&#39;"; break;
            default: escaped += character; break;
        }
    }
    return escaped;
}

[[nodiscard]] std::string_view metric_kind(
    const application::BatchQcMetricValueKind kind
) noexcept {
    using Kind = application::BatchQcMetricValueKind;
    switch (kind) {
        case Kind::integer: return "integer";
        case Kind::number: return "number";
        case Kind::boolean: return "boolean";
        case Kind::string: return "string";
        case Kind::null_value: return "null";
    }
    return "string";
}

[[nodiscard]] const application::BatchResultPackageArtifact* verified_entry(
    const application::BatchResultPackage& package,
    const application::BatchResultArtifactLink& artifact
) noexcept {
    for (const auto& entry : package.verified_artifacts) {
        if (entry.artifact.managed_file_id == artifact.managed_file_id &&
            entry.artifact.job_id == artifact.job_id &&
            entry.artifact.step_id == artifact.step_id &&
            entry.artifact.output_port == artifact.output_port) {
            return &entry;
        }
    }
    return nullptr;
}

[[nodiscard]] std::string artifact_json(
    const application::BatchResultPackage& package,
    const application::BatchResultArtifactLink& artifact
) {
    const auto* verified = verified_entry(package, artifact);
    const std::string verified_sha = verified == nullptr ? std::string{} : verified->verified_sha256;
    return "{\"managedFileId\":" + quote_json(artifact.managed_file_id) +
           ",\"jobId\":" + quote_json(artifact.job_id) +
           ",\"attemptNumber\":" + std::to_string(artifact.attempt_number) +
           ",\"attemptMode\":" + quote_json(application::to_string(artifact.attempt_mode)) +
           ",\"stepId\":" + quote_json(artifact.step_id) +
           ",\"outputPort\":" + quote_json(artifact.output_port) +
           ",\"moduleId\":" + quote_json(artifact.module_id) +
           ",\"pluginVersion\":" + quote_json(artifact.plugin_version) +
           ",\"fileType\":" + quote_json(artifact.file_type) +
           ",\"relativeProjectPath\":" + quote_json(artifact.relative_project_path) +
           ",\"sizeBytes\":" + std::to_string(artifact.size_bytes) +
           ",\"persistedSha256\":" + optional_json(artifact.sha256) +
           ",\"verifiedSha256\":" + quote_json(verified_sha) + "}";
}

[[nodiscard]] std::string metric_json(const application::BatchQcMetric& metric) {
    return "{\"key\":" + quote_json(metric.key) +
           ",\"kind\":" + quote_json(metric_kind(metric.kind)) +
           ",\"value\":" + optional_json(metric.value) + "}";
}

[[nodiscard]] std::string qc_summary_json(
    const application::BatchResultPackage& package,
    const application::BatchQcSummary& summary
) {
    std::string metrics{"["};
    for (std::size_t index = 0U; index < summary.metrics.size(); ++index) {
        if (index != 0U) metrics += ',';
        metrics += metric_json(summary.metrics[index]);
    }
    metrics += ']';
    return "{\"nodeId\":" + quote_json(summary.node_id) +
           ",\"moduleId\":" + quote_json(summary.module_id) +
           ",\"pluginVersion\":" + quote_json(summary.plugin_version) +
           ",\"metricSchemaVersion\":" + std::to_string(summary.metric_schema_version) +
           ",\"parameterSignature\":" + quote_json(summary.parameter_signature) +
           ",\"referenceSignature\":" + quote_json(summary.reference_signature) +
           ",\"referenceEvidenceComplete\":" +
           std::string{summary.reference_evidence_complete ? "true" : "false"} +
           ",\"stable\":" + std::string{summary.stable ? "true" : "false"} +
           ",\"sourceArtifact\":" + artifact_json(package, summary.source_artifact) +
           ",\"metrics\":" + metrics + "}";
}

[[nodiscard]] std::string sample_json(
    const application::BatchResultPackage& package,
    const application::BatchSampleResults& sample
) {
    std::string artifacts{"["};
    for (std::size_t index = 0U; index < sample.artifacts.size(); ++index) {
        if (index != 0U) artifacts += ',';
        artifacts += artifact_json(package, sample.artifacts[index]);
    }
    artifacts += ']';

    std::string qc{"["};
    for (std::size_t index = 0U; index < sample.qc_summaries.size(); ++index) {
        if (index != 0U) qc += ',';
        qc += qc_summary_json(package, sample.qc_summaries[index]);
    }
    qc += ']';

    return "{\"sampleId\":" + quote_json(sample.sample_id) +
           ",\"disposition\":" + quote_json(application::to_string(sample.disposition)) +
           ",\"resultState\":" + quote_json(application::to_string(sample.state)) +
           ",\"latestJobId\":" + optional_json(sample.latest_job_id) +
           ",\"latestJobStatus\":" +
           (sample.latest_job_status.has_value()
                ? quote_json(domain::to_string(*sample.latest_job_status))
                : "null") +
           ",\"latestAttemptNumber\":" + std::to_string(sample.latest_attempt_number) +
           ",\"latestAttemptMode\":" +
           (sample.latest_attempt_mode.has_value()
                ? quote_json(application::to_string(*sample.latest_attempt_mode))
                : "null") +
           ",\"artifacts\":" + artifacts +
           ",\"qcSummaries\":" + qc + "}";
}

[[nodiscard]] std::string comparison_json(
    const application::BatchQcComparisonGroup& comparison
) {
    std::string samples{"["};
    for (std::size_t index = 0U; index < comparison.sample_ids.size(); ++index) {
        if (index != 0U) samples += ',';
        samples += quote_json(comparison.sample_ids[index]);
    }
    samples += ']';
    return "{\"nodeId\":" + quote_json(comparison.node_id) +
           ",\"moduleId\":" + quote_json(comparison.module_id) +
           ",\"state\":" + quote_json(application::to_string(comparison.state)) +
           ",\"sampleIds\":" + samples +
           ",\"reason\":" + quote_json(comparison.reason) + "}";
}

[[nodiscard]] std::string issue_json(const application::BatchResultIssue& issue) {
    return "{\"sampleId\":" + quote_json(issue.sample_id) +
           ",\"code\":" + quote_json(issue.code) +
           ",\"message\":" + quote_json(issue.message) + "}";
}

[[nodiscard]] std::string html_value(const std::optional<std::string>& value) {
    return value.has_value() ? escape_html(*value) : "—";
}

}  // namespace

std::string render_batch_result_package_manifest_json(
    const application::BatchResultPackage& package
) {
    std::string samples{"["};
    for (std::size_t index = 0U; index < package.overview.samples.size(); ++index) {
        if (index != 0U) samples += ',';
        samples += sample_json(package, package.overview.samples[index]);
    }
    samples += ']';

    std::string comparisons{"["};
    for (std::size_t index = 0U; index < package.overview.qc_comparisons.size(); ++index) {
        if (index != 0U) comparisons += ',';
        comparisons += comparison_json(package.overview.qc_comparisons[index]);
    }
    comparisons += ']';

    std::string issues{"["};
    for (std::size_t index = 0U; index < package.overview.issues.size(); ++index) {
        if (index != 0U) issues += ',';
        issues += issue_json(package.overview.issues[index]);
    }
    issues += ']';

    return "{\"schemaVersion\":" + std::to_string(package.schema_version) +
           ",\"producer\":{\"name\":" + quote_json(package.producer_name) +
           ",\"version\":" + quote_json(package.producer_version) + "}" +
           ",\"generatedAtUtc\":" + quote_json(package.generated_at_utc) +
           ",\"stableSnapshot\":" + std::string{package.stable_snapshot ? "true" : "false"} +
           ",\"completeResults\":" + std::string{package.complete_results ? "true" : "false"} +
           ",\"artifactCount\":" + std::to_string(package.verified_artifacts.size()) +
           ",\"plan\":{\"id\":" + quote_json(package.overview.plan_id) +
           ",\"projectId\":" + quote_json(package.overview.project_id) +
           ",\"templateId\":" + quote_json(package.overview.template_id) +
           ",\"templateVersion\":" + quote_json(package.overview.template_version) + "}" +
           ",\"samples\":" + samples +
           ",\"qcComparisons\":" + comparisons +
           ",\"issues\":" + issues + "}";
}

std::string render_batch_result_package_html(
    const application::BatchResultPackage& package
) {
    std::string sample_rows;
    std::string artifact_rows;
    std::string qc_sections;

    for (const auto& sample : package.overview.samples) {
        sample_rows += "<tr><td>" + escape_html(sample.sample_id) + "</td><td>" +
                       escape_html(application::to_string(sample.disposition)) + "</td><td>" +
                       escape_html(application::to_string(sample.state)) + "</td><td>" +
                       (sample.latest_job_status.has_value()
                            ? escape_html(domain::to_string(*sample.latest_job_status))
                            : "—") +
                       "</td><td>" + std::to_string(sample.latest_attempt_number) +
                       "</td><td>" + std::to_string(sample.artifacts.size()) + "</td></tr>";

        for (const auto& artifact : sample.artifacts) {
            const auto* verified = verified_entry(package, artifact);
            artifact_rows += "<tr><td>" + escape_html(sample.sample_id) + "</td><td>" +
                             escape_html(artifact.step_id) + "</td><td>" +
                             escape_html(artifact.output_port) + "</td><td>" +
                             escape_html(artifact.file_type) + "</td><td>" +
                             escape_html(artifact.relative_project_path) + "</td><td>" +
                             std::to_string(artifact.size_bytes) + "</td><td><code>" +
                             escape_html(verified == nullptr ? "" : verified->verified_sha256) +
                             "</code></td></tr>";
        }

        for (const auto& summary : sample.qc_summaries) {
            qc_sections += "<section><h3>QC: " + escape_html(sample.sample_id) + " / " +
                           escape_html(summary.node_id) + "</h3><p><strong>Module:</strong> " +
                           escape_html(summary.module_id) + " @ " +
                           escape_html(summary.plugin_version) +
                           " · <strong>Stable:</strong> " +
                           std::string{summary.stable ? "yes" : "no"} +
                           "</p><table><thead><tr><th>Metric</th><th>Kind</th><th>Value</th></tr></thead><tbody>";
            for (const auto& metric : summary.metrics) {
                qc_sections += "<tr><td>" + escape_html(metric.key) + "</td><td>" +
                               escape_html(metric_kind(metric.kind)) + "</td><td>" +
                               html_value(metric.value) + "</td></tr>";
            }
            qc_sections += "</tbody></table></section>";
        }
    }

    std::string comparison_rows;
    for (const auto& comparison : package.overview.qc_comparisons) {
        comparison_rows += "<tr><td>" + escape_html(comparison.node_id) + "</td><td>" +
                           escape_html(comparison.module_id) + "</td><td>" +
                           escape_html(application::to_string(comparison.state)) + "</td><td>" +
                           escape_html(comparison.reason) + "</td></tr>";
    }

    std::string issue_rows;
    for (const auto& issue : package.overview.issues) {
        issue_rows += "<tr><td>" + escape_html(issue.sample_id) + "</td><td>" +
                      escape_html(issue.code) + "</td><td>" +
                      escape_html(issue.message) + "</td></tr>";
    }
    if (issue_rows.empty()) {
        issue_rows = "<tr><td colspan=\"3\">No aggregation issues recorded.</td></tr>";
    }

    return "<!doctype html><html><head><meta charset=\"utf-8\">"
           "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
           "<title>OpenGenesis-BioCore Batch Report " +
           escape_html(package.overview.plan_id) +
           "</title><style>"
           "body{font-family:system-ui,sans-serif;margin:2rem;line-height:1.45;color:#111}"
           "main{max-width:1200px;margin:auto}table{border-collapse:collapse;width:100%;margin:1rem 0 2rem}"
           "th,td{border:1px solid #bbb;padding:.45rem;text-align:left;vertical-align:top}"
           "th{background:#f2f2f2}code{font-size:.85em;word-break:break-all}"
           ".meta{display:grid;grid-template-columns:max-content 1fr;gap:.35rem 1rem}"
           "</style></head><body><main><h1>OpenGenesis-BioCore Batch Results Report</h1>"
           "<dl class=\"meta\"><dt>Plan</dt><dd>" + escape_html(package.overview.plan_id) +
           "</dd><dt>Project</dt><dd>" + escape_html(package.overview.project_id) +
           "</dd><dt>Template</dt><dd>" + escape_html(package.overview.template_id) + " @ " +
           escape_html(package.overview.template_version) +
           "</dd><dt>Generated</dt><dd>" + escape_html(package.generated_at_utc) +
           "</dd><dt>Producer</dt><dd>" + escape_html(package.producer_name) + " " +
           escape_html(package.producer_version) +
           "</dd><dt>Stable snapshot</dt><dd>" +
           std::string{package.stable_snapshot ? "yes" : "no"} +
           "</dd><dt>Complete results</dt><dd>" +
           std::string{package.complete_results ? "yes" : "no"} +
           "</dd><dt>Verified artifacts</dt><dd>" +
           std::to_string(package.verified_artifacts.size()) +
           "</dd></dl><h2>Samples</h2><table><thead><tr><th>Sample</th><th>Disposition</th>"
           "<th>Result state</th><th>Latest job status</th><th>Attempt</th><th>Artifacts</th>"
           "</tr></thead><tbody>" + sample_rows +
           "</tbody></table><h2>Verified current artifacts</h2><table><thead><tr>"
           "<th>Sample</th><th>Step</th><th>Port</th><th>Type</th><th>Relative project path</th>"
           "<th>Bytes</th><th>Verified SHA-256</th></tr></thead><tbody>" + artifact_rows +
           "</tbody></table><h2>QC comparisons</h2><table><thead><tr><th>Node</th><th>Module</th>"
           "<th>State</th><th>Reason</th></tr></thead><tbody>" + comparison_rows +
           "</tbody></table><h2>QC metrics</h2>" + qc_sections +
           "<h2>Issues</h2><table><thead><tr><th>Sample</th><th>Code</th><th>Message</th>"
           "</tr></thead><tbody>" + issue_rows +
           "</tbody></table></main></body></html>";
}

}  // namespace biocore::presentation
