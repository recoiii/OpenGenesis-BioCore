#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "biocore/domain/cohort.hpp"

namespace biocore::application {

enum class CohortWriteResult {
    created,
    revised,
    project_not_found,
    cohort_not_found,
    cohort_already_exists,
    stale_revision,
    duplicate_sample,
    sample_not_found,
    invalid_request,
};

class ICohortRegistryStore {
public:
    virtual ~ICohortRegistryStore() = default;

    [[nodiscard]] virtual std::optional<std::vector<domain::CohortDefinition>> list(
        std::string_view project_id) = 0;

    [[nodiscard]] virtual std::optional<domain::CohortDefinition> get(
        std::string_view project_id,
        std::string_view cohort_id) = 0;

    [[nodiscard]] virtual std::optional<domain::CohortRevision> get_revision(
        std::string_view project_id,
        std::string_view cohort_id,
        std::int64_t revision) = 0;

    virtual CohortWriteResult create(
        std::string_view project_id,
        std::string_view cohort_id,
        std::string_view name,
        std::string_view created_at_utc,
        std::span<const domain::CohortMemberDraft> members) = 0;

    virtual CohortWriteResult revise(
        std::string_view project_id,
        std::string_view cohort_id,
        std::int64_t expected_revision,
        std::string_view name,
        std::string_view created_at_utc,
        std::span<const domain::CohortMemberDraft> members) = 0;
};

}  // namespace biocore::application
