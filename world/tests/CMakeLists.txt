add_executable(WorldTests
    test_dmap_io.cpp
    test_dlevel_io.cpp
    test_sector_tessellator.cpp
    test_portal_traversal.cpp
    test_pathfinder.cpp
)

target_link_libraries(WorldTests
    PRIVATE DaedalusWorld
            GTest::gtest_main
)

target_compile_features(WorldTests PRIVATE cxx_std_23)

include(GoogleTest)
gtest_discover_tests(WorldTests)
