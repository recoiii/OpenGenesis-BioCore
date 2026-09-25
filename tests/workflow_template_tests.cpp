#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "biocore/domain/workflow.hpp"
#include "biocore/domain/workflow_template.hpp"

namespace {

using namespace biocore::domain;

[[nodiscard]] Workflow blueprint(
    std::string id = "org.biocore.template.demo"
) {
    return Workflow{
        Workflow::current_schema_version,
        WorkflowId{std::move(id)},
        "Blueprint",
        "Reusable graph",
        {
            WorkflowNode{
                WorkflowNodeId{"source"},
                "Source",
                "org.biocore.test.source",
                "1.0.0",
                {},
                {WorkflowOutputDeclaration{"result", "txt"}},
                {{"mode", "fast"}}
            },
            WorkflowNode{
                WorkflowNodeId{"target"},
                "Target",
                "org.biocore.test.target",
                "1.0.0",
                {WorkflowInputDeclaration{"input", "txt", true}},
                {WorkflowOutputDeclaration{"report", "json"}},
                {}
            },
        },
        {
            WorkflowEdge{
                WorkflowNodeId{"source"}, "result",
                WorkflowNodeId{"target"}, "input"
            }
        }
    };
}

template <typename Function>
[[nodiscard]] bool rejects(Function&& function) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_contract() {
    const WorkflowTemplate value{
        WorkflowTemplate::current_schema_version,
        "org.biocore.template.demo",
        "1.2.3",
        "Demo template",
        "Reusable workflow",
        blueprint()
    };
    return value.schema_version() == 1U &&
           value.id() == "org.biocore.template.demo" &&
           value.version() == "1.2.3" &&
           value.blueprint().nodes().size() == 2U;
}

[[nodiscard]] bool invalid_contract() {
    const bool bad_version = rejects([] {
        static_cast<void>(WorkflowTemplate{
            1U,
            "org.biocore.template.demo",
            "latest",
            "Demo",
            "",
            blueprint()
        });
    });

    const bool id_mismatch = rejects([] {
        static_cast<void>(WorkflowTemplate{
            1U,
            "org.biocore.template.demo",
            "1.0.0",
            "Demo",
            "",
            blueprint("org.biocore.template.other")
        });
    });

    const bool cycle = rejects([] {
        Workflow cyclic{
            Workflow::current_schema_version,
            WorkflowId{"org.biocore.template.demo"},
            "Cyclic",
            "",
            {
                WorkflowNode{
                    WorkflowNodeId{"a"},
                    "A",
                    "org.biocore.test.a",
                    "1.0.0",
                    {WorkflowInputDeclaration{"in", "txt", true}},
                    {WorkflowOutputDeclaration{"out", "txt"}},
                    {}
                },
                WorkflowNode{
                    WorkflowNodeId{"b"},
                    "B",
                    "org.biocore.test.b",
                    "1.0.0",
                    {WorkflowInputDeclaration{"in", "txt", true}},
                    {WorkflowOutputDeclaration{"out", "txt"}},
                    {}
                },
            },
            {
                WorkflowEdge{
                    WorkflowNodeId{"a"}, "out",
                    WorkflowNodeId{"b"}, "in"
                },
                WorkflowEdge{
                    WorkflowNodeId{"b"}, "out",
                    WorkflowNodeId{"a"}, "in"
                },
            }
        };
        static_cast<void>(WorkflowTemplate{
            1U,
            "org.biocore.template.demo",
            "1.0.0",
            "Demo",
            "",
            std::move(cyclic)
        });
    });

    return bad_version && id_mismatch && cycle;
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    const std::string_view name{argv[1]};
    const bool passed =
        (name == "valid" && valid_contract()) ||
        (name == "invalid" && invalid_contract());
    if (!passed) {
        std::cerr << "Workflow template domain test failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Workflow template domain test passed\n";
    return EXIT_SUCCESS;
}
