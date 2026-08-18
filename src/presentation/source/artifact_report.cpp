#include "biocore/presentation/artifact_report.hpp"

#include <array>
#include <charconv>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/job_status.hpp"

namespace biocore::presentation {
namespace {

[[nodiscard]] std::string escape_json(const std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    constexpr std::array hexadecimal_digits{
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    for (const char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
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
                    escaped += hexadecimal_digits[(character >> 4U) & 0x0fU];
                    escaped += hexadecimal_digits[character & 0x0fU];
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

[[nodiscard]] std::string number_json(const double value) {
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(
        buffer.data(),
        buffer.data() + buffer.size(),
        value,
        std::chars_format::general,
        std::numeric_limits<double>::max_digits10
    );
    if (result.ec != std::errc{}) {
        return "0";
    }
    return std::string{buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data())};
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

[[nodiscard]] std::string optional_html(const std::optional<std::string>& value) {
    return value.has_value() ? escape_html(*value) : "—";
}

}  // namespace

std::string render_artifact_metadata_json(const application::ArtifactMetadata& artifact) {
    return "{" +
           std::string{"\"managedFileId\":"} + quote_json(artifact.managed_file_id) +
           ",\"displayName\":" + quote_json(artifact.display_name) +
           ",\"jobId\":" + quote_json(artifact.job_id) +
           ",\"stepId\":" + quote_json(artifact.step_id) +
           ",\"outputPort\":" + quote_json(artifact.output_port) +
           ",\"pluginId\":" + quote_json(artifact.plugin_id) +
           ",\"pluginVersion\":" + quote_json(artifact.plugin_version) +
           ",\"moduleId\":" + quote_json(artifact.module_id) +
           ",\"fileType\":" + quote_json(artifact.file_type) +
           ",\"relativeProjectPath\":" + quote_json(artifact.relative_project_path) +
           ",\"sizeBytes\":" + std::to_string(artifact.size_bytes) +
           ",\"checksumAlgorithm\":" + optional_json(artifact.checksum_algorithm) +
           ",\"checksumValue\":" + optional_json(artifact.checksum_value) +
           ",\"stepProgress\":" + number_json(artifact.step_progress) +
           ",\"registeredAtUtc\":" + quote_json(artifact.registered_at_utc) + "}";
}

std::string render_pipeline_execution_report_json(
    const application::PipelineExecutionReport& report
) {
    std::string artifacts{"["};
    for (std::size_t index = 0U; index < report.artifacts.size(); ++index) {
        if (index != 0U) artifacts += ',';
        artifacts += render_artifact_metadata_json(report.artifacts[index]);
    }
    artifacts += ']';

    return "{" +
           std::string{"\"schemaVersion\":1,\"jobId\":"} + quote_json(report.job_id) +
           ",\"analysisId\":" + optional_json(report.analysis_id) +
           ",\"pipelineId\":" + optional_json(report.pipeline_id) +
           ",\"pipelineVersion\":" + optional_json(report.pipeline_version) +
           ",\"status\":" + quote_json(domain::to_string(report.status)) +
           ",\"priority\":" + quote_json(domain::to_string(report.priority)) +
           ",\"progress\":" + number_json(report.progress) +
           ",\"activeStepId\":" + optional_json(report.active_step_id) +
           ",\"createdAtUtc\":" + quote_json(report.created_at_utc) +
           ",\"updatedAtUtc\":" + quote_json(report.updated_at_utc) +
           ",\"startedAtUtc\":" + optional_json(report.started_at_utc) +
           ",\"finishedAtUtc\":" + optional_json(report.finished_at_utc) +
           ",\"revision\":" + std::to_string(report.revision) +
           ",\"generatedAtUtc\":" + quote_json(report.generated_at_utc) +
           ",\"artifacts\":" + artifacts + "}";
}

std::string render_pipeline_execution_report_html(
    const application::PipelineExecutionReport& report
) {
    std::string rows;
    for (const auto& artifact : report.artifacts) {
        rows += "<tr><td>" + escape_html(artifact.step_id) + "</td><td>" +
                escape_html(artifact.output_port) + "</td><td>" +
                escape_html(artifact.display_name) + "</td><td>" +
                escape_html(artifact.file_type) + "</td><td>" +
                std::to_string(artifact.size_bytes) + "</td><td><code>" +
                optional_html(artifact.checksum_value) + "</code></td></tr>";
    }

    return "<!doctype html><html><head><meta charset=\"utf-8\"><title>OpenGenesis-BioCore report " +
           escape_html(report.job_id) +
           "</title></head><body><main><h1>OpenGenesis-BioCore Pipeline Report</h1><dl><dt>Job</dt><dd>" +
           escape_html(report.job_id) + "</dd><dt>Status</dt><dd>" +
           escape_html(domain::to_string(report.status)) + "</dd><dt>Pipeline</dt><dd>" +
           optional_html(report.pipeline_id) + "</dd><dt>Generated</dt><dd>" +
           escape_html(report.generated_at_utc) +
           "</dd></dl><table><thead><tr><th>Step</th><th>Port</th><th>File</th><th>Type</th>"
           "<th>Bytes</th><th>SHA-256</th></tr></thead><tbody>" +
           rows + "</tbody></table></main></body></html>";
}

}  // namespace biocore::presentation
