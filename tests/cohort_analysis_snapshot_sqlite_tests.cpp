#include <sqlite3.h>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/cohort_analysis_snapshot.hpp"
#include "biocore/application/cohort_registry_service.hpp"
#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/project.hpp"
#include "biocore/domain/project_sample.hpp"
#include "biocore/infrastructure/sqlite/project_database_guard.hpp"
#include "biocore/infrastructure/sqlite/project_database_initializer.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_cohort_analysis_snapshot_store.hpp"
#include "biocore/infrastructure/sqlite/sqlite_cohort_registry_store.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"
#include "biocore/infrastructure/sqlite/sqlite_managed_file_repository.hpp"
#include "biocore/infrastructure/sqlite/sqlite_project_sample_store.hpp"

namespace {
using namespace biocore;
using namespace biocore::infrastructure::sqlite;

constexpr const char* stamp = "2026-10-02T16:00:00Z";
constexpr const char* ref_hash =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
constexpr const char* vcf_hash =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr const char* qc_hash =
    "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
constexpr const char* preview_hash =
    "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
constexpr const char* snapshot_hash =
    "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";

void check(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

template<class Exception, class Function>
void rejects(Function function) {
    try { function(); } catch (const Exception&) { return; }
    throw std::runtime_error{"Expected rejection"};
}

class FixedIds final : public application::IIdGenerator {
public:
    explicit FixedIds(std::vector<std::string> values) : values_{std::move(values)} {}
    std::string generate() override {
        if (index_ >= values_.size()) throw std::runtime_error{"ID fixture exhausted"};
        return values_[index_++];
    }
private:
    std::vector<std::string> values_;
    std::size_t index_{0U};
};

class FixedClock final : public application::IUtcClock {
public:
    std::string now_utc_iso8601() override { return stamp; }
};

[[nodiscard]] std::string scalar(SqliteConnection& connection, const char* sql) {
    sqlite3_stmt* statement = nullptr;
    check(
        sqlite3_prepare_v2(connection.native_handle(), sql, -1, &statement, nullptr) == SQLITE_OK,
        "prepare scalar"
    );
    const int result = sqlite3_step(statement);
    check(result == SQLITE_ROW, "scalar row");
    const auto* value = sqlite3_column_text(statement, 0);
    std::string output = value == nullptr
        ? std::string{}
        : std::string{
            reinterpret_cast<const char*>(value),
            static_cast<std::size_t>(sqlite3_column_bytes(statement, 0))
        };
    sqlite3_finalize(statement);
    return output;
}

[[nodiscard]] domain::ManagedFile file(
    std::string id,
    std::string type,
    std::string checksum
) {
    const auto relative = "inputs/" + id + "/" + id + "." + type;
    return {
        std::move(id),
        "fixture",
        domain::StorageMode::managed_copy,
        "/source/fixture",
        "/project/" + relative,
        relative,
        std::move(type),
        100,
        std::nullopt,
        std::string{"sha256"},
        std::move(checksum),
        stamp,
        stamp
    };
}

application::CohortMemberDraft member(
    std::string sample_id,
    application::CohortGroup group
) {
    const std::string biological = "bio-" + sample_id;
    return {
        .sample_id = std::move(sample_id),
        .biological_unit_id = biological,
        .group = group,
        .disposition = application::CohortMemberDisposition::included,
        .exclusion_reason = std::nullopt,
    };
}

void initialize(SqliteConnection& connection) {
    ProjectDatabaseInitializer{connection}.initialize(
        domain::Project{"p-001", "Research", "/local/project", stamp, stamp}
    );

    SqliteProjectSampleStore samples{connection};
    const std::vector<domain::ProjectSample> rows{
        {"p-001", "case-1", "Case α", "legacy-case"},
        {"p-001", "control-1", "Control β", "legacy-control"},
    };
    check(
        samples.add_batch("p-001", rows) == application::SampleBatchAddResult::added,
        "seed project samples"
    );

    SqliteCohortRegistryStore cohorts{connection};
    FixedIds ids{{"c-093"}};
    FixedClock clock;
    application::CohortRegistryService service{cohorts, ids, clock};
    const auto created = service.create({
        .project_id = "p-001",
        .name = "Analysis cohort",
        .members = {
            member("case-1", application::CohortGroup::case_group),
            member("control-1", application::CohortGroup::control),
        },
    });
    check(created.cohort_id == "c-093", "seed cohort id");

    SqliteManagedFileRepository files{connection};
    check(files.add(file("ref", "fasta", ref_hash)), "seed reference");
    check(files.add(file("vcf-case", "vcf", vcf_hash)), "seed case VCF");
    check(files.add(file("vcf-control", "vcf", vcf_hash)), "seed control VCF");
    check(files.add(file("qc-case", "json", qc_hash)), "seed case QC");
    check(files.add(file("qc-control", "json", qc_hash)), "seed control QC");
}

application::CohortPinnedVcfSource source(
    std::string sample,
    std::string file_id
) {
    return {
        .project_sample_id = sample,
        .biological_unit_id = "bio-" + sample,
        .plan_id = "plan-1",
        .workflow_id = "workflow-1",
        .producer_sample_id = sample,
        .attempt_number = 1,
        .job_id = "job-" + sample,
        .step_id = "variants",
        .output_port = "vcf",
        .module_id = "org.biocore.vcfqc.filter",
        .plugin_version = "0.1.0",
        .managed_file_id = std::move(file_id),
        .size_bytes = 100,
        .sha256 = vcf_hash,
        .vcf_sample_name = sample,
    };
}

application::CohortQcArtifactEvidence qc(
    std::string file_id,
    std::string job_id
) {
    return {
        .state = application::CohortQcEvidenceState::verified,
        .reason = "Verified VCF QC summary",
        .managed_file_id = std::move(file_id),
        .job_id = std::move(job_id),
        .step_id = std::string{"variants"},
        .output_port = std::string{"summary"},
        .module_id = std::string{"org.biocore.vcfqc.filter"},
        .plugin_version = std::string{"0.1.0"},
        .size_bytes = std::int64_t{100},
        .sha256 = std::string{qc_hash},
    };
}

application::CohortAnalysisSampleSnapshot sample(
    std::size_t ordinal,
    std::string id,
    application::CohortGroup group,
    std::string qc_file
) {
    return {
        .ordinal = ordinal,
        .sample_id = id,
        .sample_display_name = "display-" + id,
        .sample_group_metadata = "legacy",
        .biological_unit_id = "bio-" + id,
        .group = group,
        .cohort_disposition = application::CohortMemberDisposition::included,
        .cohort_exclusion_reason = std::nullopt,
        .analysis_disposition = application::CohortAnalysisDisposition::included,
        .analysis_reason = std::nullopt,
        .qc = qc(std::move(qc_file), "job-" + id),
    };
}

application::CohortAnalysisSnapshot snapshot_fixture() {
    return {
        .analysis_id = "analysis-1",
        .project_id = "p-001",
        .cohort_id = "c-093",
        .cohort_revision = 1U,
        .contract_version = "biocore.cohort-analysis.v1",
        .preview_digest = preview_hash,
        .snapshot_digest = snapshot_hash,
        .approved_at_utc = stamp,
        .reference = {
            .managed_file_id = "ref",
            .file_type = "fasta",
            .size_bytes = 100,
            .sha256 = ref_hash,
            .assembly = domain::ReferenceAssembly::grch38,
            .custom_assembly_id = std::nullopt,
            .normalization_contract_version = "biocore.normalized-allele.v1",
            .contigs = {{"1", 1000U}},
            .aliases = {{"chr1", "1"}},
        },
        .sources = {
            source("case-1", "vcf-case"),
            source("control-1", "vcf-control"),
        },
        .samples = {
            sample(0U, "case-1", application::CohortGroup::case_group, "qc-case"),
            sample(1U, "control-1", application::CohortGroup::control, "qc-control"),
        },
        .resource_limits = {
            .maximum_samples = 100U,
            .maximum_sources = 100U,
            .maximum_vcf_bytes = 64U * 1024U * 1024U,
            .maximum_combined_vcf_bytes = 256U * 1024U * 1024U,
            .maximum_reference_bytes = 512U * 1024U * 1024U,
            .maximum_normalized_alleles = 10000U,
            .maximum_observations = 1000000U,
        },
        .association_options = {
            .minimum_complete_case_calls = 1U,
            .minimum_complete_control_calls = 1U,
            .maximum_fisher_table_states = 100000U,
        },
        .association_contract_version = "biocore.case-control.v1",
        .test_filter_version = "biocore.complete-call-gate.v1",
        .approved_case_samples = 1U,
        .approved_control_samples = 1U,
        .allele_family_size = 1U,
        .carrier_family_size = 1U,
        .test_universe = {{
            .ordinal = 0U,
            .contig = "1",
            .start = 9U,
            .end = 10U,
            .reference = "A",
            .alternate = "G",
            .case_unobserved = 0U,
            .control_unobserved = 0U,
            .case_no_calls = 0U,
            .control_no_calls = 0U,
            .case_partial_calls = 0U,
            .control_partial_calls = 0U,
            .case_complete_calls = 1U,
            .control_complete_calls = 1U,
            .allele_family_member = true,
            .carrier_family_member = true,
        }},
    };
}

void drop_v17(SqliteConnection& connection) {
    connection.execute("DROP TABLE cohort_analysis_test_universe;");
    connection.execute("DROP TABLE cohort_analysis_reference_aliases;");
    connection.execute("DROP TABLE cohort_analysis_reference_contigs;");
    connection.execute("DROP TABLE cohort_analysis_sources;");
    connection.execute("DROP TABLE cohort_analysis_samples;");
    connection.execute("DROP TABLE cohort_analysis_snapshots;");
    connection.execute("DELETE FROM schema_migrations WHERE version=17;");
}

void roundtrip_contract() {
    SqliteConnection connection{":memory:"};
    initialize(connection);
    SqliteCohortAnalysisSnapshotStore store{connection};
    const auto snapshot = snapshot_fixture();

    check(
        store.create(snapshot) == application::CohortAnalysisStoreResult::stored,
        "snapshot create"
    );
    const auto loaded = store.find("p-001", "analysis-1");
    check(loaded.has_value(), "snapshot roundtrip missing");
    check(
        loaded->snapshot_digest == snapshot.snapshot_digest &&
        loaded->preview_digest == snapshot.preview_digest &&
        loaded->reference.sha256 == snapshot.reference.sha256 &&
        loaded->sources.size() == 2U &&
        loaded->samples.size() == 2U &&
        loaded->resource_limits == snapshot.resource_limits &&
        loaded->test_universe.size() == 1U &&
        loaded->test_universe[0].case_complete_calls == 1U &&
        loaded->allele_family_size == 1U,
        "snapshot roundtrip changed immutable content"
    );
    check(
        scalar(connection,
               "SELECT sealed FROM cohort_analysis_snapshots WHERE analysis_id='analysis-1';")
            == "1",
        "snapshot was not sealed"
    );
}

void immutability_contract() {
    SqliteConnection connection{":memory:"};
    initialize(connection);
    SqliteCohortAnalysisSnapshotStore store{connection};
    check(
        store.create(snapshot_fixture()) == application::CohortAnalysisStoreResult::stored,
        "seed snapshot"
    );

    rejects<SqliteError>([&] {
        connection.execute(
            "UPDATE cohort_analysis_snapshots SET approved_case_samples=2 "
            "WHERE analysis_id='analysis-1';"
        );
    });
    rejects<SqliteError>([&] {
        connection.execute(
            "UPDATE cohort_analysis_samples SET group_token='control' "
            "WHERE analysis_id='analysis-1' AND sample_id='case-1';"
        );
    });
    rejects<SqliteError>([&] {
        connection.execute(
            "DELETE FROM cohort_analysis_test_universe WHERE analysis_id='analysis-1';"
        );
    });
    rejects<SqliteError>([&] {
        connection.execute(
            "DELETE FROM cohort_analysis_snapshots WHERE analysis_id='analysis-1';"
        );
    });
    rejects<SqliteError>([&] {
        connection.execute("DELETE FROM managed_files WHERE id='vcf-case';");
    });

    check(
        scalar(connection,
               "SELECT approved_case_samples FROM cohort_analysis_snapshots "
               "WHERE analysis_id='analysis-1';") == "1",
        "sealed snapshot mutated"
    );
}

void atomicity_contract() {
    SqliteConnection connection{":memory:"};
    initialize(connection);
    SqliteCohortAnalysisSnapshotStore store{connection};

    connection.execute(
        "CREATE TRIGGER reject_control_snapshot BEFORE INSERT ON cohort_analysis_samples "
        "WHEN NEW.sample_id='control-1' "
        "BEGIN SELECT RAISE(ABORT,'injected snapshot write failure'); END;"
    );
    rejects<SqliteError>([&] {
        static_cast<void>(store.create(snapshot_fixture()));
    });
    check(
        scalar(connection, "SELECT COUNT(*) FROM cohort_analysis_snapshots;") == "0" &&
        scalar(connection, "SELECT COUNT(*) FROM cohort_analysis_samples;") == "0" &&
        scalar(connection, "SELECT COUNT(*) FROM cohort_analysis_sources;") == "0",
        "failed snapshot create left partial rows"
    );

    connection.execute("DROP TRIGGER reject_control_snapshot;");
    check(
        store.create(snapshot_fixture()) == application::CohortAnalysisStoreResult::stored,
        "snapshot retry after rollback"
    );
}

void migration_contract() {
    SqliteConnection connection{":memory:"};
    initialize(connection);
    drop_v17(connection);
    ProjectMigrationRunner runner{connection};
    check(runner.current_version() == 16, "v16 migration fixture setup failed");

    runner.apply_pending();
    ProjectDatabaseGuard{connection}.validate_current_schema();
    check(
        runner.current_version() == 17 &&
        scalar(connection,
               "SELECT COUNT(*) FROM sqlite_master "
               "WHERE type='table' AND name='cohort_analysis_snapshots';") == "1",
        "v16 to v17 snapshot migration failed"
    );
    check(
        scalar(connection, "SELECT COUNT(*) FROM project_samples;") == "2",
        "v17 migration changed existing project samples"
    );
}

void missing_source_contract() {
    SqliteConnection connection{":memory:"};
    initialize(connection);
    SqliteCohortAnalysisSnapshotStore store{connection};
    auto snapshot = snapshot_fixture();
    snapshot.sources[0].managed_file_id = "missing-vcf";

    check(
        store.create(snapshot) == application::CohortAnalysisStoreResult::source_not_found,
        "missing source was accepted"
    );
    check(
        scalar(connection, "SELECT COUNT(*) FROM cohort_analysis_snapshots;") == "0",
        "missing source left a partial snapshot"
    );
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        if (argc != 2) return EXIT_FAILURE;
        const std::string_view mode{argv[1]};
        if (mode == "roundtrip") roundtrip_contract();
        else if (mode == "immutability") immutability_contract();
        else if (mode == "atomicity") atomicity_contract();
        else if (mode == "migration") migration_contract();
        else if (mode == "missing-source") missing_source_contract();
        else return EXIT_FAILURE;
        std::cout << mode << " PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
