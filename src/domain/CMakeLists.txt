add_library(biocore-domain STATIC)
add_library(BioCore::domain ALIAS biocore-domain)

target_sources(
    biocore-domain
    PRIVATE
        source/component_identity.cpp
        source/job.cpp
        source/job_failure.cpp
        source/job_priority.cpp
        source/job_status.cpp
        source/managed_file.cpp
        source/pipeline_definition.cpp
        source/plugin_io_contract.cpp
        source/plugin_manifest.cpp
        source/plugin_module_definition.cpp
        source/plugin_platform.cpp
        source/pipeline_step.cpp
        source/project.cpp
        source/storage_mode.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/biocore/domain/component_identity.hpp
            include/biocore/domain/job.hpp
            include/biocore/domain/job_failure.hpp
            include/biocore/domain/job_priority.hpp
            include/biocore/domain/job_status.hpp
            include/biocore/domain/managed_file.hpp
            include/biocore/domain/pipeline_definition.hpp
            include/biocore/domain/plugin_io_contract.hpp
            include/biocore/domain/plugin_manifest.hpp
            include/biocore/domain/plugin_module_definition.hpp
            include/biocore/domain/plugin_platform.hpp
            include/biocore/domain/pipeline_step.hpp
            include/biocore/domain/project.hpp
            include/biocore/domain/storage_mode.hpp
)

target_link_libraries(
    biocore-domain
    PUBLIC BioCore::project_options
    PRIVATE BioCore::project_warnings BioCore::sanitizers
)
