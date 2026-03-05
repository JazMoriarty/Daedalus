add_executable(DaedalusEdit)

target_sources(DaedalusEdit PRIVATE
    src/main.mm
    src/document/edit_map_document.cpp
    src/document/undo_stack.cpp
    src/document/commands/cmd_draw_sector.cpp
    src/document/commands/cmd_set_sector_heights.cpp
    src/document/commands/cmd_set_wall_flags.cpp
    src/document/commands/cmd_move_vertex.cpp
    src/document/commands/cmd_link_portal.cpp
    src/document/commands/cmd_unlink_portal.cpp
    src/tools/geometry_utils.cpp
    src/tools/select_tool.cpp
    src/tools/draw_sector_tool.cpp
    src/panels/viewport_2d.cpp
    src/panels/viewport_3d.cpp
    src/panels/property_inspector.cpp
    src/panels/output_log.cpp
)

target_include_directories(DaedalusEdit
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(DaedalusEdit PRIVATE
    DaedalusCore
    DaedalusRender
    DaedalusWorld
    DaedalusImGui
    SDL3::SDL3-static
)

target_compile_features(DaedalusEdit PRIVATE cxx_std_23)

if(DAEDALUS_PLATFORM_MACOS)
    target_compile_definitions(DaedalusEdit PRIVATE DAEDALUS_BACKEND_METAL)

    find_library(METAL_FRAMEWORK     Metal       REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(COCOA_FRAMEWORK      Cocoa      REQUIRED)

    target_link_libraries(DaedalusEdit PRIVATE
        ${METAL_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
        ${COCOA_FRAMEWORK}
    )

    # Enable ARC for the Obj-C++ entry point.
    set_source_files_properties(src/main.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")

    # Copy the .metallib next to the editor executable (same pattern as app/).
    set(METALLIB_EDITOR_COPY "${CMAKE_BINARY_DIR}/editor/daedalus_shaders.metallib")
    add_custom_command(
        OUTPUT  "${METALLIB_EDITOR_COPY}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DAEDALUS_METALLIB_OUTPUT}"
                "${METALLIB_EDITOR_COPY}"
        DEPENDS "${DAEDALUS_METALLIB_OUTPUT}"
        COMMENT "Copying daedalus_shaders.metallib to editor output directory"
    )
    add_custom_target(CopyDaedalusShadersEdit ALL DEPENDS "${METALLIB_EDITOR_COPY}")
    add_dependencies(CopyDaedalusShadersEdit DaedalusShaders)
    add_dependencies(DaedalusEdit CopyDaedalusShadersEdit)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
