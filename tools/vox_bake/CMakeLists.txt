# tools/vox_bake/CMakeLists.txt
# DaedalusVoxBake — offline tool that voxelises a glTF mesh into MagicaVoxel .vox files.
#
# Targets:
#   DaedalusVoxBake      — command-line executable
#   DaedalusVoxBakeTests — GoogleTest unit tests for vox_writer and surface_voxelizer

cmake_minimum_required(VERSION 3.24)

# ─── Executable ───────────────────────────────────────────────────────────────

add_executable(DaedalusVoxBake
    main.cpp
    cgltf_impl.cpp   # CGLTF_IMPLEMENTATION in exactly one TU
)

target_include_directories(DaedalusVoxBake PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(DaedalusVoxBake PRIVATE cxx_std_23)

target_link_libraries(DaedalusVoxBake PRIVATE
    DaedalusThirdParty  # cgltf + stb headers
    glm::glm
)

# ─── Unit tests ───────────────────────────────────────────────────────────────

add_executable(DaedalusVoxBakeTests
    test_vox_bake.cpp
)

target_include_directories(DaedalusVoxBakeTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(DaedalusVoxBakeTests PRIVATE cxx_std_23)

target_link_libraries(DaedalusVoxBakeTests PRIVATE
    DaedalusThirdParty
    glm::glm
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(DaedalusVoxBakeTests)
