#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "biocore/application/batch_results.hpp"

namespace biocore::application {

struct BatchResultPackageArtifact final {
    BatchResultArtifactLink artifact;
    std::string verified_sha256;

    friend bool operator==(
        const BatchResultPackageArtifact&,
        const BatchResultPackageArtifact&
    ) = default;
};

struct BatchResultPackage final {
    std::uint32_t schema_version{1U};
    std::string producer_name{"OpenGenesis-BioCore"};
    std::string producer_version;
    std::string generated_at_utc;
    bool stable_snapshot{false};
    bool complete_results{false};
    BatchResultsOverview overview;
    std::vector<BatchResultPackageArtifact> verified_artifacts;
};

}  // namespace biocore::application
