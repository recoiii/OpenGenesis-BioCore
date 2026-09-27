#include <sqlite3.h>
#include <zlib.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "biocore/application/i_input_file_storage.hpp"
#include "biocore/application/i_reference_compatibility_inspector.hpp"
#include "biocore/application/sample_binding_service.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/project.hpp"
#include "biocore/domain/project_sample.hpp"
#include "biocore/domain/project_sample_binding.hpp"
#include "biocore/domain/storage_mode.hpp"
#include "biocore/infrastructure/filesystem_reference_compatibility_inspector.hpp"
#include "biocore/infrastructure/sqlite/project_database_guard.hpp"
#include "biocore/infrastructure/sqlite/project_database_initializer.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"
#include "biocore/infrastructure/sqlite/sqlite_managed_file_repository.hpp"
#include "biocore/infrastructure/sqlite/sqlite_project_sample_binding_store.hpp"
#include "biocore/infrastructure/sqlite/sqlite_project_sample_store.hpp"

namespace {

using namespace biocore;
using namespace biocore::infrastructure;
using namespace biocore::infrastructure::sqlite;

constexpr const char* stamp = "2026-09-27T20:00:00Z";
constexpr const char* project_id = "p-001";
constexpr const char* sample_id = "001";

void check(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

template<class Exception, class Function>
void rejects(Function function) {
    try { function(); } catch (const Exception&) { return; }
    throw std::runtime_error{"Expected rejection"};
}

class Temp final {
public:
    Temp() : root{std::filesystem::temp_directory_path() /
        ("biocore-binding-081-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()))} {
        std::filesystem::create_directory(root);
    }

    ~Temp() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }

    std::filesystem::path root;
};

class FakeInputStorage final : public application::IInputFileStorage {
public:
    std::unordered_map<std::string, application::ManagedFileIntegrityStatus> status;

    std::unique_ptr<application::IInputFileImportTransaction> prepare_managed_copy(
        std::string_view, std::string_view
    ) override {
        throw std::logic_error{"unused"};
    }

    bool begin_browser_upload(std::string_view, std::string_view) override {
        throw std::logic_error{"unused"};
    }

    std::uint64_t append_browser_upload(
        std::string_view, std::uint64_t, std::string_view
    ) override {
        throw std::logic_error{"unused"};
    }

    std::unique_ptr<application::IInputFileImportTransaction>
    prepare_browser_upload_commit(std::string_view, std::string_view) override {
        throw std::logic_error{"unused"};
    }

    void discard_browser_upload(std::string_view) noexcept override {}

    application::ManagedFileIntegrityResult verify_managed_file(
        const domain::ManagedFile& file
    ) const override {
        const auto found = status.find(std::string{file.id()});
        const auto value = found == status.end()
            ? application::ManagedFileIntegrityStatus::verified
            : found->second;
        return application::ManagedFileIntegrityResult{
            .status = value,
            .expected_size_bytes = file.size_bytes(),
            .observed_size_bytes = value == application::ManagedFileIntegrityStatus::file_missing
                ? std::nullopt
                : std::optional<std::int64_t>{file.size_bytes()},
            .expected_sha256 = file.checksum_value(),
            .observed_sha256 = value == application::ManagedFileIntegrityStatus::verified
                ? file.checksum_value()
                : std::nullopt,
        };
    }
};

class FakeReferenceInspector final
    : public application::IReferenceCompatibilityInspector {
public:
    application::ReferenceCompatibilityResult next{
        application::ReferenceCompatibilityStatus::verified,
        "verified"
    };

    application::ReferenceCompatibilityResult inspect(
        const domain::ManagedFile&,
        const domain::ManagedFile&
    ) const override {
        return next;
    }
};

[[nodiscard]] domain::ManagedFile file(
    std::string id,
    std::string type,
    std::string path
) {
    return domain::ManagedFile{
        std::move(id),
        path,
        domain::StorageMode::managed_copy,
        "/original/" + path,
        "/project/inputs/" + path,
        "inputs/" + path,
        std::move(type),
        16,
        std::nullopt,
        std::string{"sha256"},
        std::string(64U, 'a'),
        stamp,
        stamp,
    };
}

void initialize(SqliteConnection& connection) {
    ProjectDatabaseInitializer{connection}.initialize(
        domain::Project{project_id, "Research", "/local/project", stamp, stamp}
    );
}

void seed_sample_and_files(
    SqliteConnection& connection,
    const std::vector<domain::ManagedFile>& files
) {
    SqliteProjectSampleStore samples{connection};
    check(
        samples.add_batch(
            project_id,
            std::array{domain::ProjectSample{project_id, sample_id, "Sample", "case"}}
        ) == application::SampleBatchAddResult::added,
        "sample seed failed"
    );

    SqliteManagedFileRepository managed{connection};
    for (const auto& item : files) {
        check(managed.add(item), "managed file seed failed");
    }
}

[[nodiscard]] std::int64_t scalar_int64(
    SqliteConnection& connection,
    const char* sql
) {
    sqlite3_stmt* statement = nullptr;
    check(
        sqlite3_prepare_v2(connection.native_handle(), sql, -1, &statement, nullptr)
            == SQLITE_OK,
        "scalar prepare failed"
    );
    const int step = sqlite3_step(statement);
    check(step == SQLITE_ROW, "scalar row missing");
    const auto value = sqlite3_column_int64(statement, 0);
    sqlite3_finalize(statement);
    return value;
}

[[nodiscard]] std::string scalar_text(
    SqliteConnection& connection,
    const char* sql
) {
    sqlite3_stmt* statement = nullptr;
    check(
        sqlite3_prepare_v2(connection.native_handle(), sql, -1, &statement, nullptr)
            == SQLITE_OK,
        "text scalar prepare failed"
    );
    const int step = sqlite3_step(statement);
    check(step == SQLITE_ROW, "text scalar row missing");
    const auto* raw = sqlite3_column_text(statement, 0);
    const std::string value = raw == nullptr
        ? std::string{}
        : std::string{
            reinterpret_cast<const char*>(raw),
            static_cast<std::size_t>(sqlite3_column_bytes(statement, 0))
        };
    sqlite3_finalize(statement);
    return value;
}

void domain_contract() {
    const domain::ProjectSampleBinding paired{
        project_id,
        sample_id,
        domain::SampleInputLayout::paired_fastq,
        "read-1",
        std::string{"read-2"},
        std::string{"ref-1"},
    };
    check(
        paired.primary_file_id() == "read-1" &&
        paired.secondary_file_id() == std::optional<std::string>{"read-2"} &&
        domain::to_string(paired.layout()) == "paired_fastq" &&
        domain::sample_input_layout_from_string("variants") ==
            domain::SampleInputLayout::variants,
        "binding domain round-trip failed"
    );

    rejects<std::invalid_argument>([] {
        static_cast<void>(domain::ProjectSampleBinding{
            project_id, sample_id, domain::SampleInputLayout::paired_fastq,
            "same", std::string{"same"}, std::nullopt
        });
    });
    rejects<std::invalid_argument>([] {
        static_cast<void>(domain::ProjectSampleBinding{
            project_id, sample_id, domain::SampleInputLayout::single_fastq,
            "r1", std::string{"r2"}, std::nullopt
        });
    });
    rejects<std::invalid_argument>([] {
        static_cast<void>(domain::ProjectSampleBinding{
            project_id, " ", domain::SampleInputLayout::alignment,
            "a", std::nullopt, std::nullopt
        });
    });
}

void role_contract() {
    SqliteConnection connection{":memory:"};
    initialize(connection);
    seed_sample_and_files(
        connection,
        {
            file("r1", "fastq", "r1.fastq"),
            file("r2-wrong", "fasta", "r2.fa"),
            file("ref", "fasta", "ref.fa"),
        }
    );

    SqliteProjectSampleStore samples{connection};
    SqliteProjectSampleBindingStore bindings{connection};
    SqliteManagedFileRepository managed{connection};
    FakeInputStorage storage;
    FakeReferenceInspector inspector;
    application::SampleBindingService service{
        samples, bindings, managed, storage, inspector
    };

    const auto wrong_read2 = service.preview(domain::ProjectSampleBinding{
        project_id, sample_id, domain::SampleInputLayout::paired_fastq,
        "r1", std::string{"r2-wrong"}, std::string{"ref"}
    });
    check(!wrong_read2.valid(), "wrong read2 role accepted");

    const auto wrong_primary = service.preview(domain::ProjectSampleBinding{
        project_id, sample_id, domain::SampleInputLayout::alignment,
        "r1", std::nullopt, std::string{"ref"}
    });
    check(!wrong_primary.valid(), "FASTQ accepted as alignment");

    const auto missing_sample = service.preview(domain::ProjectSampleBinding{
        project_id, "missing", domain::SampleInputLayout::single_fastq,
        "r1", std::nullopt, std::nullopt
    });
    check(!missing_sample.valid(), "unknown sample accepted");
}

void integrity_contract() {
    SqliteConnection connection{":memory:"};
    initialize(connection);
    seed_sample_and_files(
        connection,
        {
            file("r1", "fastq", "r1.fastq"),
            file("r2", "fastq", "r2.fastq"),
        }
    );

    SqliteProjectSampleStore samples{connection};
    SqliteProjectSampleBindingStore bindings{connection};
    SqliteManagedFileRepository managed{connection};
    FakeInputStorage storage;
    FakeReferenceInspector inspector;
    application::SampleBindingService service{
        samples, bindings, managed, storage, inspector
    };

    storage.status["r2"] = application::ManagedFileIntegrityStatus::checksum_mismatch;
    const domain::ProjectSampleBinding requested{
        project_id, sample_id, domain::SampleInputLayout::paired_fastq,
        "r1", std::string{"r2"}, std::nullopt
    };
    const auto rejected = service.commit(requested);
    check(!rejected.valid(), "changed read2 accepted");
    check(!bindings.find(project_id, sample_id).has_value(), "invalid binding persisted");

    storage.status["r2"] = application::ManagedFileIntegrityStatus::verified;
    const auto accepted = service.commit(requested);
    check(accepted.valid(), "verified binding rejected");

    storage.status["r1"] = application::ManagedFileIntegrityStatus::file_missing;
    const domain::ProjectSampleBinding replacement{
        project_id, sample_id, domain::SampleInputLayout::single_fastq,
        "r1", std::nullopt, std::nullopt
    };
    const auto stale = service.commit(replacement);
    check(!stale.valid(), "missing input accepted");
    const auto stored = bindings.find(project_id, sample_id);
    check(
        stored.has_value() &&
        stored->layout() == domain::SampleInputLayout::paired_fastq,
        "failed revalidation replaced prior binding"
    );
}

void reference_contract() {
    SqliteConnection connection{":memory:"};
    initialize(connection);
    seed_sample_and_files(
        connection,
        {
            file("fastq", "fastq", "reads.fastq"),
            file("sam", "sam", "alignment.sam"),
            file("ref", "fasta", "reference.fa"),
        }
    );

    SqliteProjectSampleStore samples{connection};
    SqliteProjectSampleBindingStore bindings{connection};
    SqliteManagedFileRepository managed{connection};
    FakeInputStorage storage;
    FakeReferenceInspector inspector;
    application::SampleBindingService service{
        samples, bindings, managed, storage, inspector
    };

    inspector.next = {
        application::ReferenceCompatibilityStatus::not_declared_by_format,
        "FASTQ has no reference declaration"
    };
    const auto reads = service.preview(domain::ProjectSampleBinding{
        project_id, sample_id, domain::SampleInputLayout::single_fastq,
        "fastq", std::nullopt, std::string{"ref"}
    });
    check(
        reads.valid() &&
        reads.reference_status ==
            application::SampleBindingReferenceStatus::not_declared_by_format &&
        !reads.issues.empty() &&
        reads.issues.front().severity ==
            application::SampleBindingIssueSeverity::warning,
        "reference-unknown FASTQ was incorrectly treated as verified or blocked"
    );

    inspector.next = {
        application::ReferenceCompatibilityStatus::mismatch,
        "contig mismatch"
    };
    const auto mismatch = service.preview(domain::ProjectSampleBinding{
        project_id, sample_id, domain::SampleInputLayout::alignment,
        "sam", std::nullopt, std::string{"ref"}
    });
    check(
        !mismatch.valid() &&
        mismatch.reference_status == application::SampleBindingReferenceStatus::mismatch,
        "reference mismatch accepted"
    );

    inspector.next = {
        application::ReferenceCompatibilityStatus::verified,
        "verified"
    };
    const auto verified = service.preview(domain::ProjectSampleBinding{
        project_id, sample_id, domain::SampleInputLayout::alignment,
        "sam", std::nullopt, std::string{"ref"}
    });
    check(
        verified.valid() &&
        verified.reference_status == application::SampleBindingReferenceStatus::verified,
        "verified reference rejected"
    );
}

void persistence_contract() {
    SqliteConnection connection{":memory:"};
    initialize(connection);
    seed_sample_and_files(
        connection,
        {
            file("r1", "fastq", "r1.fastq"),
            file("r2", "fastq", "r2.fastq"),
            file("ref", "fasta", "reference.fa"),
        }
    );

    const std::int64_t files_before =
        scalar_int64(connection, "SELECT COUNT(*) FROM managed_files;");
    const std::string r1_path_before = scalar_text(
        connection,
        "SELECT relative_project_path FROM managed_files WHERE id='r1';"
    );

    SqliteProjectSampleBindingStore store{connection};
    store.upsert(domain::ProjectSampleBinding{
        project_id, sample_id, domain::SampleInputLayout::paired_fastq,
        "r1", std::string{"r2"}, std::string{"ref"}
    });
    const auto found = store.find(project_id, sample_id);
    check(
        found.has_value() &&
        found->secondary_file_id() == std::optional<std::string>{"r2"} &&
        store.list(project_id).size() == 1U,
        "binding persistence round-trip failed"
    );

    store.upsert(domain::ProjectSampleBinding{
        project_id, sample_id, domain::SampleInputLayout::single_fastq,
        "r1", std::nullopt, std::nullopt
    });
    check(
        store.find(project_id, sample_id)->layout() ==
            domain::SampleInputLayout::single_fastq,
        "binding update failed"
    );

    check(
        scalar_int64(connection, "SELECT COUNT(*) FROM managed_files;") == files_before &&
        scalar_text(
            connection,
            "SELECT relative_project_path FROM managed_files WHERE id='r1';"
        ) == r1_path_before,
        "binding persistence changed managed-file ownership"
    );

    rejects<SqliteError>([&] {
        store.upsert(domain::ProjectSampleBinding{
            project_id, "missing", domain::SampleInputLayout::single_fastq,
            "r1", std::nullopt, std::nullopt
        });
    });
    rejects<SqliteError>([&] {
        store.upsert(domain::ProjectSampleBinding{
            project_id, sample_id, domain::SampleInputLayout::single_fastq,
            "unknown-file", std::nullopt, std::nullopt
        });
    });
}

void write_text(const std::filesystem::path& path, const std::string_view text) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    check(static_cast<bool>(output), "unable to create fixture file");
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    check(static_cast<bool>(output), "unable to write fixture file");
}

[[nodiscard]] gzFile open_gzip_write(const std::filesystem::path& path) {
#ifdef _WIN32
    return gzopen_w(path.c_str(), "wb");
#else
    return gzopen(path.c_str(), "wb");
#endif
}

void gz_write_all(gzFile file_handle, const void* data, const std::size_t size) {
    const auto* cursor = static_cast<const unsigned char*>(data);
    std::size_t remaining = size;
    while (remaining > 0U) {
        const unsigned int chunk = static_cast<unsigned int>(
            std::min<std::size_t>(remaining, 1U << 20U)
        );
        const int written = gzwrite(file_handle, cursor, chunk);
        check(written == static_cast<int>(chunk), "unable to write BAM fixture");
        cursor += chunk;
        remaining -= chunk;
    }
}

void gz_write_i32(gzFile file_handle, const std::int32_t value) {
    const std::uint32_t raw = static_cast<std::uint32_t>(value);
    const std::array<unsigned char, 4> bytes{{
        static_cast<unsigned char>(raw & 0xffU),
        static_cast<unsigned char>((raw >> 8U) & 0xffU),
        static_cast<unsigned char>((raw >> 16U) & 0xffU),
        static_cast<unsigned char>((raw >> 24U) & 0xffU),
    }};
    gz_write_all(file_handle, bytes.data(), bytes.size());
}

void write_bam_header(
    const std::filesystem::path& path,
    const std::string_view contig,
    const std::int32_t length
) {
    gzFile raw = open_gzip_write(path);
    check(raw != nullptr, "unable to create BAM fixture");
    const std::array<char, 4> magic{{'B', 'A', 'M', '\1'}};
    gz_write_all(raw, magic.data(), magic.size());
    gz_write_i32(raw, 0);
    gz_write_i32(raw, 1);
    gz_write_i32(raw, static_cast<std::int32_t>(contig.size() + 1U));
    gz_write_all(raw, contig.data(), contig.size());
    const char nul = '\0';
    gz_write_all(raw, &nul, 1U);
    gz_write_i32(raw, length);
    check(gzclose(raw) == Z_OK, "unable to close BAM fixture");
}

[[nodiscard]] domain::ManagedFile local_file(
    std::string id,
    std::string type,
    const std::filesystem::path& path
) {
    return domain::ManagedFile{
        std::move(id),
        path.filename().string(),
        domain::StorageMode::managed_copy,
        path.string(),
        path.string(),
        "inputs/" + path.filename().string(),
        std::move(type),
        static_cast<std::int64_t>(std::filesystem::file_size(path)),
        std::nullopt,
        std::string{"sha256"},
        std::string(64U, 'b'),
        stamp,
        stamp,
    };
}

void inspector_contract() {
    Temp temp;
    const auto fasta_path = temp.root / "reference.fa";
    const auto sam_path = temp.root / "good.sam";
    const auto bad_sam_path = temp.root / "bad.sam";
    const auto vcf_path = temp.root / "good.vcf";
    const auto bare_vcf_path = temp.root / "bare.vcf";
    const auto bam_path = temp.root / "good.bam";

    write_text(fasta_path, ">chr1\nACGT\n>chr2\nAA\n");
    write_text(sam_path, "@HD\tVN:1.6\n@SQ\tSN:chr1\tLN:4\nread\t4\t*\t0\t0\t*\t*\t0\t0\tA\tI\n");
    write_text(bad_sam_path, "@SQ\tSN:chr1\tLN:5\n");
    write_text(vcf_path, "##fileformat=VCFv4.3\n##contig=<ID=chr1,length=4>\n#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\n");
    write_text(bare_vcf_path, "##fileformat=VCFv4.3\n#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\n");
    write_bam_header(bam_path, "chr1", 4);

    FilesystemReferenceCompatibilityInspector inspector;
    const auto reference = local_file("ref", "fasta", fasta_path);

    check(
        inspector.inspect(local_file("sam", "sam", sam_path), reference).status ==
            application::ReferenceCompatibilityStatus::verified,
        "SAM reference evidence not verified"
    );
    check(
        inspector.inspect(local_file("bad", "sam", bad_sam_path), reference).status ==
            application::ReferenceCompatibilityStatus::mismatch,
        "SAM reference mismatch not detected"
    );
    check(
        inspector.inspect(local_file("vcf", "vcf", vcf_path), reference).status ==
            application::ReferenceCompatibilityStatus::verified,
        "VCF reference evidence not verified"
    );
    check(
        inspector.inspect(local_file("bare", "vcf", bare_vcf_path), reference).status ==
            application::ReferenceCompatibilityStatus::not_declared_by_format,
        "VCF without contig metadata was treated as verified"
    );
    check(
        inspector.inspect(local_file("bam", "bam", bam_path), reference).status ==
            application::ReferenceCompatibilityStatus::verified,
        "BAM reference dictionary not verified"
    );

    const auto fastq_path = temp.root / "reads.fastq";
    write_text(fastq_path, "@r1\nACGT\n+\nIIII\n");
    check(
        inspector.inspect(local_file("fq", "fastq", fastq_path), reference).status ==
            application::ReferenceCompatibilityStatus::not_declared_by_format,
        "FASTQ reference status must remain unverified"
    );
}

void load_v11(SqliteConnection& connection) {
    std::ifstream input{
        std::filesystem::path{BIOCORE_SOURCE_ROOT} /
        "tests/fixtures/project-schema-v11.sql"
    };
    check(input.good(), "v11 fixture missing");
    const std::string sql{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}
    };
    connection.execute(sql);
    connection.execute(
        "INSERT INTO project_metadata(singleton,project_id,name,root_path,created_at_utc,"
        "updated_at_utc,research_description,research_organism,research_revision,"
        "research_updated_at_utc) VALUES(1,'p-001','Research','/local/project','t','t',"
        "'legacy research','Homo sapiens',2,'t2');"
        "INSERT INTO project_samples(project_id,sample_id,display_name,group_label) "
        "VALUES('p-001','001','Sample','case');"
        "INSERT INTO managed_files(id,display_name,storage_mode,original_path,managed_path,"
        "relative_project_path,file_type,size_bytes,checksum_algorithm,checksum_value,"
        "created_at_utc,updated_at_utc) VALUES"
        "('r1','r1.fastq','managed_copy','/o/r1','/m/r1','inputs/r1','fastq',4,"
        "'sha256','aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa','t','t'),"
        "('ref','ref.fa','managed_copy','/o/ref','/m/ref','inputs/ref','fasta',4,"
        "'sha256','bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb','t','t');"
    );
}

void migration_contract() {
    SqliteConnection connection{":memory:"};
    load_v11(connection);
    ProjectMigrationRunner migrations{connection};
    check(migrations.current_version() == 11, "fixture is not v11");
    migrations.apply_pending();
    ProjectDatabaseGuard{connection}.validate_current_schema();
    check(
        migrations.current_version() == 12 &&
        scalar_int64(connection, "SELECT COUNT(*) FROM project_samples;") == 1 &&
        scalar_int64(connection, "SELECT COUNT(*) FROM managed_files;") == 2 &&
        scalar_int64(connection, "SELECT COUNT(*) FROM project_sample_bindings;") == 0,
        "v11 to v12 migration did not preserve prior data"
    );
}

void rollback_contract() {
    SqliteConnection connection{":memory:"};
    load_v11(connection);
    connection.execute(
        "CREATE TRIGGER reject_v12 BEFORE INSERT ON schema_migrations "
        "WHEN NEW.version=12 BEGIN SELECT RAISE(ABORT,'injected v12 failure'); END;"
    );
    rejects<SqliteError>([&] { ProjectMigrationRunner{connection}.apply_pending(); });
    check(
        ProjectMigrationRunner{connection}.current_version() == 11 &&
        scalar_int64(
            connection,
            "SELECT COUNT(*) FROM sqlite_master "
            "WHERE type='table' AND name='project_sample_bindings';"
        ) == 0 &&
        scalar_int64(connection, "SELECT COUNT(*) FROM project_samples;") == 1,
        "failed v12 migration left partial state"
    );
    connection.execute("DROP TRIGGER reject_v12;");
    ProjectMigrationRunner{connection}.apply_pending();
    ProjectDatabaseGuard{connection}.validate_current_schema();
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        if (argc != 2) return EXIT_FAILURE;
        const std::string_view mode{argv[1]};
        if (mode == "domain") domain_contract();
        else if (mode == "roles") role_contract();
        else if (mode == "integrity") integrity_contract();
        else if (mode == "reference") reference_contract();
        else if (mode == "persistence") persistence_contract();
        else if (mode == "inspector") inspector_contract();
        else if (mode == "migration") migration_contract();
        else if (mode == "rollback") rollback_contract();
        else return EXIT_FAILURE;

        std::cout << mode << " PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
