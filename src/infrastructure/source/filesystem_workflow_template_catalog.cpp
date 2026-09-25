#include "biocore/infrastructure/filesystem_workflow_template_catalog.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "biocore/infrastructure/json_workflow_template_loader.hpp"

namespace biocore::infrastructure {
namespace {

[[nodiscard]] std::string path_to_utf8(
    const std::filesystem::path& path
) {
    const std::u8string value = path.u8string();
    return std::string{
        reinterpret_cast<const char*>(value.data()),
        value.size()
    };
}

[[nodiscard]] std::filesystem::path require_root(
    const std::filesystem::path& input
) {
    if (input.empty() || !input.is_absolute()) {
        throw std::invalid_argument(
            "Workflow template root must be an absolute path"
        );
    }

    std::error_code error;
    const auto status = std::filesystem::symlink_status(input, error);
    if (error || std::filesystem::is_symlink(status)) {
        throw std::invalid_argument(
            "Workflow template root must not be a symbolic link"
        );
    }

    const auto canonical = std::filesystem::canonical(input, error);
    if (error ||
        canonical != input.lexically_normal() ||
        !std::filesystem::is_directory(canonical, error) ||
        error) {
        throw std::invalid_argument(
            "Workflow template root must be an existing canonical directory"
        );
    }
    return canonical;
}

[[nodiscard]] bool is_template_candidate(
    const std::filesystem::path& path
) {
    constexpr std::string_view suffix = ".workflow-template.json";
    const std::string filename = path.filename().string();
    return filename.size() > suffix.size() &&
           filename.ends_with(suffix);
}

[[nodiscard]] std::string key(
    const std::string_view id,
    const std::string_view version
) {
    return std::string{id} + "\n" + std::string{version};
}

struct LoadedCandidate final {
    std::filesystem::path path;
    domain::WorkflowTemplate value;
};

}  // namespace

FilesystemWorkflowTemplateCatalog::FilesystemWorkflowTemplateCatalog(
    std::filesystem::path template_root
)
    : template_root_{require_root(template_root)} {
    JsonWorkflowTemplateLoader loader;
    std::vector<std::filesystem::path> candidates;
    std::error_code error;

    for (
        std::filesystem::directory_iterator iterator{template_root_, error}, end;
        !error && iterator != end;
        iterator.increment(error)
    ) {
        const auto status = iterator->symlink_status(error);
        if (error) break;

        if (std::filesystem::is_regular_file(status) &&
            !std::filesystem::is_symlink(status) &&
            is_template_candidate(iterator->path())) {
            candidates.push_back(iterator->path());
        }
    }

    if (error) {
        throw std::runtime_error(
            "Workflow template root could not be enumerated"
        );
    }

    std::ranges::sort(
        candidates,
        [](const auto& left, const auto& right) {
            return left.native() < right.native();
        }
    );

    std::vector<LoadedCandidate> loaded;
    std::map<std::string, std::size_t, std::less<>> counts;

    for (const auto& path : candidates) {
        try {
            auto value = loader.load(path);
            ++counts[key(value.id(), value.version())];
            loaded.push_back(
                LoadedCandidate{path, std::move(value)}
            );
        } catch (const std::exception& exception) {
            report_.rejected.push_back(
                {path_to_utf8(path), exception.what()}
            );
        }
    }

    for (auto& candidate : loaded) {
        if (counts[key(
                candidate.value.id(),
                candidate.value.version()
            )] != 1U) {
            report_.rejected.push_back({
                path_to_utf8(candidate.path),
                "Workflow template identifier/version conflicts with another candidate"
            });
            continue;
        }
        templates_.push_back(std::move(candidate.value));
    }

    std::ranges::sort(
        templates_,
        [](const auto& left, const auto& right) {
            if (left.id() != right.id()) {
                return left.id() < right.id();
            }
            return left.version() < right.version();
        }
    );

    report_.loaded_templates = templates_.size();
}

std::optional<domain::WorkflowTemplate>
FilesystemWorkflowTemplateCatalog::find(
    const std::string_view template_id,
    const std::string_view template_version
) const {
    const auto iterator = std::ranges::find_if(
        templates_,
        [&](const auto& value) {
            return value.id() == template_id &&
                   value.version() == template_version;
        }
    );
    if (iterator == templates_.end()) {
        return std::nullopt;
    }
    return *iterator;
}

std::vector<application::RegisteredWorkflowTemplate>
FilesystemWorkflowTemplateCatalog::list() const {
    std::vector<application::RegisteredWorkflowTemplate> result;
    result.reserve(templates_.size());
    for (const auto& value : templates_) {
        result.push_back({
            .id = std::string{value.id()},
            .version = std::string{value.version()},
            .name = std::string{value.name()},
            .description = std::string{value.description()},
        });
    }
    return result;
}

const WorkflowTemplateDiscoveryReport&
FilesystemWorkflowTemplateCatalog::report() const noexcept {
    return report_;
}

}  // namespace biocore::infrastructure
