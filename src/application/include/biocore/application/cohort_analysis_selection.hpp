#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "biocore/domain/contig_table.hpp"

namespace biocore::application {

struct CohortReferenceAlias final {
    std::string alias;
    std::string canonical_name;

    friend bool operator==(const CohortReferenceAlias&, const CohortReferenceAlias&) = default;
};

struct CohortReferenceSelection final {
    std::string managed_file_id;
    domain::ReferenceAssembly assembly{domain::ReferenceAssembly::unspecified};
    std::optional<std::string> custom_assembly_id;
    std::string normalization_contract_version;
    std::vector<CohortReferenceAlias> aliases;

    friend bool operator==(const CohortReferenceSelection&, const CohortReferenceSelection&) = default;
};

struct CohortVcfSelection final {
    std::string project_sample_id;
    std::string plan_id;
    std::int64_t attempt_number{0};
    std::string job_id;
    std::string step_id;
    std::string output_port;
    std::string managed_file_id;
    std::string expected_sha256;
    std::string vcf_sample_name;

    friend bool operator==(const CohortVcfSelection&, const CohortVcfSelection&) = default;
};

struct CohortAnalysisSelectionRequest final {
    std::string project_id;
    std::string cohort_id;
    std::uint32_t cohort_revision{0U};
    CohortReferenceSelection reference;
    std::vector<CohortVcfSelection> selections;
};

struct CohortReferenceContig final {
    std::string canonical_name;
    std::uint64_t length{0U};

    friend bool operator==(const CohortReferenceContig&, const CohortReferenceContig&) = default;
};

struct CohortPinnedReference final {
    std::string managed_file_id;
    std::string file_type;
    std::int64_t size_bytes{0};
    std::string sha256;
    domain::ReferenceAssembly assembly{domain::ReferenceAssembly::unspecified};
    std::optional<std::string> custom_assembly_id;
    std::string normalization_contract_version;
    std::vector<CohortReferenceContig> contigs;
    std::vector<CohortReferenceAlias> aliases;

    friend bool operator==(const CohortPinnedReference&, const CohortPinnedReference&) = default;
};

struct CohortPinnedVcfSource final {
    std::string project_sample_id;
    std::string biological_unit_id;
    std::string plan_id;
    std::string workflow_id;
    std::string producer_sample_id;
    std::int64_t attempt_number{0};
    std::string job_id;
    std::string step_id;
    std::string output_port;
    std::string module_id;
    std::string plugin_version;
    std::string managed_file_id;
    std::int64_t size_bytes{0};
    std::string sha256;
    std::string vcf_sample_name;

    friend bool operator==(const CohortPinnedVcfSource&, const CohortPinnedVcfSource&) = default;
};

struct CohortSelectionIssue final {
    std::string code;
    std::string sample_id;
    std::string artifact_id;
    std::string message;

    friend bool operator==(const CohortSelectionIssue&, const CohortSelectionIssue&) = default;
};

struct CohortAnalysisSelectionPreview final {
    std::string project_id;
    std::string cohort_id;
    std::uint32_t cohort_revision{0U};
    bool ready{false};
    std::optional<CohortPinnedReference> reference;
    std::vector<CohortPinnedVcfSource> sources;
    std::vector<CohortSelectionIssue> issues;
    std::uint64_t total_vcf_bytes{0U};
};

}  // namespace biocore::application
