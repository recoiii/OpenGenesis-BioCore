#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/batch_execution.hpp"
#include "biocore/application/batch_plan.hpp"
#include "biocore/domain/job_status.hpp"
#include "biocore/domain/multi_sample_matrix.hpp"

namespace biocore::application {

enum class BatchResultSampleState {
    excluded,
    not_submitted,
    submitted
};

[[nodiscard]] std::string_view to_string(BatchResultSampleState state) noexcept;

enum class BatchQcComparisonState {
    comparable,
    incomplete,
    incompatible
};

[[nodiscard]] std::string_view to_string(BatchQcComparisonState state) noexcept;

enum class BatchQcMetricValueKind {
    integer,
    number,
    boolean,
    string,
    null_value
};

struct BatchResultArtifactLink final {
    std::string sample_id;
    std::string job_id;
    std::int64_t attempt_number{1};
    BatchAttemptMode attempt_mode{BatchAttemptMode::initial};
    std::string managed_file_id;
    std::string step_id;
    std::string output_port;
    std::string module_id;
    std::string plugin_version;
    std::string file_type;
    std::string relative_project_path;
    std::int64_t size_bytes{0};
    std::optional<std::string> sha256;

    friend bool operator==(const BatchResultArtifactLink&, const BatchResultArtifactLink&) = default;
};

struct BatchQcMetric final {
    std::string key;
    BatchQcMetricValueKind kind{BatchQcMetricValueKind::string};
    std::optional<std::string> value;

    friend bool operator==(const BatchQcMetric&, const BatchQcMetric&) = default;
};

struct BatchQcSummary final {
    std::string sample_id;
    std::string node_id;
    std::string module_id;
    std::string plugin_version;
    std::uint32_t metric_schema_version{1U};
    std::string parameter_signature;
    std::string reference_signature;
    bool reference_evidence_complete{true};
    bool stable{false};
    BatchResultArtifactLink source_artifact;
    std::vector<BatchQcMetric> metrics;
};

struct BatchQcComparisonGroup final {
    std::string node_id;
    std::string module_id;
    BatchQcComparisonState state{BatchQcComparisonState::incomplete};
    std::vector<std::string> sample_ids;
    std::string reason;
};

struct BatchResultIssue final {
    std::string sample_id;
    std::string code;
    std::string message;
};

struct BatchSampleResults final {
    std::string sample_id;
    BatchPlanSampleDisposition disposition{BatchPlanSampleDisposition::included};
    BatchResultSampleState state{BatchResultSampleState::not_submitted};
    std::optional<std::string> latest_job_id;
    std::optional<domain::JobStatus> latest_job_status;
    std::int64_t latest_attempt_number{0};
    std::optional<BatchAttemptMode> latest_attempt_mode;
    std::vector<BatchResultArtifactLink> artifacts;
    std::vector<BatchQcSummary> qc_summaries;
};

struct BatchResultsOverview final {
    std::string plan_id;
    std::string project_id;
    std::string template_id;
    std::string template_version;
    std::vector<BatchSampleResults> samples;
    std::vector<BatchQcComparisonGroup> qc_comparisons;
    std::vector<BatchResultIssue> issues;
};

struct BatchVariantMatrixSource final {
    std::string sample_id;
    BatchResultArtifactLink artifact;
};

struct BatchVariantMatrixPreview final {
    std::string plan_id;
    bool ready{false};
    std::vector<BatchVariantMatrixSource> sources;
    std::vector<BatchResultIssue> issues;
};

struct BatchVariantMatrixBuild final {
    BatchVariantMatrixPreview preview;
    domain::MultiSampleMatrix matrix;
};

void validate_vcf_qc_summary_document(std::string_view text);

}  // namespace biocore::application
