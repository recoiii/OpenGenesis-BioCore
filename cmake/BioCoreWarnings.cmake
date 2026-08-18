include_guard(GLOBAL)

add_library(biocore_project_warnings INTERFACE)
add_library(BioCore::project_warnings ALIAS biocore_project_warnings)

if(MSVC)
    target_compile_options(
        biocore_project_warnings
        INTERFACE
            /W4
            /permissive-
            /EHsc
            /utf-8
            $<$<BOOL:${BIOCORE_WARNINGS_AS_ERRORS}>:/WX>
    )
else()
    target_compile_options(
        biocore_project_warnings
        INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Wold-style-cast
            $<$<BOOL:${BIOCORE_WARNINGS_AS_ERRORS}>:-Werror>
    )
endif()
