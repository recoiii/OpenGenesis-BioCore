#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "biocore/application/artifact_presentation_service.hpp"
#include "biocore/application/artifact_presentation_service_error.hpp"
#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/output_artifact_cleanup_service.hpp"
#include "biocore/application/output_artifact_service.hpp"
#include "biocore/application/pipeline_preparation_service.hpp"
#include "biocore/application/worker_event_ingestion_session.hpp"
#include "biocore/application/worker_launch_request.hpp"
#include "biocore/domain/job.hpp"
#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/job_status.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/pipeline_definition.hpp"
#include "biocore/domain/storage_mode.hpp"
#include "biocore/infrastructure/filesystem_artifact_content_access.hpp"
#include "biocore/infrastructure/filesystem_output_artifact_inspector.hpp"
#include "biocore/infrastructure/filesystem_partial_output_cleaner.hpp"
#include "biocore/infrastructure/filesystem_plugin_registry.hpp"
#include "biocore/infrastructure/json_execution_plan_store.hpp"
#include "biocore/infrastructure/platform_worker_supervisor.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_job_repository.hpp"
#include "biocore/infrastructure/sqlite/sqlite_managed_file_repository.hpp"
#include "fasta_statistics.hpp"
#include "fastq_input_stream.hpp"
#include "fastq_statistics.hpp"
#include "variant_calling.hpp"
#include "vcf_qc.hpp"

namespace {
namespace fs = std::filesystem;
using namespace biocore;

[[nodiscard]] fs::path path_from_utf8(std::string_view value) {
    std::u8string utf8;
    utf8.reserve(value.size());
    for (const char character : value) {
        utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
    }
    return fs::path{utf8};
}

[[nodiscard]] std::string path_to_utf8(const fs::path& value) {
    const std::u8string utf8 = value.generic_u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

class TempProject final {
public:
    TempProject() {
        root_ = fs::temp_directory_path() /
                ("biocore-artifact-e2e-" + std::to_string(
                     std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root_ / ".biocore" / "runtime");
        fs::create_directories(root_ / "outputs");
        fs::create_directories(root_ / "inputs" / "file-1");
        std::ofstream{root_ / "inputs" / "file-1" / "source.txt"} << "alpha";
        root_ = fs::canonical(root_);
    }
    ~TempProject() { std::error_code error; fs::remove_all(root_, error); }
    [[nodiscard]] const fs::path& root() const noexcept { return root_; }
private:
    fs::path root_;
};

class FixedId final : public application::IIdGenerator {
public:
    explicit FixedId(std::string value) : value_{std::move(value)} {}
    std::string generate() override { return value_; }
private:
    std::string value_;
};

class SequenceIds final : public application::IIdGenerator {
public:
    explicit SequenceIds(std::vector<std::string> values) : values_{std::move(values)} {}
    std::string generate() override {
        if (index_ >= values_.size()) throw std::runtime_error("No artifact identifier remains");
        return values_[index_++];
    }
private:
    std::vector<std::string> values_;
    std::size_t index_{0U};
};

class Clock final : public application::IUtcClock {
public:
    std::string now_utc_iso8601() override { return "2026-08-07T08:15:00Z"; }
};

template <typename Function>
[[nodiscard]] bool rejects_invalid_input(Function&& function) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return true;
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

void append_le_i32(std::string& bytes, const std::int32_t value) {
    const auto raw = static_cast<std::uint32_t>(value);
    for (unsigned shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(static_cast<char>((raw >> shift) & 0xFFU));
    }
}

void append_le_u32(std::string& bytes, const std::uint32_t value) {
    for (unsigned shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(static_cast<char>((value >> shift) & 0xFFU));
    }
}

[[nodiscard]] bool malformed_bioinformatics_inputs_are_rejected() {
    const bool empty_fasta_rejected = rejects_invalid_input([] {
        std::istringstream input{">empty\n>valid\nACGT\n"};
        static_cast<void>(fasta_qc::analyze_fasta(input));
    });

    const bool truncated_fastq_rejected = rejects_invalid_input([] {
        std::istringstream input{"@read-1\nACGT\n+\nIII\n"};
        static_cast<void>(fastq_qc::analyze_fastq(input));
    });

    TempProject gzip_project;
    const fs::path corrupt_gzip = gzip_project.root() / "corrupt.fastq.gz";
    {
        std::ofstream output{corrupt_gzip, std::ios::binary};
        const unsigned char truncated_gzip[]{0x1FU, 0x8BU, 0x08U, 0x00U, 0x00U};
        output.write(
            reinterpret_cast<const char*>(truncated_gzip),
            static_cast<std::streamsize>(sizeof(truncated_gzip))
        );
    }
    const bool corrupt_gzip_rejected = rejects_invalid_input([&] {
        auto input = fastq_qc::open_fastq_input(corrupt_gzip);
        static_cast<void>(fastq_qc::analyze_fastq(*input));
    });

    const std::vector<variant_calling::ReferenceContig> reference{{"chr1", "ACGT"}};
    const variant_calling::VariantCallingOptions variant_options{};
    const bool malformed_sam_rejected = rejects_invalid_input([&] {
        std::istringstream input{
            "@SQ\tSN:chr1\tLN:4\n"
            "read-1\t0\tchr1\t1\t60\t4Z\t*\t0\t0\tACGT\tIIII\n"
        };
        static_cast<void>(variant_calling::call_variants_from_sam(input, reference, variant_options));
    });

    std::string malformed_bam{"BAM\1", 4U};
    append_le_i32(malformed_bam, 0);  // header text length
    append_le_i32(malformed_bam, 1);  // reference count
    append_le_i32(malformed_bam, 5);  // "chr1\\0"
    malformed_bam.append("chr1", 4U);
    malformed_bam.push_back('\0');
    append_le_i32(malformed_bam, 4);  // reference length
    append_le_i32(malformed_bam, 34);  // block size: 32-byte core + 2-byte read name
    append_le_i32(malformed_bam, 0);   // ref id
    append_le_i32(malformed_bam, 0);   // zero-based position
    append_le_u32(malformed_bam, (60U << 8U) | 2U);  // MAPQ=60, read-name length=2
    append_le_u32(malformed_bam, 0U);  // mapped, but n_cigar=0: invalid by contract
    append_le_i32(malformed_bam, 0);   // l_seq
    append_le_i32(malformed_bam, -1);  // next_refID
    append_le_i32(malformed_bam, -1);  // next_pos
    append_le_i32(malformed_bam, 0);   // tlen
    malformed_bam.push_back('r');
    malformed_bam.push_back('\0');
    const bool malformed_bam_rejected = rejects_invalid_input([&] {
        std::istringstream input{malformed_bam};
        static_cast<void>(variant_calling::call_variants_from_bam(input, reference, variant_options));
    });

    const bool zero_vcf_position_rejected = rejects_invalid_input([] {
        std::istringstream input{
            "##fileformat=VCFv4.3\n"
            "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\n"
            "chr1\t0\t.\tA\tG\t.\tPASS\tDP=3;AC=2;AF=0.666667;ABQ=30\n"
        };
        static_cast<void>(vcf_qc::process_vcf(input, vcf_qc::VcfQcOptions{}));
    });

    return empty_fasta_rejected && truncated_fastq_rejected && corrupt_gzip_rejected &&
           malformed_sam_rejected && malformed_bam_rejected && zero_vcf_position_rejected;
}

void ingest_events(
    application::WorkerEventIngestionSession& session,
    const std::vector<application::WorkerLifecycleEvent>& events
) {
    for (const auto& event : events) {
        static_cast<void>(session.ingest(event));
    }
}

[[nodiscard]] bool contract(const fs::path& worker, const fs::path& plugin_root) {
    TempProject project;
    infrastructure::sqlite::SqliteConnection connection{project.root() / ".biocore" / "project.sqlite"};
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    infrastructure::sqlite::SqliteManagedFileRepository files{connection};
    infrastructure::sqlite::SqliteJobRepository jobs{connection};

    const auto input_path = project.root() / "inputs" / "file-1" / "source.txt";
    if (!files.add(domain::ManagedFile{
            "file-1", "source.txt", domain::StorageMode::managed_copy,
            std::string{"/original/source.txt"}, path_to_utf8(input_path),
            std::string{"inputs/file-1/source.txt"}, "txt", 5, std::nullopt,
            std::nullopt, std::nullopt, "2026-08-07T08:00:00Z", "2026-08-07T08:00:00Z"
        })) return false;

    const domain::Job preparing{
        "job-artifact", std::nullopt, std::string{"org.biocore.demo.io"},
        std::string{"1.0.0"}, domain::JobStatus::preparing, domain::JobPriority::normal,
        0.0, std::nullopt, "2026-08-07T08:00:00Z", "2026-08-07T08:00:01Z",
        std::string{"2026-08-07T08:00:01Z"}, std::nullopt, 1
    };
    if (!jobs.add(preparing)) return false;

    infrastructure::FilesystemPluginRegistry registry{{fs::canonical(plugin_root)}};
    const auto refresh = registry.refresh();
    if (refresh.loaded_plugins != 8U || !refresh.rejected.empty()) return false;
    infrastructure::JsonExecutionPlanStore plan_store{project.root()};
    application::PipelinePreparationService preparation{plan_store, registry, files};
    const domain::PipelineDefinition definition{
        1U, "org.biocore.demo.artifacts", "Artifacts", "1.0.0",
        {domain::PipelineStep{"copy", "org.biocore.demo.copy", {}, 1.0}}
    };
    application::PipelineRunBindings bindings{{application::PipelineStepBindings{
        .step_id = "copy",
        .parameters = {
            {"label", domain::PluginParameterValue{std::string{"beta"}}},
            {"repeat", domain::PluginParameterValue{std::int64_t{2}}},
        },
        .inputs = {{"source", application::ManagedFileInputSource{"file-1"}}},
    }}};
    const auto prepared_plan = preparation.prepare(definition, "job-artifact", 1, bindings);

    infrastructure::PlatformWorkerSupervisor supervisor{fs::canonical(worker), project.root()};
    supervisor.launch(application::WorkerLaunchRequest{
        .job_id = "job-artifact", .analysis_id = std::nullopt,
        .pipeline_id = std::string{"org.biocore.demo.artifacts"},
        .pipeline_version = std::string{"1.0.0"},
        .priority = domain::JobPriority::normal, .job_revision = 1,
        .execution_plan_path = prepared_plan.snapshot_path,
    });

    FixedId job_ids{"unused-job-id"};
    FixedId artifact_ids{"artifact-1"};
    Clock clock;
    application::JobService job_service{jobs, job_ids, clock};
    infrastructure::FilesystemOutputArtifactInspector inspector{project.root()};
    application::OutputArtifactService artifact_service{files, inspector, artifact_ids, clock};
    application::WorkerEventIngestionSession session{
        job_service, "job-artifact", 1, &artifact_service
    };

    bool finalized = false;
    for (int attempt = 0; attempt < 500 && !finalized; ++attempt) {
        for (auto& output : supervisor.poll_output()) {
            if (output.job_id != "job-artifact" || !output.protocol_issues.empty()) return false;
            ingest_events(session, output.events);
        }
        for (auto& exit : supervisor.reap_exited()) {
            if (exit.job_id != "job-artifact" || !exit.protocol_issues.empty()) return false;
            ingest_events(session, exit.events);
            static_cast<void>(session.finalize_process_exit(exit.exit_code));
            finalized = true;
        }
        if (!finalized) std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    if (!finalized) return false;

    const auto job = jobs.find_by_id("job-artifact");
    const auto artifact = files.find_generated_output("job-artifact", "copy", "result");
    const auto listed = files.list_generated_outputs("job-artifact");
    if (!job.has_value() || job->status() != domain::JobStatus::completed ||
        !artifact.has_value() || listed.size() != 1U ||
        artifact->file.storage_mode() != domain::StorageMode::generated_output ||
        artifact->file.id() != "artifact-1" || artifact->provenance.module_id != "org.biocore.demo.copy" ||
        artifact->file.checksum_algorithm() != std::optional<std::string>{"sha256"} ||
        artifact->file.checksum_value() != std::optional<std::string>{
            "f0080ce7d01a4f64cbfa8f546ef13df20489ad32b4986ab7d886e132934ac08d"}) {
        return false;
    }
    const auto result_path = project.root() / "outputs" / "job-artifact--copy--result.out";
    std::ifstream input{result_path, std::ios::binary};
    std::string content{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (content != "alpha\nbeta\nbeta" || artifact->file.size_bytes() != 15) return false;

    infrastructure::FilesystemArtifactContentAccess content_access{project.root()};
    application::ArtifactPresentationService presentation{files, jobs, content_access, clock};
    const auto download = presentation.prepare_download("job-artifact", "copy", "result");
    if (download.verified_sha256 != *artifact->file.checksum_value() ||
        download.metadata.size_bytes != 15) {
        return false;
    }

    {
        std::ofstream tamper{result_path, std::ios::binary | std::ios::trunc};
        tamper << "omega\nbeta\nbeta";
    }
    try {
        static_cast<void>(presentation.prepare_download("job-artifact", "copy", "result"));
    } catch (const application::ArtifactPresentationError& error) {
        return error.code() == application::ArtifactPresentationErrorCode::checksum_mismatch;
    }
    return false;
}


[[nodiscard]] bool failed_plugin_does_not_register_artifacts(
    const fs::path& worker, const fs::path& plugin_root
) {
    TempProject project;
    infrastructure::sqlite::SqliteConnection connection{project.root() / ".biocore" / "project.sqlite"};
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    infrastructure::sqlite::SqliteManagedFileRepository files{connection};
    infrastructure::sqlite::SqliteJobRepository jobs{connection};

    const domain::Job preparing{
        "job-artifact-fail", std::nullopt, std::string{"org.biocore.demo.failure"},
        std::string{"1.0.0"}, domain::JobStatus::preparing, domain::JobPriority::normal,
        0.0, std::nullopt, "2026-08-07T08:00:00Z", "2026-08-07T08:00:01Z",
        std::string{"2026-08-07T08:00:01Z"}, std::nullopt, 1
    };
    if (!jobs.add(preparing)) return false;

    infrastructure::FilesystemPluginRegistry registry{{fs::canonical(plugin_root)}};
    const auto refresh = registry.refresh();
    if (refresh.loaded_plugins != 8U || !refresh.rejected.empty()) return false;
    infrastructure::JsonExecutionPlanStore plan_store{project.root()};
    application::PipelinePreparationService preparation{plan_store, registry, files};
    const domain::PipelineDefinition definition{
        1U, "org.biocore.demo.failure", "Failure artifacts", "1.0.0",
        {domain::PipelineStep{"bad", "org.biocore.demo.fail", {}, 1.0}}
    };
    application::PipelineRunBindings bindings{{application::PipelineStepBindings{
        .step_id = "bad", .parameters = {}, .inputs = {},
    }}};
    const auto prepared_plan = preparation.prepare(definition, "job-artifact-fail", 1, bindings);

    infrastructure::PlatformWorkerSupervisor supervisor{fs::canonical(worker), project.root()};
    supervisor.launch(application::WorkerLaunchRequest{
        .job_id = "job-artifact-fail", .analysis_id = std::nullopt,
        .pipeline_id = std::string{"org.biocore.demo.failure"},
        .pipeline_version = std::string{"1.0.0"},
        .priority = domain::JobPriority::normal, .job_revision = 1,
        .execution_plan_path = prepared_plan.snapshot_path,
    });

    FixedId job_ids{"unused-job-id"};
    FixedId artifact_ids{"must-not-be-used"};
    Clock clock;
    application::JobService job_service{jobs, job_ids, clock};
    infrastructure::FilesystemOutputArtifactInspector inspector{project.root()};
    application::OutputArtifactService artifact_service{files, inspector, artifact_ids, clock};
    infrastructure::FilesystemPartialOutputCleaner partial_output_cleaner{project.root()};
    application::OutputArtifactCleanupService cleanup_service{files, partial_output_cleaner};
    application::WorkerEventIngestionSession session{
        job_service, "job-artifact-fail", 1, &artifact_service, &cleanup_service
    };

    bool finalized = false;
    for (int attempt = 0; attempt < 500 && !finalized; ++attempt) {
        for (auto& output : supervisor.poll_output()) {
            if (output.job_id != "job-artifact-fail" || !output.protocol_issues.empty()) return false;
            ingest_events(session, output.events);
        }
        for (auto& exit : supervisor.reap_exited()) {
            if (exit.job_id != "job-artifact-fail" || !exit.protocol_issues.empty()) return false;
            ingest_events(session, exit.events);
            static_cast<void>(session.finalize_process_exit(exit.exit_code));
            finalized = true;
        }
        if (!finalized) std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    if (!finalized) return false;

    const auto job = jobs.find_by_id("job-artifact-fail");
    const auto artifacts = files.list_generated_outputs("job-artifact-fail");
    const auto partial_path = project.root() / "outputs" / "job-artifact-fail--bad--partial.out";
    const auto quarantine_path = project.root() / ".biocore" / "quarantine" / "outputs" /
                                 "job-artifact-fail" /
                                 "job-artifact-fail--bad--partial.out.partial";
    std::ifstream input{quarantine_path, std::ios::binary};
    std::string content{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    return job.has_value() && job->status() == domain::JobStatus::failed && artifacts.empty() &&
           !fs::exists(partial_path) && fs::is_regular_file(quarantine_path) && content == "partial";
}

[[nodiscard]] bool multi_output_step_registers_one_complete_batch(
    const fs::path& worker, const fs::path& plugin_root
) {
    TempProject project;
    infrastructure::sqlite::SqliteConnection connection{project.root() / ".biocore" / "project.sqlite"};
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    infrastructure::sqlite::SqliteManagedFileRepository files{connection};
    infrastructure::sqlite::SqliteJobRepository jobs{connection};

    const domain::Job preparing{
        "job-artifact-multi", std::nullopt, std::string{"org.biocore.demo.multi-pipeline"},
        std::string{"1.0.0"}, domain::JobStatus::preparing, domain::JobPriority::normal,
        0.0, std::nullopt, "2026-08-07T08:00:00Z", "2026-08-07T08:00:01Z",
        std::string{"2026-08-07T08:00:01Z"}, std::nullopt, 1
    };
    if (!jobs.add(preparing)) return false;

    infrastructure::FilesystemPluginRegistry registry{{fs::canonical(plugin_root)}};
    const auto refresh = registry.refresh();
    if (refresh.loaded_plugins != 8U || !refresh.rejected.empty()) return false;
    infrastructure::JsonExecutionPlanStore plan_store{project.root()};
    application::PipelinePreparationService preparation{plan_store, registry, files};
    const domain::PipelineDefinition definition{
        1U, "org.biocore.demo.multi-pipeline", "Multi artifacts", "1.0.0",
        {domain::PipelineStep{"multi", "org.biocore.demo.multi", {}, 1.0}}
    };
    application::PipelineRunBindings bindings{{application::PipelineStepBindings{
        .step_id = "multi", .parameters = {}, .inputs = {},
    }}};
    const auto prepared_plan = preparation.prepare(
        definition, "job-artifact-multi", 1, bindings
    );

    infrastructure::PlatformWorkerSupervisor supervisor{fs::canonical(worker), project.root()};
    supervisor.launch(application::WorkerLaunchRequest{
        .job_id = "job-artifact-multi", .analysis_id = std::nullopt,
        .pipeline_id = std::string{"org.biocore.demo.multi-pipeline"},
        .pipeline_version = std::string{"1.0.0"},
        .priority = domain::JobPriority::normal, .job_revision = 1,
        .execution_plan_path = prepared_plan.snapshot_path,
    });

    FixedId job_ids{"unused-job-id"};
    SequenceIds artifact_ids{{"artifact-left", "artifact-right"}};
    Clock clock;
    application::JobService job_service{jobs, job_ids, clock};
    infrastructure::FilesystemOutputArtifactInspector inspector{project.root()};
    application::OutputArtifactService artifact_service{files, inspector, artifact_ids, clock};
    application::WorkerEventIngestionSession session{
        job_service, "job-artifact-multi", 1, &artifact_service
    };

    bool finalized = false;
    for (int attempt = 0; attempt < 500 && !finalized; ++attempt) {
        for (auto& output : supervisor.poll_output()) {
            if (output.job_id != "job-artifact-multi" || !output.protocol_issues.empty()) return false;
            ingest_events(session, output.events);
        }
        for (auto& exit : supervisor.reap_exited()) {
            if (exit.job_id != "job-artifact-multi" || !exit.protocol_issues.empty()) return false;
            ingest_events(session, exit.events);
            static_cast<void>(session.finalize_process_exit(exit.exit_code));
            finalized = true;
        }
        if (!finalized) std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    if (!finalized) return false;

    const auto job = jobs.find_by_id("job-artifact-multi");
    const auto left = files.find_generated_output("job-artifact-multi", "multi", "left");
    const auto right = files.find_generated_output("job-artifact-multi", "multi", "right");
    const auto listed = files.list_generated_outputs("job-artifact-multi");
    if (!job.has_value() || job->status() != domain::JobStatus::completed ||
        !left.has_value() || !right.has_value() || listed.size() != 2U ||
        left->file.id() != "artifact-left" || right->file.id() != "artifact-right") {
        return false;
    }

    std::ifstream left_input{project.root() / "outputs" / "job-artifact-multi--multi--left.out", std::ios::binary};
    std::ifstream right_input{project.root() / "outputs" / "job-artifact-multi--multi--right.out", std::ios::binary};
    const std::string left_content{
        std::istreambuf_iterator<char>{left_input}, std::istreambuf_iterator<char>{}
    };
    const std::string right_content{
        std::istreambuf_iterator<char>{right_input}, std::istreambuf_iterator<char>{}
    };
    return left_content == "left" && right_content == "right";
}


[[nodiscard]] bool fasta_qc_generates_real_statistics(
    const fs::path& worker, const fs::path& plugin_root
) {
    TempProject project;
    const auto fasta_directory = project.root() / "inputs" / "fasta-1";
    fs::create_directories(fasta_directory);
    const auto fasta_path = fasta_directory / "source.fa";
    {
        std::ofstream output{fasta_path, std::ios::binary | std::ios::trunc};
        output
            << ">seq1\n"
            << "ACGT\n"
            << ">seq2\n"
            << "GGGGNN\n"
            << ">seq3\n"
            << "AAAACCCCGG\n";
        if (!output) return false;
    }

    infrastructure::sqlite::SqliteConnection connection{
        project.root() / ".biocore" / "project.sqlite"
    };
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    infrastructure::sqlite::SqliteManagedFileRepository files{connection};
    infrastructure::sqlite::SqliteJobRepository jobs{connection};

    const auto fasta_size = fs::file_size(fasta_path);
    if (!files.add(domain::ManagedFile{
            "fasta-1", "source.fa", domain::StorageMode::managed_copy,
            std::string{"/original/source.fa"}, path_to_utf8(fasta_path),
            std::string{"inputs/fasta-1/source.fa"}, "fasta",
            static_cast<std::int64_t>(fasta_size), std::nullopt,
            std::nullopt, std::nullopt,
            "2026-08-07T08:00:00Z", "2026-08-07T08:00:00Z"
        })) {
        return false;
    }

    const domain::Job preparing{
        "job-fasta-qc", std::nullopt, std::string{"org.biocore.fastaqc.summary"},
        std::string{"0.1.0"}, domain::JobStatus::preparing,
        domain::JobPriority::normal, 0.0, std::nullopt,
        "2026-08-07T08:00:00Z", "2026-08-07T08:00:01Z",
        std::string{"2026-08-07T08:00:01Z"}, std::nullopt, 1
    };
    if (!jobs.add(preparing)) return false;

    infrastructure::FilesystemPluginRegistry registry{{fs::canonical(plugin_root)}};
    const auto refresh = registry.refresh();
    const auto fasta_module = registry.find_module("org.biocore.fastaqc.stats");
    if (refresh.loaded_plugins != 8U || refresh.loaded_modules != 17U ||
        !refresh.rejected.empty() || !fasta_module.has_value()) {
        return false;
    }

    infrastructure::JsonExecutionPlanStore plan_store{project.root()};
    application::PipelinePreparationService preparation{plan_store, registry, files};
    const domain::PipelineDefinition definition{
        1U, "org.biocore.fastaqc.summary", "FASTA DNA QC & Statistics", "0.1.0",
        {domain::PipelineStep{"stats", "org.biocore.fastaqc.stats", {}, 1.0}}
    };
    application::PipelineRunBindings bindings{{
        application::PipelineStepBindings{
            .step_id = "stats",
            .parameters = {},
            .inputs = {{"source", application::ManagedFileInputSource{"fasta-1"}}},
        }
    }};
    const auto prepared_plan =
        preparation.prepare(definition, "job-fasta-qc", 1, bindings);

    infrastructure::PlatformWorkerSupervisor supervisor{
        fs::canonical(worker), project.root()
    };
    supervisor.launch(application::WorkerLaunchRequest{
        .job_id = "job-fasta-qc",
        .analysis_id = std::nullopt,
        .pipeline_id = std::string{"org.biocore.fastaqc.summary"},
        .pipeline_version = std::string{"0.1.0"},
        .priority = domain::JobPriority::normal,
        .job_revision = 1,
        .execution_plan_path = prepared_plan.snapshot_path,
    });

    FixedId job_ids{"unused-job-id"};
    SequenceIds artifact_ids{{
        "artifact-fasta-summary",
        "artifact-fasta-table"
    }};
    Clock clock;
    application::JobService job_service{jobs, job_ids, clock};
    infrastructure::FilesystemOutputArtifactInspector inspector{project.root()};
    application::OutputArtifactService artifact_service{
        files, inspector, artifact_ids, clock
    };
    application::WorkerEventIngestionSession session{
        job_service, "job-fasta-qc", 1, &artifact_service
    };

    bool finalized = false;
    for (int attempt = 0; attempt < 500 && !finalized; ++attempt) {
        for (auto& output : supervisor.poll_output()) {
            if (output.job_id != "job-fasta-qc" ||
                !output.protocol_issues.empty()) {
                return false;
            }
            ingest_events(session, output.events);
        }
        for (auto& exit : supervisor.reap_exited()) {
            if (exit.job_id != "job-fasta-qc" ||
                !exit.protocol_issues.empty()) {
                return false;
            }
            ingest_events(session, exit.events);
            static_cast<void>(session.finalize_process_exit(exit.exit_code));
            finalized = true;
        }
        if (!finalized) {
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
    }
    if (!finalized) return false;

    const auto job = jobs.find_by_id("job-fasta-qc");
    const auto summary =
        files.find_generated_output("job-fasta-qc", "stats", "summary");
    const auto table =
        files.find_generated_output("job-fasta-qc", "stats", "table");
    const auto listed = files.list_generated_outputs("job-fasta-qc");

    if (!job.has_value() || job->status() != domain::JobStatus::completed ||
        !summary.has_value() || !table.has_value() || listed.size() != 2U ||
        summary->file.id() != "artifact-fasta-summary" ||
        table->file.id() != "artifact-fasta-table" ||
        summary->file.file_type() != "json" ||
        table->file.file_type() != "tsv" ||
        summary->provenance.module_id != "org.biocore.fastaqc.stats" ||
        table->provenance.module_id != "org.biocore.fastaqc.stats") {
        return false;
    }

    const auto summary_path =
        project.root() / "outputs" / "job-fasta-qc--stats--summary.out";
    const auto table_path =
        project.root() / "outputs" / "job-fasta-qc--stats--table.out";

    std::ifstream summary_input{summary_path, std::ios::binary};
    std::ifstream table_input{table_path, std::ios::binary};
    const std::string summary_content{
        std::istreambuf_iterator<char>{summary_input},
        std::istreambuf_iterator<char>{}
    };
    const std::string table_content{
        std::istreambuf_iterator<char>{table_input},
        std::istreambuf_iterator<char>{}
    };

    return
        summary_content.find("\"sequenceCount\": 3") != std::string::npos &&
        summary_content.find("\"totalBases\": 20") != std::string::npos &&
        summary_content.find("\"n50\": 10") != std::string::npos &&
        summary_content.find("\"gcPercentCanonical\": 66.666667") !=
            std::string::npos &&
        table_content.find("sequence_count\t3\n") != std::string::npos &&
        table_content.find("total_bases\t20\n") != std::string::npos &&
        table_content.find("n50\t10\n") != std::string::npos &&
        table_content.find("gc_percent_canonical\t66.666667\n") !=
            std::string::npos;
}


[[nodiscard]] bool fastq_qc_generates_real_statistics(
    const fs::path& worker, const fs::path& plugin_root, const bool gzip_encoded
) {
    TempProject project;
    const auto fastq_directory = project.root() / "inputs" / "fastq-1";
    fs::create_directories(fastq_directory);
    const auto fastq_path = fastq_directory /
        (gzip_encoded ? "reads.fastq.gz" : "reads.fastq");
    {
        std::ofstream output{fastq_path, std::ios::binary | std::ios::trunc};
        if (gzip_encoded) {
            static constexpr unsigned char gzip_fastq[] = {
                0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff,
                0x73, 0x28, 0x32, 0xe4, 0x72, 0x74, 0x76, 0x0f, 0xe1, 0xd2,
                0xe6, 0xf2, 0x04, 0x02, 0x2e, 0x87, 0x22, 0x23, 0x85, 0x94,
                0xd4, 0xe2, 0xe4, 0xa2, 0xcc, 0x82, 0x92, 0xcc, 0xfc, 0x3c,
                0x2e, 0x77, 0x77, 0x3f, 0x3f, 0x47, 0x47, 0x2e, 0xed, 0x22,
                0x23, 0x2e, 0x53, 0x30, 0x00, 0xaa, 0x30, 0xe6, 0x0a, 0x8a,
                0x74, 0x74, 0x06, 0xea, 0x50, 0x04, 0x02, 0x2e, 0x00, 0xe5,
                0x42, 0x5f, 0xe1, 0x42, 0x00, 0x00, 0x00
            };
            output.write(
                reinterpret_cast<const char*>(gzip_fastq),
                static_cast<std::streamsize>(sizeof(gzip_fastq))
            );
        } else {
            output
                << "@r1\n"
                << "ACGT\n"
                << "+\n"
                << "IIII\n"
                << "@r2 description\n"
                << "GGNNAA\n"
                << "+r2\n"
                << "555555\n"
                << "@r3\n"
                << "RYAC\n"
                << "+\n"
                << "!!!!\n";
        }
        if (!output) return false;
    }

    infrastructure::sqlite::SqliteConnection connection{
        project.root() / ".biocore" / "project.sqlite"
    };
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    infrastructure::sqlite::SqliteManagedFileRepository files{connection};
    infrastructure::sqlite::SqliteJobRepository jobs{connection};

    const auto fastq_size = fs::file_size(fastq_path);
    if (!files.add(domain::ManagedFile{
            "fastq-1", gzip_encoded ? "reads.fastq.gz" : "reads.fastq",
            domain::StorageMode::managed_copy,
            gzip_encoded ? std::string{"/original/reads.fastq.gz"}
                         : std::string{"/original/reads.fastq"},
            path_to_utf8(fastq_path),
            gzip_encoded ? std::string{"inputs/fastq-1/reads.fastq.gz"}
                         : std::string{"inputs/fastq-1/reads.fastq"},
            "fastq",
            static_cast<std::int64_t>(fastq_size), std::nullopt,
            std::nullopt, std::nullopt,
            "2026-08-15T08:00:00Z", "2026-08-15T08:00:00Z"
        })) {
        return false;
    }

    const domain::Job preparing{
        "job-fastq-qc", std::nullopt, std::string{"org.biocore.fastqqc.summary"},
        std::string{"0.1.0"}, domain::JobStatus::preparing,
        domain::JobPriority::normal, 0.0, std::nullopt,
        "2026-08-15T08:00:00Z", "2026-08-15T08:00:01Z",
        std::string{"2026-08-15T08:00:01Z"}, std::nullopt, 1
    };
    if (!jobs.add(preparing)) return false;

    infrastructure::FilesystemPluginRegistry registry{{fs::canonical(plugin_root)}};
    const auto refresh = registry.refresh();
    const auto fastq_module = registry.find_module("org.biocore.fastqqc.stats");
    if (refresh.loaded_plugins != 8U || refresh.loaded_modules != 17U ||
        !refresh.rejected.empty() || !fastq_module.has_value()) {
        return false;
    }

    infrastructure::JsonExecutionPlanStore plan_store{project.root()};
    application::PipelinePreparationService preparation{plan_store, registry, files};
    const domain::PipelineDefinition definition{
        1U, "org.biocore.fastqqc.summary", "FASTQ DNA QC & Quality Statistics", "0.1.0",
        {domain::PipelineStep{"stats", "org.biocore.fastqqc.stats", {}, 1.0}}
    };
    application::PipelineRunBindings bindings{{
        application::PipelineStepBindings{
            .step_id = "stats",
            .parameters = {},
            .inputs = {{"source", application::ManagedFileInputSource{"fastq-1"}}},
        }
    }};
    const auto prepared_plan =
        preparation.prepare(definition, "job-fastq-qc", 1, bindings);

    infrastructure::PlatformWorkerSupervisor supervisor{
        fs::canonical(worker), project.root()
    };
    supervisor.launch(application::WorkerLaunchRequest{
        .job_id = "job-fastq-qc",
        .analysis_id = std::nullopt,
        .pipeline_id = std::string{"org.biocore.fastqqc.summary"},
        .pipeline_version = std::string{"0.1.0"},
        .priority = domain::JobPriority::normal,
        .job_revision = 1,
        .execution_plan_path = prepared_plan.snapshot_path,
    });

    FixedId job_ids{"unused-job-id"};
    SequenceIds artifact_ids{{
        "artifact-fastq-summary",
        "artifact-fastq-table"
    }};
    Clock clock;
    application::JobService job_service{jobs, job_ids, clock};
    infrastructure::FilesystemOutputArtifactInspector inspector{project.root()};
    application::OutputArtifactService artifact_service{
        files, inspector, artifact_ids, clock
    };
    application::WorkerEventIngestionSession session{
        job_service, "job-fastq-qc", 1, &artifact_service
    };

    bool finalized = false;
    for (int attempt = 0; attempt < 500 && !finalized; ++attempt) {
        for (auto& output : supervisor.poll_output()) {
            if (output.job_id != "job-fastq-qc" ||
                !output.protocol_issues.empty()) {
                return false;
            }
            ingest_events(session, output.events);
        }
        for (auto& exit : supervisor.reap_exited()) {
            if (exit.job_id != "job-fastq-qc" ||
                !exit.protocol_issues.empty()) {
                return false;
            }
            ingest_events(session, exit.events);
            static_cast<void>(session.finalize_process_exit(exit.exit_code));
            finalized = true;
        }
        if (!finalized) {
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
    }
    if (!finalized) return false;

    const auto job = jobs.find_by_id("job-fastq-qc");
    const auto summary =
        files.find_generated_output("job-fastq-qc", "stats", "summary");
    const auto table =
        files.find_generated_output("job-fastq-qc", "stats", "table");
    const auto listed = files.list_generated_outputs("job-fastq-qc");

    if (!job.has_value() || job->status() != domain::JobStatus::completed ||
        !summary.has_value() || !table.has_value() || listed.size() != 2U ||
        summary->file.id() != "artifact-fastq-summary" ||
        table->file.id() != "artifact-fastq-table" ||
        summary->file.file_type() != "json" ||
        table->file.file_type() != "tsv" ||
        summary->provenance.module_id != "org.biocore.fastqqc.stats" ||
        table->provenance.module_id != "org.biocore.fastqqc.stats") {
        return false;
    }

    const auto summary_path =
        project.root() / "outputs" / "job-fastq-qc--stats--summary.out";
    const auto table_path =
        project.root() / "outputs" / "job-fastq-qc--stats--table.out";

    std::ifstream summary_input{summary_path, std::ios::binary};
    std::ifstream table_input{table_path, std::ios::binary};
    const std::string summary_content{
        std::istreambuf_iterator<char>{summary_input},
        std::istreambuf_iterator<char>{}
    };
    const std::string table_content{
        std::istreambuf_iterator<char>{table_input},
        std::istreambuf_iterator<char>{}
    };

    return
        summary_content.find("\"readCount\": 3") != std::string::npos &&
        summary_content.find("\"totalBases\": 14") != std::string::npos &&
        summary_content.find("\"averagePhred\": 20.000000") != std::string::npos &&
        summary_content.find("\"q30Percent\": 28.571429") != std::string::npos &&
        table_content.find("read_count\t3\n") != std::string::npos &&
        table_content.find("total_bases\t14\n") != std::string::npos &&
        table_content.find("q20_bases\t10\n") != std::string::npos &&
        table_content.find("q30_percent\t28.571429\n") != std::string::npos;
}


[[nodiscard]] bool paired_fastq_qc_generates_real_statistics(
    const fs::path& worker, const fs::path& plugin_root
) {
    TempProject project;
    const auto read1_directory = project.root() / "inputs" / "paired-r1";
    const auto read2_directory = project.root() / "inputs" / "paired-r2";
    fs::create_directories(read1_directory);
    fs::create_directories(read2_directory);
    const auto read1_path = read1_directory / "sample_R1.fastq";
    const auto read2_path = read2_directory / "sample_R2.fastq.gz";
    {
        std::ofstream output{read1_path, std::ios::binary | std::ios::trunc};
        output
            << "@pairA/1\n"
            << "ACGT\n"
            << "+\n"
            << "IIII\n"
            << "@pairB 1:N:0:1\n"
            << "GGNN\n"
            << "+\n"
            << "HHHH\n";
        if (!output) return false;
    }
    {
        static constexpr unsigned char gzip_read2[] = {
            0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff,
            0x73, 0x28, 0x48, 0xcc, 0x2c, 0x72, 0xd4, 0x37, 0xe2, 0x0a,
            0x71, 0x77, 0x76, 0xe4, 0xd2, 0xe6, 0xf2, 0x04, 0x02, 0x2e,
            0x07, 0x90, 0xa0, 0x93, 0x82, 0x91, 0x95, 0x9f, 0x95, 0x81,
            0x95, 0x21, 0x97, 0xb3, 0xb3, 0x23, 0x48, 0xca, 0x0b, 0x08,
            0xb8, 0x00, 0xc4, 0x8b, 0x9c, 0x7b, 0x30, 0x00, 0x00, 0x00
        };
        std::ofstream output{read2_path, std::ios::binary | std::ios::trunc};
        output.write(
            reinterpret_cast<const char*>(gzip_read2),
            static_cast<std::streamsize>(sizeof(gzip_read2))
        );
        if (!output) return false;
    }

    infrastructure::sqlite::SqliteConnection connection{
        project.root() / ".biocore" / "project.sqlite"
    };
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    infrastructure::sqlite::SqliteManagedFileRepository files{connection};
    infrastructure::sqlite::SqliteJobRepository jobs{connection};

    const auto add_fastq = [&](const std::string& id, const std::string& name,
                               const fs::path& path, const std::string& relative) {
        return files.add(domain::ManagedFile{
            id, name, domain::StorageMode::managed_copy,
            std::string{"/original/"} + name, path_to_utf8(path), relative, "fastq",
            static_cast<std::int64_t>(fs::file_size(path)), std::nullopt,
            std::nullopt, std::nullopt,
            "2026-08-15T09:00:00Z", "2026-08-15T09:00:00Z"
        });
    };
    if (!add_fastq("paired-r1", "sample_R1.fastq", read1_path,
                   "inputs/paired-r1/sample_R1.fastq") ||
        !add_fastq("paired-r2", "sample_R2.fastq.gz", read2_path,
                   "inputs/paired-r2/sample_R2.fastq.gz")) {
        return false;
    }

    const domain::Job preparing{
        "job-fastq-paired-qc", std::nullopt,
        std::string{"org.biocore.fastqqc.paired_summary"}, std::string{"0.1.0"},
        domain::JobStatus::preparing, domain::JobPriority::normal, 0.0, std::nullopt,
        "2026-08-15T09:00:00Z", "2026-08-15T09:00:01Z",
        std::string{"2026-08-15T09:00:01Z"}, std::nullopt, 1
    };
    if (!jobs.add(preparing)) return false;

    infrastructure::FilesystemPluginRegistry registry{{fs::canonical(plugin_root)}};
    const auto refresh = registry.refresh();
    const auto paired_module = registry.find_module("org.biocore.fastqqc.paired-stats");
    if (refresh.loaded_plugins != 8U || refresh.loaded_modules != 17U ||
        !refresh.rejected.empty() || !paired_module.has_value()) {
        return false;
    }

    infrastructure::JsonExecutionPlanStore plan_store{project.root()};
    application::PipelinePreparationService preparation{plan_store, registry, files};
    const domain::PipelineDefinition definition{
        1U, "org.biocore.fastqqc.paired_summary",
        "Paired-end FASTQ DNA QC & Quality Statistics", "0.1.0",
        {domain::PipelineStep{"stats", "org.biocore.fastqqc.paired-stats", {}, 1.0}}
    };
    application::PipelineRunBindings bindings{{
        application::PipelineStepBindings{
            .step_id = "stats",
            .parameters = {},
            .inputs = {
                {"read1", application::ManagedFileInputSource{"paired-r1"}},
                {"read2", application::ManagedFileInputSource{"paired-r2"}},
            },
        }
    }};
    const auto prepared_plan = preparation.prepare(
        definition, "job-fastq-paired-qc", 1, bindings
    );

    infrastructure::PlatformWorkerSupervisor supervisor{
        fs::canonical(worker), project.root()
    };
    supervisor.launch(application::WorkerLaunchRequest{
        .job_id = "job-fastq-paired-qc",
        .analysis_id = std::nullopt,
        .pipeline_id = std::string{"org.biocore.fastqqc.paired_summary"},
        .pipeline_version = std::string{"0.1.0"},
        .priority = domain::JobPriority::normal,
        .job_revision = 1,
        .execution_plan_path = prepared_plan.snapshot_path,
    });

    FixedId job_ids{"unused-job-id"};
    SequenceIds artifact_ids{{
        "artifact-paired-fastq-summary",
        "artifact-paired-fastq-table"
    }};
    Clock clock;
    application::JobService job_service{jobs, job_ids, clock};
    infrastructure::FilesystemOutputArtifactInspector inspector{project.root()};
    application::OutputArtifactService artifact_service{
        files, inspector, artifact_ids, clock
    };
    application::WorkerEventIngestionSession session{
        job_service, "job-fastq-paired-qc", 1, &artifact_service
    };

    bool finalized = false;
    for (int attempt = 0; attempt < 500 && !finalized; ++attempt) {
        for (auto& output : supervisor.poll_output()) {
            if (output.job_id != "job-fastq-paired-qc" || !output.protocol_issues.empty()) {
                return false;
            }
            ingest_events(session, output.events);
        }
        for (auto& exit : supervisor.reap_exited()) {
            if (exit.job_id != "job-fastq-paired-qc" || !exit.protocol_issues.empty()) {
                return false;
            }
            ingest_events(session, exit.events);
            static_cast<void>(session.finalize_process_exit(exit.exit_code));
            finalized = true;
        }
        if (!finalized) std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    if (!finalized) return false;

    const auto job = jobs.find_by_id("job-fastq-paired-qc");
    const auto summary = files.find_generated_output(
        "job-fastq-paired-qc", "stats", "summary"
    );
    const auto table = files.find_generated_output(
        "job-fastq-paired-qc", "stats", "table"
    );
    const auto listed = files.list_generated_outputs("job-fastq-paired-qc");
    if (!job.has_value() || job->status() != domain::JobStatus::completed ||
        !summary.has_value() || !table.has_value() || listed.size() != 2U ||
        summary->provenance.module_id != "org.biocore.fastqqc.paired-stats" ||
        table->provenance.module_id != "org.biocore.fastqqc.paired-stats") {
        return false;
    }

    std::ifstream summary_input{
        project.root() / "outputs" / "job-fastq-paired-qc--stats--summary.out",
        std::ios::binary
    };
    std::ifstream table_input{
        project.root() / "outputs" / "job-fastq-paired-qc--stats--table.out",
        std::ios::binary
    };
    const std::string summary_content{
        std::istreambuf_iterator<char>{summary_input}, std::istreambuf_iterator<char>{}
    };
    const std::string table_content{
        std::istreambuf_iterator<char>{table_input}, std::istreambuf_iterator<char>{}
    };

    return
        summary_content.find("\"format\": \"FASTQ-PAIRED-DNA-PHRED33\"") != std::string::npos &&
        summary_content.find("\"pairCount\": 2") != std::string::npos &&
        summary_content.find("\"readCount\": 4") != std::string::npos &&
        summary_content.find("\"totalBases\": 16") != std::string::npos &&
        table_content.find("pair_count\t2\n") != std::string::npos &&
        table_content.find("read1_read_count\t2\n") != std::string::npos &&
        table_content.find("read2_read_count\t2\n") != std::string::npos &&
        table_content.find("combined_read_count\t4\n") != std::string::npos &&
        table_content.find("combined_total_bases\t16\n") != std::string::npos;
}


[[nodiscard]] bool paired_fastq_trimming_generates_real_fastq_outputs(
    const fs::path& worker, const fs::path& plugin_root
) {
    TempProject project;
    const auto r1dir = project.root() / "inputs" / "trim-r1";
    const auto r2dir = project.root() / "inputs" / "trim-r2";
    fs::create_directories(r1dir);
    fs::create_directories(r2dir);
    const auto r1path = r1dir / "trim_R1.fastq";
    const auto r2path = r2dir / "trim_R2.fastq";
    {
        std::ofstream out{r1path, std::ios::binary | std::ios::trunc};
        out << "@pairA/1\n"
            << "ACGTAGATCGGAAGAGCACACGTCTGAACTCCAGTCA\n+\n"
            << std::string(37U, 'I') << "\n"
            << "@pairB/1\nGGTTA\n+\nII!!!\n";
        if (!out) return false;
    }
    {
        std::ofstream out{r2path, std::ios::binary | std::ios::trunc};
        out << "@pairA/2\n"
            << "TGCAAGATCGGAAGAGCGTCGTGTAGGGAAAGAGTGT\n+\n"
            << std::string(37U, 'I') << "\n"
            << "@pairB/2\nCCAAT\n+\nIIIII\n";
        if (!out) return false;
    }

    infrastructure::sqlite::SqliteConnection connection{project.root() / ".biocore" / "project.sqlite"};
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    infrastructure::sqlite::SqliteManagedFileRepository files{connection};
    infrastructure::sqlite::SqliteJobRepository jobs{connection};
    const auto add_fastq = [&](const std::string& id, const std::string& name,
                               const fs::path& path, const std::string& relative) {
        return files.add(domain::ManagedFile{
            id, name, domain::StorageMode::managed_copy,
            std::string{"/original/"} + name, path_to_utf8(path), relative, "fastq",
            static_cast<std::int64_t>(fs::file_size(path)), std::nullopt,
            std::nullopt, std::nullopt,
            "2026-08-15T12:00:00Z", "2026-08-15T12:00:00Z"
        });
    };
    if (!add_fastq("trim-r1", "trim_R1.fastq", r1path, "inputs/trim-r1/trim_R1.fastq") ||
        !add_fastq("trim-r2", "trim_R2.fastq", r2path, "inputs/trim-r2/trim_R2.fastq")) return false;

    const domain::Job preparing{
        "job-fastq-paired-trim", std::nullopt,
        std::string{"org.biocore.fastqqc.trim_paired"}, std::string{"0.1.0"},
        domain::JobStatus::preparing, domain::JobPriority::normal, 0.0, std::nullopt,
        "2026-08-15T12:00:00Z", "2026-08-15T12:00:01Z",
        std::string{"2026-08-15T12:00:01Z"}, std::nullopt, 1
    };
    if (!jobs.add(preparing)) return false;

    infrastructure::FilesystemPluginRegistry registry{{fs::canonical(plugin_root)}};
    const auto refresh = registry.refresh();
    const auto trim_module = registry.find_module("org.biocore.fastqqc.trim-paired");
    if (refresh.loaded_plugins != 8U || refresh.loaded_modules != 17U ||
        !refresh.rejected.empty() || !trim_module.has_value()) return false;

    infrastructure::JsonExecutionPlanStore plan_store{project.root()};
    application::PipelinePreparationService preparation{plan_store, registry, files};
    const domain::PipelineDefinition definition{
        1U, "org.biocore.fastqqc.trim_paired",
        "Paired-end FASTQ Adapter & Quality Trimming", "0.1.0",
        {domain::PipelineStep{"trim", "org.biocore.fastqqc.trim-paired", {}, 1.0}}
    };
    application::PipelineRunBindings bindings{{
        application::PipelineStepBindings{
            .step_id = "trim",
            .parameters = {
                {"adapter-read1", domain::PluginParameterValue{std::string{"AGATCGGAAGAGCACACGTCTGAACTCCAGTCA"}}},
                {"adapter-read2", domain::PluginParameterValue{std::string{"AGATCGGAAGAGCGTCGTGTAGGGAAAGAGTGT"}}},
                {"min-adapter-overlap", domain::PluginParameterValue{std::int64_t{6}}},
                {"max-adapter-mismatches", domain::PluginParameterValue{std::int64_t{0}}},
                {"quality-threshold", domain::PluginParameterValue{std::int64_t{20}}},
                {"minimum-length", domain::PluginParameterValue{std::int64_t{3}}},
            },
            .inputs = {
                {"read1", application::ManagedFileInputSource{"trim-r1"}},
                {"read2", application::ManagedFileInputSource{"trim-r2"}},
            },
        }
    }};
    const auto prepared = preparation.prepare(definition, "job-fastq-paired-trim", 1, bindings);

    infrastructure::PlatformWorkerSupervisor supervisor{fs::canonical(worker), project.root()};
    supervisor.launch(application::WorkerLaunchRequest{
        .job_id = "job-fastq-paired-trim", .analysis_id = std::nullopt,
        .pipeline_id = std::string{"org.biocore.fastqqc.trim_paired"},
        .pipeline_version = std::string{"0.1.0"}, .priority = domain::JobPriority::normal,
        .job_revision = 1, .execution_plan_path = prepared.snapshot_path,
    });

    FixedId job_ids{"unused-job-id"};
    SequenceIds artifact_ids{{
        "artifact-trim-r1", "artifact-trim-r2", "artifact-trim-summary", "artifact-trim-table"
    }};
    Clock clock;
    application::JobService job_service{jobs, job_ids, clock};
    infrastructure::FilesystemOutputArtifactInspector inspector{project.root()};
    application::OutputArtifactService artifact_service{files, inspector, artifact_ids, clock};
    application::WorkerEventIngestionSession session{job_service, "job-fastq-paired-trim", 1, &artifact_service};

    bool finalized = false;
    for (int attempt = 0; attempt < 500 && !finalized; ++attempt) {
        for (auto& output : supervisor.poll_output()) {
            if (output.job_id != "job-fastq-paired-trim" || !output.protocol_issues.empty()) return false;
            ingest_events(session, output.events);
        }
        for (auto& exit : supervisor.reap_exited()) {
            if (exit.job_id != "job-fastq-paired-trim" || !exit.protocol_issues.empty()) return false;
            ingest_events(session, exit.events);
            static_cast<void>(session.finalize_process_exit(exit.exit_code));
            finalized = true;
        }
        if (!finalized) std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    if (!finalized) return false;

    const auto job = jobs.find_by_id("job-fastq-paired-trim");
    const auto out1 = files.find_generated_output("job-fastq-paired-trim", "trim", "trimmed_read1");
    const auto out2 = files.find_generated_output("job-fastq-paired-trim", "trim", "trimmed_read2");
    const auto summary = files.find_generated_output("job-fastq-paired-trim", "trim", "summary");
    const auto table = files.find_generated_output("job-fastq-paired-trim", "trim", "table");
    const auto listed = files.list_generated_outputs("job-fastq-paired-trim");
    if (!job.has_value() || job->status() != domain::JobStatus::completed ||
        !out1.has_value() || !out2.has_value() || !summary.has_value() || !table.has_value() ||
        listed.size() != 4U || out1->file.file_type() != "fastq" || out2->file.file_type() != "fastq" ||
        out1->provenance.module_id != "org.biocore.fastqqc.trim-paired" ||
        out2->provenance.module_id != "org.biocore.fastqqc.trim-paired") return false;

    const auto read_file = [&](const std::string& port) {
        std::ifstream in{project.root() / "outputs" / ("job-fastq-paired-trim--trim--" + port + ".out"), std::ios::binary};
        return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    };
    const std::string r1 = read_file("trimmed_read1");
    const std::string r2 = read_file("trimmed_read2");
    const std::string json = read_file("summary");
    const std::string tsv = read_file("table");
    return r1 == "@pairA/1\nACGT\n+\nIIII\n" &&
           r2 == "@pairA/2\nTGCA\n+\nIIII\n" &&
           json.find("\"format\": \"FASTQ-TRIM-PAIRED\"") != std::string::npos &&
           json.find("\"inputPairs\": 2") != std::string::npos &&
           json.find("\"keptPairs\": 1") != std::string::npos &&
           json.find("\"discardedPairs\": 1") != std::string::npos &&
           tsv.find("kept_pairs\t1\n") != std::string::npos &&
           tsv.find("discarded_pairs\t1\n") != std::string::npos;
}


[[nodiscard]] bool native_paired_alignment_generates_sam_artifact(
    const fs::path& worker, const fs::path& plugin_root
) {
    TempProject project;
    const auto refdir = project.root() / "inputs" / "align-ref";
    const auto r1dir = project.root() / "inputs" / "align-r1";
    const auto r2dir = project.root() / "inputs" / "align-r2";
    fs::create_directories(refdir);
    fs::create_directories(r1dir);
    fs::create_directories(r2dir);
    const auto refpath = refdir / "reference.fa";
    const auto r1path = r1dir / "reads_R1.fastq";
    const auto r2path = r2dir / "reads_R2.fastq";
    std::ofstream{refpath, std::ios::binary | std::ios::trunc}
        << ">chr1\nGATCTAGACCGTTAACGCGTACCTGA\n";
    std::ofstream{r1path, std::ios::binary | std::ios::trunc}
        << "@pairA/1\nTAGACC\n+\nIIIIII\n";
    std::ofstream{r2path, std::ios::binary | std::ios::trunc}
        << "@pairA/2\nTCAGGT\n+\nIIIIII\n";
    if (!fs::is_regular_file(refpath) || !fs::is_regular_file(r1path) ||
        !fs::is_regular_file(r2path)) return false;

    infrastructure::sqlite::SqliteConnection connection{
        project.root() / ".biocore" / "project.sqlite"
    };
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    infrastructure::sqlite::SqliteManagedFileRepository files{connection};
    infrastructure::sqlite::SqliteJobRepository jobs{connection};

    const auto add_input = [&](const std::string& id, const std::string& name,
                               const fs::path& path, const std::string& relative,
                               const std::string& type) {
        return files.add(domain::ManagedFile{
            id, name, domain::StorageMode::managed_copy,
            std::string{"/original/"} + name, path_to_utf8(path), relative, type,
            static_cast<std::int64_t>(fs::file_size(path)), std::nullopt,
            std::nullopt, std::nullopt,
            "2026-08-15T16:00:00Z", "2026-08-15T16:00:00Z"
        });
    };
    if (!add_input("align-ref", "reference.fa", refpath,
                   "inputs/align-ref/reference.fa", "fasta") ||
        !add_input("align-r1", "reads_R1.fastq", r1path,
                   "inputs/align-r1/reads_R1.fastq", "fastq") ||
        !add_input("align-r2", "reads_R2.fastq", r2path,
                   "inputs/align-r2/reads_R2.fastq", "fastq")) return false;

    const domain::Job preparing{
        "job-native-paired-align", std::nullopt,
        std::string{"org.biocore.align.paired"}, std::string{"0.1.0"},
        domain::JobStatus::preparing, domain::JobPriority::normal, 0.0, std::nullopt,
        "2026-08-15T16:00:00Z", "2026-08-15T16:00:01Z",
        std::string{"2026-08-15T16:00:01Z"}, std::nullopt, 1
    };
    if (!jobs.add(preparing)) return false;

    infrastructure::FilesystemPluginRegistry registry{{fs::canonical(plugin_root)}};
    const auto refresh = registry.refresh();
    const auto module = registry.find_module("org.biocore.align.paired");
    if (refresh.loaded_plugins != 8U || refresh.loaded_modules != 17U ||
        !refresh.rejected.empty() || !module.has_value()) return false;

    infrastructure::JsonExecutionPlanStore plan_store{project.root()};
    application::PipelinePreparationService preparation{plan_store, registry, files};
    const domain::PipelineDefinition definition{
        1U, "org.biocore.align.paired", "Paired-end Native Reference Alignment", "0.1.0",
        {domain::PipelineStep{"align", "org.biocore.align.paired", {}, 1.0}}
    };
    application::PipelineRunBindings bindings{{
        application::PipelineStepBindings{
            .step_id = "align",
            .parameters = {
                {"max-mismatches", domain::PluginParameterValue{std::int64_t{0}}},
            },
            .inputs = {
                {"reference", application::ManagedFileInputSource{"align-ref"}},
                {"read1", application::ManagedFileInputSource{"align-r1"}},
                {"read2", application::ManagedFileInputSource{"align-r2"}},
            },
        }
    }};
    const auto prepared = preparation.prepare(
        definition, "job-native-paired-align", 1, bindings
    );

    infrastructure::PlatformWorkerSupervisor supervisor{
        fs::canonical(worker), project.root()
    };
    supervisor.launch(application::WorkerLaunchRequest{
        .job_id = "job-native-paired-align", .analysis_id = std::nullopt,
        .pipeline_id = std::string{"org.biocore.align.paired"},
        .pipeline_version = std::string{"0.1.0"},
        .priority = domain::JobPriority::normal, .job_revision = 1,
        .execution_plan_path = prepared.snapshot_path,
    });

    FixedId job_ids{"unused-job-id"};
    SequenceIds artifact_ids{{
        "artifact-align-sam", "artifact-align-summary", "artifact-align-table"
    }};
    Clock clock;
    application::JobService job_service{jobs, job_ids, clock};
    infrastructure::FilesystemOutputArtifactInspector inspector{project.root()};
    application::OutputArtifactService artifact_service{files, inspector, artifact_ids, clock};
    application::WorkerEventIngestionSession session{
        job_service, "job-native-paired-align", 1, &artifact_service
    };

    bool finalized = false;
    for (int attempt = 0; attempt < 500 && !finalized; ++attempt) {
        for (auto& output : supervisor.poll_output()) {
            if (output.job_id != "job-native-paired-align" ||
                !output.protocol_issues.empty()) return false;
            ingest_events(session, output.events);
        }
        for (auto& exit : supervisor.reap_exited()) {
            if (exit.job_id != "job-native-paired-align" ||
                !exit.protocol_issues.empty()) return false;
            ingest_events(session, exit.events);
            static_cast<void>(session.finalize_process_exit(exit.exit_code));
            finalized = true;
        }
        if (!finalized) std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    if (!finalized) return false;

    const auto job = jobs.find_by_id("job-native-paired-align");
    const auto sam = files.find_generated_output(
        "job-native-paired-align", "align", "alignment"
    );
    const auto summary = files.find_generated_output(
        "job-native-paired-align", "align", "summary"
    );
    const auto table = files.find_generated_output(
        "job-native-paired-align", "align", "table"
    );
    const auto listed = files.list_generated_outputs("job-native-paired-align");
    if (!job.has_value() || job->status() != domain::JobStatus::completed ||
        !sam.has_value() || !summary.has_value() || !table.has_value() ||
        listed.size() != 3U || sam->file.file_type() != "sam" ||
        sam->provenance.module_id != "org.biocore.align.paired") return false;

    const auto read_file = [&](const std::string& port) {
        std::ifstream input{
            project.root() / "outputs" /
                ("job-native-paired-align--align--" + port + ".out"),
            std::ios::binary
        };
        return std::string{
            std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}
        };
    };
    const auto sam_text = read_file("alignment");
    const auto json = read_file("summary");
    const auto tsv = read_file("table");
    return sam_text.find("@SQ\tSN:chr1\tLN:26\n") != std::string::npos &&
           sam_text.find("pairA\t97\tchr1\t5\t60\t6M\t=\t21\t22") != std::string::npos &&
           sam_text.find("pairA\t145\tchr1\t21\t60\t6M\t=\t5\t-22") != std::string::npos &&
           json.find("\"algorithm\": \"native-ungapped-hamming-v1\"") != std::string::npos &&
           json.find("\"totalPairs\": 1") != std::string::npos &&
           json.find("\"bothMappedPairs\": 1") != std::string::npos &&
           tsv.find("both_mapped_pairs\t1\n") != std::string::npos;
}


[[nodiscard]] bool alignment_qc_generates_primary_mapping_statistics(
    const fs::path& worker,
    const fs::path& plugin_root
) {
    TempProject project;
    const auto input_dir = project.root() / "inputs" / "alignment-qc-sam";
    fs::create_directories(input_dir);
    const auto input_path = input_dir / "alignment.sam";
    std::ofstream{input_path, std::ios::binary | std::ios::trunc}
        << "@HD\tVN:1.6\tSO:unsorted\n"
        << "@SQ\tSN:chr1\tLN:100\n"
        << "pairA\t99\tchr1\t1\t60\t6M\t=\t10\t15\tACGTAC\tIIIIII\tNM:i:0\n"
        << "pairA\t147\tchr1\t10\t60\t6M\t=\t1\t-15\tGTACGT\tIIIIII\tNM:i:1\n"
        << "pairB\t77\t*\t0\t0\t*\t*\t0\t0\tACGTAC\tIIIIII\n"
        << "secondary\t256\tchr1\t5\t20\t6M\t*\t0\t0\tACGTAC\tIIIIII\tNM:i:2\n"
        << "supplementary\t2048\tchr1\t7\t30\t6M\t*\t0\t0\tACGTAC\tIIIIII\tNM:i:3\n";
    if (!fs::is_regular_file(input_path)) return false;

    infrastructure::sqlite::SqliteConnection connection{
        project.root() / ".biocore" / "project.sqlite"
    };
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    infrastructure::sqlite::SqliteManagedFileRepository files{connection};
    infrastructure::sqlite::SqliteJobRepository jobs{connection};
    if (!files.add(domain::ManagedFile{
        "alignment-qc-sam", "alignment.sam", domain::StorageMode::managed_copy,
        std::string{"/original/alignment.sam"}, path_to_utf8(input_path),
        "inputs/alignment-qc-sam/alignment.sam", "sam",
        static_cast<std::int64_t>(fs::file_size(input_path)), std::nullopt,
        std::nullopt, std::nullopt,
        "2026-08-15T17:00:00Z", "2026-08-15T17:00:00Z"
    })) return false;

    const domain::Job preparing{
        "job-alignment-qc", std::nullopt,
        std::string{"org.biocore.alignmentqc.summary"}, std::string{"0.1.0"},
        domain::JobStatus::preparing, domain::JobPriority::normal, 0.0, std::nullopt,
        "2026-08-15T17:00:00Z", "2026-08-15T17:00:01Z",
        std::string{"2026-08-15T17:00:01Z"}, std::nullopt, 1
    };
    if (!jobs.add(preparing)) return false;

    infrastructure::FilesystemPluginRegistry registry{{fs::canonical(plugin_root)}};
    const auto refresh = registry.refresh();
    const auto module = registry.find_module("org.biocore.alignmentqc.summary");
    if (refresh.loaded_plugins != 8U || refresh.loaded_modules != 17U ||
        !refresh.rejected.empty() || !module.has_value()) return false;

    infrastructure::JsonExecutionPlanStore plan_store{project.root()};
    application::PipelinePreparationService preparation{plan_store, registry, files};
    const domain::PipelineDefinition definition{
        1U, "org.biocore.alignmentqc.summary", "SAM/BAM Alignment QC", "0.1.0",
        {domain::PipelineStep{"qc", "org.biocore.alignmentqc.summary", {}, 1.0}}
    };
    application::PipelineRunBindings bindings{{
        application::PipelineStepBindings{
            .step_id = "qc", .parameters = {},
            .inputs = {{"alignment", application::ManagedFileInputSource{"alignment-qc-sam"}}},
        }
    }};
    const auto prepared = preparation.prepare(definition, "job-alignment-qc", 1, bindings);

    infrastructure::PlatformWorkerSupervisor supervisor{fs::canonical(worker), project.root()};
    supervisor.launch(application::WorkerLaunchRequest{
        .job_id = "job-alignment-qc", .analysis_id = std::nullopt,
        .pipeline_id = std::string{"org.biocore.alignmentqc.summary"},
        .pipeline_version = std::string{"0.1.0"},
        .priority = domain::JobPriority::normal, .job_revision = 1,
        .execution_plan_path = prepared.snapshot_path,
    });

    FixedId job_ids{"unused-job-id"};
    SequenceIds artifact_ids{{"artifact-alignment-qc-summary", "artifact-alignment-qc-table"}};
    Clock clock;
    application::JobService job_service{jobs, job_ids, clock};
    infrastructure::FilesystemOutputArtifactInspector inspector{project.root()};
    application::OutputArtifactService artifact_service{files, inspector, artifact_ids, clock};
    application::WorkerEventIngestionSession session{
        job_service, "job-alignment-qc", 1, &artifact_service
    };

    bool finalized = false;
    for (int attempt = 0; attempt < 500 && !finalized; ++attempt) {
        for (auto& output : supervisor.poll_output()) {
            if (output.job_id != "job-alignment-qc" || !output.protocol_issues.empty()) return false;
            ingest_events(session, output.events);
        }
        for (auto& exit : supervisor.reap_exited()) {
            if (exit.job_id != "job-alignment-qc" || !exit.protocol_issues.empty()) return false;
            ingest_events(session, exit.events);
            static_cast<void>(session.finalize_process_exit(exit.exit_code));
            finalized = true;
        }
        if (!finalized) std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    if (!finalized) return false;

    const auto job = jobs.find_by_id("job-alignment-qc");
    const auto summary = files.find_generated_output("job-alignment-qc", "qc", "summary");
    const auto table = files.find_generated_output("job-alignment-qc", "qc", "table");
    const auto listed = files.list_generated_outputs("job-alignment-qc");
    if (!job.has_value() || job->status() != domain::JobStatus::completed ||
        !summary.has_value() || !table.has_value() || listed.size() != 2U ||
        summary->file.file_type() != "json" || table->file.file_type() != "tsv" ||
        summary->provenance.module_id != "org.biocore.alignmentqc.summary") return false;

    const auto read_file = [&](const std::string& port) {
        std::ifstream input{project.root() / "outputs" /
            ("job-alignment-qc--qc--" + port + ".out"), std::ios::binary};
        return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    };
    const auto json = read_file("summary");
    const auto tsv = read_file("table");
    return json.find("\"format\": \"sam\"") != std::string::npos &&
           json.find("\"totalRecords\": 5") != std::string::npos &&
           json.find("\"primaryRecords\": 3") != std::string::npos &&
           json.find("\"primaryMapped\": 2") != std::string::npos &&
           json.find("\"primaryUnmapped\": 1") != std::string::npos &&
           json.find("\"secondaryRecords\": 1") != std::string::npos &&
           json.find("\"supplementaryRecords\": 1") != std::string::npos &&
           json.find("\"averageMapq\": 60.000000") != std::string::npos &&
           json.find("\"totalNmMismatches\": 1") != std::string::npos &&
           json.find("\"coverageAvailable\": true") != std::string::npos &&
           json.find("\"coveredReferenceBases\": 12") != std::string::npos &&
           json.find("\"coverageBreadthPercent\": 12.000000") != std::string::npos &&
           json.find("\"meanDepth\": 0.120000") != std::string::npos &&
           json.find("\"maximumDepth\": 1") != std::string::npos &&
           json.find("\"templateLengthObservations\": 1") != std::string::npos &&
           json.find("\"averageTemplateLength\": 15.000000") != std::string::npos &&
           json.find("\"name\":\"chr1\",\"primaryMapped\":2") != std::string::npos &&
           tsv.find("mapping_rate_percent\t66.666667\n") != std::string::npos &&
           tsv.find("coverage_breadth_percent\t12.000000\n") != std::string::npos &&
           tsv.find("average_template_length\t15.000000\n") != std::string::npos &&
           tsv.find("contig_primary_mapped:chr1\t2\n") != std::string::npos;
}


[[nodiscard]] bool native_snv_calling_generates_vcf_artifact(
    const fs::path& worker, const fs::path& plugin_root
) {
    TempProject project;
    infrastructure::sqlite::SqliteConnection connection{project.root() / ".biocore" / "project.sqlite"};
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    infrastructure::sqlite::SqliteManagedFileRepository files{connection};
    infrastructure::sqlite::SqliteJobRepository jobs{connection};

    fs::create_directories(project.root() / "inputs" / "variant-reference");
    fs::create_directories(project.root() / "inputs" / "variant-sam");
    const auto reference_path = project.root() / "inputs" / "variant-reference" / "reference.fa";
    const auto sam_path = project.root() / "inputs" / "variant-sam" / "alignment.sam";
    {
        std::ofstream output{reference_path, std::ios::binary};
        output << ">chr1\nACGTACGTACGT\n";
    }
    {
        std::ofstream output{sam_path, std::ios::binary};
        output << "@HD\tVN:1.6\tSO:unsorted\n"
               << "@SQ\tSN:chr1\tLN:12\n"
               << "alt1\t0\tchr1\t3\t60\t6M\t*\t0\t0\tGTGCGT\tIIIIII\n"
               << "alt2\t0\tchr1\t3\t60\t6M\t*\t0\t0\tGTGCGT\tIIIIII\n"
               << "ref1\t0\tchr1\t3\t60\t6M\t*\t0\t0\tGTACGT\tIIIIII\n"
               << "dup-alt\t1024\tchr1\t3\t60\t6M\t*\t0\t0\tGTGCGT\tIIIIII\n"
               << "lowmap-alt\t0\tchr1\t3\t10\t6M\t*\t0\t0\tGTGCGT\tIIIIII\n"
               << "secondary-alt\t256\tchr1\t3\t60\t6M\t*\t0\t0\tGTGCGT\tIIIIII\n";
    }
    if (!files.add(domain::ManagedFile{
            "variant-reference", "reference.fa", domain::StorageMode::managed_copy,
            std::string{"/original/reference.fa"}, path_to_utf8(reference_path),
            std::string{"inputs/variant-reference/reference.fa"}, "fasta", 19,
            std::nullopt, std::nullopt, std::nullopt,
            "2026-08-15T17:00:00Z", "2026-08-15T17:00:00Z"
        }) ||
        !files.add(domain::ManagedFile{
            "variant-sam", "alignment.sam", domain::StorageMode::managed_copy,
            std::string{"/original/alignment.sam"}, path_to_utf8(sam_path),
            std::string{"inputs/variant-sam/alignment.sam"}, "sam",
            static_cast<std::int64_t>(fs::file_size(sam_path)),
            std::nullopt, std::nullopt, std::nullopt,
            "2026-08-15T17:00:00Z", "2026-08-15T17:00:00Z"
        })) return false;

    const domain::Job preparing{
        "job-variant-call", std::nullopt, std::string{"org.biocore.variantcall.snv"},
        std::string{"0.1.0"}, domain::JobStatus::preparing, domain::JobPriority::normal,
        0.0, std::nullopt, "2026-08-15T17:00:00Z", "2026-08-15T17:00:01Z",
        std::string{"2026-08-15T17:00:01Z"}, std::nullopt, 1
    };
    if (!jobs.add(preparing)) return false;

    infrastructure::FilesystemPluginRegistry registry{{fs::canonical(plugin_root)}};
    const auto refresh = registry.refresh();
    const auto module = registry.find_module("org.biocore.variantcall.snv");
    if (refresh.loaded_plugins != 8U || refresh.loaded_modules != 17U ||
        !refresh.rejected.empty() || !module.has_value()) return false;

    infrastructure::JsonExecutionPlanStore plan_store{project.root()};
    application::PipelinePreparationService preparation{plan_store, registry, files};
    const domain::PipelineDefinition definition{
        1U, "org.biocore.variantcall.snv", "Native SNV Variant Calling", "0.1.0",
        {domain::PipelineStep{"call", "org.biocore.variantcall.snv", {}, 1.0}}
    };
    application::PipelineRunBindings bindings{{
        application::PipelineStepBindings{
            .step_id = "call",
            .parameters = {
                {"min-depth", domain::PluginParameterValue{std::int64_t{3}}},
                {"min-alt-count", domain::PluginParameterValue{std::int64_t{2}}},
                {"min-alt-fraction", domain::PluginParameterValue{0.2}},
                {"min-mapq", domain::PluginParameterValue{std::int64_t{20}}},
                {"min-base-quality", domain::PluginParameterValue{std::int64_t{20}}},
            },
            .inputs = {
                {"reference", application::ManagedFileInputSource{"variant-reference"}},
                {"alignment", application::ManagedFileInputSource{"variant-sam"}},
            },
        }
    }};
    const auto prepared = preparation.prepare(definition, "job-variant-call", 1, bindings);

    infrastructure::PlatformWorkerSupervisor supervisor{fs::canonical(worker), project.root()};
    supervisor.launch(application::WorkerLaunchRequest{
        .job_id = "job-variant-call", .analysis_id = std::nullopt,
        .pipeline_id = std::string{"org.biocore.variantcall.snv"},
        .pipeline_version = std::string{"0.1.0"},
        .priority = domain::JobPriority::normal, .job_revision = 1,
        .execution_plan_path = prepared.snapshot_path,
    });

    FixedId job_ids{"unused-job-id"};
    SequenceIds artifact_ids{{"artifact-variant-vcf", "artifact-variant-summary", "artifact-variant-table"}};
    Clock clock;
    application::JobService job_service{jobs, job_ids, clock};
    infrastructure::FilesystemOutputArtifactInspector inspector{project.root()};
    application::OutputArtifactService artifact_service{files, inspector, artifact_ids, clock};
    application::WorkerEventIngestionSession session{
        job_service, "job-variant-call", 1, &artifact_service
    };

    bool finalized = false;
    for (int attempt = 0; attempt < 500 && !finalized; ++attempt) {
        for (auto& output : supervisor.poll_output()) {
            if (output.job_id != "job-variant-call" || !output.protocol_issues.empty()) return false;
            ingest_events(session, output.events);
        }
        for (auto& exit : supervisor.reap_exited()) {
            if (exit.job_id != "job-variant-call" || !exit.protocol_issues.empty()) return false;
            ingest_events(session, exit.events);
            static_cast<void>(session.finalize_process_exit(exit.exit_code));
            finalized = true;
        }
        if (!finalized) std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    if (!finalized) return false;

    const auto job = jobs.find_by_id("job-variant-call");
    const auto variants = files.find_generated_output("job-variant-call", "call", "variants");
    const auto summary = files.find_generated_output("job-variant-call", "call", "summary");
    const auto table = files.find_generated_output("job-variant-call", "call", "table");
    const auto listed = files.list_generated_outputs("job-variant-call");
    if (!job.has_value() || job->status() != domain::JobStatus::completed ||
        !variants.has_value() || !summary.has_value() || !table.has_value() || listed.size() != 3U ||
        variants->file.file_type() != "vcf" || summary->file.file_type() != "json" ||
        table->file.file_type() != "tsv" ||
        variants->provenance.module_id != "org.biocore.variantcall.snv") return false;

    const auto read_file = [&](const std::string& port) {
        std::ifstream input{project.root() / "outputs" /
            ("job-variant-call--call--" + port + ".out"), std::ios::binary};
        return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    };
    const auto vcf = read_file("variants");
    const auto json = read_file("summary");
    const auto tsv = read_file("table");
    return vcf.find("##fileformat=VCFv4.3\n") != std::string::npos &&
           vcf.find("chr1\t5\t.\tA\tG\t.\tPASS\tDP=3;AC=2;AF=0.666667;ABQ=40.000000\n") != std::string::npos &&
           json.find("\"algorithm\": \"native-snv-pileup-v1\"") != std::string::npos &&
           json.find("\"totalRecords\": 6") != std::string::npos &&
           json.find("\"eligibleRecords\": 3") != std::string::npos &&
           json.find("\"secondaryRecords\": 1") != std::string::npos &&
           json.find("\"duplicateRecords\": 1") != std::string::npos &&
           json.find("\"lowMapqRecords\": 1") != std::string::npos &&
           json.find("\"calledSites\": 1") != std::string::npos &&
           json.find("\"calledAltAlleles\": 1") != std::string::npos &&
           tsv.find("chr1\t5\tA\tG\t3\t2\t0.666667\t40.000000\n") != std::string::npos;
}


[[nodiscard]] bool vcf_qc_filters_and_registers_annotation_ready_artifacts(
    const fs::path& worker,
    const fs::path& plugin_root
) {
    TempProject project;
    infrastructure::sqlite::SqliteConnection connection{project.root() / ".biocore" / "project.sqlite"};
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    infrastructure::sqlite::SqliteManagedFileRepository files{connection};
    infrastructure::sqlite::SqliteJobRepository jobs{connection};

    const auto input_dir = project.root() / "inputs" / "vcf-source";
    fs::create_directories(input_dir);
    const auto input_path = input_dir / "variants.vcf";
    {
        std::ofstream output{input_path, std::ios::binary};
        output << "##fileformat=VCFv4.3\n"
                  "##INFO=<ID=DP,Number=1,Type=Integer,Description=\"Depth\">\n"
                  "##INFO=<ID=AC,Number=A,Type=Integer,Description=\"ALT count\">\n"
                  "##INFO=<ID=AF,Number=A,Type=Float,Description=\"ALT fraction\">\n"
                  "##INFO=<ID=ABQ,Number=A,Type=Float,Description=\"ALT average base quality\">\n"
                  "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\n"
                  "chr1\t5\t.\tA\tG\t.\tPASS\tDP=3;AC=2;AF=0.666667;ABQ=40.000000\n"
                  "chr1\t8\t.\tC\tT\t.\tPASS\tDP=3;AC=1;AF=0.333333;ABQ=40.000000\n"
                  "chr1\t10\t.\tG\tA\t.\t.\tDP=5;AC=3;AF=0.600000;ABQ=40.000000\n";
    }
    const auto size = static_cast<std::int64_t>(fs::file_size(input_path));
    if (!files.add(domain::ManagedFile{
            "vcf-source", "variants.vcf", domain::StorageMode::managed_copy,
            std::string{"/original/variants.vcf"}, path_to_utf8(input_path),
            std::string{"inputs/vcf-source/variants.vcf"}, "vcf", size,
            std::nullopt, std::nullopt, std::nullopt,
            "2026-08-15T17:00:00Z", "2026-08-15T17:00:00Z"
        })) return false;

    const domain::Job preparing{
        "job-vcf-qc", std::nullopt, std::string{"org.biocore.vcfqc.filter"},
        std::string{"0.1.0"}, domain::JobStatus::preparing, domain::JobPriority::normal,
        0.0, std::nullopt, "2026-08-15T17:00:00Z", "2026-08-15T17:00:01Z",
        std::string{"2026-08-15T17:00:01Z"}, std::nullopt, 1
    };
    if (!jobs.add(preparing)) return false;

    infrastructure::FilesystemPluginRegistry registry{{fs::canonical(plugin_root)}};
    const auto refresh = registry.refresh();
    const auto module = registry.find_module("org.biocore.vcfqc.filter");
    if (refresh.loaded_plugins != 8U || refresh.loaded_modules != 17U ||
        !refresh.rejected.empty() || !module.has_value()) return false;

    infrastructure::JsonExecutionPlanStore plan_store{project.root()};
    application::PipelinePreparationService preparation{plan_store, registry, files};
    const domain::PipelineDefinition definition{
        1U, "org.biocore.vcfqc.filter", "VCF QC and Threshold Filtering", "0.1.0",
        {domain::PipelineStep{"qc", "org.biocore.vcfqc.filter", {}, 1.0}}
    };
    application::PipelineRunBindings bindings{{application::PipelineStepBindings{
        .step_id = "qc",
        .parameters = {
            {"enable-depth-filter", domain::PluginParameterValue{true}},
            {"min-depth", domain::PluginParameterValue{std::int64_t{3}}},
            {"enable-alt-count-filter", domain::PluginParameterValue{false}},
            {"min-alt-count", domain::PluginParameterValue{std::int64_t{2}}},
            {"enable-alt-fraction-filter", domain::PluginParameterValue{true}},
            {"min-alt-fraction", domain::PluginParameterValue{0.20}},
            {"enable-alt-base-quality-filter", domain::PluginParameterValue{true}},
            {"min-alt-base-quality", domain::PluginParameterValue{20.0}},
        },
        .inputs = {{"variants", application::ManagedFileInputSource{"vcf-source"}}},
    }}};
    const auto prepared = preparation.prepare(definition, "job-vcf-qc", 1, bindings);

    infrastructure::PlatformWorkerSupervisor supervisor{fs::canonical(worker), project.root()};
    supervisor.launch(application::WorkerLaunchRequest{
        .job_id = "job-vcf-qc", .analysis_id = std::nullopt,
        .pipeline_id = std::string{"org.biocore.vcfqc.filter"},
        .pipeline_version = std::string{"0.1.0"},
        .priority = domain::JobPriority::normal, .job_revision = 1,
        .execution_plan_path = prepared.snapshot_path,
    });

    FixedId job_ids{"unused-job-id"};
    SequenceIds artifact_ids{{"artifact-vcf-filtered", "artifact-vcf-summary", "artifact-vcf-table"}};
    Clock clock;
    application::JobService job_service{jobs, job_ids, clock};
    infrastructure::FilesystemOutputArtifactInspector inspector{project.root()};
    application::OutputArtifactService artifact_service{files, inspector, artifact_ids, clock};
    application::WorkerEventIngestionSession session{job_service, "job-vcf-qc", 1, &artifact_service};

    bool finalized = false;
    for (int attempt = 0; attempt < 500 && !finalized; ++attempt) {
        for (auto& output : supervisor.poll_output()) {
            if (output.job_id != "job-vcf-qc" || !output.protocol_issues.empty()) return false;
            ingest_events(session, output.events);
        }
        for (auto& exit : supervisor.reap_exited()) {
            if (exit.job_id != "job-vcf-qc" || !exit.protocol_issues.empty()) return false;
            ingest_events(session, exit.events);
            static_cast<void>(session.finalize_process_exit(exit.exit_code));
            finalized = true;
        }
        if (!finalized) std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    if (!finalized) return false;

    const auto job = jobs.find_by_id("job-vcf-qc");
    const auto filtered = files.find_generated_output("job-vcf-qc", "qc", "filtered");
    const auto summary = files.find_generated_output("job-vcf-qc", "qc", "summary");
    const auto table = files.find_generated_output("job-vcf-qc", "qc", "table");
    const auto listed = files.list_generated_outputs("job-vcf-qc");
    if (!job.has_value() || job->status() != domain::JobStatus::completed ||
        !filtered.has_value() || !summary.has_value() || !table.has_value() || listed.size() != 3U ||
        filtered->file.file_type() != "vcf" || summary->file.file_type() != "json" ||
        table->file.file_type() != "tsv" ||
        filtered->provenance.module_id != "org.biocore.vcfqc.filter") return false;

    const auto read_file = [&](const std::string& port) {
        std::ifstream input{project.root() / "outputs" / ("job-vcf-qc--qc--" + port + ".out"), std::ios::binary};
        return std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    };
    const auto vcf = read_file("filtered");
    const auto json = read_file("summary");
    const auto tsv = read_file("table");
    return vcf.find("##BioCoreVCFQC=<Version=0.1.0,AnnotationKey=CHROM:POS:REF:ALT,Normalization=none,DPFilter=on,MinDP=3,ACFilter=off,MinAC=2,AFFilter=on,MinAF=0.2,ABQFilter=on,MinABQ=20>\n") != std::string::npos &&
           vcf.find("chr1\t5\t.\tA\tG\t.\tPASS\tDP=3;AC=2;AF=0.666667;ABQ=40.000000\n") != std::string::npos &&
           vcf.find("chr1\t8\t.\tC\tT\t.\tPASS\tDP=3;AC=1;AF=0.333333;ABQ=40.000000\n") != std::string::npos &&
           vcf.find("chr1\t10\t.\tG\tA\t.\tBioCoreUnfiltered\tDP=5;AC=3;AF=0.600000;ABQ=40.000000\n") != std::string::npos &&
           json.find("\"depthFilterEnabled\":true") != std::string::npos &&
           json.find("\"altCountFilterEnabled\":false") != std::string::npos &&
           json.find("\"altFractionFilterEnabled\":true") != std::string::npos &&
           json.find("\"altBaseQualityFilterEnabled\":true") != std::string::npos &&
           json.find("\"totalRecords\":3") != std::string::npos &&
           json.find("\"passedRecords\":2") != std::string::npos &&
           json.find("\"filteredRecords\":1") != std::string::npos &&
           json.find("\"snvAlleles\":3") != std::string::npos &&
           json.find("\"transitions\":3") != std::string::npos &&
           tsv.find("chr1:5:A:G\tchr1\t5\t.\tA\tG\t1\tsnv") != std::string::npos &&
           tsv.find("chr1:8:C:T\tchr1\t8\t.\tC\tT\t1\tsnv") != std::string::npos;
}


[[nodiscard]] bool local_variant_annotation_generates_report_artifacts(
    const fs::path& worker, const fs::path& plugin_root
) {
    TempProject project;
    infrastructure::sqlite::SqliteConnection connection{project.root() / ".biocore" / "project.sqlite"};
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    infrastructure::sqlite::SqliteManagedFileRepository files{connection};
    infrastructure::sqlite::SqliteJobRepository jobs{connection};

    const auto vcf_dir = project.root() / "inputs" / "annotation-vcf";
    const auto tsv_dir = project.root() / "inputs" / "annotation-db";
    fs::create_directories(vcf_dir); fs::create_directories(tsv_dir);
    const auto vcf_path = vcf_dir / "filtered.vcf";
    const auto tsv_path = tsv_dir / "annotations.tsv";
    {
        std::ofstream out{vcf_path, std::ios::binary};
        out << "##fileformat=VCFv4.3\n"
               "##INFO=<ID=DP,Number=1,Type=Integer,Description=\"Depth\">\n"
               "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\n"
               "chr1\t5\t.\tA\tG\t.\tPASS\tDP=10\n"
               "chr1\t8\t.\tC\tT\t.\tBioCoreLowAF\tDP=8\n";
    }
    {
        std::ofstream out{tsv_path, std::ios::binary};
        out << "key\tgene\tconsequence\tclinical_significance\tsource\tsource_id\n"
               "chr1:5:A:G\tGENE1\tmissense_variant\tPathogenic & reviewed\tLocalDB\tVAR-001\n";
    }
    if (!files.add(domain::ManagedFile{
            "annotation-vcf", "filtered.vcf", domain::StorageMode::managed_copy,
            std::string{"/original/filtered.vcf"}, path_to_utf8(vcf_path),
            std::string{"inputs/annotation-vcf/filtered.vcf"}, "vcf",
            static_cast<std::int64_t>(fs::file_size(vcf_path)), std::nullopt, std::nullopt,
            std::nullopt, "2026-08-15T18:00:00Z", "2026-08-15T18:00:00Z"
        })) return false;
    if (!files.add(domain::ManagedFile{
            "annotation-db", "annotations.tsv", domain::StorageMode::managed_copy,
            std::string{"/original/annotations.tsv"}, path_to_utf8(tsv_path),
            std::string{"inputs/annotation-db/annotations.tsv"}, "tsv",
            static_cast<std::int64_t>(fs::file_size(tsv_path)), std::nullopt, std::nullopt,
            std::nullopt, "2026-08-15T18:00:00Z", "2026-08-15T18:00:00Z"
        })) return false;

    const domain::Job preparing{
        "job-annotation", std::nullopt, std::string{"org.biocore.variantannotate.local"},
        std::string{"0.1.0"}, domain::JobStatus::preparing, domain::JobPriority::normal,
        0.0, std::nullopt, "2026-08-15T18:00:00Z", "2026-08-15T18:00:01Z",
        std::string{"2026-08-15T18:00:01Z"}, std::nullopt, 1
    };
    if (!jobs.add(preparing)) return false;

    infrastructure::FilesystemPluginRegistry registry{{fs::canonical(plugin_root)}};
    const auto refresh = registry.refresh();
    if (refresh.loaded_plugins != 8U || refresh.loaded_modules != 17U ||
        !refresh.rejected.empty() || !registry.find_module("org.biocore.variantannotate.local").has_value()) return false;
    infrastructure::JsonExecutionPlanStore plan_store{project.root()};
    application::PipelinePreparationService preparation{plan_store, registry, files};
    const domain::PipelineDefinition definition{
        1U, "org.biocore.variantannotate.local", "Local Variant Annotation and Report", "0.1.0",
        {domain::PipelineStep{"annotate", "org.biocore.variantannotate.local", {}, 1.0}}
    };
    application::PipelineRunBindings bindings{{application::PipelineStepBindings{
        .step_id = "annotate", .parameters = {},
        .inputs = {
            {"variants", application::ManagedFileInputSource{"annotation-vcf"}},
            {"annotations", application::ManagedFileInputSource{"annotation-db"}},
        },
    }}};
    const auto prepared = preparation.prepare(definition, "job-annotation", 1, bindings);

    infrastructure::PlatformWorkerSupervisor supervisor{fs::canonical(worker), project.root()};
    supervisor.launch(application::WorkerLaunchRequest{
        .job_id = "job-annotation", .analysis_id = std::nullopt,
        .pipeline_id = std::string{"org.biocore.variantannotate.local"},
        .pipeline_version = std::string{"0.1.0"}, .priority = domain::JobPriority::normal,
        .job_revision = 1, .execution_plan_path = prepared.snapshot_path,
    });

    FixedId job_ids{"unused-job-id"};
    SequenceIds artifact_ids{{"artifact-ann-vcf", "artifact-ann-json", "artifact-ann-tsv", "artifact-ann-html"}};
    Clock clock; application::JobService job_service{jobs, job_ids, clock};
    infrastructure::FilesystemOutputArtifactInspector inspector{project.root()};
    application::OutputArtifactService artifact_service{files, inspector, artifact_ids, clock};
    application::WorkerEventIngestionSession session{job_service, "job-annotation", 1, &artifact_service};
    bool finalized=false;
    for(int attempt=0;attempt<500&&!finalized;++attempt){
        for(auto& output:supervisor.poll_output()){if(output.job_id!="job-annotation"||!output.protocol_issues.empty())return false;ingest_events(session,output.events);}
        for(auto& exit:supervisor.reap_exited()){if(exit.job_id!="job-annotation"||!exit.protocol_issues.empty())return false;ingest_events(session,exit.events);static_cast<void>(session.finalize_process_exit(exit.exit_code));finalized=true;}
        if(!finalized)std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    if(!finalized) return false;
    const auto annotated=files.find_generated_output("job-annotation","annotate","annotated");
    const auto summary=files.find_generated_output("job-annotation","annotate","summary");
    const auto table=files.find_generated_output("job-annotation","annotate","table");
    const auto report=files.find_generated_output("job-annotation","annotate","report");
    if(!annotated||!summary||!table||!report||files.list_generated_outputs("job-annotation").size()!=4U||
       annotated->file.file_type()!="vcf"||summary->file.file_type()!="json"||table->file.file_type()!="tsv"||report->file.file_type()!="html") return false;
    const auto read=[&](std::string_view port){std::ifstream in{project.root()/"outputs"/("job-annotation--annotate--"+std::string{port}+".out"),std::ios::binary};return std::string{std::istreambuf_iterator<char>{in},std::istreambuf_iterator<char>{}};};
    const auto vcf=read("annotated"), json=read("summary"), tsv=read("table"), html=read("report");
    return vcf.find("##BioCoreVariantAnnotation=<Version=0.1.0,Key=CHROM:POS:REF:ALT,Normalization=none,Mode=local-tsv>")!=std::string::npos &&
           vcf.find("BC_GENE=GENE1;BC_CSQ=missense_variant;BC_CLNSIG=Pathogenic%20%26%20reviewed;BC_SOURCE=LocalDB;BC_SOURCE_ID=VAR-001")!=std::string::npos &&
           vcf.find("BC_GENE=.;BC_CSQ=.;BC_CLNSIG=.;BC_SOURCE=.;BC_SOURCE_ID=.")!=std::string::npos &&
           json.find("\"annotationHits\":1")!=std::string::npos && json.find("\"annotationMisses\":1")!=std::string::npos &&
           json.find("\"name\":\"GENE1\",\"count\":1")!=std::string::npos &&
           tsv.find("chr1:5:A:G\tchr1\t5\tA\tG\tPASS\t1\tGENE1\tmissense_variant\tPathogenic & reviewed\tLocalDB\tVAR-001")!=std::string::npos &&
           html.find("OpenGenesis-BioCore Variant Annotation Report")!=std::string::npos && html.find("Pathogenic &amp; reviewed")!=std::string::npos;
}

}  // namespace

int main(int argc, const char* const argv[]) {
    if (argc != 3) {
        std::cerr << "Expected worker executable and plugin root\n";
        return EXIT_FAILURE;
    }
    const auto worker = fs::canonical(path_from_utf8(argv[1]));
    const auto plugin_root = fs::canonical(path_from_utf8(argv[2]));
    if (!contract(worker, plugin_root) ||
        !multi_output_step_registers_one_complete_batch(worker, plugin_root) ||
        !failed_plugin_does_not_register_artifacts(worker, plugin_root) ||
        !fasta_qc_generates_real_statistics(worker, plugin_root) ||
        !fastq_qc_generates_real_statistics(worker, plugin_root, false) ||
        !fastq_qc_generates_real_statistics(worker, plugin_root, true) ||
        !paired_fastq_qc_generates_real_statistics(worker, plugin_root) ||
        !paired_fastq_trimming_generates_real_fastq_outputs(worker, plugin_root) ||
        !native_paired_alignment_generates_sam_artifact(worker, plugin_root) ||
        !alignment_qc_generates_primary_mapping_statistics(worker, plugin_root) ||
        !native_snv_calling_generates_vcf_artifact(worker, plugin_root) ||
        !vcf_qc_filters_and_registers_annotation_ready_artifacts(worker, plugin_root) ||
        !local_variant_annotation_generates_report_artifacts(worker, plugin_root) ||
        !malformed_bioinformatics_inputs_are_rejected()) {
        std::cerr << "Plugin output artifact registration integration failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
