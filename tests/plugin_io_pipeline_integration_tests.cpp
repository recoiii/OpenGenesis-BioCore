#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "biocore/application/pipeline_preparation_service.hpp"
#include "biocore/application/worker_launch_request.hpp"
#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/pipeline_definition.hpp"
#include "biocore/infrastructure/filesystem_plugin_registry.hpp"
#include "biocore/infrastructure/json_execution_plan_store.hpp"
#include "biocore/infrastructure/platform_worker_supervisor.hpp"

namespace {
namespace fs=std::filesystem;

[[nodiscard]] fs::path path_from_utf8(std::string_view value){std::u8string s;for(char c:value)s.push_back(static_cast<char8_t>(static_cast<unsigned char>(c)));return fs::path{s};}

class Project final {
public:
    Project(){root_=fs::temp_directory_path()/std::string{"biocore-plugin-io-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())};fs::create_directories(root_/".biocore"/"runtime");fs::create_directories(root_/"outputs");fs::create_directories(root_/"inputs"/"file-1");std::ofstream{root_/"inputs"/"file-1"/"source.txt"}<<"alpha";root_=fs::canonical(root_);}
    ~Project(){std::error_code e;fs::remove_all(root_,e);} const fs::path& root()const{return root_;}
private:fs::path root_;
};

class Repo final:public biocore::application::IManagedFileRepository{
public:
    explicit Repo(const fs::path& root):file_{"file-1","source.txt",biocore::domain::StorageMode::managed_copy,std::string{"/original/source.txt"},(root/"inputs"/"file-1"/"source.txt").string(),std::string{"inputs/file-1/source.txt"},"txt",5,std::nullopt,std::nullopt,std::nullopt,"2026-08-07T00:00:00Z","2026-08-07T00:00:00Z"}{}
    bool add(const biocore::domain::ManagedFile&)override{return false;}
    std::optional<biocore::domain::ManagedFile> find_by_id(std::string_view id)override{return id==file_.id()?std::optional<biocore::domain::ManagedFile>{file_}:std::nullopt;}
    std::optional<biocore::domain::ManagedFile> find_by_relative_project_path(std::string_view)override{return std::nullopt;}
    std::vector<biocore::domain::ManagedFile> list()override{return{file_};}
    bool add_generated_output(const biocore::domain::ManagedFile&, const biocore::application::GeneratedOutputProvenance&) override{return false;}
    bool add_generated_outputs_batch(
        std::span<const biocore::application::GeneratedOutputArtifact>
    ) override { return false; }
    std::optional<biocore::application::GeneratedOutputArtifact> find_generated_output(std::string_view,std::string_view,std::string_view) override{return std::nullopt;}
    std::vector<biocore::application::GeneratedOutputArtifact> list_generated_outputs(std::string_view) override{return{};}
private:biocore::domain::ManagedFile file_;
};

[[nodiscard]] std::vector<biocore::application::WorkerProcessExit> await_exit(biocore::infrastructure::PlatformWorkerSupervisor& supervisor){for(int i=0;i<500;++i){auto e=supervisor.reap_exited();if(!e.empty())return e;std::this_thread::sleep_for(std::chrono::milliseconds{10});}return{};}
}

int main(int argc,const char* const argv[]){
    if(argc!=3){std::cerr<<"Expected worker and plugin root\n";return EXIT_FAILURE;}
    Project project;Repo repo{project.root()};
    biocore::infrastructure::FilesystemPluginRegistry registry{{fs::canonical(path_from_utf8(argv[2]))}};
    const auto report=registry.refresh();if(report.loaded_plugins!=8U||!report.rejected.empty()){std::cerr<<"Plugin discovery failed\n";return EXIT_FAILURE;}
    biocore::infrastructure::JsonExecutionPlanStore plan_store{project.root()};
    biocore::application::PipelinePreparationService preparation{plan_store,registry,repo};
    const biocore::domain::PipelineDefinition definition{2U,"org.biocore.demo.io","IO","1.0.0",{biocore::domain::PipelineStep{"copy","org.biocore.demo.copy", "0.1.0",{},1.0}}};
    biocore::application::PipelineRunBindings bindings{{biocore::application::PipelineStepBindings{
        .step_id="copy",
        .parameters={{"label",biocore::domain::PluginParameterValue{std::string{"beta"}}},{"repeat",biocore::domain::PluginParameterValue{std::int64_t{2}}}},
        .inputs={{"source",biocore::application::ManagedFileInputSource{"file-1"}}}
    }}};
    const auto prepared=preparation.prepare(definition,"job-io",0,bindings);
    biocore::infrastructure::PlatformWorkerSupervisor supervisor{fs::canonical(path_from_utf8(argv[1])),project.root()};
    supervisor.launch(biocore::application::WorkerLaunchRequest{
        .job_id="job-io",.analysis_id=std::nullopt,.pipeline_id=std::string{"org.biocore.demo.io"},.pipeline_version=std::string{"1.0.0"},
        .priority=biocore::domain::JobPriority::normal,.job_revision=0,.execution_plan_path=prepared.snapshot_path
    });
    const auto exits=await_exit(supervisor);
    const fs::path result=project.root()/"outputs"/"job-io--copy--result.out";
    std::ifstream input{result,std::ios::binary};std::string content((std::istreambuf_iterator<char>(input)),{});
    if(exits.size()!=1U||exits.front().exit_code!=0||!exits.front().protocol_issues.empty()||content!="alpha\nbeta\nbeta"){
        std::cerr<<"Plugin I/O pipeline integration tests failed\n";return EXIT_FAILURE;
    }
    std::cout<<"Plugin I/O pipeline integration tests passed\n";return EXIT_SUCCESS;
}
