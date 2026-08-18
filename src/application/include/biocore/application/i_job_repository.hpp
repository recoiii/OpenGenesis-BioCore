#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "biocore/domain/job.hpp"

namespace biocore::application {

class IJobRepository {
public:
    virtual ~IJobRepository() = default;

    // Inserts a new job. Returns false only when the identifier already exists.
    // Other persistence failures are reported as exceptions by the concrete adapter.
    virtual bool add(const domain::Job& job) = 0;
    [[nodiscard]] virtual std::optional<domain::Job> find_by_id(std::string_view job_id) = 0;
    [[nodiscard]] virtual std::vector<domain::Job> list() = 0;

    // Persists only mutable runtime fields using optimistic concurrency.
    // The supplied job revision must be exactly expected_revision + 1.
    // Returns false when the record is missing or its revision no longer matches.
    virtual bool update_runtime_state(
        const domain::Job& job,
        std::int64_t expected_revision
    ) = 0;
};

}  // namespace biocore::application
