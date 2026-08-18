#pragma once

#include <filesystem>
#include <shared_mutex>
#include <string>
#include <vector>

#include "biocore/application/i_pipeline_catalog.hpp"

namespace biocore::infrastructure {

struct PipelineDiscoveryIssue final {
    std::string candidate_path;
    std::string message;
};

struct PipelineDiscoveryReport final {
    std::size_t loaded_pipelines{0U};
    std::vector<PipelineDiscoveryIssue> rejected;
};

class FilesystemPipelineCatalog final : public application::IPipelineCatalog {
public:
    explicit FilesystemPipelineCatalog(std::filesystem::path pipeline_root);

    [[nodiscard]] PipelineDiscoveryReport refresh();
    [[nodiscard]] std::optional<domain::PipelineDefinition> find(
        std::string_view pipeline_id,
        std::string_view pipeline_version
    ) const override;
    [[nodiscard]] std::vector<application::RegisteredPipeline> list() const override;

private:
    std::filesystem::path pipeline_root_;
    mutable std::shared_mutex mutex_;
    std::vector<domain::PipelineDefinition> pipelines_;
};

}  // namespace biocore::infrastructure
