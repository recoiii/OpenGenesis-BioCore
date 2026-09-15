#include "biocore/domain/multi_sample_matrix.hpp"

#include <stdexcept>

namespace {
using namespace biocore::domain;
void require(bool c,const char* m){if(!c) throw std::runtime_error(m);} 
template<class E,class F> void expect(F&& f){bool ok=false;try{f();}catch(const E&){ok=true;}require(ok,"expected exception");}
ContigTable ct(){ContigTableBuilder b{ReferenceAssembly::grch38};b.add_contig("1");return b.build();}
VariantRecord v(ContigId c){VariantRecord x;x.locus={c,0,1};x.reference={"A",VariantType::unknown,false};x.alternates.push_back({"G",VariantType::snv,false});return x;}
GenotypeCall gt(){GenotypeCall g;g.allele_indices.push_back(0);g.allele_indices.push_back(1);g.phased_separators.push_back(0);return g;}
}
int main(){
 auto c=ct();auto id=*c.resolve("1");ReferenceAssemblyIdentity a{ReferenceAssembly::grch38,{}};
 VcfIngestionResult x;x.header.sample_names={"S"};CanonicalVariantRecord r;r.variant=v(id);r.samples.resize(1);r.samples[0].genotype_present=true;r.samples[0].genotype=gt();x.records.push_back(r);
 MultiSampleMatrixOptions bad{};bad.maximum_samples=0;expect<std::invalid_argument>([&]{(void)build_multi_sample_matrix(a,c,{{"x",a,&c,&x}},bad);});
 MultiSampleMatrixOptions obs{};obs.maximum_observations=0;expect<std::invalid_argument>([&]{(void)build_multi_sample_matrix(a,c,{{"x",a,&c,&x}},obs);});
 MultiSampleMatrixOptions tight{};tight.maximum_observations=1;auto ok=build_multi_sample_matrix(a,c,{{"x",a,&c,&x}},tight);require(ok.observation_count()==1U,"exact observation bound");

 VcfIngestionResult two_samples=x; two_samples.header.sample_names={"A","B"}; two_samples.records[0].samples.resize(2U); two_samples.records[0].samples[1]=two_samples.records[0].samples[0]; MultiSampleMatrixOptions one_obs{}; one_obs.maximum_observations=1U; expect<std::length_error>([&]{(void)build_multi_sample_matrix(a,c,{{"two",a,&c,&two_samples}},one_obs);});
 VcfIngestionResult dup=x;dup.header.sample_names={"S"};expect<std::invalid_argument>([&]{(void)build_multi_sample_matrix(a,c,{{"x",a,&c,&x},{"y",a,&c,&dup}});});
 expect<std::invalid_argument>([&]{(void)build_multi_sample_matrix(a,c,{{"x",{ReferenceAssembly::grch37,{}},&c,&x}});});
 VcfIngestionResult repeated=x;repeated.records.push_back(repeated.records[0]);expect<std::invalid_argument>([&]{(void)build_multi_sample_matrix(a,c,{{"x",a,&c,&repeated}});});
 expect<std::invalid_argument>([&]{(void)build_multi_sample_matrix(a,c,{{"x",a,nullptr,&x}});});
}
