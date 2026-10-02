#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace biocore::domain {

enum class CohortGroup {
    case_sample,
    control,
    unassigned,
};

[[nodiscard]] constexpr std::string_view to_string(const CohortGroup group) noexcept {
    switch (group) {
        case CohortGroup::case_sample: return "case";
        case CohortGroup::control: return "control";
        case CohortGroup::unassigned: return "unassigned";
    }
    return "unassigned";
}

[[nodiscard]] inline std::optional<CohortGroup> cohort_group_from_string(
    const std::string_view value) noexcept {
    if (value == "case") return CohortGroup::case_sample;
    if (value == "control") return CohortGroup::control;
    if (value == "unassigned") return CohortGroup::unassigned;
    return std::nullopt;
}

struct CohortMemberDraft final {
    std::string sample_id;
    std::string biological_unit_id;
    CohortGroup group{CohortGroup::unassigned};
    bool included{true};
    std::string exclusion_reason;

    friend bool operator==(const CohortMemberDraft&, const CohortMemberDraft&) = default;
};

struct CohortMember final {
    std::string sample_id;
    std::string display_name;
    std::string biological_unit_id;
    CohortGroup group{CohortGroup::unassigned};
    bool included{true};
    std::string exclusion_reason;

    friend bool operator==(const CohortMember&, const CohortMember&) = default;
};

struct CohortDefinition final {
    std::string project_id;
    std::string cohort_id;
    std::int64_t current_revision{0};
    std::string name;
    std::string created_at_utc;
    std::string updated_at_utc;

    friend bool operator==(const CohortDefinition&, const CohortDefinition&) = default;
};

struct CohortRevision final {
    std::string project_id;
    std::string cohort_id;
    std::int64_t revision{0};
    std::optional<std::int64_t> parent_revision;
    std::string created_at_utc;
    std::vector<CohortMember> members;

    friend bool operator==(const CohortRevision&, const CohortRevision&) = default;
};

}  // namespace biocore::domain
