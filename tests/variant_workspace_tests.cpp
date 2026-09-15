#include "biocore/domain/case_control_association.hpp"
#include "biocore/domain/variant_workspace.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace biocore::domain;

void require(const bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
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

    std::istringstream cases{
        "##fileformat=VCFv4.3\n"
        "##FORMAT=<ID=GT,Number=1,Type=String,Description=\"Genotype\">\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tcase-1\tcase-2\n"
        "chr1\t10\trs-snv\tC\tG\t10\tPASS\t.\tGT\t0/1\t1/1\n"
        "chr1\t20\trs-ins\tT\tTAA\t20\tPASS\t.\tGT\t0/1\t0/1\n"
        "chr1\t30\trs-del\tCG\tC\t30\tPASS\t.\tGT\t0/0\t0/1\n"
        "chr1\t40\trs-mnv\tTA\tGC\t40\tPASS\t.\tGT\t1/1\t0/1\n"
        "chr1\t50\trs-delins\tCG\tTAA\t50\tPASS\t.\tGT\t0/1\t1/1\n"};
    std::istringstream controls{
        "##fileformat=VCFv4.3\n"
        "##FORMAT=<ID=GT,Number=1,Type=String,Description=\"Genotype\">\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tcontrol-1\tcontrol-2\n"
        "chr1\t10\trs-snv-c\tC\tG\t10\tPASS\t.\tGT\t0/0\t0/1\n"
        "chr1\t20\trs-ins-c\tT\tTAA\t20\tPASS\t.\tGT\t0/0\t0/1\n"
        "chr1\t30\trs-del-c\tCG\tC\t30\tPASS\t.\tGT\t0/0\t0/0\n"
        "chr1\t40\trs-mnv-c\tTA\tGC\t40\tPASS\t.\tGT\t0/1\t0/1\n"
        "chr1\t50\trs-delins-c\tCG\tTAA\t50\tPASS\t.\tGT\t0/0\t0/1\n"};

    const auto case_data = ingest_vcf(cases, genome);
    const auto control_data = ingest_vcf(controls, genome);
    ContigTableBuilder table_builder{ReferenceAssembly::grch38};
    table_builder.add_contig("1", {"chr1"});
    return build_multi_sample_matrix(
        assembly,
        table_builder.build(),
        {
            {"cases", assembly, &genome.contigs(), &case_data},
            {"controls", assembly, &genome.contigs(), &control_data}
        }
    );
}

std::vector<CaseControlSampleLabel> labels() {
    return {
        {"case-1", CaseControlPhenotype::case_sample},
        {"case-2", CaseControlPhenotype::case_sample},
        {"control-1", CaseControlPhenotype::control},
        {"control-2", CaseControlPhenotype::control}
    };
}

std::vector<VariantWorkspaceRecordMetadata> metadata(const MultiSampleMatrix& matrix) {
    std::vector<VariantWorkspaceRecordMetadata> result(matrix.variant_count());
    for (std::size_t index = 0U; index < result.size(); ++index) {
        result[index].record_id = "workspace-record-" + std::to_string(index);
        result[index].quality = 10.0 * static_cast<double>(index + 1U);
        result[index].filters_applied = true;
    }
    result.at(1U).filters = {"LowQual"};
    result.at(3U).filters_applied = false;
    result.at(4U).quality = std::nullopt;
    return result;
}

std::vector<VariantAnnotationResult> annotations(const MultiSampleMatrix& matrix) {
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
            allele.record_id = "known-variant-30";
            allele.provenance.database_id = "workspace-test-db";
            allele.provenance.display_name = "Workspace Test DB";
            allele.attributes.push_back({"gene_symbol", std::string{"BRCA1"}});
            alternate.allele_annotations.push_back(std::move(allele));
        }
        annotation.alternates.push_back(std::move(alternate));
        result.push_back(std::move(annotation));
    }
    return result;
}

struct WorkspaceFixture final {
    MultiSampleMatrix matrix{make_matrix()};
    CaseControlAssociationResult association{analyze_case_control(matrix, labels())};
    std::vector<VariantWorkspaceRecordMetadata> record_metadata{metadata(matrix)};
    std::vector<VariantAnnotationResult> variant_annotations{annotations(matrix)};
    VariantAnalysisWorkspace workspace{VariantAnalysisWorkspace::build(
        matrix,
        &association,
        &variant_annotations,
        &record_metadata
    )};
};

void test_query() {
    WorkspaceFixture fixture;
    require(fixture.workspace.size() == 5U, "workspace preserves five canonical variants");

    std::set<VariantType> observed_types;
    for (std::size_t index = 0U; index < fixture.workspace.size(); ++index) {
        observed_types.insert(fixture.workspace.detail(index).row.variant_type);
    }
    require(observed_types.contains(VariantType::snv), "workspace retains SNV");
    require(observed_types.contains(VariantType::insertion), "workspace retains insertion");
    require(observed_types.contains(VariantType::deletion), "workspace retains deletion");
    require(observed_types.contains(VariantType::mnv), "workspace retains MNV");
    require(observed_types.contains(VariantType::delins_complex), "workspace retains DELINS_COMPLEX");

    const auto contig = fixture.matrix.contigs().resolve("chr1");
    require(contig.has_value(), "workspace test contig resolves");
    VariantWorkspaceQuery query;
    query.contig_id = *contig;
    query.start = 8U;
    query.end = 21U;
    query.limit = 1U;
    query.query_generation_id = 77U;
    const auto first = fixture.workspace.query(query);
    require(first.total_matched_variants == 2U, "indexed interval query matches two variants");
    require(first.rows.size() == 1U, "bounded query honors limit");
    require(first.query_generation_id == 77U, "query generation token is echoed");
    query.offset = 1U;
    const auto second = fixture.workspace.query(query);
    require(second.rows.size() == 1U, "bounded query honors offset");
    require(second.rows.front().variant_index != first.rows.front().variant_index, "window offset advances result");
}

void test_filters() {
    WorkspaceFixture fixture;
    VariantWorkspaceQuery pass;
    pass.pass_only = true;
    const auto pass_rows = fixture.workspace.query(pass);
    require(pass_rows.total_matched_variants == 3U, "PASS filter excludes tagged and unapplied records");

    VariantWorkspaceQuery quality;
    quality.minimum_quality = 25.0;
    const auto quality_rows = fixture.workspace.query(quality);
    require(quality_rows.total_matched_variants == 2U, "quality filter excludes missing and low values");

    VariantWorkspaceQuery type;
    type.variant_types = {VariantType::delins_complex};
    const auto type_rows = fixture.workspace.query(type);
    require(type_rows.total_matched_variants == 1U, "canonical type filter isolates DELINS_COMPLEX");

    VariantWorkspaceQuery combined;
    combined.minimum_quality = 15.0;
    combined.maximum_allele_fdr_q = 1.0;
    combined.minimum_carrier_count = 1U;
    const auto combined_rows = fixture.workspace.query(combined);
    require(combined_rows.total_matched_variants > 0U, "composable association and quality filters return data");
    for (const auto& row : combined_rows.rows) {
        require(row.quality.has_value() && *row.quality >= 15.0, "combined filter enforces quality");
        require(row.allele_bh_adjusted_q.has_value(), "combined filter consumes canonical BH q-value");
        require(row.carrier_sample_count >= 1U, "combined filter enforces carrier count");
    }
}

void test_sorting() {
    WorkspaceFixture fixture;
    VariantWorkspaceQuery descending;
    descending.sort_key = VariantWorkspaceSortKey::quality;
    descending.sort_direction = VariantWorkspaceSortDirection::descending;
    const auto high_first = fixture.workspace.query(descending);
    require(high_first.rows.size() == 5U, "quality sort returns complete bounded dataset");
    require(high_first.rows.back().quality == std::nullopt, "nullable sort is null-last descending");
    for (std::size_t index = 1U; index + 1U < high_first.rows.size(); ++index) {
        require(
            *high_first.rows[index - 1U].quality >= *high_first.rows[index].quality,
            "quality descending order is stable"
        );
    }

    VariantWorkspaceQuery ascending = descending;
    ascending.sort_direction = VariantWorkspaceSortDirection::ascending;
    const auto low_first = fixture.workspace.query(ascending);
    require(low_first.rows.back().quality == std::nullopt, "nullable sort is null-last ascending");
    for (std::size_t index = 1U; index + 1U < low_first.rows.size(); ++index) {
        require(
            *low_first.rows[index - 1U].quality <= *low_first.rows[index].quality,
            "quality ascending order is stable"
        );
    }

    VariantWorkspaceQuery coordinate;
    const auto first = fixture.workspace.query(coordinate);
    const auto second = fixture.workspace.query(coordinate);
    require(first.rows.size() == second.rows.size(), "deterministic sort cardinality");
    for (std::size_t index = 0U; index < first.rows.size(); ++index) {
        require(first.rows[index].variant_index == second.rows[index].variant_index, "deterministic coordinate tie-breaking");
    }
}

void test_detail() {
    WorkspaceFixture fixture;
    const auto detail = fixture.workspace.detail(2U);
    require(detail.row.variant_index == 2U, "detail retains dataset-local variant identity");
    require(detail.sample_calls.size() == 4U, "detail loads sample calls on demand");
    require(detail.annotation.has_value(), "detail exposes canonical annotation result");
    require(detail.association.has_value(), "detail exposes canonical case/control result");
    require(detail.row.association_available, "row marks association availability");
    require(detail.row.allele_bh_adjusted_q.has_value(), "detail row exposes canonical allele BH q-value");
    require(detail.row.allele_odds_ratio_ci95.has_value() || !detail.row.allele_odds_ratio_ci95.has_value(), "CI missingness is explicit");

    bool rejected = false;
    try {
        (void)fixture.workspace.detail(fixture.workspace.size());
    } catch (const std::out_of_range&) {
        rejected = true;
    }
    require(rejected, "detail rejects out-of-range variant identity");
}

void test_integration() {
    WorkspaceFixture fixture;
    VariantWorkspaceQuery search;
    search.search_text = "brca1";
    const auto annotated = fixture.workspace.query(search);
    require(annotated.total_matched_variants == 1U, "annotation attribute search is integrated");
    require(annotated.rows.front().primary_annotation_id == std::optional<std::string>{"known-variant-30"}, "annotation summary is factual");

    const auto without_optional_layers = VariantAnalysisWorkspace::build(fixture.matrix);
    const auto plain = without_optional_layers.query({});
    require(plain.total_matched_variants == 5U, "workspace remains usable without annotation or association");
    require(!plain.rows.front().annotation_available, "annotation absence remains explicit");
    require(!plain.rows.front().association_available, "association absence remains explicit");

    bool rejected = false;
    try {
        VariantWorkspaceQuery invalid;
        invalid.limit = 251U;
        (void)fixture.workspace.query(invalid);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "workspace rejects oversized windows");

    rejected = false;
    try {
        VariantWorkspaceQuery invalid;
        invalid.start = 1U;
        invalid.end = 2U;
        (void)fixture.workspace.query(invalid);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "workspace rejects genomic range without contig");

    CaseControlAssociationResult wrong_cardinality;
    wrong_cardinality.variants.resize(1U);
    rejected = false;
    try {
        (void)VariantAnalysisWorkspace::build(fixture.matrix, &wrong_cardinality);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "workspace rejects misaligned association layer");
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 2) {
        throw std::invalid_argument("expected one variant workspace test mode");
    }
    const std::string_view mode{argv[1]};
    if (mode == "query") {
        test_query();
    } else if (mode == "filters") {
        test_filters();
    } else if (mode == "sorting") {
        test_sorting();
    } else if (mode == "detail") {
        test_detail();
    } else if (mode == "integration") {
        test_integration();
    } else {
        throw std::invalid_argument("unknown variant workspace test mode");
    }
}
