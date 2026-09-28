#include "biocore/application/batch_results_service.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "biocore/application/i_batch_execution_store.hpp"
#include "biocore/application/i_batch_plan_store.hpp"
#include "biocore/application/i_job_repository.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_result_artifact_reader.hpp"
#include "biocore/domain/plugin_io_contract.hpp"
#include "biocore/domain/vcf_ingestion.hpp"

namespace biocore::application {
namespace {

using ArtifactKey = std::pair<std::string, std::string>;

struct CurrentSampleContext final {
    const ApprovedBatchSamplePlan* sample{nullptr};
    const BatchExecutionAttemptRecord* latest_attempt{nullptr};
    std::optional<domain::Job> latest_job;
    std::map<ArtifactKey, BatchResultArtifactLink> artifacts;
};

[[nodiscard]] bool blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
               return std::isspace(static_cast<unsigned char>(character)) != 0;
           });
}

void require_plan_id(const std::string_view plan_id) {
    if (blank(plan_id) || plan_id.size() > 128U ||
        plan_id.find('\0') != std::string_view::npos) {
        throw std::invalid_argument{"Batch results plan id is invalid"};
    }
}

[[nodiscard]] const BatchPlanNodeSnapshot* find_node(
    const ApprovedBatchSamplePlan& sample,
    const std::string_view node_id
) {
    const auto found = std::ranges::find_if(sample.nodes, [node_id](const auto& node) {
        return node.node_id == node_id;
    });
    return found == sample.nodes.end() ? nullptr : &*found;
}

[[nodiscard]] const BatchPlanOutputSnapshot* find_output(
    const BatchPlanNodeSnapshot& node,
    const std::string_view output_port
) {
    const auto found = std::ranges::find_if(node.outputs, [output_port](const auto& output) {
        return output.port_name == output_port;
    });
    return found == node.outputs.end() ? nullptr : &*found;
}

[[nodiscard]] std::map<std::string, std::vector<BatchExecutionAttemptRecord>, std::less<>>
index_attempts(std::vector<BatchExecutionAttemptRecord> attempts) {
    std::map<std::string, std::vector<BatchExecutionAttemptRecord>, std::less<>> indexed;
    for (auto& attempt : attempts) {
        indexed[attempt.sample_id].push_back(std::move(attempt));
    }
    for (auto& [sample_id, values] : indexed) {
        static_cast<void>(sample_id);
        std::ranges::sort(values, [](const auto& left, const auto& right) {
            return left.attempt_number < right.attempt_number;
        });
    }
    return indexed;
}

[[nodiscard]] std::vector<const BatchExecutionAttemptRecord*> current_result_segment(
    const std::vector<BatchExecutionAttemptRecord>& attempts
) {
    std::vector<const BatchExecutionAttemptRecord*> segment;
    if (attempts.empty()) return segment;
    for (std::size_t index = attempts.size(); index > 0U; --index) {
        const auto& attempt = attempts[index - 1U];
        segment.push_back(&attempt);
        if (attempt.mode != BatchAttemptMode::resume) break;
    }
    return segment;
}

[[nodiscard]] BatchResultArtifactLink artifact_link(
    const std::string_view sample_id,
    const BatchExecutionAttemptRecord& attempt,
    const GeneratedOutputArtifact& artifact
) {
    return BatchResultArtifactLink{
        .sample_id = std::string{sample_id},
        .job_id = attempt.job_id,
        .attempt_number = attempt.attempt_number,
        .attempt_mode = attempt.mode,
        .managed_file_id = std::string{artifact.file.id()},
        .step_id = artifact.provenance.step_id,
        .output_port = artifact.provenance.output_port,
        .module_id = artifact.provenance.module_id,
        .plugin_version = artifact.provenance.plugin_version,
        .file_type = artifact.provenance.file_type,
        .relative_project_path = artifact.provenance.relative_project_path,
        .size_bytes = artifact.file.size_bytes(),
        .sha256 = artifact.file.checksum_algorithm() == std::optional<std::string>{"sha256"}
            ? artifact.file.checksum_value()
            : std::optional<std::string>{},
    };
}

void add_artifact_issue(
    std::vector<BatchResultIssue>& issues,
    const std::string_view sample_id,
    std::string code,
    std::string message
) {
    issues.push_back(BatchResultIssue{
        .sample_id = std::string{sample_id},
        .code = std::move(code),
        .message = std::move(message),
    });
}

[[nodiscard]] std::map<ArtifactKey, BatchResultArtifactLink> collect_current_artifacts(
    const ApprovedBatchSamplePlan& sample,
    const std::vector<BatchExecutionAttemptRecord>& attempts,
    IManagedFileRepository& managed_files,
    std::vector<BatchResultIssue>& issues
) {
    std::map<ArtifactKey, BatchResultArtifactLink> selected;
    std::set<ArtifactKey> shadowed_by_newer_attempt;
    for (const auto* attempt : current_result_segment(attempts)) {
        const std::set<std::string, std::less<>> scope{
            attempt->execution_node_ids.begin(), attempt->execution_node_ids.end()
        };
        for (const auto& artifact : managed_files.list_generated_outputs(attempt->job_id)) {
            if (!scope.contains(artifact.provenance.step_id)) {
                add_artifact_issue(
                    issues, sample.sample_id, "artifact_outside_attempt_scope",
                    "Generated artifact is outside its immutable batch attempt node scope"
                );
                continue;
            }
            const auto* node = find_node(sample, artifact.provenance.step_id);
            if (node == nullptr) {
                add_artifact_issue(
                    issues, sample.sample_id, "artifact_node_unknown",
                    "Generated artifact does not belong to the frozen sample plan"
                );
                continue;
            }
            const auto* output = find_output(*node, artifact.provenance.output_port);
            if (output == nullptr || output->artifact_type != artifact.provenance.file_type ||
                node->module_id != artifact.provenance.module_id ||
                node->plugin_version != artifact.provenance.plugin_version) {
                add_artifact_issue(
                    issues, sample.sample_id, "artifact_contract_mismatch",
                    "Generated artifact provenance does not match the frozen node output contract"
                );
                continue;
            }
            const ArtifactKey key{artifact.provenance.step_id, artifact.provenance.output_port};
            if (!shadowed_by_newer_attempt.contains(key)) {
                selected.try_emplace(
                    key, artifact_link(sample.sample_id, *attempt, artifact)
                );
            }
        }
        for (const auto& node_id : scope) {
            const auto* node = find_node(sample, node_id);
            if (node == nullptr) continue;
            for (const auto& output : node->outputs) {
                shadowed_by_newer_attempt.emplace(node->node_id, output.port_name);
            }
        }
    }
    return selected;
}

[[nodiscard]] bool qc_module(const std::string_view module_id) noexcept {
    return module_id == "org.biocore.fastaqc.stats" ||
           module_id == "org.biocore.fastqqc.stats" ||
           module_id == "org.biocore.fastqqc.paired-stats" ||
           module_id == "org.biocore.fastqqc.trim-single" ||
           module_id == "org.biocore.fastqqc.trim-paired" ||
           module_id == "org.biocore.alignmentqc.summary" ||
           module_id == "org.biocore.vcfqc.filter";
}

[[nodiscard]] bool reference_sensitive_qc(const std::string_view module_id) noexcept {
    return module_id == "org.biocore.alignmentqc.summary" ||
           module_id == "org.biocore.vcfqc.filter";
}

[[nodiscard]] std::string qc_port(const std::string_view module_id) {
    return module_id == "org.biocore.vcfqc.filter" ? "summary" : "table";
}

[[nodiscard]] std::string normalized_parameter_name(std::string value) {
    std::ranges::replace(value, '-', '_');
    return value;
}

[[nodiscard]] std::string parameter_signature(const BatchPlanNodeSnapshot& node) {
    std::vector<std::string> parts;
    parts.reserve(node.parameters.size());
    for (const auto& parameter : node.parameters) {
        parts.push_back(
            parameter.name + ":" + std::string{domain::to_string(parameter.type)} + "=" +
            parameter.value
        );
    }
    std::ranges::sort(parts);
    std::string result;
    for (const auto& part : parts) {
        if (!result.empty()) result.push_back('|');
        result += part;
    }
    return result.empty() ? "none" : result;
}

struct ReferenceSignature final {
    std::string value;
    bool complete{true};
};

[[nodiscard]] ReferenceSignature reference_signature(
    const ApprovedBatchSamplePlan& sample,
    const BatchPlanNodeSnapshot& node
) {
    std::vector<std::string> parts;
    for (const auto& plan_node : sample.nodes) {
        for (const auto& input : plan_node.inputs) {
            if (!input.managed_file.has_value() ||
                input.managed_file->role != BatchInputFileRole::reference) {
                continue;
            }
            const auto& file = *input.managed_file;
            parts.push_back(
                file.file_type + ":" + std::to_string(file.size_bytes) + ":" + file.sha256
            );
        }
    }
    std::ranges::sort(parts);
    parts.erase(std::unique(parts.begin(), parts.end()), parts.end());
    if (parts.empty()) {
        if (reference_sensitive_qc(node.module_id)) {
            return {"unverified", false};
        }
        return {"not_applicable", true};
    }
    std::string result;
    for (const auto& part : parts) {
        if (!result.empty()) result.push_back('|');
        result += part;
    }
    return {std::move(result), true};
}

[[nodiscard]] bool looks_integer(const std::string_view value) noexcept {
    if (value.empty()) return false;
    std::size_t index = value.front() == '-' ? 1U : 0U;
    if (index == value.size()) return false;
    for (; index < value.size(); ++index) {
        if (value[index] < '0' || value[index] > '9') return false;
    }
    return true;
}

[[nodiscard]] bool looks_number(const std::string_view value) noexcept {
    if (value.empty()) return false;
    bool digit = false;
    bool decimal = false;
    bool exponent = false;
    for (std::size_t index = 0U; index < value.size(); ++index) {
        const char c = value[index];
        if (c >= '0' && c <= '9') {
            digit = true;
            continue;
        }
        if ((c == '+' || c == '-') &&
            (index == 0U || (index > 0U && (value[index - 1U] == 'e' || value[index - 1U] == 'E')))) {
            continue;
        }
        if (c == '.' && !decimal && !exponent) {
            decimal = true;
            continue;
        }
        if ((c == 'e' || c == 'E') && digit && !exponent) {
            exponent = true;
            digit = false;
            continue;
        }
        return false;
    }
    return digit && (decimal || exponent);
}

[[nodiscard]] BatchQcMetric make_metric(std::string key, std::string value) {
    if (value == "null" || value.empty()) {
        return {std::move(key), BatchQcMetricValueKind::null_value, std::nullopt};
    }
    if (value == "true" || value == "false") {
        return {std::move(key), BatchQcMetricValueKind::boolean, std::move(value)};
    }
    if (looks_integer(value)) {
        return {std::move(key), BatchQcMetricValueKind::integer, std::move(value)};
    }
    if (looks_number(value)) {
        return {std::move(key), BatchQcMetricValueKind::number, std::move(value)};
    }
    return {std::move(key), BatchQcMetricValueKind::string, std::move(value)};
}

[[nodiscard]] std::vector<BatchQcMetric> parse_metric_tsv(
    const std::string_view text,
    const BatchPlanNodeSnapshot& node
) {
    if (text.size() > BatchResultsService::maximum_qc_summary_bytes ||
        text.find('\0') != std::string_view::npos) {
        throw std::invalid_argument{"QC metric table is invalid"};
    }
    std::set<std::string, std::less<>> option_names;
    for (const auto& parameter : node.parameters) {
        option_names.emplace(normalized_parameter_name(parameter.name));
    }

    std::istringstream input{std::string{text}};
    std::string line;
    if (!std::getline(input, line) || line != "metric\tvalue") {
        throw std::invalid_argument{"QC metric table header is invalid"};
    }
    std::set<std::string, std::less<>> seen;
    std::vector<BatchQcMetric> metrics;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto tab = line.find('\t');
        if (tab == std::string::npos || tab == 0U || tab > 200U ||
            line.size() - tab - 1U > 4096U) {
            throw std::invalid_argument{"QC metric table row is invalid"};
        }
        std::string key = line.substr(0U, tab);
        if (!seen.emplace(key).second) {
            throw std::invalid_argument{"QC metric table contains a duplicate metric"};
        }
        if (option_names.contains(key)) continue;
        metrics.push_back(make_metric(std::move(key), line.substr(tab + 1U)));
        if (metrics.size() > 4096U) {
            throw std::length_error{"QC metric table contains too many metrics"};
        }
    }
    if (metrics.empty()) {
        throw std::invalid_argument{"QC metric table contains no metrics"};
    }
    std::ranges::sort(metrics, [](const auto& left, const auto& right) {
        return left.key < right.key;
    });
    return metrics;
}

void skip_ws(const std::string_view text, std::size_t& offset) {
    while (offset < text.size() &&
           std::isspace(static_cast<unsigned char>(text[offset])) != 0) {
        ++offset;
    }
}

[[nodiscard]] std::string parse_json_string(const std::string_view text, std::size_t& offset) {
    skip_ws(text, offset);
    if (offset >= text.size() || text[offset] != '"') {
        throw std::invalid_argument{"QC JSON string is invalid"};
    }
    ++offset;
    std::string value;
    while (offset < text.size()) {
        const char c = text[offset++];
        if (c == '"') return value;
        if (c == '\\') {
            if (offset >= text.size()) throw std::invalid_argument{"QC JSON escape is invalid"};
            const char escaped = text[offset++];
            switch (escaped) {
                case '"': case '\\': case '/': value.push_back(escaped); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                default: throw std::invalid_argument{"QC JSON escape is unsupported"};
            }
        } else {
            if (static_cast<unsigned char>(c) < 0x20U) {
                throw std::invalid_argument{"QC JSON string contains a control character"};
            }
            value.push_back(c);
        }
        if (value.size() > 4096U) throw std::length_error{"QC JSON string is too large"};
    }
    throw std::invalid_argument{"QC JSON string is unterminated"};
}

[[nodiscard]] std::string parse_json_scalar(const std::string_view text, std::size_t& offset) {
    skip_ws(text, offset);
    if (offset >= text.size()) throw std::invalid_argument{"QC JSON scalar is missing"};
    if (text[offset] == '"') return parse_json_string(text, offset);
    const auto begin = offset;
    while (offset < text.size() && text[offset] != ',' && text[offset] != '}') ++offset;
    auto end = offset;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1U])) != 0) --end;
    if (end == begin) throw std::invalid_argument{"QC JSON scalar is empty"};
    return std::string{text.substr(begin, end - begin)};
}

[[nodiscard]] std::uint32_t parse_schema_version(const std::string_view text) {
    const auto key = text.find("\"schemaVersion\"");
    if (key == std::string_view::npos) {
        throw std::invalid_argument{"VCF QC summary has no schemaVersion"};
    }
    auto offset = text.find(':', key);
    if (offset == std::string_view::npos) throw std::invalid_argument{"VCF QC schemaVersion is invalid"};
    ++offset;
    skip_ws(text, offset);
    const auto begin = offset;
    while (offset < text.size() && text[offset] >= '0' && text[offset] <= '9') ++offset;
    if (begin == offset) throw std::invalid_argument{"VCF QC schemaVersion is invalid"};
    std::uint32_t version = 0U;
    const auto parsed = std::from_chars(text.data() + begin, text.data() + offset, version);
    if (parsed.ec != std::errc{} || version == 0U) {
        throw std::invalid_argument{"VCF QC schemaVersion is invalid"};
    }
    return version;
}

[[nodiscard]] std::vector<BatchQcMetric> parse_vcf_qc_metrics(const std::string_view text) {
    if (text.size() > BatchResultsService::maximum_qc_summary_bytes ||
        text.find('\0') != std::string_view::npos ||
        text.find("\"module\":\"org.biocore.vcfqc.filter\"") == std::string_view::npos) {
        throw std::invalid_argument{"VCF QC summary identity is invalid"};
    }
    const auto key = text.find("\"metrics\"");
    if (key == std::string_view::npos) throw std::invalid_argument{"VCF QC metrics object is missing"};
    auto offset = text.find('{', key);
    if (offset == std::string_view::npos) throw std::invalid_argument{"VCF QC metrics object is invalid"};
    ++offset;
    std::set<std::string, std::less<>> seen;
    std::vector<BatchQcMetric> metrics;
    for (;;) {
        skip_ws(text, offset);
        if (offset >= text.size()) throw std::invalid_argument{"VCF QC metrics object is unterminated"};
        if (text[offset] == '}') {
            ++offset;
            break;
        }
        std::string metric_key = parse_json_string(text, offset);
        skip_ws(text, offset);
        if (offset >= text.size() || text[offset] != ':') {
            throw std::invalid_argument{"VCF QC metric separator is invalid"};
        }
        ++offset;
        std::string value = parse_json_scalar(text, offset);
        if (!seen.emplace(metric_key).second) {
            throw std::invalid_argument{"VCF QC summary contains a duplicate metric"};
        }
        metrics.push_back(make_metric(std::move(metric_key), std::move(value)));
        if (metrics.size() > 4096U) throw std::length_error{"VCF QC summary contains too many metrics"};
        skip_ws(text, offset);
        if (offset >= text.size()) throw std::invalid_argument{"VCF QC metrics object is unterminated"};
        if (text[offset] == ',') {
            ++offset;
            continue;
        }
        if (text[offset] == '}') {
            ++offset;
            break;
        }
        throw std::invalid_argument{"VCF QC metrics object delimiter is invalid"};
    }
    if (metrics.empty()) throw std::invalid_argument{"VCF QC summary contains no metrics"};
    std::ranges::sort(metrics, [](const auto& left, const auto& right) {
        return left.key < right.key;
    });
    return metrics;
}

[[nodiscard]] std::string read_status_text(const ResultArtifactReadStatus status) {
    switch (status) {
        case ResultArtifactReadStatus::verified: return "verified";
        case ResultArtifactReadStatus::too_large: return "too_large";
        case ResultArtifactReadStatus::missing: return "missing";
        case ResultArtifactReadStatus::unsafe_path: return "unsafe_path";
        case ResultArtifactReadStatus::not_regular: return "not_regular";
        case ResultArtifactReadStatus::size_mismatch: return "size_mismatch";
        case ResultArtifactReadStatus::checksum_unavailable: return "checksum_unavailable";
        case ResultArtifactReadStatus::checksum_mismatch: return "checksum_mismatch";
        case ResultArtifactReadStatus::io_error: return "io_error";
    }
    return "io_error";
}

[[nodiscard]] std::optional<GeneratedOutputArtifact> resolve_artifact(
    IManagedFileRepository& managed_files,
    const BatchResultArtifactLink& link
) {
    auto artifact = managed_files.find_generated_output(link.job_id, link.step_id, link.output_port);
    if (!artifact.has_value() || artifact->file.id() != link.managed_file_id) return std::nullopt;
    return artifact;
}

[[nodiscard]] std::vector<BatchQcMetric> qc_metrics(
    IResultArtifactReader& reader,
    IManagedFileRepository& managed_files,
    const BatchResultArtifactLink& link,
    const BatchPlanNodeSnapshot& node,
    std::uint32_t& schema_version
) {
    const auto artifact = resolve_artifact(managed_files, link);
    if (!artifact.has_value()) throw std::runtime_error{"QC artifact disappeared from persistence"};
    const auto read = reader.read_verified_text(*artifact, BatchResultsService::maximum_qc_summary_bytes);
    if (read.status != ResultArtifactReadStatus::verified || !read.text.has_value()) {
        throw std::runtime_error{"QC artifact content is not verified: " + read_status_text(read.status)};
    }
    if (node.module_id == "org.biocore.vcfqc.filter") {
        schema_version = parse_schema_version(*read.text);
        if (schema_version != 1U) throw std::runtime_error{"Unsupported VCF QC metric schema version"};
        return parse_vcf_qc_metrics(*read.text);
    }
    schema_version = 1U;
    return parse_metric_tsv(*read.text, node);
}

[[nodiscard]] std::vector<std::pair<std::string, BatchQcMetricValueKind>> metric_shape(
    const BatchQcSummary& summary
) {
    std::vector<std::pair<std::string, BatchQcMetricValueKind>> shape;
    shape.reserve(summary.metrics.size());
    for (const auto& metric : summary.metrics) shape.emplace_back(metric.key, metric.kind);
    return shape;
}

[[nodiscard]] std::set<ArtifactKey> consumed_outputs(const ApprovedBatchSamplePlan& sample) {
    std::set<ArtifactKey> consumed;
    for (const auto& node : sample.nodes) {
        for (const auto& input : node.inputs) {
            if (input.source_kind == BatchPlanInputSourceKind::node_output) {
                consumed.emplace(input.source_id, input.source_port);
            }
        }
    }
    return consumed;
}

[[nodiscard]] std::vector<ArtifactKey> terminal_vcf_outputs(
    const ApprovedBatchSamplePlan& sample
) {
    const auto consumed = consumed_outputs(sample);
    std::vector<ArtifactKey> outputs;
    for (const auto& node : sample.nodes) {
        for (const auto& output : node.outputs) {
            const ArtifactKey key{node.node_id, output.port_name};
            if (output.artifact_type == "vcf" && !consumed.contains(key)) outputs.push_back(key);
        }
    }
    return outputs;
}

[[nodiscard]] std::string terminal_contract_signature(
    const ApprovedBatchSamplePlan& sample,
    const BatchPlanNodeSnapshot& node
) {
    const auto reference = reference_signature(sample, node);
    return node.module_id + "|" + node.plugin_version + "|" + parameter_signature(node) +
           "|reference=" + reference.value;
}

[[nodiscard]] const BatchPlanNodeSnapshot& require_node(
    const ApprovedBatchSamplePlan& sample,
    const std::string& node_id
) {
    const auto* node = find_node(sample, node_id);
    if (node == nullptr) throw std::logic_error{"Frozen batch terminal node disappeared"};
    return *node;
}

}  // namespace

BatchResultsService::BatchResultsService(
    IBatchPlanStore& plans,
    IBatchExecutionStore& executions,
    IJobRepository& jobs,
    IManagedFileRepository& managed_files,
    IResultArtifactReader& artifact_reader
) noexcept
    : plans_{plans}, executions_{executions}, jobs_{jobs}, managed_files_{managed_files},
      artifact_reader_{artifact_reader} {}

BatchResultsOverview BatchResultsService::overview(const std::string_view plan_id) {
    require_plan_id(plan_id);
    const auto plan = plans_.find(plan_id);
    if (!plan.has_value()) throw std::invalid_argument{"Batch results plan was not found"};

    BatchResultsOverview result{
        .plan_id = plan->plan_id,
        .project_id = plan->project_id,
        .template_id = plan->template_id,
        .template_version = plan->template_version,
        .samples = {},
        .qc_comparisons = {},
        .issues = {},
    };

    const auto execution = executions_.find(plan_id);
    auto attempts_by_sample = execution.has_value()
        ? index_attempts(executions_.list_attempts(plan_id))
        : std::map<std::string, std::vector<BatchExecutionAttemptRecord>, std::less<>>{};

    std::map<std::pair<std::string, std::string>, std::vector<const BatchQcSummary*>> summaries_by_node;
    std::map<std::pair<std::string, std::string>, std::vector<std::string>> expected_by_node;

    for (const auto& sample : plan->samples) {
        BatchSampleResults sample_result{
            .sample_id = sample.sample_id,
            .disposition = sample.disposition,
            .state = sample.disposition == BatchPlanSampleDisposition::excluded
                ? BatchResultSampleState::excluded
                : BatchResultSampleState::not_submitted,
            .latest_job_id = std::nullopt,
            .latest_job_status = std::nullopt,
            .latest_attempt_number = 0,
            .latest_attempt_mode = std::nullopt,
            .artifacts = {},
            .qc_summaries = {},
        };

        for (const auto& node : sample.nodes) {
            if (sample.disposition == BatchPlanSampleDisposition::included && qc_module(node.module_id)) {
                expected_by_node[{node.node_id, node.module_id}].push_back(sample.sample_id);
            }
        }

        if (sample.disposition == BatchPlanSampleDisposition::excluded) {
            result.samples.push_back(std::move(sample_result));
            continue;
        }

        const auto attempts_it = attempts_by_sample.find(sample.sample_id);
        if (attempts_it == attempts_by_sample.end() || attempts_it->second.empty()) {
            add_artifact_issue(
                result.issues, sample.sample_id, "sample_not_submitted",
                "Included sample has no persisted batch attempt"
            );
            result.samples.push_back(std::move(sample_result));
            continue;
        }

        const auto& attempts = attempts_it->second;
        const auto& latest = attempts.back();
        const auto latest_job = jobs_.find_by_id(latest.job_id);
        if (!latest_job.has_value()) {
            throw std::runtime_error{"Batch result latest attempt references a missing Job"};
        }
        sample_result.state = BatchResultSampleState::submitted;
        sample_result.latest_job_id = latest.job_id;
        sample_result.latest_job_status = latest_job->status();
        sample_result.latest_attempt_number = latest.attempt_number;
        sample_result.latest_attempt_mode = latest.mode;

        auto selected = collect_current_artifacts(sample, attempts, managed_files_, result.issues);
        for (const auto& [key, link] : selected) {
            static_cast<void>(key);
            sample_result.artifacts.push_back(link);
        }
        std::ranges::sort(sample_result.artifacts, [](const auto& left, const auto& right) {
            return std::tie(left.step_id, left.output_port, left.job_id) <
                   std::tie(right.step_id, right.output_port, right.job_id);
        });

        for (const auto& node : sample.nodes) {
            if (!qc_module(node.module_id)) continue;
            const ArtifactKey key{node.node_id, qc_port(node.module_id)};
            const auto artifact_it = selected.find(key);
            if (artifact_it == selected.end()) {
                add_artifact_issue(
                    result.issues, sample.sample_id,
                    latest_job->status() == domain::JobStatus::completed
                        ? "qc_summary_missing" : "qc_summary_pending",
                    "QC output is not available in the current result lineage"
                );
                continue;
            }
            try {
                std::uint32_t schema_version = 1U;
                auto metrics = qc_metrics(
                    artifact_reader_, managed_files_, artifact_it->second, node, schema_version
                );
                const auto reference = reference_signature(sample, node);
                sample_result.qc_summaries.push_back(BatchQcSummary{
                    .sample_id = sample.sample_id,
                    .node_id = node.node_id,
                    .module_id = node.module_id,
                    .plugin_version = node.plugin_version,
                    .metric_schema_version = schema_version,
                    .parameter_signature = parameter_signature(node),
                    .reference_signature = reference.value,
                    .reference_evidence_complete = reference.complete,
                    .stable = latest_job->status() == domain::JobStatus::completed,
                    .source_artifact = artifact_it->second,
                    .metrics = std::move(metrics),
                });
            } catch (const std::exception& error) {
                add_artifact_issue(
                    result.issues, sample.sample_id, "qc_summary_unavailable",
                    std::string{"QC summary could not be safely aggregated: "} + error.what()
                );
            }
        }

        result.samples.push_back(std::move(sample_result));
    }

    for (auto& sample : result.samples) {
        for (auto& summary : sample.qc_summaries) {
            summaries_by_node[{summary.node_id, summary.module_id}].push_back(&summary);
        }
    }

    for (const auto& [key, expected_samples] : expected_by_node) {
        const auto found = summaries_by_node.find(key);
        const auto& summaries = found == summaries_by_node.end()
            ? std::vector<const BatchQcSummary*>{}
            : found->second;
        BatchQcComparisonGroup group{
            .node_id = key.first,
            .module_id = key.second,
            .state = BatchQcComparisonState::incomplete,
            .sample_ids = expected_samples,
            .reason = {},
        };
        if (summaries.size() != expected_samples.size()) {
            group.reason = "One or more included samples have no available QC metric set; missing values are not imputed";
        } else if (std::ranges::any_of(summaries, [](const auto* summary) { return !summary->stable; })) {
            group.reason = "One or more QC summaries belong to a non-completed latest attempt";
        } else if (std::ranges::any_of(summaries, [](const auto* summary) {
                       return !summary->reference_evidence_complete;
                   })) {
            group.reason = "Reference identity evidence is unavailable for a reference-sensitive QC comparison";
        } else {
            const auto* first = summaries.front();
            const auto first_shape = metric_shape(*first);
            const bool contract_mismatch = std::ranges::any_of(summaries, [first](const auto* summary) {
                return summary->plugin_version != first->plugin_version ||
                       summary->metric_schema_version != first->metric_schema_version ||
                       summary->parameter_signature != first->parameter_signature ||
                       summary->reference_signature != first->reference_signature;
            });
            if (contract_mismatch) {
                group.state = BatchQcComparisonState::incompatible;
                group.reason = "QC plugin/schema/parameter/reference contract differs across samples";
            } else if (std::ranges::any_of(summaries, [&first_shape](const auto* summary) {
                           return metric_shape(*summary) != first_shape;
                       })) {
                group.reason = "QC metric sets differ across samples; absent metrics remain missing rather than zero";
            } else {
                group.state = BatchQcComparisonState::comparable;
                group.reason = "QC metric contract is identical across completed sample results";
            }
        }
        result.qc_comparisons.push_back(std::move(group));
    }
    std::ranges::sort(result.qc_comparisons, [](const auto& left, const auto& right) {
        return std::tie(left.node_id, left.module_id) < std::tie(right.node_id, right.module_id);
    });
    return result;
}

BatchVariantMatrixPreview BatchResultsService::preview_variant_matrix(
    const std::string_view plan_id
) {
    require_plan_id(plan_id);
    const auto plan = plans_.find(plan_id);
    if (!plan.has_value()) throw std::invalid_argument{"Batch matrix plan was not found"};
    BatchVariantMatrixPreview preview{
        .plan_id = plan->plan_id,
        .ready = false,
        .sources = {},
        .issues = {},
    };
    const auto execution = executions_.find(plan_id);
    if (!execution.has_value()) {
        add_artifact_issue(preview.issues, {}, "batch_not_submitted", "Batch has no persisted execution");
        return preview;
    }
    const auto attempts_by_sample = index_attempts(executions_.list_attempts(plan_id));
    std::optional<std::string> expected_contract;

    for (const auto& sample : plan->samples) {
        if (sample.disposition != BatchPlanSampleDisposition::included) continue;
        const auto attempts_it = attempts_by_sample.find(sample.sample_id);
        if (attempts_it == attempts_by_sample.end() || attempts_it->second.empty()) {
            add_artifact_issue(preview.issues, sample.sample_id, "matrix_sample_not_submitted", "Sample has no batch attempt");
            continue;
        }
        const auto& latest = attempts_it->second.back();
        const auto job = jobs_.find_by_id(latest.job_id);
        if (!job.has_value()) throw std::runtime_error{"Batch matrix latest attempt references a missing Job"};
        if (job->status() != domain::JobStatus::completed) {
            add_artifact_issue(preview.issues, sample.sample_id, "matrix_sample_not_completed", "Sample latest attempt is not completed");
            continue;
        }
        const auto terminal = terminal_vcf_outputs(sample);
        if (terminal.size() != 1U) {
            add_artifact_issue(
                preview.issues, sample.sample_id,
                terminal.empty() ? "matrix_terminal_vcf_missing" : "matrix_terminal_vcf_ambiguous",
                terminal.empty()
                    ? "Frozen sample plan has no terminal VCF output"
                    : "Frozen sample plan has more than one terminal VCF output"
            );
            continue;
        }
        const auto& node = require_node(sample, terminal.front().first);
        const std::string contract = terminal_contract_signature(sample, node);
        if (!expected_contract.has_value()) expected_contract = contract;
        else if (*expected_contract != contract) {
            add_artifact_issue(
                preview.issues, sample.sample_id, "matrix_contract_mismatch",
                "Terminal VCF plugin/version/parameter contract differs across samples"
            );
            continue;
        }

        std::vector<BatchResultIssue> artifact_issues;
        const auto selected = collect_current_artifacts(
            sample, attempts_it->second, managed_files_, artifact_issues
        );
        preview.issues.insert(preview.issues.end(), artifact_issues.begin(), artifact_issues.end());
        const auto artifact_it = selected.find(terminal.front());
        if (artifact_it == selected.end()) {
            add_artifact_issue(preview.issues, sample.sample_id, "matrix_vcf_artifact_missing", "Terminal VCF artifact is not registered in the current result lineage");
            continue;
        }
        const auto artifact = resolve_artifact(managed_files_, artifact_it->second);
        if (!artifact.has_value()) throw std::runtime_error{"Batch matrix artifact disappeared from persistence"};
        const auto read = artifact_reader_.read_verified_text(*artifact, maximum_matrix_vcf_bytes);
        if (read.status != ResultArtifactReadStatus::verified || !read.text.has_value()) {
            add_artifact_issue(
                preview.issues, sample.sample_id, "matrix_vcf_unavailable",
                "Terminal VCF content is not verified: " + read_status_text(read.status)
            );
            continue;
        }
        preview.sources.push_back(BatchVariantMatrixSource{
            .sample_id = sample.sample_id,
            .artifact = artifact_it->second,
        });
    }

    preview.ready = preview.issues.empty() && !preview.sources.empty();
    return preview;
}

BatchVariantMatrixBuild BatchResultsService::build_variant_matrix(
    const std::string_view plan_id,
    const domain::ReferenceAssemblyIdentity& assembly,
    const domain::ReferenceGenome& reference
) {
    domain::validate_reference_assembly_identity(assembly);
    if (reference.contigs().assembly() != assembly.assembly) {
        throw std::invalid_argument{"Batch matrix reference assembly does not match requested assembly identity"};
    }
    auto preview = preview_variant_matrix(plan_id);
    if (!preview.ready) {
        const std::string reason = preview.issues.empty()
            ? "Batch has no compatible terminal VCF outputs"
            : preview.issues.front().message;
        throw std::invalid_argument{"Batch variant matrix is not ready: " + reason};
    }

    std::vector<domain::VcfIngestionResult> ingestions;
    ingestions.reserve(preview.sources.size());
    for (const auto& source : preview.sources) {
        const auto artifact = resolve_artifact(managed_files_, source.artifact);
        if (!artifact.has_value()) throw std::runtime_error{"Batch matrix artifact disappeared from persistence"};
        const auto read = artifact_reader_.read_verified_text(*artifact, maximum_matrix_vcf_bytes);
        if (read.status != ResultArtifactReadStatus::verified || !read.text.has_value()) {
            throw std::runtime_error{"Batch matrix VCF failed content verification"};
        }
        std::istringstream input{*read.text};
        auto ingestion = domain::ingest_vcf(input, reference);
        if (ingestion.header.sample_names.size() != 1U ||
            ingestion.header.sample_names.front() != source.sample_id) {
            throw std::invalid_argument{
                "Terminal VCF does not satisfy the single-sample v0.3 matrix input contract for sample '" +
                source.sample_id + "'"
            };
        }
        ingestions.push_back(std::move(ingestion));
    }

    std::vector<domain::MultiSampleMatrixSource> sources;
    sources.reserve(preview.sources.size());
    for (std::size_t index = 0U; index < preview.sources.size(); ++index) {
        sources.push_back(domain::MultiSampleMatrixSource{
            .source_id = preview.sources[index].artifact.managed_file_id,
            .assembly = assembly,
            .contigs = &reference.contigs(),
            .variants = &ingestions[index],
        });
    }

    auto matrix = domain::build_multi_sample_matrix(
        assembly, reference.contigs(), sources
    );
    return BatchVariantMatrixBuild{
        .preview = std::move(preview),
        .matrix = std::move(matrix),
    };
}

}  // namespace biocore::application
