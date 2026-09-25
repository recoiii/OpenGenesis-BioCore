#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "biocore/domain/workflow.hpp"
#include "biocore/domain/workflow_template.hpp"
#include "biocore/infrastructure/filesystem_workflow_template_catalog.hpp"
#include "biocore/pipeline_protocol/workflow_template_document_codec.hpp"

namespace {

using namespace biocore;
namespace fs = std::filesystem;

class TempRoot final {
public:
    explicit TempRoot(const std::string_view suffix) {
        root_ = fs::temp_directory_path() /
            ("biocore-workflow-template-" + std::string{suffix} + "-" +
             std::to_string(
                 std::chrono::steady_clock::now().time_since_epoch().count()
             ));
        fs::create_directories(root_);
        root_ = fs::canonical(root_);
    }

    ~TempRoot() {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    [[nodiscard]] const fs::path& path() const noexcept {
        return root_;
    }

private:
    fs::path root_;
};

[[nodiscard]] domain::WorkflowTemplate make_template(
    std::string version,
    std::string name
) {
    return domain::WorkflowTemplate{
        1U,
        "org.biocore.template.demo",
        std::move(version),
        std::move(name),
        "Catalog template",
        domain::Workflow{
            1U,
            domain::WorkflowId{"org.biocore.template.demo"},
            "Blueprint",
            "",
            {
                domain::WorkflowNode{
                    domain::WorkflowNodeId{"step"},
                    "Step",
                    "org.biocore.test.step",
                    "1.0.0",
                    {},
                    {domain::WorkflowOutputDeclaration{"result", "txt"}},
                    {}
                }
            },
            {}
        }
    };
}

void write_template(
    const fs::path& path,
    const domain::WorkflowTemplate& value
) {
    std::ofstream stream{path, std::ios::binary};
    stream << pipeline_protocol::serialize_workflow_template_document(value);
}

[[nodiscard]] bool exact_contract() {
    TempRoot root{"exact"};
    write_template(
        root.path() / "v2.workflow-template.json",
        make_template("2.0.0", "Demo v2")
    );
    write_template(
        root.path() / "v1.workflow-template.json",
        make_template("1.0.0", "Demo v1")
    );
    {
        std::ofstream ignored{root.path() / "ignored.json"};
        ignored << "{}";
    }

    infrastructure::FilesystemWorkflowTemplateCatalog catalog{root.path()};
    const auto list = catalog.list();
    const auto exact = catalog.find(
        "org.biocore.template.demo", "2.0.0"
    );
    const auto missing = catalog.find(
        "org.biocore.template.demo", "3.0.0"
    );

    return catalog.report().loaded_templates == 2U &&
           catalog.report().rejected.empty() &&
           list.size() == 2U &&
           list[0].version == "1.0.0" &&
           list[1].version == "2.0.0" &&
           exact.has_value() &&
           exact->name() == "Demo v2" &&
           !missing.has_value();
}

[[nodiscard]] bool duplicate_contract() {
    TempRoot root{"duplicate"};
    const auto value = make_template("1.0.0", "Demo");
    write_template(
        root.path() / "first.workflow-template.json", value
    );
    write_template(
        root.path() / "second.workflow-template.json", value
    );

    infrastructure::FilesystemWorkflowTemplateCatalog catalog{root.path()};
    return catalog.report().loaded_templates == 0U &&
           catalog.report().rejected.size() == 2U &&
           catalog.list().empty() &&
           !catalog.find(
               "org.biocore.template.demo", "1.0.0"
           ).has_value();
}

[[nodiscard]] bool snapshot_contract() {
    TempRoot root{"snapshot"};
    write_template(
        root.path() / "v1.workflow-template.json",
        make_template("1.0.0", "Demo v1")
    );
    {
        std::ofstream invalid{
            root.path() / "bad.workflow-template.json",
            std::ios::binary
        };
        invalid << "{not-json}";
    }

    infrastructure::FilesystemWorkflowTemplateCatalog catalog{root.path()};
    if (catalog.report().loaded_templates != 1U ||
        catalog.report().rejected.size() != 1U) {
        return false;
    }

    write_template(
        root.path() / "v2.workflow-template.json",
        make_template("2.0.0", "Demo v2")
    );

    return catalog.list().size() == 1U &&
           !catalog.find(
               "org.biocore.template.demo", "2.0.0"
           ).has_value();
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    const std::string_view name{argv[1]};
    bool passed = false;
    if (name == "exact") passed = exact_contract();
    else if (name == "duplicate") passed = duplicate_contract();
    else if (name == "snapshot") passed = snapshot_contract();
    else return EXIT_FAILURE;

    if (!passed) {
        std::cerr << "Filesystem workflow template catalog test failed: "
                  << name << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Filesystem workflow template catalog test passed: "
              << name << '\n';
    return EXIT_SUCCESS;
}
