add_library(DaedalusCore STATIC)

target_sources(DaedalusCore PRIVATE
    # Sources added here as files are created
)

target_include_directories(DaedalusCore
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(DaedalusCore PUBLIC cxx_std_23)

# ─── Tests ────────────────────────────────────────────────────────────────────
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
