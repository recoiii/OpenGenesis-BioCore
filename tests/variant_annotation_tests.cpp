#include "biocore/domain/variant_annotation.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace biocore::domain;
void require(bool c){ if(!c) throw std::runtime_error("annotation test failed"); }
ContigTable make_contigs(){ ContigTableBuilder b{ReferenceAssembly::grch38}; b.add_contig("1",{"chr1"}); return b.build(); }
ReferenceDatabaseMetadata meta(std::string id,char sha){ return {std::move(id),"DB","1.0",1U,{ReferenceAssembly::grch38,{}},"file:///db",std::string(64U,sha)}; }
ReferenceDatabaseRecord rec(ContigId c,std::string alt,std::string id,std::string value){ return {{c,9U,10U},{"A",VariantType::unknown,false},{alt,classify_variant("A",alt,false),false},std::move(id),{{"clinical",std::move(value)},{"missing",std::nullopt}}}; }
VariantRecord query(ContigId c){ VariantRecord v; v.locus={c,9U,10U}; v.reference={"A",VariantType::unknown,false}; v.alternates.push_back({"G",VariantType::snv,false}); v.alternates.push_back({"T",VariantType::snv,false}); return v; }
template<class F> void expect_invalid(F&& f){ bool ok=false; try{f();}catch(const std::invalid_argument&){ok=true;} require(ok); }
}
int main(){
 auto ct=make_contigs(); auto chr1=*ct.resolve("1"); auto dbb=ReferenceDatabase::build(meta("b.db",'b'),ct,{rec(chr1,"T","b-t","two")}); auto dba=ReferenceDatabase::build(meta("a.db",'a'),ct,{rec(chr1,"G","a-g","one")}); auto v=query(chr1); ReferenceAssemblyIdentity asm38{ReferenceAssembly::grch38,{}};
 auto r=annotate_variant(asm38,ct,v,{&dbb,&dba});
 require(r.alternates.size()==2U && r.alternates[0].alternate_index==0U && r.alternates[1].alternate_index==1U);
 require(r.alternates[0].allele_annotations.size()==1U && r.alternates[0].allele_annotations[0].record_id=="a-g");
 require(r.alternates[1].allele_annotations.size()==1U && r.alternates[1].allele_annotations[0].record_id=="b-t");
 require(r.alternates[0].allele_annotations[0].attributes[1].value==std::nullopt);
 auto reversed=annotate_variant(asm38,ct,v,{&dba,&dbb});
 require(reversed.alternates[0].allele_annotations[0].provenance.database_id==r.alternates[0].allele_annotations[0].provenance.database_id);
 expect_invalid([&]{ (void)annotate_variant({ReferenceAssembly::grch37,{}},ct,v,{&dba}); });
 expect_invalid([&]{ (void)annotate_variant(asm38,ct,v,{&dba,&dba}); });
 expect_invalid([&]{ (void)annotate_variant(asm38,ct,v,{nullptr}); });
 VariantAnnotationOptions limit{}; limit.maximum_allele_annotations_per_alternate=0U; expect_invalid([&]{(void)annotate_variant(asm38,ct,v,{&dba},limit);});
}
