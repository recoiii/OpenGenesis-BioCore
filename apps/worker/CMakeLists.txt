add_executable(biocore-worker main.cpp)

target_link_libraries(
    biocore-worker
    PRIVATE
        BioCore::application
        BioCore::infrastructure
        BioCore::pipeline_protocol
        BioCore::worker_protocol
        BioCore::project_options
        BioCore::project_warnings
        BioCore::sanitizers
)
