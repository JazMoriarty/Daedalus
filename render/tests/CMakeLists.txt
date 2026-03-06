add_executable(RenderTests
    test_billboard_render_system.cpp
    test_transparent_pass.cpp
    test_sprite_animation_system.cpp
    test_vox_mesher.cpp
    test_decal_render_system.cpp
)

target_link_libraries(RenderTests
    PRIVATE DaedalusRender
            GTest::gtest_main
)

target_compile_features(RenderTests PRIVATE cxx_std_23)

include(GoogleTest)
gtest_discover_tests(RenderTests)
