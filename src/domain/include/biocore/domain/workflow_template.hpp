#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "biocore/domain/workflow.hpp"

namespace biocore::domain {

class WorkflowTemplate final {
public:
    static constexpr std::uint32_t current_schema_version = 1U;
    static constexpr std::size_t maximum_id_length = 256U;
    static constexpr std::size_t maximum_version_length = 64U;
    static constexpr std::size_t maximum_name_length = 256U;
    static constexpr std::size_t maximum_description_length = 8192U;

    WorkflowTemplate(
        std::uint32_t schema_version,
        std::string id,
        std::string version,
        std::string name,
        std::string description,
        Workflow blueprint
    );

    [[nodiscard]] std::uint32_t schema_version() const noexcept;
    [[nodiscard]] std::string_view id() const noexcept;
    [[nodiscard]] std::string_view version() const noexcept;
    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] std::string_view description() const noexcept;
    [[nodiscard]] const Workflow& blueprint() const noexcept;

private:
    std::uint32_t schema_version_;
    std::string id_;
    std::string version_;
    std::string name_;
    std::string description_;
    Workflow blueprint_;
};

}  // namespace biocore::domain
