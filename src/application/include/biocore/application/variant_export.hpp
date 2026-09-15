#pragma once

#include "biocore/domain/variant_workspace.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace biocore::application {

enum class VariantExportFormat : std::uint8_t {
    tsv,
    csv,
    json,
    vcf
};

[[nodiscard]] std::string_view to_string(VariantExportFormat format) noexcept;

struct VariantExportPipelineProvenance final {
    std::string pipeline_id;
    std::string pipeline_version;
    std::optional<std::string> job_id;
    std::optional<std::int64_t> job_revision;
    std::optional<std::int64_t> attempt_number;
};

struct VariantExportReferenceProvenance final {
    std::string reference_id;
    domain::ReferenceAssemblyIdentity assembly;
    std::string source_sha256;
};

struct VariantExportDatabaseProvenance final {
    std::string database_id;
    std::string display_name;
    std::string version;
    std::uint32_t schema_version{0U};
    domain::ReferenceAssemblyIdentity assembly;
    std::string source_sha256;

    friend bool operator==(
        const VariantExportDatabaseProvenance&,
        const VariantExportDatabaseProvenance&
    ) = default;
};

struct VariantExportProvenance final {
    static constexpr std::uint32_t current_schema_version = 1U;

    std::uint32_t schema_version{current_schema_version};
    std::string producer_name{"OpenGenesis-BioCore"};
    std::string producer_version;
    std::string generated_at_utc;
    VariantExportPipelineProvenance pipeline;
    VariantExportReferenceProvenance reference;
    std::vector<VariantExportDatabaseProvenance> databases;
};

void validate_variant_export_provenance(const VariantExportProvenance& provenance);

class VariantExportPlan final {
public:
    VariantExportPlan() = default;

    [[nodiscard]] const VariantExportProvenance& provenance() const noexcept {
        return provenance_;
    }
    [[nodiscard]] std::size_t row_count() const noexcept;
    [[nodiscard]] const domain::VariantWorkspaceRowDto& row(std::size_t export_index) const;
    [[nodiscard]] std::size_t source_variant_index(std::size_t export_index) const;

private:
    friend VariantExportPlan build_variant_export_plan(
        const domain::VariantAnalysisWorkspace&,
        VariantExportProvenance,
        std::span<const std::size_t>
    );

    const domain::VariantAnalysisWorkspace* workspace_{nullptr};
    VariantExportProvenance provenance_;
    bool all_variants_{true};
    std::vector<std::size_t> variant_indices_;
};

[[nodiscard]] VariantExportPlan build_variant_export_plan(
    const domain::VariantAnalysisWorkspace& workspace,
    VariantExportProvenance provenance,
    std::span<const std::size_t> variant_indices = {}
);

}  // namespace biocore::application
