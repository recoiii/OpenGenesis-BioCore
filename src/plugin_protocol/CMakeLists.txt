add_library(biocore-plugin-protocol STATIC)
add_library(BioCore::plugin_protocol ALIAS biocore-plugin-protocol)

target_sources(
    biocore-plugin-protocol
    PRIVATE
        source/plugin_document_codec.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/biocore/plugin_protocol/plugin_document.hpp
            include/biocore/plugin_protocol/plugin_document_codec.hpp
)

target_link_libraries(
    biocore-plugin-protocol
    PUBLIC BioCore::project_options
    PRIVATE BioCore::project_warnings BioCore::sanitizers
)
