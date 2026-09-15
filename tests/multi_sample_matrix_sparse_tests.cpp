#include "biocore/domain/multi_sample_matrix.hpp"

#include <stdexcept>

namespace {
using namespace biocore::domain;
void require(bool c,const char* m){if(!c) throw std::runtime_error(m);} 
ContigTable ct(){ContigTableBuilder b{ReferenceAssembly::grch38}; b.add_contig("1"); return b.build();}
VariantRecord v(ContigId c,std::uint64_t s,std::string alt){VariantRecord x; x.locus={c,s,s+1U}; x.reference={"A",VariantType::unknown,false}; x.alternates.push_back({alt,classify_variant("A",alt,false),false}); return x;}
GenotypeCall partial(){GenotypeCall g; g.allele_indices.push_back(1); g.allele_indices.push_back(missing_allele_index); g.phased_separators.push_back(0); return g;}
}
int main(){
 auto c1=ct(); auto c2=ct(); auto id=*c1.resolve("1"); ReferenceAssemblyIdentity a{ReferenceAssembly::grch38,{}};
 VcfIngestionResult one; one.header.sample_names={"A"}; CanonicalVariantRecord r1; r1.variant=v(id,4U,"G"); r1.samples.resize(1); r1.samples[0].genotype_present=true; r1.samples[0].genotype=partial(); one.records.push_back(r1);
 VcfIngestionResult two; two.header.sample_names={"B"}; CanonicalVariantRecord r2; r2.variant=v(id,8U,"T"); r2.samples.resize(1); r2.samples[0].genotype_present=false; two.records.push_back(r2);
 auto m=build_multi_sample_matrix(a,c2,{{"s1",a,&c1,&one},{"s2",a,&c1,&two}});
 require(m.sample_count()==2U && m.variant_count()==2U && m.observation_count()==2U,"sparse shape");
 auto ai=*m.sample_index("A"), bi=*m.sample_index("B");
 auto* ao=m.find_observation(ai,0U); require(ao && m.call(ao->call_index).state==MultiSampleCallState::partial_call && ao->alternate_dosage==1U && ao->missing_allele_count==1U,"partial call");
 require(m.find_observation(ai,1U)==nullptr,"absence is unobserved");
 require(m.find_observation(bi,0U)==nullptr,"reverse absence is unobserved");
 auto* bo=m.find_observation(bi,1U); require(bo && m.call(bo->call_index).state==MultiSampleCallState::no_call && !m.call(bo->call_index).genotype_present,"explicit no call");
 require(m.sample_observations(ai).size()==1U && m.sample_observations(bi).size()==1U,"row index");
}
