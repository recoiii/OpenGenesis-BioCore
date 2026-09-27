#include "biocore/application/batch_plan.hpp"

#include <algorithm>

namespace biocore::application {

std::string_view to_string(const BatchInputFileRole role) noexcept {
    switch (role) {
        case BatchInputFileRole::primary: return "primary";
        case BatchInputFileRole::secondary: return "secondary";
        case BatchInputFileRole::reference: return "reference";
    }
    return "primary";
}

std::optional<BatchInputFileRole> batch_input_file_role_from_string(
    const std::string_view value
) noexcept {
    if (value == "primary") return BatchInputFileRole::primary;
    if (value == "secondary") return BatchInputFileRole::secondary;
    if (value == "reference") return BatchInputFileRole::reference;
    return std::nullopt;
}

std::string_view to_string(const BatchPlanInputSourceKind kind) noexcept {
    switch (kind) {
        case BatchPlanInputSourceKind::managed_file: return "managed_file";
        case BatchPlanInputSourceKind::node_output: return "node_output";
    }
    return "managed_file";
}

std::optional<BatchPlanInputSourceKind> batch_plan_input_source_kind_from_string(
    const std::string_view value
) noexcept {
    if (value == "managed_file") return BatchPlanInputSourceKind::managed_file;
    if (value == "node_output") return BatchPlanInputSourceKind::node_output;
    return std::nullopt;
}

std::string_view to_string(const BatchPlanParameterSource source) noexcept {
    switch (source) {
        case BatchPlanParameterSource::node_literal: return "node_literal";
        case BatchPlanParameterSource::workflow_parameter: return "workflow_parameter";
        case BatchPlanParameterSource::module_default: return "module_default";
    }
    return "node_literal";
}

std::optional<BatchPlanParameterSource> batch_plan_parameter_source_from_string(
    const std::string_view value
) noexcept {
    if (value == "node_literal") return BatchPlanParameterSource::node_literal;
    if (value == "workflow_parameter") return BatchPlanParameterSource::workflow_parameter;
    if (value == "module_default") return BatchPlanParameterSource::module_default;
    return std::nullopt;
}

bool BatchSamplePlanPreview::valid() const noexcept {
    return std::ranges::none_of(issues, [](const BatchPlanIssue& issue) {
        return issue.severity == BatchPlanIssueSeverity::blocker;
    });
}

bool BatchPlanPreview::globally_valid() const noexcept {
    return std::ranges::none_of(issues, [](const BatchPlanIssue& issue) {
        return issue.severity == BatchPlanIssueSeverity::blocker;
    });
}

std::string_view to_string(const BatchPlanSampleDisposition disposition) noexcept {
    switch (disposition) {
        case BatchPlanSampleDisposition::included: return "included";
        case BatchPlanSampleDisposition::excluded: return "excluded";
    }
    return "included";
}

std::optional<BatchPlanSampleDisposition>
batch_plan_sample_disposition_from_string(const std::string_view value) noexcept {
    if (value == "included") return BatchPlanSampleDisposition::included;
    if (value == "excluded") return BatchPlanSampleDisposition::excluded;
    return std::nullopt;
}

}  // namespace biocore::application
