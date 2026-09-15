#include "biocore/domain/multi_sample_matrix.hpp"

#include <algorithm>

#include <stdexcept>
#include <string>

namespace {
using namespace biocore::domain;
void require(bool condition, const char* message){ if(!condition) throw std::runtime_error(message); }
ContigTable source_contigs(){ ContigTableBuilder b{ReferenceAssembly::grch38}; b.add_contig("1",{"chr1"}); return b.build(); }
ContigTable target_contigs(){ ContigTableBuilder b{ReferenceAssembly::grch38}; b.add_contig("2",{"chr2"}); b.add_contig("1",{"chr1"}); return b.build(); }
GenotypeCall gt(std::initializer_list<std::int16_t> alleles){ GenotypeCall g; for(auto a:alleles) g.allele_indices.push_back(a); for(std::size_t i=1;i<g.allele_indices.size();++i) g.phased_separators.push_back(0U); return g; }
VariantRecord variant(ContigId c){ VariantRecord v; v.locus={c,9U,10U}; v.reference={"A",VariantType::unknown,false}; v.alternates.push_back({"G",VariantType::snv,false}); v.alternates.push_back({"T",VariantType::snv,false}); return v; }
}
int main(){
 auto sc=source_contigs(); auto tc=target_contigs(); auto chr1=*sc.resolve("1");
 VcfIngestionResult data; data.header.sample_names={"sample-B","sample-A"};
 CanonicalVariantRecord r; r.variant=variant(chr1); r.id="rs-multi"; r.samples.resize(2U);
 r.samples[0].genotype_present=true; r.samples[0].genotype=gt({1,2});
 r.samples[0].genotype.depth=30U; r.samples[0].genotype.genotype_quality=88U; r.samples[0].extra_format_fields.push_back({"PS",std::int32_t{42}});
 r.samples[1].genotype_present=true; r.samples[1].genotype=gt({0,1});
 data.records.push_back(r);

 VcfIngestionResult single; single.header.sample_names={"sample-C"};
 CanonicalVariantRecord r2; r2.variant.locus={chr1,9U,10U}; r2.variant.reference={"A",VariantType::unknown,false}; r2.variant.alternates.push_back({"G",VariantType::snv,false}); r2.id="rs-bi"; r2.samples.resize(1U); r2.samples[0].genotype_present=true; r2.samples[0].genotype=gt({0,1}); single.records.push_back(r2);

 ReferenceAssemblyIdentity asm38{ReferenceAssembly::grch38,{}};
 std::vector<MultiSampleMatrixSource> sources{{"source-z",asm38,&sc,&data},{"source-a",asm38,&sc,&single}};
 auto matrix=build_multi_sample_matrix(asm38,tc,sources);
 require(matrix.sample_count()==3U,"sample count"); require(matrix.variant_count()==2U,"variant count"); require(matrix.observation_count()==5U,"observation count");
 require(matrix.sample(0).sample_id=="sample-A" && matrix.sample(1).sample_id=="sample-B" && matrix.sample(2).sample_id=="sample-C","sample sort");
 require(matrix.variant(0).locus.contig_id==*tc.resolve("1"),"contig remap");
 auto a=*matrix.sample_index("sample-A"); auto b=*matrix.sample_index("sample-B"); auto c=*matrix.sample_index("sample-C");
 auto* a_g=matrix.find_observation(a,0U); auto* b_g=matrix.find_observation(b,0U); auto* b_t=matrix.find_observation(b,1U); auto* c_g=matrix.find_observation(c,0U);
 require(a_g && a_g->alternate_dosage==1U && a_g->reference_dosage==1U && a_g->other_alternate_dosage==0U,"sample A dosage");
 require(b_g && b_g->alternate_dosage==1U && b_g->other_alternate_dosage==1U && b_g->reference_dosage==0U,"sample B G dosage");
 require(b_t && b_t->alternate_dosage==1U && b_t->other_alternate_dosage==1U,"sample B T dosage");
 require(c_g && c_g->alternate_dosage==1U && c_g->source_alternate_index==0U,"biallelic source projection");
 require(matrix.call(b_g->call_index).genotype.depth==30U && matrix.call(b_g->call_index).genotype.genotype_quality==88U,"genotype metrics preserved");
 require(matrix.call(b_g->call_index).extra_format_fields.size()==1U && matrix.call(b_g->call_index).extra_format_fields[0].key=="PS","extra FORMAT preserved");
 require(matrix.variant_observation_ordinals(0U).size()==3U,"column index");
 require(matrix.record_provenance_count()==2U,"record provenance count");
 require(matrix.record_provenance(matrix.call(b_g->call_index).record_provenance_index).record_id=="rs-multi","record provenance");
 auto record=matrix.variant_record(0U); require(record.alternates.size()==1U && record.alternates[0].sequence=="G","biallelic projection");

 std::reverse(sources.begin(),sources.end()); auto reversed=build_multi_sample_matrix(asm38,tc,sources);
 require(reversed.sample_count()==matrix.sample_count() && reversed.variant_count()==matrix.variant_count() && reversed.observation_count()==matrix.observation_count(),"source-order shape");
 for(std::size_t i=0;i<matrix.sample_count();++i) require(reversed.sample(i)==matrix.sample(i),"source-order samples");
 for(std::size_t i=0;i<matrix.variant_count();++i){ const auto& x=matrix.variant(i); const auto& y=reversed.variant(i); require(x.locus.contig_id==y.locus.contig_id && x.locus.start==y.locus.start && x.locus.end==y.locus.end && x.reference.sequence==y.reference.sequence && x.alternate.sequence==y.alternate.sequence && x.alternate.type==y.alternate.type && x.alternate.symbolic==y.alternate.symbolic,"source-order variants"); }
 for(std::size_t i=0;i<matrix.observation_count();++i){
   const auto& x=matrix.observation(i); const auto& y=reversed.observation(i);
   require(x.sample_index==y.sample_index && x.variant_index==y.variant_index && x.alternate_dosage==y.alternate_dosage && x.call_index==y.call_index,"source-order observations");
 }
}
