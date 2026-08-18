find_package(ZLIB REQUIRED)

add_executable(
    biocore-fastq-qc-plugin
    main.cpp
    fastq_input_stream.cpp
    fastq_invocation_contract.cpp
    fastq_statistics.cpp
    paired_fastq_statistics.cpp
    fastq_trimming.cpp
)

target_link_libraries(
    biocore-fastq-qc-plugin
    PRIVATE
        BioCore::plugin_protocol
        BioCore::project_options
        BioCore::project_warnings
        BioCore::sanitizers
        ZLIB::ZLIB
)

if(WIN32)
    set(BIOCORE_FASTQ_QC_PLATFORM_DIRECTORY "windows-x64")
else()
    set(BIOCORE_FASTQ_QC_PLATFORM_DIRECTORY "linux-x64")
endif()

set(BIOCORE_FASTQ_QC_RUNTIME_DIRECTORY
    "${PROJECT_BINARY_DIR}/plugins/org.biocore.fastqqc/bin/${BIOCORE_FASTQ_QC_PLATFORM_DIRECTORY}"
)

set_target_properties(
    biocore-fastq-qc-plugin
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${BIOCORE_FASTQ_QC_RUNTIME_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${BIOCORE_FASTQ_QC_RUNTIME_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${BIOCORE_FASTQ_QC_RUNTIME_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${BIOCORE_FASTQ_QC_RUNTIME_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${BIOCORE_FASTQ_QC_RUNTIME_DIRECTORY}"
)

set(BIOCORE_FASTQ_QC_MANIFEST_SOURCE
    "${PROJECT_SOURCE_DIR}/plugins/org.biocore.fastqqc/plugin.json"
)
set(BIOCORE_FASTQ_QC_MANIFEST_BINARY
    "${PROJECT_BINARY_DIR}/plugins/org.biocore.fastqqc/plugin.json"
)
add_custom_command(
    OUTPUT "${BIOCORE_FASTQ_QC_MANIFEST_BINARY}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${PROJECT_BINARY_DIR}/plugins/org.biocore.fastqqc"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${BIOCORE_FASTQ_QC_MANIFEST_SOURCE}"
        "${BIOCORE_FASTQ_QC_MANIFEST_BINARY}"
    DEPENDS "${BIOCORE_FASTQ_QC_MANIFEST_SOURCE}"
    VERBATIM
)
add_custom_target(
    biocore-fastq-qc-plugin-manifest ALL
    DEPENDS "${BIOCORE_FASTQ_QC_MANIFEST_BINARY}"
)
add_dependencies(biocore-fastq-qc-plugin biocore-fastq-qc-plugin-manifest)

if(WIN32)
    install(
        TARGETS biocore-fastq-qc-plugin
        RUNTIME_DEPENDENCY_SET biocore_windows_fastq_qc_runtime_dependencies
        RUNTIME DESTINATION
            "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.fastqqc/bin/${BIOCORE_FASTQ_QC_PLATFORM_DIRECTORY}"
    )
    install(
        RUNTIME_DEPENDENCY_SET biocore_windows_fastq_qc_runtime_dependencies
        PRE_EXCLUDE_REGEXES
            "^api-ms-"
            "^ext-ms-"
            "^(azureattestmanager|azureattestnormal|hvsifiletrust|pdmutilities|wpaxholder)\\.dll$"
            "^(vcruntime|msvcp|concrt|vccorlib).*\\.dll$"
        POST_EXCLUDE_REGEXES
            ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Tt][Ee][Mm]32[\\\\/].*"
            ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Ww][Oo][Ww]64[\\\\/].*"
        RUNTIME DESTINATION
            "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.fastqqc/bin/${BIOCORE_FASTQ_QC_PLATFORM_DIRECTORY}"
    )
else()
    install(
        TARGETS biocore-fastq-qc-plugin
        RUNTIME DESTINATION
            "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.fastqqc/bin/${BIOCORE_FASTQ_QC_PLATFORM_DIRECTORY}"
    )
endif()

install(
    FILES "${PROJECT_SOURCE_DIR}/plugins/org.biocore.fastqqc/plugin.json"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.fastqqc"
)
