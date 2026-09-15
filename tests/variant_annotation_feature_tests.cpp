#include "biocore/domain/variant_annotation.hpp"
#include <sstream>
#include <stdexcept>
namespace { using namespace biocore::domain; void require(bool c){if(!c)throw std::runtime_error("feature annotation test failed");} ContigTable ct(){ContigTableBuilder b{ReferenceAssembly::grch38};b.add_contig("1",{"chr1"});return b.build();} ReferenceDatabaseMetadata m(){return {"genes","Genes","2026",2U,{ReferenceAssembly::grch38,{}},"file:///genes.gtf",std::string(64U,'c')};}}
int main(){
 auto table=ct(); std::istringstream gtf{"chr1\tens\tgene\t5\t20\t.\t+\t.\tgene_id \"G1\"; tag \"basic\"; tag \"MANE\";\nchr1\tens\texon\t10\t12\t.\t+\t.\tgene_id \"G1\"; transcript_id \"T1\";\n"}; auto db=ReferenceDatabase::from_gtf(gtf,m(),table); auto c=*table.resolve("1"); VariantRecord v;v.locus={c,9U,10U};v.reference={"A",VariantType::unknown,false};v.alternates.push_back({"G",VariantType::snv,false}); auto r=annotate_variant({ReferenceAssembly::grch38,{}},table,v,{&db}); require(r.alternates[0].feature_annotations.size()==2U); require(r.alternates[0].feature_annotations[0].feature_type=="gene"); require(r.alternates[0].feature_annotations[0].attributes.size()==3U); require(r.alternates[0].feature_annotations[0].provenance.schema_version==2U);
 VariantAnnotationOptions no_features{}; no_features.include_feature_annotations=false; auto n=annotate_variant({ReferenceAssembly::grch38,{}},table,v,{&db},no_features); require(n.alternates[0].feature_annotations.empty());
 VariantAnnotationOptions tiny{}; tiny.maximum_feature_annotations_per_alternate=1U; bool limited=false; try{(void)annotate_variant({ReferenceAssembly::grch38,{}},table,v,{&db},tiny);}catch(const std::length_error&){limited=true;} require(limited);
}
