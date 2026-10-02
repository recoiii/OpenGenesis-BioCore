#include "biocore/application/cohort_registry_service.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <utility>

#include "biocore/application/i_cohort_registry_store.hpp"
#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_utc_clock.hpp"

namespace biocore::application {
namespace {

[[nodiscard]] bool blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
        return std::isspace(static_cast<unsigned char>(character)) != 0;
    });
}

void require_text(
    const std::string_view value,
    const std::size_t maximum,
    const char* field
) {
    if (blank(value) || value.size() > maximum ||
        value.find('\0') != std::string_view::npos) {
        throw CohortRegistryError{
            CohortRegistryErrorCode::invalid_request,
            std::string{"Invalid cohort "} + field
        };
    }
}

void validate_members(const std::vector<CohortMemberDraft>& members) {
    std::set<std::string, std::less<>> sample_ids;
    for (const auto& member : members) {
        require_text(member.sample_id, 128U, "sample id");
        require_text(member.biological_unit_id, 128U, "biological unit id");
        if (!sample_ids.emplace(member.sample_id).second) {
            throw CohortRegistryError{
                CohortRegistryErrorCode::duplicate_sample,
                "Cohort revision contains a duplicate sample id"
            };
        }
        if (member.disposition == CohortMemberDisposition::included) {
            if (member.exclusion_reason.has_value()) {
                throw CohortRegistryError{
                    CohortRegistryErrorCode::invalid_request,
                    "Included cohort member cannot have an exclusion reason"
                };
            }
        } else {
            if (!member.exclusion_reason.has_value()) {
                throw CohortRegistryError{
                    CohortRegistryErrorCode::invalid_request,
                    "Excluded cohort member requires an explicit exclusion reason"
                };
            }
            require_text(*member.exclusion_reason, 512U, "exclusion reason");
        }
    }
}

[[noreturn]] void throw_store_error(const CohortStoreWriteResult result) {
    switch (result) {
        case CohortStoreWriteResult::project_not_found:
            throw CohortRegistryError{CohortRegistryErrorCode::project_not_found,
                                      "Project does not own this cohort registry"};
        case CohortStoreWriteResult::cohort_not_found:
            throw CohortRegistryError{CohortRegistryErrorCode::cohort_not_found,
                                      "Cohort was not found"};
        case CohortStoreWriteResult::stale_revision:
            throw CohortRegistryError{CohortRegistryErrorCode::stale_revision,
                                      "Cohort revision is stale"};
        case CohortStoreWriteResult::duplicate_sample:
            throw CohortRegistryError{CohortRegistryErrorCode::duplicate_sample,
                                      "Cohort revision contains a duplicate sample id"};
        case CohortStoreWriteResult::sample_not_found:
            throw CohortRegistryError{CohortRegistryErrorCode::sample_not_found,
                                      "Cohort member sample does not exist in this project"};
        case CohortStoreWriteResult::cohort_id_conflict:
        case CohortStoreWriteResult::stored:
            break;
    }
    throw CohortRegistryError{CohortRegistryErrorCode::invalid_request,
                              "Unexpected cohort registry write result"};
}

}  // namespace

CohortRegistryError::CohortRegistryError(
    const CohortRegistryErrorCode code,
    std::string message
) : std::runtime_error{std::move(message)}, code_{code} {}

CohortRegistryErrorCode CohortRegistryError::code() const noexcept { return code_; }

CohortRegistryService::CohortRegistryService(
    ICohortRegistryStore& store,
    IIdGenerator& id_generator,
    IUtcClock& clock
) noexcept : store_{store}, id_generator_{id_generator}, clock_{clock} {}

CohortDefinition CohortRegistryService::create(CreateCohortRequest request) {
    require_text(request.project_id, 128U, "project id");
    require_text(request.name, 200U, "name");
    validate_members(request.members);

    for (std::size_t attempt = 0U; attempt < maximum_id_generation_attempts; ++attempt) {
        CreateCohortStoreRequest store_request{
            .project_id = request.project_id,
            .cohort_id = id_generator_.generate(),
            .name = request.name,
            .created_at_utc = clock_.now_utc_iso8601(),
            .members = request.members,
        };
        require_text(store_request.cohort_id, 128U, "id");
        require_text(store_request.created_at_utc, 200U, "timestamp");
        const auto result = store_.create(store_request);
        if (result == CohortStoreWriteResult::cohort_id_conflict) continue;
        if (result != CohortStoreWriteResult::stored) throw_store_error(result);
        auto stored = store_.find(store_request.project_id, store_request.cohort_id);
        if (!stored.has_value()) {
            throw CohortRegistryError{CohortRegistryErrorCode::invalid_request,
                                      "Created cohort disappeared from registry"};
        }
        return std::move(*stored);
    }
    throw CohortRegistryError{CohortRegistryErrorCode::id_generation_exhausted,
                              "Unable to allocate a unique cohort id"};
}

CohortDefinition CohortRegistryService::revise(ReviseCohortRequest request) {
    require_text(request.project_id, 128U, "project id");
    require_text(request.cohort_id, 128U, "id");
    if (request.expected_revision == 0U) {
        throw CohortRegistryError{CohortRegistryErrorCode::invalid_request,
                                  "Expected cohort revision must be positive"};
    }
    validate_members(request.members);
    AppendCohortRevisionStoreRequest store_request{
        .project_id = std::move(request.project_id),
        .cohort_id = std::move(request.cohort_id),
        .expected_revision = request.expected_revision,
        .created_at_utc = clock_.now_utc_iso8601(),
        .members = std::move(request.members),
    };
    require_text(store_request.created_at_utc, 200U, "timestamp");
    const auto result = store_.append_revision(store_request);
    if (result != CohortStoreWriteResult::stored) throw_store_error(result);
    auto stored = store_.find(store_request.project_id, store_request.cohort_id);
    if (!stored.has_value()) {
        throw CohortRegistryError{CohortRegistryErrorCode::invalid_request,
                                  "Revised cohort disappeared from registry"};
    }
    return std::move(*stored);
}

std::optional<CohortDefinition> CohortRegistryService::find(
    const std::string_view project_id,
    const std::string_view cohort_id,
    const std::optional<std::uint32_t> revision
) {
    return store_.find(project_id, cohort_id, revision);
}

std::vector<CohortDefinition> CohortRegistryService::list(const std::string_view project_id) {
    return store_.list(project_id);
}

}  // namespace biocore::application
