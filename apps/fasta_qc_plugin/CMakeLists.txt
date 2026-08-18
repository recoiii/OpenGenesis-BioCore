add_executable(
    biocore-fasta-qc-plugin
    main.cpp
    fasta_invocation_contract.cpp
    fasta_statistics.cpp
)

target_link_libraries(
    biocore-fasta-qc-plugin
    PRIVATE
        BioCore::plugin_protocol
        BioCore::project_options
        BioCore::project_warnings
        BioCore::sanitizers
)

if(WIN32)
    set(BIOCORE_FASTA_QC_PLATFORM_DIRECTORY "windows-x64")
else()
    set(BIOCORE_FASTA_QC_PLATFORM_DIRECTORY "linux-x64")
endif()

set_target_properties(
    biocore-fasta-qc-plugin
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY
            "${PROJECT_BINARY_DIR}/plugins/org.biocore.fastaqc/bin/${BIOCORE_FASTA_QC_PLATFORM_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG
            "${PROJECT_BINARY_DIR}/plugins/org.biocore.fastaqc/bin/${BIOCORE_FASTA_QC_PLATFORM_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE
            "${PROJECT_BINARY_DIR}/plugins/org.biocore.fastaqc/bin/${BIOCORE_FASTA_QC_PLATFORM_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO
            "${PROJECT_BINARY_DIR}/plugins/org.biocore.fastaqc/bin/${BIOCORE_FASTA_QC_PLATFORM_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL
            "${PROJECT_BINARY_DIR}/plugins/org.biocore.fastaqc/bin/${BIOCORE_FASTA_QC_PLATFORM_DIRECTORY}"
)

set(BIOCORE_FASTA_QC_MANIFEST_SOURCE
    "${PROJECT_SOURCE_DIR}/plugins/org.biocore.fastaqc/plugin.json"
)
set(BIOCORE_FASTA_QC_MANIFEST_BINARY
    "${PROJECT_BINARY_DIR}/plugins/org.biocore.fastaqc/plugin.json"
)
add_custom_command(
    OUTPUT "${BIOCORE_FASTA_QC_MANIFEST_BINARY}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${PROJECT_BINARY_DIR}/plugins/org.biocore.fastaqc"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${BIOCORE_FASTA_QC_MANIFEST_SOURCE}"
        "${BIOCORE_FASTA_QC_MANIFEST_BINARY}"
    DEPENDS "${BIOCORE_FASTA_QC_MANIFEST_SOURCE}"
    VERBATIM
)
add_custom_target(
    biocore-fasta-qc-plugin-manifest ALL
    DEPENDS "${BIOCORE_FASTA_QC_MANIFEST_BINARY}"
)
add_dependencies(biocore-fasta-qc-plugin biocore-fasta-qc-plugin-manifest)

if(WIN32)
    install(
        TARGETS biocore-fasta-qc-plugin
        RUNTIME_DEPENDENCY_SET biocore_windows_fasta_qc_runtime_dependencies
        RUNTIME DESTINATION
            "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.fastaqc/bin/${BIOCORE_FASTA_QC_PLATFORM_DIRECTORY}"
    )
    install(
        RUNTIME_DEPENDENCY_SET biocore_windows_fasta_qc_runtime_dependencies
        PRE_EXCLUDE_REGEXES
            "^api-ms-"
            "^ext-ms-"
            "^(azureattestmanager|azureattestnormal|hvsifiletrust|pdmutilities|wpaxholder)\\.dll$"
            "^(vcruntime|msvcp|concrt|vccorlib).*\\.dll$"
        POST_EXCLUDE_REGEXES
            ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Tt][Ee][Mm]32[\\\\/].*"
            ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Ww][Oo][Ww]64[\\\\/].*"
        RUNTIME DESTINATION
            "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.fastaqc/bin/${BIOCORE_FASTA_QC_PLATFORM_DIRECTORY}"
    )
else()
    install(
        TARGETS biocore-fasta-qc-plugin
        RUNTIME DESTINATION
            "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.fastaqc/bin/${BIOCORE_FASTA_QC_PLATFORM_DIRECTORY}"
    )
endif()

install(
    FILES "${PROJECT_SOURCE_DIR}/plugins/org.biocore.fastaqc/plugin.json"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.fastaqc"
)
