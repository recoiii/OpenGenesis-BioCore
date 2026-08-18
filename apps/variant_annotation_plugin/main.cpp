#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/plugin_protocol/plugin_document_codec.hpp"
#include "variant_annotation.hpp"
#include "variant_annotation_invocation_contract.hpp"

namespace {
constexpr std::uint64_t maximum_vcf_bytes=512ULL*1024ULL*1024ULL;
constexpr std::uint64_t maximum_annotation_bytes=1024ULL*1024ULL*1024ULL;

std::string read_invocation(const std::filesystem::path& path){
    std::error_code ec; const auto st=std::filesystem::symlink_status(path,ec);
    if(ec||std::filesystem::is_symlink(st)||!std::filesystem::is_regular_file(st)) throw std::invalid_argument("Variant-annotation invocation must be a regular non-symlink file");
    const auto size=std::filesystem::file_size(path,ec); if(ec||size==0U||size>biocore::plugin_protocol::maximum_plugin_invocation_bytes) throw std::invalid_argument("Variant-annotation invocation size is invalid");
    std::ifstream in{path,std::ios::binary}; if(!in) throw std::runtime_error("Unable to open variant-annotation invocation");
    std::string text{std::istreambuf_iterator<char>{in},std::istreambuf_iterator<char>{}}; if(in.bad()||text.size()!=size) throw std::runtime_error("Unable to read variant-annotation invocation"); return text;
}
const biocore::plugin_protocol::PluginInvocationInputDocument& input(const biocore::plugin_protocol::PluginInvocationDocument& d,std::string_view port){for(const auto&v:d.inputs)if(v.port==port)return v;throw std::logic_error("Validated annotation input disappeared");}
const biocore::plugin_protocol::PluginInvocationOutputDocument& output(const biocore::plugin_protocol::PluginInvocationDocument& d,std::string_view port){for(const auto&v:d.outputs)if(v.port==port)return v;throw std::logic_error("Validated annotation output disappeared");}
void validate_document(const biocore::plugin_protocol::PluginInvocationDocument& d){
    if(!d.parameters.empty()) throw std::invalid_argument("Variant annotation does not accept parameters");
    std::vector<biocore::variant_annotation::InvocationInput> ins; for(const auto&v:d.inputs)ins.push_back({v.port,v.source_kind,v.file_type});
    std::vector<biocore::variant_annotation::InvocationOutput> outs; for(const auto&v:d.outputs)outs.push_back({v.port,v.file_type});
    biocore::variant_annotation::validate_invocation_contract(d.module_id,ins,outs);
}
void validate_file(const std::filesystem::path& path,std::uint64_t maximum,std::string_view label){std::error_code ec;const auto st=std::filesystem::symlink_status(path,ec);if(ec||std::filesystem::is_symlink(st)||!std::filesystem::is_regular_file(st))throw std::invalid_argument(std::string{label}+" must be a regular non-symlink file");const auto size=std::filesystem::file_size(path,ec);if(ec||size==0U||size>maximum)throw std::invalid_argument(std::string{label}+" size is invalid");}
void write_text(const std::filesystem::path& path,std::string_view text){std::ofstream out{path,std::ios::binary|std::ios::trunc};if(!out)throw std::runtime_error("Unable to open annotation output");out.write(text.data(),static_cast<std::streamsize>(text.size()));out.flush();if(!out)throw std::runtime_error("Unable to write annotation output");}
int run(const std::filesystem::path& invocation_path,std::string_view module_id,std::string_view step_id){
    const auto d=biocore::plugin_protocol::parse_plugin_invocation_document(read_invocation(invocation_path)); validate_document(d);
    if(d.module_id!=module_id||d.step_id!=step_id)throw std::invalid_argument("Variant-annotation invocation identity mismatch");
    const std::filesystem::path vcf_path{input(d,"variants").path}; const std::filesystem::path ann_path{input(d,"annotations").path};
    validate_file(vcf_path,maximum_vcf_bytes,"VCF input"); validate_file(ann_path,maximum_annotation_bytes,"Annotation TSV input");
    std::ifstream ann{ann_path,std::ios::binary}; std::ifstream vcf{vcf_path,std::ios::binary}; if(!ann||!vcf)throw std::runtime_error("Unable to open annotation inputs");
    std::uint64_t rows{}; const auto annotations=biocore::variant_annotation::load_annotation_table(ann,rows); const auto result=biocore::variant_annotation::annotate_vcf(vcf,annotations,rows);
    write_text(std::filesystem::path{output(d,"annotated").path},result.annotated_vcf);
    write_text(std::filesystem::path{output(d,"summary").path},biocore::variant_annotation::render_annotation_json(result.statistics));
    write_text(std::filesystem::path{output(d,"table").path},result.table_tsv);
    write_text(std::filesystem::path{output(d,"report").path},result.report_html);
    return EXIT_SUCCESS;
}
}

int main(int argc,const char*const argv[]){
    if(argc!=7||std::string_view{argv[1]}!="--module-id"||std::string_view{argv[3]}!="--step-id"||std::string_view{argv[5]}!="--invocation"){
        std::cerr<<"Usage: biocore-variant-annotation-plugin --module-id <module-id> --step-id <step-id> --invocation <snapshot-path>\n";return 2;}
    try{return run(std::filesystem::path{argv[6]},argv[2],argv[4]);}catch(const std::exception&e){std::cerr<<"Variant annotation failed: "<<e.what()<<'\n';return 3;}
}
