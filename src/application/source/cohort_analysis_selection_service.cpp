#include "biocore/application/cohort_analysis_selection_service.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "biocore/application/batch_execution.hpp"
#include "biocore/application/batch_plan.hpp"
#include "biocore/application/cohort_registry.hpp"
#include "biocore/application/generated_output_artifact.hpp"
#include "biocore/application/i_batch_execution_store.hpp"
#include "biocore/application/i_batch_plan_store.hpp"
#include "biocore/application/i_cohort_registry_store.hpp"
#include "biocore/application/i_input_file_storage.hpp"
#include "biocore/application/i_job_repository.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_reference_manifest_reader.hpp"
#include "biocore/application/i_result_artifact_reader.hpp"
#include "biocore/domain/job_status.hpp"

namespace biocore::application {
namespace {

[[nodiscard]] bool blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
        return std::isspace(static_cast<unsigned char>(character)) != 0;
    });
}

[[nodiscard]] bool valid_sha256(const std::string_view value) noexcept {
    return value.size() == 64U && std::ranges::all_of(value, [](const char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
    });
}

void add_issue(
    CohortAnalysisSelectionPreview& preview,
    std::string code,
    std::string sample_id,
    std::string artifact_id,
    std::string message
) {
    preview.issues.push_back({
        .code = std::move(code),
        .sample_id = std::move(sample_id),
        .artifact_id = std::move(artifact_id),
        .message = std::move(message),
    });
}

[[nodiscard]] const CohortMemberSnapshot* find_member(
    const CohortRevision& revision,
    const std::string_view sample_id
) {
    const auto found = std::ranges::find_if(revision.members, [sample_id](const auto& member) {
        return member.sample_id == sample_id;
    });
    return found == revision.members.end() ? nullptr : &*found;
}

[[nodiscard]] const ApprovedBatchSamplePlan* find_plan_sample(
    const ApprovedBatchPlan& plan,
    const std::string_view sample_id
) {
    const auto found = std::ranges::find_if(plan.samples, [sample_id](const auto& sample) {
        return sample.sample_id == sample_id;
    });
    return found == plan.samples.end() ? nullptr : &*found;
}

[[nodiscard]] const BatchPlanNodeSnapshot* find_node(
    const ApprovedBatchSamplePlan& sample,
    const std::string_view node_id
) {
    const auto found = std::ranges::find_if(sample.nodes, [node_id](const auto& node) {
        return node.node_id == node_id;
    });
    return found == sample.nodes.end() ? nullptr : &*found;
}

[[nodiscard]] const BatchPlanOutputSnapshot* find_output(
    const BatchPlanNodeSnapshot& node,
    const std::string_view port
) {
    const auto found = std::ranges::find_if(node.outputs, [port](const auto& output) {
        return output.port_name == port;
    });
    return found == node.outputs.end() ? nullptr : &*found;
}

[[nodiscard]] std::vector<std::string_view> split(
    const std::string_view value,
    const char delimiter
) {
    std::vector<std::string_view> result;
    std::size_t start = 0U;
    for (;;) {
        const auto position = value.find(delimiter, start);
        result.push_back(value.substr(
            start,
            position == std::string_view::npos ? value.size() - start : position - start
        ));
        if (position == std::string_view::npos) return result;
        start = position + 1U;
    }
}

[[nodiscard]] bool valid_allele_token(const std::string_view token) noexcept {
    if (token == ".") return true;
    if (token.empty()) return false;
    return std::ranges::all_of(token, [](const char c) {
        return c >= '0' && c <= '9';
    });
}

struct VcfMappedColumn final {
    std::size_t source_index{0U};
    std::size_t column{0U};
};

void inspect_vcf(
    CohortAnalysisSelectionPreview& preview,
    const std::string_view text,
    const std::vector<std::size_t>& source_indices
) {
    if (text.find('\0') != std::string_view::npos) {
        for (const auto index : source_indices) {
            add_issue(preview, "invalid_vcf", preview.sources[index].project_sample_id,
                      preview.sources[index].managed_file_id,
                      "Selected VCF contains a NUL byte");
        }
        return;
    }

    std::vector<std::string_view> header;
    std::vector<VcfMappedColumn> mapped;
    std::set<std::string, std::less<>> duplicate_header_names;
    std::size_t offset = 0U;
    std::size_t data_offset = std::string_view::npos;

    while (offset <= text.size()) {
        const auto newline = text.find('\n', offset);
        auto line = text.substr(offset,
            newline == std::string_view::npos ? text.size() - offset : newline - offset);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1U);
        if (line.starts_with("#CHROM\t")) {
            header = split(line, '\t');
            data_offset = newline == std::string_view::npos ? text.size() : newline + 1U;
            break;
        }
        if (newline == std::string_view::npos) break;
        offset = newline + 1U;
    }

    if (header.size() < 10U || data_offset == std::string_view::npos) {
        for (const auto index : source_indices) {
            add_issue(preview, "invalid_vcf", preview.sources[index].project_sample_id,
                      preview.sources[index].managed_file_id,
                      "Selected VCF has no valid sample header");
        }
        return;
    }

    std::map<std::string_view, std::size_t, std::less<>> columns;
    for (std::size_t index = 9U; index < header.size(); ++index) {
        if (!columns.emplace(header[index], index).second) {
            duplicate_header_names.emplace(header[index]);
        }
    }

    for (const auto source_index : source_indices) {
        const auto& source = preview.sources[source_index];
        const auto found = columns.find(source.vcf_sample_name);
        if (found == columns.end() || duplicate_header_names.contains(source.vcf_sample_name)) {
            add_issue(preview, "mapping_conflict", source.project_sample_id,
                      source.managed_file_id,
                      "Explicit VCF sample column is missing or duplicated");
            continue;
        }
        mapped.push_back({source_index, found->second});
    }
    if (mapped.empty()) return;

    offset = data_offset;
    while (offset < text.size()) {
        const auto newline = text.find('\n', offset);
        auto line = text.substr(offset,
            newline == std::string_view::npos ? text.size() - offset : newline - offset);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1U);
        offset = newline == std::string_view::npos ? text.size() : newline + 1U;
        if (line.empty() || line.front() == '#') continue;

        const auto fields = split(line, '\t');
        if (fields.size() < header.size() || fields.size() < 10U) {
            for (const auto& value : mapped) {
                add_issue(preview, "invalid_vcf",
                          preview.sources[value.source_index].project_sample_id,
                          preview.sources[value.source_index].managed_file_id,
                          "Selected VCF data row has fewer columns than its header");
            }
            return;
        }

        const auto format = split(fields[8], ':');
        const auto gt_it = std::ranges::find(format, std::string_view{"GT"});
        if (gt_it == format.end()) continue;
        const auto gt_index = static_cast<std::size_t>(gt_it - format.begin());

        for (const auto& value : mapped) {
            const auto sample_fields = split(fields[value.column], ':');
            if (gt_index >= sample_fields.size()) continue;
            const auto genotype = sample_fields[gt_index];
            if (genotype.empty() || genotype == ".") continue;

            std::size_t separators = 0U;
            std::size_t separator_position = std::string_view::npos;
            for (std::size_t i = 0U; i < genotype.size(); ++i) {
                if (genotype[i] == '/' || genotype[i] == '|') {
                    ++separators;
                    separator_position = i;
                }
            }
            if (separators != 1U) {
                add_issue(preview, "unsupported_ploidy",
                          preview.sources[value.source_index].project_sample_id,
                          preview.sources[value.source_index].managed_file_id,
                          "Cohort v1 accepts only diploid GT values");
                continue;
            }
            const auto first = genotype.substr(0U, separator_position);
            const auto second = genotype.substr(separator_position + 1U);
            if (!valid_allele_token(first) || !valid_allele_token(second)) {
                add_issue(preview, "invalid_vcf",
                          preview.sources[value.source_index].project_sample_id,
                          preview.sources[value.source_index].managed_file_id,
                          "Selected VCF contains an invalid GT allele token");
            }
        }
    }
}

[[nodiscard]] bool reference_matches_plan(
    const ApprovedBatchSamplePlan& sample,
    const std::string_view file_id,
    const std::int64_t size_bytes,
    const std::string_view sha256
) {
    bool found = false;
    for (const auto& node : sample.nodes) {
        for (const auto& input : node.inputs) {
            if (!input.managed_file.has_value() ||
                input.managed_file->role != BatchInputFileRole::reference) {
                continue;
            }
            const auto& reference = *input.managed_file;
            if (reference.file_id != file_id ||
                reference.size_bytes != size_bytes ||
                reference.sha256 != sha256) {
                return false;
            }
            found = true;
        }
    }
    return found;
}

[[nodiscard]] bool validate_aliases(
    CohortAnalysisSelectionPreview& preview,
    const std::vector<CohortReferenceContig>& contigs,
    const std::vector<CohortReferenceAlias>& aliases
) {
    std::set<std::string, std::less<>> canonical;
    for (const auto& contig : contigs) canonical.insert(contig.canonical_name);
    std::set<std::string, std::less<>> seen_alias;
    bool valid = true;
    for (const auto& alias : aliases) {
        if (blank(alias.alias) || blank(alias.canonical_name) ||
            !canonical.contains(alias.canonical_name) ||
            canonical.contains(alias.alias) ||
            !seen_alias.emplace(alias.alias).second) {
            add_issue(preview, "reference_unverified", {}, {},
                      "Reference alias mapping is ambiguous or targets an unknown canonical contig");
            valid = false;
        }
    }
    return valid;
}

[[nodiscard]] std::string result_read_status(const ResultArtifactReadStatus status) {
    switch (status) {
        case ResultArtifactReadStatus::verified: return "verified";
        case ResultArtifactReadStatus::too_large: return "too_large";
        case ResultArtifactReadStatus::missing: return "missing";
        case ResultArtifactReadStatus::unsafe_path: return "unsafe_path";
        case ResultArtifactReadStatus::not_regular: return "not_regular";
        case ResultArtifactReadStatus::size_mismatch: return "size_mismatch";
        case ResultArtifactReadStatus::checksum_unavailable: return "checksum_unavailable";
        case ResultArtifactReadStatus::checksum_mismatch: return "checksum_mismatch";
        case ResultArtifactReadStatus::io_error: return "io_error";
    }
    return "io_error";
}

}  // namespace

CohortAnalysisSelectionService::CohortAnalysisSelectionService(
    ICohortRegistryStore& cohorts,
    IBatchPlanStore& plans,
    IBatchExecutionStore& executions,
    IJobRepository& jobs,
    IManagedFileRepository& files,
    IInputFileStorage& input_storage,
    IResultArtifactReader& artifact_reader,
    IReferenceManifestReader& reference_reader
) noexcept
    : cohorts_{cohorts}, plans_{plans}, executions_{executions}, jobs_{jobs}, files_{files},
      input_storage_{input_storage}, artifact_reader_{artifact_reader},
      reference_reader_{reference_reader} {}

CohortAnalysisSelectionPreview CohortAnalysisSelectionService::preview(
    const CohortAnalysisSelectionRequest& request
) {
    if (blank(request.project_id) || blank(request.cohort_id) || request.cohort_revision == 0U) {
        throw std::invalid_argument{"Cohort selection identity is invalid"};
    }

    CohortAnalysisSelectionPreview preview{
        .project_id = request.project_id,
        .cohort_id = request.cohort_id,
        .cohort_revision = request.cohort_revision,
    };

    const auto cohort = cohorts_.find(
        request.project_id, request.cohort_id, request.cohort_revision);
    if (!cohort.has_value()) {
        add_issue(preview, "cohort_not_found", {}, {}, "Requested cohort revision was not found");
        return preview;
    }

    std::size_t included_count = 0U;
    for (const auto& member : cohort->revision.members) {
        if (member.disposition == CohortMemberDisposition::included) ++included_count;
    }
    if (included_count > maximum_samples) {
        add_issue(preview, "limit_exceeded", {}, {},
                  "Included cohort exceeds the 100-sample admission budget");
    }
    if (request.selections.size() > maximum_samples) {
        add_issue(preview, "limit_exceeded", {}, {},
                  "Selection request exceeds the 100-sample admission budget");
    }

    const auto reference_file = files_.find_by_id(request.reference.managed_file_id);
    std::optional<CohortPinnedReference> pinned_reference;
    if (!reference_file.has_value() || reference_file->file_type() != "fasta") {
        add_issue(preview, "reference_unverified", {}, request.reference.managed_file_id,
                  "Selected reference is not a managed FASTA file");
    } else if (request.reference.assembly == domain::ReferenceAssembly::unspecified ||
               (request.reference.assembly == domain::ReferenceAssembly::custom &&
                (!request.reference.custom_assembly_id.has_value() ||
                 blank(*request.reference.custom_assembly_id))) ||
               (request.reference.assembly != domain::ReferenceAssembly::custom &&
                request.reference.custom_assembly_id.has_value())) {
        add_issue(preview, "reference_unverified", {}, request.reference.managed_file_id,
                  "Reference assembly identity is incomplete");
    } else if (request.reference.normalization_contract_version != normalization_contract_v1) {
        add_issue(preview, "reference_unverified", {}, request.reference.managed_file_id,
                  "Normalization contract version is unsupported");
    } else if (reference_file->size_bytes() < 0 ||
               static_cast<std::uint64_t>(reference_file->size_bytes()) > maximum_reference_bytes) {
        add_issue(preview, "limit_exceeded", {}, request.reference.managed_file_id,
                  "Reference FASTA exceeds the 512 MiB cohort admission budget");
    } else {
        const auto integrity = input_storage_.verify_managed_file(*reference_file);
        if (integrity.status != ManagedFileIntegrityStatus::verified ||
            !integrity.observed_sha256.has_value()) {
            add_issue(preview, "reference_unverified", {}, request.reference.managed_file_id,
                      "Reference FASTA integrity verification failed");
        } else {
            const auto manifest = reference_reader_.read_verified_manifest(
                *reference_file, maximum_reference_bytes);
            if (manifest.status != ReferenceManifestReadStatus::verified ||
                !manifest.verified_sha256.has_value() ||
                *manifest.verified_sha256 != *integrity.observed_sha256 ||
                manifest.verified_size_bytes != reference_file->size_bytes()) {
                add_issue(preview, "reference_unverified", {}, request.reference.managed_file_id,
                          "Reference FASTA manifest could not be verified against exact bytes");
            } else {
                std::vector<CohortReferenceContig> contigs;
                contigs.reserve(manifest.contigs.size());
                std::set<std::string, std::less<>> seen_contigs;
                bool manifest_valid = !manifest.contigs.empty();
                for (const auto& contig : manifest.contigs) {
                    if (blank(contig.canonical_name) || contig.length == 0U ||
                        !seen_contigs.emplace(contig.canonical_name).second) {
                        manifest_valid = false;
                        break;
                    }
                    contigs.push_back({contig.canonical_name, contig.length});
                }
                if (!manifest_valid) {
                    add_issue(preview, "reference_unverified", {}, request.reference.managed_file_id,
                              "Reference FASTA manifest contains invalid contig evidence");
                } else if (!validate_aliases(preview, contigs, request.reference.aliases)) {
                    // Detailed issue already emitted.
                } else {
                    pinned_reference = CohortPinnedReference{
                        .managed_file_id = std::string{reference_file->id()},
                        .file_type = std::string{reference_file->file_type()},
                        .size_bytes = reference_file->size_bytes(),
                        .sha256 = *integrity.observed_sha256,
                        .assembly = request.reference.assembly,
                        .custom_assembly_id = request.reference.custom_assembly_id,
                        .normalization_contract_version =
                            request.reference.normalization_contract_version,
                        .contigs = std::move(contigs),
                        .aliases = request.reference.aliases,
                    };
                    preview.reference = pinned_reference;
                }
            }
        }
    }

    std::map<std::string, std::size_t, std::less<>> selection_count;
    std::set<std::pair<std::string, std::string>, std::less<>> mapped_columns;
    for (const auto& selection : request.selections) {
        ++selection_count[selection.project_sample_id];
        if (!mapped_columns.emplace(selection.managed_file_id, selection.vcf_sample_name).second) {
            add_issue(preview, "mapping_conflict", selection.project_sample_id,
                      selection.managed_file_id,
                      "One VCF sample column cannot map to multiple cohort samples");
        }
    }

    for (const auto& member : cohort->revision.members) {
        const auto count = selection_count.contains(member.sample_id)
            ? selection_count[member.sample_id] : 0U;
        if (member.disposition == CohortMemberDisposition::included && count != 1U) {
            add_issue(preview, "mapping_conflict", member.sample_id, {},
                      "Every included cohort sample requires exactly one explicit VCF mapping");
        }
        if (member.disposition == CohortMemberDisposition::excluded && count != 0U) {
            add_issue(preview, "mapping_conflict", member.sample_id, {},
                      "Excluded cohort samples cannot enter the selected matrix");
        }
    }

    std::map<std::string, std::vector<std::size_t>, std::less<>> source_indices_by_artifact;
    std::map<std::string, std::string, std::less<>> verified_text_by_artifact;
    std::set<std::string, std::less<>> counted_artifacts;

    for (const auto& selection : request.selections) {
        const auto* member = find_member(cohort->revision, selection.project_sample_id);
        if (member == nullptr) {
            add_issue(preview, "wrong_project", selection.project_sample_id,
                      selection.managed_file_id,
                      "VCF mapping references a sample outside the frozen cohort revision");
            continue;
        }
        if (member->disposition != CohortMemberDisposition::included) continue;
        if (selection_count[selection.project_sample_id] != 1U) continue;
        if (selection.attempt_number < 1 || blank(selection.plan_id) || blank(selection.job_id) ||
            blank(selection.step_id) || blank(selection.output_port) ||
            blank(selection.managed_file_id) || blank(selection.vcf_sample_name) ||
            !valid_sha256(selection.expected_sha256)) {
            add_issue(preview, "mapping_conflict", selection.project_sample_id,
                      selection.managed_file_id,
                      "Explicit artifact/run/attempt/VCF-column mapping is incomplete");
            continue;
        }

        const auto plan = plans_.find(selection.plan_id);
        if (!plan.has_value() || plan->project_id != request.project_id) {
            add_issue(preview, "wrong_project", selection.project_sample_id,
                      selection.managed_file_id,
                      "Selected batch plan does not belong to this project");
            continue;
        }

        const auto attempts = executions_.list_attempts(selection.plan_id);
        const auto attempt_it = std::ranges::find_if(attempts, [&](const auto& attempt) {
            return attempt.attempt_number == selection.attempt_number &&
                   attempt.job_id == selection.job_id;
        });
        if (attempt_it == attempts.end()) {
            add_issue(preview, "mapping_conflict", selection.project_sample_id,
                      selection.managed_file_id,
                      "Selected job does not match the explicit batch attempt");
            continue;
        }
        const auto& attempt = *attempt_it;
        const auto job = jobs_.find_by_id(selection.job_id);
        if (!job.has_value() || job->status() != domain::JobStatus::completed) {
            add_issue(preview, "artifact_unavailable", selection.project_sample_id,
                      selection.managed_file_id,
                      "Selected artifact must belong to a completed job");
            continue;
        }

        const auto* producer = find_plan_sample(*plan, attempt.sample_id);
        if (producer == nullptr || !producer->workflow_id.has_value()) {
            add_issue(preview, "mapping_conflict", selection.project_sample_id,
                      selection.managed_file_id,
                      "Selected attempt has no frozen producer sample/workflow");
            continue;
        }
        if (std::ranges::find(attempt.execution_node_ids, selection.step_id) ==
            attempt.execution_node_ids.end()) {
            add_issue(preview, "mapping_conflict", selection.project_sample_id,
                      selection.managed_file_id,
                      "Selected step is outside the immutable attempt execution scope");
            continue;
        }
        const auto* node = find_node(*producer, selection.step_id);
        const auto* output = node == nullptr ? nullptr : find_output(*node, selection.output_port);
        if (node == nullptr || output == nullptr || output->artifact_type != "vcf") {
            add_issue(preview, "mapping_conflict", selection.project_sample_id,
                      selection.managed_file_id,
                      "Selected output is not a frozen VCF-producing node output");
            continue;
        }

        const auto artifact = files_.find_generated_output(
            selection.job_id, selection.step_id, selection.output_port);
        if (!artifact.has_value() || artifact->file.id() != selection.managed_file_id ||
            artifact->provenance.file_type != "vcf" ||
            artifact->provenance.module_id != node->module_id ||
            artifact->provenance.plugin_version != node->plugin_version) {
            add_issue(preview, "mapping_conflict", selection.project_sample_id,
                      selection.managed_file_id,
                      "Selected generated artifact does not match frozen output provenance");
            continue;
        }
        if (artifact->file.checksum_algorithm() != std::optional<std::string>{"sha256"} ||
            !artifact->file.checksum_value().has_value() ||
            !valid_sha256(*artifact->file.checksum_value()) ||
            *artifact->file.checksum_value() != selection.expected_sha256) {
            add_issue(preview, "artifact_changed", selection.project_sample_id,
                      selection.managed_file_id,
                      "Selected artifact SHA-256 no longer matches the explicit selection");
            continue;
        }
        if (artifact->file.size_bytes() < 0 ||
            static_cast<std::uint64_t>(artifact->file.size_bytes()) > maximum_vcf_bytes) {
            add_issue(preview, "limit_exceeded", selection.project_sample_id,
                      selection.managed_file_id,
                      "Selected VCF exceeds the 64 MiB per-source admission budget");
            continue;
        }
        if (pinned_reference.has_value() &&
            !reference_matches_plan(
                *producer,
                pinned_reference->managed_file_id,
                pinned_reference->size_bytes,
                pinned_reference->sha256)) {
            add_issue(preview, "reference_unverified", selection.project_sample_id,
                      selection.managed_file_id,
                      "Producing workflow does not pin the exact selected reference bytes");
            continue;
        }

        const bool first_artifact = !counted_artifacts.contains(selection.managed_file_id);
        if (first_artifact) {
            const auto bytes = static_cast<std::uint64_t>(artifact->file.size_bytes());
            if (preview.total_vcf_bytes >
                    std::numeric_limits<std::uint64_t>::max() - bytes ||
                preview.total_vcf_bytes + bytes > maximum_combined_vcf_bytes) {
                add_issue(preview, "limit_exceeded", selection.project_sample_id,
                          selection.managed_file_id,
                          "Combined selected VCF text exceeds the 256 MiB admission budget");
                continue;
            }
        }

        if (!verified_text_by_artifact.contains(selection.managed_file_id)) {
            const auto read = artifact_reader_.read_verified_text(*artifact, maximum_vcf_bytes);
            if (read.status != ResultArtifactReadStatus::verified ||
                !read.text.has_value() || !read.verified_sha256.has_value() ||
                *read.verified_sha256 != selection.expected_sha256) {
                add_issue(preview, "artifact_changed", selection.project_sample_id,
                          selection.managed_file_id,
                          "Selected VCF exact-byte verification failed: " +
                              result_read_status(read.status));
                continue;
            }
            verified_text_by_artifact.emplace(selection.managed_file_id, *read.text);
        }

        if (first_artifact) {
            counted_artifacts.emplace(selection.managed_file_id);
            preview.total_vcf_bytes += static_cast<std::uint64_t>(artifact->file.size_bytes());
        }

        preview.sources.push_back({
            .project_sample_id = selection.project_sample_id,
            .biological_unit_id = member->biological_unit_id,
            .plan_id = selection.plan_id,
            .workflow_id = *producer->workflow_id,
            .producer_sample_id = attempt.sample_id,
            .attempt_number = selection.attempt_number,
            .job_id = selection.job_id,
            .step_id = selection.step_id,
            .output_port = selection.output_port,
            .module_id = node->module_id,
            .plugin_version = node->plugin_version,
            .managed_file_id = selection.managed_file_id,
            .size_bytes = artifact->file.size_bytes(),
            .sha256 = selection.expected_sha256,
            .vcf_sample_name = selection.vcf_sample_name,
        });
        source_indices_by_artifact[selection.managed_file_id].push_back(preview.sources.size() - 1U);
    }

    if (counted_artifacts.size() > maximum_sources) {
        add_issue(preview, "limit_exceeded", {}, {},
                  "Selection exceeds the 100-source admission budget");
    }
    if (preview.total_vcf_bytes > maximum_combined_vcf_bytes) {
        add_issue(preview, "limit_exceeded", {}, {},
                  "Combined selected VCF text exceeds the 256 MiB admission budget");
    }

    for (const auto& [artifact_id, indices] : source_indices_by_artifact) {
        const auto text = verified_text_by_artifact.find(artifact_id);
        if (text != verified_text_by_artifact.end()) {
            inspect_vcf(preview, text->second, indices);
        }
    }

    std::ranges::sort(preview.sources, [](const auto& left, const auto& right) {
        return std::tie(left.project_sample_id, left.managed_file_id, left.vcf_sample_name) <
               std::tie(right.project_sample_id, right.managed_file_id, right.vcf_sample_name);
    });
    preview.ready = preview.reference.has_value() &&
                    preview.sources.size() == included_count &&
                    preview.issues.empty();
    return preview;
}

}  // namespace biocore::application
