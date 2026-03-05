add_executable(DaedalusEdit)

target_sources(DaedalusEdit PRIVATE
    # Sources added here as files are created
)

target_include_directories(DaedalusEdit
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(DaedalusEdit PRIVATE
    DaedalusCore
    DaedalusRender
    DaedalusWorld
)

target_compile_features(DaedalusEdit PRIVATE cxx_std_23)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
