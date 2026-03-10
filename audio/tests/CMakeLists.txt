add_executable(AudioTests
    test_audio_engine.cpp
)

target_link_libraries(AudioTests
    PRIVATE DaedalusAudio
            GTest::gtest_main
)

target_compile_features(AudioTests PRIVATE cxx_std_23)

include(GoogleTest)
gtest_discover_tests(AudioTests)
