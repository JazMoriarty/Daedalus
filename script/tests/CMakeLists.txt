add_executable(ScriptTests
    test_script_engine.cpp
)

target_link_libraries(ScriptTests
    PRIVATE DaedalusScript
            GTest::gtest_main
)

target_compile_features(ScriptTests PRIVATE cxx_std_23)

include(GoogleTest)
gtest_discover_tests(ScriptTests)
