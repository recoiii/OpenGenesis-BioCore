#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/workflow_binding_resolver.hpp"
#include "biocore/domain/workflow.hpp"

namespace {

using namespace biocore;

class Registry final : public application::IPluginRegistry {
public:
    std::optional<application::ResolvedPluginModule> find_module(
        const std::string_view id
    ) const override {
        if (id == "org.biocore.test.align") {
            return application::ResolvedPluginModule{
                .plugin_id = "org.biocore.test",
                .plugin_version = "1.0.0",
                .module_id = std::string{id},
                .plugin_root_path = {},
                .executable_path = {},
                .parameters = {
                    domain::PluginParameterDefinition{
                        "threads",
                        domain::PluginParameterType::integer,
                        true,
                        std::nullopt,
                        1.0,
                        64.0
                    },
                    domain::PluginParameterDefinition{
                        "mode",
                        domain::PluginParameterType::enumeration,
                        false,
                        domain::PluginParameterValue{std::string{"sensitive"}},
                        std::nullopt,
                        std::nullopt,
                        {"fast", "sensitive"}
                    },
                },
                .inputs = {
                    domain::PluginInputPortDefinition{"reads", true, {"fastq"}},
                    domain::PluginInputPortDefinition{"reference", true, {"fasta"}},
                },
                .outputs = {
                    domain::PluginOutputPortDefinition{"alignment", "bam"},
                },
            };
        }
        if (id == "org.biocore.test.qc") {
            return application::ResolvedPluginModule{
                .plugin_id = "org.biocore.test",
                .plugin_version = "1.0.0",
                .module_id = std::string{id},
                .plugin_root_path = {},
                .executable_path = {},
                .parameters = {
                    domain::PluginParameterDefinition{
                        "threshold",
                        domain::PluginParameterType::number,
                        false,
                        domain::PluginParameterValue{20.0},
                        0.0,
                        100.0
                    },
                },
                .inputs = {
                    domain::PluginInputPortDefinition{"alignment", true, {"bam", "cram"}},
                },
                .outputs = {
                    domain::PluginOutputPortDefinition{"report", "json"},
                },
            };
        }
        if (id == "org.biocore.test.caller") {
            return application::ResolvedPluginModule{
                .plugin_id = "org.biocore.test",
                .plugin_version = "1.0.0",
                .module_id = std::string{id},
                .plugin_root_path = {},
                .executable_path = {},
                .inputs = {
                    domain::PluginInputPortDefinition{"alignment", true, {"bam", "cram"}},
                    domain::PluginInputPortDefinition{"reference", true, {"fasta"}},
                },
                .outputs = {
                    domain::PluginOutputPortDefinition{"variants", "vcf"},
                },
            };
        }
        return std::nullopt;
    }

    std::vector<application::RegisteredPlugin> list_plugins() const override { return {}; }
};

[[nodiscard]] domain::WorkflowNode align_node(
    const std::string& version = "1.0.0",
    domain::WorkflowParameters parameters = {}
) {
    return domain::WorkflowNode{
        domain::WorkflowNodeId{"align"},
        "Alignment",
        "org.biocore.test.align",
        version,
        {
            domain::WorkflowInputDeclaration{"reads", "fastq", true},
            domain::WorkflowInputDeclaration{"reference", "fasta", true},
        },
        {domain::WorkflowOutputDeclaration{"alignment", "bam"}},
        std::move(parameters),
    };
}

[[nodiscard]] domain::WorkflowNode qc_node(
    std::string artifact_type = "bam",
    domain::WorkflowParameters parameters = {}
) {
    return domain::WorkflowNode{
        domain::WorkflowNodeId{"qc"},
        "QC",
        "org.biocore.test.qc",
        "1.0.0",
        {domain::WorkflowInputDeclaration{"alignment", std::move(artifact_type), true}},
        {domain::WorkflowOutputDeclaration{"report", "json"}},
        std::move(parameters),
    };
}

[[nodiscard]] domain::WorkflowNode caller_node() {
    return domain::WorkflowNode{
        domain::WorkflowNodeId{"caller"},
        "Caller",
        "org.biocore.test.caller",
        "1.0.0",
        {
            domain::WorkflowInputDeclaration{"alignment", "bam", true},
            domain::WorkflowInputDeclaration{"reference", "fasta", true},
        },
        {domain::WorkflowOutputDeclaration{"variants", "vcf"}},
        {},
    };
}

[[nodiscard]] domain::Workflow workflow(
    domain::WorkflowNode align = align_node(),
    domain::WorkflowNode qc = qc_node(),
    bool include_caller = true
) {
    std::vector<domain::WorkflowNode> nodes;
    nodes.push_back(std::move(align));
    nodes.push_back(std::move(qc));
    if (include_caller) nodes.push_back(caller_node());

    std::vector<domain::WorkflowEdge> edges{
        domain::WorkflowEdge{
            domain::WorkflowNodeId{"align"},
            "alignment",
            domain::WorkflowNodeId{"qc"},
            "alignment"
        },
    };
    if (include_caller) {
        edges.emplace_back(
            domain::WorkflowNodeId{"align"},
            "alignment",
            domain::WorkflowNodeId{"caller"},
            "alignment"
        );
    }

    return domain::Workflow{
        domain::Workflow::current_schema_version,
        domain::WorkflowId{"wf-binding"},
        "Binding test",
        "",
        std::move(nodes),
        std::move(edges),
    };
}

[[nodiscard]] application::WorkflowBindingRequest valid_request() {
    return application::WorkflowBindingRequest{
        .parameters = {
            application::WorkflowParameterBinding{
                "threads",
                domain::PluginParameterValue{std::int64_t{8}}
            },
            application::WorkflowParameterBinding{
                "qc-threshold",
                domain::PluginParameterValue{30.5}
            },
        },
        .resources = {
            application::WorkflowArtifactResource{"reads", "file-reads", "fastq"},
            application::WorkflowArtifactResource{"reference", "ref-grch38", "fasta"},
        },
        .parameter_references = {
            application::WorkflowNodeParameterReference{
                domain::WorkflowNodeId{"align"}, "threads", "threads"
            },
            application::WorkflowNodeParameterReference{
                domain::WorkflowNodeId{"qc"}, "threshold", "qc-threshold"
            },
        },
        .resource_references = {
            application::WorkflowNodeResourceReference{
                domain::WorkflowNodeId{"align"}, "reads", "reads"
            },
            application::WorkflowNodeResourceReference{
                domain::WorkflowNodeId{"align"}, "reference", "reference"
            },
            application::WorkflowNodeResourceReference{
                domain::WorkflowNodeId{"caller"}, "reference", "reference"
            },
        },
    };
}

[[nodiscard]] bool throws_code(
    const domain::Workflow& input_workflow,
    const application::WorkflowBindingRequest& request,
    const application::WorkflowBindingErrorCode expected
) {
    Registry registry;
    try {
        static_cast<void>(
            application::WorkflowBindingResolver::resolve(
                input_workflow, request, registry
            )
        );
    } catch (const application::WorkflowBindingError& error) {
        return error.code() == expected &&
               std::string_view{error.what()}.find("Workflow binding failed") !=
                   std::string_view::npos;
    }
    return false;
}

[[nodiscard]] const application::ResolvedWorkflowNodeBinding* find_node(
    const application::WorkflowBindingPlan& plan,
    const std::string_view id
) {
    const auto iterator = std::ranges::find_if(
        plan.nodes(),
        [id](const auto& node) { return node.node_id.value() == id; }
    );
    return iterator == plan.nodes().end() ? nullptr : &*iterator;
}

[[nodiscard]] bool valid_contract() {
    Registry registry;
    const auto plan = application::WorkflowBindingResolver::resolve(
        workflow(), valid_request(), registry
    );
    if (plan.workflow_id().value() != "wf-binding" || plan.nodes().size() != 3U) {
        return false;
    }

    const auto* align = find_node(plan, "align");
    const auto* qc = find_node(plan, "qc");
    const auto* caller = find_node(plan, "caller");
    if (align == nullptr || qc == nullptr || caller == nullptr) return false;

    return align->parameters.size() == 2U &&
           align->parameters[0].name == "mode" &&
           align->parameters[0].source ==
               application::ResolvedWorkflowParameterSource::module_default &&
           align->parameters[1].name == "threads" &&
           align->parameters[1].value == "8" &&
           qc->parameters.size() == 1U &&
           qc->parameters[0].value == "30.5" &&
           qc->inputs.size() == 1U &&
           qc->inputs[0].source_kind ==
               application::ResolvedWorkflowInputSourceKind::node_output &&
           qc->inputs[0].source_id == "align" &&
           qc->inputs[0].artifact_type == "bam" &&
           caller->outputs.size() == 1U &&
           caller->outputs[0].artifact_type == "vcf";
}

[[nodiscard]] bool deterministic_contract() {
    Registry registry;
    auto first_request = valid_request();
    auto second_request = valid_request();
    std::reverse(second_request.parameters.begin(), second_request.parameters.end());
    std::reverse(second_request.resources.begin(), second_request.resources.end());
    std::reverse(
        second_request.parameter_references.begin(),
        second_request.parameter_references.end()
    );
    std::reverse(
        second_request.resource_references.begin(),
        second_request.resource_references.end()
    );

    const auto first = application::WorkflowBindingResolver::resolve(
        workflow(), first_request, registry
    );
    const auto second = application::WorkflowBindingResolver::resolve(
        workflow(), second_request, registry
    );

    if (first.nodes().size() != second.nodes().size()) return false;
    for (std::size_t index = 0U; index < first.nodes().size(); ++index) {
        const auto& left = first.nodes()[index];
        const auto& right = second.nodes()[index];
        if (left.node_id != right.node_id ||
            left.parameters.size() != right.parameters.size() ||
            left.inputs.size() != right.inputs.size() ||
            left.outputs.size() != right.outputs.size()) {
            return false;
        }
        for (std::size_t parameter = 0U; parameter < left.parameters.size(); ++parameter) {
            if (left.parameters[parameter].name != right.parameters[parameter].name ||
                left.parameters[parameter].value != right.parameters[parameter].value ||
                left.parameters[parameter].source != right.parameters[parameter].source) {
                return false;
            }
        }
        for (std::size_t input = 0U; input < left.inputs.size(); ++input) {
            if (left.inputs[input].port_name != right.inputs[input].port_name ||
                left.inputs[input].artifact_type != right.inputs[input].artifact_type ||
                left.inputs[input].source_kind != right.inputs[input].source_kind ||
                left.inputs[input].source_id != right.inputs[input].source_id ||
                left.inputs[input].source_port != right.inputs[input].source_port) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool module_contract() {
    Registry registry;
    auto bad_input = workflow(
        domain::WorkflowNode{
            domain::WorkflowNodeId{"align"},
            "Alignment",
            "org.biocore.test.align",
            "1.0.0",
            {
                domain::WorkflowInputDeclaration{"reads", "vcf", true},
                domain::WorkflowInputDeclaration{"reference", "fasta", true},
            },
            {domain::WorkflowOutputDeclaration{"alignment", "bam"}}
        }
    );
    return throws_code(
               bad_input,
               valid_request(),
               application::WorkflowBindingErrorCode::incompatible_declared_input_type
           ) &&
           throws_code(
               workflow(
                   domain::WorkflowNode{
                       domain::WorkflowNodeId{"align"},
                       "Alignment",
                       "org.biocore.test.align",
                       "1.0.0",
                       {domain::WorkflowInputDeclaration{"reads", "fastq", true}},
                       {domain::WorkflowOutputDeclaration{"alignment", "bam"}}
                   }
               ),
               valid_request(),
               application::WorkflowBindingErrorCode::required_input_declaration_missing
           );
}

[[nodiscard]] bool edge_type_contract() {
    auto input_workflow = workflow(align_node(), qc_node("cram"), false);
    auto request = valid_request();
    request.resource_references.erase(
        std::remove_if(
            request.resource_references.begin(),
            request.resource_references.end(),
            [](const auto& item) { return item.node_id.value() == "caller"; }
        ),
        request.resource_references.end()
    );
    return throws_code(
        input_workflow,
        request,
        application::WorkflowBindingErrorCode::incompatible_artifact_type
    );
}

[[nodiscard]] bool parameter_reference_contract() {
    Registry registry;
    auto input_workflow = workflow(
        align_node("1.0.0", {{"mode", "fast"}}),
        qc_node(),
        false
    );
    auto request = valid_request();
    request.resource_references.erase(
        std::remove_if(
            request.resource_references.begin(),
            request.resource_references.end(),
            [](const auto& item) { return item.node_id.value() == "caller"; }
        ),
        request.resource_references.end()
    );
    const auto plan = application::WorkflowBindingResolver::resolve(
        input_workflow, request, registry
    );
    const auto* align = find_node(plan, "align");
    return align != nullptr && align->parameters.size() == 2U &&
           align->parameters[0].name == "mode" &&
           align->parameters[0].value == "fast" &&
           align->parameters[0].source ==
               application::ResolvedWorkflowParameterSource::node_literal &&
           align->parameters[1].value == "8";
}

[[nodiscard]] bool parameter_invalid_contract() {
    auto request = valid_request();
    request.parameters[0].value = domain::PluginParameterValue{std::int64_t{128}};
    if (!throws_code(
            workflow(),
            request,
            application::WorkflowBindingErrorCode::invalid_parameter_value
        )) {
        return false;
    }

    auto conflict_workflow = workflow(
        align_node("1.0.0", {{"threads", "4"}}),
        qc_node()
    );
    return throws_code(
        conflict_workflow,
        valid_request(),
        application::WorkflowBindingErrorCode::parameter_source_conflict
    );
}

[[nodiscard]] bool required_parameter_contract() {
    auto request = valid_request();
    request.parameter_references.erase(
        std::remove_if(
            request.parameter_references.begin(),
            request.parameter_references.end(),
            [](const auto& item) {
                return item.node_id.value() == "align" &&
                       item.parameter_name == "threads";
            }
        ),
        request.parameter_references.end()
    );
    return throws_code(
        workflow(),
        request,
        application::WorkflowBindingErrorCode::required_parameter_missing
    );
}

[[nodiscard]] bool resource_propagation_contract() {
    Registry registry;
    const auto plan = application::WorkflowBindingResolver::resolve(
        workflow(), valid_request(), registry
    );
    const auto* align = find_node(plan, "align");
    const auto* caller = find_node(plan, "caller");
    if (align == nullptr || caller == nullptr) return false;

    const auto align_ref = std::ranges::find_if(
        align->inputs,
        [](const auto& input) { return input.port_name == "reference"; }
    );
    const auto caller_ref = std::ranges::find_if(
        caller->inputs,
        [](const auto& input) { return input.port_name == "reference"; }
    );
    return align_ref != align->inputs.end() &&
           caller_ref != caller->inputs.end() &&
           align_ref->source_id == "ref-grch38" &&
           caller_ref->source_id == "ref-grch38" &&
           align_ref->source_kind ==
               application::ResolvedWorkflowInputSourceKind::workflow_resource &&
           caller_ref->source_kind ==
               application::ResolvedWorkflowInputSourceKind::workflow_resource;
}

[[nodiscard]] bool resource_invalid_contract() {
    auto request = valid_request();
    request.resources[1].artifact_type = "vcf";
    return throws_code(
        workflow(),
        request,
        application::WorkflowBindingErrorCode::incompatible_artifact_type
    );
}

[[nodiscard]] bool required_input_contract() {
    auto request = valid_request();
    request.resource_references.erase(
        std::remove_if(
            request.resource_references.begin(),
            request.resource_references.end(),
            [](const auto& item) {
                return item.node_id.value() == "align" && item.input_port == "reads";
            }
        ),
        request.resource_references.end()
    );
    return throws_code(
        workflow(),
        request,
        application::WorkflowBindingErrorCode::required_input_missing
    );
}

[[nodiscard]] bool binding_conflict_contract() {
    auto request = valid_request();
    request.resources.push_back(
        application::WorkflowArtifactResource{"alignment", "external-bam", "bam"}
    );
    request.resource_references.push_back(
        application::WorkflowNodeResourceReference{
            domain::WorkflowNodeId{"qc"}, "alignment", "alignment"
        }
    );
    return throws_code(
        workflow(),
        request,
        application::WorkflowBindingErrorCode::input_source_conflict
    );
}

[[nodiscard]] bool version_contract() {
    auto request = valid_request();
    return throws_code(
        workflow(align_node("2.0.0")),
        request,
        application::WorkflowBindingErrorCode::plugin_version_mismatch
    );
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: biocore-workflow-binding-resolver-tests <case>\n";
        return EXIT_FAILURE;
    }

    const std::string_view test_case{argv[1]};
    bool passed = false;
    if (test_case == "valid") passed = valid_contract();
    else if (test_case == "deterministic") passed = deterministic_contract();
    else if (test_case == "module-contract") passed = module_contract();
    else if (test_case == "edge-type") passed = edge_type_contract();
    else if (test_case == "parameter-reference") passed = parameter_reference_contract();
    else if (test_case == "parameter-invalid") passed = parameter_invalid_contract();
    else if (test_case == "parameter-required") passed = required_parameter_contract();
    else if (test_case == "resource-propagation") passed = resource_propagation_contract();
    else if (test_case == "resource-invalid") passed = resource_invalid_contract();
    else if (test_case == "required-input") passed = required_input_contract();
    else if (test_case == "binding-conflict") passed = binding_conflict_contract();
    else if (test_case == "version") passed = version_contract();
    else {
        std::cerr << "Unknown workflow binding test case\n";
        return EXIT_FAILURE;
    }

    if (!passed) {
        std::cerr << "Workflow binding test failed: " << test_case << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Workflow binding test passed: " << test_case << '\n';
    return EXIT_SUCCESS;
}
