add_executable(
    biocore-variant-annotation-plugin
    main.cpp
    variant_annotation.cpp
    variant_annotation_invocation_contract.cpp
)

target_link_libraries(
    biocore-variant-annotation-plugin
    PRIVATE
        BioCore::plugin_protocol
        BioCore::project_options
        BioCore::project_warnings
        BioCore::sanitizers
)

if(WIN32)
    set(BIOCORE_VARIANT_ANNOTATION_PLATFORM_DIRECTORY "windows-x64")
else()
    set(BIOCORE_VARIANT_ANNOTATION_PLATFORM_DIRECTORY "linux-x64")
endif()

set(BIOCORE_VARIANT_ANNOTATION_RUNTIME_DIRECTORY
    "${PROJECT_BINARY_DIR}/plugins/org.biocore.variantannotate/bin/${BIOCORE_VARIANT_ANNOTATION_PLATFORM_DIRECTORY}"
)
set_target_properties(
    biocore-variant-annotation-plugin
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${BIOCORE_VARIANT_ANNOTATION_RUNTIME_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${BIOCORE_VARIANT_ANNOTATION_RUNTIME_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${BIOCORE_VARIANT_ANNOTATION_RUNTIME_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${BIOCORE_VARIANT_ANNOTATION_RUNTIME_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${BIOCORE_VARIANT_ANNOTATION_RUNTIME_DIRECTORY}"
)

set(BIOCORE_VARIANT_ANNOTATION_MANIFEST_SOURCE "${PROJECT_SOURCE_DIR}/plugins/org.biocore.variantannotate/plugin.json")
set(BIOCORE_VARIANT_ANNOTATION_MANIFEST_BINARY "${PROJECT_BINARY_DIR}/plugins/org.biocore.variantannotate/plugin.json")
add_custom_command(
    OUTPUT "${BIOCORE_VARIANT_ANNOTATION_MANIFEST_BINARY}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${PROJECT_BINARY_DIR}/plugins/org.biocore.variantannotate"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${BIOCORE_VARIANT_ANNOTATION_MANIFEST_SOURCE}" "${BIOCORE_VARIANT_ANNOTATION_MANIFEST_BINARY}"
    DEPENDS "${BIOCORE_VARIANT_ANNOTATION_MANIFEST_SOURCE}"
    VERBATIM
)
add_custom_target(biocore-variant-annotation-plugin-manifest ALL DEPENDS "${BIOCORE_VARIANT_ANNOTATION_MANIFEST_BINARY}")
add_dependencies(biocore-variant-annotation-plugin biocore-variant-annotation-plugin-manifest)

if(WIN32)
    install(
        TARGETS biocore-variant-annotation-plugin
        RUNTIME_DEPENDENCY_SET biocore_windows_variant_annotation_runtime_dependencies
        RUNTIME DESTINATION "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.variantannotate/bin/${BIOCORE_VARIANT_ANNOTATION_PLATFORM_DIRECTORY}"
    )
    install(
        RUNTIME_DEPENDENCY_SET biocore_windows_variant_annotation_runtime_dependencies
        PRE_EXCLUDE_REGEXES
            "^api-ms-" "^ext-ms-"
            "^(azureattestmanager|azureattestnormal|hvsifiletrust|pdmutilities|wpaxholder)\\.dll$"
            "^(vcruntime|msvcp|concrt|vccorlib).*\\.dll$"
        POST_EXCLUDE_REGEXES
            ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Tt][Ee][Mm]32[\\\\/].*"
            ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Ww][Oo][Ww]64[\\\\/].*"
        RUNTIME DESTINATION "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.variantannotate/bin/${BIOCORE_VARIANT_ANNOTATION_PLATFORM_DIRECTORY}"
    )
else()
    install(TARGETS biocore-variant-annotation-plugin RUNTIME DESTINATION "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.variantannotate/bin/${BIOCORE_VARIANT_ANNOTATION_PLATFORM_DIRECTORY}")
endif()
install(FILES "${PROJECT_SOURCE_DIR}/plugins/org.biocore.variantannotate/plugin.json" DESTINATION "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.variantannotate")
