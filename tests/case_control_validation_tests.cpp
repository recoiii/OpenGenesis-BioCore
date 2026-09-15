#include "biocore/domain/case_control_association.hpp"

#include <limits>
#include <stdexcept>

namespace {
using namespace biocore::domain;
void require(bool c,const char*m){if(!c)throw std::runtime_error(m);}
template<class F> void throws(F&&f,const char*m){try{f();}catch(const std::exception&){return;}throw std::runtime_error(m);}
GenotypeCall gt(std::int16_t a,std::int16_t b){GenotypeCall g;g.allele_indices.push_back(a);g.allele_indices.push_back(b);g.phased_separators.push_back(0);return g;}
MultiSampleMatrix matrix(){
 ContigTableBuilder sb{ReferenceAssembly::grch38}; sb.add_contig("1"); auto sc=sb.build(); ContigTableBuilder tb{ReferenceAssembly::grch38}; tb.add_contig("1"); auto tc=tb.build(); auto c=*sc.resolve("1"); ReferenceAssemblyIdentity a{ReferenceAssembly::grch38,{}};
 VcfIngestionResult d; d.header.sample_names={"case","control"}; CanonicalVariantRecord r; r.id="v"; r.variant.locus={c,0U,1U}; r.variant.reference={"A",VariantType::unknown,false}; r.variant.alternates.push_back({"G",VariantType::snv,false}); r.samples.resize(2); r.samples[0].genotype_present=true; r.samples[0].genotype=gt(0,1); r.samples[1].genotype_present=true; r.samples[1].genotype=gt(0,0); d.records.push_back(r);
 return build_multi_sample_matrix(a,tc,{{"cohort",a,&sc,&d}});
}
}
int main(){
 CaseControlAssociationOptions options; options.maximum_labels=0U; throws([&]{validate_case_control_association_options(options);},"zero label limit");
 options={}; options.maximum_variants=0U; throws([&]{validate_case_control_association_options(options);},"zero variant limit");
 options={}; options.minimum_complete_case_calls=0U; throws([&]{validate_case_control_association_options(options);},"zero case minimum");
 options={}; options.maximum_fisher_table_states=0U; throws([&]{validate_case_control_association_options(options);},"zero Fisher state limit");

 AssociationContingencyTable zero{0U,2U,3U,4U}; require(association_odds_ratio(zero).kind==AssociationOddsRatioKind::zero,"zero OR");
 AssociationContingencyTable inf{2U,0U,3U,4U}; require(association_odds_ratio(inf).kind==AssociationOddsRatioKind::positive_infinity,"infinite OR");
 AssociationContingencyTable undef{0U,0U,3U,4U}; require(association_odds_ratio(undef).kind==AssociationOddsRatioKind::undefined,"undefined OR");
 require(fisher_exact_two_sided({0U,0U,0U,0U})==1.0,"degenerate Fisher");
 throws([&]{ (void)fisher_exact_two_sided({5U,5U,5U,5U},3U); },"Fisher support bound");
 throws([&]{ (void)fisher_exact_two_sided({std::numeric_limits<std::uint64_t>::max(),1U,0U,0U}); },"margin overflow");

 MultiSampleMatrix empty; throws([&]{ (void)analyze_case_control(empty,{}); },"empty matrix rejected");
 auto m=matrix();
 const std::vector<CaseControlSampleLabel> valid{{"case",CaseControlPhenotype::case_sample},{"control",CaseControlPhenotype::control}};
 require(analyze_case_control(m,valid).variants.size()==1U,"valid labels");
 throws([&]{ (void)analyze_case_control(m,{{"case",CaseControlPhenotype::case_sample}}); },"incomplete labels rejected");
 throws([&]{ (void)analyze_case_control(m,{{"case",CaseControlPhenotype::case_sample},{"case",CaseControlPhenotype::control}}); },"duplicate labels rejected");
 throws([&]{ (void)analyze_case_control(m,{{"case",CaseControlPhenotype::case_sample},{"control",CaseControlPhenotype::case_sample}}); },"single phenotype group rejected");
 throws([&]{ (void)analyze_case_control(m,{{"case",CaseControlPhenotype::case_sample},{"control",static_cast<CaseControlPhenotype>(99)}}); },"invalid phenotype enum rejected");
 options={}; options.maximum_labels=1U; throws([&]{ (void)analyze_case_control(m,valid,options); },"label bound enforced");
 options={}; options.maximum_fisher_table_states=1U; throws([&]{ (void)analyze_case_control(m,valid,options); },"Fisher bound propagated");
}
