add_executable(DaedalusApp)

target_sources(DaedalusApp PRIVATE
    # Sources added here as files are created
)

target_include_directories(DaedalusApp
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(DaedalusApp PRIVATE
    DaedalusCore
    DaedalusRender
    DaedalusWorld
    DaedalusPhysics
    DaedalusAudio
    DaedalusScript
)

target_compile_features(DaedalusApp PRIVATE cxx_std_23)
