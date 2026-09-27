#include "biocore/application/batch_planning_service.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "biocore/application/i_batch_plan_store.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_plugin_registry.hpp"
#include "biocore/application/i_project_sample_binding_store.hpp"
#include "biocore/application/i_project_sample_store.hpp"
#include "biocore/application/i_workflow_template_catalog.hpp"
#include "biocore/application/sample_binding_service.hpp"
#include "biocore/application/workflow_binding_resolver.hpp"
#include "biocore/domain/component_identity.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/project_sample_binding.hpp"

namespace biocore::application {
namespace {

using RoleFileMap = std::map<BatchInputFileRole, domain::ManagedFile>;

[[nodiscard]] bool blank(const std::string_view value) noexcept {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
        return std::isspace(static_cast<unsigned char>(character)) != 0;
    });
}

void validate_request(const BatchPlanRequest& request) {
    if (!domain::is_namespaced_identifier(request.plan_id, 128U)) {
        throw std::invalid_argument{"Batch plan id is invalid"};
    }
    if (blank(request.project_id) || request.project_id.size() > 128U ||
        request.project_id.find('\0') != std::string::npos) {
        throw std::invalid_argument{"Batch project id is invalid"};
    }
    if (request.sample_ids.empty() ||
        request.sample_ids.size() > BatchPlanningService::maximum_selected_samples) {
        throw std::invalid_argument{"Batch sample selection size is invalid"};
    }

    std::set<std::string, std::less<>> samples;
    for (const std::string& sample_id : request.sample_ids) {
        if (blank(sample_id) || sample_id.size() > 128U ||
            sample_id.find('\0') != std::string::npos) {
            throw std::invalid_argument{"Batch sample id is invalid"};
        }
        if (!samples.emplace(sample_id).second) {
            throw std::invalid_argument{"Batch sample selection contains duplicates"};
        }
    }

    std::set<std::pair<std::string, std::string>> assignments;
    for (const BatchWorkflowInputAssignment& assignment : request.input_assignments) {
        const std::pair<std::string, std::string> key{
            std::string{assignment.node_id.value()}, assignment.input_port
        };
        if (!assignments.emplace(key).second) {
            throw std::invalid_argument{"Batch workflow input assignment is duplicated"};
        }
    }
}

void add_issue(
    BatchSamplePlanPreview& sample,
    const BatchPlanIssueSeverity severity,
    std::string code,
    std::string message
) {
    sample.issues.push_back(BatchPlanIssue{
        severity, std::move(code), std::move(message)
    });
}

[[nodiscard]] std::optional<std::string> role_file_id(
    const domain::ProjectSampleBinding& binding,
    const BatchInputFileRole role
) {
    switch (role) {
        case BatchInputFileRole::primary:
            return std::string{binding.primary_file_id()};
        case BatchInputFileRole::secondary:
            return binding.secondary_file_id();
        case BatchInputFileRole::reference:
            return binding.reference_file_id();
    }
    return std::nullopt;
}

[[nodiscard]] bool has_lowercase_sha256(const domain::ManagedFile& file) noexcept {
    if (!file.checksum_algorithm().has_value() ||
        !file.checksum_value().has_value() ||
        *file.checksum_algorithm() != "sha256" ||
        file.checksum_value()->size() != 64U) {
        return false;
    }
    return std::ranges::all_of(*file.checksum_value(), [](const char value) {
        return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
    });
}

[[nodiscard]] std::string workflow_id_for(
    const std::string_view plan_id,
    const std::size_t ordinal
) {
    return std::string{plan_id} + ".sample.s" + std::to_string(ordinal + 1U);
}

[[nodiscard]] BatchPlanParameterSource map_parameter_source(
    const ResolvedWorkflowParameterSource source
) noexcept {
    switch (source) {
        case ResolvedWorkflowParameterSource::node_literal:
            return BatchPlanParameterSource::node_literal;
        case ResolvedWorkflowParameterSource::workflow_parameter:
            return BatchPlanParameterSource::workflow_parameter;
        case ResolvedWorkflowParameterSource::module_default:
            return BatchPlanParameterSource::module_default;
    }
    return BatchPlanParameterSource::node_literal;
}

[[nodiscard]] BatchPlanManagedFileSnapshot file_snapshot(
    const BatchInputFileRole role,
    const domain::ManagedFile& file
) {
    if (!has_lowercase_sha256(file)) {
        throw std::logic_error{
            "A run-ready batch input does not have a lowercase SHA-256 identity"
        };
    }
    return BatchPlanManagedFileSnapshot{
        .role = role,
        .file_id = std::string{file.id()},
        .file_type = std::string{file.file_type()},
        .size_bytes = file.size_bytes(),
        .sha256 = *file.checksum_value(),
    };
}

[[nodiscard]] std::optional<BatchInputFileRole> role_for_file(
    const RoleFileMap& files,
    const std::string_view file_id
) noexcept {
    for (const auto& [role, file] : files) {
        if (file.id() == file_id) return role;
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<BatchPlanNodeSnapshot> snapshot_nodes(
    const WorkflowBindingPlan& plan,
    const RoleFileMap& role_files
) {
    std::vector<BatchPlanNodeSnapshot> result;
    result.reserve(plan.nodes().size());

    for (const ResolvedWorkflowNodeBinding& node : plan.nodes()) {
        BatchPlanNodeSnapshot snapshot{
            .node_id = std::string{node.node_id.value()},
            .module_id = node.module_id,
            .plugin_version = node.plugin_version,
            .parameters = {},
            .inputs = {},
            .outputs = {},
        };

        snapshot.parameters.reserve(node.parameters.size());
        for (const ResolvedWorkflowParameter& parameter : node.parameters) {
            snapshot.parameters.push_back(BatchPlanParameterSnapshot{
                .name = parameter.name,
                .type = parameter.type,
                .value = parameter.value,
                .source = map_parameter_source(parameter.source),
            });
        }

        snapshot.inputs.reserve(node.inputs.size());
        for (const ResolvedWorkflowInputBinding& input : node.inputs) {
            BatchPlanInputSnapshot captured{
                .port_name = input.port_name,
                .artifact_type = input.artifact_type,
                .source_kind = input.source_kind ==
                        ResolvedWorkflowInputSourceKind::workflow_resource
                    ? BatchPlanInputSourceKind::managed_file
                    : BatchPlanInputSourceKind::node_output,
                .source_id = input.source_id,
                .source_port = input.source_port,
                .managed_file = std::nullopt,
            };

            if (input.source_kind ==
                ResolvedWorkflowInputSourceKind::workflow_resource) {
                const auto role = role_for_file(role_files, input.source_id);
                if (!role.has_value()) {
                    throw std::logic_error{
                        "Resolved workflow resource is absent from the batch role map"
                    };
                }
                captured.managed_file = file_snapshot(
                    *role, role_files.at(*role)
                );
            }
            snapshot.inputs.push_back(std::move(captured));
        }

        snapshot.outputs.reserve(node.outputs.size());
        for (const ResolvedWorkflowOutputBinding& output : node.outputs) {
            snapshot.outputs.push_back(BatchPlanOutputSnapshot{
                .port_name = output.port_name,
                .artifact_type = output.artifact_type,
            });
        }
        result.push_back(std::move(snapshot));
    }
    return result;
}

void require_preview_identity(const BatchPlanPreview& preview) {
    if (!domain::is_namespaced_identifier(preview.plan_id, 128U) ||
        blank(preview.project_id) || preview.project_id.size() > 128U ||
        preview.project_id.find('\0') != std::string::npos ||
        blank(preview.template_id) || blank(preview.template_version) ||
        preview.samples.empty()) {
        throw std::invalid_argument{"Batch preview identity is invalid"};
    }
}

[[nodiscard]] bool current_snapshot_matches(
    IManagedFileRepository& managed_files,
    const domain::ProjectSampleBinding& binding,
    const BatchPlanManagedFileSnapshot& expected
) {
    const auto expected_file_id = role_file_id(binding, expected.role);
    if (!expected_file_id.has_value() || *expected_file_id != expected.file_id) {
        return false;
    }

    const auto current = managed_files.find_by_id(expected.file_id);
    return current.has_value() &&
           current->file_type() == expected.file_type &&
           current->size_bytes() == expected.size_bytes &&
           current->checksum_algorithm() == std::optional<std::string>{"sha256"} &&
           current->checksum_value() == std::optional<std::string>{expected.sha256};
}

}  // namespace

BatchPlanningService::BatchPlanningService(
    IProjectSampleStore& samples,
    IProjectSampleBindingStore& bindings,
    IManagedFileRepository& managed_files,
    SampleBindingService& binding_validation,
    const IWorkflowTemplateCatalog& templates,
    const IPluginRegistry& plugins,
    IBatchPlanStore& plans
) noexcept
    : samples_{samples},
      bindings_{bindings},
      managed_files_{managed_files},
      binding_validation_{binding_validation},
      templates_{templates},
      plugins_{plugins},
      plans_{plans} {}

BatchPlanPreview BatchPlanningService::preview(const BatchPlanRequest& request) {
    validate_request(request);

    BatchPlanPreview result{
        .plan_id = request.plan_id,
        .project_id = request.project_id,
        .template_id = request.template_id,
        .template_version = request.template_version,
        .issues = {},
        .samples = {},
    };

    std::vector<std::string> sample_ids = request.sample_ids;
    std::ranges::sort(sample_ids);

    const auto project_samples = samples_.list(request.project_id);
    if (!project_samples.has_value()) {
        result.issues.push_back({
            BatchPlanIssueSeverity::blocker,
            "project_not_found",
            "Batch plan project does not exist"
        });
    }

    std::set<std::string, std::less<>> known_samples;
    if (project_samples.has_value()) {
        for (const auto& sample : *project_samples) {
            known_samples.emplace(sample.sample_id());
        }
    }

    std::optional<InstantiatedWorkflowTemplate> instantiated;
    try {
        WorkflowTemplateInstantiationService instantiation{templates_};
        instantiated = instantiation.instantiate({
            .template_id = request.template_id,
            .template_version = request.template_version,
            .workflow_id = domain::WorkflowId{request.plan_id + ".preview"},
            .name = std::nullopt,
            .description = std::nullopt,
            .parameter_overrides = request.parameter_overrides,
        });
    } catch (const WorkflowTemplateInstantiationError& error) {
        result.issues.push_back({
            BatchPlanIssueSeverity::blocker,
            "template_" + std::string{to_string(error.code())},
            error.what()
        });
    }

    std::vector<BatchWorkflowInputAssignment> assignments =
        request.input_assignments;
    std::ranges::sort(
        assignments,
        [](const auto& left, const auto& right) {
            if (left.node_id.value() != right.node_id.value()) {
                return left.node_id.value() < right.node_id.value();
            }
            return left.input_port < right.input_port;
        }
    );

    result.samples.reserve(sample_ids.size());
    for (std::size_t ordinal = 0U; ordinal < sample_ids.size(); ++ordinal) {
        BatchSamplePlanPreview sample{
            .sample_id = sample_ids[ordinal],
            .workflow_id = workflow_id_for(request.plan_id, ordinal),
            .issues = {},
            .nodes = {},
        };

        if (!known_samples.contains(sample.sample_id)) {
            add_issue(
                sample,
                BatchPlanIssueSeverity::blocker,
                "sample_not_found",
                "Selected sample does not exist in the project"
            );
            result.samples.push_back(std::move(sample));
            continue;
        }

        const auto binding =
            bindings_.find(request.project_id, sample.sample_id);
        if (!binding.has_value()) {
            add_issue(
                sample,
                BatchPlanIssueSeverity::blocker,
                "sample_binding_missing",
                "Selected sample has no input/reference binding"
            );
            result.samples.push_back(std::move(sample));
            continue;
        }

        const SampleBindingPreview binding_preview =
            binding_validation_.preview(*binding);
        for (const SampleBindingIssue& issue : binding_preview.issues) {
            add_issue(
                sample,
                issue.severity == SampleBindingIssueSeverity::blocker
                    ? BatchPlanIssueSeverity::blocker
                    : BatchPlanIssueSeverity::warning,
                "binding_" + issue.code,
                issue.message
            );
        }
        if (!binding_preview.valid()) {
            result.samples.push_back(std::move(sample));
            continue;
        }

        if (!instantiated.has_value()) {
            add_issue(
                sample,
                BatchPlanIssueSeverity::blocker,
                "template_unavailable",
                "Workflow template could not be instantiated"
            );
            result.samples.push_back(std::move(sample));
            continue;
        }

        RoleFileMap role_files;
        WorkflowBindingRequest binding_request;

        std::set<BatchInputFileRole> required_roles;
        for (const auto& assignment : assignments) {
            required_roles.emplace(assignment.role);
        }

        bool role_failure = false;
        for (const BatchInputFileRole role : required_roles) {
            const auto file_id = role_file_id(*binding, role);
            if (!file_id.has_value()) {
                add_issue(
                    sample,
                    BatchPlanIssueSeverity::blocker,
                    "input_role_unavailable",
                    "Template input assignment requires unavailable sample role '" +
                        std::string{to_string(role)} + "'"
                );
                role_failure = true;
                continue;
            }
            const auto file = managed_files_.find_by_id(*file_id);
            if (!file.has_value() || !has_lowercase_sha256(*file)) {
                add_issue(
                    sample,
                    BatchPlanIssueSeverity::blocker,
                    "input_identity_unavailable",
                    "Run-ready sample input has no immutable managed-file SHA-256 identity"
                );
                role_failure = true;
                continue;
            }
            role_files.emplace(role, *file);
            binding_request.resources.push_back(WorkflowArtifactResource{
                .name = std::string{to_string(role)},
                .resource_id = std::string{file->id()},
                .artifact_type = std::string{file->file_type()},
            });
        }

        if (role_failure) {
            result.samples.push_back(std::move(sample));
            continue;
        }

        for (const auto& assignment : assignments) {
            binding_request.resource_references.push_back(
                WorkflowNodeResourceReference{
                    .node_id = assignment.node_id,
                    .input_port = assignment.input_port,
                    .resource_name = std::string{to_string(assignment.role)},
                }
            );
        }

        try {
            const WorkflowBindingPlan plan = WorkflowBindingResolver::resolve(
                instantiated->workflow, binding_request, plugins_
            );
            sample.nodes = snapshot_nodes(plan, role_files);
        } catch (const WorkflowBindingError& error) {
            add_issue(
                sample,
                BatchPlanIssueSeverity::blocker,
                "workflow_" + std::string{to_string(error.code())},
                error.what()
            );
        }

        result.samples.push_back(std::move(sample));
    }

    return result;
}

ApprovedBatchPlan BatchPlanningService::approve(
    const BatchPlanPreview& preview,
    const std::vector<std::string>& excluded_sample_ids,
    const std::string_view approved_at_utc
) {
    require_preview_identity(preview);
    if (blank(approved_at_utc) || approved_at_utc.size() > 200U ||
        approved_at_utc.find('\0') != std::string_view::npos) {
        throw std::invalid_argument{"Batch plan approval timestamp is invalid"};
    }
    if (!preview.globally_valid()) {
        throw std::invalid_argument{"Batch plan has global blocking findings"};
    }

    std::set<std::string, std::less<>> known_samples;
    for (const auto& sample : preview.samples) {
        if (!known_samples.emplace(sample.sample_id).second) {
            throw std::invalid_argument{"Batch preview contains duplicate samples"};
        }
    }

    std::set<std::string, std::less<>> excluded;
    for (const std::string& sample_id : excluded_sample_ids) {
        if (!known_samples.contains(sample_id)) {
            throw std::invalid_argument{
                "Batch exclusion references a sample outside the preview"
            };
        }
        if (!excluded.emplace(sample_id).second) {
            throw std::invalid_argument{"Batch exclusion list contains duplicates"};
        }
    }

    ApprovedBatchPlan approved{
        .plan_id = preview.plan_id,
        .project_id = preview.project_id,
        .template_id = preview.template_id,
        .template_version = preview.template_version,
        .approved_at_utc = std::string{approved_at_utc},
        .samples = {},
    };
    approved.samples.reserve(preview.samples.size());

    std::size_t included_count = 0U;
    for (const BatchSamplePlanPreview& sample : preview.samples) {
        const bool is_excluded = excluded.contains(sample.sample_id);
        if (!sample.valid() && !is_excluded) {
            throw std::invalid_argument{
                "Every invalid sample must be explicitly excluded before batch approval"
            };
        }

        if (is_excluded) {
            approved.samples.push_back(ApprovedBatchSamplePlan{
                .sample_id = sample.sample_id,
                .disposition = BatchPlanSampleDisposition::excluded,
                .workflow_id = std::nullopt,
                .nodes = {},
            });
            continue;
        }

        if (sample.nodes.empty() || sample.workflow_id.empty()) {
            throw std::invalid_argument{
                "Included sample preview does not contain a complete run plan"
            };
        }

        const auto binding =
            bindings_.find(preview.project_id, sample.sample_id);
        if (!binding.has_value()) {
            throw std::runtime_error{
                "Batch preview is stale: included sample binding disappeared"
            };
        }

        const SampleBindingPreview current =
            binding_validation_.preview(*binding);
        if (!current.valid()) {
            throw std::runtime_error{
                "Batch preview is stale: included sample binding is no longer valid"
            };
        }

        for (const BatchPlanNodeSnapshot& node : sample.nodes) {
            for (const BatchPlanInputSnapshot& input : node.inputs) {
                if (input.source_kind != BatchPlanInputSourceKind::managed_file) {
                    continue;
                }
                if (!input.managed_file.has_value() ||
                    !current_snapshot_matches(
                        managed_files_, *binding, *input.managed_file
                    )) {
                    throw std::runtime_error{
                        "Batch preview is stale: managed input identity changed"
                    };
                }
            }
        }

        ++included_count;
        approved.samples.push_back(ApprovedBatchSamplePlan{
            .sample_id = sample.sample_id,
            .disposition = BatchPlanSampleDisposition::included,
            .workflow_id = sample.workflow_id,
            .nodes = sample.nodes,
        });
    }

    if (included_count == 0U) {
        throw std::invalid_argument{
            "An approved batch plan must include at least one valid sample"
        };
    }

    if (!plans_.add(approved)) {
        throw std::runtime_error{
            "Batch plan id already identifies an immutable approved plan"
        };
    }
    return approved;
}

}  // namespace biocore::application
