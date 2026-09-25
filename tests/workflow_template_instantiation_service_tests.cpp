#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/i_workflow_template_catalog.hpp"
#include "biocore/application/workflow_template_instantiation_service.hpp"
#include "biocore/domain/workflow.hpp"
#include "biocore/domain/workflow_template.hpp"

namespace {

using namespace biocore;

class Catalog final : public application::IWorkflowTemplateCatalog {
public:
    std::optional<domain::WorkflowTemplate> find(
        const std::string_view id,
        const std::string_view version
    ) const override {
        if (id != "org.biocore.template.demo") return std::nullopt;
        if (version != "1.0.0" && version != "2.0.0") return std::nullopt;

        return domain::WorkflowTemplate{
            1U,
            "org.biocore.template.demo",
            std::string{version},
            version == "1.0.0" ? "Demo v1" : "Demo v2",
            "Reusable",
            domain::Workflow{
                1U,
                domain::WorkflowId{"org.biocore.template.demo"},
                "Blueprint",
                "",
                {
                    domain::WorkflowNode{
                        domain::WorkflowNodeId{"source"},
                        "Source",
                        "org.biocore.test.source",
                        version == "1.0.0" ? "1.0.0" : "2.0.0",
                        {},
                        {domain::WorkflowOutputDeclaration{"result", "txt"}},
                        {{"mode", "fast"}}
                    },
                    domain::WorkflowNode{
                        domain::WorkflowNodeId{"target"},
                        "Target",
                        "org.biocore.test.target",
                        "1.0.0",
                        {domain::WorkflowInputDeclaration{"input", "txt", true}},
                        {domain::WorkflowOutputDeclaration{"report", "json"}},
                        {}
                    },
                },
                {
                    domain::WorkflowEdge{
                        domain::WorkflowNodeId{"source"}, "result",
                        domain::WorkflowNodeId{"target"}, "input"
                    }
                }
            }
        };
    }

    std::vector<application::RegisteredWorkflowTemplate> list() const override {
        return {
            {
                "org.biocore.template.demo",
                "1.0.0",
                "Demo v1",
                "Reusable"
            },
            {
                "org.biocore.template.demo",
                "2.0.0",
                "Demo v2",
                "Reusable"
            },
        };
    }
};

[[nodiscard]] bool exact_version_contract() {
    Catalog catalog;
    application::WorkflowTemplateInstantiationService service{catalog};

    const auto result = service.instantiate({
        .template_id = "org.biocore.template.demo",
        .template_version = "2.0.0",
        .workflow_id = domain::WorkflowId{"org.biocore.run.case-001"},
        .name = std::nullopt,
        .description = std::nullopt,
        .parameter_overrides = {},
    });

    return result.template_id == "org.biocore.template.demo" &&
           result.template_version == "2.0.0" &&
           result.workflow.id().value() == "org.biocore.run.case-001" &&
           result.workflow.name() == "Demo v2" &&
           result.workflow.nodes()[0].plugin_version() == "2.0.0";
}

[[nodiscard]] bool metadata_contract() {
    Catalog catalog;
    application::WorkflowTemplateInstantiationService service{catalog};

    const auto result = service.instantiate({
        .template_id = "org.biocore.template.demo",
        .template_version = "1.0.0",
        .workflow_id = domain::WorkflowId{"org.biocore.run.named"},
        .name = std::string{"My run"},
        .description = std::string{"User-facing instance"},
        .parameter_overrides = {},
    });

    return result.workflow.name() == "My run" &&
           result.workflow.description() == "User-facing instance" &&
           result.workflow.nodes().size() == 2U &&
           result.workflow.edges().size() == 1U;
}

[[nodiscard]] bool override_contract() {
    Catalog catalog;
    application::WorkflowTemplateInstantiationService service{catalog};

    const auto result = service.instantiate({
        .template_id = "org.biocore.template.demo",
        .template_version = "1.0.0",
        .workflow_id = domain::WorkflowId{"org.biocore.run.override"},
        .name = std::nullopt,
        .description = std::nullopt,
        .parameter_overrides = {
            {
                domain::WorkflowNodeId{"source"},
                "mode",
                "sensitive"
            },
            {
                domain::WorkflowNodeId{"target"},
                "threshold",
                "30"
            },
        },
    });

    return result.workflow.nodes()[0].parameters().at("mode") == "sensitive" &&
           result.workflow.nodes()[1].parameters().at("threshold") == "30";
}

[[nodiscard]] bool errors_contract() {
    Catalog catalog;
    application::WorkflowTemplateInstantiationService service{catalog};

    bool missing = false;
    try {
        static_cast<void>(service.instantiate({
            .template_id = "org.biocore.template.demo",
            .template_version = "9.0.0",
            .workflow_id = domain::WorkflowId{"org.biocore.run.missing"},
            .name = std::nullopt,
            .description = std::nullopt,
            .parameter_overrides = {},
        }));
    } catch (const application::WorkflowTemplateInstantiationError& error) {
        missing =
            error.code() ==
            application::WorkflowTemplateInstantiationErrorCode::template_not_found;
    }

    bool duplicate = false;
    try {
        static_cast<void>(service.instantiate({
            .template_id = "org.biocore.template.demo",
            .template_version = "1.0.0",
            .workflow_id = domain::WorkflowId{"org.biocore.run.duplicate"},
            .name = std::nullopt,
            .description = std::nullopt,
            .parameter_overrides = {
                {domain::WorkflowNodeId{"source"}, "mode", "fast"},
                {domain::WorkflowNodeId{"source"}, "mode", "slow"},
            },
        }));
    } catch (const application::WorkflowTemplateInstantiationError& error) {
        duplicate =
            error.code() ==
            application::WorkflowTemplateInstantiationErrorCode::
                duplicate_parameter_override;
    }

    bool unknown_node = false;
    try {
        static_cast<void>(service.instantiate({
            .template_id = "org.biocore.template.demo",
            .template_version = "1.0.0",
            .workflow_id = domain::WorkflowId{"org.biocore.run.unknown"},
            .name = std::nullopt,
            .description = std::nullopt,
            .parameter_overrides = {
                {domain::WorkflowNodeId{"missing"}, "mode", "fast"}
            },
        }));
    } catch (const application::WorkflowTemplateInstantiationError& error) {
        unknown_node =
            error.code() ==
            application::WorkflowTemplateInstantiationErrorCode::
                unknown_override_node;
    }

    return missing && duplicate && unknown_node;
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    const std::string_view name{argv[1]};
    bool passed = false;
    if (name == "exact-version") passed = exact_version_contract();
    else if (name == "metadata") passed = metadata_contract();
    else if (name == "override") passed = override_contract();
    else if (name == "errors") passed = errors_contract();
    else return EXIT_FAILURE;

    if (!passed) {
        std::cerr << "Workflow template instantiation test failed: "
                  << name << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Workflow template instantiation test passed: "
              << name << '\n';
    return EXIT_SUCCESS;
}
