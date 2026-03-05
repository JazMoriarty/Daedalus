cmake_minimum_required(VERSION 3.28)
project(Daedalus VERSION 0.1.0 LANGUAGES CXX OBJCXX)

# ─── Language standard ────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 23)

# ─── Build type default ───────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# ─── Platform detection ───────────────────────────────────────────────────────
if(APPLE)
    set(DAEDALUS_PLATFORM_MACOS ON)
    set(DAEDALUS_RENDER_BACKEND "Metal")
elseif(WIN32)
    set(DAEDALUS_PLATFORM_WINDOWS ON)
    set(DAEDALUS_RENDER_BACKEND "Vulkan")
else()
    message(FATAL_ERROR "Unsupported platform. Daedalus targets macOS and Windows only.")
endif()

message(STATUS "Daedalus: platform=${CMAKE_SYSTEM_NAME}  backend=${DAEDALUS_RENDER_BACKEND}")

# ─── Third-party dependencies ─────────────────────────────────────────────────
include(FetchContent)

# stb — single-header image load/write
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stb)

# cgltf — single-header glTF 2.0 parser (spec-mandated for mesh import)
FetchContent_Declare(
    cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG        v1.14
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(cgltf)

# DaedalusThirdParty — INTERFACE target exposing stb + cgltf include paths.
# Any module that needs image or mesh loading links against this target.
add_library(DaedalusThirdParty INTERFACE)
target_include_directories(DaedalusThirdParty INTERFACE
    ${stb_SOURCE_DIR}
    ${cgltf_SOURCE_DIR}
)

# SDL3 — window, input, Metal view
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.8
    GIT_SHALLOW    TRUE
)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL3)

# GLM — mathematics
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

# nlohmann/json — JSON serialisation for .dmap.json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
set(JSON_BuildTests     OFF CACHE BOOL "" FORCE)
set(JSON_Install        OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# Dear ImGui — dockable immediate-mode UI (editor)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

# DaedalusImGui — compiles ImGui core + SDL3 + Metal backends into one static lib.
# The Metal backend is Obj-C++ (.mm), so we need OBJCXX enabled (already set above).
add_library(DaedalusImGui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_metal.mm
)
target_include_directories(DaedalusImGui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_compile_definitions(DaedalusImGui PUBLIC IMGUI_ENABLE_DOCKING)
target_compile_features(DaedalusImGui PUBLIC cxx_std_23)
target_link_libraries(DaedalusImGui PUBLIC SDL3::SDL3-static)
if(APPLE)
    target_link_libraries(DaedalusImGui PUBLIC
        "-framework Metal"
        "-framework QuartzCore"
    )
endif()

# GoogleTest — unit testing
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ─── Testing ──────────────────────────────────────────────────────────────────
include(CTest)
enable_testing()

# ─── Modules ──────────────────────────────────────────────────────────────────
add_subdirectory(core)
add_subdirectory(render)
add_subdirectory(world)
add_subdirectory(physics)
add_subdirectory(audio)
add_subdirectory(script)
add_subdirectory(editor)
add_subdirectory(app)
