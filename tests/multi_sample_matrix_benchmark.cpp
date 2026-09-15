#include "biocore/domain/multi_sample_matrix.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

using namespace biocore::domain;
int main(){
 constexpr std::size_t sample_count=100U, variant_count=1000U, scan_rounds=10U;
 ContigTableBuilder b{ReferenceAssembly::grch38}; b.add_contig("1"); auto c=b.build(); auto chr1=*c.resolve("1"); ReferenceAssemblyIdentity a{ReferenceAssembly::grch38,{}};
 VcfIngestionResult data; data.header.sample_names.reserve(sample_count); for(std::size_t s=0;s<sample_count;++s) data.header.sample_names.push_back("S"+std::to_string(s));
 data.records.reserve(variant_count);
 for(std::size_t v=0;v<variant_count;++v){ CanonicalVariantRecord r; r.variant.locus={chr1,v,v+1U}; r.variant.reference={"A",VariantType::unknown,false}; r.variant.alternates.push_back({"G",VariantType::snv,false}); r.samples.resize(sample_count); for(auto& sd:r.samples){ sd.genotype_present=true; sd.genotype.allele_indices.push_back(0); sd.genotype.allele_indices.push_back(1); sd.genotype.phased_separators.push_back(0);} data.records.push_back(std::move(r)); }
 auto start=std::chrono::steady_clock::now(); auto matrix=build_multi_sample_matrix(a,c,{{"bench",a,&c,&data}}); auto built=std::chrono::steady_clock::now();
 std::uint64_t checksum=0; for(std::size_t round=0;round<scan_rounds;++round){ for(std::size_t v=0;v<matrix.variant_count();++v){ for(auto ord:matrix.variant_observation_ordinals(v)){ checksum += matrix.observation(ord).alternate_dosage; }}} auto end=std::chrono::steady_clock::now();
 auto build_ms=std::chrono::duration_cast<std::chrono::milliseconds>(built-start).count(); auto scan_ms=std::chrono::duration_cast<std::chrono::milliseconds>(end-built).count();
 std::cout<<"samples="<<matrix.sample_count()<<"\nvariants="<<matrix.variant_count()<<"\nobservations="<<matrix.observation_count()<<"\nscan_evaluations="<<(matrix.observation_count()*scan_rounds)<<"\nchecksum="<<checksum<<"\nbuild_ms="<<build_ms<<"\nscan_ms="<<scan_ms<<"\n";
 return matrix.sample_count()==sample_count && matrix.variant_count()==variant_count && matrix.observation_count()==sample_count*variant_count && checksum==sample_count*variant_count*scan_rounds ? 0:1;
}
