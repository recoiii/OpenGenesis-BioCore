#include "biocore/application/cohort_analysis_snapshot.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace biocore::application {
namespace {

void append_token(std::string& out, const std::string_view key, const std::string_view value) {
    std::array<char, 32> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value.size());
    if (error != std::errc{}) {
        throw std::logic_error{"Unable to serialize canonical cohort snapshot length"};
    }
    out.append(key);
    out.push_back('=');
    out.append(buffer.data(), static_cast<std::size_t>(end - buffer.data()));
    out.push_back(':');
    out.append(value);
    out.push_back('\n');
}

template <typename Integer>
void append_integer(std::string& out, const std::string_view key, const Integer value) {
    std::array<char, 64> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (error != std::errc{}) {
        throw std::logic_error{"Unable to serialize canonical cohort snapshot integer"};
    }
    append_token(out, key, std::string_view{buffer.data(), static_cast<std::size_t>(end - buffer.data())});
}

void append_optional(
    std::string& out,
    const std::string_view key,
    const std::optional<std::string>& value
) {
    append_token(out, std::string{key} + ".present", value.has_value() ? "1" : "0");
    if (value.has_value()) append_token(out, key, *value);
}

}  // namespace

std::string_view to_string(const CohortAnalysisDisposition value) noexcept {
    switch (value) {
        case CohortAnalysisDisposition::included: return "included";
        case CohortAnalysisDisposition::excluded: return "excluded";
    }
    return "excluded";
}

std::optional<CohortAnalysisDisposition>
cohort_analysis_disposition_from_string(const std::string_view value) noexcept {
    if (value == "included") return CohortAnalysisDisposition::included;
    if (value == "excluded") return CohortAnalysisDisposition::excluded;
    return std::nullopt;
}

std::string_view to_string(const CohortQcEvidenceState value) noexcept {
    switch (value) {
        case CohortQcEvidenceState::verified: return "verified";
        case CohortQcEvidenceState::unavailable: return "unavailable";
        case CohortQcEvidenceState::not_applicable: return "not_applicable";
    }
    return "unavailable";
}

std::optional<CohortQcEvidenceState>
cohort_qc_evidence_state_from_string(const std::string_view value) noexcept {
    if (value == "verified") return CohortQcEvidenceState::verified;
    if (value == "unavailable") return CohortQcEvidenceState::unavailable;
    if (value == "not_applicable") return CohortQcEvidenceState::not_applicable;
    return std::nullopt;
}

std::string canonical_cohort_analysis_snapshot(
    const CohortAnalysisSnapshot& snapshot,
    const bool include_final_identity
) {
    std::string out;
    out.reserve(8192U + snapshot.samples.size() * 512U +
                snapshot.sources.size() * 512U + snapshot.test_universe.size() * 384U);
    append_token(out, "canonical_format", "biocore.cohort-analysis.canonical.v1");
    append_token(out, "contract_version", snapshot.contract_version);
    append_token(out, "project_id", snapshot.project_id);
    append_token(out, "cohort_id", snapshot.cohort_id);
    append_integer(out, "cohort_revision", snapshot.cohort_revision);

    if (include_final_identity) {
        append_token(out, "analysis_id", snapshot.analysis_id);
        append_token(out, "preview_digest", snapshot.preview_digest);
        append_token(out, "approved_at_utc", snapshot.approved_at_utc);
    }

    append_token(out, "reference.file_id", snapshot.reference.managed_file_id);
    append_token(out, "reference.file_type", snapshot.reference.file_type);
    append_integer(out, "reference.size_bytes", snapshot.reference.size_bytes);
    append_token(out, "reference.sha256", snapshot.reference.sha256);
    append_token(out, "reference.assembly", domain::to_string(snapshot.reference.assembly));
    append_optional(out, "reference.custom_assembly_id", snapshot.reference.custom_assembly_id);
    append_token(out, "reference.normalization_contract", snapshot.reference.normalization_contract_version);

    append_integer(out, "reference.contig_count", snapshot.reference.contigs.size());
    for (std::size_t index = 0U; index < snapshot.reference.contigs.size(); ++index) {
        const auto prefix = "reference.contig." + std::to_string(index) + ".";
        append_token(out, prefix + "name", snapshot.reference.contigs[index].canonical_name);
        append_integer(out, prefix + "length", snapshot.reference.contigs[index].length);
    }

    auto aliases = snapshot.reference.aliases;
    std::ranges::sort(aliases, [](const auto& left, const auto& right) {
        return std::tie(left.alias, left.canonical_name) <
               std::tie(right.alias, right.canonical_name);
    });
    append_integer(out, "reference.alias_count", aliases.size());
    for (std::size_t index = 0U; index < aliases.size(); ++index) {
        const auto prefix = "reference.alias." + std::to_string(index) + ".";
        append_token(out, prefix + "alias", aliases[index].alias);
        append_token(out, prefix + "canonical", aliases[index].canonical_name);
    }

    auto sources = snapshot.sources;
    std::ranges::sort(sources, [](const auto& left, const auto& right) {
        return std::tie(left.project_sample_id, left.managed_file_id, left.vcf_sample_name,
                        left.plan_id, left.attempt_number, left.job_id,
                        left.step_id, left.output_port) <
               std::tie(right.project_sample_id, right.managed_file_id, right.vcf_sample_name,
                        right.plan_id, right.attempt_number, right.job_id,
                        right.step_id, right.output_port);
    });
    append_integer(out, "source_count", sources.size());
    for (std::size_t index = 0U; index < sources.size(); ++index) {
        const auto& source = sources[index];
        const auto prefix = "source." + std::to_string(index) + ".";
        append_token(out, prefix + "project_sample_id", source.project_sample_id);
        append_token(out, prefix + "biological_unit_id", source.biological_unit_id);
        append_token(out, prefix + "plan_id", source.plan_id);
        append_token(out, prefix + "workflow_id", source.workflow_id);
        append_token(out, prefix + "producer_sample_id", source.producer_sample_id);
        append_integer(out, prefix + "attempt_number", source.attempt_number);
        append_token(out, prefix + "job_id", source.job_id);
        append_token(out, prefix + "step_id", source.step_id);
        append_token(out, prefix + "output_port", source.output_port);
        append_token(out, prefix + "module_id", source.module_id);
        append_token(out, prefix + "plugin_version", source.plugin_version);
        append_token(out, prefix + "managed_file_id", source.managed_file_id);
        append_integer(out, prefix + "size_bytes", source.size_bytes);
        append_token(out, prefix + "sha256", source.sha256);
        append_token(out, prefix + "vcf_sample_name", source.vcf_sample_name);
    }

    auto samples = snapshot.samples;
    std::ranges::sort(samples, [](const auto& left, const auto& right) {
        return std::tie(left.ordinal, left.sample_id) < std::tie(right.ordinal, right.sample_id);
    });
    append_integer(out, "sample_count", samples.size());
    for (std::size_t index = 0U; index < samples.size(); ++index) {
        const auto& sample = samples[index];
        const auto prefix = "sample." + std::to_string(index) + ".";
        append_integer(out, prefix + "ordinal", sample.ordinal);
        append_token(out, prefix + "sample_id", sample.sample_id);
        append_token(out, prefix + "display_name", sample.sample_display_name);
        append_token(out, prefix + "group_metadata", sample.sample_group_metadata);
        append_token(out, prefix + "biological_unit_id", sample.biological_unit_id);
        append_token(out, prefix + "group", to_string(sample.group));
        append_token(out, prefix + "cohort_disposition", to_string(sample.cohort_disposition));
        append_optional(out, prefix + "cohort_exclusion_reason", sample.cohort_exclusion_reason);
        append_token(out, prefix + "analysis_disposition", to_string(sample.analysis_disposition));
        append_optional(out, prefix + "analysis_reason", sample.analysis_reason);
        append_token(out, prefix + "qc_state", to_string(sample.qc.state));
        append_token(out, prefix + "qc_reason", sample.qc.reason);
        append_optional(out, prefix + "qc_file_id", sample.qc.managed_file_id);
        append_optional(out, prefix + "qc_job_id", sample.qc.job_id);
        append_optional(out, prefix + "qc_step_id", sample.qc.step_id);
        append_optional(out, prefix + "qc_output_port", sample.qc.output_port);
        append_optional(out, prefix + "qc_module_id", sample.qc.module_id);
        append_optional(out, prefix + "qc_plugin_version", sample.qc.plugin_version);
        append_token(out, prefix + "qc_size.present", sample.qc.size_bytes.has_value() ? "1" : "0");
        if (sample.qc.size_bytes.has_value()) {
            append_integer(out, prefix + "qc_size", *sample.qc.size_bytes);
        }
        append_optional(out, prefix + "qc_sha256", sample.qc.sha256);
    }

    append_token(out, "association.contract_version", snapshot.association_contract_version);
    append_token(out, "association.test_filter_version", snapshot.test_filter_version);
    append_integer(out, "association.minimum_complete_case_calls",
                   snapshot.association_options.minimum_complete_case_calls);
    append_integer(out, "association.minimum_complete_control_calls",
                   snapshot.association_options.minimum_complete_control_calls);
    append_integer(out, "association.maximum_fisher_table_states",
                   snapshot.association_options.maximum_fisher_table_states);
    append_integer(out, "association.approved_case_samples", snapshot.approved_case_samples);
    append_integer(out, "association.approved_control_samples", snapshot.approved_control_samples);
    append_integer(out, "association.allele_family_size", snapshot.allele_family_size);
    append_integer(out, "association.carrier_family_size", snapshot.carrier_family_size);

    append_integer(out, "test_universe_count", snapshot.test_universe.size());
    for (std::size_t index = 0U; index < snapshot.test_universe.size(); ++index) {
        const auto& variant = snapshot.test_universe[index];
        const auto prefix = "variant." + std::to_string(index) + ".";
        append_integer(out, prefix + "ordinal", variant.ordinal);
        append_token(out, prefix + "contig", variant.contig);
        append_integer(out, prefix + "start", variant.start);
        append_integer(out, prefix + "end", variant.end);
        append_token(out, prefix + "reference", variant.reference);
        append_token(out, prefix + "alternate", variant.alternate);
        append_integer(out, prefix + "case_unobserved", variant.case_unobserved);
        append_integer(out, prefix + "control_unobserved", variant.control_unobserved);
        append_integer(out, prefix + "case_no_calls", variant.case_no_calls);
        append_integer(out, prefix + "control_no_calls", variant.control_no_calls);
        append_integer(out, prefix + "case_partial_calls", variant.case_partial_calls);
        append_integer(out, prefix + "control_partial_calls", variant.control_partial_calls);
        append_integer(out, prefix + "case_complete_calls", variant.case_complete_calls);
        append_integer(out, prefix + "control_complete_calls", variant.control_complete_calls);
        append_token(out, prefix + "allele_family_member", variant.allele_family_member ? "1" : "0");
        append_token(out, prefix + "carrier_family_member", variant.carrier_family_member ? "1" : "0");
    }
    return out;
}

}  // namespace biocore::application
