add_library(DaedalusCore STATIC)

target_sources(DaedalusCore PRIVATE
    # ─── Platform ────────────────────────────────────────────────────────────
    src/platform/macos_platform.cpp

    # ─── Memory ──────────────────────────────────────────────────────────────
    src/memory/heap_allocator.cpp
    src/memory/frame_allocator.cpp
    src/memory/pool_allocator.cpp

    # ─── ECS ─────────────────────────────────────────────────────────────────
    src/ecs/world.cpp

    # ─── Threading ───────────────────────────────────────────────────────────
    src/threading/job_system.cpp

    # ─── Factory ───────────────────────────────────────────────────────────
    src/create_platform.cpp
)

target_include_directories(DaedalusCore
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(DaedalusCore PUBLIC cxx_std_23)

# GLM is header-only; expose it transitively so all modules can use maths.
target_link_libraries(DaedalusCore PUBLIC glm::glm)

# ─── Tests ────────────────────────────────────────────────────────────────────
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
