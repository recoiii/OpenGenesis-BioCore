#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "biocore/application/i_job_repository.hpp"

namespace biocore::infrastructure::sqlite {

class SqliteConnection;

class SqliteJobRepository final : public application::IJobRepository {
public:
    explicit SqliteJobRepository(SqliteConnection& connection) noexcept;

    bool add(const domain::Job& job) override;
    [[nodiscard]] std::optional<domain::Job> find_by_id(std::string_view job_id) override;
    [[nodiscard]] std::vector<domain::Job> list() override;
    bool update_runtime_state(const domain::Job& job, std::int64_t expected_revision) override;

private:
    SqliteConnection& connection_;
};

}  // namespace biocore::infrastructure::sqlite
