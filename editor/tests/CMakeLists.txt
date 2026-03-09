add_executable(DaedalusEditTests
    test_geometry_utils.cpp
    test_undo_stack.cpp
    test_map_doctor.cpp
    test_commands.cpp
    test_sidecar_entities.cpp
    test_sidecar_render_settings.cpp
    test_sprite_anim.cpp
    test_mesh_albedo_path.cpp
    test_prefab.cpp
    test_rotated_sprite.cpp
    # Editor source files under test (editor is an executable, not a library).
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/tools/geometry_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/undo_stack.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/map_doctor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/edit_map_document.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/emap_sidecar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_place_light.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_delete_light.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_move_light.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_set_light_props.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_set_wall_uv.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_set_sector_ambient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_move_sector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_split_wall.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_duplicate_sector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_place_entity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_delete_entity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_move_entity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_rotate_entity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_scale_entity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_set_entity_props.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_draw_sector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_rotate_sector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_set_player_start.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/compound_command.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_save_prefab.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_place_prefab.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_set_wall_material.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_set_sector_material.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_set_map_meta.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_set_map_defaults.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/document/commands/cmd_set_global_ambient.cpp
)

target_include_directories(DaedalusEditTests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(DaedalusEditTests
    PRIVATE DaedalusCore
            DaedalusWorld
            GTest::gtest_main
            nlohmann_json::nlohmann_json
)

target_compile_features(DaedalusEditTests PRIVATE cxx_std_23)

include(GoogleTest)
gtest_discover_tests(DaedalusEditTests)
