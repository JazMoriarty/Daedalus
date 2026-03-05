cmake_minimum_required(VERSION 3.28)
project(Daedalus VERSION 0.1.0 LANGUAGES CXX)

# ─── Language standard ────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# ─── Modules ──────────────────────────────────────────────────────────────────
add_subdirectory(core)
add_subdirectory(render)
add_subdirectory(world)
add_subdirectory(physics)
add_subdirectory(audio)
add_subdirectory(script)
add_subdirectory(editor)
add_subdirectory(app)
