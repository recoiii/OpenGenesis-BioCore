add_library(biocore-worker-protocol STATIC)
add_library(BioCore::worker_protocol ALIAS biocore-worker-protocol)

target_sources(
    biocore-worker-protocol
    PRIVATE
        source/launch_arguments.cpp
        source/protocol.cpp
        source/ndjson_framer.cpp
        source/worker_event.cpp
        source/worker_event_codec.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/biocore/worker_protocol/launch_arguments.hpp
            include/biocore/worker_protocol/protocol.hpp
            include/biocore/worker_protocol/ndjson_framer.hpp
            include/biocore/worker_protocol/worker_event.hpp
            include/biocore/worker_protocol/worker_event_codec.hpp
)

target_link_libraries(
    biocore-worker-protocol
    PUBLIC BioCore::project_options
    PRIVATE BioCore::project_warnings BioCore::sanitizers
)
