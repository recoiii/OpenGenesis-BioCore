#include "biocore/domain/case_control_association.hpp"

#include <stdexcept>

namespace { using namespace biocore::domain; void require(bool c,const char*m){if(!c)throw std::runtime_error(m);} GenotypeCall gt(std::initializer_list<std::int16_t>a){GenotypeCall g;for(auto x:a)g.allele_indices.push_back(x);for(std::size_t i=1;i<g.allele_indices.size();++i)g.phased_separators.push_back(0);return g;} }
int main(){
 ContigTableBuilder b{ReferenceAssembly::grch38}; b.add_contig("1"); auto sc=b.build(); ContigTableBuilder b2{ReferenceAssembly::grch38}; b2.add_contig("1"); auto tc=b2.build(); auto c=*sc.resolve("1"); ReferenceAssemblyIdentity a{ReferenceAssembly::grch38,{}};
 VcfIngestionResult data; data.header.sample_names={"case","control"}; CanonicalVariantRecord r; r.id="multi"; r.variant.locus={c,9U,10U}; r.variant.reference={"A",VariantType::unknown,false}; r.variant.alternates.push_back({"G",VariantType::snv,false}); r.variant.alternates.push_back({"T",VariantType::snv,false}); r.samples.resize(2); r.samples[0].genotype_present=true; r.samples[0].genotype=gt({1,2}); r.samples[1].genotype_present=true; r.samples[1].genotype=gt({0,2}); data.records.push_back(r);
 auto m=build_multi_sample_matrix(a,tc,{{"cohort",a,&sc,&data}}); require(m.variant_count()==2U,"two ALT columns");
 auto out=analyze_case_control(m,{{"case",CaseControlPhenotype::case_sample},{"control",CaseControlPhenotype::control}});
 const auto& g=out.variants[0]; const auto& t=out.variants[1];
 require(m.variant(0).alternate.sequence=="G" && m.variant(1).alternate.sequence=="T","deterministic ALT columns");
 require(g.allele_table==AssociationContingencyTable{1U,1U,0U,2U},"G selected vs other alleles");
 require(t.allele_table==AssociationContingencyTable{1U,1U,1U,1U},"T selected vs other alleles");
 require(g.carrier_table==AssociationContingencyTable{1U,0U,0U,1U},"G carriers");
 require(t.carrier_table==AssociationContingencyTable{1U,0U,1U,0U},"T carriers");
 require(g.case_complete_calls==1U && t.case_complete_calls==1U,"shared source call remains complete");
}
