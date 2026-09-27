#include "biocore/domain/project_research_metadata.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>
#include "biocore/domain/project.hpp"

namespace biocore::domain {
namespace {
void validate_text(const std::string_view value, const std::size_t limit,
                   const bool required) {
    if (value.size() > limit || value.find('\0') != std::string_view::npos ||
        (required && (value.empty() || std::ranges::all_of(value, [](const char c) {
            return std::isspace(static_cast<unsigned char>(c)) != 0;
        })))) {
        throw std::invalid_argument{"Invalid project research metadata text"};
    }
}
}

ProjectResearchMetadata::ProjectResearchMetadata(
    std::string project_id, std::string description, std::string organism,
    const std::int64_t revision, std::string updated_at_utc)
    : project_id_{std::move(project_id)}, description_{std::move(description)},
      organism_{std::move(organism)}, revision_{revision},
      updated_at_utc_{std::move(updated_at_utc)} {
    validate_text(project_id_, Project::maximum_id_length, true);
    validate_text(description_, maximum_description_bytes, false);
    validate_text(organism_, maximum_organism_bytes, false);
    // Retain the existing Project timestamp contract: nonblank, NUL-free text.
    validate_text(updated_at_utc_, updated_at_utc_.max_size(), true);
    if (revision_ < 0) throw std::invalid_argument{"Metadata revision must be nonnegative"};
}

std::string_view ProjectResearchMetadata::project_id() const noexcept { return project_id_; }
std::string_view ProjectResearchMetadata::description() const noexcept { return description_; }
std::string_view ProjectResearchMetadata::organism() const noexcept { return organism_; }
std::int64_t ProjectResearchMetadata::revision() const noexcept { return revision_; }
std::string_view ProjectResearchMetadata::updated_at_utc() const noexcept { return updated_at_utc_; }
}  // namespace biocore::domain
