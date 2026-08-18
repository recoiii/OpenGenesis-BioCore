add_library(
    biocore-local-bootstrap STATIC
    local_server_bootstrap.cpp
    project_init_bootstrap.cpp
)
target_include_directories(biocore-local-bootstrap PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(
    biocore-local-bootstrap
    PUBLIC
        BioCore::application
        BioCore::infrastructure
        BioCore::presentation
        BioCore::project_options
    PRIVATE
        BioCore::project_warnings
        BioCore::sanitizers
)

add_executable(biocore main.cpp)
target_link_libraries(
    biocore
    PRIVATE
        biocore-local-bootstrap
        BioCore::application
        BioCore::infrastructure
        BioCore::presentation
        BioCore::project_options
        BioCore::project_warnings
        BioCore::sanitizers
)
