#pragma once

#include <filesystem>

#include "biocore/domain/pipeline_definition.hpp"

namespace biocore::infrastructure {

class JsonPipelineDefinitionLoader final {
public:
    [[nodiscard]] domain::PipelineDefinition load(
        const std::filesystem::path& definition_path
    ) const;
};

}  // namespace biocore::infrastructure
