#include "biocore/infrastructure/sqlite/sqlite_cohort_analysis_snapshot_store.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "biocore/application/cohort_analysis_snapshot.hpp"
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
        require(sqlite3_bind_text64(
            statement_, index, value.data(),
            static_cast<sqlite3_uint64>(value.size()), SQLITE_TRANSIENT, SQLITE_UTF8
        ));
    }
    void bind_integer(const int index, const std::int64_t value) {
        require(sqlite3_bind_int64(statement_, index, value));
    }
    void bind_null(const int index) { require(sqlite3_bind_null(statement_, index)); }
    void bind_optional_text(const int index, const std::optional<std::string>& value) {
        if (value.has_value()) bind_text(index, *value);
        else bind_null(index);
    }
    void bind_optional_integer(const int index, const std::optional<std::int64_t>& value) {
        if (value.has_value()) bind_integer(index, *value);
        else bind_null(index);
    }

    [[nodiscard]] int step() { return sqlite3_step(statement_); }
    [[nodiscard]] std::string text(const int column) const {
        const auto* raw = sqlite3_column_text(statement_, column);
        if (raw == nullptr) {
            throw SqliteError{SQLITE_MISMATCH, description_ + ": unexpected NULL text"};
        }
        return {
            reinterpret_cast<const char*>(raw),
            static_cast<std::size_t>(sqlite3_column_bytes(statement_, column))
        };
    }
    [[nodiscard]] std::optional<std::string> optional_text(const int column) const {
        if (sqlite3_column_type(statement_, column) == SQLITE_NULL) return std::nullopt;
        return text(column);
    }
    [[nodiscard]] std::int64_t integer(const int column) const noexcept {
        return sqlite3_column_int64(statement_, column);
    }
    [[nodiscard]] std::optional<std::int64_t> optional_integer(const int column) const noexcept {
        if (sqlite3_column_type(statement_, column) == SQLITE_NULL) return std::nullopt;
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

[[nodiscard]] bool row_exists(
    sqlite3* database,
    const char* sql,
    const std::vector<std::string_view>& values,
    const std::string& description
) {
    Statement statement{database, sql, description};
    for (std::size_t index = 0U; index < values.size(); ++index) {
        statement.bind_text(static_cast<int>(index + 1U), values[index]);
    }
    const int result = statement.step();
    if (result == SQLITE_ROW) return true;
    if (result == SQLITE_DONE) return false;
    throw SqliteError{result, description + ": " + sqlite3_errmsg(database)};
}

[[nodiscard]] std::string_view assembly_token(
    const domain::ReferenceAssembly assembly
) noexcept {
    switch (assembly) {
        case domain::ReferenceAssembly::unspecified: return "unspecified";
        case domain::ReferenceAssembly::grch37: return "grch37";
        case domain::ReferenceAssembly::grch38: return "grch38";
        case domain::ReferenceAssembly::custom: return "custom";
    }
    return "unspecified";
}

[[nodiscard]] std::optional<domain::ReferenceAssembly> assembly_from_token(
    const std::string_view value
) noexcept {
    if (value == "grch37") return domain::ReferenceAssembly::grch37;
    if (value == "grch38") return domain::ReferenceAssembly::grch38;
    if (value == "custom") return domain::ReferenceAssembly::custom;
    return std::nullopt;
}

[[nodiscard]] bool cohort_revision_exists(
    sqlite3* database,
    const std::string_view project_id,
    const std::string_view cohort_id,
    const std::uint32_t revision
) {
    Statement statement{
        database,
        "SELECT 1 FROM cohort_revisions "
        "WHERE project_id=? AND cohort_id=? AND revision=? AND sealed=1;",
        "Unable to inspect cohort analysis revision"
    };
    statement.bind_text(1, project_id);
    statement.bind_text(2, cohort_id);
    statement.bind_integer(3, revision);
    const int result = statement.step();
    if (result == SQLITE_ROW) return true;
    if (result == SQLITE_DONE) return false;
    throw SqliteError{
        result,
        std::string{"Unable to inspect cohort analysis revision: "} + sqlite3_errmsg(database)
    };
}

[[nodiscard]] bool managed_file_exists(
    sqlite3* database,
    const std::string_view file_id
) {
    return row_exists(
        database,
        "SELECT 1 FROM managed_files WHERE id=?;",
        {file_id},
        "Unable to inspect cohort analysis managed file"
    );
}

void insert_samples(
    sqlite3* database,
    const application::CohortAnalysisSnapshot& snapshot
) {
    Statement statement{
        database,
        "INSERT INTO cohort_analysis_samples("
        "project_id,analysis_id,ordinal,sample_id,sample_display_name,"
        "sample_group_metadata,biological_unit_id,group_token,cohort_disposition,"
        "cohort_exclusion_reason,analysis_disposition,analysis_reason,qc_state,qc_reason,"
        "qc_file_id,qc_job_id,qc_step_id,qc_output_port,qc_module_id,qc_plugin_version,"
        "qc_size_bytes,qc_sha256"
        ") VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);",
        "Unable to insert cohort analysis sample"
    };

    for (const auto& sample : snapshot.samples) {
        statement.bind_text(1, snapshot.project_id);
        statement.bind_text(2, snapshot.analysis_id);
        statement.bind_integer(3, static_cast<std::int64_t>(sample.ordinal));
        statement.bind_text(4, sample.sample_id);
        statement.bind_text(5, sample.sample_display_name);
        statement.bind_text(6, sample.sample_group_metadata);
        statement.bind_text(7, sample.biological_unit_id);
        statement.bind_text(8, application::to_string(sample.group));
        statement.bind_text(9, application::to_string(sample.cohort_disposition));
        statement.bind_optional_text(10, sample.cohort_exclusion_reason);
        statement.bind_text(11, application::to_string(sample.analysis_disposition));
        statement.bind_optional_text(12, sample.analysis_reason);
        statement.bind_text(13, application::to_string(sample.qc.state));
        statement.bind_text(14, sample.qc.reason);
        statement.bind_optional_text(15, sample.qc.managed_file_id);
        statement.bind_optional_text(16, sample.qc.job_id);
        statement.bind_optional_text(17, sample.qc.step_id);
        statement.bind_optional_text(18, sample.qc.output_port);
        statement.bind_optional_text(19, sample.qc.module_id);
        statement.bind_optional_text(20, sample.qc.plugin_version);
        statement.bind_optional_integer(21, sample.qc.size_bytes);
        statement.bind_optional_text(22, sample.qc.sha256);
        require_done(database, statement.step(), "Unable to insert cohort analysis sample");
        statement.reset();
    }
}

void insert_sources(
    sqlite3* database,
    const application::CohortAnalysisSnapshot& snapshot
) {
    Statement statement{
        database,
        "INSERT INTO cohort_analysis_sources("
        "project_id,analysis_id,project_sample_id,biological_unit_id,plan_id,workflow_id,"
        "producer_sample_id,attempt_number,job_id,step_id,output_port,module_id,plugin_version,"
        "managed_file_id,size_bytes,sha256,vcf_sample_name"
        ") VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);",
        "Unable to insert cohort analysis source"
    };
    for (const auto& source : snapshot.sources) {
        statement.bind_text(1, snapshot.project_id);
        statement.bind_text(2, snapshot.analysis_id);
        statement.bind_text(3, source.project_sample_id);
        statement.bind_text(4, source.biological_unit_id);
        statement.bind_text(5, source.plan_id);
        statement.bind_text(6, source.workflow_id);
        statement.bind_text(7, source.producer_sample_id);
        statement.bind_integer(8, source.attempt_number);
        statement.bind_text(9, source.job_id);
        statement.bind_text(10, source.step_id);
        statement.bind_text(11, source.output_port);
        statement.bind_text(12, source.module_id);
        statement.bind_text(13, source.plugin_version);
        statement.bind_text(14, source.managed_file_id);
        statement.bind_integer(15, source.size_bytes);
        statement.bind_text(16, source.sha256);
        statement.bind_text(17, source.vcf_sample_name);
        require_done(database, statement.step(), "Unable to insert cohort analysis source");
        statement.reset();
    }
}

void insert_reference(
    sqlite3* database,
    const application::CohortAnalysisSnapshot& snapshot
) {
    Statement contig{
        database,
        "INSERT INTO cohort_analysis_reference_contigs("
        "project_id,analysis_id,ordinal,canonical_name,length_bases"
        ") VALUES(?,?,?,?,?);",
        "Unable to insert cohort analysis reference contig"
    };
    for (std::size_t index = 0U; index < snapshot.reference.contigs.size(); ++index) {
        const auto& value = snapshot.reference.contigs[index];
        contig.bind_text(1, snapshot.project_id);
        contig.bind_text(2, snapshot.analysis_id);
        contig.bind_integer(3, static_cast<std::int64_t>(index));
        contig.bind_text(4, value.canonical_name);
        contig.bind_integer(5, static_cast<std::int64_t>(value.length));
        require_done(database, contig.step(), "Unable to insert cohort analysis reference contig");
        contig.reset();
    }

    auto aliases = snapshot.reference.aliases;
    std::ranges::sort(aliases, [](const auto& left, const auto& right) {
        return std::tie(left.alias, left.canonical_name) <
               std::tie(right.alias, right.canonical_name);
    });
    Statement alias{
        database,
        "INSERT INTO cohort_analysis_reference_aliases("
        "project_id,analysis_id,ordinal,alias,canonical_name"
        ") VALUES(?,?,?,?,?);",
        "Unable to insert cohort analysis reference alias"
    };
    for (std::size_t index = 0U; index < aliases.size(); ++index) {
        alias.bind_text(1, snapshot.project_id);
        alias.bind_text(2, snapshot.analysis_id);
        alias.bind_integer(3, static_cast<std::int64_t>(index));
        alias.bind_text(4, aliases[index].alias);
        alias.bind_text(5, aliases[index].canonical_name);
        require_done(database, alias.step(), "Unable to insert cohort analysis reference alias");
        alias.reset();
    }
}

void insert_test_universe(
    sqlite3* database,
    const application::CohortAnalysisSnapshot& snapshot
) {
    Statement statement{
        database,
        "INSERT INTO cohort_analysis_test_universe("
        "project_id,analysis_id,ordinal,contig,start_pos,end_pos,reference_allele,alternate_allele,"
        "case_unobserved,control_unobserved,case_no_calls,control_no_calls,"
        "case_partial_calls,control_partial_calls,case_complete_calls,control_complete_calls,"
        "allele_family_member,carrier_family_member"
        ") VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);",
        "Unable to insert cohort analysis test universe"
    };
    for (const auto& value : snapshot.test_universe) {
        statement.bind_text(1, snapshot.project_id);
        statement.bind_text(2, snapshot.analysis_id);
        statement.bind_integer(3, static_cast<std::int64_t>(value.ordinal));
        statement.bind_text(4, value.contig);
        statement.bind_integer(5, static_cast<std::int64_t>(value.start));
        statement.bind_integer(6, static_cast<std::int64_t>(value.end));
        statement.bind_text(7, value.reference);
        statement.bind_text(8, value.alternate);
        statement.bind_integer(9, static_cast<std::int64_t>(value.case_unobserved));
        statement.bind_integer(10, static_cast<std::int64_t>(value.control_unobserved));
        statement.bind_integer(11, static_cast<std::int64_t>(value.case_no_calls));
        statement.bind_integer(12, static_cast<std::int64_t>(value.control_no_calls));
        statement.bind_integer(13, static_cast<std::int64_t>(value.case_partial_calls));
        statement.bind_integer(14, static_cast<std::int64_t>(value.control_partial_calls));
        statement.bind_integer(15, static_cast<std::int64_t>(value.case_complete_calls));
        statement.bind_integer(16, static_cast<std::int64_t>(value.control_complete_calls));
        statement.bind_integer(17, value.allele_family_member ? 1 : 0);
        statement.bind_integer(18, value.carrier_family_member ? 1 : 0);
        require_done(database, statement.step(), "Unable to insert cohort analysis test universe");
        statement.reset();
    }
}

[[nodiscard]] std::vector<application::CohortReferenceContig> read_contigs(
    sqlite3* database,
    const std::string_view project_id,
    const std::string_view analysis_id
) {
    Statement statement{
        database,
        "SELECT canonical_name,length_bases FROM cohort_analysis_reference_contigs "
        "WHERE project_id=? AND analysis_id=? ORDER BY ordinal;",
        "Unable to read cohort analysis reference contigs"
    };
    statement.bind_text(1, project_id);
    statement.bind_text(2, analysis_id);
    std::vector<application::CohortReferenceContig> result;
    for (;;) {
        const int step = statement.step();
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW) {
            throw SqliteError{step, std::string{"Unable to read cohort analysis reference contigs: "} +
                                    sqlite3_errmsg(database)};
        }
        result.push_back({
            .canonical_name = statement.text(0),
            .length = static_cast<std::uint64_t>(statement.integer(1)),
        });
    }
    return result;
}

[[nodiscard]] std::vector<application::CohortReferenceAlias> read_aliases(
    sqlite3* database,
    const std::string_view project_id,
    const std::string_view analysis_id
) {
    Statement statement{
        database,
        "SELECT alias,canonical_name FROM cohort_analysis_reference_aliases "
        "WHERE project_id=? AND analysis_id=? ORDER BY ordinal;",
        "Unable to read cohort analysis reference aliases"
    };
    statement.bind_text(1, project_id);
    statement.bind_text(2, analysis_id);
    std::vector<application::CohortReferenceAlias> result;
    for (;;) {
        const int step = statement.step();
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW) {
            throw SqliteError{step, std::string{"Unable to read cohort analysis reference aliases: "} +
                                    sqlite3_errmsg(database)};
        }
        result.push_back({statement.text(0), statement.text(1)});
    }
    return result;
}

[[nodiscard]] std::vector<application::CohortPinnedVcfSource> read_sources(
    sqlite3* database,
    const std::string_view project_id,
    const std::string_view analysis_id
) {
    Statement statement{
        database,
        "SELECT project_sample_id,biological_unit_id,plan_id,workflow_id,producer_sample_id,"
        "attempt_number,job_id,step_id,output_port,module_id,plugin_version,managed_file_id,"
        "size_bytes,sha256,vcf_sample_name FROM cohort_analysis_sources "
        "WHERE project_id=? AND analysis_id=? ORDER BY project_sample_id COLLATE BINARY;",
        "Unable to read cohort analysis sources"
    };
    statement.bind_text(1, project_id);
    statement.bind_text(2, analysis_id);
    std::vector<application::CohortPinnedVcfSource> result;
    for (;;) {
        const int step = statement.step();
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW) {
            throw SqliteError{step, std::string{"Unable to read cohort analysis sources: "} +
                                    sqlite3_errmsg(database)};
        }
        result.push_back({
            .project_sample_id = statement.text(0),
            .biological_unit_id = statement.text(1),
            .plan_id = statement.text(2),
            .workflow_id = statement.text(3),
            .producer_sample_id = statement.text(4),
            .attempt_number = statement.integer(5),
            .job_id = statement.text(6),
            .step_id = statement.text(7),
            .output_port = statement.text(8),
            .module_id = statement.text(9),
            .plugin_version = statement.text(10),
            .managed_file_id = statement.text(11),
            .size_bytes = statement.integer(12),
            .sha256 = statement.text(13),
            .vcf_sample_name = statement.text(14),
        });
    }
    return result;
}

[[nodiscard]] std::vector<application::CohortAnalysisSampleSnapshot> read_samples(
    sqlite3* database,
    const std::string_view project_id,
    const std::string_view analysis_id
) {
    Statement statement{
        database,
        "SELECT ordinal,sample_id,sample_display_name,sample_group_metadata,biological_unit_id,"
        "group_token,cohort_disposition,cohort_exclusion_reason,analysis_disposition,"
        "analysis_reason,qc_state,qc_reason,qc_file_id,qc_job_id,qc_step_id,qc_output_port,"
        "qc_module_id,qc_plugin_version,qc_size_bytes,qc_sha256 "
        "FROM cohort_analysis_samples WHERE project_id=? AND analysis_id=? ORDER BY ordinal;",
        "Unable to read cohort analysis samples"
    };
    statement.bind_text(1, project_id);
    statement.bind_text(2, analysis_id);
    std::vector<application::CohortAnalysisSampleSnapshot> result;
    for (;;) {
        const int step = statement.step();
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW) {
            throw SqliteError{step, std::string{"Unable to read cohort analysis samples: "} +
                                    sqlite3_errmsg(database)};
        }
        const auto group = application::cohort_group_from_string(statement.text(5));
        const auto cohort_disposition =
            application::cohort_member_disposition_from_string(statement.text(6));
        const auto analysis_disposition =
            application::cohort_analysis_disposition_from_string(statement.text(8));
        const auto qc_state = application::cohort_qc_evidence_state_from_string(statement.text(10));
        if (!group.has_value() || !cohort_disposition.has_value() ||
            !analysis_disposition.has_value() || !qc_state.has_value()) {
            throw SqliteError{SQLITE_CORRUPT, "Cohort analysis sample contains unsupported enum"};
        }
        const auto qc_size = statement.optional_integer(18);
        result.push_back({
            .ordinal = static_cast<std::size_t>(statement.integer(0)),
            .sample_id = statement.text(1),
            .sample_display_name = statement.text(2),
            .sample_group_metadata = statement.text(3),
            .biological_unit_id = statement.text(4),
            .group = *group,
            .cohort_disposition = *cohort_disposition,
            .cohort_exclusion_reason = statement.optional_text(7),
            .analysis_disposition = *analysis_disposition,
            .analysis_reason = statement.optional_text(9),
            .qc = application::CohortQcArtifactEvidence{
                .state = *qc_state,
                .reason = statement.text(11),
                .managed_file_id = statement.optional_text(12),
                .job_id = statement.optional_text(13),
                .step_id = statement.optional_text(14),
                .output_port = statement.optional_text(15),
                .module_id = statement.optional_text(16),
                .plugin_version = statement.optional_text(17),
                .size_bytes = qc_size,
                .sha256 = statement.optional_text(19),
            },
        });
    }
    return result;
}

[[nodiscard]] std::vector<application::CohortAnalysisVariantUniverse> read_universe(
    sqlite3* database,
    const std::string_view project_id,
    const std::string_view analysis_id
) {
    Statement statement{
        database,
        "SELECT ordinal,contig,start_pos,end_pos,reference_allele,alternate_allele,"
        "case_unobserved,control_unobserved,case_no_calls,control_no_calls,"
        "case_partial_calls,control_partial_calls,case_complete_calls,control_complete_calls,"
        "allele_family_member,carrier_family_member "
        "FROM cohort_analysis_test_universe WHERE project_id=? AND analysis_id=? ORDER BY ordinal;",
        "Unable to read cohort analysis test universe"
    };
    statement.bind_text(1, project_id);
    statement.bind_text(2, analysis_id);
    std::vector<application::CohortAnalysisVariantUniverse> result;
    for (;;) {
        const int step = statement.step();
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW) {
            throw SqliteError{step, std::string{"Unable to read cohort analysis test universe: "} +
                                    sqlite3_errmsg(database)};
        }
        result.push_back({
            .ordinal = static_cast<std::size_t>(statement.integer(0)),
            .contig = statement.text(1),
            .start = static_cast<std::uint64_t>(statement.integer(2)),
            .end = static_cast<std::uint64_t>(statement.integer(3)),
            .reference = statement.text(4),
            .alternate = statement.text(5),
            .case_unobserved = static_cast<std::size_t>(statement.integer(6)),
            .control_unobserved = static_cast<std::size_t>(statement.integer(7)),
            .case_no_calls = static_cast<std::size_t>(statement.integer(8)),
            .control_no_calls = static_cast<std::size_t>(statement.integer(9)),
            .case_partial_calls = static_cast<std::size_t>(statement.integer(10)),
            .control_partial_calls = static_cast<std::size_t>(statement.integer(11)),
            .case_complete_calls = static_cast<std::size_t>(statement.integer(12)),
            .control_complete_calls = static_cast<std::size_t>(statement.integer(13)),
            .allele_family_member = statement.integer(14) == 1,
            .carrier_family_member = statement.integer(15) == 1,
        });
    }
    return result;
}

}  // namespace

SqliteCohortAnalysisSnapshotStore::SqliteCohortAnalysisSnapshotStore(
    SqliteConnection& connection
) noexcept : connection_{connection} {}

application::CohortAnalysisStoreResult SqliteCohortAnalysisSnapshotStore::create(
    const application::CohortAnalysisSnapshot& snapshot
) {
    Transaction transaction{connection_};
    sqlite3* database = connection_.native_handle();

    if (!row_exists(
            database,
            "SELECT 1 FROM project_metadata WHERE singleton=1 AND project_id=?;",
            {snapshot.project_id},
            "Unable to inspect cohort analysis project")) {
        return application::CohortAnalysisStoreResult::project_not_found;
    }
    if (!cohort_revision_exists(
            database, snapshot.project_id, snapshot.cohort_id, snapshot.cohort_revision)) {
        return application::CohortAnalysisStoreResult::cohort_revision_not_found;
    }
    if (!managed_file_exists(database, snapshot.reference.managed_file_id)) {
        return application::CohortAnalysisStoreResult::source_not_found;
    }
    for (const auto& source : snapshot.sources) {
        if (!managed_file_exists(database, source.managed_file_id)) {
            return application::CohortAnalysisStoreResult::source_not_found;
        }
    }
    for (const auto& sample : snapshot.samples) {
        if (sample.qc.managed_file_id.has_value() &&
            !managed_file_exists(database, *sample.qc.managed_file_id)) {
            return application::CohortAnalysisStoreResult::source_not_found;
        }
    }

    if (row_exists(
            database,
            "SELECT 1 FROM cohort_analysis_snapshots WHERE project_id=? AND analysis_id=?;",
            {snapshot.project_id, snapshot.analysis_id},
            "Unable to inspect cohort analysis id conflict")) {
        return application::CohortAnalysisStoreResult::analysis_id_conflict;
    }

    Statement header{
        database,
        "INSERT INTO cohort_analysis_snapshots("
        "project_id,analysis_id,cohort_id,cohort_revision,contract_version,preview_digest,"
        "snapshot_digest,approved_at_utc,reference_file_id,reference_file_type,"
        "reference_size_bytes,reference_sha256,reference_assembly,reference_custom_id,"
        "normalization_contract_version,association_contract_version,test_filter_version,"
        "minimum_complete_case_calls,minimum_complete_control_calls,maximum_fisher_table_states,"
        "approved_case_samples,approved_control_samples,allele_family_size,carrier_family_size,sealed"
        ") VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,0);",
        "Unable to insert cohort analysis snapshot"
    };
    header.bind_text(1, snapshot.project_id);
    header.bind_text(2, snapshot.analysis_id);
    header.bind_text(3, snapshot.cohort_id);
    header.bind_integer(4, snapshot.cohort_revision);
    header.bind_text(5, snapshot.contract_version);
    header.bind_text(6, snapshot.preview_digest);
    header.bind_text(7, snapshot.snapshot_digest);
    header.bind_text(8, snapshot.approved_at_utc);
    header.bind_text(9, snapshot.reference.managed_file_id);
    header.bind_text(10, snapshot.reference.file_type);
    header.bind_integer(11, snapshot.reference.size_bytes);
    header.bind_text(12, snapshot.reference.sha256);
    header.bind_text(13, assembly_token(snapshot.reference.assembly));
    header.bind_optional_text(14, snapshot.reference.custom_assembly_id);
    header.bind_text(15, snapshot.reference.normalization_contract_version);
    header.bind_text(16, snapshot.association_contract_version);
    header.bind_text(17, snapshot.test_filter_version);
    header.bind_integer(18, static_cast<std::int64_t>(
        snapshot.association_options.minimum_complete_case_calls));
    header.bind_integer(19, static_cast<std::int64_t>(
        snapshot.association_options.minimum_complete_control_calls));
    header.bind_integer(20, static_cast<std::int64_t>(
        snapshot.association_options.maximum_fisher_table_states));
    header.bind_integer(21, static_cast<std::int64_t>(snapshot.approved_case_samples));
    header.bind_integer(22, static_cast<std::int64_t>(snapshot.approved_control_samples));
    header.bind_integer(23, static_cast<std::int64_t>(snapshot.allele_family_size));
    header.bind_integer(24, static_cast<std::int64_t>(snapshot.carrier_family_size));
    require_done(database, header.step(), "Unable to insert cohort analysis snapshot");

    insert_samples(database, snapshot);
    insert_sources(database, snapshot);
    insert_reference(database, snapshot);
    insert_test_universe(database, snapshot);

    Statement seal{
        database,
        "UPDATE cohort_analysis_snapshots SET sealed=1 "
        "WHERE project_id=? AND analysis_id=? AND sealed=0;",
        "Unable to seal cohort analysis snapshot"
    };
    seal.bind_text(1, snapshot.project_id);
    seal.bind_text(2, snapshot.analysis_id);
    require_done(database, seal.step(), "Unable to seal cohort analysis snapshot");
    if (sqlite3_changes(database) != 1) {
        throw SqliteError{SQLITE_CONSTRAINT, "Cohort analysis snapshot could not be sealed exactly once"};
    }

    transaction.commit();
    return application::CohortAnalysisStoreResult::stored;
}

std::optional<application::CohortAnalysisSnapshot>
SqliteCohortAnalysisSnapshotStore::find(
    const std::string_view project_id,
    const std::string_view analysis_id
) {
    sqlite3* database = connection_.native_handle();
    Statement header{
        database,
        "SELECT cohort_id,cohort_revision,contract_version,preview_digest,snapshot_digest,"
        "approved_at_utc,reference_file_id,reference_file_type,reference_size_bytes,"
        "reference_sha256,reference_assembly,reference_custom_id,normalization_contract_version,"
        "association_contract_version,test_filter_version,minimum_complete_case_calls,"
        "minimum_complete_control_calls,maximum_fisher_table_states,approved_case_samples,"
        "approved_control_samples,allele_family_size,carrier_family_size,sealed "
        "FROM cohort_analysis_snapshots WHERE project_id=? AND analysis_id=?;",
        "Unable to read cohort analysis snapshot"
    };
    header.bind_text(1, project_id);
    header.bind_text(2, analysis_id);
    const int result = header.step();
    if (result == SQLITE_DONE) return std::nullopt;
    if (result != SQLITE_ROW) {
        throw SqliteError{result, std::string{"Unable to read cohort analysis snapshot: "} +
                                sqlite3_errmsg(database)};
    }
    if (header.integer(22) != 1) {
        throw SqliteError{SQLITE_CORRUPT, "Cohort analysis snapshot is not sealed"};
    }
    const auto assembly = assembly_from_token(header.text(10));
    if (!assembly.has_value()) {
        throw SqliteError{SQLITE_CORRUPT, "Cohort analysis snapshot reference assembly is invalid"};
    }

    application::CohortAnalysisSnapshot snapshot{
        .analysis_id = std::string{analysis_id},
        .project_id = std::string{project_id},
        .cohort_id = header.text(0),
        .cohort_revision = static_cast<std::uint32_t>(header.integer(1)),
        .contract_version = header.text(2),
        .preview_digest = header.text(3),
        .snapshot_digest = header.text(4),
        .approved_at_utc = header.text(5),
        .reference = application::CohortPinnedReference{
            .managed_file_id = header.text(6),
            .file_type = header.text(7),
            .size_bytes = header.integer(8),
            .sha256 = header.text(9),
            .assembly = *assembly,
            .custom_assembly_id = header.optional_text(11),
            .normalization_contract_version = header.text(12),
            .contigs = read_contigs(database, project_id, analysis_id),
            .aliases = read_aliases(database, project_id, analysis_id),
        },
        .sources = read_sources(database, project_id, analysis_id),
        .samples = read_samples(database, project_id, analysis_id),
        .association_options = application::CohortAssociationApprovalOptions{
            .minimum_complete_case_calls = static_cast<std::size_t>(header.integer(15)),
            .minimum_complete_control_calls = static_cast<std::size_t>(header.integer(16)),
            .maximum_fisher_table_states = static_cast<std::size_t>(header.integer(17)),
        },
        .association_contract_version = header.text(13),
        .test_filter_version = header.text(14),
        .approved_case_samples = static_cast<std::size_t>(header.integer(18)),
        .approved_control_samples = static_cast<std::size_t>(header.integer(19)),
        .allele_family_size = static_cast<std::size_t>(header.integer(20)),
        .carrier_family_size = static_cast<std::size_t>(header.integer(21)),
        .test_universe = read_universe(database, project_id, analysis_id),
    };
    return snapshot;
}

}  // namespace biocore::infrastructure::sqlite
