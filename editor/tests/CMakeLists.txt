add_executable(DaedalusEditTests
    test_geometry_utils.cpp
    test_undo_stack.cpp
    # Editor source files under test (editor is an executable, not a library).
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/tools/geometry_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/undo_stack.cpp
)

target_include_directories(DaedalusEditTests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(DaedalusEditTests
    PRIVATE DaedalusCore
            DaedalusWorld
            GTest::gtest_main
)

target_compile_features(DaedalusEditTests PRIVATE cxx_std_23)

include(GoogleTest)
gtest_discover_tests(DaedalusEditTests)
