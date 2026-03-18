add_library(DaedalusRender STATIC)

target_include_directories(DaedalusRender
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(DaedalusRender PUBLIC DaedalusCore)
target_link_libraries(DaedalusRender PRIVATE DaedalusThirdParty)
target_compile_features(DaedalusRender PUBLIC cxx_std_23)

# ─── Cross-platform render-graph + frame renderer sources ───────────────────────
target_sources(DaedalusRender PRIVATE
    src/render_graph/render_graph.cpp
    src/renderer/frame_renderer.cpp
    src/renderer/rt_scene_manager.cpp
    src/loader/asset_loader.cpp
    src/loader/mipmap_generator.cpp
    src/loader/normal_map_generator.cpp
)

# ─── Platform backend ───────────────────────────────────────────────────────
if(DAEDALUS_PLATFORM_MACOS)
    target_sources(DaedalusRender PRIVATE
        src/rhi/create_render_device.cpp
        src/rhi/backends/metal/metal_render_device.mm
    )

    target_compile_definitions(DaedalusRender PRIVATE DAEDALUS_BACKEND_METAL)

    find_library(METAL_FRAMEWORK    Metal       REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)

    target_link_libraries(DaedalusRender PRIVATE
        ${METAL_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
        SDL3::SDL3-static
    )

    # Enable ARC for Objective-C++ sources.
    set_source_files_properties(
        src/rhi/backends/metal/metal_render_device.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )

    # ─── Metal shader compilation ────────────────────────────────────────────
    find_program(XCRUN_EXE xcrun REQUIRED)

    file(GLOB METAL_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/*.metal")
    set(METAL_COMMON_H
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/metal/common.h")

    set(AIR_FILES "")
    foreach(METAL_SRC ${METAL_SOURCES})
        get_filename_component(METAL_NAME "${METAL_SRC}" NAME_WE)
        set(AIR_FILE "${CMAKE_CURRENT_BINARY_DIR}/${METAL_NAME}.air")
        add_custom_command(
            OUTPUT  "${AIR_FILE}"
            COMMAND "${XCRUN_EXE}" -sdk macosx metal -c "${METAL_SRC}" -o "${AIR_FILE}"
            DEPENDS "${METAL_SRC}" "${METAL_COMMON_H}"
            COMMENT "Compiling Metal shader: ${METAL_NAME}.metal"
        )
        list(APPEND AIR_FILES "${AIR_FILE}")
    endforeach()

    set(METALLIB_OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/daedalus_shaders.metallib")
    add_custom_command(
        OUTPUT  "${METALLIB_OUTPUT}"
        COMMAND "${XCRUN_EXE}" -sdk macosx metallib ${AIR_FILES}
                    -o "${METALLIB_OUTPUT}"
        DEPENDS ${AIR_FILES}
        COMMENT "Linking daedalus_shaders.metallib"
    )
    add_custom_target(DaedalusShaders DEPENDS "${METALLIB_OUTPUT}")

    # Export the metallib path so app/CMakeLists.txt can copy it.
    set(DAEDALUS_METALLIB_OUTPUT "${METALLIB_OUTPUT}" PARENT_SCOPE)

elseif(DAEDALUS_PLATFORM_WINDOWS)
    target_compile_definitions(DaedalusRender PRIVATE DAEDALUS_BACKEND_VULKAN)
    find_package(Vulkan REQUIRED)
    target_link_libraries(DaedalusRender PRIVATE Vulkan::Vulkan)
endif()

# ─── Tests ────────────────────────────────────────────────────────────────────
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
