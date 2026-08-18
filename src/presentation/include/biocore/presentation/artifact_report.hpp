#pragma once

#include <string>

#include "biocore/application/artifact_presentation_service.hpp"

namespace biocore::presentation {

[[nodiscard]] std::string render_artifact_metadata_json(
    const application::ArtifactMetadata& artifact
);
[[nodiscard]] std::string render_pipeline_execution_report_json(
    const application::PipelineExecutionReport& report
);
[[nodiscard]] std::string render_pipeline_execution_report_html(
    const application::PipelineExecutionReport& report
);

}  // namespace biocore::presentation
