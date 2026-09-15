#include "biocore/domain/case_control_association.hpp"

#include <stdexcept>

namespace {
using namespace biocore::domain;
void require(bool c,const char* m){if(!c) throw std::runtime_error(m);}
ContigTable ct(){ContigTableBuilder b{ReferenceAssembly::grch38}; b.add_contig("1"); return b.build();}
VariantRecord vr(ContigId c,std::uint64_t s){VariantRecord v; v.locus={c,s,s+1U}; v.reference={"A",VariantType::unknown,false}; v.alternates.push_back({"G",VariantType::snv,false}); return v;}
GenotypeCall gt(std::initializer_list<std::int16_t> a){GenotypeCall g; for(auto x:a) g.allele_indices.push_back(x); for(std::size_t i=1;i<g.allele_indices.size();++i) g.phased_separators.push_back(0U); return g;}
}
int main(){
 auto sc=ct(); auto tc=ct(); auto id=*sc.resolve("1"); ReferenceAssemblyIdentity a{ReferenceAssembly::grch38,{}};
 VcfIngestionResult cases; cases.header.sample_names={"case-observed","case-absent","case-partial"}; CanonicalVariantRecord cr; cr.variant=vr(id,9U); cr.id="case-record"; cr.samples.resize(3U); cr.samples[0].genotype_present=true; cr.samples[0].genotype=gt({0,1}); cr.samples[1].genotype_present=false; cr.samples[2].genotype_present=true; cr.samples[2].genotype=gt({1,missing_allele_index}); cases.records.push_back(cr);
 VcfIngestionResult controls; controls.header.sample_names={"control-observed"}; CanonicalVariantRecord rr; rr.variant=vr(id,9U); rr.id="control-record"; rr.samples.resize(1U); rr.samples[0].genotype_present=true; rr.samples[0].genotype=gt({0,0}); controls.records.push_back(rr);
 auto m=build_multi_sample_matrix(a,tc,{{"cases",a,&sc,&cases},{"controls",a,&sc,&controls}});
 std::vector<CaseControlSampleLabel> labels{{"case-observed",CaseControlPhenotype::case_sample},{"case-absent",CaseControlPhenotype::case_sample},{"case-partial",CaseControlPhenotype::case_sample},{"control-observed",CaseControlPhenotype::control}};
 auto r=analyze_case_control(m,labels); const auto& x=r.variants[0];
 // case-absent has an explicit source row because the source record spans all source samples: genotype_present=false => no_call.
 require(x.case_no_calls==1U && x.case_partial_calls==1U && x.case_complete_calls==1U,"case call states");
 require(x.case_unobserved==0U,"case source-record presence tracked");
 require(x.control_complete_calls==1U,"control complete");
 require(x.allele_table==AssociationContingencyTable{1U,1U,0U,2U},"only complete calls contribute alleles");

 // Add a fourth case from a distinct source with no record for this allele: it must be unobserved, never hom-ref.
 VcfIngestionResult empty; empty.header.sample_names={"case-unobserved"};
 auto m2=build_multi_sample_matrix(a,tc,{{"cases",a,&sc,&cases},{"controls",a,&sc,&controls},{"empty",a,&sc,&empty}});
 labels.push_back({"case-unobserved",CaseControlPhenotype::case_sample});
 auto r2=analyze_case_control(m2,labels); const auto& y=r2.variants[0];
 require(y.case_unobserved==1U,"record absence remains unobserved");
 require(y.allele_table==x.allele_table,"unobserved sample is not inferred hom-ref");

 CaseControlAssociationOptions opts; opts.minimum_complete_case_calls=2U;
 auto gated=analyze_case_control(m2,labels,opts);
 require(!gated.variants[0].allele_fisher_two_sided_p.has_value() && !gated.variants[0].carrier_fisher_two_sided_p.has_value(),"minimum complete-call gate");
}
