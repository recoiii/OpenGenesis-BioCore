add_library(biocore-presentation STATIC)
add_library(BioCore::presentation ALIAS biocore-presentation)

target_sources(
    biocore-presentation
    PRIVATE
        source/artifact_report.cpp
        source/frontend_asset_store.cpp
        source/health_json.cpp
        source/local_api.cpp
        source/local_browser_session.cpp
        source/worker_lifecycle_event_broadcast.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES
            include/biocore/presentation/artifact_report.hpp
            include/biocore/presentation/frontend_asset_store.hpp
            include/biocore/presentation/health_json.hpp
            include/biocore/presentation/local_api.hpp
            include/biocore/presentation/local_web_server.hpp
            include/biocore/presentation/local_browser_session.hpp
            include/biocore/presentation/worker_lifecycle_event_broadcast.hpp
)

target_link_libraries(
    biocore-presentation
    PUBLIC BioCore::application BioCore::project_options
    PRIVATE BioCore::project_warnings BioCore::sanitizers
)

if(Drogon_FOUND)
    target_sources(biocore-presentation PRIVATE source/drogon_local_web_server.cpp)
    target_link_libraries(biocore-presentation PRIVATE Drogon::Drogon)
    target_compile_definitions(biocore-presentation PUBLIC BIOCORE_HAS_DROGON=1)
else()
    target_sources(biocore-presentation PRIVATE source/local_web_server_unavailable.cpp)
    target_compile_definitions(biocore-presentation PUBLIC BIOCORE_HAS_DROGON=0)
endif()
