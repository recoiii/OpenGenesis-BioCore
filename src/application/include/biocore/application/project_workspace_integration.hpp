#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "biocore/application/batch_execution.hpp"
#include "biocore/application/batch_plan.hpp"
#include "biocore/application/batch_planning_service.hpp"
#include "biocore/application/batch_recovery_service.hpp"
#include "biocore/application/batch_results.hpp"
#include "biocore/application/sample_binding_service.hpp"
#include "biocore/application/sample_registry_import.hpp"
#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/project.hpp"
#include "biocore/domain/project_research_metadata.hpp"
#include "biocore/domain/project_sample.hpp"
#include "biocore/domain/project_sample_binding.hpp"

namespace biocore::application {

struct ProjectWorkspaceLinkIssue final {
    std::string code;
    std::string message;
};

struct ProjectWorkspaceSample final {
    domain::ProjectSample sample;
    std::optional<domain::ProjectSampleBinding> binding;
    bool binding_complete{false};
    std::vector<ProjectWorkspaceLinkIssue> issues;
};

struct ProjectWorkspaceFile final {
    std::string file_id;
    std::string display_name;
    std::string file_type;
    domain::StorageMode storage_mode{domain::StorageMode::managed_copy};
    std::int64_t size_bytes{0};
    bool orphaned{false};
};

struct ProjectWorkspaceBatch final {
    std::string plan_id;
    std::string template_id;
    std::string template_version;
    std::string approved_at_utc;
    std::size_t included_samples{0U};
    std::size_t excluded_samples{0U};
    std::optional<BatchExecutionSnapshot> execution;
    std::size_t recovery_attention{0U};
};

struct ProjectWorkspaceSnapshot final {
    domain::Project project;
    std::optional<domain::ProjectResearchMetadata> research;
    std::vector<ProjectWorkspaceSample> samples;
    std::vector<ProjectWorkspaceFile> files;
    std::vector<ProjectWorkspaceBatch> batches;
};

struct ProjectWorkspaceBindingRequest final {
    std::string sample_id;
    domain::SampleInputLayout layout{domain::SampleInputLayout::single_fastq};
    std::string primary_file_id;
    std::optional<std::string> secondary_file_id;
    std::optional<std::string> reference_file_id;
};

struct ProjectWorkspaceBatchPreviewRequest final {
    std::string plan_id;
    std::string template_id;
    std::string template_version;
    std::vector<std::string> sample_ids;
    std::vector<WorkflowTemplateParameterOverride> parameter_overrides;
    std::vector<BatchWorkflowInputAssignment> input_assignments;
};

struct ProjectWorkspaceBatchSubmitRequest final {
    std::size_t maximum_concurrent_jobs{1U};
    domain::JobPriority priority{domain::JobPriority::normal};
};

}  // namespace biocore::application
