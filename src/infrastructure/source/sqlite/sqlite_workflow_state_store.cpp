#include "biocore/infrastructure/sqlite/sqlite_workflow_state_store.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/domain/workflow_branch_decision.hpp"
#include "biocore/domain/workflow_checkpoint.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"
#include "biocore/pipeline_protocol/workflow_document_codec.hpp"

namespace biocore::infrastructure::sqlite {
namespace {

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

class Statement final {
public:
    Statement(
        sqlite3* database,
        const char* sql,
        const std::string_view description
    )
        : database_{database}, description_{description} {
        const int result = sqlite3_prepare_v2(
            database_, sql, -1, &statement_, nullptr
        );
        if (result != SQLITE_OK) {
            throw SqliteError{
                result,
                std::string{description_} + ": " + sqlite3_errmsg(database_)
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
        require(sqlite3_bind_text64(
            statement_,
            index,
            value.data(),
            static_cast<sqlite3_uint64>(value.size()),
            SQLITE_TRANSIENT,
            SQLITE_UTF8
        ));
    }

    void bind_integer(const int index, const std::int64_t value) {
        require(sqlite3_bind_int64(statement_, index, value));
    }

    void bind_optional_integer(
        const int index,
        const std::optional<std::int64_t>& value
    ) {
        if (value.has_value()) {
            bind_integer(index, *value);
        } else {
            require(sqlite3_bind_null(statement_, index));
        }
    }

    void bind_optional_text(
        const int index,
        const std::optional<std::string>& value
    ) {
        if (value.has_value()) {
            bind_text(index, *value);
        } else {
            require(sqlite3_bind_null(statement_, index));
        }
    }

    [[nodiscard]] int step() {
        return sqlite3_step(statement_);
    }

    [[nodiscard]] bool is_null(const int column) const noexcept {
        return sqlite3_column_type(statement_, column) == SQLITE_NULL;
    }

    [[nodiscard]] std::string text(const int column) const {
        const auto* raw = sqlite3_column_text(statement_, column);
        if (raw == nullptr) {
            throw SqliteError{
                SQLITE_MISMATCH,
                std::string{description_} + ": unexpected NULL text"
            };
        }
        return {
            reinterpret_cast<const char*>(raw),
            static_cast<std::size_t>(sqlite3_column_bytes(statement_, column))
        };
    }

    [[nodiscard]] std::int64_t integer(const int column) const noexcept {
        return sqlite3_column_int64(statement_, column);
    }

private:
    void require(const int result) const {
        if (result != SQLITE_OK) {
            throw SqliteError{
                result,
                std::string{description_} + ": " + sqlite3_errmsg(database_)
            };
        }
    }

    sqlite3* database_;
    std::string_view description_;
    sqlite3_stmt* statement_{nullptr};
};

void require_done(
    sqlite3* database,
    Statement& statement,
    const std::string_view operation
) {
    const int result = statement.step();
    if (result != SQLITE_DONE) {
        throw SqliteError{
            result,
            std::string{operation} + ": " + sqlite3_errmsg(database)
        };
    }
}
void validate_state(const application::PersistedWorkflowState& state) {
    if (state.revision < 0 || state.updated_at_utc.empty()) {
        throw std::invalid_argument("Persisted workflow state metadata is invalid");
    }
    if (state.checkpoint.workflow_id() != state.workflow.id()) {
        throw std::invalid_argument(
            "Persisted workflow checkpoint belongs to a different workflow"
        );
    }
    if (state.branch_decisions.has_value() &&
        state.branch_decisions->workflow_id() != state.workflow.id()) {
        throw std::invalid_argument(
            "Persisted workflow branch decisions belong to a different workflow"
        );
    }

    std::map<std::string, const domain::WorkflowNode*, std::less<>> workflow_nodes;
    for (const domain::WorkflowNode& node : state.workflow.nodes()) {
        workflow_nodes.emplace(std::string{node.id().value()}, &node);
    }

    if (state.checkpoint.nodes().size() != workflow_nodes.size()) {
        throw std::invalid_argument(
            "Persisted workflow checkpoint must contain every workflow node exactly once"
        );
    }

    std::set<std::string, std::less<>> checkpoint_ids;
    for (const domain::WorkflowNodeCheckpoint& checkpoint : state.checkpoint.nodes()) {
        const std::string node_id{checkpoint.node_id.value()};
        const auto node = workflow_nodes.find(node_id);
        if (node == workflow_nodes.end() ||
            !checkpoint_ids.emplace(node_id).second) {
            throw std::invalid_argument(
                "Persisted workflow checkpoint contains an unknown node"
            );
        }

        for (const domain::WorkflowCheckpointArtifact& artifact : checkpoint.outputs) {
            const auto output = std::ranges::find_if(
                node->second->outputs(),
                [&artifact](const auto& declared) {
                    return declared.name() == artifact.output_port;
                }
            );
            if (output == node->second->outputs().end()) {
                throw std::invalid_argument(
                    "Persisted checkpoint artifact references an unknown output port"
                );
            }
        }
    }

    if (state.branch_decisions.has_value()) {
        if (state.branch_decisions->decisions().size() != workflow_nodes.size()) {
            throw std::invalid_argument(
                "Persisted branch decisions must contain every workflow node exactly once"
            );
        }
        std::set<std::string, std::less<>> decision_ids;
        for (const domain::WorkflowBranchDecision& decision :
             state.branch_decisions->decisions()) {
            const std::string node_id{decision.node_id.value()};
            if (!workflow_nodes.contains(node_id) ||
                !decision_ids.emplace(node_id).second) {
                throw std::invalid_argument(
                    "Persisted branch decisions contain an unknown workflow node"
                );
            }
        }
    }
}

void insert_checkpoint_rows(
    sqlite3* database,
    const application::PersistedWorkflowState& state
) {
    constexpr const char* node_sql = R"sql(
        INSERT INTO workflow_node_checkpoints(
            workflow_id, node_id, ordinal, state, attempt_number, max_attempts,
            failure_message, failure_exit_code
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?);
    )sql";

    constexpr const char* artifact_sql = R"sql(
        INSERT INTO workflow_checkpoint_artifacts(
            workflow_id, node_id, output_port, ordinal,
            relative_project_path, size_bytes, sha256
        ) VALUES (?, ?, ?, ?, ?, ?, ?);
    )sql";

    std::int64_t node_ordinal = 0;
    for (const domain::WorkflowNodeCheckpoint& node : state.checkpoint.nodes()) {
        Statement node_statement{
            database, node_sql, "Unable to insert workflow node checkpoint"
        };
        node_statement.bind_text(1, state.workflow.id().value());
        node_statement.bind_text(2, node.node_id.value());
        node_statement.bind_integer(3, node_ordinal++);
        node_statement.bind_text(4, domain::to_string(node.state));
        node_statement.bind_integer(
            5, static_cast<std::int64_t>(node.attempt_number)
        );
        node_statement.bind_integer(
            6, static_cast<std::int64_t>(node.max_attempts)
        );
        if (node.failure.has_value()) {
            node_statement.bind_text(7, node.failure->message);
            node_statement.bind_optional_integer(8, node.failure->exit_code);
        } else {
            node_statement.bind_optional_text(7, std::nullopt);
            node_statement.bind_optional_integer(8, std::nullopt);
        }
        require_done(
            database, node_statement, "Unable to insert workflow node checkpoint"
        );

        std::int64_t artifact_ordinal = 0;
        for (const domain::WorkflowCheckpointArtifact& artifact : node.outputs) {
            Statement artifact_statement{
                database,
                artifact_sql,
                "Unable to insert workflow checkpoint artifact"
            };
            artifact_statement.bind_text(1, state.workflow.id().value());
            artifact_statement.bind_text(2, node.node_id.value());
            artifact_statement.bind_text(3, artifact.output_port);
            artifact_statement.bind_integer(4, artifact_ordinal++);
            artifact_statement.bind_text(5, artifact.relative_project_path);
            artifact_statement.bind_integer(6, artifact.size_bytes);
            artifact_statement.bind_text(7, artifact.sha256);
            require_done(
                database,
                artifact_statement,
                "Unable to insert workflow checkpoint artifact"
            );
        }
    }
}

void insert_branch_rows(
    sqlite3* database,
    const application::PersistedWorkflowState& state
) {
    if (!state.branch_decisions.has_value()) return;

    constexpr const char* sql = R"sql(
        INSERT INTO workflow_branch_decisions(
            workflow_id, node_id, ordinal, state, reason, condition_result
        ) VALUES (?, ?, ?, ?, ?, ?);
    )sql";

    std::int64_t ordinal = 0;
    for (const domain::WorkflowBranchDecision& decision :
         state.branch_decisions->decisions()) {
        Statement statement{
            database, sql, "Unable to insert workflow branch decision"
        };
        statement.bind_text(1, state.workflow.id().value());
        statement.bind_text(2, decision.node_id.value());
        statement.bind_integer(3, ordinal++);
        statement.bind_text(4, domain::to_string(decision.state));
        statement.bind_text(5, domain::to_string(decision.reason));
        if (decision.condition_result.has_value()) {
            statement.bind_integer(6, *decision.condition_result ? 1 : 0);
        } else {
            statement.bind_optional_integer(6, std::nullopt);
        }
        require_done(
            database, statement, "Unable to insert workflow branch decision"
        );
    }
}

[[nodiscard]] std::vector<domain::WorkflowCheckpointArtifact>
load_artifacts(
    sqlite3* database,
    const std::string_view workflow_id,
    const std::string_view node_id
) {
    constexpr const char* sql = R"sql(
        SELECT output_port, relative_project_path, size_bytes, sha256
        FROM workflow_checkpoint_artifacts
        WHERE workflow_id = ? AND node_id = ?
        ORDER BY ordinal;
    )sql";
    Statement statement{
        database, sql, "Unable to load workflow checkpoint artifacts"
    };
    statement.bind_text(1, workflow_id);
    statement.bind_text(2, node_id);

    std::vector<domain::WorkflowCheckpointArtifact> outputs;
    for (;;) {
        const int result = statement.step();
        if (result == SQLITE_DONE) break;
        if (result != SQLITE_ROW) {
            throw SqliteError{
                result,
                std::string{"Unable to load workflow checkpoint artifacts: "} +
                    sqlite3_errmsg(database)
            };
        }
        outputs.push_back(domain::WorkflowCheckpointArtifact{
            .output_port = statement.text(0),
            .relative_project_path = statement.text(1),
            .size_bytes = statement.integer(2),
            .sha256 = statement.text(3),
        });
    }
    return outputs;
}

[[nodiscard]] domain::WorkflowCheckpointManifest load_checkpoint(
    sqlite3* database,
    const std::string_view workflow_id,
    const std::uint32_t schema_version
) {
    constexpr const char* sql = R"sql(
        SELECT node_id, state, attempt_number, max_attempts,
               failure_message, failure_exit_code
        FROM workflow_node_checkpoints
        WHERE workflow_id = ?
        ORDER BY ordinal;
    )sql";
    Statement statement{
        database, sql, "Unable to load workflow node checkpoints"
    };
    statement.bind_text(1, workflow_id);

    std::vector<domain::WorkflowNodeCheckpoint> nodes;
    for (;;) {
        const int result = statement.step();
        if (result == SQLITE_DONE) break;
        if (result != SQLITE_ROW) {
            throw SqliteError{
                result,
                std::string{"Unable to load workflow node checkpoints: "} +
                    sqlite3_errmsg(database)
            };
        }

        const std::string node_id = statement.text(0);
        const auto state =
            domain::workflow_checkpoint_node_state_from_string(statement.text(1));
        if (!state.has_value()) {
            throw SqliteError{
                SQLITE_CORRUPT,
                "Workflow checkpoint contains an unknown node state"
            };
        }
        const std::int64_t attempt = statement.integer(2);
        const std::int64_t maximum = statement.integer(3);
        if (attempt < 0 || maximum < 0 ||
            attempt > std::numeric_limits<std::uint32_t>::max() ||
            maximum > std::numeric_limits<std::uint32_t>::max()) {
            throw SqliteError{
                SQLITE_CORRUPT,
                "Workflow checkpoint attempt values are out of range"
            };
        }

        std::optional<domain::WorkflowCheckpointFailure> failure;
        if (!statement.is_null(4)) {
            failure = domain::WorkflowCheckpointFailure{
                statement.text(4),
                statement.is_null(5)
                    ? std::nullopt
                    : std::optional<std::int64_t>{statement.integer(5)},
            };
        }

        nodes.push_back(domain::WorkflowNodeCheckpoint{
            .node_id = domain::WorkflowNodeId{node_id},
            .state = *state,
            .attempt_number = static_cast<std::uint32_t>(attempt),
            .max_attempts = static_cast<std::uint32_t>(maximum),
            .outputs = load_artifacts(database, workflow_id, node_id),
            .failure = std::move(failure),
        });
    }

    return domain::WorkflowCheckpointManifest{
        schema_version,
        domain::WorkflowId{std::string{workflow_id}},
        std::move(nodes),
    };
}

[[nodiscard]] std::optional<domain::WorkflowBranchDecisionSnapshot>
load_branch_decisions(
    sqlite3* database,
    const std::string_view workflow_id,
    const std::optional<std::uint32_t> schema_version
) {
    if (!schema_version.has_value()) return std::nullopt;

    constexpr const char* sql = R"sql(
        SELECT node_id, state, reason, condition_result
        FROM workflow_branch_decisions
        WHERE workflow_id = ?
        ORDER BY ordinal;
    )sql";
    Statement statement{
        database, sql, "Unable to load workflow branch decisions"
    };
    statement.bind_text(1, workflow_id);

    std::vector<domain::WorkflowBranchDecision> decisions;
    for (;;) {
        const int result = statement.step();
        if (result == SQLITE_DONE) break;
        if (result != SQLITE_ROW) {
            throw SqliteError{
                result,
                std::string{"Unable to load workflow branch decisions: "} +
                    sqlite3_errmsg(database)
            };
        }

        const auto state =
            domain::workflow_branch_decision_state_from_string(statement.text(1));
        const auto reason =
            domain::workflow_branch_decision_reason_from_string(statement.text(2));
        if (!state.has_value() || !reason.has_value()) {
            throw SqliteError{
                SQLITE_CORRUPT,
                "Workflow branch decision contains an unknown state or reason"
            };
        }
        std::optional<bool> condition_result;
        if (!statement.is_null(3)) {
            const std::int64_t value = statement.integer(3);
            if (value != 0 && value != 1) {
                throw SqliteError{
                    SQLITE_CORRUPT,
                    "Workflow branch decision condition result is invalid"
                };
            }
            condition_result = value == 1;
        }
        decisions.push_back(domain::WorkflowBranchDecision{
            domain::WorkflowNodeId{statement.text(0)},
            *state,
            *reason,
            condition_result,
        });
    }

    return domain::WorkflowBranchDecisionSnapshot{
        *schema_version,
        domain::WorkflowId{std::string{workflow_id}},
        std::move(decisions),
    };
}

void delete_child_rows(
    SqliteConnection& connection,
    const std::string_view workflow_id
) {
    sqlite3* database = connection.native_handle();
    for (const char* sql : {
             "DELETE FROM workflow_checkpoint_artifacts WHERE workflow_id = ?;",
             "DELETE FROM workflow_node_checkpoints WHERE workflow_id = ?;",
             "DELETE FROM workflow_branch_decisions WHERE workflow_id = ?;"
         }) {
        Statement statement{
            database, sql, "Unable to replace workflow state child rows"
        };
        statement.bind_text(1, workflow_id);
        require_done(
            database, statement, "Unable to replace workflow state child rows"
        );
    }
}

}  // namespace

SqliteWorkflowStateStore::SqliteWorkflowStateStore(
    SqliteConnection& connection
) noexcept
    : connection_{connection} {}

bool SqliteWorkflowStateStore::create(
    const application::PersistedWorkflowState& state
) {
    validate_state(state);
    if (state.revision != 0) {
        throw std::invalid_argument(
            "New persisted workflow state must begin at revision zero"
        );
    }

    sqlite3* const database = connection_.native_handle();
    Transaction transaction{connection_};

    constexpr const char* sql = R"sql(
        INSERT INTO workflow_states(
            workflow_id, workflow_document_json, checkpoint_schema_version,
            branch_decision_schema_version, revision, updated_at_utc
        ) VALUES (?, ?, ?, ?, ?, ?)
        ON CONFLICT(workflow_id) DO NOTHING;
    )sql";
    Statement statement{
        database, sql, "Unable to create workflow state"
    };
    statement.bind_text(1, state.workflow.id().value());
    const std::string document =
        pipeline_protocol::serialize_workflow_document(state.workflow);
    statement.bind_text(2, document);
    statement.bind_integer(
        3, static_cast<std::int64_t>(state.checkpoint.schema_version())
    );
    if (state.branch_decisions.has_value()) {
        statement.bind_integer(
            4,
            static_cast<std::int64_t>(
                state.branch_decisions->schema_version()
            )
        );
    } else {
        statement.bind_optional_integer(4, std::nullopt);
    }
    statement.bind_integer(5, state.revision);
    statement.bind_text(6, state.updated_at_utc);
    require_done(database, statement, "Unable to create workflow state");
    if (sqlite3_changes(database) != 1) {
        return false;
    }

    insert_checkpoint_rows(database, state);
    insert_branch_rows(database, state);
    transaction.commit();
    return true;
}

bool SqliteWorkflowStateStore::update(
    const application::PersistedWorkflowState& state,
    const std::int64_t expected_revision
) {
    validate_state(state);
    if (expected_revision < 0 ||
        expected_revision == std::numeric_limits<std::int64_t>::max() ||
        state.revision != expected_revision + 1) {
        throw std::invalid_argument(
            "Workflow state update revision is invalid"
        );
    }

    sqlite3* const database = connection_.native_handle();
    Transaction transaction{connection_};

    // Remove children first so branch schema metadata can be cleared. A failed
    // optimistic parent update rolls the whole transaction back.
    delete_child_rows(connection_, state.workflow.id().value());

    constexpr const char* sql = R"sql(
        UPDATE workflow_states
        SET checkpoint_schema_version = ?,
            branch_decision_schema_version = ?,
            revision = ?,
            updated_at_utc = ?
        WHERE workflow_id = ?
          AND revision = ?
          AND workflow_document_json = ?;
    )sql";
    Statement statement{
        database, sql, "Unable to update workflow state"
    };
    statement.bind_integer(
        1, static_cast<std::int64_t>(state.checkpoint.schema_version())
    );
    if (state.branch_decisions.has_value()) {
        statement.bind_integer(
            2,
            static_cast<std::int64_t>(
                state.branch_decisions->schema_version()
            )
        );
    } else {
        statement.bind_optional_integer(2, std::nullopt);
    }
    statement.bind_integer(3, state.revision);
    statement.bind_text(4, state.updated_at_utc);
    statement.bind_text(5, state.workflow.id().value());
    statement.bind_integer(6, expected_revision);
    const std::string document =
        pipeline_protocol::serialize_workflow_document(state.workflow);
    statement.bind_text(7, document);
    require_done(database, statement, "Unable to update workflow state");
    if (sqlite3_changes(database) != 1) {
        return false;
    }

    insert_checkpoint_rows(database, state);
    insert_branch_rows(database, state);
    transaction.commit();
    return true;
}

std::optional<application::PersistedWorkflowState>
SqliteWorkflowStateStore::find_by_workflow_id(
    const std::string_view workflow_id
) {
    constexpr const char* sql = R"sql(
        SELECT workflow_document_json, checkpoint_schema_version,
               branch_decision_schema_version, revision, updated_at_utc
        FROM workflow_states
        WHERE workflow_id = ?;
    )sql";
    sqlite3* const database = connection_.native_handle();
    Statement statement{
        database, sql, "Unable to load workflow state"
    };
    statement.bind_text(1, workflow_id);
    const int result = statement.step();
    if (result == SQLITE_DONE) return std::nullopt;
    if (result != SQLITE_ROW) {
        throw SqliteError{
            result,
            std::string{"Unable to load workflow state: "} +
                sqlite3_errmsg(database)
        };
    }

    const std::string document = statement.text(0);
    const std::int64_t checkpoint_schema = statement.integer(1);
    if (checkpoint_schema <= 0 ||
        checkpoint_schema > std::numeric_limits<std::uint32_t>::max()) {
        throw SqliteError{
            SQLITE_CORRUPT,
            "Workflow checkpoint schema version is invalid"
        };
    }

    std::optional<std::uint32_t> branch_schema;
    if (!statement.is_null(2)) {
        const std::int64_t value = statement.integer(2);
        if (value <= 0 ||
            value > std::numeric_limits<std::uint32_t>::max()) {
            throw SqliteError{
                SQLITE_CORRUPT,
                "Workflow branch decision schema version is invalid"
            };
        }
        branch_schema = static_cast<std::uint32_t>(value);
    }

    application::PersistedWorkflowState state{
        .workflow =
            pipeline_protocol::parse_workflow_document(document),
        .checkpoint = load_checkpoint(
            database,
            workflow_id,
            static_cast<std::uint32_t>(checkpoint_schema)
        ),
        .branch_decisions = load_branch_decisions(
            database, workflow_id, branch_schema
        ),
        .revision = statement.integer(3),
        .updated_at_utc = statement.text(4),
    };
    validate_state(state);
    return state;
}

std::vector<application::PersistedWorkflowState>
SqliteWorkflowStateStore::list() {
    constexpr const char* sql =
        "SELECT workflow_id FROM workflow_states ORDER BY workflow_id;";
    sqlite3* const database = connection_.native_handle();
    Statement statement{
        database, sql, "Unable to list workflow states"
    };

    std::vector<std::string> ids;
    for (;;) {
        const int result = statement.step();
        if (result == SQLITE_DONE) break;
        if (result != SQLITE_ROW) {
            throw SqliteError{
                result,
                std::string{"Unable to list workflow states: "} +
                    sqlite3_errmsg(database)
            };
        }
        ids.push_back(statement.text(0));
    }

    std::vector<application::PersistedWorkflowState> states;
    states.reserve(ids.size());
    for (const std::string& id : ids) {
        auto state = find_by_workflow_id(id);
        if (!state.has_value()) {
            throw SqliteError{
                SQLITE_CORRUPT,
                "Workflow state disappeared while listing persisted states"
            };
        }
        states.push_back(std::move(*state));
    }
    return states;
}

}  // namespace biocore::infrastructure::sqlite
