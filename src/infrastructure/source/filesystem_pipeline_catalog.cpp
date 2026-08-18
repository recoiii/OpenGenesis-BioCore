#include "biocore/infrastructure/filesystem_pipeline_catalog.hpp"

#include <algorithm>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#include "biocore/infrastructure/json_pipeline_definition_loader.hpp"

namespace biocore::infrastructure {
namespace {

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string value = path.u8string();
    return std::string{reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] std::filesystem::path require_root(const std::filesystem::path& input) {
    if (input.empty() || !input.is_absolute()) {
        throw std::invalid_argument("Pipeline root must be an absolute path");
    }
    std::error_code error;
    const auto status = std::filesystem::symlink_status(input, error);
    if (error || std::filesystem::is_symlink(status)) {
        throw std::invalid_argument("Pipeline root must not be a symbolic link");
    }
    const auto canonical = std::filesystem::canonical(input, error);
    if (error || canonical != input.lexically_normal() ||
        !std::filesystem::is_directory(canonical, error) || error) {
        throw std::invalid_argument("Pipeline root must be an existing canonical directory");
    }
    return canonical;
}

[[nodiscard]] std::string key(const std::string_view id, const std::string_view version) {
    return std::string{id} + "\n" + std::string{version};
}

}  // namespace

FilesystemPipelineCatalog::FilesystemPipelineCatalog(std::filesystem::path pipeline_root)
    : pipeline_root_{require_root(pipeline_root)} {}

PipelineDiscoveryReport FilesystemPipelineCatalog::refresh() {
    PipelineDiscoveryReport report;
    JsonPipelineDefinitionLoader loader;
    std::vector<std::filesystem::path> candidates;
    std::error_code error;
    for (std::filesystem::directory_iterator iterator{pipeline_root_, error}, end;
         !error && iterator != end; iterator.increment(error)) {
        const auto status = iterator->symlink_status(error);
        if (error) break;
        if (std::filesystem::is_regular_file(status) && !std::filesystem::is_symlink(status) &&
            iterator->path().extension() == ".json") {
            candidates.push_back(iterator->path());
        }
    }
    if (error) {
        throw std::runtime_error("Pipeline root could not be enumerated");
    }
    std::ranges::sort(candidates, [](const auto& left, const auto& right) {
        return left.native() < right.native();
    });

    std::vector<domain::PipelineDefinition> loaded;
    std::map<std::string, std::size_t, std::less<>> counts;
    for (const auto& path : candidates) {
        try {
            auto definition = loader.load(path);
            ++counts[key(definition.id(), definition.version())];
            loaded.push_back(std::move(definition));
        } catch (const std::exception& exception) {
            report.rejected.push_back({path_to_utf8(path), exception.what()});
        }
    }

    std::vector<domain::PipelineDefinition> accepted;
    accepted.reserve(loaded.size());
    for (auto& definition : loaded) {
        if (counts[key(definition.id(), definition.version())] != 1U) {
            report.rejected.push_back({
                std::string{definition.id()} + "@" + std::string{definition.version()},
                "Pipeline identifier/version conflicts with another candidate",
            });
            continue;
        }
        accepted.push_back(std::move(definition));
    }
    std::ranges::sort(accepted, [](const auto& left, const auto& right) {
        if (left.id() != right.id()) return left.id() < right.id();
        return left.version() < right.version();
    });
    report.loaded_pipelines = accepted.size();
    {
        std::unique_lock lock{mutex_};
        pipelines_ = std::move(accepted);
    }
    return report;
}

std::optional<domain::PipelineDefinition> FilesystemPipelineCatalog::find(
    const std::string_view pipeline_id,
    const std::string_view pipeline_version
) const {
    std::shared_lock lock{mutex_};
    const auto iterator = std::ranges::find_if(pipelines_, [&](const auto& definition) {
        return definition.id() == pipeline_id && definition.version() == pipeline_version;
    });
    if (iterator == pipelines_.end()) return std::nullopt;
    return *iterator;
}

std::vector<application::RegisteredPipeline> FilesystemPipelineCatalog::list() const {
    std::shared_lock lock{mutex_};
    std::vector<application::RegisteredPipeline> result;
    result.reserve(pipelines_.size());
    for (const auto& definition : pipelines_) {
        result.push_back({
            std::string{definition.id()}, std::string{definition.name()}, std::string{definition.version()}
        });
    }
    return result;
}

}  // namespace biocore::infrastructure
