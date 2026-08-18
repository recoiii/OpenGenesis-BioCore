#pragma once

#include "biocore/domain/project.hpp"

namespace biocore::infrastructure::sqlite {

class SqliteConnection;

class ProjectDatabaseInitializer final {
public:
    explicit ProjectDatabaseInitializer(SqliteConnection& connection) noexcept;

    void initialize(const domain::Project& project);

private:
    SqliteConnection& connection_;
};

}  // namespace biocore::infrastructure::sqlite
