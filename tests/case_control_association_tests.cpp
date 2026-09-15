#include "biocore/domain/case_control_association.hpp"
#include "biocore/domain/variant_workspace.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
using namespace biocore::domain;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
MultiSampleMatrix make_matrix() {
    std::istringstream fasta{">chr1\nAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"};
    auto genome = ReferenceGenome::from_fasta(fasta, ReferenceAssembly::grch38);
    ReferenceAssemblyIdentity assembly{ReferenceAssembly::grch38,{}};
    std::istringstream cases{
        "##fileformat=VCFv4.3\n##FORMAT=<ID=GT,Number=1,Type=String,Description=\"Genotype\">\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tcase-1\tcase-2\n"
        "chr1\t10\trs1\tA\tG\t.\tPASS\t.\tGT\t0/1\t1/1\n"};
    std::istringstream controls{
        "##fileformat=VCFv4.3\n##FORMAT=<ID=GT,Number=1,Type=String,Description=\"Genotype\">\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tcontrol-1\tcontrol-2\n"
        "chr1\t10\trs1c\tA\tG\t.\tPASS\t.\tGT\t0/0\t0/1\n"};
    auto case_data=ingest_vcf(cases,genome); auto control_data=ingest_vcf(controls,genome);
    ContigTableBuilder tb{ReferenceAssembly::grch38}; tb.add_contig("1",{"chr1"});
    return build_multi_sample_matrix(assembly,tb.build(),{{"cases",assembly,&genome.contigs(),&case_data},{"controls",assembly,&genome.contigs(),&control_data}});
}

MultiSampleMatrix make_workspace_matrix() {
    std::istringstream fasta{">chr1\nAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"};
    auto genome = ReferenceGenome::from_fasta(fasta, ReferenceAssembly::grch38);
    ReferenceAssemblyIdentity assembly{ReferenceAssembly::grch38,{}};
    std::istringstream cases{
        "##fileformat=VCFv4.3\n##FORMAT=<ID=GT,Number=1,Type=String,Description=\"Genotype\">\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tcase-1\tcase-2\n"
        "chr1\t10\trs10\tA\tG\t10\tPASS\t.\tGT\t0/1\t1/1\n"
        "chr1\t20\trs20\tA\tC\t20\tPASS\t.\tGT\t0/0\t0/1\n"
        "chr1\t30\trs30\tA\tT\t30\tPASS\t.\tGT\t1/1\t1/1\n"};
    std::istringstream controls{
        "##fileformat=VCFv4.3\n##FORMAT=<ID=GT,Number=1,Type=String,Description=\"Genotype\">\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tcontrol-1\tcontrol-2\n"
        "chr1\t10\trs10c\tA\tG\t10\tPASS\t.\tGT\t0/0\t0/1\n"
        "chr1\t20\trs20c\tA\tC\t20\tPASS\t.\tGT\t0/0\t0/0\n"
        "chr1\t30\trs30c\tA\tT\t30\tPASS\t.\tGT\t0/1\t0/1\n"};
    auto case_data=ingest_vcf(cases,genome); auto control_data=ingest_vcf(controls,genome);
    ContigTableBuilder tb{ReferenceAssembly::grch38}; tb.add_contig("1",{"chr1"});
    return build_multi_sample_matrix(assembly,tb.build(),{{"cases",assembly,&genome.contigs(),&case_data},{"controls",assembly,&genome.contigs(),&control_data}});
}

void test_workspace_core() {
    auto matrix=make_workspace_matrix();
    std::vector<CaseControlSampleLabel> labels{{"case-1",CaseControlPhenotype::case_sample},{"case-2",CaseControlPhenotype::case_sample},{"control-1",CaseControlPhenotype::control},{"control-2",CaseControlPhenotype::control}};
    auto associations=analyze_case_control(matrix,labels);

    std::vector<VariantWorkspaceRecordMetadata> metadata(3U);
    metadata[0].record_id="rs10"; metadata[0].quality=10.0; metadata[0].filters_applied=true;
    metadata[1].record_id="rs20"; metadata[1].quality=20.0; metadata[1].filters_applied=true; metadata[1].filters={"LowQual"};
    metadata[2].record_id="rs30"; metadata[2].quality=30.0; metadata[2].filters_applied=false;

    std::vector<VariantAnnotationResult> annotations;
    annotations.reserve(matrix.variant_count());
    for (std::size_t index=0U; index<matrix.variant_count(); ++index) {
        const auto& variant=matrix.variant(index);
        VariantAnnotationResult annotation;
        annotation.locus=variant.locus;
        annotation.reference=variant.reference;
        AlternateAnnotation alternate;
        alternate.alternate_index=0U;
        alternate.alternate=variant.alternate;
        if (index==1U) {
            AlleleAnnotation allele;
            allele.record_id="db-20";
            allele.provenance.database_id="test-db";
            allele.provenance.display_name="Test DB";
            allele.attributes.push_back({"gene_symbol",std::string{"BRCA1"}});
            alternate.allele_annotations.push_back(std::move(allele));
        }
        annotation.alternates.push_back(std::move(alternate));
        annotations.push_back(std::move(annotation));
    }

    auto workspace=VariantAnalysisWorkspace::build(matrix,&associations,&annotations,&metadata);
    require(workspace.size()==3U,"workspace row count");
    const auto contig=matrix.contigs().resolve("chr1");
    require(contig.has_value(),"workspace contig lookup");

    VariantWorkspaceQuery range;
    range.contig_id=*contig;
    range.start=18U;
    range.end=31U;
    range.limit=1U;
    range.query_generation_id=42U;
    const auto first_window=workspace.query(range);
    require(first_window.total_matched_variants==2U,"workspace indexed range count");
    require(first_window.rows.size()==1U,"workspace bounded window");
    require(first_window.rows[0].variant_index==1U,"workspace range first row");
    require(first_window.query_generation_id==42U,"workspace generation echo");

    range.offset=1U;
    const auto second_window=workspace.query(range);
    require(second_window.rows.size()==1U && second_window.rows[0].variant_index==2U,"workspace offset window");

    VariantWorkspaceQuery pass_query;
    pass_query.pass_only=true;
    const auto pass_rows=workspace.query(pass_query);
    require(pass_rows.total_matched_variants==1U && pass_rows.rows[0].variant_index==0U,"workspace pass filter");

    VariantWorkspaceQuery annotation_query;
    annotation_query.search_text="brca1";
    const auto annotation_rows=workspace.query(annotation_query);
    require(annotation_rows.total_matched_variants==1U && annotation_rows.rows[0].variant_index==1U,"workspace annotation search");
    require(annotation_rows.rows[0].annotation_available && annotation_rows.rows[0].allele_annotation_count==1U,"workspace annotation summary");

    VariantWorkspaceQuery quality_sort;
    quality_sort.sort_key=VariantWorkspaceSortKey::quality;
    quality_sort.sort_direction=VariantWorkspaceSortDirection::descending;
    const auto quality_rows=workspace.query(quality_sort);
    require(quality_rows.rows.size()==3U,"workspace quality sort cardinality");
    require(quality_rows.rows[0].variant_index==2U && quality_rows.rows[2].variant_index==0U,"workspace quality descending");

    VariantWorkspaceQuery fdr_query;
    fdr_query.maximum_allele_fdr_q=1.0;
    const auto fdr_rows=workspace.query(fdr_query);
    require(fdr_rows.total_matched_variants==3U,"workspace FDR filter consumes canonical association q-values");
    require(fdr_rows.rows[0].association_available,"workspace association summary");

    const auto detail=workspace.detail(1U);
    require(detail.row.variant_index==1U,"workspace detail identity");
    require(detail.sample_calls.size()==4U,"workspace detail sample observations");
    require(detail.annotation.has_value() && detail.association.has_value(),"workspace detail on-demand context");

    bool rejected=false;
    try { VariantWorkspaceQuery invalid; invalid.limit=251U; (void)workspace.query(invalid); }
    catch (const std::invalid_argument&) { rejected=true; }
    require(rejected,"workspace rejects oversized windows");

    rejected=false;
    try { VariantWorkspaceQuery invalid; invalid.start=1U; invalid.end=2U; (void)workspace.query(invalid); }
    catch (const std::invalid_argument&) { rejected=true; }
    require(rejected,"workspace rejects range without contig");
}
}
int main() {
    auto matrix=make_matrix();
    std::vector<CaseControlSampleLabel> labels{{"control-2",CaseControlPhenotype::control},{"case-1",CaseControlPhenotype::case_sample},{"control-1",CaseControlPhenotype::control},{"case-2",CaseControlPhenotype::case_sample}};
    auto result=analyze_case_control(matrix,labels);
    require(result.variants.size()==1U,"variant result count");
    const auto& a=result.variants[0];
    require(a.total_cases==2U && a.total_controls==2U,"group totals");
    require(a.case_complete_calls==2U && a.control_complete_calls==2U,"complete call totals");
    require(a.allele_table==AssociationContingencyTable{3U,1U,1U,3U},"allele table");
    require(a.carrier_table==AssociationContingencyTable{2U,0U,1U,1U},"carrier table");
    require(a.allele_odds_ratio.kind==AssociationOddsRatioKind::finite && std::abs(a.allele_odds_ratio.value-9.0)<1e-12,"allele OR");
    require(a.carrier_odds_ratio.kind==AssociationOddsRatioKind::positive_infinity,"carrier OR infinity");
    require(a.allele_odds_ratio_ci95.has_value(),"allele OR CI95 present");
    require(a.allele_odds_ratio_ci95->lower < a.allele_odds_ratio.value && a.allele_odds_ratio_ci95->upper > a.allele_odds_ratio.value,"allele OR CI95 brackets estimate");
    require(!a.carrier_odds_ratio_ci95.has_value(),"zero-cell carrier OR CI95 is explicitly unavailable");
    require(a.allele_fisher_two_sided_p.has_value() && std::abs(*a.allele_fisher_two_sided_p-0.4857142857142857)<1e-12,"allele Fisher");
    require(a.carrier_fisher_two_sided_p.has_value() && std::abs(*a.carrier_fisher_two_sided_p-1.0)<1e-12,"carrier Fisher");
    require(a.allele_bh_adjusted_q.has_value() && std::abs(*a.allele_bh_adjusted_q-*a.allele_fisher_two_sided_p)<1e-12,"single allele BH q");
    require(a.carrier_bh_adjusted_q.has_value() && std::abs(*a.carrier_bh_adjusted_q-*a.carrier_fisher_two_sided_p)<1e-12,"single carrier BH q");

    AssociationContingencyTable classic{1U,9U,11U,3U};
    require(std::abs(fisher_exact_two_sided(classic)-0.0027594561852200836)<1e-12,"classic Fisher exact");
    const auto classic_or=association_odds_ratio(classic);
    require(classic_or.kind==AssociationOddsRatioKind::finite && std::abs(classic_or.value-(1.0/33.0))<1e-15,"classic OR");
    const auto classic_ci=association_odds_ratio_woolf_ci95(classic);
    require(classic_ci.has_value() && classic_ci->lower < classic_or.value && classic_ci->upper > classic_or.value,"classic OR CI95");

    const std::vector<std::optional<double>> raw_p{0.01,0.04,0.03,std::nullopt};
    const auto bh=benjamini_hochberg_adjust(raw_p);
    require(bh.size()==4U,"BH output cardinality");
    require(bh[0].has_value() && std::abs(*bh[0]-0.03)<1e-12,"BH rank one");
    require(bh[1].has_value() && std::abs(*bh[1]-0.04)<1e-12,"BH rank three");
    require(bh[2].has_value() && std::abs(*bh[2]-0.04)<1e-12,"BH monotonicity");
    require(!bh[3].has_value(),"BH preserves missing p-values");

    bool rejected=false;
    try { (void)benjamini_hochberg_adjust({0.1,1.1}); } catch (const std::invalid_argument&) { rejected=true; }
    require(rejected,"BH rejects invalid p-values");

    std::reverse(labels.begin(),labels.end());
    const auto reversed=analyze_case_control(matrix,labels);
    require(reversed.variants[0].allele_table==a.allele_table && reversed.variants[0].carrier_table==a.carrier_table,"label-order determinism");
    require(reversed.variants[0].allele_bh_adjusted_q==a.allele_bh_adjusted_q,"BH determinism");

    test_workspace_core();
}
