
find_package(ZLIB REQUIRED)

add_executable(biocore-job-tests job_tests.cpp)
target_link_libraries(
    biocore-job-tests
    PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME domain.job COMMAND biocore-job-tests)

add_executable(biocore-job-service-tests job_service_tests.cpp)
target_link_libraries(
    biocore-job-service-tests
    PRIVATE BioCore::application BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME application.job_service COMMAND biocore-job-service-tests)

add_executable(biocore-sqlite-job-repository-tests sqlite_job_repository_tests.cpp)
target_link_libraries(
    biocore-sqlite-job-repository-tests
    PRIVATE
        BioCore::infrastructure
        SQLite::SQLite3
        BioCore::project_options
        BioCore::project_warnings
        BioCore::sanitizers
)
add_test(NAME infrastructure.sqlite_job_repository COMMAND biocore-sqlite-job-repository-tests)

add_executable(biocore-domain-tests job_status_tests.cpp)
target_link_libraries(
    biocore-domain-tests
    PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME domain.job_status COMMAND biocore-domain-tests)

add_executable(biocore-project-tests project_tests.cpp)
target_link_libraries(
    biocore-project-tests
    PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME domain.project COMMAND biocore-project-tests)

add_executable(biocore-sqlite-project-repository-tests sqlite_project_repository_tests.cpp)
target_link_libraries(
    biocore-sqlite-project-repository-tests
    PRIVATE
        BioCore::infrastructure
        SQLite::SQLite3
        BioCore::project_options
        BioCore::project_warnings
        BioCore::sanitizers
)
add_test(NAME infrastructure.sqlite_project_repository COMMAND biocore-sqlite-project-repository-tests)


add_executable(biocore-project-database-tests project_database_tests.cpp)
target_link_libraries(
    biocore-project-database-tests
    PRIVATE
        BioCore::infrastructure
        SQLite::SQLite3
        BioCore::project_options
        BioCore::project_warnings
        BioCore::sanitizers
)
add_test(NAME infrastructure.project_database COMMAND biocore-project-database-tests)

add_executable(biocore-presentation-tests health_json_tests.cpp)
target_link_libraries(
    biocore-presentation-tests
    PRIVATE BioCore::presentation BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME presentation.health_json COMMAND biocore-presentation-tests)

add_executable(biocore-worker-protocol-tests worker_protocol_tests.cpp)
target_link_libraries(
    biocore-worker-protocol-tests
    PRIVATE BioCore::worker_protocol BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME worker.protocol COMMAND biocore-worker-protocol-tests)

add_test(NAME smoke.biocore_health COMMAND biocore --health)
set_tests_properties(smoke.biocore_health PROPERTIES PASS_REGULAR_EXPRESSION "\\\"status\\\":\\\"healthy\\\"")

add_test(NAME smoke.worker_self_test COMMAND biocore-worker --self-test)
set_tests_properties(smoke.worker_self_test PROPERTIES PASS_REGULAR_EXPRESSION "\\\"protocolVersion\\\":2")

add_executable(biocore-project-service-tests project_service_tests.cpp)
target_link_libraries(
    biocore-project-service-tests
    PRIVATE BioCore::application BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME application.project_service COMMAND biocore-project-service-tests)


add_executable(biocore-filesystem-project-workspace-tests filesystem_project_workspace_tests.cpp)
target_link_libraries(
    biocore-filesystem-project-workspace-tests
    PRIVATE
        BioCore::infrastructure
        SQLite::SQLite3
        BioCore::project_options
        BioCore::project_warnings
        BioCore::sanitizers
)
add_test(NAME infrastructure.filesystem_project_workspace COMMAND biocore-filesystem-project-workspace-tests)

add_executable(biocore-project-runtime-services-tests project_runtime_services_tests.cpp)
target_link_libraries(
    biocore-project-runtime-services-tests
    PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME infrastructure.project_runtime_services COMMAND biocore-project-runtime-services-tests)

add_executable(biocore-project-service-sqlite-integration-tests project_service_sqlite_integration_tests.cpp)
target_link_libraries(
    biocore-project-service-sqlite-integration-tests
    PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME integration.project_service_sqlite COMMAND biocore-project-service-sqlite-integration-tests)

add_executable(
    biocore-project-workspace-service-integration-tests
    project_workspace_service_integration_tests.cpp
)
target_link_libraries(
    biocore-project-workspace-service-integration-tests
    PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(
    NAME integration.project_workspace_service_rollback
    COMMAND biocore-project-workspace-service-integration-tests
)

add_executable(
    biocore-job-service-sqlite-integration-tests
    job_service_sqlite_integration_tests.cpp
)
target_link_libraries(
    biocore-job-service-sqlite-integration-tests
    PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(
    NAME integration.job_service_sqlite
    COMMAND biocore-job-service-sqlite-integration-tests
)

add_executable(biocore-managed-file-tests managed_file_tests.cpp)
target_link_libraries(
    biocore-managed-file-tests
    PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME domain.managed_file COMMAND biocore-managed-file-tests)

add_executable(biocore-managed-file-service-tests managed_file_service_tests.cpp)
target_link_libraries(
    biocore-managed-file-service-tests
    PRIVATE BioCore::application BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME application.managed_file_service COMMAND biocore-managed-file-service-tests)

add_executable(biocore-filesystem-input-file-storage-tests filesystem_input_file_storage_tests.cpp)
target_link_libraries(
    biocore-filesystem-input-file-storage-tests
    PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME infrastructure.filesystem_input_file_storage COMMAND biocore-filesystem-input-file-storage-tests)

add_executable(biocore-sqlite-managed-file-repository-tests sqlite_managed_file_repository_tests.cpp)
target_link_libraries(
    biocore-sqlite-managed-file-repository-tests
    PRIVATE BioCore::infrastructure SQLite::SQLite3 BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME infrastructure.sqlite_managed_file_repository COMMAND biocore-sqlite-managed-file-repository-tests)

add_executable(
    biocore-managed-file-service-sqlite-integration-tests
    managed_file_service_sqlite_integration_tests.cpp
)
target_link_libraries(
    biocore-managed-file-service-sqlite-integration-tests
    PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(
    NAME integration.managed_file_service_sqlite
    COMMAND biocore-managed-file-service-sqlite-integration-tests
)


add_executable(biocore-job-scheduler-tests job_scheduler_tests.cpp)
target_link_libraries(
    biocore-job-scheduler-tests
    PRIVATE BioCore::application BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME application.job_scheduler COMMAND biocore-job-scheduler-tests)

add_executable(
    biocore-job-scheduler-sqlite-integration-tests
    job_scheduler_sqlite_integration_tests.cpp
)
target_link_libraries(
    biocore-job-scheduler-sqlite-integration-tests
    PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(
    NAME integration.job_scheduler_sqlite
    COMMAND biocore-job-scheduler-sqlite-integration-tests
)

add_executable(biocore-worker-process-probe worker_process_probe.cpp)
target_link_libraries(
    biocore-worker-process-probe
    PRIVATE
        BioCore::worker_protocol
        BioCore::project_options
        BioCore::project_warnings
        BioCore::sanitizers
)

add_executable(
    biocore-platform-worker-supervisor-tests
    platform_worker_supervisor_tests.cpp
)
target_link_libraries(
    biocore-platform-worker-supervisor-tests
    PRIVATE
        BioCore::infrastructure
        BioCore::project_options
        BioCore::project_warnings
        BioCore::sanitizers
)
add_test(
    NAME infrastructure.platform_worker_supervisor
    COMMAND
        biocore-platform-worker-supervisor-tests
        $<TARGET_FILE:biocore-worker-process-probe>
        $<TARGET_FILE:biocore-worker>
)
set_tests_properties(infrastructure.platform_worker_supervisor PROPERTIES RUN_SERIAL TRUE)

add_test(
    NAME smoke.worker_launch_arguments
    COMMAND
        biocore-worker
        --job-id smoke-job
        --project-root ${CMAKE_CURRENT_BINARY_DIR}
        --job-revision 0
)

add_executable(
    biocore-windows-process-arguments-tests
    windows_process_arguments_tests.cpp
)
target_link_libraries(
    biocore-windows-process-arguments-tests
    PRIVATE
        BioCore::infrastructure
        BioCore::project_options
        BioCore::project_warnings
        BioCore::sanitizers
)
target_compile_definitions(
    biocore-windows-process-arguments-tests
    PRIVATE BIOCORE_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
add_test(
    NAME infrastructure.windows_process_arguments
    COMMAND biocore-windows-process-arguments-tests
)

add_executable(biocore-worker-event-codec-tests worker_event_codec_tests.cpp)
target_link_libraries(
    biocore-worker-event-codec-tests
    PRIVATE BioCore::worker_protocol BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME worker.event_codec COMMAND biocore-worker-event-codec-tests)

add_executable(biocore-worker-event-ingestion-tests worker_event_ingestion_tests.cpp)
target_link_libraries(
    biocore-worker-event-ingestion-tests
    PRIVATE BioCore::application BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME application.worker_event_ingestion COMMAND biocore-worker-event-ingestion-tests)

add_executable(
    biocore-worker-artifact-batch-ingestion-tests
    worker_artifact_batch_ingestion_tests.cpp
)
target_link_libraries(
    biocore-worker-artifact-batch-ingestion-tests
    PRIVATE BioCore::application BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(
    NAME application.worker_artifact_batch_ingestion
    COMMAND biocore-worker-artifact-batch-ingestion-tests
)

add_executable(
    biocore-worker-event-pipeline-sqlite-integration-tests
    worker_event_pipeline_sqlite_integration_tests.cpp
)
target_link_libraries(
    biocore-worker-event-pipeline-sqlite-integration-tests
    PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(
    NAME integration.worker_event_pipeline_sqlite
    COMMAND biocore-worker-event-pipeline-sqlite-integration-tests $<TARGET_FILE:biocore-worker>
)
set_tests_properties(integration.worker_event_pipeline_sqlite PROPERTIES RUN_SERIAL TRUE)

add_executable(biocore-worker-runtime-tests worker_runtime_tests.cpp)
target_link_libraries(
    biocore-worker-runtime-tests
    PRIVATE BioCore::application BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME application.worker_runtime COMMAND biocore-worker-runtime-tests)

add_executable(
    biocore-worker-runtime-sqlite-integration-tests
    worker_runtime_sqlite_integration_tests.cpp
)
target_link_libraries(
    biocore-worker-runtime-sqlite-integration-tests
    PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_dependencies(biocore-worker-runtime-sqlite-integration-tests biocore-worker biocore-demo-plugin)
add_test(
    NAME integration.worker_runtime_sqlite
    COMMAND biocore-worker-runtime-sqlite-integration-tests $<TARGET_FILE:biocore-worker> ${CMAKE_BINARY_DIR}/plugins
)
set_tests_properties(integration.worker_runtime_sqlite PROPERTIES RUN_SERIAL TRUE)

add_executable(biocore-pipeline-definition-tests pipeline_definition_tests.cpp)
target_link_libraries(
    biocore-pipeline-definition-tests
    PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME domain.pipeline_definition COMMAND biocore-pipeline-definition-tests)

add_executable(biocore-execution-plan-tests execution_plan_tests.cpp)
target_link_libraries(
    biocore-execution-plan-tests
    PRIVATE BioCore::application BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME application.execution_plan COMMAND biocore-execution-plan-tests)

add_executable(biocore-pipeline-document-codec-tests pipeline_document_codec_tests.cpp)
target_link_libraries(
    biocore-pipeline-document-codec-tests
    PRIVATE BioCore::pipeline_protocol BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME pipeline_protocol.document_codec COMMAND biocore-pipeline-document-codec-tests)

add_executable(biocore-json-pipeline-io-tests json_pipeline_io_tests.cpp)
target_link_libraries(
    biocore-json-pipeline-io-tests
    PRIVATE BioCore::infrastructure BioCore::pipeline_protocol BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME infrastructure.json_pipeline_io COMMAND biocore-json-pipeline-io-tests)

add_executable(biocore-slow-plugin-probe slow_plugin_probe.cpp)
target_link_libraries(biocore-slow-plugin-probe PRIVATE BioCore::project_options BioCore::project_warnings BioCore::sanitizers)

add_executable(biocore-pipeline-worker-integration-tests pipeline_worker_integration_tests.cpp)
target_link_libraries(
    biocore-pipeline-worker-integration-tests
    PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(
    NAME integration.pipeline_worker
    COMMAND
        biocore-pipeline-worker-integration-tests
        $<TARGET_FILE:biocore-worker>
        ${PROJECT_BINARY_DIR}/plugins
        $<TARGET_FILE:biocore-slow-plugin-probe>
)
add_dependencies(biocore-pipeline-worker-integration-tests biocore-worker biocore-demo-plugin biocore-slow-plugin-probe)
set_tests_properties(integration.pipeline_worker PROPERTIES RUN_SERIAL TRUE)

add_executable(biocore-plugin-manifest-tests plugin_manifest_tests.cpp)
target_link_libraries(
    biocore-plugin-manifest-tests
    PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME domain.plugin_manifest COMMAND biocore-plugin-manifest-tests)

add_executable(biocore-plugin-document-codec-tests plugin_document_codec_tests.cpp)
target_link_libraries(
    biocore-plugin-document-codec-tests
    PRIVATE BioCore::plugin_protocol BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME plugin_protocol.document_codec COMMAND biocore-plugin-document-codec-tests)

add_executable(biocore-filesystem-plugin-registry-tests filesystem_plugin_registry_tests.cpp)
target_link_libraries(
    biocore-filesystem-plugin-registry-tests
    PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_dependencies(biocore-filesystem-plugin-registry-tests biocore-demo-plugin)
add_test(
    NAME infrastructure.filesystem_plugin_registry
    COMMAND biocore-filesystem-plugin-registry-tests $<TARGET_FILE:biocore-demo-plugin>
)

add_executable(
    biocore-platform-plugin-process-runner-tests
    platform_plugin_process_runner_tests.cpp
)
target_link_libraries(
    biocore-platform-plugin-process-runner-tests
    PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_dependencies(biocore-platform-plugin-process-runner-tests biocore-demo-plugin)
add_test(
    NAME infrastructure.platform_plugin_process_runner
    COMMAND biocore-platform-plugin-process-runner-tests $<TARGET_FILE:biocore-demo-plugin>
)
set_tests_properties(infrastructure.platform_plugin_process_runner PROPERTIES RUN_SERIAL TRUE)

add_executable(biocore-plugin-io-contract-tests plugin_io_contract_tests.cpp)
target_link_libraries(biocore-plugin-io-contract-tests PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers)
add_test(NAME domain.plugin_io_contract COMMAND biocore-plugin-io-contract-tests)

add_executable(biocore-plugin-binding-resolver-tests plugin_binding_resolver_tests.cpp)
target_link_libraries(biocore-plugin-binding-resolver-tests PRIVATE BioCore::application BioCore::project_options BioCore::project_warnings BioCore::sanitizers)
add_test(NAME application.plugin_binding_resolver COMMAND biocore-plugin-binding-resolver-tests)

add_executable(biocore-json-plugin-invocation-store-tests json_plugin_invocation_store_tests.cpp)
target_link_libraries(biocore-json-plugin-invocation-store-tests PRIVATE BioCore::infrastructure BioCore::plugin_protocol BioCore::project_options BioCore::project_warnings BioCore::sanitizers)
add_test(NAME infrastructure.json_plugin_invocation_store COMMAND biocore-json-plugin-invocation-store-tests)

add_executable(biocore-plugin-io-pipeline-integration-tests plugin_io_pipeline_integration_tests.cpp)
target_link_libraries(biocore-plugin-io-pipeline-integration-tests PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers)
add_dependencies(biocore-plugin-io-pipeline-integration-tests biocore-worker biocore-demo-plugin)
add_test(NAME integration.plugin_io_pipeline COMMAND biocore-plugin-io-pipeline-integration-tests $<TARGET_FILE:biocore-worker> ${PROJECT_BINARY_DIR}/plugins)
set_tests_properties(integration.plugin_io_pipeline PROPERTIES RUN_SERIAL TRUE)

add_executable(biocore-output-artifact-service-tests output_artifact_service_tests.cpp)
target_link_libraries(
    biocore-output-artifact-service-tests
    PRIVATE BioCore::application BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME application.output_artifact_service COMMAND biocore-output-artifact-service-tests)

add_executable(
    biocore-filesystem-output-artifact-inspector-tests
    filesystem_output_artifact_inspector_tests.cpp
)
target_link_libraries(
    biocore-filesystem-output-artifact-inspector-tests
    PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(
    NAME infrastructure.filesystem_output_artifact_inspector
    COMMAND biocore-filesystem-output-artifact-inspector-tests
)

add_executable(
    biocore-filesystem-partial-output-cleaner-tests
    filesystem_partial_output_cleaner_tests.cpp
)
target_link_libraries(
    biocore-filesystem-partial-output-cleaner-tests
    PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(
    NAME infrastructure.filesystem_partial_output_cleaner
    COMMAND biocore-filesystem-partial-output-cleaner-tests
)

add_executable(
    biocore-sqlite-generated-artifact-repository-tests
    sqlite_generated_artifact_repository_tests.cpp
)
target_link_libraries(
    biocore-sqlite-generated-artifact-repository-tests
    PRIVATE BioCore::infrastructure SQLite::SQLite3 BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(
    NAME infrastructure.sqlite_generated_artifact_repository
    COMMAND biocore-sqlite-generated-artifact-repository-tests
)

add_executable(
    biocore-plugin-output-artifact-registration-integration-tests
    plugin_output_artifact_registration_integration_tests.cpp
    ${PROJECT_SOURCE_DIR}/apps/fasta_qc_plugin/fasta_statistics.cpp
    ${PROJECT_SOURCE_DIR}/apps/fastq_qc_plugin/fastq_input_stream.cpp
    ${PROJECT_SOURCE_DIR}/apps/fastq_qc_plugin/fastq_statistics.cpp
    ${PROJECT_SOURCE_DIR}/apps/variant_calling_plugin/variant_calling.cpp
    ${PROJECT_SOURCE_DIR}/apps/vcf_qc_plugin/vcf_qc.cpp
)
target_include_directories(
    biocore-plugin-output-artifact-registration-integration-tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/apps/fasta_qc_plugin
        ${PROJECT_SOURCE_DIR}/apps/fastq_qc_plugin
        ${PROJECT_SOURCE_DIR}/apps/variant_calling_plugin
        ${PROJECT_SOURCE_DIR}/apps/vcf_qc_plugin
)
target_link_libraries(
    biocore-plugin-output-artifact-registration-integration-tests
    PRIVATE
        BioCore::infrastructure
        BioCore::project_options
        BioCore::project_warnings
        BioCore::sanitizers
        ZLIB::ZLIB
)
add_dependencies(
    biocore-plugin-output-artifact-registration-integration-tests
    biocore-worker biocore-demo-plugin biocore-fasta-qc-plugin biocore-fastq-qc-plugin biocore-alignment-plugin biocore-alignment-qc-plugin biocore-variant-call-plugin biocore-vcf-qc-plugin biocore-variant-annotation-plugin
)
add_test(
    NAME integration.plugin_output_artifact_registration
    COMMAND
        biocore-plugin-output-artifact-registration-integration-tests
        $<TARGET_FILE:biocore-worker>
        ${CMAKE_BINARY_DIR}/plugins
)
set_tests_properties(integration.plugin_output_artifact_registration PROPERTIES RUN_SERIAL TRUE)


add_executable(biocore-project-recovery-service-tests project_recovery_service_tests.cpp)
target_link_libraries(
    biocore-project-recovery-service-tests
    PRIVATE BioCore::application BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME application.project_recovery_service COMMAND biocore-project-recovery-service-tests)

add_executable(
    biocore-filesystem-quarantine-retention-store-tests
    filesystem_quarantine_retention_store_tests.cpp
)
target_link_libraries(
    biocore-filesystem-quarantine-retention-store-tests
    PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(
    NAME infrastructure.filesystem_quarantine_retention_store
    COMMAND biocore-filesystem-quarantine-retention-store-tests
)

add_executable(
    biocore-project-recovery-sqlite-integration-tests
    project_recovery_sqlite_integration_tests.cpp
)
target_link_libraries(
    biocore-project-recovery-sqlite-integration-tests
    PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(
    NAME integration.project_recovery_sqlite
    COMMAND biocore-project-recovery-sqlite-integration-tests
)


add_executable(
    biocore-project-recovery-crash-window-integration-tests
    project_recovery_crash_window_integration_tests.cpp
)
target_link_libraries(
    biocore-project-recovery-crash-window-integration-tests
    PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(
    NAME integration.project_recovery_crash_window
    COMMAND biocore-project-recovery-crash-window-integration-tests
)

add_executable(biocore-sha256-tests sha256_tests.cpp)
target_link_libraries(
    biocore-sha256-tests
    PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME infrastructure.sha256 COMMAND biocore-sha256-tests)

add_executable(
    biocore-artifact-presentation-service-tests
    artifact_presentation_service_tests.cpp
)
target_link_libraries(
    biocore-artifact-presentation-service-tests
    PRIVATE BioCore::application BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(
    NAME application.artifact_presentation_service
    COMMAND biocore-artifact-presentation-service-tests
)

add_executable(
    biocore-filesystem-artifact-content-access-tests
    filesystem_artifact_content_access_tests.cpp
)
target_link_libraries(
    biocore-filesystem-artifact-content-access-tests
    PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(
    NAME infrastructure.filesystem_artifact_content_access
    COMMAND biocore-filesystem-artifact-content-access-tests
)

add_executable(biocore-artifact-report-tests artifact_report_tests.cpp)
target_link_libraries(
    biocore-artifact-report-tests
    PRIVATE BioCore::presentation BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME presentation.artifact_report COMMAND biocore-artifact-report-tests)

add_executable(biocore-local-api-tests local_api_tests.cpp)
target_link_libraries(
    biocore-local-api-tests
    PRIVATE BioCore::presentation BioCore::application BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME presentation.local_api COMMAND biocore-local-api-tests)

add_executable(biocore-secure-token-tests secure_token_tests.cpp)
target_link_libraries(
    biocore-secure-token-tests
    PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME infrastructure.secure_token COMMAND biocore-secure-token-tests)

add_executable(biocore-local-web-server-tests local_web_server_tests.cpp)
target_link_libraries(
    biocore-local-web-server-tests
    PRIVATE BioCore::presentation BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME presentation.local_web_server COMMAND biocore-local-web-server-tests)

add_test(NAME smoke.biocore_serve_backend COMMAND $<TARGET_FILE:biocore> --serve .)
set_tests_properties(smoke.biocore_serve_backend PROPERTIES WILL_FAIL TRUE)

add_executable(biocore-local-server-bootstrap-tests local_server_bootstrap_tests.cpp)
target_link_libraries(
    biocore-local-server-bootstrap-tests
    PRIVATE biocore-local-bootstrap BioCore::infrastructure BioCore::presentation BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_dependencies(biocore-local-server-bootstrap-tests biocore-worker biocore-demo-plugin)
add_test(
    NAME integration.local_server_bootstrap
    COMMAND biocore-local-server-bootstrap-tests $<TARGET_FILE:biocore-worker> ${CMAKE_BINARY_DIR}/pipelines ${CMAKE_BINARY_DIR}/plugins
)

add_executable(biocore-job-submission-service-tests job_submission_service_tests.cpp)
target_link_libraries(biocore-job-submission-service-tests PRIVATE BioCore::application BioCore::project_options BioCore::project_warnings BioCore::sanitizers)
add_test(NAME application.job_submission_service COMMAND biocore-job-submission-service-tests)

add_executable(biocore-sqlite-prepared-job-store-tests sqlite_prepared_job_store_tests.cpp)
target_link_libraries(biocore-sqlite-prepared-job-store-tests PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers)
add_test(NAME infrastructure.sqlite_prepared_job_store COMMAND biocore-sqlite-prepared-job-store-tests)

add_executable(biocore-filesystem-pipeline-catalog-tests filesystem_pipeline_catalog_tests.cpp)
target_link_libraries(biocore-filesystem-pipeline-catalog-tests PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers)
add_test(NAME infrastructure.filesystem_pipeline_catalog COMMAND biocore-filesystem-pipeline-catalog-tests)
