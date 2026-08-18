include_guard(GLOBAL)

function(biocore_define_project_options)
    option(BIOCORE_BUILD_TESTS "Build OpenGenesis-BioCore tests" ON)
    option(BIOCORE_REQUIRE_DROGON "Require the Drogon local web server backend at configure time" OFF)
    option(BIOCORE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
    option(BIOCORE_ENABLE_ASAN "Enable AddressSanitizer" OFF)
    option(BIOCORE_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

    add_library(biocore_project_options INTERFACE)
    add_library(BioCore::project_options ALIAS biocore_project_options)

    target_compile_features(biocore_project_options INTERFACE cxx_std_20)
    set_target_properties(biocore_project_options PROPERTIES EXPORT_NAME project_options)
endfunction()
