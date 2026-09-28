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
