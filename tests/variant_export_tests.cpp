#include "biocore/application/variant_export.hpp"
#include "biocore/presentation/variant_export.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace biocore;
using namespace biocore::domain;

constexpr std::string_view reference_sha =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr std::string_view database_sha =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

void require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string reference_sequence(const std::size_t length) {
    constexpr std::string_view bases{"ACGT"};
    std::string sequence;
    sequence.reserve(length);
    for (std::size_t index = 0U; index < length; ++index) {
        sequence.push_back(bases[index % bases.size()]);
    }
    return sequence;
}

MultiSampleMatrix make_matrix() {
    std::istringstream fasta{">chr1\n" + reference_sequence(200U) + "\n"};
    const auto genome = ReferenceGenome::from_fasta(fasta, ReferenceAssembly::grch38);
    const ReferenceAssemblyIdentity assembly{ReferenceAssembly::grch38, {}};
    std::istringstream variants{
        "##fileformat=VCFv4.3\n"
        "##FORMAT=<ID=GT,Number=1,Type=String,Description=\"Genotype\">\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tcase-1\tcontrol-1\n"
        "chr1\t10\trs-snv\tC\tG\t10\tPASS\t.\tGT\t0/1\t0/0\n"
        "chr1\t20\trs-ins\tT\tTAA\t20\tPASS\t.\tGT\t0/1\t0/0\n"
        "chr1\t30\trs-del\tCG\tC\t30\tPASS\t.\tGT\t0/1\t0/0\n"
        "chr1\t40\trs-mnv\tTA\tGC\t40\tPASS\t.\tGT\t1/1\t0/1\n"
        "chr1\t50\trs-delins\tCG\tTAA\t50\tPASS\t.\tGT\t0/1\t0/0\n"};
    const auto data = ingest_vcf(variants, genome);
    ContigTableBuilder table_builder{ReferenceAssembly::grch38};
    table_builder.add_contig("1", {"chr1"});
    return build_multi_sample_matrix(
        assembly,
        table_builder.build(),
        {{"cohort", assembly, &genome.contigs(), &data}}
    );
}

std::vector<VariantWorkspaceRecordMetadata> make_metadata(const MultiSampleMatrix& matrix) {
    std::vector<VariantWorkspaceRecordMetadata> result(matrix.variant_count());
    for (std::size_t index = 0U; index < result.size(); ++index) {
        result[index].record_id = index == 2U ? std::string{"record,with,comma"}
                                              : std::string{"record-"} + std::to_string(index);
        result[index].quality = 10.0 * static_cast<double>(index + 1U);
        result[index].filters_applied = true;
    }
    result[1U].filters = {"LowQual"};
    result[4U].quality = std::nullopt;
    return result;
}

std::vector<VariantAnnotationResult> make_annotations(const MultiSampleMatrix& matrix) {
    const AnnotationDatabaseProvenance database{
        .database_id = "dbsnp-test",
        .display_name = "dbSNP Test",
        .version = "2026.09",
        .schema_version = 3U,
        .assembly = {ReferenceAssembly::grch38, {}},
        .source_uri = "/private/reference/dbsnp.tsv",
        .source_sha256 = std::string{database_sha},
    };
    std::vector<VariantAnnotationResult> result;
    result.reserve(matrix.variant_count());
    for (std::size_t index = 0U; index < matrix.variant_count(); ++index) {
        const auto& variant = matrix.variant(index);
        VariantAnnotationResult annotation;
        annotation.locus = variant.locus;
        annotation.reference = variant.reference;
        AlternateAnnotation alternate;
        alternate.alternate_index = 0U;
        alternate.alternate = variant.alternate;
        if (index == 2U) {
            AlleleAnnotation allele;
            allele.provenance = database;
            allele.record_id = "known-variant-30";
            allele.attributes.push_back({"gene_symbol", std::string{"BRCA1"}});
            alternate.allele_annotations.push_back(std::move(allele));
        }
        annotation.alternates.push_back(std::move(alternate));
        result.push_back(std::move(annotation));
    }
    return result;
}

application::VariantExportProvenance provenance() {
    return {
        .schema_version = application::VariantExportProvenance::current_schema_version,
        .producer_name = "OpenGenesis-BioCore",
        .producer_version = "0.3.0-dev",
        .generated_at_utc = "2026-09-15T12:00:00Z",
        .pipeline = {
            .pipeline_id = "org.biocore.variant.workspace",
            .pipeline_version = "0.3.0",
            .job_id = std::string{"job-066"},
            .job_revision = 7,
            .attempt_number = 2,
        },
        .reference = {
            .reference_id = "GRCh38-test-reference",
            .assembly = {ReferenceAssembly::grch38, {}},
            .source_sha256 = std::string{reference_sha},
        },
        .databases = {},
    };
}

struct Fixture final {
    MultiSampleMatrix matrix{make_matrix()};
    CaseControlAssociationResult associations{analyze_case_control(
        matrix,
        {{"case-1", CaseControlPhenotype::case_sample},
         {"control-1", CaseControlPhenotype::control}}
    )};
    std::vector<VariantWorkspaceRecordMetadata> metadata{make_metadata(matrix)};
    std::vector<VariantAnnotationResult> annotations{make_annotations(matrix)};
    VariantAnalysisWorkspace workspace{VariantAnalysisWorkspace::build(
        matrix, &associations, &annotations, &metadata
    )};
};

void test_provenance() {
    Fixture fixture;
    const auto plan = application::build_variant_export_plan(fixture.workspace, provenance());
    require(plan.row_count() == 5U, "export plan covers complete workspace");
    require(plan.provenance().databases.size() == 1U, "annotation database provenance is collected");
    require(plan.provenance().databases[0].database_id == "dbsnp-test", "database identity is preserved");
    require(plan.provenance().databases[0].source_sha256 == database_sha, "database digest is preserved");

    const std::array<std::size_t, 3U> selected{4U, 0U, 2U};
    const auto subset = application::build_variant_export_plan(fixture.workspace, provenance(), selected);
    require(subset.row_count() == 3U, "subset export cardinality is preserved");
    require(subset.source_variant_index(0U) == 0U && subset.source_variant_index(1U) == 2U &&
            subset.source_variant_index(2U) == 4U,
            "subset export is deterministically coordinate ordered");
}

void test_tabular() {
    Fixture fixture;
    const auto plan = application::build_variant_export_plan(fixture.workspace, provenance());
    const std::string tsv = presentation::render_variant_export_tsv(plan);
    const std::string csv = presentation::render_variant_export_csv(plan);
    require(tsv.find("#pipeline=org.biocore.variant.workspace@0.3.0") != std::string::npos,
            "TSV embeds pipeline provenance");
    require(tsv.find("#reference=GRCh38-test-reference|GRCh38|") != std::string::npos,
            "TSV embeds reference provenance");
    require(tsv.find("#database=dbsnp-test|2026.09|3|GRCh38|") != std::string::npos,
            "TSV embeds database provenance");
    require(tsv.find("DELINS_COMPLEX") != std::string::npos, "TSV preserves complex variant type");
    require(csv.find("\"record,with,comma\"") != std::string::npos,
            "CSV quotes delimiter-bearing values");
    require(tsv.find("/private/reference/") == std::string::npos,
            "tabular provenance does not disclose source paths");
}

void test_json() {
    Fixture fixture;
    const auto plan = application::build_variant_export_plan(fixture.workspace, provenance());
    const std::string json = presentation::render_variant_export_json(plan);
    require(json.find("\"schemaVersion\":1") != std::string::npos, "JSON schema version is emitted");
    require(json.find("\"variantCount\":5") != std::string::npos, "JSON variant count is emitted");
    require(json.find("\"sourceSha256\":\"aaaaaaaa") != std::string::npos,
            "JSON reference SHA-256 is emitted");
    require(json.find("\"databaseId\":\"dbsnp-test\"") != std::string::npos,
            "JSON database provenance is emitted");
    require(json.find("\"variantType\":\"DELINS_COMPLEX\"") != std::string::npos,
            "JSON preserves DELINS/COMPLEX identity");
    require(json.find("/private/reference/") == std::string::npos,
            "JSON provenance does not disclose source paths");
    require(json.find("\"quality\":null") != std::string::npos,
            "JSON keeps missing numeric values as null");
}

void test_vcf() {
    Fixture fixture;
    const auto plan = application::build_variant_export_plan(fixture.workspace, provenance());
    const std::string vcf = presentation::render_variant_export_vcf(plan);
    require(vcf.starts_with("##fileformat=VCFv4.3\n"), "VCF declares version 4.3");
    require(vcf.find("##biocore_reference_sha256=\"aaaaaaaa") != std::string::npos,
            "VCF embeds reference SHA-256");
    require(vcf.find("##biocore_database=\"dbsnp-test|2026.09|3|GRCh38|") != std::string::npos,
            "VCF embeds database provenance");
    require(vcf.find("#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\n") != std::string::npos,
            "VCF is site-level and does not invent FORMAT/sample genotypes");
    require(vcf.find("1\t10\trecord-0\tC\tG\t10\tPASS\tBC_TYPE=SNV") != std::string::npos,
            "VCF converts canonical 0-based start to 1-based POS");
    require(vcf.find("BC_TYPE=DELINS_COMPLEX") != std::string::npos,
            "VCF preserves complex variant type in INFO");
    require(vcf.find("BC_AQ=") != std::string::npos && vcf.find("BC_CQ=") != std::string::npos,
            "VCF exposes canonical BH FDR values");
    require(vcf.find("/private/reference/") == std::string::npos,
            "VCF provenance does not disclose source paths");

    std::istringstream exported{vcf};
    std::istringstream fasta{">1\n" + reference_sequence(200U) + "\n"};
    const auto genome = ReferenceGenome::from_fasta(fasta, ReferenceAssembly::grch38);
    const auto round_trip = ingest_vcf(exported, genome);
    require(round_trip.records.size() == 5U, "site-level VCF re-enters canonical ingestion");
    require(round_trip.records.back().variant.alternates[0U].type == VariantType::delins_complex,
            "VCF round-trip preserves DELINS_COMPLEX canonical identity");
}

void test_validation() {
    Fixture fixture;
    auto bad_sha = provenance();
    bad_sha.reference.source_sha256 = "not-a-sha";
    bool rejected = false;
    try {
        (void)application::build_variant_export_plan(fixture.workspace, bad_sha);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "invalid reference SHA-256 is rejected");

    auto wrong_assembly = provenance();
    wrong_assembly.reference.assembly = {ReferenceAssembly::grch37, {}};
    rejected = false;
    try {
        (void)application::build_variant_export_plan(fixture.workspace, wrong_assembly);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "reference assembly mismatch is rejected");

    auto conflicting_database = provenance();
    conflicting_database.databases.push_back({
        .database_id = "dbsnp-test",
        .display_name = "Conflicting dbSNP",
        .version = "2026.09",
        .schema_version = 3U,
        .assembly = {ReferenceAssembly::grch38, {}},
        .source_sha256 = std::string(64U, 'c'),
    });
    rejected = false;
    try {
        (void)application::build_variant_export_plan(fixture.workspace, conflicting_database);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "conflicting database provenance is rejected");

    const std::array<std::size_t, 2U> duplicates{1U, 1U};
    rejected = false;
    try {
        (void)application::build_variant_export_plan(fixture.workspace, provenance(), duplicates);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "duplicate variant selection is rejected");

    const std::array<std::size_t, 1U> out_of_range{99U};
    rejected = false;
    try {
        (void)application::build_variant_export_plan(fixture.workspace, provenance(), out_of_range);
    } catch (const std::out_of_range&) {
        rejected = true;
    }
    require(rejected, "out-of-range variant selection is rejected");
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        if (argc != 2) throw std::invalid_argument("variant export test mode is required");
        const std::string_view mode{argv[1]};
        if (mode == "provenance") test_provenance();
        else if (mode == "tabular") test_tabular();
        else if (mode == "json") test_json();
        else if (mode == "vcf") test_vcf();
        else if (mode == "validation") test_validation();
        else throw std::invalid_argument("unknown variant export test mode");
    } catch (const std::exception& error) {
        std::cerr << "Variant export test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
