find_package(ZLIB REQUIRED)

add_executable(
    biocore-variant-call-plugin
    main.cpp
    variant_calling_invocation_contract.cpp
    variant_calling.cpp
)

target_link_libraries(
    biocore-variant-call-plugin
    PRIVATE
        BioCore::plugin_protocol
        BioCore::project_options
        BioCore::project_warnings
        BioCore::sanitizers
        ZLIB::ZLIB
)

if(WIN32)
    set(BIOCORE_VARIANT_CALL_PLATFORM_DIRECTORY "windows-x64")
else()
    set(BIOCORE_VARIANT_CALL_PLATFORM_DIRECTORY "linux-x64")
endif()

set(BIOCORE_VARIANT_CALL_RUNTIME_DIRECTORY
    "${PROJECT_BINARY_DIR}/plugins/org.biocore.variantcall/bin/${BIOCORE_VARIANT_CALL_PLATFORM_DIRECTORY}"
)
set_target_properties(
    biocore-variant-call-plugin
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${BIOCORE_VARIANT_CALL_RUNTIME_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${BIOCORE_VARIANT_CALL_RUNTIME_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${BIOCORE_VARIANT_CALL_RUNTIME_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${BIOCORE_VARIANT_CALL_RUNTIME_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${BIOCORE_VARIANT_CALL_RUNTIME_DIRECTORY}"
)

set(BIOCORE_VARIANT_CALL_MANIFEST_SOURCE "${PROJECT_SOURCE_DIR}/plugins/org.biocore.variantcall/plugin.json")
set(BIOCORE_VARIANT_CALL_MANIFEST_BINARY "${PROJECT_BINARY_DIR}/plugins/org.biocore.variantcall/plugin.json")
add_custom_command(
    OUTPUT "${BIOCORE_VARIANT_CALL_MANIFEST_BINARY}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${PROJECT_BINARY_DIR}/plugins/org.biocore.variantcall"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${BIOCORE_VARIANT_CALL_MANIFEST_SOURCE}" "${BIOCORE_VARIANT_CALL_MANIFEST_BINARY}"
    DEPENDS "${BIOCORE_VARIANT_CALL_MANIFEST_SOURCE}"
    VERBATIM
)
add_custom_target(biocore-variant-call-plugin-manifest ALL DEPENDS "${BIOCORE_VARIANT_CALL_MANIFEST_BINARY}")
add_dependencies(biocore-variant-call-plugin biocore-variant-call-plugin-manifest)

if(WIN32)
    install(
        TARGETS biocore-variant-call-plugin
        RUNTIME_DEPENDENCY_SET biocore_windows_variant_call_runtime_dependencies
        RUNTIME DESTINATION "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.variantcall/bin/${BIOCORE_VARIANT_CALL_PLATFORM_DIRECTORY}"
    )
    install(
        RUNTIME_DEPENDENCY_SET biocore_windows_variant_call_runtime_dependencies
        PRE_EXCLUDE_REGEXES
            "^api-ms-" "^ext-ms-"
            "^(azureattestmanager|azureattestnormal|hvsifiletrust|pdmutilities|wpaxholder)\\.dll$"
            "^(vcruntime|msvcp|concrt|vccorlib).*\\.dll$"
        POST_EXCLUDE_REGEXES
            ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Tt][Ee][Mm]32[\\\\/].*"
            ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Ww][Oo][Ww]64[\\\\/].*"
        RUNTIME DESTINATION "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.variantcall/bin/${BIOCORE_VARIANT_CALL_PLATFORM_DIRECTORY}"
    )
else()
    install(TARGETS biocore-variant-call-plugin RUNTIME DESTINATION "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.variantcall/bin/${BIOCORE_VARIANT_CALL_PLATFORM_DIRECTORY}")
endif()

install(FILES "${PROJECT_SOURCE_DIR}/plugins/org.biocore.variantcall/plugin.json" DESTINATION "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.variantcall")
