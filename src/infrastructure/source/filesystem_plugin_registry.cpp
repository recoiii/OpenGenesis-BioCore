#include "biocore/infrastructure/filesystem_plugin_registry.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include "biocore/domain/plugin_manifest.hpp"
#include "biocore/plugin_protocol/plugin_document_codec.hpp"

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace biocore::infrastructure {
namespace {

struct CandidatePlugin final {
    application::RegisteredPlugin plugin;
    std::vector<application::ResolvedPluginModule> modules;
    std::string candidate_path;
};

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string value = path.u8string();
    return std::string{reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] std::filesystem::path path_from_manifest_relative(
    const std::string_view value
) {
    std::filesystem::path result;
    std::size_t begin = 0U;
    while (begin < value.size()) {
        const std::size_t end = value.find('/', begin);
        const std::string_view segment = value.substr(
            begin, end == std::string_view::npos ? value.size() - begin : end - begin
        );
        std::u8string utf8;
        utf8.reserve(segment.size());
        for (const char character : segment) {
            utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
        }
        result /= std::filesystem::path{utf8};
        if (end == std::string_view::npos) break;
        begin = end + 1U;
    }
    return result;
}

[[nodiscard]] bool is_within(
    const std::filesystem::path& parent,
    const std::filesystem::path& child
) {
    const std::filesystem::path relative = child.lexically_relative(parent);
    if (relative.empty() || relative.is_absolute()) return false;
    const auto first = relative.begin();
    return first != relative.end() && *first != "..";
}

[[nodiscard]] std::filesystem::path require_canonical_directory(
    const std::filesystem::path& input,
    const std::string_view description
) {
    if (input.empty() || !input.is_absolute()) {
        throw std::invalid_argument(std::string{description} + " must be an absolute path");
    }
    std::error_code error;
    const auto status = std::filesystem::symlink_status(input, error);
    if (error || std::filesystem::is_symlink(status)) {
        throw std::invalid_argument(std::string{description} + " must not be a symbolic link");
    }
    const auto canonical = std::filesystem::canonical(input, error);
    if (error || input.lexically_normal() != canonical ||
        !std::filesystem::is_directory(canonical, error) || error ||
        canonical == canonical.root_path()) {
        throw std::invalid_argument(std::string{description} + " must be an existing canonical directory");
    }
    return canonical;
}

[[nodiscard]] std::string read_manifest(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0U || size > plugin_protocol::maximum_plugin_manifest_bytes) {
        throw std::invalid_argument("Plugin manifest file size is invalid");
    }
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("Unable to open plugin manifest");
    std::string content{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}
    };
    if (input.bad()) throw std::runtime_error("Unable to read plugin manifest");
    return content;
}

[[nodiscard]] domain::PluginManifest to_manifest(
    const plugin_protocol::PluginManifestDocument& document
) {
    std::vector<domain::PluginModuleDefinition> modules;
    modules.reserve(document.modules.size());
    for (const auto& module : document.modules) {
        const auto type = domain::plugin_module_type_from_string(module.type);
        if (!type.has_value()) {
            throw std::invalid_argument("Plugin module type is unsupported");
        }
        std::vector<domain::PluginEntrypoint> entrypoints;
        entrypoints.reserve(module.entrypoints.size());
        for (const auto& entrypoint : module.entrypoints) {
            const auto platform = domain::plugin_platform_from_string(entrypoint.platform);
            if (!platform.has_value()) {
                throw std::invalid_argument("Plugin entrypoint platform is unsupported");
            }
            entrypoints.push_back(domain::PluginEntrypoint{
                .platform = *platform,
                .relative_path = entrypoint.relative_path,
            });
        }
        std::vector<domain::PluginParameterDefinition> parameters;
        parameters.reserve(module.parameters.size());
        for (const auto& parameter : module.parameters) {
            const auto parameter_type = domain::plugin_parameter_type_from_string(parameter.type);
            if (!parameter_type.has_value()) {
                throw std::invalid_argument("Plugin parameter type is unsupported");
            }
            std::optional<domain::PluginParameterValue> default_value;
            if (parameter.default_value.has_value()) {
                default_value = domain::plugin_parameter_value_from_string(
                    *parameter.default_value, *parameter_type
                );
            }
            auto parse_bound = [](const std::optional<std::string>& value) -> std::optional<double> {
                if (!value.has_value()) return std::nullopt;
                const auto parsed = domain::plugin_parameter_value_from_string(
                    *value, domain::PluginParameterType::number
                );
                return std::get<double>(parsed);
            };
            parameters.emplace_back(
                parameter.name, *parameter_type, parameter.required, std::move(default_value),
                parse_bound(parameter.minimum), parse_bound(parameter.maximum), parameter.enum_values
            );
        }
        std::vector<domain::PluginInputPortDefinition> inputs;
        inputs.reserve(module.inputs.size());
        for (const auto& input : module.inputs) {
            inputs.emplace_back(input.name, input.required, input.accepted_file_types);
        }
        std::vector<domain::PluginOutputPortDefinition> outputs;
        outputs.reserve(module.outputs.size());
        for (const auto& output : module.outputs) {
            outputs.emplace_back(output.name, output.file_type);
        }
        modules.emplace_back(
            module.id, *type, std::move(entrypoints), std::move(parameters),
            std::move(inputs), std::move(outputs)
        );
    }
    return domain::PluginManifest{
        document.manifest_version,
        document.id,
        document.name,
        document.version,
        document.api_version,
        document.publisher,
        std::move(modules),
    };
}

[[nodiscard]] std::filesystem::path validate_entrypoint(
    const std::filesystem::path& plugin_root,
    const std::string_view relative_path
) {
    const std::filesystem::path input = plugin_root / path_from_manifest_relative(relative_path);
    std::error_code error;
    const auto status = std::filesystem::symlink_status(input, error);
    if (error || std::filesystem::is_symlink(status)) {
        throw std::invalid_argument("Plugin entrypoint must not be a symbolic link");
    }
    const auto canonical = std::filesystem::canonical(input, error);
    if (error || input.lexically_normal() != canonical ||
        !std::filesystem::is_regular_file(canonical, error) || error ||
        !is_within(plugin_root, canonical)) {
        throw std::invalid_argument("Plugin entrypoint must be a canonical plugin-local file");
    }
#if !defined(_WIN32)
    if (::access(canonical.c_str(), X_OK) != 0) {
        throw std::invalid_argument("Plugin entrypoint is not executable");
    }
#endif
    return canonical;
}

[[nodiscard]] CandidatePlugin load_candidate(
    const std::filesystem::path& directory,
    const domain::PluginPlatform platform
) {
    const std::filesystem::path plugin_root = require_canonical_directory(
        directory, "Plugin directory"
    );
    const std::filesystem::path manifest_path = plugin_root / "plugin.json";
    std::error_code error;
    const auto manifest_status = std::filesystem::symlink_status(manifest_path, error);
    if (error || std::filesystem::is_symlink(manifest_status)) {
        throw std::invalid_argument("Plugin manifest must not be a symbolic link");
    }
    const auto canonical_manifest = std::filesystem::canonical(manifest_path, error);
    if (error || manifest_path.lexically_normal() != canonical_manifest ||
        !std::filesystem::is_regular_file(canonical_manifest, error) || error ||
        !is_within(plugin_root, canonical_manifest)) {
        throw std::invalid_argument("Plugin manifest must be a canonical plugin-local file");
    }

    const domain::PluginManifest manifest = to_manifest(
        plugin_protocol::parse_plugin_manifest_document(read_manifest(canonical_manifest))
    );
    CandidatePlugin candidate{
        .plugin = application::RegisteredPlugin{
            .id = std::string{manifest.id()},
            .name = std::string{manifest.name()},
            .version = std::string{manifest.version()},
            .manifest_version = manifest.manifest_version(),
            .api_version = std::string{manifest.api_version()},
            .publisher = std::string{manifest.publisher()},
            .root_path = path_to_utf8(plugin_root),
        },
        .modules = {},
        .candidate_path = path_to_utf8(plugin_root),
    };
    candidate.modules.reserve(manifest.modules().size());
    for (const domain::PluginModuleDefinition& module : manifest.modules()) {
        const auto entrypoint = module.entrypoint_for(platform);
        if (!entrypoint.has_value()) {
            throw std::invalid_argument(
                "Plugin module has no entrypoint for platform " +
                std::string{domain::to_string(platform)}
            );
        }
        const auto executable = validate_entrypoint(plugin_root, *entrypoint);
        candidate.modules.push_back(application::ResolvedPluginModule{
            .plugin_id = std::string{manifest.id()},
            .plugin_version = std::string{manifest.version()},
            .plugin_manifest_version = manifest.manifest_version(),
            .plugin_api_version = std::string{manifest.api_version()},
            .module_id = std::string{module.id()},
            .module_type = module.type(),
            .plugin_root_path = path_to_utf8(plugin_root),
            .executable_path = path_to_utf8(executable),
            .parameters = module.parameters(),
            .inputs = module.inputs(),
            .outputs = module.outputs(),
        });
    }
    return candidate;
}

}  // namespace

domain::PluginPlatform current_plugin_platform() {
#if defined(_WIN32)
#if defined(_M_ARM64) || defined(__aarch64__)
    return domain::PluginPlatform::windows_arm64;
#elif defined(_M_X64) || defined(__x86_64__)
    return domain::PluginPlatform::windows_x64;
#else
    throw std::runtime_error("OpenGenesis-BioCore plugin discovery does not support this Windows architecture");
#endif
#else
#if defined(__aarch64__)
    return domain::PluginPlatform::linux_arm64;
#elif defined(__x86_64__)
    return domain::PluginPlatform::linux_x64;
#else
    throw std::runtime_error("OpenGenesis-BioCore plugin discovery does not support this Linux architecture");
#endif
#endif
}

FilesystemPluginRegistry::FilesystemPluginRegistry(
    std::vector<std::filesystem::path> plugin_roots,
    const domain::PluginPlatform platform
)
    : platform_{platform} {
    plugin_roots_.reserve(plugin_roots.size());
    for (const auto& root : plugin_roots) {
        plugin_roots_.push_back(require_canonical_directory(root, "Plugin root"));
    }
}

PluginDiscoveryReport FilesystemPluginRegistry::refresh() {
    std::vector<CandidatePlugin> candidates;
    PluginDiscoveryReport report;

    for (const auto& root : plugin_roots_) {
        std::vector<std::filesystem::path> directories;
        std::error_code error;
        for (std::filesystem::directory_iterator iterator{root, error}, end;
             !error && iterator != end; iterator.increment(error)) {
            const auto status = iterator->symlink_status(error);
            if (error) break;
            if (std::filesystem::is_directory(status) && !std::filesystem::is_symlink(status)) {
                directories.push_back(iterator->path());
            }
        }
        if (error) {
            report.rejected.push_back(PluginDiscoveryIssue{
                .candidate_path = path_to_utf8(root),
                .message = "Plugin root could not be enumerated: " + error.message(),
            });
            continue;
        }
        std::ranges::sort(directories, [](const auto& left, const auto& right) {
            return left.native() < right.native();
        });
        for (const auto& directory : directories) {
            try {
                candidates.push_back(load_candidate(directory, platform_));
            } catch (const std::exception& error_message) {
                report.rejected.push_back(PluginDiscoveryIssue{
                    .candidate_path = path_to_utf8(directory),
                    .message = error_message.what(),
                });
            }
        }
    }

    std::unordered_map<std::string, std::size_t> plugin_counts;
    std::unordered_map<std::string, std::size_t> module_counts;
    for (const CandidatePlugin& candidate : candidates) {
        ++plugin_counts[candidate.plugin.id];
        for (const auto& module : candidate.modules) ++module_counts[module.module_id];
    }

    std::vector<application::RegisteredPlugin> next_plugins;
    std::vector<application::ResolvedPluginModule> next_modules;
    for (CandidatePlugin& candidate : candidates) {
        bool conflict = plugin_counts[candidate.plugin.id] > 1U;
        for (const auto& module : candidate.modules) {
            conflict = conflict || module_counts[module.module_id] > 1U;
        }
        if (conflict) {
            report.rejected.push_back(PluginDiscoveryIssue{
                .candidate_path = candidate.candidate_path,
                .message = "Plugin or module identifier conflicts with another candidate",
            });
            continue;
        }
        next_plugins.push_back(std::move(candidate.plugin));
        for (auto& module : candidate.modules) next_modules.push_back(std::move(module));
    }

    std::ranges::sort(next_plugins, {}, &application::RegisteredPlugin::id);
    std::ranges::sort(next_modules, {}, &application::ResolvedPluginModule::module_id);
    report.loaded_plugins = next_plugins.size();
    report.loaded_modules = next_modules.size();
    {
        std::unique_lock lock{mutex_};
        plugins_ = std::move(next_plugins);
        modules_ = std::move(next_modules);
    }
    return report;
}

std::optional<application::ResolvedPluginModule> FilesystemPluginRegistry::find_module(
    const std::string_view module_id
) const {
    std::shared_lock lock{mutex_};
    const auto iterator = std::ranges::lower_bound(
        modules_, module_id, {}, &application::ResolvedPluginModule::module_id
    );
    if (iterator == modules_.end() || iterator->module_id != module_id) return std::nullopt;
    return *iterator;
}

std::vector<application::RegisteredPlugin> FilesystemPluginRegistry::list_plugins() const {
    std::shared_lock lock{mutex_};
    return plugins_;
}

}  // namespace biocore::infrastructure
