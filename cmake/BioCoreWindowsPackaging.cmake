# Windows-only portable runtime dependency and CPack policy.
#
# Included by the top-level CMakeLists.txt after all executable targets have
# registered their runtime dependency sets.

if(NOT WIN32)
    return()
endif()

# Keep compiler runtimes explicit instead of relying on dependency scanning of
# Windows system directories. The module populates the redistributable files,
# while SKIP lets OpenGenesis-BioCore install the same release runtime set beside the
# Core executables and every out-of-process native plugin.
if(MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
    if(NOT CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        message(FATAL_ERROR "MSVC runtime redistributable files were not detected")
    endif()

    # Keep this list in lockstep with the native Windows plugin manifests.
    # Each plugin is an independently launched process, so it must be able to
    # resolve the MSVC runtime from its own executable directory even on a
    # machine without a globally installed Visual C++ Redistributable.
    set(BIOCORE_WINDOWS_NATIVE_PLUGIN_IDS
        org.biocore.demo
        org.biocore.fastaqc
        org.biocore.fastqqc
        org.biocore.align
        org.biocore.alignmentqc
        org.biocore.variantcall
        org.biocore.vcfqc
        org.biocore.variantannotate
    )

    install(
        PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
    foreach(BIOCORE_WINDOWS_NATIVE_PLUGIN_ID IN LISTS BIOCORE_WINDOWS_NATIVE_PLUGIN_IDS)
        install(
            PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
            DESTINATION
                "${CMAKE_INSTALL_DATADIR}/biocore/plugins/${BIOCORE_WINDOWS_NATIVE_PLUGIN_ID}/bin/windows-x64"
        )
    endforeach()
endif()

install(
    RUNTIME_DEPENDENCY_SET biocore_windows_core_runtime_dependencies
    PRE_EXCLUDE_REGEXES
        "^api-ms-"
        "^ext-ms-"
        "^(azureattestmanager|azureattestnormal|hvsifiletrust|pdmutilities|wpaxholder)\\.dll$"
        "^(vcruntime|msvcp|concrt|vccorlib).*\\.dll$"
    POST_EXCLUDE_REGEXES
        ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Tt][Ee][Mm]32[\\\\/].*"
        ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Ww][Oo][Ww]64[\\\\/].*"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "OpenGenesis-BioCore")
set(CPACK_PACKAGE_VENDOR "OpenGenesis-BioCore Project")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_FILE_NAME "OpenGenesis-BioCore-${BIOCORE_VERSION_STRING}-windows-x64")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)

include(CPack)
