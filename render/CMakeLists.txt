add_library(DaedalusRender STATIC)

target_include_directories(DaedalusRender
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(DaedalusRender PUBLIC DaedalusCore)
target_compile_features(DaedalusRender PUBLIC cxx_std_23)

# ─── Platform backend ───────────────────────────────────────────────────────
if(DAEDALUS_PLATFORM_MACOS)
    target_sources(DaedalusRender PRIVATE
        src/create_render_device.cpp
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

elseif(DAEDALUS_PLATFORM_WINDOWS)
    target_compile_definitions(DaedalusRender PRIVATE DAEDALUS_BACKEND_VULKAN)
    find_package(Vulkan REQUIRED)
    target_link_libraries(DaedalusRender PRIVATE Vulkan::Vulkan)
endif()

# ─── Tests ────────────────────────────────────────────────────────────────────
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
