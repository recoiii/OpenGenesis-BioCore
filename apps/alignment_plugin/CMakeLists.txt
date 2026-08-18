find_package(ZLIB REQUIRED)

add_executable(
    biocore-alignment-plugin
    main.cpp
    alignment_invocation_contract.cpp
    alignment_io.cpp
    reference_alignment.cpp
)

target_link_libraries(
    biocore-alignment-plugin
    PRIVATE
        BioCore::plugin_protocol
        BioCore::project_options
        BioCore::project_warnings
        BioCore::sanitizers
        ZLIB::ZLIB
)

if(WIN32)
    set(BIOCORE_ALIGNMENT_PLATFORM_DIRECTORY "windows-x64")
else()
    set(BIOCORE_ALIGNMENT_PLATFORM_DIRECTORY "linux-x64")
endif()

set(BIOCORE_ALIGNMENT_RUNTIME_DIRECTORY
    "${PROJECT_BINARY_DIR}/plugins/org.biocore.align/bin/${BIOCORE_ALIGNMENT_PLATFORM_DIRECTORY}"
)
set_target_properties(
    biocore-alignment-plugin
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${BIOCORE_ALIGNMENT_RUNTIME_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${BIOCORE_ALIGNMENT_RUNTIME_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${BIOCORE_ALIGNMENT_RUNTIME_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${BIOCORE_ALIGNMENT_RUNTIME_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${BIOCORE_ALIGNMENT_RUNTIME_DIRECTORY}"
)

set(BIOCORE_ALIGNMENT_MANIFEST_SOURCE
    "${PROJECT_SOURCE_DIR}/plugins/org.biocore.align/plugin.json"
)
set(BIOCORE_ALIGNMENT_MANIFEST_BINARY
    "${PROJECT_BINARY_DIR}/plugins/org.biocore.align/plugin.json"
)
add_custom_command(
    OUTPUT "${BIOCORE_ALIGNMENT_MANIFEST_BINARY}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${PROJECT_BINARY_DIR}/plugins/org.biocore.align"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${BIOCORE_ALIGNMENT_MANIFEST_SOURCE}" "${BIOCORE_ALIGNMENT_MANIFEST_BINARY}"
    DEPENDS "${BIOCORE_ALIGNMENT_MANIFEST_SOURCE}"
    VERBATIM
)
add_custom_target(
    biocore-alignment-plugin-manifest ALL
    DEPENDS "${BIOCORE_ALIGNMENT_MANIFEST_BINARY}"
)
add_dependencies(biocore-alignment-plugin biocore-alignment-plugin-manifest)

if(WIN32)
    install(
        TARGETS biocore-alignment-plugin
        RUNTIME_DEPENDENCY_SET biocore_windows_alignment_runtime_dependencies
        RUNTIME DESTINATION
            "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.align/bin/${BIOCORE_ALIGNMENT_PLATFORM_DIRECTORY}"
    )
    install(
        RUNTIME_DEPENDENCY_SET biocore_windows_alignment_runtime_dependencies
        PRE_EXCLUDE_REGEXES
            "^api-ms-"
            "^ext-ms-"
            "^(azureattestmanager|azureattestnormal|hvsifiletrust|pdmutilities|wpaxholder)\\.dll$"
            "^(vcruntime|msvcp|concrt|vccorlib).*\\.dll$"
        POST_EXCLUDE_REGEXES
            ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Tt][Ee][Mm]32[\\\\/].*"
            ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Ww][Oo][Ww]64[\\\\/].*"
        RUNTIME DESTINATION
            "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.align/bin/${BIOCORE_ALIGNMENT_PLATFORM_DIRECTORY}"
    )
else()
    install(
        TARGETS biocore-alignment-plugin
        RUNTIME DESTINATION
            "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.align/bin/${BIOCORE_ALIGNMENT_PLATFORM_DIRECTORY}"
    )
endif()

install(
    FILES "${PROJECT_SOURCE_DIR}/plugins/org.biocore.align/plugin.json"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.align"
)
