add_library(DaedalusWorld STATIC)

target_sources(DaedalusWorld PRIVATE
    # Sources added here as files are created
)

target_include_directories(DaedalusWorld
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(DaedalusWorld PUBLIC DaedalusCore DaedalusRender)
target_compile_features(DaedalusWorld PUBLIC cxx_std_23)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
