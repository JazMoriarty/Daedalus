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

    # Depend on the Metal shader compilation and copy the .metallib next to the executable.
    add_dependencies(DaedalusApp DaedalusShaders)
    add_custom_command(TARGET DaedalusApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DAEDALUS_METALLIB_OUTPUT}"
            "$<TARGET_FILE_DIR:DaedalusApp>/daedalus_shaders.metallib"
        COMMENT "Copying daedalus_shaders.metallib to app output directory"
    )
endif()
