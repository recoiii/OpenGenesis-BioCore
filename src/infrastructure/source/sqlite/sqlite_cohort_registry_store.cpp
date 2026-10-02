#include "biocore/infrastructure/sqlite/sqlite_cohort_registry_store.hpp"

#include <sqlite3.h>

#include <cstdint>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/cohort_registry.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"

namespace biocore::infrastructure::sqlite {
namespace {

class Transaction final {
public:
    explicit Transaction(SqliteConnection& connection) : connection_{connection} {
        connection_.execute("BEGIN IMMEDIATE;");
    }
    ~Transaction() {
        if (!committed_) {
            try { connection_.execute("ROLLBACK;"); } catch (...) {}
        }
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    void commit() {
        connection_.execute("COMMIT;");
        committed_ = true;
    }
private:
    SqliteConnection& connection_;
    bool committed_{false};
};

class Statement final {
public:
    Statement(sqlite3* database, const char* sql, std::string description)
        : database_{database}, description_{std::move(description)} {
        require(sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr));
    }
    ~Statement() { if (statement_ != nullptr) sqlite3_finalize(statement_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind_text(const int index, const std::string_view value) {
        require(sqlite3_bind_text64(statement_, index, value.data(),
            static_cast<sqlite3_uint64>(value.size()), SQLITE_TRANSIENT, SQLITE_UTF8));
    }
    void bind_integer(const int index, const std::int64_t value) {
        require(sqlite3_bind_int64(statement_, index, value));
    }
    void bind_null(const int index) { require(sqlite3_bind_null(statement_, index)); }
    void bind_optional_text(const int index, const std::optional<std::string>& value) {
        if (value.has_value()) bind_text(index, *value);
        else bind_null(index);
    }
    [[nodiscard]] int step() { return sqlite3_step(statement_); }
    [[nodiscard]] std::string text(const int column) const {
        const auto* raw = sqlite3_column_text(statement_, column);
        if (raw == nullptr) {
            throw SqliteError{SQLITE_MISMATCH, description_ + ": unexpected NULL text"};
        }
        return {reinterpret_cast<const char*>(raw),
                static_cast<std::size_t>(sqlite3_column_bytes(statement_, column))};
    }
    [[nodiscard]] std::optional<std::string> optional_text(const int column) const {
        if (sqlite3_column_type(statement_, column) == SQLITE_NULL) return std::nullopt;
        return text(column);
    }
    [[nodiscard]] std::int64_t integer(const int column) const noexcept {
        return sqlite3_column_int64(statement_, column);
    }
    void reset() {
        require(sqlite3_reset(statement_));
        require(sqlite3_clear_bindings(statement_));
    }
private:
    void require(const int result) const {
        if (result != SQLITE_OK) {
            throw SqliteError{result, description_ + ": " + sqlite3_errmsg(database_)};
        }
    }
    sqlite3* database_;
    std::string description_;
    sqlite3_stmt* statement_{nullptr};
};

void require_done(sqlite3* database, const int result, const std::string_view message) {
    if (result != SQLITE_DONE) {
        throw SqliteError{result, std::string{message} + ": " + sqlite3_errmsg(database)};
    }
}

[[nodiscard]] bool owns_project(sqlite3* database, const std::string_view project_id) {
    Statement statement{database,
        "SELECT 1 FROM project_metadata WHERE singleton=1 AND project_id=?;",
        "Unable to verify cohort project owner"};
    statement.bind_text(1, project_id);
    const int result = statement.step();
    if (result == SQLITE_ROW) return true;
    if (result == SQLITE_DONE) return false;
    throw SqliteError{result, std::string{"Unable to verify cohort project owner: "} +
        sqlite3_errmsg(database)};
}

[[nodiscard]] bool duplicate_samples(
    const std::vector<application::CohortMemberDraft>& members
) {
    std::set<std::string, std::less<>> ids;
    for (const auto& member : members) {
        if (!ids.emplace(member.sample_id).second) return true;
    }
    return false;
}

struct SampleSnapshot final {
    std::string display_name;
    std::string group_metadata;
};

[[nodiscard]] std::optional<SampleSnapshot> read_sample_snapshot(
    sqlite3* database,
    const std::string_view project_id,
    const std::string_view sample_id
) {
    Statement statement{database,
        "SELECT display_name,group_label FROM project_samples "
        "WHERE project_id=? AND sample_id=?;",
        "Unable to inspect cohort member sample"};
    statement.bind_text(1, project_id);
    statement.bind_text(2, sample_id);
    const int result = statement.step();
    if (result == SQLITE_DONE) return std::nullopt;
    if (result != SQLITE_ROW) {
        throw SqliteError{result, std::string{"Unable to inspect cohort member sample: "} +
            sqlite3_errmsg(database)};
    }
    return SampleSnapshot{statement.text(0), statement.text(1)};
}

void insert_revision(
    SqliteConnection& connection,
    const std::string_view project_id,
    const std::string_view cohort_id,
    const std::uint32_t revision,
    const std::optional<std::uint32_t> parent_revision,
    const std::string_view created_at_utc,
    const std::vector<application::CohortMemberDraft>& members
) {
    sqlite3* database = connection.native_handle();
    Statement revision_statement{database,
        "INSERT INTO cohort_revisions(project_id,cohort_id,revision,parent_revision,"
        "created_at_utc,sealed) VALUES(?,?,?,?,?,0);",
        "Unable to insert cohort revision"};
    revision_statement.bind_text(1, project_id);
    revision_statement.bind_text(2, cohort_id);
    revision_statement.bind_integer(3, revision);
    if (parent_revision.has_value()) revision_statement.bind_integer(4, *parent_revision);
    else revision_statement.bind_null(4);
    revision_statement.bind_text(5, created_at_utc);
    require_done(database, revision_statement.step(), "Unable to insert cohort revision");

    Statement member_statement{database,
        "INSERT INTO cohort_revision_members("
        "project_id,cohort_id,revision,sample_id,ordinal,sample_display_name,"
        "sample_group_metadata,biological_unit_id,group_token,disposition,exclusion_reason"
        ") VALUES(?,?,?,?,?,?,?,?,?,?,?);",
        "Unable to insert cohort revision member"};

    for (std::size_t index = 0U; index < members.size(); ++index) {
        const auto& member = members[index];
        const auto sample = read_sample_snapshot(database, project_id, member.sample_id);
        if (!sample.has_value()) {
            throw std::invalid_argument{"COHORT_SAMPLE_NOT_FOUND:" + member.sample_id};
        }
        member_statement.bind_text(1, project_id);
        member_statement.bind_text(2, cohort_id);
        member_statement.bind_integer(3, revision);
        member_statement.bind_text(4, member.sample_id);
        member_statement.bind_integer(5, static_cast<std::int64_t>(index));
        member_statement.bind_text(6, sample->display_name);
        member_statement.bind_text(7, sample->group_metadata);
        member_statement.bind_text(8, member.biological_unit_id);
        member_statement.bind_text(9, application::to_string(member.group));
        member_statement.bind_text(10, application::to_string(member.disposition));
        member_statement.bind_optional_text(11, member.exclusion_reason);
        require_done(database, member_statement.step(), "Unable to insert cohort revision member");
        member_statement.reset();
    }

    Statement seal{database,
        "UPDATE cohort_revisions SET sealed=1 "
        "WHERE project_id=? AND cohort_id=? AND revision=? AND sealed=0;",
        "Unable to seal cohort revision"};
    seal.bind_text(1, project_id);
    seal.bind_text(2, cohort_id);
    seal.bind_integer(3, revision);
    require_done(database, seal.step(), "Unable to seal cohort revision");
    if (sqlite3_changes(database) != 1) {
        throw SqliteError{SQLITE_CONSTRAINT, "Cohort revision could not be sealed exactly once"};
    }
}

[[nodiscard]] std::vector<application::CohortMemberSnapshot> read_members(
    SqliteConnection& connection,
    const std::string_view project_id,
    const std::string_view cohort_id,
    const std::uint32_t revision
) {
    Statement statement{connection.native_handle(),
        "SELECT sample_id,sample_display_name,sample_group_metadata,biological_unit_id,"
        "group_token,disposition,exclusion_reason FROM cohort_revision_members "
        "WHERE project_id=? AND cohort_id=? AND revision=? ORDER BY ordinal;",
        "Unable to read cohort revision members"};
    statement.bind_text(1, project_id);
    statement.bind_text(2, cohort_id);
    statement.bind_integer(3, revision);
    std::vector<application::CohortMemberSnapshot> members;
    for (;;) {
        const int result = statement.step();
        if (result == SQLITE_DONE) return members;
        if (result != SQLITE_ROW) {
            throw SqliteError{result, std::string{"Unable to read cohort revision members: "} +
                sqlite3_errmsg(connection.native_handle())};
        }
        const auto group = application::cohort_group_from_string(statement.text(4));
        const auto disposition = application::cohort_member_disposition_from_string(statement.text(5));
        if (!group.has_value() || !disposition.has_value()) {
            throw SqliteError{SQLITE_CORRUPT, "Persisted cohort member contains unsupported enum value"};
        }
        members.push_back(application::CohortMemberSnapshot{
            .sample_id = statement.text(0),
            .sample_display_name = statement.text(1),
            .sample_group_metadata = statement.text(2),
            .biological_unit_id = statement.text(3),
            .group = *group,
            .disposition = *disposition,
            .exclusion_reason = statement.optional_text(6),
        });
    }
}

}  // namespace

SqliteCohortRegistryStore::SqliteCohortRegistryStore(SqliteConnection& connection) noexcept
    : connection_{connection} {}

application::CohortStoreWriteResult SqliteCohortRegistryStore::create(
    const application::CreateCohortStoreRequest& request
) {
    if (duplicate_samples(request.members)) {
        return application::CohortStoreWriteResult::duplicate_sample;
    }
    Transaction transaction{connection_};
    sqlite3* database = connection_.native_handle();
    if (!owns_project(database, request.project_id)) {
        return application::CohortStoreWriteResult::project_not_found;
    }

    for (const auto& member : request.members) {
        if (!read_sample_snapshot(database, request.project_id, member.sample_id).has_value()) {
            return application::CohortStoreWriteResult::sample_not_found;
        }
    }

    try {
        Statement definition{database,
            "INSERT INTO cohort_definitions(project_id,cohort_id,name,current_revision,"
            "created_at_utc,updated_at_utc) VALUES(?,?,?,0,?,?);",
            "Unable to create cohort definition"};
        definition.bind_text(1, request.project_id);
        definition.bind_text(2, request.cohort_id);
        definition.bind_text(3, request.name);
        definition.bind_text(4, request.created_at_utc);
        definition.bind_text(5, request.created_at_utc);
        require_done(database, definition.step(), "Unable to create cohort definition");
    } catch (const SqliteError& error) {
        if (error.is_constraint_violation()) {
            Statement existing{database,
                "SELECT 1 FROM cohort_definitions WHERE project_id=? AND cohort_id=?;",
                "Unable to inspect cohort id conflict"};
            existing.bind_text(1, request.project_id);
            existing.bind_text(2, request.cohort_id);
            if (existing.step() == SQLITE_ROW) {
                return application::CohortStoreWriteResult::cohort_id_conflict;
            }
        }
        throw;
    }

    insert_revision(connection_, request.project_id, request.cohort_id, 1U,
                    std::nullopt, request.created_at_utc, request.members);

    Statement activate{database,
        "UPDATE cohort_definitions SET current_revision=1,updated_at_utc=? "
        "WHERE project_id=? AND cohort_id=? AND current_revision=0;",
        "Unable to activate cohort revision"};
    activate.bind_text(1, request.created_at_utc);
    activate.bind_text(2, request.project_id);
    activate.bind_text(3, request.cohort_id);
    require_done(database, activate.step(), "Unable to activate cohort revision");
    if (sqlite3_changes(database) != 1) {
        throw SqliteError{SQLITE_CONSTRAINT, "Cohort creation lost optimistic revision state"};
    }
    transaction.commit();
    return application::CohortStoreWriteResult::stored;
}

application::CohortStoreWriteResult SqliteCohortRegistryStore::append_revision(
    const application::AppendCohortRevisionStoreRequest& request
) {
    if (duplicate_samples(request.members)) {
        return application::CohortStoreWriteResult::duplicate_sample;
    }
    Transaction transaction{connection_};
    sqlite3* database = connection_.native_handle();
    if (!owns_project(database, request.project_id)) {
        return application::CohortStoreWriteResult::project_not_found;
    }

    Statement current{database,
        "SELECT current_revision FROM cohort_definitions "
        "WHERE project_id=? AND cohort_id=?;",
        "Unable to inspect cohort revision"};
    current.bind_text(1, request.project_id);
    current.bind_text(2, request.cohort_id);
    const int current_result = current.step();
    if (current_result == SQLITE_DONE) return application::CohortStoreWriteResult::cohort_not_found;
    if (current_result != SQLITE_ROW) {
        throw SqliteError{current_result, std::string{"Unable to inspect cohort revision: "} +
            sqlite3_errmsg(database)};
    }
    const auto actual_revision = static_cast<std::uint32_t>(current.integer(0));
    if (actual_revision != request.expected_revision) {
        return application::CohortStoreWriteResult::stale_revision;
    }

    for (const auto& member : request.members) {
        if (!read_sample_snapshot(database, request.project_id, member.sample_id).has_value()) {
            return application::CohortStoreWriteResult::sample_not_found;
        }
    }

    const std::uint32_t next_revision = actual_revision + 1U;
    insert_revision(connection_, request.project_id, request.cohort_id, next_revision,
                    actual_revision, request.created_at_utc, request.members);

    Statement activate{database,
        "UPDATE cohort_definitions SET current_revision=?,updated_at_utc=? "
        "WHERE project_id=? AND cohort_id=? AND current_revision=?;",
        "Unable to advance cohort revision"};
    activate.bind_integer(1, next_revision);
    activate.bind_text(2, request.created_at_utc);
    activate.bind_text(3, request.project_id);
    activate.bind_text(4, request.cohort_id);
    activate.bind_integer(5, actual_revision);
    require_done(database, activate.step(), "Unable to advance cohort revision");
    if (sqlite3_changes(database) != 1) {
        return application::CohortStoreWriteResult::stale_revision;
    }
    transaction.commit();
    return application::CohortStoreWriteResult::stored;
}

std::optional<application::CohortDefinition> SqliteCohortRegistryStore::find(
    const std::string_view project_id,
    const std::string_view cohort_id,
    const std::optional<std::uint32_t> revision
) {
    Statement definition{connection_.native_handle(),
        "SELECT name,current_revision,created_at_utc,updated_at_utc "
        "FROM cohort_definitions WHERE project_id=? AND cohort_id=?;",
        "Unable to find cohort definition"};
    definition.bind_text(1, project_id);
    definition.bind_text(2, cohort_id);
    const int definition_result = definition.step();
    if (definition_result == SQLITE_DONE) return std::nullopt;
    if (definition_result != SQLITE_ROW) {
        throw SqliteError{definition_result, std::string{"Unable to find cohort definition: "} +
            sqlite3_errmsg(connection_.native_handle())};
    }
    const std::uint32_t current_revision = static_cast<std::uint32_t>(definition.integer(1));
    const std::uint32_t selected_revision = revision.value_or(current_revision);
    if (selected_revision == 0U || selected_revision > current_revision) return std::nullopt;

    Statement revision_statement{connection_.native_handle(),
        "SELECT parent_revision,created_at_utc,sealed FROM cohort_revisions "
        "WHERE project_id=? AND cohort_id=? AND revision=?;",
        "Unable to read cohort revision"};
    revision_statement.bind_text(1, project_id);
    revision_statement.bind_text(2, cohort_id);
    revision_statement.bind_integer(3, selected_revision);
    const int revision_result = revision_statement.step();
    if (revision_result == SQLITE_DONE) {
        throw SqliteError{SQLITE_CORRUPT, "Cohort definition references a missing revision"};
    }
    if (revision_result != SQLITE_ROW) {
        throw SqliteError{revision_result, std::string{"Unable to read cohort revision: "} +
            sqlite3_errmsg(connection_.native_handle())};
    }
    if (revision_statement.integer(2) != 1) {
        throw SqliteError{SQLITE_CORRUPT, "Persisted cohort revision is not sealed"};
    }
    std::optional<std::uint32_t> parent_revision;
    // Schema guarantees parent is NULL only for revision 1.
    if (selected_revision > 1U) {
        parent_revision = static_cast<std::uint32_t>(revision_statement.integer(0));
    }

    application::CohortDefinition result{
        .project_id = std::string{project_id},
        .cohort_id = std::string{cohort_id},
        .name = definition.text(0),
        .current_revision = current_revision,
        .created_at_utc = definition.text(2),
        .updated_at_utc = definition.text(3),
        .revision = application::CohortRevision{
            .project_id = std::string{project_id},
            .cohort_id = std::string{cohort_id},
            .revision = selected_revision,
            .parent_revision = parent_revision,
            .created_at_utc = revision_statement.text(1),
            .members = read_members(connection_, project_id, cohort_id, selected_revision),
        },
    };
    return result;
}

std::vector<application::CohortDefinition> SqliteCohortRegistryStore::list(
    const std::string_view project_id
) {
    if (!owns_project(connection_.native_handle(), project_id)) return {};
    Statement statement{connection_.native_handle(),
        "SELECT cohort_id FROM cohort_definitions WHERE project_id=? "
        "ORDER BY created_at_utc,cohort_id COLLATE BINARY;",
        "Unable to list cohort definitions"};
    statement.bind_text(1, project_id);
    std::vector<std::string> ids;
    for (;;) {
        const int result = statement.step();
        if (result == SQLITE_DONE) break;
        if (result != SQLITE_ROW) {
            throw SqliteError{result, std::string{"Unable to list cohort definitions: "} +
                sqlite3_errmsg(connection_.native_handle())};
        }
        ids.push_back(statement.text(0));
    }
    std::vector<application::CohortDefinition> cohorts;
    cohorts.reserve(ids.size());
    for (const auto& id : ids) {
        auto value = find(project_id, id);
        if (!value.has_value()) {
            throw SqliteError{SQLITE_CORRUPT, "Listed cohort disappeared during read"};
        }
        cohorts.push_back(std::move(*value));
    }
    return cohorts;
}

}  // namespace biocore::infrastructure::sqlite
