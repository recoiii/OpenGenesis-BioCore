#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace biocore::domain {

class ProjectSample final {
public:
    static constexpr std::size_t maximum_id_bytes = 128U;
    static constexpr std::size_t maximum_display_name_bytes = 255U;
    static constexpr std::size_t maximum_group_label_bytes = 128U;

    ProjectSample(std::string project_id, std::string sample_id,
                  std::string display_name, std::string group_label);

    [[nodiscard]] std::string_view project_id() const noexcept;
    [[nodiscard]] std::string_view sample_id() const noexcept;
    [[nodiscard]] std::string_view display_name() const noexcept;
    [[nodiscard]] std::string_view group_label() const noexcept;

    friend bool operator==(const ProjectSample&, const ProjectSample&) = default;

private:
    std::string project_id_;
    std::string sample_id_;
    std::string display_name_;
    std::string group_label_;
};

}  // namespace biocore::domain
