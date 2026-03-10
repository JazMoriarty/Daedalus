add_library(DaedalusPhysics STATIC)

target_sources(DaedalusPhysics PRIVATE
    src/jolt_physics_world.cpp
)

target_include_directories(DaedalusPhysics
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${joltphysics_SOURCE_DIR}   # Jolt headers at <Jolt/Jolt.h>
)

target_link_libraries(DaedalusPhysics
    PUBLIC  DaedalusCore
            DaedalusWorld        # map_data.h / world_types.h used in i_physics_world.h
    PRIVATE Jolt                 # Jolt static library; never re-exported
)
target_compile_features(DaedalusPhysics PUBLIC cxx_std_23)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
