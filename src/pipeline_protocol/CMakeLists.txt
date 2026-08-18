add_library(biocore-pipeline-protocol STATIC)
add_library(BioCore::pipeline_protocol ALIAS biocore-pipeline-protocol)

target_sources(
    biocore-pipeline-protocol
    PRIVATE
        source/pipeline_document_codec.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/biocore/pipeline_protocol/pipeline_document.hpp
            include/biocore/pipeline_protocol/pipeline_document_codec.hpp
)

target_link_libraries(
    biocore-pipeline-protocol
    PUBLIC BioCore::project_options
    PRIVATE BioCore::project_warnings BioCore::sanitizers
)
