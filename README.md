# Daedalus Engine

A modern 2.5D first-person shooter engine and editor, built from first principles in C++23.

Inspired by the design philosophy of Ken Silverman's Build 2.0 engine — the sector/wall/sprite world model and dual 2D/3D editor paradigm — with every internal system replaced by GPU-accelerated, architecturally sound, cross-platform implementations.

See [`docs/design_spec.md`](docs/design_spec.md) for the full design specification.

## Platform Targets

- **macOS** (primary — Metal backend, Apple Silicon native)
- **Windows** (secondary — Vulkan backend)

## Building

Requires CMake 3.28+ and a C++23 capable compiler (Clang 17+ on macOS, MSVC 2022 on Windows).

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Project Structure

```
core/       DaedalusCore   — platform, memory, ECS, assets, events, threading
render/     DaedalusRender — RHI abstraction + Metal/Vulkan backends + renderer
world/      DaedalusWorld  — .dmap format, sector graph, portal visibility
physics/    DaedalusPhysics — Jolt Physics integration
audio/      DaedalusAudio  — miniaudio integration, 3D positional sound
script/     DaedalusScript — Lua 5.4 / sol2 integration, hot-reload
editor/     DaedalusEdit   — map editor application
app/        DaedalusApp    — game runtime entry point
docs/       Design documentation
third_party/ External dependencies
```

## Coding Standards

All code must conform to the engineering principles defined in `docs/design_spec.md`.  
The short version: interface-first design, no hacks, no workarounds, no raw owning pointers, `const` by default, `std::expected` for errors, clang-format enforced.
