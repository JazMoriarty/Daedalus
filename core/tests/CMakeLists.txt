add_executable(CoreTests
    test_transform_component.cpp
    test_ecs_world.cpp
)

target_link_libraries(CoreTests
    PRIVATE DaedalusCore
            GTest::gtest_main
)

target_compile_features(CoreTests PRIVATE cxx_std_23)

include(GoogleTest)
gtest_discover_tests(CoreTests)
