add_library(DaedalusWorld STATIC)

target_sources(DaedalusWorld PRIVATE
    src/world_map.cpp
    src/portal_traversal.cpp
    src/sector_tessellator.cpp
    src/dmap_binary.cpp
    src/dmap_json.cpp
)

target_include_directories(DaedalusWorld
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(DaedalusWorld
    PUBLIC  DaedalusCore DaedalusRender
    PRIVATE nlohmann_json::nlohmann_json
)
target_compile_features(DaedalusWorld PUBLIC cxx_std_23)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
