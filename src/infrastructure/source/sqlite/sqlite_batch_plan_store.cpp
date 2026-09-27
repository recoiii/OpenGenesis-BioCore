#include "biocore/infrastructure/sqlite/sqlite_batch_plan_store.hpp"

#include <sqlite3.h>

#include <cstdint>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/batch_plan.hpp"
#include "biocore/domain/plugin_io_contract.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"

namespace biocore::infrastructure::sqlite {
namespace {

class Statement final {
public:
    Statement(sqlite3* database, const char* sql, std::string description)
        : database_{database}, description_{std::move(description)} {
        require(sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr));
    }

    ~Statement() {
        if (statement_ != nullptr) sqlite3_finalize(statement_);
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind_text(const int index, const std::string_view value) {
        require(sqlite3_bind_text64(
            statement_, index, value.data(),
            static_cast<sqlite3_uint64>(value.size()),
            SQLITE_TRANSIENT, SQLITE_UTF8
        ));
    }

    void bind_optional_text(
        const int index,
        const std::optional<std::string>& value
    ) {
        if (value.has_value()) bind_text(index, *value);
        else require(sqlite3_bind_null(statement_, index));
    }

    void bind_integer(const int index, const std::int64_t value) {
        require(sqlite3_bind_int64(statement_, index, value));
    }

    void bind_null(const int index) {
        require(sqlite3_bind_null(statement_, index));
    }

    [[nodiscard]] int step() { return sqlite3_step(statement_); }

    [[nodiscard]] bool is_null(const int column) const noexcept {
        return sqlite3_column_type(statement_, column) == SQLITE_NULL;
    }

    [[nodiscard]] std::string text(const int column) const {
        const auto* raw = sqlite3_column_text(statement_, column);
        if (raw == nullptr) {
            throw SqliteError{
                SQLITE_MISMATCH,
                description_ + ": unexpected NULL text"
            };
        }
        return {
            reinterpret_cast<const char*>(raw),
            static_cast<std::size_t>(sqlite3_column_bytes(statement_, column))
        };
    }

    [[nodiscard]] std::optional<std::string> optional_text(
        const int column
    ) const {
        if (is_null(column)) return std::nullopt;
        return text(column);
    }

    [[nodiscard]] std::int64_t integer(const int column) const noexcept {
        return sqlite3_column_int64(statement_, column);
    }

private:
    void require(const int result) const {
        if (result != SQLITE_OK) {
            throw SqliteError{
                result,
                description_ + ": " + sqlite3_errmsg(database_)
            };
        }
    }

    sqlite3* database_;
    std::string description_;
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

void require_done(
    sqlite3* database,
    const int result,
    const std::string_view message
) {
    if (result != SQLITE_DONE) {
        throw SqliteError{
            result,
            std::string{message} + ": " + sqlite3_errmsg(database)
        };
    }
}

void validate_plan_shape(const application::ApprovedBatchPlan& plan) {
    if (plan.plan_id.empty() || plan.project_id.empty() ||
        plan.template_id.empty() || plan.template_version.empty() ||
        plan.approved_at_utc.empty() || plan.samples.empty()) {
        throw std::invalid_argument{"Approved batch plan identity is incomplete"};
    }

    std::set<std::string, std::less<>> sample_ids;
    std::set<std::string, std::less<>> workflow_ids;
    std::size_t included = 0U;

    for (const auto& sample : plan.samples) {
        if (sample.sample_id.empty() ||
            !sample_ids.emplace(sample.sample_id).second) {
            throw std::invalid_argument{
                "Approved batch plan contains an invalid or duplicate sample"
            };
        }

        if (sample.disposition ==
            application::BatchPlanSampleDisposition::excluded) {
            if (sample.workflow_id.has_value() || !sample.nodes.empty()) {
                throw std::invalid_argument{
                    "Excluded batch sample must not contain a run plan"
                };
            }
            continue;
        }

        ++included;
        if (!sample.workflow_id.has_value() || sample.workflow_id->empty() ||
            sample.nodes.empty() ||
            !workflow_ids.emplace(*sample.workflow_id).second) {
            throw std::invalid_argument{
                "Included batch sample run plan is invalid or duplicated"
            };
        }

        std::set<std::string, std::less<>> node_ids;
        for (const auto& node : sample.nodes) {
            if (node.node_id.empty() || node.module_id.empty() ||
                node.plugin_version.empty() ||
                !node_ids.emplace(node.node_id).second) {
                throw std::invalid_argument{
                    "Approved batch sample contains an invalid or duplicate node"
                };
            }
        }
    }

    if (included == 0U) {
        throw std::invalid_argument{
            "Approved batch plan must contain an included sample"
        };
    }
}

void insert_plan_header(
    SqliteConnection& connection,
    const application::ApprovedBatchPlan& plan
) {
    Statement statement{
        connection.native_handle(),
        "INSERT INTO batch_plans("
        "plan_id,project_id,template_id,template_version,approved_at_utc,sealed"
        ") VALUES(?,?,?,?,?,0);",
        "Unable to insert batch plan"
    };
    statement.bind_text(1, plan.plan_id);
    statement.bind_text(2, plan.project_id);
    statement.bind_text(3, plan.template_id);
    statement.bind_text(4, plan.template_version);
    statement.bind_text(5, plan.approved_at_utc);
    require_done(
        connection.native_handle(), statement.step(), "Unable to insert batch plan"
    );
}

void insert_sample(
    SqliteConnection& connection,
    const application::ApprovedBatchPlan& plan,
    const application::ApprovedBatchSamplePlan& sample,
    const std::size_t ordinal
) {
    Statement statement{
        connection.native_handle(),
        "INSERT INTO batch_plan_samples("
        "plan_id,sample_id,ordinal,disposition,workflow_id"
        ") VALUES(?,?,?,?,?);",
        "Unable to insert batch plan sample"
    };
    statement.bind_text(1, plan.plan_id);
    statement.bind_text(2, sample.sample_id);
    statement.bind_integer(3, static_cast<std::int64_t>(ordinal));
    statement.bind_text(4, application::to_string(sample.disposition));
    statement.bind_optional_text(5, sample.workflow_id);
    require_done(
        connection.native_handle(),
        statement.step(),
        "Unable to insert batch plan sample"
    );
}

void insert_node(
    SqliteConnection& connection,
    const std::string_view plan_id,
    const std::string_view sample_id,
    const application::BatchPlanNodeSnapshot& node,
    const std::size_t ordinal
) {
    Statement statement{
        connection.native_handle(),
        "INSERT INTO batch_plan_nodes("
        "plan_id,sample_id,ordinal,node_id,module_id,plugin_version"
        ") VALUES(?,?,?,?,?,?);",
        "Unable to insert batch plan node"
    };
    statement.bind_text(1, plan_id);
    statement.bind_text(2, sample_id);
    statement.bind_integer(3, static_cast<std::int64_t>(ordinal));
    statement.bind_text(4, node.node_id);
    statement.bind_text(5, node.module_id);
    statement.bind_text(6, node.plugin_version);
    require_done(
        connection.native_handle(),
        statement.step(),
        "Unable to insert batch plan node"
    );
}

void insert_parameter(
    SqliteConnection& connection,
    const std::string_view plan_id,
    const std::string_view sample_id,
    const std::string_view node_id,
    const application::BatchPlanParameterSnapshot& parameter,
    const std::size_t ordinal
) {
    Statement statement{
        connection.native_handle(),
        "INSERT INTO batch_plan_parameters("
        "plan_id,sample_id,node_id,ordinal,name,parameter_type,value,source"
        ") VALUES(?,?,?,?,?,?,?,?);",
        "Unable to insert batch plan parameter"
    };
    statement.bind_text(1, plan_id);
    statement.bind_text(2, sample_id);
    statement.bind_text(3, node_id);
    statement.bind_integer(4, static_cast<std::int64_t>(ordinal));
    statement.bind_text(5, parameter.name);
    statement.bind_text(6, domain::to_string(parameter.type));
    statement.bind_text(7, parameter.value);
    statement.bind_text(8, application::to_string(parameter.source));
    require_done(
        connection.native_handle(),
        statement.step(),
        "Unable to insert batch plan parameter"
    );
}

void insert_input(
    SqliteConnection& connection,
    const std::string_view plan_id,
    const std::string_view sample_id,
    const std::string_view node_id,
    const application::BatchPlanInputSnapshot& input,
    const std::size_t ordinal
) {
    Statement statement{
        connection.native_handle(),
        "INSERT INTO batch_plan_inputs("
        "plan_id,sample_id,node_id,ordinal,port_name,artifact_type,"
        "source_kind,source_id,source_port,file_role,file_id,file_type,size_bytes,sha256"
        ") VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?);",
        "Unable to insert batch plan input"
    };
    statement.bind_text(1, plan_id);
    statement.bind_text(2, sample_id);
    statement.bind_text(3, node_id);
    statement.bind_integer(4, static_cast<std::int64_t>(ordinal));
    statement.bind_text(5, input.port_name);
    statement.bind_text(6, input.artifact_type);
    statement.bind_text(7, application::to_string(input.source_kind));
    statement.bind_text(8, input.source_id);
    statement.bind_text(9, input.source_port);

    if (input.managed_file.has_value()) {
        statement.bind_text(10, application::to_string(input.managed_file->role));
        statement.bind_text(11, input.managed_file->file_id);
        statement.bind_text(12, input.managed_file->file_type);
        statement.bind_integer(13, input.managed_file->size_bytes);
        statement.bind_text(14, input.managed_file->sha256);
    } else {
        statement.bind_null(10);
        statement.bind_null(11);
        statement.bind_null(12);
        statement.bind_null(13);
        statement.bind_null(14);
    }

    require_done(
        connection.native_handle(),
        statement.step(),
        "Unable to insert batch plan input"
    );
}

void insert_output(
    SqliteConnection& connection,
    const std::string_view plan_id,
    const std::string_view sample_id,
    const std::string_view node_id,
    const application::BatchPlanOutputSnapshot& output,
    const std::size_t ordinal
) {
    Statement statement{
        connection.native_handle(),
        "INSERT INTO batch_plan_outputs("
        "plan_id,sample_id,node_id,ordinal,port_name,artifact_type"
        ") VALUES(?,?,?,?,?,?);",
        "Unable to insert batch plan output"
    };
    statement.bind_text(1, plan_id);
    statement.bind_text(2, sample_id);
    statement.bind_text(3, node_id);
    statement.bind_integer(4, static_cast<std::int64_t>(ordinal));
    statement.bind_text(5, output.port_name);
    statement.bind_text(6, output.artifact_type);
    require_done(
        connection.native_handle(),
        statement.step(),
        "Unable to insert batch plan output"
    );
}

[[nodiscard]] std::vector<application::BatchPlanParameterSnapshot>
read_parameters(
    SqliteConnection& connection,
    const std::string_view plan_id,
    const std::string_view sample_id,
    const std::string_view node_id
) {
    Statement statement{
        connection.native_handle(),
        "SELECT name,parameter_type,value,source "
        "FROM batch_plan_parameters "
        "WHERE plan_id=? AND sample_id=? AND node_id=? "
        "ORDER BY ordinal;",
        "Unable to read batch plan parameters"
    };
    statement.bind_text(1, plan_id);
    statement.bind_text(2, sample_id);
    statement.bind_text(3, node_id);

    std::vector<application::BatchPlanParameterSnapshot> values;
    for (;;) {
        const int result = statement.step();
        if (result == SQLITE_DONE) return values;
        if (result != SQLITE_ROW) {
            throw SqliteError{
                result,
                std::string{"Unable to read batch plan parameters: "} +
                    sqlite3_errmsg(connection.native_handle())
            };
        }

        const auto type = domain::plugin_parameter_type_from_string(
            statement.text(1)
        );
        const auto source =
            application::batch_plan_parameter_source_from_string(
                statement.text(3)
            );
        if (!type.has_value() || !source.has_value()) {
            throw SqliteError{
                SQLITE_CORRUPT,
                "Batch plan parameter contains an unsupported enum value"
            };
        }

        values.push_back(application::BatchPlanParameterSnapshot{
            .name = statement.text(0),
            .type = *type,
            .value = statement.text(2),
            .source = *source,
        });
    }
}

[[nodiscard]] std::vector<application::BatchPlanInputSnapshot>
read_inputs(
    SqliteConnection& connection,
    const std::string_view plan_id,
    const std::string_view sample_id,
    const std::string_view node_id
) {
    Statement statement{
        connection.native_handle(),
        "SELECT port_name,artifact_type,source_kind,source_id,source_port,"
        "file_role,file_id,file_type,size_bytes,sha256 "
        "FROM batch_plan_inputs "
        "WHERE plan_id=? AND sample_id=? AND node_id=? "
        "ORDER BY ordinal;",
        "Unable to read batch plan inputs"
    };
    statement.bind_text(1, plan_id);
    statement.bind_text(2, sample_id);
    statement.bind_text(3, node_id);

    std::vector<application::BatchPlanInputSnapshot> values;
    for (;;) {
        const int result = statement.step();
        if (result == SQLITE_DONE) return values;
        if (result != SQLITE_ROW) {
            throw SqliteError{
                result,
                std::string{"Unable to read batch plan inputs: "} +
                    sqlite3_errmsg(connection.native_handle())
            };
        }

        const auto kind =
            application::batch_plan_input_source_kind_from_string(
                statement.text(2)
            );
        if (!kind.has_value()) {
            throw SqliteError{
                SQLITE_CORRUPT,
                "Batch plan input contains an unsupported source kind"
            };
        }

        std::optional<application::BatchPlanManagedFileSnapshot> file;
        if (*kind == application::BatchPlanInputSourceKind::managed_file) {
            if (statement.is_null(5) || statement.is_null(6) ||
                statement.is_null(7) || statement.is_null(8) ||
                statement.is_null(9)) {
                throw SqliteError{
                    SQLITE_CORRUPT,
                    "Managed batch plan input is missing file identity"
                };
            }
            const auto role =
                application::batch_input_file_role_from_string(
                    statement.text(5)
                );
            if (!role.has_value()) {
                throw SqliteError{
                    SQLITE_CORRUPT,
                    "Batch plan input contains an unsupported file role"
                };
            }
            file = application::BatchPlanManagedFileSnapshot{
                .role = *role,
                .file_id = statement.text(6),
                .file_type = statement.text(7),
                .size_bytes = statement.integer(8),
                .sha256 = statement.text(9),
            };
        }

        values.push_back(application::BatchPlanInputSnapshot{
            .port_name = statement.text(0),
            .artifact_type = statement.text(1),
            .source_kind = *kind,
            .source_id = statement.text(3),
            .source_port = statement.text(4),
            .managed_file = std::move(file),
        });
    }
}

[[nodiscard]] std::vector<application::BatchPlanOutputSnapshot>
read_outputs(
    SqliteConnection& connection,
    const std::string_view plan_id,
    const std::string_view sample_id,
    const std::string_view node_id
) {
    Statement statement{
        connection.native_handle(),
        "SELECT port_name,artifact_type "
        "FROM batch_plan_outputs "
        "WHERE plan_id=? AND sample_id=? AND node_id=? "
        "ORDER BY ordinal;",
        "Unable to read batch plan outputs"
    };
    statement.bind_text(1, plan_id);
    statement.bind_text(2, sample_id);
    statement.bind_text(3, node_id);

    std::vector<application::BatchPlanOutputSnapshot> values;
    for (;;) {
        const int result = statement.step();
        if (result == SQLITE_DONE) return values;
        if (result != SQLITE_ROW) {
            throw SqliteError{
                result,
                std::string{"Unable to read batch plan outputs: "} +
                    sqlite3_errmsg(connection.native_handle())
            };
        }
        values.push_back(application::BatchPlanOutputSnapshot{
            .port_name = statement.text(0),
            .artifact_type = statement.text(1),
        });
    }
}

[[nodiscard]] std::vector<application::BatchPlanNodeSnapshot> read_nodes(
    SqliteConnection& connection,
    const std::string_view plan_id,
    const std::string_view sample_id
) {
    Statement statement{
        connection.native_handle(),
        "SELECT node_id,module_id,plugin_version "
        "FROM batch_plan_nodes "
        "WHERE plan_id=? AND sample_id=? ORDER BY ordinal;",
        "Unable to read batch plan nodes"
    };
    statement.bind_text(1, plan_id);
    statement.bind_text(2, sample_id);

    std::vector<application::BatchPlanNodeSnapshot> values;
    for (;;) {
        const int result = statement.step();
        if (result == SQLITE_DONE) return values;
        if (result != SQLITE_ROW) {
            throw SqliteError{
                result,
                std::string{"Unable to read batch plan nodes: "} +
                    sqlite3_errmsg(connection.native_handle())
            };
        }

        const std::string node_id = statement.text(0);
        values.push_back(application::BatchPlanNodeSnapshot{
            .node_id = node_id,
            .module_id = statement.text(1),
            .plugin_version = statement.text(2),
            .parameters = read_parameters(
                connection, plan_id, sample_id, node_id
            ),
            .inputs = read_inputs(
                connection, plan_id, sample_id, node_id
            ),
            .outputs = read_outputs(
                connection, plan_id, sample_id, node_id
            ),
        });
    }
}

}  // namespace

SqliteBatchPlanStore::SqliteBatchPlanStore(SqliteConnection& connection) noexcept
    : connection_{connection} {}

bool SqliteBatchPlanStore::add(
    const application::ApprovedBatchPlan& plan
) {
    validate_plan_shape(plan);
    sqlite3* database = connection_.native_handle();
    Transaction transaction{connection_};

    try {
        insert_plan_header(connection_, plan);
    } catch (const SqliteError& error) {
        if (error.is_constraint_violation()) {
            Statement existing{
                database,
                "SELECT 1 FROM batch_plans WHERE plan_id=? LIMIT 1;",
                "Unable to inspect batch plan conflict"
            };
            existing.bind_text(1, plan.plan_id);
            if (existing.step() == SQLITE_ROW) {
                return false;
            }
        }
        throw;
    }

    for (std::size_t sample_index = 0U;
         sample_index < plan.samples.size();
         ++sample_index) {
        const auto& sample = plan.samples[sample_index];
        insert_sample(connection_, plan, sample, sample_index);

        if (sample.disposition ==
            application::BatchPlanSampleDisposition::excluded) {
            continue;
        }

        for (std::size_t node_index = 0U;
             node_index < sample.nodes.size();
             ++node_index) {
            const auto& node = sample.nodes[node_index];
            insert_node(
                connection_, plan.plan_id, sample.sample_id,
                node, node_index
            );

            for (std::size_t index = 0U;
                 index < node.parameters.size();
                 ++index) {
                insert_parameter(
                    connection_, plan.plan_id, sample.sample_id,
                    node.node_id, node.parameters[index], index
                );
            }
            for (std::size_t index = 0U;
                 index < node.inputs.size();
                 ++index) {
                insert_input(
                    connection_, plan.plan_id, sample.sample_id,
                    node.node_id, node.inputs[index], index
                );
            }
            for (std::size_t index = 0U;
                 index < node.outputs.size();
                 ++index) {
                insert_output(
                    connection_, plan.plan_id, sample.sample_id,
                    node.node_id, node.outputs[index], index
                );
            }
        }
    }

    Statement seal{
        database,
        "UPDATE batch_plans SET sealed=1 "
        "WHERE plan_id=? AND sealed=0;",
        "Unable to seal batch plan"
    };
    seal.bind_text(1, plan.plan_id);
    require_done(database, seal.step(), "Unable to seal batch plan");
    if (sqlite3_changes(database) != 1) {
        throw SqliteError{
            SQLITE_CONSTRAINT,
            "Batch plan could not be sealed exactly once"
        };
    }

    transaction.commit();
    return true;
}

std::optional<application::ApprovedBatchPlan>
SqliteBatchPlanStore::find(const std::string_view plan_id) {
    Statement plan_statement{
        connection_.native_handle(),
        "SELECT project_id,template_id,template_version,approved_at_utc,sealed "
        "FROM batch_plans WHERE plan_id=?;",
        "Unable to find batch plan"
    };
    plan_statement.bind_text(1, plan_id);
    const int plan_result = plan_statement.step();
    if (plan_result == SQLITE_DONE) return std::nullopt;
    if (plan_result != SQLITE_ROW) {
        throw SqliteError{
            plan_result,
            std::string{"Unable to find batch plan: "} +
                sqlite3_errmsg(connection_.native_handle())
        };
    }
    if (plan_statement.integer(4) != 1) {
        throw SqliteError{
            SQLITE_CORRUPT,
            "Persisted batch plan is not sealed"
        };
    }

    application::ApprovedBatchPlan plan{
        .plan_id = std::string{plan_id},
        .project_id = plan_statement.text(0),
        .template_id = plan_statement.text(1),
        .template_version = plan_statement.text(2),
        .approved_at_utc = plan_statement.text(3),
    };

    Statement samples{
        connection_.native_handle(),
        "SELECT sample_id,disposition,workflow_id "
        "FROM batch_plan_samples WHERE plan_id=? ORDER BY ordinal;",
        "Unable to read batch plan samples"
    };
    samples.bind_text(1, plan_id);

    for (;;) {
        const int result = samples.step();
        if (result == SQLITE_DONE) break;
        if (result != SQLITE_ROW) {
            throw SqliteError{
                result,
                std::string{"Unable to read batch plan samples: "} +
                    sqlite3_errmsg(connection_.native_handle())
            };
        }

        const auto disposition =
            application::batch_plan_sample_disposition_from_string(
                samples.text(1)
            );
        if (!disposition.has_value()) {
            throw SqliteError{
                SQLITE_CORRUPT,
                "Batch plan sample contains an unsupported disposition"
            };
        }

        const std::string sample_id = samples.text(0);
        const auto workflow_id = samples.optional_text(2);
        std::vector<application::BatchPlanNodeSnapshot> nodes;
        if (*disposition ==
            application::BatchPlanSampleDisposition::included) {
            if (!workflow_id.has_value()) {
                throw SqliteError{
                    SQLITE_CORRUPT,
                    "Included batch sample is missing workflow identity"
                };
            }
            nodes = read_nodes(connection_, plan_id, sample_id);
            if (nodes.empty()) {
                throw SqliteError{
                    SQLITE_CORRUPT,
                    "Included batch sample is missing run-plan nodes"
                };
            }
        } else if (workflow_id.has_value()) {
            throw SqliteError{
                SQLITE_CORRUPT,
                "Excluded batch sample unexpectedly has a workflow identity"
            };
        }

        plan.samples.push_back(application::ApprovedBatchSamplePlan{
            .sample_id = sample_id,
            .disposition = *disposition,
            .workflow_id = workflow_id,
            .nodes = std::move(nodes),
        });
    }

    if (plan.samples.empty()) {
        throw SqliteError{
            SQLITE_CORRUPT,
            "Persisted batch plan contains no sample snapshots"
        };
    }
    return plan;
}

}  // namespace biocore::infrastructure::sqlite
