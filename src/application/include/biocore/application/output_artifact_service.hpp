#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/generated_output_artifact.hpp"

namespace biocore::application {

class IIdGenerator;
class IManagedFileRepository;
class IOutputArtifactInspector;
class IUtcClock;

struct RegisterGeneratedOutputRequest final {
    std::string job_id;
    std::string step_id;
    std::string output_port;
    std::string plugin_id;
    std::string plugin_version;
    std::string module_id;
    std::string file_type;
    std::string relative_project_path;
};

class OutputArtifactService final {
public:
    static constexpr int maximum_identifier_attempts = 8;

    OutputArtifactService(
        IManagedFileRepository& repository,
        IOutputArtifactInspector& inspector,
        IIdGenerator& id_generator,
        IUtcClock& clock
    ) noexcept;

    [[nodiscard]] GeneratedOutputArtifact register_generated_output(
        const RegisterGeneratedOutputRequest& request,
        double step_progress = 0.0
    );
    [[nodiscard]] std::vector<GeneratedOutputArtifact> register_generated_outputs_batch(
        const std::vector<RegisterGeneratedOutputRequest>& requests,
        double step_progress = 0.0
    );
    [[nodiscard]] std::vector<GeneratedOutputArtifact> list_for_job(std::string_view job_id);

private:
    IManagedFileRepository& repository_;
    IOutputArtifactInspector& inspector_;
    IIdGenerator& id_generator_;
    IUtcClock& clock_;
};

}  // namespace biocore::application
