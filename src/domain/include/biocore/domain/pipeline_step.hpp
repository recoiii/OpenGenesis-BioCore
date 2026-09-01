#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace biocore::domain {

class PipelineStep final {
public:
    static constexpr std::size_t maximum_id_length = 128U;
    static constexpr std::size_t maximum_module_id_length = 256U;
    static constexpr std::size_t maximum_plugin_version_length = 64U;
    static constexpr std::size_t maximum_dependencies = 64U;

    PipelineStep(
        std::string id,
        std::string module_id,
        std::string plugin_version,
        std::vector<std::string> depends_on,
        double weight
    );

    [[nodiscard]] std::string_view id() const noexcept;
    [[nodiscard]] std::string_view module_id() const noexcept;
    [[nodiscard]] std::string_view plugin_version() const noexcept;
    [[nodiscard]] const std::vector<std::string>& depends_on() const noexcept;
    [[nodiscard]] double weight() const noexcept;

private:
    std::string id_;
    std::string module_id_;
    std::string plugin_version_;
    std::vector<std::string> depends_on_;
    double weight_;
};

}  // namespace biocore::domain
