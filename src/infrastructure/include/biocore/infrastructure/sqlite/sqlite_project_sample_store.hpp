#pragma once

#include "biocore/application/i_project_sample_store.hpp"

namespace biocore::infrastructure::sqlite {

class SqliteConnection;

class SqliteProjectSampleStore final : public application::IProjectSampleStore {
public:
    explicit SqliteProjectSampleStore(SqliteConnection& connection) noexcept;

    [[nodiscard]] std::optional<std::vector<domain::ProjectSample>> list(
        std::string_view project_id) override;

    application::SampleBatchAddResult add_batch(
        std::string_view project_id,
        std::span<const domain::ProjectSample> samples) override;

private:
    SqliteConnection& connection_;
};

}  // namespace biocore::infrastructure::sqlite
