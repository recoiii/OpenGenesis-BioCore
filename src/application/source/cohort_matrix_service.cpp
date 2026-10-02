#include "biocore/application/cohort_matrix_service.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "biocore/application/cohort_analysis_selection_service.hpp"
#include "biocore/application/generated_output_artifact.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_reference_genome_reader.hpp"
#include "biocore/application/i_result_artifact_reader.hpp"
#include "biocore/domain/reference_database.hpp"
#include "biocore/domain/vcf_ingestion.hpp"
#include "biocore/domain/variant_types.hpp"

namespace biocore::application {
namespace {

[[nodiscard]] domain::ReferenceAssemblyIdentity assembly_identity(
    const CohortPinnedReference& reference
) {
    return {
        .assembly = reference.assembly,
        .custom_id = reference.custom_assembly_id.value_or(std::string{}),
    };
}

[[nodiscard]] domain::ContigTable build_target_contigs(
    const CohortPinnedReference& pinned,
    const domain::ReferenceGenome& genome
) {
    std::vector<std::string> order;
    std::set<std::string, std::less<>> seen;
    std::map<std::string, std::vector<std::string>, std::less<>> aliases;

    for (const auto& contig : pinned.contigs) {
        const auto id = genome.contigs().resolve(contig.canonical_name);
        if (!id.has_value()) {
            throw std::runtime_error{
                "Pinned reference contig is absent from the reverified reference genome"
            };
        }
        const auto canonical = genome.contigs().canonical_name(*id);
        if (!canonical.has_value()) {
            throw std::logic_error{"Reverified reference canonical contig disappeared"};
        }
        const std::string key{*canonical};
        if (seen.emplace(key).second) order.push_back(key);
        if (contig.canonical_name != key) {
            aliases[key].push_back(contig.canonical_name);
        }
    }

    for (const auto& mapping : pinned.aliases) {
        const auto id = genome.contigs().resolve(mapping.canonical_name);
        if (!id.has_value()) {
            throw std::runtime_error{
                "Pinned reference alias target is absent from the reverified reference genome"
            };
        }
        const auto canonical = genome.contigs().canonical_name(*id);
        if (!canonical.has_value()) {
            throw std::logic_error{"Reverified reference alias target disappeared"};
        }
        aliases[std::string{*canonical}].push_back(mapping.alias);
    }

    domain::ContigTableBuilder builder{pinned.assembly};
    for (const auto& canonical : order) {
        auto values = aliases[canonical];
        std::ranges::sort(values);
        values.erase(std::unique(values.begin(), values.end()), values.end());
        values.erase(
            std::remove(values.begin(), values.end(), canonical),
            values.end()
        );
        builder.add_contig(canonical, std::move(values));
    }
    return builder.build();
}

[[nodiscard]] std::string rewrite_explicit_contig_aliases(
    const std::string_view text,
    const std::vector<CohortReferenceAlias>& aliases
) {
    if (aliases.empty()) return std::string{text};

    std::map<std::string_view, std::string_view, std::less<>> mapping;
    for (const auto& alias : aliases) {
        mapping.emplace(alias.alias, alias.canonical_name);
    }

    std::string output;
    output.reserve(text.size());
    std::size_t offset = 0U;
    while (offset < text.size()) {
        const auto newline = text.find('\n', offset);
        const auto length = newline == std::string_view::npos
            ? text.size() - offset
            : newline - offset;
        const auto line = text.substr(offset, length);

        if (!line.empty() && line.front() != '#') {
            const auto tab = line.find('\t');
            if (tab == std::string_view::npos) {
                throw std::invalid_argument{"VCF data row has no CHROM separator"};
            }
            const auto chrom = line.substr(0U, tab);
            const auto found = mapping.find(chrom);
            if (found != mapping.end()) {
                output.append(found->second);
                output.append(line.substr(tab));
            } else {
                output.append(line);
            }
        } else {
            output.append(line);
        }

        if (newline == std::string_view::npos) break;
        output.push_back('\n');
        offset = newline + 1U;
    }
    return output;
}

void validate_cohort_variant_contract(
    const domain::VcfIngestionResult& ingestion
) {
    for (const auto& record : ingestion.records) {
        const auto reference_length = record.variant.reference.sequence.size();
        for (const auto& alternate : record.variant.alternates.values()) {
            if (alternate.symbolic) {
                throw std::invalid_argument{
                    "Cohort v1 does not accept symbolic or structural ALT alleles"
                };
            }

            const auto alternate_length = alternate.sequence.size();
            switch (alternate.type) {
                case domain::VariantType::snv:
                    if (reference_length != 1U || alternate_length != 1U) {
                        throw std::invalid_argument{
                            "Cohort v1 SNV representation is invalid"
                        };
                    }
                    break;
                case domain::VariantType::insertion:
                case domain::VariantType::deletion: {
                    const auto difference = reference_length > alternate_length
                        ? reference_length - alternate_length
                        : alternate_length - reference_length;
                    if (difference == 0U || difference > 50U) {
                        throw std::invalid_argument{
                            "Cohort v1 indel length must be between 1 and 50 bases"
                        };
                    }
                    break;
                }
                case domain::VariantType::mnv:
                case domain::VariantType::delins_complex:
                case domain::VariantType::unknown:
                    throw std::invalid_argument{
                        "Cohort v1 accepts only literal SNVs and small insertions/deletions"
                    };
            }
        }
    }
}

[[nodiscard]] std::size_t require_unique_sample_column(
    const domain::VcfIngestionResult& ingestion,
    const std::string_view sample_name
) {
    std::optional<std::size_t> result;
    for (std::size_t index = 0U; index < ingestion.header.sample_names.size(); ++index) {
        if (ingestion.header.sample_names[index] != sample_name) continue;
        if (result.has_value()) {
            throw std::invalid_argument{
                "Selected VCF sample column is duplicated after exact-byte revalidation"
            };
        }
        result = index;
    }
    if (!result.has_value()) {
        throw std::invalid_argument{
            "Selected VCF sample column disappeared after exact-byte revalidation"
        };
    }
    return *result;
}

[[nodiscard]] domain::VcfIngestionResult project_sample(
    const domain::VcfIngestionResult& ingestion,
    const std::size_t source_sample_index,
    const std::string& project_sample_id
) {
    domain::VcfIngestionResult projected;
    projected.header = ingestion.header;
    projected.header.sample_names = {project_sample_id};
    projected.records.reserve(ingestion.records.size());

    for (const auto& record : ingestion.records) {
        if (source_sample_index >= record.samples.size()) {
            throw std::logic_error{
                "Parsed VCF record sample cardinality no longer matches its header"
            };
        }
        auto copy = record;
        copy.samples = {record.samples[source_sample_index]};
        projected.records.push_back(std::move(copy));
    }
    return projected;
}

[[nodiscard]] std::optional<GeneratedOutputArtifact> resolve_source_artifact(
    IManagedFileRepository& files,
    const CohortPinnedVcfSource& source
) {
    auto artifact = files.find_generated_output(
        source.job_id, source.step_id, source.output_port
    );
    if (!artifact.has_value() ||
        artifact->file.id() != source.managed_file_id ||
        artifact->file.checksum_algorithm() != std::optional<std::string>{"sha256"} ||
        artifact->file.checksum_value() != std::optional<std::string>{source.sha256}) {
        return std::nullopt;
    }
    return artifact;
}

[[nodiscard]] CohortMatrixDispatchPlan dispatch_plan(
    const CohortAnalysisSelectionPreview& selection
) {
    if (!selection.reference.has_value()) {
        throw std::logic_error{"Ready cohort selection has no pinned reference"};
    }

    CohortMatrixDispatchPlan plan{
        .project_id = selection.project_id,
        .cohort_id = selection.cohort_id,
        .cohort_revision = selection.cohort_revision,
        .stage_id = "matrix",
        .native_module_id = "org.biocore.cohort.matrix",
        .normalization_contract_version =
            selection.reference->normalization_contract_version,
        .reference_file_id = selection.reference->managed_file_id,
        .reference_sha256 = selection.reference->sha256,
        .sources = {},
        .maximum_samples = CohortMatrixBudget::maximum_samples,
        .maximum_sources = CohortMatrixBudget::maximum_sources,
        .maximum_normalized_alleles = CohortMatrixBudget::maximum_normalized_alleles,
        .maximum_observations = CohortMatrixBudget::maximum_observations,
    };
    plan.sources.reserve(selection.sources.size());
    for (const auto& source : selection.sources) {
        plan.sources.push_back({
            .project_sample_id = source.project_sample_id,
            .managed_file_id = source.managed_file_id,
            .sha256 = source.sha256,
            .vcf_sample_name = source.vcf_sample_name,
        });
    }
    std::ranges::sort(plan.sources, [](const auto& left, const auto& right) {
        return std::tie(
            left.project_sample_id,
            left.managed_file_id,
            left.vcf_sample_name
        ) < std::tie(
            right.project_sample_id,
            right.managed_file_id,
            right.vcf_sample_name
        );
    });
    return plan;
}

}  // namespace

CohortMatrixService::CohortMatrixService(
    CohortAnalysisSelectionService& selection_service,
    IManagedFileRepository& files,
    IResultArtifactReader& artifact_reader,
    IReferenceGenomeReader& reference_reader
) noexcept
    : selection_service_{selection_service},
      files_{files},
      artifact_reader_{artifact_reader},
      reference_reader_{reference_reader} {}

CohortMatrixBuildResult CohortMatrixService::build(
    const CohortAnalysisSelectionRequest& request
) {
    auto selection = selection_service_.preview(request);
    if (!selection.ready || !selection.reference.has_value()) {
        throw std::invalid_argument{
            "Cohort matrix construction requires a ready revalidated selection"
        };
    }

    const auto& pinned_reference = *selection.reference;
    const auto reference_file = files_.find_by_id(pinned_reference.managed_file_id);
    if (!reference_file.has_value()) {
        throw std::runtime_error{"Pinned reference disappeared before matrix construction"};
    }

    auto reference_read = reference_reader_.read_verified_genome(
        *reference_file,
        pinned_reference.assembly,
        CohortAnalysisSelectionService::maximum_reference_bytes
    );
    if (reference_read.status != ReferenceGenomeReadStatus::verified ||
        !reference_read.genome.has_value() ||
        !reference_read.verified_sha256.has_value() ||
        *reference_read.verified_sha256 != pinned_reference.sha256 ||
        reference_read.verified_size_bytes != pinned_reference.size_bytes) {
        throw std::runtime_error{
            "Pinned reference bytes changed before matrix construction"
        };
    }

    const auto assembly = assembly_identity(pinned_reference);
    domain::validate_reference_assembly_identity(assembly);
    auto target_contigs = build_target_contigs(
        pinned_reference, *reference_read.genome
    );

    std::map<std::string, domain::VcfIngestionResult, std::less<>> parsed;
    std::map<std::string, GeneratedOutputArtifact, std::less<>> artifacts;

    for (const auto& source : selection.sources) {
        if (parsed.contains(source.managed_file_id)) continue;

        const auto artifact = resolve_source_artifact(files_, source);
        if (!artifact.has_value()) {
            throw std::runtime_error{
                "Selected VCF artifact changed before matrix construction"
            };
        }
        const auto read = artifact_reader_.read_verified_text(
            *artifact,
            CohortAnalysisSelectionService::maximum_vcf_bytes
        );
        if (read.status != ResultArtifactReadStatus::verified ||
            !read.text.has_value() ||
            !read.verified_sha256.has_value() ||
            *read.verified_sha256 != source.sha256) {
            throw std::runtime_error{
                "Selected VCF bytes changed before matrix construction"
            };
        }

        const auto rewritten = rewrite_explicit_contig_aliases(
            *read.text, pinned_reference.aliases
        );
        std::istringstream input{rewritten};
        auto ingestion = domain::ingest_vcf(input, *reference_read.genome);
        validate_cohort_variant_contract(ingestion);

        parsed.emplace(source.managed_file_id, std::move(ingestion));
        artifacts.emplace(source.managed_file_id, *artifact);
    }

    validate_cohort_matrix_budget(
        selection.sources.size(),
        parsed.size(),
        0U,
        0U
    );

    std::vector<domain::VcfIngestionResult> projected;
    projected.reserve(selection.sources.size());
    std::vector<domain::MultiSampleMatrixSource> matrix_sources;
    matrix_sources.reserve(selection.sources.size());

    for (const auto& source : selection.sources) {
        const auto found = parsed.find(source.managed_file_id);
        if (found == parsed.end()) {
            throw std::logic_error{"Parsed cohort VCF source disappeared"};
        }
        const auto column = require_unique_sample_column(
            found->second, source.vcf_sample_name
        );
        projected.push_back(
            project_sample(found->second, column, source.project_sample_id)
        );
        const auto source_id =
            source.managed_file_id + ":" + source.project_sample_id;
        matrix_sources.push_back({
            .source_id = source_id,
            .assembly = assembly,
            .contigs = &reference_read.genome->contigs(),
            .variants = &projected.back(),
        });
    }

    domain::MultiSampleMatrixOptions options{
        .maximum_sources = CohortMatrixBudget::maximum_sources,
        .maximum_samples = CohortMatrixBudget::maximum_samples,
        .maximum_variant_alleles = CohortMatrixBudget::maximum_normalized_alleles,
        .maximum_observations = CohortMatrixBudget::maximum_observations,
    };
    auto matrix = domain::build_multi_sample_matrix(
        assembly,
        std::move(target_contigs),
        matrix_sources,
        options
    );
    validate_cohort_matrix_budget(
        matrix.sample_count(),
        parsed.size(),
        matrix.variant_count(),
        matrix.observation_count()
    );

    return {
        .selection = std::move(selection),
        .dispatch = dispatch_plan(selection),
        .matrix = std::move(matrix),
        .parsed_vcf_artifacts = parsed.size(),
    };
}

}  // namespace biocore::application
