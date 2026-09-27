#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include "biocore/domain/project_research_metadata.hpp"

namespace biocore::application {
class IProjectResearchMetadataStore {
public:
    virtual ~IProjectResearchMetadataStore() = default;
    // Project identity must match the singleton in this project's database.
    [[nodiscard]] virtual std::optional<domain::ProjectResearchMetadata> find(
        std::string_view project_id) = 0;
    // Atomic compare-and-swap. False means missing/wrong project or stale revision.
    // The supplied value must carry expected_revision + 1; no implicit creation.
    virtual bool update(const domain::ProjectResearchMetadata& metadata,
                        std::int64_t expected_revision) = 0;
};
}  // namespace biocore::application
