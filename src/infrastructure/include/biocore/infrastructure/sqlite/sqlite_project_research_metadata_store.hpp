#pragma once

#include "biocore/application/i_project_research_metadata_store.hpp"

namespace biocore::infrastructure::sqlite {
class SqliteConnection;
class SqliteProjectResearchMetadataStore final : public application::IProjectResearchMetadataStore {
public:
    explicit SqliteProjectResearchMetadataStore(SqliteConnection& connection) noexcept;
    [[nodiscard]] std::optional<domain::ProjectResearchMetadata> find(
        std::string_view project_id) override;
    bool update(const domain::ProjectResearchMetadata& metadata,
                std::int64_t expected_revision) override;
private:
    SqliteConnection& connection_;
};
}  // namespace biocore::infrastructure::sqlite
