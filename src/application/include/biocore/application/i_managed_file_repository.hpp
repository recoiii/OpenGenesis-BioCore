#pragma once

#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "biocore/application/generated_output_artifact.hpp"
#include "biocore/domain/managed_file.hpp"

namespace biocore::application {

class IManagedFileRepository {
public:
    virtual ~IManagedFileRepository() = default;

    // Returns false when a unique identifier or relative project path conflicts.
    // Other persistence failures are reported as exceptions by the concrete adapter.
    virtual bool add(const domain::ManagedFile& file) = 0;
    [[nodiscard]] virtual std::optional<domain::ManagedFile> find_by_id(
        std::string_view file_id
    ) = 0;
    [[nodiscard]] virtual std::optional<domain::ManagedFile> find_by_relative_project_path(
        std::string_view relative_project_path
    ) = 0;
    [[nodiscard]] virtual std::vector<domain::ManagedFile> list() = 0;

    // Generated outputs are persisted atomically with their provenance. Returns false when
    // a file/provenance uniqueness conflict occurs; adapters must not leave a partial row.
    virtual bool add_generated_output(
        const domain::ManagedFile& file,
        const GeneratedOutputProvenance& provenance
    ) = 0;

    // Every artifact in the span belongs to one completed pipeline step. Concrete adapters
    // must persist the entire span in one transaction: either every managed-file/provenance
    // pair is committed or none of them is. An empty span is invalid.
    virtual bool add_generated_outputs_batch(
        std::span<const GeneratedOutputArtifact> artifacts
    ) = 0;
    [[nodiscard]] virtual std::optional<GeneratedOutputArtifact> find_generated_output(
        std::string_view job_id,
        std::string_view step_id,
        std::string_view output_port
    ) = 0;
    [[nodiscard]] virtual std::vector<GeneratedOutputArtifact> list_generated_outputs(
        std::string_view job_id
    ) = 0;
    // Returns the highest durable step progress stored atomically with generated-output batches.
    [[nodiscard]] virtual std::optional<double> latest_generated_output_progress(
        std::string_view job_id
    ) {
        static_cast<void>(job_id);
        return std::nullopt;
    }
};

}  // namespace biocore::application
