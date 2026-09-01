add_library(biocore-application STATIC)
add_library(BioCore::application ALIAS biocore-application)

target_sources(
    biocore-application
    PRIVATE
        source/artifact_presentation_service.cpp
        source/artifact_presentation_service_error.cpp
        source/build_info.cpp
        source/execution_plan.cpp
        source/job_scheduler.cpp
        source/job_scheduler_error.cpp
        source/job_retry_service.cpp
        source/job_service.cpp
        source/job_service_error.cpp
        source/job_submission_service.cpp
        source/job_submission_service_error.cpp
        source/managed_file_service.cpp
        source/managed_file_service_error.cpp
        source/output_artifact_cleanup_service.cpp
        source/output_artifact_service.cpp
        source/output_artifact_service_error.cpp
        source/pipeline_planner.cpp
        source/plugin_binding_resolver.cpp
        source/pipeline_preparation_service.cpp
        source/project_recovery_service.cpp
        source/project_service.cpp
        source/project_service_error.cpp
        source/project_workspace_error.cpp
        source/worker_event_ingestion_error.cpp
        source/worker_event_ingestion_session.cpp
        source/worker_runtime.cpp
        source/worker_runtime_error.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/biocore/application/artifact_presentation_service.hpp
            include/biocore/application/artifact_presentation_service_error.hpp
            include/biocore/application/build_info.hpp
            include/biocore/application/execution_plan.hpp
            include/biocore/application/generated_output_artifact.hpp
            include/biocore/application/health_snapshot.hpp
            include/biocore/application/i_artifact_content_access.hpp
            include/biocore/application/i_execution_plan_store.hpp
            include/biocore/application/i_id_generator.hpp
            include/biocore/application/i_input_file_storage.hpp
            include/biocore/application/i_monotonic_clock.hpp
            include/biocore/application/i_job_repository.hpp
            include/biocore/application/i_job_submitter.hpp
            include/biocore/application/i_pipeline_catalog.hpp
            include/biocore/application/i_prepared_job_store.hpp
            include/biocore/application/i_managed_file_repository.hpp
            include/biocore/application/i_output_artifact_inspector.hpp
            include/biocore/application/i_partial_output_cleaner.hpp
            include/biocore/application/i_path_canonicalizer.hpp
            include/biocore/application/i_plugin_process_runner.hpp
            include/biocore/application/i_plugin_registry.hpp
            include/biocore/application/i_project_repository.hpp
            include/biocore/application/i_quarantine_retention_store.hpp
            include/biocore/application/i_project_workspace.hpp
            include/biocore/application/i_utc_clock.hpp
            include/biocore/application/i_worker_supervisor.hpp
            include/biocore/application/i_worker_lifecycle_event_sink.hpp
            include/biocore/application/job_scheduler.hpp
            include/biocore/application/job_scheduler_error.hpp
            include/biocore/application/job_retry_service.hpp
            include/biocore/application/job_service.hpp
            include/biocore/application/job_service_error.hpp
            include/biocore/application/job_submission_service.hpp
            include/biocore/application/job_submission_service_error.hpp
            include/biocore/application/managed_file_service.hpp
            include/biocore/application/output_artifact_service.hpp
            include/biocore/application/output_artifact_cleanup_service.hpp
            include/biocore/application/output_artifact_service_error.hpp
            include/biocore/application/managed_file_service_error.hpp
            include/biocore/application/pipeline_bindings.hpp
            include/biocore/application/pipeline_planner.hpp
            include/biocore/application/plugin_binding_resolver.hpp
            include/biocore/application/pipeline_preparation_service.hpp
            include/biocore/application/project_recovery_service.hpp
            include/biocore/application/project_service.hpp
            include/biocore/application/project_service_error.hpp
            include/biocore/application/project_workspace_error.hpp
            include/biocore/application/worker_launch_request.hpp
            include/biocore/application/worker_process.hpp
            include/biocore/application/worker_lifecycle_event.hpp
            include/biocore/application/worker_event_ingestion_error.hpp
            include/biocore/application/worker_event_ingestion_session.hpp
            include/biocore/application/worker_runtime.hpp
            include/biocore/application/worker_runtime_error.hpp
)

target_include_directories(
    biocore-application
    PRIVATE "${PROJECT_BINARY_DIR}/generated"
)

target_link_libraries(
    biocore-application
    PUBLIC BioCore::domain BioCore::project_options
    PRIVATE Threads::Threads BioCore::project_warnings BioCore::sanitizers
)
