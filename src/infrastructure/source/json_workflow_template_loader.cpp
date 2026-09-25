#include "biocore/infrastructure/json_workflow_template_loader.hpp"

#include <fstream>
#include <stdexcept>
#include <string>

#include "biocore/pipeline_protocol/workflow_template_document_codec.hpp"

namespace biocore::infrastructure {

domain::WorkflowTemplate JsonWorkflowTemplateLoader::load(
    const std::filesystem::path& path
) const {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || !std::filesystem::is_regular_file(status) ||
        std::filesystem::is_symlink(status)) {
        throw std::invalid_argument(
            "Workflow template path must reference a regular non-symlink file"
        );
    }

    const auto size = std::filesystem::file_size(path, error);
    if (error ||
        size == 0U ||
        size > pipeline_protocol::maximum_workflow_template_document_bytes) {
        throw std::invalid_argument(
            "Workflow template document size is invalid"
        );
    }

    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error(
            "Workflow template document could not be opened"
        );
    }

    std::string content;
    content.resize(static_cast<std::size_t>(size));
    stream.read(content.data(), static_cast<std::streamsize>(content.size()));
    if (!stream ||
        stream.gcount() != static_cast<std::streamsize>(content.size())) {
        throw std::runtime_error(
            "Workflow template document could not be read completely"
        );
    }

    return pipeline_protocol::parse_workflow_template_document(content);
}

}  // namespace biocore::infrastructure
