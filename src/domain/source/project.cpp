#include "biocore/domain/project.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace biocore::domain {
namespace {

[[nodiscard]] bool is_blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
               return std::isspace(static_cast<unsigned char>(character)) != 0;
           });
}

void require_non_blank(const std::string_view value, const char* const field_name) {
    if (is_blank(value)) {
        throw std::invalid_argument(std::string{field_name} + " must not be blank");
    }

    if (value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(std::string{field_name} + " must not contain NUL characters");
    }
}

}  // namespace

Project::Project(
    std::string id,
    std::string name,
    std::string root_path,
    std::string created_at_utc,
    std::string updated_at_utc
)
    : id_{std::move(id)},
      name_{std::move(name)},
      root_path_{std::move(root_path)},
      created_at_utc_{std::move(created_at_utc)},
      updated_at_utc_{std::move(updated_at_utc)} {
    require_non_blank(id_, "Project id");
    require_non_blank(name_, "Project name");
    require_non_blank(root_path_, "Project root path");
    require_non_blank(created_at_utc_, "Project creation timestamp");
    require_non_blank(updated_at_utc_, "Project update timestamp");

    if (id_.size() > maximum_id_length) {
        throw std::invalid_argument("Project id exceeds the maximum length");
    }

    if (name_.size() > maximum_name_length) {
        throw std::invalid_argument("Project name exceeds the maximum length");
    }
}

std::string_view Project::id() const noexcept {
    return id_;
}

std::string_view Project::name() const noexcept {
    return name_;
}

std::string_view Project::root_path() const noexcept {
    return root_path_;
}

std::string_view Project::created_at_utc() const noexcept {
    return created_at_utc_;
}

std::string_view Project::updated_at_utc() const noexcept {
    return updated_at_utc_;
}

}  // namespace biocore::domain
