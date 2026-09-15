#include "biocore/application/variant_export.hpp"

#include "biocore/application/build_info.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace biocore::application {
namespace {

[[nodiscard]] bool is_blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
               return std::isspace(static_cast<unsigned char>(character)) != 0;
           });
}

void require_text(
    const std::string_view value,
    const char* field,
    const std::size_t maximum_length
) {
    if (is_blank(value) || value.size() > maximum_length ||
        value.find('\0') != std::string_view::npos ||
        std::ranges::any_of(value, [](const char character) {
            return static_cast<unsigned char>(character) < 0x20U;
        })) {
        throw std::invalid_argument(std::string{field} + " is invalid");
    }
}

[[nodiscard]] bool sha256_digest(const std::string_view value) noexcept {
    if (value.size() != 64U) {
        return false;
    }
    return std::ranges::all_of(value, [](const char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f') ||
               (character >= 'A' && character <= 'F');
    });
}

[[nodiscard]] std::string lowercase_digest(std::string value) {
    std::ranges::transform(value, value.begin(), [](const char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    return value;
}

void validate_database(const VariantExportDatabaseProvenance& database) {
    require_text(database.database_id, "Variant export database id", 256U);
    require_text(database.display_name, "Variant export database display name", 512U);
    require_text(database.version, "Variant export database version", 128U);
    if (database.schema_version == 0U) {
        throw std::invalid_argument("Variant export database schema version must be positive");
    }
    domain::validate_reference_assembly_identity(database.assembly);
    if (!sha256_digest(database.source_sha256)) {
        throw std::invalid_argument("Variant export database SHA-256 is invalid");
    }
}

[[nodiscard]] VariantExportDatabaseProvenance database_provenance(
    const domain::AnnotationDatabaseProvenance& provenance
) {
    return VariantExportDatabaseProvenance{
        .database_id = provenance.database_id,
        .display_name = provenance.display_name,
        .version = provenance.version,
        .schema_version = provenance.schema_version,
        .assembly = provenance.assembly,
        .source_sha256 = lowercase_digest(provenance.source_sha256),
    };
}

void merge_database(
    std::vector<VariantExportDatabaseProvenance>& databases,
    VariantExportDatabaseProvenance candidate
) {
    validate_database(candidate);
    candidate.source_sha256 = lowercase_digest(std::move(candidate.source_sha256));
    const auto found = std::find_if(
        databases.begin(),
        databases.end(),
        [&](const VariantExportDatabaseProvenance& existing) {
            return existing.database_id == candidate.database_id &&
                   existing.version == candidate.version;
        }
    );
    if (found == databases.end()) {
        databases.push_back(std::move(candidate));
        return;
    }
    if (*found != candidate) {
        throw std::invalid_argument(
            "Variant export database provenance conflicts for the same database id/version"
        );
    }
}

void collect_annotation_databases(
    const domain::VariantAnalysisWorkspace& workspace,
    const std::span<const std::size_t> indices,
    const bool all_variants,
    std::vector<VariantExportDatabaseProvenance>& databases
) {
    const std::size_t count = all_variants ? workspace.size() : indices.size();
    for (std::size_t export_index = 0U; export_index < count; ++export_index) {
        const std::size_t variant_index = all_variants ? export_index : indices[export_index];
        const auto* annotation = workspace.annotation(variant_index);
        if (annotation == nullptr) {
            continue;
        }
        for (const auto& alternate : annotation->alternates) {
            for (const auto& allele : alternate.allele_annotations) {
                merge_database(databases, database_provenance(allele.provenance));
            }
            for (const auto& feature : alternate.feature_annotations) {
                merge_database(databases, database_provenance(feature.provenance));
            }
        }
    }
}

[[nodiscard]] auto coordinate_key(const domain::VariantWorkspaceRowDto& row) {
    return std::tie(
        row.contig_id,
        row.start,
        row.end,
        row.reference_allele,
        row.alternate_allele,
        row.variant_index
    );
}

}  // namespace

std::string_view to_string(const VariantExportFormat format) noexcept {
    switch (format) {
        case VariantExportFormat::tsv:
            return "tsv";
        case VariantExportFormat::csv:
            return "csv";
        case VariantExportFormat::json:
            return "json";
        case VariantExportFormat::vcf:
            return "vcf";
    }
    return "unknown";
}

void validate_variant_export_provenance(const VariantExportProvenance& provenance) {
    if (provenance.schema_version != VariantExportProvenance::current_schema_version) {
        throw std::invalid_argument("Variant export provenance schema version is unsupported");
    }
    if (provenance.producer_name != "OpenGenesis-BioCore") {
        throw std::invalid_argument("Variant export producer name must be OpenGenesis-BioCore");
    }
    require_text(provenance.producer_version, "Variant export producer version", 128U);
    if (provenance.producer_version != BuildInfo::version()) {
        throw std::invalid_argument("Variant export producer version does not match this build");
    }
    require_text(provenance.generated_at_utc, "Variant export generation timestamp", 128U);
    require_text(provenance.pipeline.pipeline_id, "Variant export pipeline id", 256U);
    require_text(provenance.pipeline.pipeline_version, "Variant export pipeline version", 128U);
    if (provenance.pipeline.job_id.has_value()) {
        require_text(*provenance.pipeline.job_id, "Variant export job id", 128U);
    }
    if (provenance.pipeline.job_revision.has_value() && *provenance.pipeline.job_revision < 0) {
        throw std::invalid_argument("Variant export job revision must be non-negative");
    }
    if (provenance.pipeline.attempt_number.has_value() && *provenance.pipeline.attempt_number < 1) {
        throw std::invalid_argument("Variant export attempt number must be positive");
    }
    require_text(provenance.reference.reference_id, "Variant export reference id", 256U);
    domain::validate_reference_assembly_identity(provenance.reference.assembly);
    if (!sha256_digest(provenance.reference.source_sha256)) {
        throw std::invalid_argument("Variant export reference SHA-256 is invalid");
    }
    for (const auto& database : provenance.databases) {
        validate_database(database);
    }
}

std::size_t VariantExportPlan::row_count() const noexcept {
    if (workspace_ == nullptr) {
        return 0U;
    }
    return all_variants_ ? workspace_->size() : variant_indices_.size();
}

std::size_t VariantExportPlan::source_variant_index(const std::size_t export_index) const {
    if (workspace_ == nullptr || export_index >= row_count()) {
        throw std::out_of_range("Variant export row index is out of range");
    }
    return all_variants_ ? export_index : variant_indices_[export_index];
}

const domain::VariantWorkspaceRowDto& VariantExportPlan::row(const std::size_t export_index) const {
    return workspace_->export_row(source_variant_index(export_index));
}

VariantExportPlan build_variant_export_plan(
    const domain::VariantAnalysisWorkspace& workspace,
    VariantExportProvenance provenance,
    const std::span<const std::size_t> variant_indices
) {
    validate_variant_export_provenance(provenance);
    if (workspace.assembly() != provenance.reference.assembly) {
        throw std::invalid_argument(
            "Variant export reference assembly does not match the workspace assembly"
        );
    }

    provenance.reference.source_sha256 = lowercase_digest(
        std::move(provenance.reference.source_sha256)
    );
    std::vector<VariantExportDatabaseProvenance> normalized_databases;
    normalized_databases.reserve(provenance.databases.size());
    for (auto& database : provenance.databases) {
        merge_database(normalized_databases, std::move(database));
    }
    provenance.databases = std::move(normalized_databases);

    VariantExportPlan plan;
    plan.workspace_ = &workspace;
    plan.provenance_ = std::move(provenance);
    plan.all_variants_ = variant_indices.empty();

    if (!plan.all_variants_) {
        plan.variant_indices_.assign(variant_indices.begin(), variant_indices.end());
        std::unordered_set<std::size_t> unique;
        unique.reserve(plan.variant_indices_.size());
        for (const auto index : plan.variant_indices_) {
            if (index >= workspace.size()) {
                throw std::out_of_range("Variant export selection contains an out-of-range variant index");
            }
            if (!unique.insert(index).second) {
                throw std::invalid_argument("Variant export selection contains a duplicate variant index");
            }
        }
        std::ranges::sort(plan.variant_indices_, [&](const std::size_t left, const std::size_t right) {
            return coordinate_key(workspace.export_row(left)) < coordinate_key(workspace.export_row(right));
        });
    }

    collect_annotation_databases(
        workspace,
        plan.variant_indices_,
        plan.all_variants_,
        plan.provenance_.databases
    );
    std::ranges::sort(
        plan.provenance_.databases,
        [](const VariantExportDatabaseProvenance& left, const VariantExportDatabaseProvenance& right) {
            return std::tie(left.database_id, left.version, left.source_sha256) <
                   std::tie(right.database_id, right.version, right.source_sha256);
        }
    );
    validate_variant_export_provenance(plan.provenance_);
    return plan;
}

}  // namespace biocore::application
