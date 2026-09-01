#include "biocore/infrastructure/json_pipeline_definition_loader.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "biocore/domain/pipeline_step.hpp"
#include "biocore/pipeline_protocol/pipeline_document.hpp"
#include "biocore/pipeline_protocol/pipeline_document_codec.hpp"

namespace biocore::infrastructure {
namespace {

[[nodiscard]] std::string read_document(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || std::filesystem::is_symlink(status)) {
        throw std::invalid_argument("Pipeline definition path must not be a symbolic link");
    }
    const std::filesystem::path canonical = std::filesystem::canonical(path, error);
    if (error || !std::filesystem::is_regular_file(canonical, error) || error) {
        throw std::invalid_argument("Pipeline definition path must identify an existing regular file");
    }
    const auto size = std::filesystem::file_size(canonical, error);
    if (error || size == 0U ||
        size > biocore::pipeline_protocol::maximum_pipeline_document_bytes) {
        throw std::invalid_argument("Pipeline definition file size is invalid");
    }
    std::ifstream input{canonical, std::ios::binary};
    if (!input) {
        throw std::runtime_error("Unable to open pipeline definition file");
    }
    std::string content{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}
    };
    if (input.bad()) {
        throw std::runtime_error("Unable to read pipeline definition file");
    }
    return content;
}

}  // namespace

domain::PipelineDefinition JsonPipelineDefinitionLoader::load(
    const std::filesystem::path& definition_path
) const {
    const pipeline_protocol::PipelineDefinitionDocument document =
        pipeline_protocol::parse_pipeline_definition_document(read_document(definition_path));
    std::vector<domain::PipelineStep> steps;
    steps.reserve(document.steps.size());
    for (const pipeline_protocol::PipelineStepDocument& step : document.steps) {
        steps.emplace_back(
            step.id, step.module_id, step.plugin_version, step.depends_on, step.weight
        );
    }
    return domain::PipelineDefinition{
        document.schema_version,
        document.id,
        document.name,
        document.version,
        std::move(steps),
    };
}

}  // namespace biocore::infrastructure
