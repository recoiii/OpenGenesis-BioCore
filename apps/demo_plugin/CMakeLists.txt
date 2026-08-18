add_executable(biocore-demo-plugin main.cpp)

target_link_libraries(
    biocore-demo-plugin
    PRIVATE BioCore::plugin_protocol BioCore::project_options BioCore::project_warnings BioCore::sanitizers
)

if(WIN32)
    set(BIOCORE_PLUGIN_PLATFORM_DIRECTORY "windows-x64")
else()
    set(BIOCORE_PLUGIN_PLATFORM_DIRECTORY "linux-x64")
endif()

set_target_properties(
    biocore-demo-plugin
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/plugins/org.biocore.demo/bin/${BIOCORE_PLUGIN_PLATFORM_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${PROJECT_BINARY_DIR}/plugins/org.biocore.demo/bin/${BIOCORE_PLUGIN_PLATFORM_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PROJECT_BINARY_DIR}/plugins/org.biocore.demo/bin/${BIOCORE_PLUGIN_PLATFORM_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${PROJECT_BINARY_DIR}/plugins/org.biocore.demo/bin/${BIOCORE_PLUGIN_PLATFORM_DIRECTORY}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${PROJECT_BINARY_DIR}/plugins/org.biocore.demo/bin/${BIOCORE_PLUGIN_PLATFORM_DIRECTORY}"
)

set(BIOCORE_DEMO_PLUGIN_MANIFEST_SOURCE
    "${PROJECT_SOURCE_DIR}/plugins/org.biocore.demo/plugin.json"
)
set(BIOCORE_DEMO_PLUGIN_MANIFEST_BINARY
    "${PROJECT_BINARY_DIR}/plugins/org.biocore.demo/plugin.json"
)
add_custom_command(
    OUTPUT "${BIOCORE_DEMO_PLUGIN_MANIFEST_BINARY}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${PROJECT_BINARY_DIR}/plugins/org.biocore.demo"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${BIOCORE_DEMO_PLUGIN_MANIFEST_SOURCE}"
        "${BIOCORE_DEMO_PLUGIN_MANIFEST_BINARY}"
    DEPENDS "${BIOCORE_DEMO_PLUGIN_MANIFEST_SOURCE}"
    VERBATIM
)
add_custom_target(
    biocore-demo-plugin-manifest ALL
    DEPENDS "${BIOCORE_DEMO_PLUGIN_MANIFEST_BINARY}"
)
add_dependencies(biocore-demo-plugin biocore-demo-plugin-manifest)

if(WIN32)
    install(
        TARGETS biocore-demo-plugin
        RUNTIME_DEPENDENCY_SET biocore_windows_demo_plugin_runtime_dependencies
        RUNTIME DESTINATION "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.demo/bin/${BIOCORE_PLUGIN_PLATFORM_DIRECTORY}"
    )
    install(
        RUNTIME_DEPENDENCY_SET biocore_windows_demo_plugin_runtime_dependencies
        PRE_EXCLUDE_REGEXES
            "^api-ms-"
            "^ext-ms-"
            "^(azureattestmanager|azureattestnormal|hvsifiletrust|pdmutilities|wpaxholder)\\.dll$"
            "^(vcruntime|msvcp|concrt|vccorlib).*\\.dll$"
        POST_EXCLUDE_REGEXES
            ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Tt][Ee][Mm]32[\\\\/].*"
            ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Ww][Oo][Ww]64[\\\\/].*"
        RUNTIME DESTINATION "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.demo/bin/${BIOCORE_PLUGIN_PLATFORM_DIRECTORY}"
    )
else()
    install(
        TARGETS biocore-demo-plugin
        RUNTIME DESTINATION "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.demo/bin/${BIOCORE_PLUGIN_PLATFORM_DIRECTORY}"
    )
endif()
install(
    FILES "${PROJECT_SOURCE_DIR}/plugins/org.biocore.demo/plugin.json"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/biocore/plugins/org.biocore.demo"
)
