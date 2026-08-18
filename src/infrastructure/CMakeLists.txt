add_library(biocore-infrastructure STATIC)
add_library(BioCore::infrastructure ALIAS biocore-infrastructure)

target_sources(
    biocore-infrastructure
    PRIVATE
        source/filesystem_artifact_content_access.cpp
        source/filesystem_input_file_storage.cpp
        source/filesystem_output_artifact_inspector.cpp
        source/filesystem_partial_output_cleaner.cpp
        source/filesystem_pipeline_catalog.cpp
        source/filesystem_path_canonicalizer.cpp
        source/filesystem_plugin_registry.cpp
        source/filesystem_project_workspace.cpp
        source/filesystem_quarantine_retention_store.cpp
        source/json_execution_plan_store.cpp
        source/json_plugin_invocation_store.cpp
        source/json_pipeline_definition_loader.cpp
        source/platform_plugin_process_runner.cpp
        source/platform_worker_supervisor.cpp
        source/monotonic_clock.cpp
        source/sha256.cpp
        source/secure_token.cpp
        source/system_clock.cpp
        source/uuid_v4_generator.cpp
        source/windows_process_arguments.cpp
        source/worker_event_mapper.cpp
        source/sqlite/catalog_migration_runner.cpp
        source/sqlite/project_database_initializer.cpp
        source/sqlite/project_migration_runner.cpp
        source/sqlite/sqlite_connection.cpp
        source/sqlite/sqlite_error.cpp
        source/sqlite/sqlite_job_repository.cpp
        source/sqlite/sqlite_managed_file_repository.cpp
        source/sqlite/sqlite_prepared_job_store.cpp
        source/sqlite/sqlite_project_repository.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/biocore/infrastructure/filesystem_artifact_content_access.hpp
            include/biocore/infrastructure/filesystem_input_file_storage.hpp
            include/biocore/infrastructure/filesystem_output_artifact_inspector.hpp
            include/biocore/infrastructure/filesystem_partial_output_cleaner.hpp
            include/biocore/infrastructure/filesystem_pipeline_catalog.hpp
            include/biocore/infrastructure/filesystem_path_canonicalizer.hpp
            include/biocore/infrastructure/filesystem_plugin_registry.hpp
            include/biocore/infrastructure/filesystem_project_workspace.hpp
            include/biocore/infrastructure/filesystem_quarantine_retention_store.hpp
            include/biocore/infrastructure/json_execution_plan_store.hpp
            include/biocore/infrastructure/json_plugin_invocation_store.hpp
            include/biocore/infrastructure/json_pipeline_definition_loader.hpp
            include/biocore/infrastructure/platform_plugin_process_runner.hpp
            include/biocore/infrastructure/platform_worker_supervisor.hpp
            include/biocore/infrastructure/monotonic_clock.hpp
            include/biocore/infrastructure/sha256.hpp
            include/biocore/infrastructure/secure_token.hpp
            include/biocore/infrastructure/system_clock.hpp
            include/biocore/infrastructure/uuid_v4_generator.hpp
            include/biocore/infrastructure/windows_process_arguments.hpp
            include/biocore/infrastructure/worker_event_mapper.hpp
            include/biocore/infrastructure/sqlite/catalog_migration_runner.hpp
            include/biocore/infrastructure/sqlite/project_database_initializer.hpp
            include/biocore/infrastructure/sqlite/project_migration_runner.hpp
            include/biocore/infrastructure/sqlite/sqlite_connection.hpp
            include/biocore/infrastructure/sqlite/sqlite_error.hpp
            include/biocore/infrastructure/sqlite/sqlite_job_repository.hpp
            include/biocore/infrastructure/sqlite/sqlite_managed_file_repository.hpp
            include/biocore/infrastructure/sqlite/sqlite_prepared_job_store.hpp
            include/biocore/infrastructure/sqlite/sqlite_project_repository.hpp
)

target_link_libraries(
    biocore-infrastructure
    PUBLIC BioCore::application BioCore::project_options
    PRIVATE SQLite::SQLite3 BioCore::worker_protocol BioCore::pipeline_protocol BioCore::plugin_protocol BioCore::project_warnings BioCore::sanitizers
)

if(WIN32)
    target_link_libraries(biocore-infrastructure PRIVATE bcrypt)
endif()
