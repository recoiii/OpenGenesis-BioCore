#include "biocore/infrastructure/sqlite/sqlite_project_research_metadata_store.hpp"

#include <sqlite3.h>
#include <limits>
#include <stdexcept>
#include <string>
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"

namespace biocore::infrastructure::sqlite {
namespace {
class Statement final {
public:
    Statement(sqlite3* database, const char* sql) : database_{database} {
        require(sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr));
    }
    ~Statement() { sqlite3_finalize(statement_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    void bind(const int index, const std::string_view value) {
        require(sqlite3_bind_text64(statement_, index, value.empty() ? "" : value.data(),
            static_cast<sqlite3_uint64>(value.size()), SQLITE_TRANSIENT, SQLITE_UTF8));
    }
    void bind(const int index, const std::int64_t value) {
        require(sqlite3_bind_int64(statement_, index, value));
    }
    int step() {
        const int result = sqlite3_step(statement_);
        if (result != SQLITE_ROW && result != SQLITE_DONE) require(result);
        return result;
    }
    std::string text(const int column) const {
        const auto* value = sqlite3_column_text(statement_, column);
        if (value == nullptr) throw SqliteError{SQLITE_MISMATCH, "NULL project metadata"};
        return {reinterpret_cast<const char*>(value),
                static_cast<std::size_t>(sqlite3_column_bytes(statement_, column))};
    }
    std::int64_t integer(const int column) const { return sqlite3_column_int64(statement_, column); }
private:
    void require(const int result) const {
        if (result != SQLITE_OK) throw SqliteError{result,
            std::string{"Project research metadata: "} + sqlite3_errmsg(database_)};
    }
    sqlite3* database_;
    sqlite3_stmt* statement_{nullptr};
};
}

SqliteProjectResearchMetadataStore::SqliteProjectResearchMetadataStore(
    SqliteConnection& connection) noexcept : connection_{connection} {}

std::optional<domain::ProjectResearchMetadata> SqliteProjectResearchMetadataStore::find(
    const std::string_view project_id) {
    Statement query{connection_.native_handle(),
        "SELECT project_id, research_description, research_organism, research_revision, "
        "research_updated_at_utc FROM project_metadata WHERE singleton=1 AND project_id=?;"};
    query.bind(1, project_id);
    if (query.step() == SQLITE_DONE) return std::nullopt;
    return domain::ProjectResearchMetadata{query.text(0), query.text(1), query.text(2),
        query.integer(3), query.text(4)};
}

bool SqliteProjectResearchMetadataStore::update(
    const domain::ProjectResearchMetadata& metadata, const std::int64_t expected_revision) {
    if (expected_revision < 0 || expected_revision == std::numeric_limits<std::int64_t>::max() ||
        metadata.revision() != expected_revision + 1) {
        throw std::invalid_argument{"Metadata revision must advance by exactly one"};
    }
    Statement update{connection_.native_handle(),
        "UPDATE project_metadata SET research_description=?, research_organism=?, "
        "research_revision=?, research_updated_at_utc=? "
        "WHERE singleton=1 AND project_id=? AND research_revision=? RETURNING research_revision;"};
    update.bind(1, metadata.description());
    update.bind(2, metadata.organism());
    update.bind(3, metadata.revision());
    update.bind(4, metadata.updated_at_utc());
    update.bind(5, metadata.project_id());
    update.bind(6, expected_revision);
    if (update.step() == SQLITE_DONE) return false;
    if (update.step() != SQLITE_DONE) throw SqliteError{SQLITE_ERROR, "Unexpected metadata update row"};
    return true;
}
}  // namespace biocore::infrastructure::sqlite
