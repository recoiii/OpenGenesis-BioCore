#include "biocore/domain/multi_sample_matrix.hpp"
#include "biocore/domain/variant_annotation.hpp"

#include <sstream>
#include <stdexcept>

namespace {
using namespace biocore::domain;
void require(bool c,const char* m){if(!c) throw std::runtime_error(m);} 
}
int main(){
 std::istringstream fasta{">chr1\nAAAAAAAAAAAAAAAAAAAA\n"}; auto genome=ReferenceGenome::from_fasta(fasta,ReferenceAssembly::grch38); ReferenceAssemblyIdentity a{ReferenceAssembly::grch38,{}};
 std::istringstream vcf1{
  "##fileformat=VCFv4.3\n##FORMAT=<ID=GT,Number=1,Type=String,Description=\"Genotype\">\n"
  "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tcase-1\n"
  "chr1\t10\trs1\tA\tG\t.\tPASS\t.\tGT\t0/1\n"};
 std::istringstream vcf2{
  "##fileformat=VCFv4.3\n##FORMAT=<ID=GT,Number=1,Type=String,Description=\"Genotype\">\n"
  "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tcontrol-1\n"
  "chr1\t10\trs1b\tA\tG\t.\tPASS\t.\tGT\t1/1\n"};
 auto p1=ingest_vcf(vcf1,genome); auto p2=ingest_vcf(vcf2,genome);
 ContigTableBuilder target_builder{ReferenceAssembly::grch38}; target_builder.add_contig("2",{"chr2"}); target_builder.add_contig("1",{"chr1"}); auto target=target_builder.build();
 auto matrix=build_multi_sample_matrix(a,target,{{"case.vcf",a,&genome.contigs(),&p1},{"control.vcf",a,&genome.contigs(),&p2}});
 require(matrix.variant_count()==1U && matrix.observation_count()==2U,"VCF allele merge");
 auto case_i=*matrix.sample_index("case-1"), control_i=*matrix.sample_index("control-1");
 require(matrix.find_observation(case_i,0U)->alternate_dosage==1U,"case dosage");
 require(matrix.find_observation(control_i,0U)->alternate_dosage==2U,"control dosage");
 ReferenceDatabaseMetadata meta{"known","Known","1",1U,a,"file:///known",std::string(64U,'a')};
 auto target_chr1=*target.resolve("1"); ReferenceDatabaseRecord known{{target_chr1,9U,10U},{"A",VariantType::unknown,false},{"G",VariantType::snv,false},"known-g",{{"label",std::string{"known"}}}};
 auto db=ReferenceDatabase::build(meta,target,{known}); auto ann=annotate_variant(a,target,matrix.variant_record(0U),{&db});
 require(ann.alternates.size()==1U && ann.alternates[0].allele_annotations.size()==1U,"062 annotation compatibility");
}
