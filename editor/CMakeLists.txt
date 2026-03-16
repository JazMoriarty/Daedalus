# Primary debug executable (standalone, no bundle, full debug info)
add_executable(DaedalusEdit)

target_sources(DaedalusEdit PRIVATE
    src/main.mm
    src/document/edit_map_document.cpp
    src/document/undo_stack.cpp
    src/document/commands/cmd_draw_sector.cpp
    src/document/commands/cmd_set_sector_heights.cpp
    src/document/commands/cmd_set_wall_flags.cpp
    src/document/commands/cmd_move_vertex.cpp
    src/document/commands/cmd_duplicate_sector.cpp
    src/document/commands/cmd_paste_sector.cpp
    src/document/commands/cmd_link_portal.cpp
    src/document/commands/cmd_unlink_portal.cpp
    src/tools/geometry_utils.cpp
    src/tools/select_tool.cpp
    src/tools/draw_sector_tool.cpp
    src/tools/vertex_tool.cpp
    src/document/commands/cmd_split_wall.cpp
    src/document/commands/cmd_delete_sector.cpp
    src/document/commands/cmd_move_sector.cpp
    src/document/commands/cmd_slide_wall.cpp
    src/document/commands/cmd_set_wall_uv.cpp
    src/document/commands/cmd_set_sector_ambient.cpp
    src/document/commands/cmd_set_sector_flags.cpp
    src/document/map_doctor.cpp
    src/document/map_doctor_runner.cpp
    src/document/commands/cmd_place_light.cpp
    src/document/commands/cmd_move_light.cpp
    src/document/commands/cmd_delete_light.cpp
    src/document/commands/cmd_set_light_props.cpp
    src/document/commands/cmd_place_entity.cpp
    src/document/commands/cmd_delete_entity.cpp
    src/document/commands/cmd_move_entity.cpp
    src/document/commands/cmd_rotate_entity.cpp
    src/document/commands/cmd_scale_entity.cpp
    src/document/commands/cmd_set_entity_props.cpp
    src/document/commands/cmd_rotate_sector.cpp
    src/document/commands/cmd_set_player_start.cpp
    src/document/commands/compound_command.cpp
    src/document/commands/cmd_save_prefab.cpp
    src/document/commands/cmd_place_prefab.cpp
    src/document/commands/cmd_set_wall_material.cpp
    src/document/commands/cmd_set_sector_material.cpp
    src/document/commands/cmd_set_map_meta.cpp
    src/document/commands/cmd_set_map_defaults.cpp
    src/document/commands/cmd_set_global_ambient.cpp
    src/panels/viewport_2d.cpp
    src/panels/viewport_3d.cpp
    src/panels/entity_gpu_cache.cpp
    src/panels/property_inspector.cpp
    src/panels/output_log.cpp
    src/panels/render_settings_panel.cpp
    src/panels/object_browser_panel.cpp
    src/panels/layers_panel.cpp
    src/panels/help_panel.cpp
    src/document/emap_sidecar.cpp
    src/catalog/material_catalog.cpp
    src/catalog/model_catalog.cpp
    src/panels/asset_browser_panel.cpp
    src/panels/splash_screen.mm
)

target_include_directories(DaedalusEdit
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(DaedalusEdit PRIVATE
    DaedalusCore
    DaedalusRender
    DaedalusWorld
    DaedalusThirdParty
    DaedalusImGui
    SDL3::SDL3-static
    nlohmann_json::nlohmann_json
)

target_compile_features(DaedalusEdit PRIVATE cxx_std_23)

if(DAEDALUS_PLATFORM_MACOS)
    # Ensure shaders are built before the editor
    add_dependencies(DaedalusEdit DaedalusShaders)
    target_compile_definitions(DaedalusEdit PRIVATE DAEDALUS_BACKEND_METAL)

    find_library(METAL_FRAMEWORK       Metal       REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK   QuartzCore REQUIRED)
    find_library(COCOA_FRAMEWORK        Cocoa      REQUIRED)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)

    target_link_libraries(DaedalusEdit PRIVATE
        ${METAL_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
        ${COCOA_FRAMEWORK}
        ${COREGRAPHICS_FRAMEWORK}
    )

    # Copy shader library to standalone executable directory
    add_custom_command(TARGET DaedalusEdit POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DAEDALUS_METALLIB_OUTPUT}"
                "$<TARGET_FILE_DIR:DaedalusEdit>/daedalus_shaders.metallib"
        DEPENDS DaedalusShaders
        COMMENT "Copying daedalus_shaders.metallib to editor directory"
    )

    # Copy shared assets directory next to the editor executable
    add_custom_command(TARGET DaedalusEdit POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:DaedalusEdit>/assets"
        COMMENT "Copying shared assets to editor directory"
    )

    # Copy editor-private assets
    add_custom_command(TARGET DaedalusEdit POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:DaedalusEdit>/editor_assets"
        COMMENT "Copying editor-private assets to editor directory"
    )

    # Enable ARC for all Obj-C++ source files
    set_source_files_properties(src/main.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    set_source_files_properties(src/panels/splash_screen.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")

    # ─── Optional Release App Bundle Target ─────────────────────────────────────
    # This creates an optimized .app bundle for distribution
    # Build with: cmake --build build --target DaedalusEditApp
    add_executable(DaedalusEditApp MACOSX_BUNDLE EXCLUDE_FROM_ALL)
    
    # Same sources as debug executable
    target_sources(DaedalusEditApp PRIVATE
        src/main.mm
        src/document/edit_map_document.cpp
        src/document/undo_stack.cpp
        src/document/commands/cmd_draw_sector.cpp
        src/document/commands/cmd_set_sector_heights.cpp
        src/document/commands/cmd_set_wall_flags.cpp
        src/document/commands/cmd_move_vertex.cpp
        src/document/commands/cmd_duplicate_sector.cpp
        src/document/commands/cmd_paste_sector.cpp
        src/document/commands/cmd_link_portal.cpp
        src/document/commands/cmd_unlink_portal.cpp
        src/tools/geometry_utils.cpp
        src/tools/select_tool.cpp
        src/tools/draw_sector_tool.cpp
        src/tools/vertex_tool.cpp
        src/document/commands/cmd_split_wall.cpp
        src/document/commands/cmd_delete_sector.cpp
        src/document/commands/cmd_move_sector.cpp
        src/document/commands/cmd_slide_wall.cpp
        src/document/commands/cmd_set_wall_uv.cpp
        src/document/commands/cmd_set_sector_ambient.cpp
        src/document/commands/cmd_set_sector_flags.cpp
        src/document/map_doctor.cpp
        src/document/map_doctor_runner.cpp
        src/document/commands/cmd_place_light.cpp
        src/document/commands/cmd_move_light.cpp
        src/document/commands/cmd_delete_light.cpp
        src/document/commands/cmd_set_light_props.cpp
        src/document/commands/cmd_place_entity.cpp
        src/document/commands/cmd_delete_entity.cpp
        src/document/commands/cmd_move_entity.cpp
        src/document/commands/cmd_rotate_entity.cpp
        src/document/commands/cmd_scale_entity.cpp
        src/document/commands/cmd_set_entity_props.cpp
        src/document/commands/cmd_rotate_sector.cpp
        src/document/commands/cmd_set_player_start.cpp
        src/document/commands/compound_command.cpp
        src/document/commands/cmd_save_prefab.cpp
        src/document/commands/cmd_place_prefab.cpp
        src/document/commands/cmd_set_wall_material.cpp
        src/document/commands/cmd_set_sector_material.cpp
        src/document/commands/cmd_set_map_meta.cpp
        src/document/commands/cmd_set_map_defaults.cpp
        src/document/commands/cmd_set_global_ambient.cpp
        src/panels/viewport_2d.cpp
        src/panels/viewport_3d.cpp
        src/panels/entity_gpu_cache.cpp
        src/panels/property_inspector.cpp
        src/panels/output_log.cpp
        src/panels/render_settings_panel.cpp
        src/panels/object_browser_panel.cpp
        src/panels/layers_panel.cpp
        src/panels/help_panel.cpp
        src/document/emap_sidecar.cpp
        src/catalog/material_catalog.cpp
        src/catalog/model_catalog.cpp
        src/panels/asset_browser_panel.cpp
        src/panels/splash_screen.mm
    )
    
    target_include_directories(DaedalusEditApp
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    
    target_link_libraries(DaedalusEditApp PRIVATE
        DaedalusCore DaedalusRender DaedalusWorld DaedalusThirdParty DaedalusImGui
        SDL3::SDL3-static nlohmann_json::nlohmann_json
        ${METAL_FRAMEWORK} ${QUARTZCORE_FRAMEWORK} ${COCOA_FRAMEWORK} ${COREGRAPHICS_FRAMEWORK}
    )
    
    target_compile_features(DaedalusEditApp PRIVATE cxx_std_23)
    target_compile_definitions(DaedalusEditApp PRIVATE DAEDALUS_BACKEND_METAL)
    add_dependencies(DaedalusEditApp DaedalusShaders)
    
    # App bundle configuration
    set_target_properties(DaedalusEditApp PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/resources/DaedalusEdit-Info.plist"
        MACOSX_BUNDLE_BUNDLE_NAME "Daedalus Edit"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.daedalus.editor"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
        MACOSX_BUNDLE_ICON_FILE "EditorIcon.icns"
        OUTPUT_NAME "DaedalusEdit"
    )
    
    # Release optimizations for app bundle
    target_compile_options(DaedalusEditApp PRIVATE -O3 -DNDEBUG -flto=thin)
    target_link_options(DaedalusEditApp PRIVATE -flto=thin -Wl,-dead_strip)
    
    # ARC for app bundle
    set_source_files_properties(
        src/main.mm src/panels/splash_screen.mm
        TARGET_DIRECTORY DaedalusEditApp
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )
    
    # Copy resources to app bundle
    add_custom_command(TARGET DaedalusEditApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_SOURCE_DIR}/resources/icons/EditorIcon.icns"
                "$<TARGET_BUNDLE_CONTENT_DIR:DaedalusEditApp>/Resources/EditorIcon.icns"
        COMMENT "Copying editor icon to app bundle"
    )
    
    add_custom_command(TARGET DaedalusEditApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DAEDALUS_METALLIB_OUTPUT}"
                "$<TARGET_FILE_DIR:DaedalusEditApp>/daedalus_shaders.metallib"
        DEPENDS DaedalusShaders
        COMMENT "Copying shaders to app bundle"
    )
    
    add_custom_command(TARGET DaedalusEditApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:DaedalusEditApp>/assets"
        COMMENT "Copying assets to app bundle"
    )
    
    add_custom_command(TARGET DaedalusEditApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:DaedalusEditApp>/editor_assets"
        COMMENT "Copying editor assets to app bundle"
    )
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
