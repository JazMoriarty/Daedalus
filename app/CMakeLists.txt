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
    DaedalusWorld
    DaedalusPhysics
    DaedalusAudio
    DaedalusScript
    DaedalusThirdParty
    SDL3::SDL3-static
)

target_compile_features(DaedalusApp PRIVATE cxx_std_23)

if(DAEDALUS_PLATFORM_MACOS)
    target_compile_definitions(DaedalusApp PRIVATE DAEDALUS_BACKEND_METAL)

    # Copy the .metallib next to the executable whenever DaedalusShaders changes.
    # Use add_custom_command(OUTPUT ...) so the copy fires on every metallib update,
    # not only when DaedalusApp itself rebuilds (POST_BUILD limitation).
    set(METALLIB_APP_COPY "${CMAKE_BINARY_DIR}/app/daedalus_shaders.metallib")
    add_custom_command(
        OUTPUT  "${METALLIB_APP_COPY}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DAEDALUS_METALLIB_OUTPUT}"
                "${METALLIB_APP_COPY}"
        DEPENDS "${DAEDALUS_METALLIB_OUTPUT}"
        COMMENT "Copying daedalus_shaders.metallib to app output directory"
    )
    add_custom_target(CopyDaedalusShaders ALL DEPENDS "${METALLIB_APP_COPY}")
    # Ensure the render-side DaedalusShaders target runs (and recompiles changed
    # .metal files) before we attempt to copy the metallib to the app directory.
    add_dependencies(CopyDaedalusShaders DaedalusShaders)
    add_dependencies(DaedalusApp CopyDaedalusShaders)

    # Copy shared assets directory next to the app executable.
    add_custom_command(TARGET DaedalusApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:DaedalusApp>/assets"
        COMMENT "Copying shared assets to app binary dir"
    )
endif()
