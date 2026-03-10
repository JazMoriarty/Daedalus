add_library(DaedalusAudio STATIC)

target_sources(DaedalusAudio PRIVATE
    src/miniaudio_engine.cpp
    src/audio_system.cpp
)

target_include_directories(DaedalusAudio
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${miniaudio_SOURCE_DIR}   # miniaudio.h — private to this module only
)

target_link_libraries(DaedalusAudio PUBLIC DaedalusCore)
target_compile_features(DaedalusAudio PUBLIC cxx_std_23)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
