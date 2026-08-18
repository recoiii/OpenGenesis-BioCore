#pragma once

#include <string>

#include "biocore/domain/managed_file.hpp"

namespace biocore::application {

struct GeneratedOutputProvenance final {
    std::string job_id;
    std::string step_id;
    std::string output_port;
    std::string plugin_id;
    std::string plugin_version;
    std::string module_id;
    std::string file_type;
    std::string relative_project_path;
    double step_progress;
    std::string registered_at_utc;
};

struct GeneratedOutputArtifact final {
    domain::ManagedFile file;
    GeneratedOutputProvenance provenance;
};

}  // namespace biocore::application
