#include "biocore/domain/case_control_association.hpp"
#include "biocore/domain/variant_annotation.hpp"

#include <sstream>
#include <stdexcept>

namespace { using namespace biocore::domain; void require(bool c,const char*m){if(!c)throw std::runtime_error(m);} }
int main(){
 std::istringstream fasta{">chr1\nAAAAAAAAAAAAAAAAAAAA\n"}; auto genome=ReferenceGenome::from_fasta(fasta,ReferenceAssembly::grch38); ReferenceAssemblyIdentity a{ReferenceAssembly::grch38,{}};
 std::istringstream vcf{
  "##fileformat=VCFv4.3\n##FORMAT=<ID=GT,Number=1,Type=String,Description=\"Genotype\">\n"
  "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tcase-1\tcontrol-1\n"
  "chr1\t10\trs1\tA\tG\t.\tPASS\t.\tGT\t1/1\t0/0\n"};
 auto parsed=ingest_vcf(vcf,genome); ContigTableBuilder tb{ReferenceAssembly::grch38}; tb.add_contig("1",{"chr1"}); auto target=tb.build();
 auto matrix=build_multi_sample_matrix(a,target,{{"cohort",a,&genome.contigs(),&parsed}});
 ReferenceDatabaseMetadata meta{"known","Known","1",1U,a,"file:///known",std::string(64U,'a')}; auto chr=*target.resolve("1"); ReferenceDatabaseRecord known{{chr,9U,10U},{"A",VariantType::unknown,false},{"G",VariantType::snv,false},"known-g",{{"gene",std::string{"GENE1"}}}}; auto db=ReferenceDatabase::build(meta,target,{known});
 auto ann=annotate_variant(a,target,matrix.variant_record(0U),{&db}); require(ann.alternates[0].allele_annotations.size()==1U,"062 annotation still consumes 063 projection");
 auto assoc=analyze_case_control(matrix,{{"case-1",CaseControlPhenotype::case_sample},{"control-1",CaseControlPhenotype::control}}); require(assoc.variants.size()==1U,"064 result count"); require(assoc.variants[0].allele_table==AssociationContingencyTable{2U,0U,0U,2U},"063 to 064 exact dosage integration"); require(assoc.variants[0].allele_odds_ratio.kind==AssociationOddsRatioKind::positive_infinity,"association direction");
}
