add_executable(DaedalusApp)

target_sources(DaedalusApp PRIVATE
    src/main.cpp
)

target_include_directories(DaedalusApp
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(DaedalusApp PRIVATE
    DaedalusCore
    DaedalusRender
    SDL3::SDL3-static
)

target_compile_features(DaedalusApp PRIVATE cxx_std_23)

if(DAEDALUS_PLATFORM_MACOS)
    target_compile_definitions(DaedalusApp PRIVATE DAEDALUS_BACKEND_METAL)
endif()
