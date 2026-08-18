#pragma once

#include "biocore/application/i_managed_file_repository.hpp"

namespace biocore::infrastructure::sqlite {

class SqliteConnection;

class SqliteManagedFileRepository final : public application::IManagedFileRepository {
public:
    explicit SqliteManagedFileRepository(SqliteConnection& connection) noexcept;

    bool add(const domain::ManagedFile& file) override;
    [[nodiscard]] std::optional<domain::ManagedFile> find_by_id(
        std::string_view file_id
    ) override;
    [[nodiscard]] std::optional<domain::ManagedFile> find_by_relative_project_path(
        std::string_view relative_project_path
    ) override;
    [[nodiscard]] std::vector<domain::ManagedFile> list() override;
    bool add_generated_output(
        const domain::ManagedFile& file,
        const application::GeneratedOutputProvenance& provenance
    ) override;
    bool add_generated_outputs_batch(
        std::span<const application::GeneratedOutputArtifact> artifacts
    ) override;
    [[nodiscard]] std::optional<application::GeneratedOutputArtifact> find_generated_output(
        std::string_view job_id,
        std::string_view step_id,
        std::string_view output_port
    ) override;
    [[nodiscard]] std::vector<application::GeneratedOutputArtifact> list_generated_outputs(
        std::string_view job_id
    ) override;
    [[nodiscard]] std::optional<double> latest_generated_output_progress(
        std::string_view job_id
    ) override;

private:
    SqliteConnection& connection_;
};

}  // namespace biocore::infrastructure::sqlite
