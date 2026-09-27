#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/domain/plugin_io_contract.hpp"

namespace biocore::application {

enum class BatchInputFileRole {
    primary,
    secondary,
    reference
};

[[nodiscard]] std::string_view to_string(BatchInputFileRole role) noexcept;
[[nodiscard]] std::optional<BatchInputFileRole> batch_input_file_role_from_string(
    std::string_view value
) noexcept;

enum class BatchPlanIssueSeverity {
    blocker,
    warning
};

struct BatchPlanIssue final {
    BatchPlanIssueSeverity severity{BatchPlanIssueSeverity::blocker};
    std::string code;
    std::string message;

    friend bool operator==(const BatchPlanIssue&, const BatchPlanIssue&) = default;
};

enum class BatchPlanInputSourceKind {
    managed_file,
    node_output
};

[[nodiscard]] std::string_view to_string(BatchPlanInputSourceKind kind) noexcept;
[[nodiscard]] std::optional<BatchPlanInputSourceKind> batch_plan_input_source_kind_from_string(
    std::string_view value
) noexcept;

enum class BatchPlanParameterSource {
    node_literal,
    workflow_parameter,
    module_default
};

[[nodiscard]] std::string_view to_string(BatchPlanParameterSource source) noexcept;
[[nodiscard]] std::optional<BatchPlanParameterSource> batch_plan_parameter_source_from_string(
    std::string_view value
) noexcept;

struct BatchPlanManagedFileSnapshot final {
    BatchInputFileRole role{BatchInputFileRole::primary};
    std::string file_id;
    std::string file_type;
    std::int64_t size_bytes{0};
    std::string sha256;

    friend bool operator==(
        const BatchPlanManagedFileSnapshot&,
        const BatchPlanManagedFileSnapshot&
    ) = default;
};

struct BatchPlanInputSnapshot final {
    std::string port_name;
    std::string artifact_type;
    BatchPlanInputSourceKind source_kind{BatchPlanInputSourceKind::managed_file};
    std::string source_id;
    std::string source_port;
    std::optional<BatchPlanManagedFileSnapshot> managed_file;

    friend bool operator==(const BatchPlanInputSnapshot&, const BatchPlanInputSnapshot&) = default;
};

struct BatchPlanParameterSnapshot final {
    std::string name;
    domain::PluginParameterType type{domain::PluginParameterType::string};
    std::string value;
    BatchPlanParameterSource source{BatchPlanParameterSource::node_literal};

    friend bool operator==(
        const BatchPlanParameterSnapshot&,
        const BatchPlanParameterSnapshot&
    ) = default;
};

struct BatchPlanOutputSnapshot final {
    std::string port_name;
    std::string artifact_type;

    friend bool operator==(
        const BatchPlanOutputSnapshot&,
        const BatchPlanOutputSnapshot&
    ) = default;
};

struct BatchPlanNodeSnapshot final {
    std::string node_id;
    std::string module_id;
    std::string plugin_version;
    std::vector<BatchPlanParameterSnapshot> parameters;
    std::vector<BatchPlanInputSnapshot> inputs;
    std::vector<BatchPlanOutputSnapshot> outputs;

    friend bool operator==(
        const BatchPlanNodeSnapshot&,
        const BatchPlanNodeSnapshot&
    ) = default;
};

struct BatchSamplePlanPreview final {
    std::string sample_id;
    std::string workflow_id;
    std::vector<BatchPlanIssue> issues;
    std::vector<BatchPlanNodeSnapshot> nodes;

    [[nodiscard]] bool valid() const noexcept;

    friend bool operator==(
        const BatchSamplePlanPreview&,
        const BatchSamplePlanPreview&
    ) = default;
};

struct BatchPlanPreview final {
    std::string plan_id;
    std::string project_id;
    std::string template_id;
    std::string template_version;
    std::vector<BatchPlanIssue> issues;
    std::vector<BatchSamplePlanPreview> samples;

    [[nodiscard]] bool globally_valid() const noexcept;

    friend bool operator==(
        const BatchPlanPreview&,
        const BatchPlanPreview&
    ) = default;
};

enum class BatchPlanSampleDisposition {
    included,
    excluded
};

[[nodiscard]] std::string_view to_string(BatchPlanSampleDisposition disposition) noexcept;
[[nodiscard]] std::optional<BatchPlanSampleDisposition>
batch_plan_sample_disposition_from_string(std::string_view value) noexcept;

struct ApprovedBatchSamplePlan final {
    std::string sample_id;
    BatchPlanSampleDisposition disposition{BatchPlanSampleDisposition::included};
    std::optional<std::string> workflow_id;
    std::vector<BatchPlanNodeSnapshot> nodes;

    friend bool operator==(
        const ApprovedBatchSamplePlan&,
        const ApprovedBatchSamplePlan&
    ) = default;
};

struct ApprovedBatchPlan final {
    std::string plan_id;
    std::string project_id;
    std::string template_id;
    std::string template_version;
    std::string approved_at_utc;
    std::vector<ApprovedBatchSamplePlan> samples;

    friend bool operator==(const ApprovedBatchPlan&, const ApprovedBatchPlan&) = default;
};

}  // namespace biocore::application
