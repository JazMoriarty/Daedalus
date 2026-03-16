add_executable(DaedalusApp MACOSX_BUNDLE)

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

    find_library(COREGRAPHICS_APP_FRAMEWORK CoreGraphics REQUIRED)
    target_link_libraries(DaedalusApp PRIVATE ${COREGRAPHICS_APP_FRAMEWORK})

    # Configure app bundle properties
    set_target_properties(DaedalusApp PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/resources/DaedalusApp-Info.plist"
        MACOSX_BUNDLE_BUNDLE_NAME "Daedalus"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.daedalus.app"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
        MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
    )

    # Build app bundle in Release mode with full optimizations (standalone stays Debug)
    target_compile_options(DaedalusApp PRIVATE
        $<$<CONFIG:Debug>:
            $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-O3 -DNDEBUG -flto=thin>
        >
    )
    target_link_options(DaedalusApp PRIVATE
        $<$<CONFIG:Debug>:-flto=thin -Wl,-dead_strip>
    )

    # Copy app icon to Resources
    add_custom_command(TARGET DaedalusApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_SOURCE_DIR}/resources/icons/AppIcon.icns"
                "$<TARGET_BUNDLE_CONTENT_DIR:DaedalusApp>/Resources/AppIcon.icns"
        COMMENT "Copying app icon to app bundle"
    )

    # Copy the .metallib into the app bundle next to the executable
    add_custom_command(TARGET DaedalusApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DAEDALUS_METALLIB_OUTPUT}"
                "$<TARGET_FILE_DIR:DaedalusApp>/daedalus_shaders.metallib"
        DEPENDS "${DAEDALUS_METALLIB_OUTPUT}"
        COMMENT "Copying daedalus_shaders.metallib to app bundle"
    )

    # Also copy to standalone executable directory for debugging
    add_custom_command(TARGET DaedalusApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${DAEDALUS_METALLIB_OUTPUT}"
                "${CMAKE_CURRENT_BINARY_DIR}/daedalus_shaders.metallib"
        DEPENDS "${DAEDALUS_METALLIB_OUTPUT}"
        COMMENT "Copying daedalus_shaders.metallib to standalone app directory"
    )

    # Copy shared assets directory next to the app executable.
    add_custom_command(TARGET DaedalusApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:DaedalusApp>/assets"
        COMMENT "Copying shared assets to app binary dir"
    )

    # Also copy assets to standalone executable directory for debugging
    add_custom_command(TARGET DaedalusApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/assets"
                "${CMAKE_CURRENT_BINARY_DIR}/assets"
        COMMENT "Copying shared assets to standalone app directory"
    )
endif()
