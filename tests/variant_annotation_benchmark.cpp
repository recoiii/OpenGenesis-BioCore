#include "biocore/domain/variant_annotation.hpp"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
using namespace biocore::domain;
int main(){ ContigTableBuilder b{ReferenceAssembly::grch38};b.add_contig("1",{"chr1"});auto ct=b.build();auto c=*ct.resolve("1"); std::vector<ReferenceDatabaseRecord> records;records.reserve(100000U);for(std::uint64_t i=0;i<100000U;++i){records.push_back({{c,i,i+1U},{"A",VariantType::unknown,false},{"G",VariantType::snv,false},"r"+std::to_string(i),{{"x",std::to_string(i)}}});} ReferenceDatabaseMetadata md{"bench","Bench","1",1U,{ReferenceAssembly::grch38,{}},"file:///bench",std::string(64U,'e')};auto db=ReferenceDatabase::build(md,ct,std::move(records)); VariantRecord v;v.reference={"A",VariantType::unknown,false};v.alternates.push_back({"G",VariantType::snv,false}); std::uint64_t hits=0,checksum=0;auto begin=std::chrono::steady_clock::now();for(std::uint64_t i=0;i<1000000U;++i){auto p=i%100000U;v.locus={c,p,p+1U};auto r=annotate_variant({ReferenceAssembly::grch38,{}},ct,v,{&db});hits+=r.alternates[0].allele_annotations.size();checksum+=p;}auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();std::cout<<"records=100000\nqueries=1000000\nhits="<<hits<<"\nchecksum="<<checksum<<"\nelapsed_ms="<<ms<<"\n";return hits==1000000U&&checksum==49999500000ULL?0:1; }
