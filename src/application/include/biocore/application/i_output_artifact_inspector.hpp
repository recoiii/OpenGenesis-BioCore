#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace biocore::application {

struct InspectedOutputArtifact final {
    std::string display_name;
    std::string managed_path;
    std::string relative_project_path;
    std::int64_t size_bytes{0};
    std::optional<std::string> modified_at_utc;
    std::string checksum_algorithm;
    std::string checksum_value;
};

class IOutputArtifactInspector {
public:
    virtual ~IOutputArtifactInspector() = default;

    [[nodiscard]] virtual InspectedOutputArtifact inspect_existing_output(
        std::string_view relative_project_path
    ) = 0;
};

}  // namespace biocore::application
