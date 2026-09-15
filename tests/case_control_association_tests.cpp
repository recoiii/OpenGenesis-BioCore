#include "biocore/domain/case_control_association.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

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
    require(a.allele_fisher_two_sided_p.has_value() && std::abs(*a.allele_fisher_two_sided_p-0.4857142857142857)<1e-12,"allele Fisher");
    require(a.carrier_fisher_two_sided_p.has_value() && std::abs(*a.carrier_fisher_two_sided_p-1.0)<1e-12,"carrier Fisher");

    AssociationContingencyTable classic{1U,9U,11U,3U};
    require(std::abs(fisher_exact_two_sided(classic)-0.0027594561852200836)<1e-12,"classic Fisher exact");
    const auto classic_or=association_odds_ratio(classic);
    require(classic_or.kind==AssociationOddsRatioKind::finite && std::abs(classic_or.value-(1.0/33.0))<1e-15,"classic OR");

    std::reverse(labels.begin(),labels.end());
    const auto reversed=analyze_case_control(matrix,labels);
    require(reversed.variants[0].allele_table==a.allele_table && reversed.variants[0].carrier_table==a.carrier_table,"label-order determinism");
}
