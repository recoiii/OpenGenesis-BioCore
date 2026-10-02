#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/cohort_registry.hpp"

namespace biocore::application {

class ICohortRegistryStore;
class IIdGenerator;
class IUtcClock;

enum class CohortRegistryErrorCode {
    project_not_found,
    cohort_not_found,
    stale_revision,
    duplicate_sample,
    sample_not_found,
    invalid_request,
    id_generation_exhausted
};

class CohortRegistryError final : public std::runtime_error {
public:
    CohortRegistryError(CohortRegistryErrorCode code, std::string message);
    [[nodiscard]] CohortRegistryErrorCode code() const noexcept;
private:
    CohortRegistryErrorCode code_;
};

struct CreateCohortRequest final {
    std::string project_id;
    std::string name;
    std::vector<CohortMemberDraft> members;
};

struct ReviseCohortRequest final {
    std::string project_id;
    std::string cohort_id;
    std::uint32_t expected_revision{0U};
    std::vector<CohortMemberDraft> members;
};

class CohortRegistryService final {
public:
    static constexpr std::size_t maximum_id_generation_attempts = 8U;

    CohortRegistryService(
        ICohortRegistryStore& store,
        IIdGenerator& id_generator,
        IUtcClock& clock
    ) noexcept;

    [[nodiscard]] CohortDefinition create(CreateCohortRequest request);
    [[nodiscard]] CohortDefinition revise(ReviseCohortRequest request);

    [[nodiscard]] std::optional<CohortDefinition> find(
        std::string_view project_id,
        std::string_view cohort_id,
        std::optional<std::uint32_t> revision = std::nullopt
    );

    [[nodiscard]] std::vector<CohortDefinition> list(std::string_view project_id);

private:
    ICohortRegistryStore& store_;
    IIdGenerator& id_generator_;
    IUtcClock& clock_;
};

}  // namespace biocore::application
