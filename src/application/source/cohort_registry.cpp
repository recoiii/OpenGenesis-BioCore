#include "biocore/application/cohort_registry.hpp"

namespace biocore::application {

std::string_view to_string(const CohortGroup group) noexcept {
    switch (group) {
        case CohortGroup::case_group: return "case";
        case CohortGroup::control: return "control";
        case CohortGroup::unassigned: return "unassigned";
    }
    return "unassigned";
}

std::optional<CohortGroup> cohort_group_from_string(const std::string_view value) noexcept {
    if (value == "case") return CohortGroup::case_group;
    if (value == "control") return CohortGroup::control;
    if (value == "unassigned") return CohortGroup::unassigned;
    return std::nullopt;
}

std::string_view to_string(const CohortMemberDisposition disposition) noexcept {
    switch (disposition) {
        case CohortMemberDisposition::included: return "included";
        case CohortMemberDisposition::excluded: return "excluded";
    }
    return "included";
}

std::optional<CohortMemberDisposition> cohort_member_disposition_from_string(
    const std::string_view value
) noexcept {
    if (value == "included") return CohortMemberDisposition::included;
    if (value == "excluded") return CohortMemberDisposition::excluded;
    return std::nullopt;
}

}  // namespace biocore::application
