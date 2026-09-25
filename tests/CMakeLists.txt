
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

add_executable(biocore-workflow-model-tests workflow_model_tests.cpp)
target_link_libraries(
    biocore-workflow-model-tests
    PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME domain.workflow_model COMMAND biocore-workflow-model-tests)

add_executable(biocore-workflow-document-codec-tests workflow_document_codec_tests.cpp)
target_link_libraries(
    biocore-workflow-document-codec-tests
    PRIVATE BioCore::pipeline_protocol BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME pipeline_protocol.workflow_document_codec COMMAND biocore-workflow-document-codec-tests)

add_executable(biocore-workflow-dag-tests workflow_dag_tests.cpp)
target_link_libraries(
    biocore-workflow-dag-tests
    PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME domain.workflow_dag.valid COMMAND biocore-workflow-dag-tests valid)
add_test(NAME domain.workflow_dag.deterministic COMMAND biocore-workflow-dag-tests deterministic)
add_test(NAME domain.workflow_dag.missing_node COMMAND biocore-workflow-dag-tests missing-node)
add_test(NAME domain.workflow_dag.missing_port COMMAND biocore-workflow-dag-tests missing-port)
add_test(NAME domain.workflow_dag.duplicate_connection COMMAND biocore-workflow-dag-tests duplicate)
add_test(NAME domain.workflow_dag.self_edge COMMAND biocore-workflow-dag-tests self-edge)
add_test(NAME domain.workflow_dag.cycle COMMAND biocore-workflow-dag-tests cycle)
add_test(NAME domain.workflow_dag.disconnected COMMAND biocore-workflow-dag-tests disconnected)

add_executable(biocore-workflow-binding-resolver-tests workflow_binding_resolver_tests.cpp)
target_link_libraries(
    biocore-workflow-binding-resolver-tests
    PRIVATE BioCore::application BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME application.workflow_binding.valid COMMAND biocore-workflow-binding-resolver-tests valid)
add_test(NAME application.workflow_binding.deterministic COMMAND biocore-workflow-binding-resolver-tests deterministic)
add_test(NAME application.workflow_binding.module_contract COMMAND biocore-workflow-binding-resolver-tests module-contract)
add_test(NAME application.workflow_binding.edge_type COMMAND biocore-workflow-binding-resolver-tests edge-type)
add_test(NAME application.workflow_binding.parameter_reference COMMAND biocore-workflow-binding-resolver-tests parameter-reference)
add_test(NAME application.workflow_binding.parameter_invalid COMMAND biocore-workflow-binding-resolver-tests parameter-invalid)
add_test(NAME application.workflow_binding.parameter_required COMMAND biocore-workflow-binding-resolver-tests parameter-required)
add_test(NAME application.workflow_binding.resource_propagation COMMAND biocore-workflow-binding-resolver-tests resource-propagation)
add_test(NAME application.workflow_binding.resource_invalid COMMAND biocore-workflow-binding-resolver-tests resource-invalid)
add_test(NAME application.workflow_binding.required_input COMMAND biocore-workflow-binding-resolver-tests required-input)
add_test(NAME application.workflow_binding.binding_conflict COMMAND biocore-workflow-binding-resolver-tests binding-conflict)
add_test(NAME application.workflow_binding.version COMMAND biocore-workflow-binding-resolver-tests version)

add_executable(biocore-workflow-condition-evaluator-tests workflow_condition_evaluator_tests.cpp)
target_link_libraries(
    biocore-workflow-condition-evaluator-tests
    PRIVATE BioCore::application BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME application.workflow_condition.numeric COMMAND biocore-workflow-condition-evaluator-tests numeric)
add_test(NAME application.workflow_condition.status COMMAND biocore-workflow-condition-evaluator-tests status)
add_test(NAME application.workflow_condition.artifact COMMAND biocore-workflow-condition-evaluator-tests artifact)
add_test(NAME application.workflow_condition.exit_branch COMMAND biocore-workflow-condition-evaluator-tests exit-branch)
add_test(NAME application.workflow_condition.deferred COMMAND biocore-workflow-condition-evaluator-tests deferred)
add_test(NAME application.workflow_condition.required_block COMMAND biocore-workflow-condition-evaluator-tests required-block)
add_test(NAME application.workflow_condition.optional_skip COMMAND biocore-workflow-condition-evaluator-tests optional-skip)
add_test(NAME application.workflow_condition.source_block COMMAND biocore-workflow-condition-evaluator-tests source-block)
add_test(NAME application.workflow_condition.control_cycle COMMAND biocore-workflow-condition-evaluator-tests control-cycle)
add_test(NAME application.workflow_condition.duplicate_rule COMMAND biocore-workflow-condition-evaluator-tests duplicate-rule)
add_test(NAME application.workflow_condition.deterministic COMMAND biocore-workflow-condition-evaluator-tests deterministic)
add_test(NAME application.workflow_condition.invalid_context COMMAND biocore-workflow-condition-evaluator-tests invalid-context)

add_executable(biocore-workflow-branch-decision-codec-tests workflow_branch_decision_codec_tests.cpp)
target_link_libraries(
    biocore-workflow-branch-decision-codec-tests
    PRIVATE BioCore::pipeline_protocol BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME pipeline_protocol.workflow_branch_decision.round_trip COMMAND biocore-workflow-branch-decision-codec-tests round-trip)
add_test(NAME pipeline_protocol.workflow_branch_decision.deterministic COMMAND biocore-workflow-branch-decision-codec-tests deterministic)
add_test(NAME pipeline_protocol.workflow_branch_decision.invalid COMMAND biocore-workflow-branch-decision-codec-tests invalid)

add_executable(biocore-workflow-checkpoint-tests workflow_checkpoint_tests.cpp)
target_link_libraries(
    biocore-workflow-checkpoint-tests
    PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME domain.workflow_checkpoint.valid COMMAND biocore-workflow-checkpoint-tests valid)
add_test(NAME domain.workflow_checkpoint.invalid COMMAND biocore-workflow-checkpoint-tests invalid)

add_executable(biocore-workflow-resume-planner-tests workflow_resume_planner_tests.cpp)
target_link_libraries(
    biocore-workflow-resume-planner-tests
    PRIVATE BioCore::application BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME application.workflow_resume.initialize COMMAND biocore-workflow-resume-planner-tests initialize)
add_test(NAME application.workflow_resume.reuse COMMAND biocore-workflow-resume-planner-tests reuse)
add_test(NAME application.workflow_resume.corrupt COMMAND biocore-workflow-resume-planner-tests corrupt)
add_test(NAME application.workflow_resume.cascade COMMAND biocore-workflow-resume-planner-tests cascade)
add_test(NAME application.workflow_resume.missing_output COMMAND biocore-workflow-resume-planner-tests missing-output)
add_test(NAME application.workflow_resume.failed COMMAND biocore-workflow-resume-planner-tests failed)
add_test(NAME application.workflow_resume.interrupted COMMAND biocore-workflow-resume-planner-tests interrupted)
add_test(NAME application.workflow_resume.running COMMAND biocore-workflow-resume-planner-tests running)
add_test(NAME application.workflow_resume.exhausted COMMAND biocore-workflow-resume-planner-tests exhausted)
add_test(NAME application.workflow_resume.required_block COMMAND biocore-workflow-resume-planner-tests required-block)
add_test(NAME application.workflow_resume.optional_continue COMMAND biocore-workflow-resume-planner-tests optional-continue)
add_test(NAME application.workflow_resume.reevaluate COMMAND biocore-workflow-resume-planner-tests reevaluate)
add_test(NAME application.workflow_resume.manifest_errors COMMAND biocore-workflow-resume-planner-tests manifest-errors)
add_test(NAME application.workflow_resume.policy_errors COMMAND biocore-workflow-resume-planner-tests policy-errors)

add_executable(
    biocore-filesystem-workflow-checkpoint-artifact-verifier-tests
    filesystem_workflow_checkpoint_artifact_verifier_tests.cpp
)
target_link_libraries(
    biocore-filesystem-workflow-checkpoint-artifact-verifier-tests
    PRIVATE BioCore::infrastructure BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME infrastructure.workflow_checkpoint_artifact.valid COMMAND biocore-filesystem-workflow-checkpoint-artifact-verifier-tests valid)
add_test(NAME infrastructure.workflow_checkpoint_artifact.hash COMMAND biocore-filesystem-workflow-checkpoint-artifact-verifier-tests hash)
add_test(NAME infrastructure.workflow_checkpoint_artifact.size COMMAND biocore-filesystem-workflow-checkpoint-artifact-verifier-tests size)
add_test(NAME infrastructure.workflow_checkpoint_artifact.traversal COMMAND biocore-filesystem-workflow-checkpoint-artifact-verifier-tests traversal)

add_executable(biocore-workflow-state-service-tests workflow_state_service_tests.cpp)
target_link_libraries(
    biocore-workflow-state-service-tests
    PRIVATE BioCore::application BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME application.workflow_state.create_update COMMAND biocore-workflow-state-service-tests create-update)
add_test(NAME application.workflow_state.conflict COMMAND biocore-workflow-state-service-tests conflict)
add_test(NAME application.workflow_state.identity COMMAND biocore-workflow-state-service-tests identity)
add_test(NAME application.workflow_state.recovery_running COMMAND biocore-workflow-state-service-tests recovery-running)
add_test(NAME application.workflow_state.recovery_idempotent COMMAND biocore-workflow-state-service-tests recovery-idempotent)
add_test(NAME application.workflow_state.recovery_concurrent COMMAND biocore-workflow-state-service-tests recovery-concurrent)

add_executable(biocore-sqlite-workflow-state-store-tests sqlite_workflow_state_store_tests.cpp)
target_link_libraries(
    biocore-sqlite-workflow-state-store-tests
    PRIVATE BioCore::infrastructure SQLite::SQLite3 BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME infrastructure.sqlite_workflow_state.round_trip COMMAND biocore-sqlite-workflow-state-store-tests round-trip)
add_test(NAME infrastructure.sqlite_workflow_state.update COMMAND biocore-sqlite-workflow-state-store-tests update)
add_test(NAME infrastructure.sqlite_workflow_state.immutable COMMAND biocore-sqlite-workflow-state-store-tests immutable)
add_test(NAME infrastructure.sqlite_workflow_state.list COMMAND biocore-sqlite-workflow-state-store-tests list)
add_test(NAME infrastructure.sqlite_workflow_state.constraints COMMAND biocore-sqlite-workflow-state-store-tests constraints)

add_executable(
    biocore-workflow-state-recovery-sqlite-integration-tests
    workflow_state_recovery_sqlite_integration_tests.cpp
)
target_link_libraries(
    biocore-workflow-state-recovery-sqlite-integration-tests
    PRIVATE BioCore::infrastructure BioCore::application BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME integration.workflow_state_recovery.reopen COMMAND biocore-workflow-state-recovery-sqlite-integration-tests reopen)
add_test(NAME integration.workflow_state_recovery.verified_completed COMMAND biocore-workflow-state-recovery-sqlite-integration-tests verified-completed)

add_executable(biocore-workflow-template-tests workflow_template_tests.cpp)
target_link_libraries(
    biocore-workflow-template-tests
    PRIVATE BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME domain.workflow_template.valid COMMAND biocore-workflow-template-tests valid)
add_test(NAME domain.workflow_template.invalid COMMAND biocore-workflow-template-tests invalid)

add_executable(
    biocore-workflow-template-document-codec-tests
    workflow_template_document_codec_tests.cpp
)
target_link_libraries(
    biocore-workflow-template-document-codec-tests
    PRIVATE BioCore::pipeline_protocol BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME pipeline_protocol.workflow_template.round_trip COMMAND biocore-workflow-template-document-codec-tests round-trip)
add_test(NAME pipeline_protocol.workflow_template.deterministic COMMAND biocore-workflow-template-document-codec-tests deterministic)
add_test(NAME pipeline_protocol.workflow_template.invalid COMMAND biocore-workflow-template-document-codec-tests invalid)

add_executable(
    biocore-workflow-template-instantiation-service-tests
    workflow_template_instantiation_service_tests.cpp
)
target_link_libraries(
    biocore-workflow-template-instantiation-service-tests
    PRIVATE BioCore::application BioCore::domain BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME application.workflow_template.exact_version COMMAND biocore-workflow-template-instantiation-service-tests exact-version)
add_test(NAME application.workflow_template.metadata COMMAND biocore-workflow-template-instantiation-service-tests metadata)
add_test(NAME application.workflow_template.override COMMAND biocore-workflow-template-instantiation-service-tests override)
add_test(NAME application.workflow_template.errors COMMAND biocore-workflow-template-instantiation-service-tests errors)

add_executable(
    biocore-filesystem-workflow-template-catalog-tests
    filesystem_workflow_template_catalog_tests.cpp
)
target_link_libraries(
    biocore-filesystem-workflow-template-catalog-tests
    PRIVATE BioCore::infrastructure BioCore::pipeline_protocol BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME infrastructure.workflow_template_catalog.exact COMMAND biocore-filesystem-workflow-template-catalog-tests exact)
add_test(NAME infrastructure.workflow_template_catalog.duplicate COMMAND biocore-filesystem-workflow-template-catalog-tests duplicate)
add_test(NAME infrastructure.workflow_template_catalog.snapshot COMMAND biocore-filesystem-workflow-template-catalog-tests snapshot)

add_executable(
    biocore-workflow-builder-frontend-contract-tests
    workflow_builder_frontend_contract_tests.cpp
)
target_link_libraries(
    biocore-workflow-builder-frontend-contract-tests
    PRIVATE BioCore::presentation BioCore::infrastructure
            BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
target_compile_definitions(
    biocore-workflow-builder-frontend-contract-tests
    PRIVATE BIOCORE_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
)
add_test(NAME presentation.workflow_builder.surface COMMAND biocore-workflow-builder-frontend-contract-tests surface)
add_test(NAME presentation.workflow_builder.safe_rendering COMMAND biocore-workflow-builder-frontend-contract-tests safe-rendering)
add_test(NAME presentation.workflow_builder.scope COMMAND biocore-workflow-builder-frontend-contract-tests scope)
add_test(NAME presentation.workflow_builder.asset_store COMMAND biocore-workflow-builder-frontend-contract-tests asset-store)
add_test(NAME integration.workflow_builder.bundled_template COMMAND biocore-workflow-builder-frontend-contract-tests bundled-template)
add_test(NAME presentation.workflow_execution.surface COMMAND biocore-workflow-builder-frontend-contract-tests execution-surface)
add_test(NAME presentation.workflow_execution.safe_rendering COMMAND biocore-workflow-builder-frontend-contract-tests execution-safe-rendering)
add_test(NAME presentation.workflow_execution.api_contract COMMAND biocore-workflow-builder-frontend-contract-tests execution-api)
add_test(NAME presentation.workflow_execution.scope COMMAND biocore-workflow-builder-frontend-contract-tests execution-scope)

add_executable(
    biocore-v04-release-compatibility-tests
    v04_release_compatibility_tests.cpp
)
target_link_libraries(
    biocore-v04-release-compatibility-tests
    PRIVATE
        BioCore::application
        BioCore::infrastructure
        SQLite::SQLite3
        BioCore::project_options
        BioCore::project_warnings
        BioCore::sanitizers
)
target_compile_definitions(
    biocore-v04-release-compatibility-tests
    PRIVATE BIOCORE_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
)
add_test(NAME release.v04.v03_schema_upgrade COMMAND biocore-v04-release-compatibility-tests v03-schema-upgrade)
add_test(NAME release.v04.v03_assets COMMAND biocore-v04-release-compatibility-tests v03-assets)
add_test(NAME release.v04.identity COMMAND biocore-v04-release-compatibility-tests identity)

add_executable(
    biocore-workflow-engine-v04-e2e-tests
    workflow_engine_v04_e2e_tests.cpp
)
target_link_libraries(
    biocore-workflow-engine-v04-e2e-tests
    PRIVATE BioCore::application BioCore::infrastructure BioCore::pipeline_protocol
            BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME release.v04.workflow_engine_e2e COMMAND biocore-workflow-engine-v04-e2e-tests)

add_executable(
    biocore-workflow-execution-workspace-service-tests
    workflow_execution_workspace_service_tests.cpp
)
target_link_libraries(
    biocore-workflow-execution-workspace-service-tests
    PRIVATE BioCore::application BioCore::domain
            BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)
add_test(NAME application.workflow_execution_workspace.create COMMAND biocore-workflow-execution-workspace-service-tests create)
add_test(NAME application.workflow_execution_workspace.completed_reuse COMMAND biocore-workflow-execution-workspace-service-tests completed-reuse)
add_test(NAME application.workflow_execution_workspace.branch_overlay COMMAND biocore-workflow-execution-workspace-service-tests branch-overlay)
add_test(NAME application.workflow_execution_workspace.deterministic_list COMMAND biocore-workflow-execution-workspace-service-tests deterministic-list)

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
