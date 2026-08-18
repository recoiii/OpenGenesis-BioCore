#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/domain/pipeline_step.hpp"

namespace biocore::domain {

class PipelineDefinition final {
public:
    static constexpr std::uint32_t current_schema_version = 1U;
    static constexpr std::size_t maximum_id_length = 256U;
    static constexpr std::size_t maximum_name_length = 256U;
    static constexpr std::size_t maximum_version_length = 64U;
    static constexpr std::size_t maximum_steps = 256U;

    PipelineDefinition(
        std::uint32_t schema_version,
        std::string id,
        std::string name,
        std::string version,
        std::vector<PipelineStep> steps
    );

    [[nodiscard]] std::uint32_t schema_version() const noexcept;
    [[nodiscard]] std::string_view id() const noexcept;
    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] std::string_view version() const noexcept;
    [[nodiscard]] const std::vector<PipelineStep>& steps() const noexcept;
    [[nodiscard]] const std::vector<std::size_t>& topological_order() const noexcept;
    [[nodiscard]] double total_weight() const noexcept;

private:
    std::uint32_t schema_version_;
    std::string id_;
    std::string name_;
    std::string version_;
    std::vector<PipelineStep> steps_;
    std::vector<std::size_t> topological_order_;
    double total_weight_{0.0};
};

}  // namespace biocore::domain
