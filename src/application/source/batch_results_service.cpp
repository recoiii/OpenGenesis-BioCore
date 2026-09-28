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
