#pragma once

namespace biocore::infrastructure::sqlite {

class SqliteConnection;

class ProjectDatabaseGuard final {
public:
    explicit ProjectDatabaseGuard(SqliteConnection& connection) noexcept;

    // Validate an existing database before migrations are allowed to mutate it.
    // Empty/pre-schema databases are allowed, but an existing migration ledger
    // must be an exact contiguous prefix of the supported OpenGenesis-BioCore history.
    void validate_before_migration() const;

    // Validate the fully migrated project database required by the v0.2 runtime.
    void validate_current_schema() const;

private:
    SqliteConnection& connection_;
};

}  // namespace biocore::infrastructure::sqlite
