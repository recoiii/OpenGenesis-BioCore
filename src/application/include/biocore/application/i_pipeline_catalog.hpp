#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "biocore/domain/pipeline_definition.hpp"

namespace biocore::application {

struct RegisteredPipeline final {
    std::string id;
    std::string name;
    std::string version;
};

class IPipelineCatalog {
public:
    virtual ~IPipelineCatalog() = default;
    [[nodiscard]] virtual std::optional<domain::PipelineDefinition> find(
        std::string_view pipeline_id,
        std::string_view pipeline_version
    ) const = 0;
    [[nodiscard]] virtual std::vector<RegisteredPipeline> list() const = 0;
};

}  // namespace biocore::application
