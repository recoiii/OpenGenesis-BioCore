#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace biocore::domain {

// A value copy of mutable project context, not a persisted workflow snapshot.
class ProjectResearchMetadata final {
public:
    static constexpr std::size_t maximum_description_bytes = 4096U;
    static constexpr std::size_t maximum_organism_bytes = 256U;
    ProjectResearchMetadata(std::string project_id, std::string description,
                            std::string organism, std::int64_t revision,
                            std::string updated_at_utc);
    [[nodiscard]] std::string_view project_id() const noexcept;
    [[nodiscard]] std::string_view description() const noexcept;
    [[nodiscard]] std::string_view organism() const noexcept;
    [[nodiscard]] std::int64_t revision() const noexcept;
    [[nodiscard]] std::string_view updated_at_utc() const noexcept;
private:
    std::string project_id_;
    std::string description_;
    std::string organism_;
    std::int64_t revision_;
    std::string updated_at_utc_;
};

}  // namespace biocore::domain
