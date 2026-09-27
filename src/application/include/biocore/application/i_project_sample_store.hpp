#pragma once

#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "biocore/domain/project_sample.hpp"

namespace biocore::application {

enum class SampleBatchAddResult {
    added,
    duplicate_sample,
    project_not_found
};

class IProjectSampleStore {
public:
    virtual ~IProjectSampleStore() = default;

    [[nodiscard]] virtual std::optional<std::vector<domain::ProjectSample>> list(
        std::string_view project_id) = 0;

    virtual SampleBatchAddResult add_batch(
        std::string_view project_id,
        std::span<const domain::ProjectSample> samples) = 0;
};

}  // namespace biocore::application
