#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace biocore::application {

enum class CohortGroup {
    case_group,
    control,
    unassigned
};

[[nodiscard]] std::string_view to_string(CohortGroup group) noexcept;
[[nodiscard]] std::optional<CohortGroup> cohort_group_from_string(
    std::string_view value
) noexcept;

enum class CohortMemberDisposition {
    included,
    excluded
};

[[nodiscard]] std::string_view to_string(CohortMemberDisposition disposition) noexcept;
[[nodiscard]] std::optional<CohortMemberDisposition> cohort_member_disposition_from_string(
    std::string_view value
) noexcept;

struct CohortMemberDraft final {
    std::string sample_id;
    std::string biological_unit_id;
    CohortGroup group{CohortGroup::unassigned};
    CohortMemberDisposition disposition{CohortMemberDisposition::included};
    std::optional<std::string> exclusion_reason;

    friend bool operator==(const CohortMemberDraft&, const CohortMemberDraft&) = default;
};

struct CohortMemberSnapshot final {
    std::string sample_id;
    std::string sample_display_name;
    std::string sample_group_metadata;
    std::string biological_unit_id;
    CohortGroup group{CohortGroup::unassigned};
    CohortMemberDisposition disposition{CohortMemberDisposition::included};
    std::optional<std::string> exclusion_reason;

    friend bool operator==(const CohortMemberSnapshot&, const CohortMemberSnapshot&) = default;
};

struct CohortRevision final {
    std::string project_id;
    std::string cohort_id;
    std::uint32_t revision{0U};
    std::optional<std::uint32_t> parent_revision;
    std::string created_at_utc;
    std::vector<CohortMemberSnapshot> members;

    friend bool operator==(const CohortRevision&, const CohortRevision&) = default;
};

struct CohortDefinition final {
    std::string project_id;
    std::string cohort_id;
    std::string name;
    std::uint32_t current_revision{0U};
    std::string created_at_utc;
    std::string updated_at_utc;
    CohortRevision revision;

    friend bool operator==(const CohortDefinition&, const CohortDefinition&) = default;
};

struct CreateCohortStoreRequest final {
    std::string project_id;
    std::string cohort_id;
    std::string name;
    std::string created_at_utc;
    std::vector<CohortMemberDraft> members;
};

struct AppendCohortRevisionStoreRequest final {
    std::string project_id;
    std::string cohort_id;
    std::uint32_t expected_revision{0U};
    std::string created_at_utc;
    std::vector<CohortMemberDraft> members;
};

enum class CohortStoreWriteResult {
    stored,
    project_not_found,
    cohort_not_found,
    cohort_id_conflict,
    stale_revision,
    duplicate_sample,
    sample_not_found
};

}  // namespace biocore::application
