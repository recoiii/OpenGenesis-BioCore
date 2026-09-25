add_library(biocore-pipeline-protocol STATIC)
add_library(BioCore::pipeline_protocol ALIAS biocore-pipeline-protocol)

target_sources(
    biocore-pipeline-protocol
    PRIVATE
        source/pipeline_document_codec.cpp
        source/workflow_document_codec.cpp
        source/workflow_branch_decision_codec.cpp
        source/workflow_template_document_codec.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/biocore/pipeline_protocol/pipeline_document.hpp
            include/biocore/pipeline_protocol/pipeline_document_codec.hpp
            include/biocore/pipeline_protocol/workflow_document_codec.hpp
            include/biocore/pipeline_protocol/workflow_branch_decision_codec.hpp
            include/biocore/pipeline_protocol/workflow_template_document_codec.hpp
)

target_link_libraries(
    biocore-pipeline-protocol
    PUBLIC BioCore::project_options BioCore::domain
    PRIVATE BioCore::project_warnings BioCore::sanitizers
)
