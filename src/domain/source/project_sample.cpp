#include "biocore/domain/project_sample.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

#include "biocore/domain/project.hpp"

namespace biocore::domain {
namespace {

[[nodiscard]] bool blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
        return std::isspace(static_cast<unsigned char>(character)) != 0;
    });
}

void validate_required(const std::string_view value, const std::size_t maximum,
                       const char* const field) {
    if (blank(value) || value.size() > maximum ||
        value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument{std::string{"Invalid project sample "} + field};
    }
}

void validate_optional(const std::string_view value, const std::size_t maximum,
                       const char* const field) {
    if (value.size() > maximum || value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument{std::string{"Invalid project sample "} + field};
    }
}

}  // namespace

ProjectSample::ProjectSample(std::string project_id, std::string sample_id,
                             std::string display_name, std::string group_label)
    : project_id_{std::move(project_id)},
      sample_id_{std::move(sample_id)},
      display_name_{std::move(display_name)},
      group_label_{std::move(group_label)} {
    validate_required(project_id_, Project::maximum_id_length, "project id");
    validate_required(sample_id_, maximum_id_bytes, "sample id");
    validate_optional(display_name_, maximum_display_name_bytes, "display name");
    validate_optional(group_label_, maximum_group_label_bytes, "group label");
}

std::string_view ProjectSample::project_id() const noexcept { return project_id_; }
std::string_view ProjectSample::sample_id() const noexcept { return sample_id_; }
std::string_view ProjectSample::display_name() const noexcept { return display_name_; }
std::string_view ProjectSample::group_label() const noexcept { return group_label_; }

}  // namespace biocore::domain
