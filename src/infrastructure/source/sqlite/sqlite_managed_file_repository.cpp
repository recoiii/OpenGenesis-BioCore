#include "biocore/infrastructure/sqlite/sqlite_managed_file_repository.hpp"

#include <sqlite3.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/storage_mode.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"

namespace biocore::infrastructure::sqlite {
namespace {

class Statement final {
public:
    Statement(sqlite3* database, const char* sql) : database_{database} {
        const int result = sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr);
        if (result != SQLITE_OK) {
            throw SqliteError{
                result,
                std::string{"Unable to prepare managed file statement: "} + sqlite3_errmsg(database_),
            };
        }
    }

    ~Statement() {
        if (statement_ != nullptr) {
            sqlite3_finalize(statement_);
        }
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind_text(const int index, const std::string_view value) {
        require_bind(sqlite3_bind_text64(
            statement_,
            index,
            value.data(),
            static_cast<sqlite3_uint64>(value.size()),
            SQLITE_TRANSIENT,
            SQLITE_UTF8
        ));
    }

    void bind_optional_text(const int index, const std::optional<std::string>& value) {
        if (value.has_value()) {
            bind_text(index, *value);
        } else {
            require_bind(sqlite3_bind_null(statement_, index));
        }
    }

    void bind_integer(const int index, const std::int64_t value) {
        require_bind(sqlite3_bind_int64(statement_, index, value));
    }

    void bind_double(const int index, const double value) {
        require_bind(sqlite3_bind_double(statement_, index, value));
    }

    [[nodiscard]] int step() { return sqlite3_step(statement_); }

    [[nodiscard]] bool is_null(const int column) const noexcept {
        return sqlite3_column_type(statement_, column) == SQLITE_NULL;
    }

    [[nodiscard]] std::string text(const int column) const {
        const auto* value = sqlite3_column_text(statement_, column);
        if (value == nullptr) {
            throw SqliteError{SQLITE_MISMATCH, "Unexpected NULL in managed file record"};
        }
        return {
            reinterpret_cast<const char*>(value),
            static_cast<std::size_t>(sqlite3_column_bytes(statement_, column)),
        };
    }

    [[nodiscard]] std::optional<std::string> optional_text(const int column) const {
        if (is_null(column)) {
            return std::nullopt;
        }
        return text(column);
    }

    [[nodiscard]] std::int64_t integer(const int column) const noexcept {
        return sqlite3_column_int64(statement_, column);
    }

    [[nodiscard]] double real(const int column) const noexcept {
        return sqlite3_column_double(statement_, column);
    }

private:
    void require_bind(const int result) const {
        if (result != SQLITE_OK) {
            throw SqliteError{
                result,
                std::string{"Unable to bind managed file value: "} + sqlite3_errmsg(database_),
            };
        }
    }

    sqlite3* database_;
    sqlite3_stmt* statement_{nullptr};
};

class Transaction final {
public:
    explicit Transaction(SqliteConnection& connection) : connection_{connection} {
        connection_.execute("BEGIN IMMEDIATE;");
    }

    ~Transaction() {
        if (!committed_) {
            try {
                connection_.execute("ROLLBACK;");
            } catch (...) {
            }
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

constexpr const char* select_columns = R"sql(
    id,
    display_name,
    storage_mode,
    original_path,
    managed_path,
    relative_project_path,
    file_type,
    size_bytes,
    modified_at_utc,
    checksum_algorithm,
    checksum_value,
    created_at_utc,
    updated_at_utc
)sql";

[[nodiscard]] domain::ManagedFile read_file(const Statement& statement) {
    const auto mode = domain::storage_mode_from_string(statement.text(2));
    if (!mode.has_value()) {
        throw SqliteError{SQLITE_MISMATCH, "Managed file record contains an unsupported storage mode"};
    }

    try {
        return domain::ManagedFile{
            statement.text(0),
            statement.text(1),
            *mode,
            statement.optional_text(3),
            statement.optional_text(4),
            statement.optional_text(5),
            statement.text(6),
            statement.integer(7),
            statement.optional_text(8),
            statement.optional_text(9),
            statement.optional_text(10),
            statement.text(11),
            statement.text(12),
        };
    } catch (const std::invalid_argument& error) {
        throw SqliteError{
            SQLITE_MISMATCH,
            std::string{"Managed file record violates domain invariants: "} + error.what(),
        };
    }
}

}  // namespace

SqliteManagedFileRepository::SqliteManagedFileRepository(SqliteConnection& connection) noexcept
    : connection_{connection} {}

bool SqliteManagedFileRepository::add(const domain::ManagedFile& file) {
    constexpr const char* sql = R"sql(
        INSERT INTO managed_files(
            id, display_name, storage_mode, original_path, managed_path,
            relative_project_path, file_type, size_bytes, modified_at_utc,
            checksum_algorithm, checksum_value, created_at_utc, updated_at_utc
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )sql";

    sqlite3* database = connection_.native_handle();
    Statement statement{database, sql};
    statement.bind_text(1, file.id());
    statement.bind_text(2, file.display_name());
    statement.bind_text(3, domain::to_string(file.storage_mode()));
    statement.bind_optional_text(4, file.original_path());
    statement.bind_optional_text(5, file.managed_path());
    statement.bind_optional_text(6, file.relative_project_path());
    statement.bind_text(7, file.file_type());
    statement.bind_integer(8, file.size_bytes());
    statement.bind_optional_text(9, file.modified_at_utc());
    statement.bind_optional_text(10, file.checksum_algorithm());
    statement.bind_optional_text(11, file.checksum_value());
    statement.bind_text(12, file.created_at_utc());
    statement.bind_text(13, file.updated_at_utc());

    const int result = statement.step();
    if (result == SQLITE_DONE) {
        return true;
    }
    if (result == SQLITE_CONSTRAINT_PRIMARYKEY || result == SQLITE_CONSTRAINT_UNIQUE) {
        return false;
    }
    throw SqliteError{
        result,
        std::string{"Unable to insert managed file: "} + sqlite3_errmsg(database),
    };
}

std::optional<domain::ManagedFile> SqliteManagedFileRepository::find_by_id(
    const std::string_view file_id
) {
    const std::string sql =
        std::string{"SELECT "} + select_columns + " FROM managed_files WHERE id = ?;";
    Statement statement{connection_.native_handle(), sql.c_str()};
    statement.bind_text(1, file_id);
    const int result = statement.step();
    if (result == SQLITE_DONE) {
        return std::nullopt;
    }
    if (result != SQLITE_ROW) {
        throw SqliteError{
            result,
            std::string{"Unable to find managed file: "} + sqlite3_errmsg(connection_.native_handle()),
        };
    }
    return read_file(statement);
}

std::optional<domain::ManagedFile> SqliteManagedFileRepository::find_by_relative_project_path(
    const std::string_view relative_project_path
) {
    const std::string sql = std::string{"SELECT "} + select_columns +
                            " FROM managed_files WHERE relative_project_path = ?;";
    Statement statement{connection_.native_handle(), sql.c_str()};
    statement.bind_text(1, relative_project_path);
    const int result = statement.step();
    if (result == SQLITE_DONE) {
        return std::nullopt;
    }
    if (result != SQLITE_ROW) {
        throw SqliteError{
            result,
            std::string{"Unable to find managed file by relative path: "} +
                sqlite3_errmsg(connection_.native_handle()),
        };
    }
    return read_file(statement);
}

std::vector<domain::ManagedFile> SqliteManagedFileRepository::list() {
    const std::string sql = std::string{"SELECT "} + select_columns +
                            " FROM managed_files ORDER BY created_at_utc, id;";
    Statement statement{connection_.native_handle(), sql.c_str()};
    std::vector<domain::ManagedFile> files;
    for (;;) {
        const int result = statement.step();
        if (result == SQLITE_DONE) {
            return files;
        }
        if (result != SQLITE_ROW) {
            throw SqliteError{
                result,
                std::string{"Unable to list managed files: "} +
                    sqlite3_errmsg(connection_.native_handle()),
            };
        }
        files.push_back(read_file(statement));
    }
}


bool SqliteManagedFileRepository::add_generated_output(
    const domain::ManagedFile& file,
    const application::GeneratedOutputProvenance& provenance
) {
    const application::GeneratedOutputArtifact artifact{file, provenance};
    return add_generated_outputs_batch(std::span{&artifact, 1U});
}

bool SqliteManagedFileRepository::add_generated_outputs_batch(
    const std::span<const application::GeneratedOutputArtifact> artifacts
) {
    if (artifacts.empty()) {
        throw std::invalid_argument("Generated output batch must not be empty");
    }
    const auto& first = artifacts.front().provenance;
    for (const auto& artifact : artifacts) {
        const auto& file = artifact.file;
        const auto& provenance = artifact.provenance;
        if (file.storage_mode() != domain::StorageMode::generated_output ||
            !file.relative_project_path().has_value() ||
            *file.relative_project_path() != provenance.relative_project_path ||
            file.file_type() != provenance.file_type || provenance.job_id != first.job_id ||
            provenance.step_id != first.step_id || provenance.plugin_id != first.plugin_id ||
            provenance.plugin_version != first.plugin_version ||
            provenance.module_id != first.module_id) {
            throw std::invalid_argument(
                "Generated output batch files and provenance do not match one step"
            );
        }
    }

    Transaction transaction{connection_};
    constexpr const char* sql = R"sql(
        INSERT INTO generated_artifacts(
            managed_file_id, job_id, step_id, output_port, plugin_id, plugin_version,
            module_id, file_type, relative_project_path, step_progress, registered_at_utc
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )sql";
    sqlite3* database = connection_.native_handle();

    for (const auto& artifact : artifacts) {
        if (!add(artifact.file)) {
            return false;
        }

        const auto& provenance = artifact.provenance;
        Statement statement{database, sql};
        statement.bind_text(1, artifact.file.id());
        statement.bind_text(2, provenance.job_id);
        statement.bind_text(3, provenance.step_id);
        statement.bind_text(4, provenance.output_port);
        statement.bind_text(5, provenance.plugin_id);
        statement.bind_text(6, provenance.plugin_version);
        statement.bind_text(7, provenance.module_id);
        statement.bind_text(8, provenance.file_type);
        statement.bind_text(9, provenance.relative_project_path);
        statement.bind_double(10, provenance.step_progress);
        statement.bind_text(11, provenance.registered_at_utc);
        const int result = statement.step();
        if (result == SQLITE_DONE) {
            continue;
        }
        if (result == SQLITE_CONSTRAINT_PRIMARYKEY || result == SQLITE_CONSTRAINT_UNIQUE ||
            result == SQLITE_CONSTRAINT_FOREIGNKEY || result == SQLITE_CONSTRAINT_CHECK) {
            return false;
        }
        throw SqliteError{
            result,
            std::string{"Unable to insert generated artifact provenance: "} +
                sqlite3_errmsg(database),
        };
    }
    transaction.commit();
    return true;
}

std::optional<application::GeneratedOutputArtifact>
SqliteManagedFileRepository::find_generated_output(
    const std::string_view job_id,
    const std::string_view step_id,
    const std::string_view output_port
) {
    constexpr const char* sql = R"sql(
        SELECT managed_file_id, job_id, step_id, output_port, plugin_id, plugin_version,
               module_id, file_type, relative_project_path, step_progress, registered_at_utc
        FROM generated_artifacts
        WHERE job_id = ? AND step_id = ? AND output_port = ?;
    )sql";
    Statement statement{connection_.native_handle(), sql};
    statement.bind_text(1, job_id);
    statement.bind_text(2, step_id);
    statement.bind_text(3, output_port);
    const int result = statement.step();
    if (result == SQLITE_DONE) {
        return std::nullopt;
    }
    if (result != SQLITE_ROW) {
        throw SqliteError{
            result,
            std::string{"Unable to find generated artifact: "} +
                sqlite3_errmsg(connection_.native_handle()),
        };
    }

    const std::string file_id = statement.text(0);
    application::GeneratedOutputProvenance provenance{
        .job_id = statement.text(1),
        .step_id = statement.text(2),
        .output_port = statement.text(3),
        .plugin_id = statement.text(4),
        .plugin_version = statement.text(5),
        .module_id = statement.text(6),
        .file_type = statement.text(7),
        .relative_project_path = statement.text(8),
        .step_progress = statement.real(9),
        .registered_at_utc = statement.text(10),
    };
    const auto file = find_by_id(file_id);
    if (!file.has_value()) {
        throw SqliteError{SQLITE_CORRUPT, "Generated artifact references a missing managed file"};
    }
    return application::GeneratedOutputArtifact{*file, std::move(provenance)};
}

std::vector<application::GeneratedOutputArtifact>
SqliteManagedFileRepository::list_generated_outputs(const std::string_view job_id) {
    constexpr const char* sql = R"sql(
        SELECT step_id, output_port
        FROM generated_artifacts
        WHERE job_id = ?
        ORDER BY step_id, output_port;
    )sql";
    Statement statement{connection_.native_handle(), sql};
    statement.bind_text(1, job_id);
    std::vector<std::pair<std::string, std::string>> keys;
    for (;;) {
        const int result = statement.step();
        if (result == SQLITE_DONE) break;
        if (result != SQLITE_ROW) {
            throw SqliteError{
                result,
                std::string{"Unable to list generated artifacts: "} +
                    sqlite3_errmsg(connection_.native_handle()),
            };
        }
        keys.emplace_back(statement.text(0), statement.text(1));
    }

    std::vector<application::GeneratedOutputArtifact> artifacts;
    artifacts.reserve(keys.size());
    for (const auto& [step_id, output_port] : keys) {
        auto artifact = find_generated_output(job_id, step_id, output_port);
        if (!artifact.has_value()) {
            throw SqliteError{SQLITE_CORRUPT, "Generated artifact disappeared during listing"};
        }
        artifacts.push_back(std::move(*artifact));
    }
    return artifacts;
}


std::optional<double> SqliteManagedFileRepository::latest_generated_output_progress(
    const std::string_view job_id
) {
    constexpr const char* sql = R"sql(
        SELECT MAX(step_progress)
        FROM generated_artifacts
        WHERE job_id = ?;
    )sql";
    Statement statement{connection_.native_handle(), sql};
    statement.bind_text(1, job_id);
    const int result = statement.step();
    if (result != SQLITE_ROW) {
        throw SqliteError{
            result,
            std::string{"Unable to read generated artifact recovery progress: "} +
                sqlite3_errmsg(connection_.native_handle()),
        };
    }
    if (statement.is_null(0)) {
        return std::nullopt;
    }
    return statement.real(0);
}

}  // namespace biocore::infrastructure::sqlite
