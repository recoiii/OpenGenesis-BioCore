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

#include "alignment_qc.hpp"
#include "alignment_qc_invocation_contract.hpp"

namespace {

[[nodiscard]] std::string read_invocation(const std::filesystem::path& path) {
    std::error_code error;
    const auto status=std::filesystem::symlink_status(path,error);
    if(error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) throw std::invalid_argument("Alignment QC invocation must be a regular non-symlink file");
    const auto size=std::filesystem::file_size(path,error);
    if(error || size==0U || size>biocore::plugin_protocol::maximum_plugin_invocation_bytes) throw std::invalid_argument("Alignment QC invocation file size is invalid");
    std::ifstream input{path,std::ios::binary}; if(!input) throw std::runtime_error("Unable to open alignment QC invocation");
    std::string content{std::istreambuf_iterator<char>{input},std::istreambuf_iterator<char>{}};
    if(input.bad() || content.size()!=size) throw std::runtime_error("Unable to read alignment QC invocation");
    return content;
}

[[nodiscard]] const biocore::plugin_protocol::PluginInvocationInputDocument& find_input(const biocore::plugin_protocol::PluginInvocationDocument& invocation,const std::string_view port){for(const auto& input:invocation.inputs)if(input.port==port)return input;throw std::logic_error("Validated alignment QC input disappeared");}
[[nodiscard]] const biocore::plugin_protocol::PluginInvocationOutputDocument& find_output(const biocore::plugin_protocol::PluginInvocationDocument& invocation,const std::string_view port){for(const auto& output:invocation.outputs)if(output.port==port)return output;throw std::logic_error("Validated alignment QC output disappeared");}

void validate_document_contract(const biocore::plugin_protocol::PluginInvocationDocument& invocation){
    if(!invocation.parameters.empty()) throw std::invalid_argument("Alignment QC does not accept parameters");
    std::vector<biocore::alignment_qc::InvocationInput> inputs; for(const auto& value:invocation.inputs)inputs.push_back({value.port,value.source_kind,value.file_type});
    std::vector<biocore::alignment_qc::InvocationOutput> outputs; for(const auto& value:invocation.outputs)outputs.push_back({value.port,value.file_type});
    biocore::alignment_qc::validate_invocation_contract(invocation.module_id,inputs,outputs);
}

void write_text(const std::filesystem::path& path,const std::string_view content){std::ofstream output{path,std::ios::binary|std::ios::trunc};if(!output)throw std::runtime_error("Unable to open alignment QC output");output.write(content.data(),static_cast<std::streamsize>(content.size()));output.flush();if(!output)throw std::runtime_error("Unable to write alignment QC output");}

int run(const std::filesystem::path& invocation_path,const std::string_view module_id,const std::string_view step_id){
    const auto invocation=biocore::plugin_protocol::parse_plugin_invocation_document(read_invocation(invocation_path)); validate_document_contract(invocation);
    if(invocation.module_id!=module_id || invocation.step_id!=step_id) throw std::invalid_argument("Alignment QC invocation identity mismatch");
    const auto& alignment=find_input(invocation,"alignment");
    const auto statistics=biocore::alignment_qc::analyze_alignment_file(std::filesystem::path{alignment.path},alignment.file_type);
    write_text(std::filesystem::path{find_output(invocation,"summary").path},biocore::alignment_qc::render_alignment_qc_json(statistics));
    write_text(std::filesystem::path{find_output(invocation,"table").path},biocore::alignment_qc::render_alignment_qc_tsv(statistics));
    return EXIT_SUCCESS;
}
}

int main(const int argc,const char* const argv[]){
    if(argc!=7 || std::string_view{argv[1]}!="--module-id" || std::string_view{argv[3]}!="--step-id" || std::string_view{argv[5]}!="--invocation"){
        std::cerr<<"Usage: biocore-alignment-qc-plugin --module-id <module-id> --step-id <step-id> --invocation <snapshot-path>\n";return 2;
    }
    const std::string_view module_id{argv[2]},step_id{argv[4]}; if(module_id.empty()||step_id.empty())return 2;
    try{return run(std::filesystem::path{argv[6]},module_id,step_id);}catch(const std::exception& e){std::cerr<<"Alignment QC failed: "<<e.what()<<'\n';return 3;}
}
