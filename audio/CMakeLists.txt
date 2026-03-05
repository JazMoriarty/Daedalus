add_library(DaedalusAudio STATIC)

target_sources(DaedalusAudio PRIVATE
    # Sources added here as files are created
)

target_include_directories(DaedalusAudio
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(DaedalusAudio PUBLIC DaedalusCore)
target_compile_features(DaedalusAudio PUBLIC cxx_std_23)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
