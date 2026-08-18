#include "biocore/infrastructure/json_plugin_invocation_store.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "biocore/plugin_protocol/plugin_document.hpp"
#include "biocore/plugin_protocol/plugin_document_codec.hpp"

namespace biocore::infrastructure {
namespace {

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string value = path.u8string();
    return std::string{reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] std::filesystem::path path_from_utf8(const std::string_view value) {
    std::u8string utf8;
    utf8.reserve(value.size());
    for (const char character : value) {
        utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
    }
    return std::filesystem::path{utf8};
}

[[nodiscard]] bool is_safe_component(const std::string_view value) {
    return !value.empty() && value.size() <= 128U && value != "." && value != ".." &&
           std::ranges::all_of(value, [](const char raw) {
               const auto character = static_cast<unsigned char>(raw);
               return std::isalnum(character) != 0 || raw == '-' || raw == '_' || raw == '.';
           });
}

[[nodiscard]] bool is_within(
    const std::filesystem::path& parent,
    const std::filesystem::path& child
) {
    const auto relative = child.lexically_relative(parent);
    if (relative.empty() || relative.is_absolute()) return false;
    const auto first = relative.begin();
    return first != relative.end() && *first != "..";
}

[[nodiscard]] std::filesystem::path require_real_directory(
    const std::filesystem::path& input,
    const std::string_view description
) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(input, error);
    if (error || std::filesystem::is_symlink(status)) {
        throw std::invalid_argument(std::string{description} + " must not be a symbolic link");
    }
    const auto canonical = std::filesystem::canonical(input, error);
    if (error || input.lexically_normal() != canonical ||
        !std::filesystem::is_directory(canonical, error) || error) {
        throw std::invalid_argument(std::string{description} + " must be a canonical directory");
    }
    return canonical;
}

[[nodiscard]] std::filesystem::path safe_relative(const std::string_view value) {
    if (value.empty() || value.size() > 4096U || value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("Plugin binding relative path is invalid");
    }
    const auto path = path_from_utf8(value);
    if (path.is_absolute() || path.has_root_path()) {
        throw std::invalid_argument("Plugin binding path must be project-relative");
    }
    const auto normalized = path.lexically_normal();
    if (normalized != path || normalized.empty() || *normalized.begin() == "..") {
        throw std::invalid_argument("Plugin binding relative path is unsafe");
    }
    return normalized;
}

[[nodiscard]] std::filesystem::path resolve_existing_input(
    const std::filesystem::path& project_root,
    const std::string_view relative
) {
    const auto input = project_root / safe_relative(relative);
    std::error_code error;
    const auto status = std::filesystem::symlink_status(input, error);
    if (error || std::filesystem::is_symlink(status)) {
        throw std::invalid_argument("Managed input path must not be a symbolic link");
    }
    const auto canonical = std::filesystem::canonical(input, error);
    if (error || input.lexically_normal() != canonical ||
        !std::filesystem::is_regular_file(canonical, error) || error ||
        !is_within(project_root, canonical)) {
        throw std::invalid_argument("Managed input path must be a canonical project-local file");
    }
    return canonical;
}

[[nodiscard]] std::filesystem::path resolve_existing_upstream_output(
    const std::filesystem::path& project_root,
    const std::string_view relative
) {
    const auto relative_path = safe_relative(relative);
    if (relative_path.begin() == relative_path.end() || *relative_path.begin() != "outputs") {
        throw std::invalid_argument("Upstream output must be inside the project outputs directory");
    }
    const auto input = project_root / relative_path;
    std::error_code error;
    const auto status = std::filesystem::symlink_status(input, error);
    if (error || std::filesystem::is_symlink(status)) {
        throw std::invalid_argument("Upstream output must not be a symbolic link");
    }
    const auto canonical = std::filesystem::canonical(input, error);
    if (error || input.lexically_normal() != canonical ||
        !std::filesystem::is_regular_file(canonical, error) || error ||
        !is_within(project_root, canonical)) {
        throw std::invalid_argument("Upstream output must be an existing project-local file");
    }
    return canonical;
}

[[nodiscard]] std::filesystem::path resolve_output_path(
    const std::filesystem::path& project_root,
    const std::string_view relative
) {
    const auto relative_path = safe_relative(relative);
    if (relative_path.begin() == relative_path.end() || *relative_path.begin() != "outputs") {
        throw std::invalid_argument("Plugin output path must be inside the project outputs directory");
    }
    const auto parent = require_real_directory(project_root / "outputs", "Project outputs directory");
    const auto target = (project_root / relative_path).lexically_normal();
    if (!is_within(project_root, target) || target.parent_path() != parent) {
        throw std::invalid_argument("Plugin output target must use the flat OpenGenesis-BioCore outputs namespace");
    }
    std::error_code error;
    if (std::filesystem::exists(target, error) || error) {
        throw std::runtime_error("Plugin output target already exists");
    }
    return target;
}

}  // namespace

JsonPluginInvocationStore::JsonPluginInvocationStore(std::filesystem::path project_root)
    : project_root_{require_real_directory(project_root, "Plugin invocation project root")} {
    static_cast<void>(require_real_directory(project_root_ / ".biocore", "Project metadata directory"));
    static_cast<void>(require_real_directory(project_root_ / ".biocore" / "runtime", "Project runtime directory"));
    static_cast<void>(require_real_directory(project_root_ / "outputs", "Project outputs directory"));
}

std::string JsonPluginInvocationStore::store(
    const std::string_view job_id,
    const std::int64_t job_revision,
    const application::ExecutionPlanStep& step
) {
    if (!is_safe_component(job_id) || !is_safe_component(step.id) || job_revision < 0) {
        throw std::invalid_argument("Plugin invocation identity is unsafe");
    }
    const auto job_directory = require_real_directory(
        project_root_ / ".biocore" / "runtime" / "jobs" / std::string{job_id},
        "Plugin invocation job directory"
    );
    const auto final_path = job_directory /
        ("invocation-" + step.id + "-r" + std::to_string(job_revision) + ".json");
    const auto temporary_path = final_path.string() + ".tmp";

    plugin_protocol::PluginInvocationDocument document{
        .schema_version = plugin_protocol::current_plugin_invocation_schema_version,
        .job_id = std::string{job_id},
        .job_revision = job_revision,
        .step_id = step.id,
        .module_id = step.module_id,
        .parameters = {},
        .inputs = {},
        .outputs = {},
    };
    document.parameters.reserve(step.parameters.size());
    for (const auto& parameter : step.parameters) {
        document.parameters.push_back({
            parameter.name, std::string{domain::to_string(parameter.type)}, parameter.value
        });
    }
    document.inputs.reserve(step.inputs.size());
    for (const auto& input : step.inputs) {
        std::filesystem::path absolute;
        if (input.source_kind == application::ExecutionInputSourceKind::managed_file) {
            absolute = resolve_existing_input(project_root_, input.relative_project_path);
        } else {
            absolute = resolve_existing_upstream_output(project_root_, input.relative_project_path);
        }
        document.inputs.push_back({
            input.port_name,
            std::string{application::to_string(input.source_kind)},
            input.source_id,
            input.file_type,
            path_to_utf8(absolute),
        });
    }
    document.outputs.reserve(step.outputs.size());
    for (const auto& output : step.outputs) {
        document.outputs.push_back({
            output.port_name,
            output.file_type,
            path_to_utf8(resolve_output_path(project_root_, output.relative_project_path)),
        });
    }

    std::error_code error;
    if (std::filesystem::exists(final_path, error) || error ||
        std::filesystem::exists(temporary_path, error) || error) {
        throw std::runtime_error("Plugin invocation snapshot path already exists");
    }
    const std::string content = plugin_protocol::serialize_plugin_invocation_document(document);
    bool published = false;
    try {
        std::ofstream output{temporary_path, std::ios::binary | std::ios::trunc};
        if (!output) throw std::runtime_error("Unable to create plugin invocation snapshot");
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.flush();
        if (!output) throw std::runtime_error("Unable to write plugin invocation snapshot");
        output.close();
        if (!output) throw std::runtime_error("Unable to close plugin invocation snapshot");
        std::filesystem::rename(temporary_path, final_path, error);
        if (error) throw std::runtime_error("Unable to publish plugin invocation snapshot");
        published = true;
        const auto status = std::filesystem::symlink_status(final_path, error);
        const auto canonical = std::filesystem::canonical(final_path, error);
        if (error || std::filesystem::is_symlink(status) ||
            !std::filesystem::is_regular_file(canonical, error) || error ||
            canonical.parent_path() != job_directory) {
            throw std::runtime_error("Unable to validate plugin invocation snapshot");
        }
        return path_to_utf8(canonical);
    } catch (...) {
        error.clear();
        static_cast<void>(std::filesystem::remove(temporary_path, error));
        if (published) {
            error.clear();
            static_cast<void>(std::filesystem::remove(final_path, error));
        }
        throw;
    }
}

}  // namespace biocore::infrastructure
