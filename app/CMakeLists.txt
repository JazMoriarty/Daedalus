# Primary debug runtime executable (standalone, no bundle, full debug info)
add_executable(DaedalusApp)

target_sources(DaedalusApp PRIVATE src/main.cpp)
target_include_directories(DaedalusApp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(DaedalusApp PRIVATE
    DaedalusCore DaedalusRender DaedalusWorld DaedalusPhysics
    DaedalusAudio DaedalusScript DaedalusThirdParty SDL3::SDL3-static
)

target_compile_features(DaedalusApp PRIVATE cxx_std_23)

if(DAEDALUS_PLATFORM_MACOS)
    target_compile_definitions(DaedalusApp PRIVATE DAEDALUS_BACKEND_METAL)
    find_library(COREGRAPHICS_APP_FRAMEWORK CoreGraphics REQUIRED)
    target_link_libraries(DaedalusApp PRIVATE ${COREGRAPHICS_APP_FRAMEWORK})
    
    # Ensure shaders are built before the app
    add_dependencies(DaedalusApp DaedalusShaders)
    
    # Copy shader library to standalone executable directory
    add_custom_command(TARGET DaedalusApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DAEDALUS_METALLIB_OUTPUT}"
                "$<TARGET_FILE_DIR:DaedalusApp>/daedalus_shaders.metallib"
        DEPENDS DaedalusShaders
        COMMENT "Copying shaders to app directory"
    )
    
    # Copy shared assets directory
    add_custom_command(TARGET DaedalusApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:DaedalusApp>/assets"
        COMMENT "Copying assets to app directory"
    )
    
    # ─── Optional Release App Bundle Target ─────────────────────────────────────
    # This creates an optimized .app bundle for distribution
    # Build with: cmake --build build --target DaedalusAppBundle
    add_executable(DaedalusAppBundle MACOSX_BUNDLE EXCLUDE_FROM_ALL)
    
    target_sources(DaedalusAppBundle PRIVATE src/main.cpp)
    target_include_directories(DaedalusAppBundle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    
    target_link_libraries(DaedalusAppBundle PRIVATE
        DaedalusCore DaedalusRender DaedalusWorld DaedalusPhysics
        DaedalusAudio DaedalusScript DaedalusThirdParty SDL3::SDL3-static
        ${COREGRAPHICS_APP_FRAMEWORK}
    )
    
    target_compile_features(DaedalusAppBundle PRIVATE cxx_std_23)
    target_compile_definitions(DaedalusAppBundle PRIVATE DAEDALUS_BACKEND_METAL)
    add_dependencies(DaedalusAppBundle DaedalusShaders)
    
    # App bundle configuration
    set_target_properties(DaedalusAppBundle PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/resources/DaedalusApp-Info.plist"
        MACOSX_BUNDLE_BUNDLE_NAME "Daedalus"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.daedalus.app"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
        MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
        OUTPUT_NAME "DaedalusApp"
    )
    
    # Release optimizations for app bundle
    target_compile_options(DaedalusAppBundle PRIVATE -O3 -DNDEBUG -flto=thin)
    target_link_options(DaedalusAppBundle PRIVATE -flto=thin -Wl,-dead_strip)
    
    # Copy resources to app bundle
    add_custom_command(TARGET DaedalusAppBundle POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_SOURCE_DIR}/resources/icons/AppIcon.icns"
                "$<TARGET_BUNDLE_CONTENT_DIR:DaedalusAppBundle>/Resources/AppIcon.icns"
        COMMENT "Copying app icon to app bundle"
    )
    
    add_custom_command(TARGET DaedalusAppBundle POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DAEDALUS_METALLIB_OUTPUT}"
                "$<TARGET_FILE_DIR:DaedalusAppBundle>/daedalus_shaders.metallib"
        DEPENDS DaedalusShaders
        COMMENT "Copying shaders to app bundle"
    )
    
    add_custom_command(TARGET DaedalusAppBundle POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:DaedalusAppBundle>/assets"
        COMMENT "Copying assets to app bundle"
    )
endif()
