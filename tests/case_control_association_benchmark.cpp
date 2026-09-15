#include "biocore/domain/case_control_association.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
using namespace biocore::domain;
GenotypeCall gt(std::int16_t a,std::int16_t b){GenotypeCall g;g.allele_indices.push_back(a);g.allele_indices.push_back(b);g.phased_separators.push_back(0);return g;}
}
int main(){
 constexpr std::size_t samples=100U, variants=10000U;
 ContigTableBuilder sb{ReferenceAssembly::grch38}; sb.add_contig("1"); auto sc=sb.build(); ContigTableBuilder tb{ReferenceAssembly::grch38}; tb.add_contig("1"); auto tc=tb.build(); auto c=*sc.resolve("1"); ReferenceAssemblyIdentity a{ReferenceAssembly::grch38,{}};
 VcfIngestionResult data; data.header.sample_names.reserve(samples); for(std::size_t s=0;s<samples;++s)data.header.sample_names.push_back("S"+std::to_string(s)); data.records.reserve(variants);
 for(std::size_t v=0;v<variants;++v){CanonicalVariantRecord r; r.id="v"+std::to_string(v); r.variant.locus={c,v,v+1U}; r.variant.reference={"A",VariantType::unknown,false}; r.variant.alternates.push_back({"G",VariantType::snv,false}); r.samples.resize(samples); for(std::size_t s=0;s<samples;++s){r.samples[s].genotype_present=true; r.samples[s].genotype=((s+v)%4U==0U)?gt(0,1):gt(0,0);} data.records.push_back(std::move(r));}
 const auto build_start=std::chrono::steady_clock::now(); auto matrix=build_multi_sample_matrix(a,tc,{{"cohort",a,&sc,&data}}); const auto build_end=std::chrono::steady_clock::now();
 std::vector<CaseControlSampleLabel> labels; labels.reserve(samples); for(std::size_t s=0;s<samples;++s)labels.push_back({"S"+std::to_string(s),s<50U?CaseControlPhenotype::case_sample:CaseControlPhenotype::control});
 const auto assoc_start=std::chrono::steady_clock::now(); auto result=analyze_case_control(matrix,labels); const auto assoc_end=std::chrono::steady_clock::now();
 std::uint64_t checksum=0U; for(const auto& x:result.variants){checksum+=x.allele_table.case_exposed+x.allele_table.control_exposed; if(!x.allele_fisher_two_sided_p) throw std::runtime_error("missing benchmark Fisher result");}
 std::cout<<"samples="<<samples<<"\nvariants="<<variants<<"\nobservations="<<matrix.observation_count()<<"\nassociation_tests="<<result.variants.size()*2U<<"\nchecksum="<<checksum<<"\nbuild_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(build_end-build_start).count()<<"\nassociation_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(assoc_end-assoc_start).count()<<"\n";
}
