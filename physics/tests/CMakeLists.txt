add_executable(PhysicsTests
    test_physics_world.cpp
)

target_link_libraries(PhysicsTests
    PRIVATE DaedalusPhysics
            GTest::gtest_main
)

target_compile_features(PhysicsTests PRIVATE cxx_std_23)

include(GoogleTest)
gtest_discover_tests(PhysicsTests)
