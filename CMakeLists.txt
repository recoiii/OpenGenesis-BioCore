cmake_minimum_required(VERSION 3.25)

project(
    OpenGenesisBioCore
    VERSION 0.2.0
    DESCRIPTION "OpenGenesis-BioCore local-first modular bioinformatics platform"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CTest)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(BioCoreOptions)

biocore_define_project_options()

include(BioCoreWarnings)
include(BioCoreSanitizers)

find_package(SQLite3 3.35 REQUIRED)
find_package(Threads REQUIRED)
find_package(Drogon CONFIG QUIET)
if(BIOCORE_REQUIRE_DROGON AND NOT Drogon_FOUND)
    message(FATAL_ERROR "BIOCORE_REQUIRE_DROGON=ON but Drogon was not found")
endif()
if(Drogon_FOUND)
    message(STATUS "OpenGenesis-BioCore local web server backend: Drogon")
else()
    message(STATUS "OpenGenesis-BioCore local web server backend: unavailable (Drogon not found)")
endif()

set(BIOCORE_VERSION_SUFFIX "dev" CACHE STRING "Semantic version suffix; empty for final release")
if(BIOCORE_VERSION_SUFFIX STREQUAL "")
    set(BIOCORE_VERSION_STRING "${PROJECT_VERSION}")
else()
    set(BIOCORE_VERSION_STRING "${PROJECT_VERSION}-${BIOCORE_VERSION_SUFFIX}")
endif()

file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/pipelines/" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/pipelines")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/frontend/" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/frontend")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_config.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/biocore/build_config.hpp"
    @ONLY
)

add_subdirectory(src/domain)
add_subdirectory(src/application)
add_subdirectory(src/worker_protocol)
add_subdirectory(src/pipeline_protocol)
add_subdirectory(src/plugin_protocol)
add_subdirectory(src/infrastructure)
add_subdirectory(src/presentation)
add_subdirectory(apps/biocore)
add_subdirectory(apps/worker)
add_subdirectory(apps/demo_plugin)
add_subdirectory(apps/fasta_qc_plugin)
add_subdirectory(apps/fastq_qc_plugin)
add_subdirectory(apps/alignment_plugin)
add_subdirectory(apps/alignment_qc_plugin)
add_subdirectory(apps/variant_calling_plugin)
add_subdirectory(apps/vcf_qc_plugin)
add_subdirectory(apps/variant_annotation_plugin)

if(BIOCORE_BUILD_TESTS)
    add_subdirectory(tests)

    add_executable(
        biocore-project-database-guard-tests
        tests/project_database_guard_tests.cpp
    )
    target_link_libraries(
        biocore-project-database-guard-tests
        PRIVATE BioCore::infrastructure BioCore::project_warnings BioCore::sanitizers
    )
    add_test(
        NAME infrastructure.project_database_guard
        COMMAND biocore-project-database-guard-tests
    )

    add_executable(
        biocore-job-failure-diagnostics-tests
        tests/job_failure_diagnostics_tests.cpp
    )
    target_link_libraries(
        biocore-job-failure-diagnostics-tests
        PRIVATE BioCore::infrastructure BioCore::project_warnings BioCore::sanitizers
    )
    add_test(
        NAME integration.job_failure_diagnostics
        COMMAND biocore-job-failure-diagnostics-tests
    )

    add_executable(
        biocore-job-retry-semantics-tests
        tests/job_retry_semantics_tests.cpp
    )
    target_link_libraries(
        biocore-job-retry-semantics-tests
        PRIVATE BioCore::infrastructure BioCore::project_warnings BioCore::sanitizers
    )
    add_test(
        NAME integration.job_retry_semantics
        COMMAND biocore-job-retry-semantics-tests
    )

    add_executable(
        biocore-managed-file-integrity-tests
        tests/managed_file_integrity_tests.cpp
    )
    target_link_libraries(
        biocore-managed-file-integrity-tests
        PRIVATE BioCore::infrastructure BioCore::project_warnings BioCore::sanitizers
    )
    add_test(
        NAME integration.managed_file_integrity
        COMMAND biocore-managed-file-integrity-tests
    )

    add_executable(
        biocore-plugin-pipeline-contract-hardening-tests
        tests/plugin_pipeline_contract_hardening_tests.cpp
    )
    target_link_libraries(
        biocore-plugin-pipeline-contract-hardening-tests
        PRIVATE BioCore::infrastructure BioCore::pipeline_protocol BioCore::project_warnings BioCore::sanitizers
    )
    add_test(
        NAME integration.plugin_pipeline_contract_hardening
        COMMAND biocore-plugin-pipeline-contract-hardening-tests
    )

    add_executable(
        biocore-export-report-foundation-tests
        tests/export_report_foundation_tests.cpp
    )
    target_link_libraries(
        biocore-export-report-foundation-tests
        PRIVATE BioCore::application BioCore::presentation BioCore::project_warnings BioCore::sanitizers
    )
    add_test(
        NAME integration.export_report_foundation
        COMMAND biocore-export-report-foundation-tests
    )
endif()

if(WIN32)
    install(
        TARGETS biocore biocore-worker
        RUNTIME_DEPENDENCY_SET biocore_windows_core_runtime_dependencies
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
else()
    install(
        TARGETS biocore biocore-worker
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
endif()

install(
    DIRECTORY frontend/
    DESTINATION "${CMAKE_INSTALL_DATADIR}/biocore/frontend"
)

install(
    DIRECTORY pipelines/
    DESTINATION "${CMAKE_INSTALL_DATADIR}/biocore/pipelines"
)

install(
    FILES README.md SECURITY.md LICENSE
    DESTINATION "${CMAKE_INSTALL_DATADIR}/biocore/docs"
)

install(
    DIRECTORY docs/release/
    DESTINATION "${CMAKE_INSTALL_DATADIR}/biocore/docs/release"
)

if(WIN32)
    include(BioCoreWindowsPackaging)
endif()
