#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>

namespace biocore::presentation { class ILocalWebServer; }

namespace biocore::bootstrap {

struct ServeArguments final {
    std::filesystem::path project_root;
    std::uint16_t port{8421U};
    std::filesystem::path executable_path{};
    std::optional<std::filesystem::path> pipeline_root{};
    std::optional<std::filesystem::path> plugin_root{};
    std::optional<std::filesystem::path> worker_executable{};
    std::optional<std::filesystem::path> frontend_root{};
    std::size_t maximum_concurrent_jobs{2U};
};

struct RuntimeAssets final {
    std::filesystem::path pipeline_root;
    std::filesystem::path plugin_root;
    std::filesystem::path worker_executable;
};

[[nodiscard]] std::filesystem::path validated_project_root(std::filesystem::path root);
[[nodiscard]] RuntimeAssets resolve_runtime_assets(const ServeArguments& arguments);
[[nodiscard]] int run_local_server(
    const ServeArguments& arguments,
    presentation::ILocalWebServer& server,
    std::ostream& standard_output,
    std::ostream& standard_error
);

}  // namespace biocore::bootstrap
