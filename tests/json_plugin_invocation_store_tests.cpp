#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "biocore/infrastructure/json_plugin_invocation_store.hpp"
#include "biocore/plugin_protocol/plugin_document_codec.hpp"

namespace {
namespace fs=std::filesystem;

class Project final {
public:
    Project() {
        root_=fs::temp_directory_path()/std::string{"biocore-invocation-store-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())};
        fs::create_directories(root_/".biocore"/"runtime"/"jobs"/"job-1");
        fs::create_directories(root_/"outputs");
        fs::create_directories(root_/"inputs"/"file-1");
        std::ofstream{root_/"inputs"/"file-1"/"source.txt"} << "data";
        root_=fs::canonical(root_);
    }
    ~Project(){std::error_code e;fs::remove_all(root_,e);} const fs::path& root()const{return root_;}
private:fs::path root_;
};

template <typename Function>
[[nodiscard]] bool rejects(Function&& function) {
    try { function(); } catch (const std::exception&) { return true; }
    return false;
}

[[nodiscard]] biocore::application::ExecutionPlanStep step() {
    return {
        .id="copy",.module_id="org.biocore.demo.copy",.plugin_id="org.biocore.demo",.plugin_version="0.1.0",
        .module_type=biocore::domain::PluginModuleType::process,.plugin_root_path="/plugins/demo",.executable_path="/plugins/demo/bin/demo",
        .depends_on={},.normalized_weight=1.0,.parameter_definitions={},.input_definitions={},.output_definitions={},
        .parameters={{"label",biocore::domain::PluginParameterType::string,"hello"}},
        .inputs={{"source",biocore::application::ExecutionInputSourceKind::managed_file,"file-1","txt","inputs/file-1/source.txt"}},
        .outputs={{"result","txt","outputs/job-1--copy--result.out"}},
    };
}
}

int main(){
    Project project; biocore::infrastructure::JsonPluginInvocationStore store{project.root()};
    const std::string path=store.store("job-1",0,step());
    std::ifstream input{path}; std::string json((std::istreambuf_iterator<char>(input)),{});
    const auto doc=biocore::plugin_protocol::parse_plugin_invocation_document(json);
    auto unsafe = step();
    unsafe.outputs.front().relative_project_path = "../escape.out";
    const bool traversal_rejected = rejects([&] {
        static_cast<void>(store.store("job-1", 1, unsafe));
    });
    auto nested = step();
    nested.outputs.front().relative_project_path = "outputs/nested/result.out";
    const bool nested_output_rejected = rejects([&] {
        static_cast<void>(store.store("job-1", 2, nested));
    });
    if(doc.parameters.size()!=1U||doc.inputs.size()!=1U||doc.outputs.size()!=1U||
       !fs::is_regular_file(fs::path{doc.inputs.front().path})||
       fs::path{doc.outputs.front().path}.parent_path()!=project.root()/"outputs"||
       !traversal_rejected||!nested_output_rejected){
        std::cerr<<"JSON plugin invocation store tests failed\n";return EXIT_FAILURE;
    }
    std::cout<<"JSON plugin invocation store tests passed\n";return EXIT_SUCCESS;
}
