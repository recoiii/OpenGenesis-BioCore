#include "biocore/infrastructure/sqlite/sqlite_cohort_registry_store.hpp"

#include <sqlite3.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

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
    Statement(sqlite3* database, const char* sql) : database_{database} {
        const int result = sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr);
        if (result != SQLITE_OK) {
            throw SqliteError{result, std::string{"Unable to prepare cohort registry statement: "} +
                sqlite3_errmsg(database_)};
        }
    }
    ~Statement() {
        if (statement_ != nullptr) sqlite3_finalize(statement_);
    }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind(const int index, const std::string_view value) {
        const int result = sqlite3_bind_text(
            statement_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
        if (result != SQLITE_OK) {
            throw SqliteError{result, std::string{"Unable to bind cohort registry text: "} +
                sqlite3_errmsg(database_)};
        }
    }
    void bind(const int index, const std::int64_t value) {
        const int result = sqlite3_bind_int64(statement_, index, value);
        if (result != SQLITE_OK) {
            throw SqliteError{result, std::string{"Unable to bind cohort registry integer: "} +
                sqlite3_errmsg(database_)};
        }
    }
    void bind_null(const int index) {
        const int result = sqlite3_bind_null(statement_, index);
        if (result != SQLITE_OK) {
            throw SqliteError{result, std::string{"Unable to bind cohort registry NULL: "} +
                sqlite3_errmsg(database_)};
        }
    }
    [[nodiscard]] int step() {
        return sqlite3_step(statement_);
    }
    void require_done() {
        const int result = step();
        if (result != SQLITE_DONE) {
            throw SqliteError{result, std::string{"Unable to execute cohort registry statement: "} +
                sqlite3_errmsg(database_)};
        }
    }
    void reset() {
        const int reset_result = sqlite3_reset(statement_);
        const int clear_result = sqlite3_clear_bindings(statement_);
        if (reset_result != SQLITE_OK || clear_result != SQLITE_OK) {
            throw SqliteError{
                reset_result != SQLITE_OK ? reset_result : clear_result,
                std::string{"Unable to reset cohort registry statement: "} + sqlite3_errmsg(database_)};
        }
    }
    [[nodiscard]] std::string text(const int column) const {
        const auto* value = sqlite3_column_text(statement_, column);
        if (value == nullptr) return {};
        return {reinterpret_cast<const char*>(value),
                static_cast<std::size_t>(sqlite3_column_bytes(statement_, column))};
    }
    [[nodiscard]] std::int64_t integer(const int column) const {
        return sqlite3_column_int64(statement_, column);
    }
    [[nodiscard]] bool is_null(const int column) const {
        return sqlite3_column_type(statement_, column) == SQLITE_NULL;
    }

private:
    sqlite3* database_;
    sqlite3_stmt* statement_{nullptr};
};

[[nodiscard]] bool is_ascii_space(const unsigned char value) noexcept {
    return value == ' ' || value == '\t' || value == '\n' || value == '\v' ||
           value == '\f' || value == '\r';
}

[[nodiscard]] bool valid_text(
    const std::string_view value,
    const std::size_t maximum_bytes,
    const bool allow_empty = false) {
    if (value.size() > maximum_bytes || value.find('\0') != std::string_view::npos) return false;
    if (value.empty()) return allow_empty;
    for (const unsigned char value_byte : value) {
        if (!is_ascii_space(value_byte)) return true;
    }
    return allow_empty;
}

[[nodiscard]] bool owns_project(sqlite3* database, const std::string_view project_id) {
    Statement query{database,
        "SELECT 1 FROM project_metadata WHERE singleton=1 AND project_id=?;"};
    query.bind(1, project_id);
    const int result = query.step();
    if (result == SQLITE_ROW) return true;
    if (result == SQLITE_DONE) return false;
    throw SqliteError{result, std::string{"Unable to verify cohort project: "} +
        sqlite3_errmsg(database)};
}

[[nodiscard]] bool cohort_exists(
    sqlite3* database,
    const std::string_view project_id,
    const std::string_view cohort_id) {
    Statement query{database,
        "SELECT 1 FROM cohort_definitions WHERE project_id=? AND cohort_id=?;"};
    query.bind(1, project_id);
    query.bind(2, cohort_id);
    const int result = query.step();
    if (result == SQLITE_ROW) return true;
    if (result == SQLITE_DONE) return false;
    throw SqliteError{result, std::string{"Unable to inspect cohort identity: "} +
        sqlite3_errmsg(database)};
}

[[nodiscard]] std::optional<std::int64_t> current_revision(
    sqlite3* database,
    const std::string_view project_id,
    const std::string_view cohort_id) {
    Statement query{database,
        "SELECT current_revision FROM cohort_definitions WHERE project_id=? AND cohort_id=?;"};
    query.bind(1, project_id);
    query.bind(2, cohort_id);
    const int result = query.step();
    if (result == SQLITE_DONE) return std::nullopt;
    if (result != SQLITE_ROW) {
        throw SqliteError{result, std::string{"Unable to read cohort revision: "} +
            sqlite3_errmsg(database)};
    }
    return query.integer(0);
}

[[nodiscard]] std::optional<std::string> sample_display_name(
    sqlite3* database,
    const std::string_view project_id,
    const std::string_view sample_id) {
    Statement query{database,
        "SELECT display_name FROM project_samples WHERE project_id=? AND sample_id=?;"};
    query.bind(1, project_id);
    query.bind(2, sample_id);
    const int result = query.step();
    if (result == SQLITE_DONE) return std::nullopt;
    if (result != SQLITE_ROW) {
        throw SqliteError{result, std::string{"Unable to read cohort sample: "} +
            sqlite3_errmsg(database)};
    }
    return query.text(0);
}

[[nodiscard]] application::CohortWriteResult validate_members(
    sqlite3* database,
    const std::string_view project_id,
    const std::span<const domain::CohortMemberDraft> members,
    std::vector<std::string>& display_names) {
    std::unordered_set<std::string> seen;
    seen.reserve(members.size());
    display_names.clear();
    display_names.reserve(members.size());

    for (const auto& member : members) {
        if (!valid_text(member.sample_id, 128U) ||
            !valid_text(member.biological_unit_id, 128U) ||
            !valid_text(member.exclusion_reason, 512U, true) ||
            (member.included && !member.exclusion_reason.empty()) ||
            (!member.included && member.exclusion_reason.empty())) {
            return application::CohortWriteResult::invalid_request;
        }
        if (!seen.emplace(member.sample_id).second) {
            return application::CohortWriteResult::duplicate_sample;
        }
        const auto display = sample_display_name(database, project_id, member.sample_id);
        if (!display) return application::CohortWriteResult::sample_not_found;
        display_names.push_back(*display);
    }
    return application::CohortWriteResult::created;
}

void insert_revision(
    sqlite3* database,
    const std::string_view project_id,
    const std::string_view cohort_id,
    const std::int64_t revision,
    const std::optional<std::int64_t> parent_revision,
    const std::string_view created_at_utc,
    const std::span<const domain::CohortMemberDraft> members,
    const std::vector<std::string>& display_names) {
    Statement revision_insert{database,
        "INSERT INTO cohort_revisions("
        "project_id,cohort_id,revision,parent_revision,created_at_utc,sealed"
        ") VALUES(?,?,?,?,?,0);"};
    revision_insert.bind(1, project_id);
    revision_insert.bind(2, cohort_id);
    revision_insert.bind(3, revision);
    if (parent_revision) revision_insert.bind(4, *parent_revision);
    else revision_insert.bind_null(4);
    revision_insert.bind(5, created_at_utc);
    revision_insert.require_done();

    Statement member_insert{database,
        "INSERT INTO cohort_revision_members("
        "project_id,cohort_id,revision,ordinal,sample_id,display_name_copy,"
        "biological_unit_id,group_token,included,exclusion_reason"
        ") VALUES(?,?,?,?,?,?,?,?,?,?);"};
    for (std::size_t index = 0; index < members.size(); ++index) {
        const auto& member = members[index];
        member_insert.bind(1, project_id);
        member_insert.bind(2, cohort_id);
        member_insert.bind(3, revision);
        member_insert.bind(4, static_cast<std::int64_t>(index));
        member_insert.bind(5, member.sample_id);
        member_insert.bind(6, display_names[index]);
        member_insert.bind(7, member.biological_unit_id);
        member_insert.bind(8, domain::to_string(member.group));
        member_insert.bind(9, static_cast<std::int64_t>(member.included ? 1 : 0));
        member_insert.bind(10, member.exclusion_reason);
        member_insert.require_done();
        member_insert.reset();
    }

    Statement seal{database,
        "UPDATE cohort_revisions SET sealed=1 "
        "WHERE project_id=? AND cohort_id=? AND revision=? AND sealed=0;"};
    seal.bind(1, project_id);
    seal.bind(2, cohort_id);
    seal.bind(3, revision);
    seal.require_done();
    if (sqlite3_changes(database) != 1) {
        throw SqliteError{SQLITE_CONSTRAINT, "Unable to seal cohort revision"};
    }
}

}  // namespace

SqliteCohortRegistryStore::SqliteCohortRegistryStore(SqliteConnection& connection) noexcept
    : connection_{connection} {}

std::optional<std::vector<domain::CohortDefinition>> SqliteCohortRegistryStore::list(
    const std::string_view project_id) {
    sqlite3* const database = connection_.native_handle();
    if (!owns_project(database, project_id)) return std::nullopt;

    Statement query{database,
        "SELECT project_id,cohort_id,current_revision,name,created_at_utc,updated_at_utc "
        "FROM cohort_definitions WHERE project_id=? ORDER BY cohort_id COLLATE BINARY;"};
    query.bind(1, project_id);
    std::vector<domain::CohortDefinition> definitions;
    for (;;) {
        const int result = query.step();
        if (result == SQLITE_DONE) return definitions;
        if (result != SQLITE_ROW) {
            throw SqliteError{result, std::string{"Unable to list cohorts: "} +
                sqlite3_errmsg(database)};
        }
        definitions.push_back({
            query.text(0), query.text(1), query.integer(2),
            query.text(3), query.text(4), query.text(5)});
    }
}

std::optional<domain::CohortDefinition> SqliteCohortRegistryStore::get(
    const std::string_view project_id,
    const std::string_view cohort_id) {
    sqlite3* const database = connection_.native_handle();
    Statement query{database,
        "SELECT project_id,cohort_id,current_revision,name,created_at_utc,updated_at_utc "
        "FROM cohort_definitions WHERE project_id=? AND cohort_id=?;"};
    query.bind(1, project_id);
    query.bind(2, cohort_id);
    const int result = query.step();
    if (result == SQLITE_DONE) return std::nullopt;
    if (result != SQLITE_ROW) {
        throw SqliteError{result, std::string{"Unable to read cohort: "} +
            sqlite3_errmsg(database)};
    }
    return domain::CohortDefinition{
        query.text(0), query.text(1), query.integer(2),
        query.text(3), query.text(4), query.text(5)};
}

std::optional<domain::CohortRevision> SqliteCohortRegistryStore::get_revision(
    const std::string_view project_id,
    const std::string_view cohort_id,
    const std::int64_t revision) {
    sqlite3* const database = connection_.native_handle();
    Statement revision_query{database,
        "SELECT project_id,cohort_id,revision,parent_revision,created_at_utc "
        "FROM cohort_revisions "
        "WHERE project_id=? AND cohort_id=? AND revision=? AND sealed=1;"};
    revision_query.bind(1, project_id);
    revision_query.bind(2, cohort_id);
    revision_query.bind(3, revision);
    const int revision_result = revision_query.step();
    if (revision_result == SQLITE_DONE) return std::nullopt;
    if (revision_result != SQLITE_ROW) {
        throw SqliteError{revision_result, std::string{"Unable to read cohort revision: "} +
            sqlite3_errmsg(database)};
    }

    domain::CohortRevision output;
    output.project_id = revision_query.text(0);
    output.cohort_id = revision_query.text(1);
    output.revision = revision_query.integer(2);
    if (!revision_query.is_null(3)) output.parent_revision = revision_query.integer(3);
    output.created_at_utc = revision_query.text(4);

    Statement member_query{database,
        "SELECT sample_id,display_name_copy,biological_unit_id,group_token,"
        "included,exclusion_reason FROM cohort_revision_members "
        "WHERE project_id=? AND cohort_id=? AND revision=? ORDER BY ordinal;"};
    member_query.bind(1, project_id);
    member_query.bind(2, cohort_id);
    member_query.bind(3, revision);
    for (;;) {
        const int result = member_query.step();
        if (result == SQLITE_DONE) break;
        if (result != SQLITE_ROW) {
            throw SqliteError{result, std::string{"Unable to read cohort members: "} +
                sqlite3_errmsg(database)};
        }
        const auto group = domain::cohort_group_from_string(member_query.text(3));
        if (!group) throw SqliteError{SQLITE_CORRUPT, "Unknown persisted cohort group"};
        output.members.push_back({
            member_query.text(0),
            member_query.text(1),
            member_query.text(2),
            *group,
            member_query.integer(4) != 0,
            member_query.text(5),
        });
    }
    return output;
}

application::CohortWriteResult SqliteCohortRegistryStore::create(
    const std::string_view project_id,
    const std::string_view cohort_id,
    const std::string_view name,
    const std::string_view created_at_utc,
    const std::span<const domain::CohortMemberDraft> members) {
    if (!valid_text(project_id, 128U) || !valid_text(cohort_id, 128U) ||
        !valid_text(name, 200U) || !valid_text(created_at_utc, 200U)) {
        return application::CohortWriteResult::invalid_request;
    }

    Transaction transaction{connection_};
    sqlite3* const database = connection_.native_handle();
    if (!owns_project(database, project_id)) {
        return application::CohortWriteResult::project_not_found;
    }
    if (cohort_exists(database, project_id, cohort_id)) {
        return application::CohortWriteResult::cohort_already_exists;
    }

    std::vector<std::string> display_names;
    const auto member_result = validate_members(database, project_id, members, display_names);
    if (member_result != application::CohortWriteResult::created) return member_result;

    Statement definition_insert{database,
        "INSERT INTO cohort_definitions("
        "project_id,cohort_id,name,current_revision,created_at_utc,updated_at_utc"
        ") VALUES(?,?,?,0,?,?);"};
    definition_insert.bind(1, project_id);
    definition_insert.bind(2, cohort_id);
    definition_insert.bind(3, name);
    definition_insert.bind(4, created_at_utc);
    definition_insert.bind(5, created_at_utc);
    definition_insert.require_done();

    insert_revision(
        database, project_id, cohort_id, 1, std::nullopt,
        created_at_utc, members, display_names);

    Statement advance{database,
        "UPDATE cohort_definitions SET current_revision=1,updated_at_utc=? "
        "WHERE project_id=? AND cohort_id=? AND current_revision=0;"};
    advance.bind(1, created_at_utc);
    advance.bind(2, project_id);
    advance.bind(3, cohort_id);
    advance.require_done();
    if (sqlite3_changes(database) != 1) {
        throw SqliteError{SQLITE_CONSTRAINT, "Unable to publish initial cohort revision"};
    }

    transaction.commit();
    return application::CohortWriteResult::created;
}

application::CohortWriteResult SqliteCohortRegistryStore::revise(
    const std::string_view project_id,
    const std::string_view cohort_id,
    const std::int64_t expected_revision,
    const std::string_view name,
    const std::string_view created_at_utc,
    const std::span<const domain::CohortMemberDraft> members) {
    if (!valid_text(project_id, 128U) || !valid_text(cohort_id, 128U) ||
        !valid_text(name, 200U) || !valid_text(created_at_utc, 200U) ||
        expected_revision < 1) {
        return application::CohortWriteResult::invalid_request;
    }

    Transaction transaction{connection_};
    sqlite3* const database = connection_.native_handle();
    if (!owns_project(database, project_id)) {
        return application::CohortWriteResult::project_not_found;
    }
    const auto current = current_revision(database, project_id, cohort_id);
    if (!current) return application::CohortWriteResult::cohort_not_found;
    if (*current != expected_revision) return application::CohortWriteResult::stale_revision;

    std::vector<std::string> display_names;
    auto member_result = validate_members(database, project_id, members, display_names);
    if (member_result != application::CohortWriteResult::created) return member_result;

    const std::int64_t next_revision = expected_revision + 1;
    insert_revision(
        database, project_id, cohort_id, next_revision, expected_revision,
        created_at_utc, members, display_names);

    Statement advance{database,
        "UPDATE cohort_definitions SET current_revision=?,name=?,updated_at_utc=? "
        "WHERE project_id=? AND cohort_id=? AND current_revision=?;"};
    advance.bind(1, next_revision);
    advance.bind(2, name);
    advance.bind(3, created_at_utc);
    advance.bind(4, project_id);
    advance.bind(5, cohort_id);
    advance.bind(6, expected_revision);
    advance.require_done();
    if (sqlite3_changes(database) != 1) {
        return application::CohortWriteResult::stale_revision;
    }

    transaction.commit();
    return application::CohortWriteResult::revised;
}

}  // namespace biocore::infrastructure::sqlite
