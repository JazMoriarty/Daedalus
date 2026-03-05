add_library(DaedalusScript STATIC)

target_sources(DaedalusScript PRIVATE
    # Sources added here as files are created
)

target_include_directories(DaedalusScript
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(DaedalusScript PUBLIC DaedalusCore)
target_compile_features(DaedalusScript PUBLIC cxx_std_23)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
