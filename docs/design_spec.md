# Daedalus Engine — Design Specification v0.1

## Project Vision

Daedalus is a modern 2.5D first-person shooter engine and editor built from first principles in C++23. It draws its design philosophy from Ken Silverman's Build 2.0 — the sector/wall/sprite world model, the dual 2D/3D editor paradigm, and the ease of level construction — but replaces every internal system with architecturally sound, GPU-accelerated, cross-platform implementations. No code is borrowed from Build or Build 2.0. Daedalus is a clean-sheet design that goes further in every dimension.

The engine is named after the master craftsman of Greek mythology — the architect of the labyrinth.

---

## Platform Targets

- **macOS** (primary development platform, Apple Silicon native via Metal)
- **Windows** (secondary, via Vulkan)

---

## Scope — Phase 1

Phase 1 delivers two things: the **renderer** (DaedalusRender) and the **editor** (DaedalusEdit), underpinned by the **engine core** (DaedalusCore). The game itself is built on top of these in a later phase.

---

## North Star: Build 2.0 vs Daedalus

Build 2.0 capabilities inform what Daedalus must match and exceed. Every Build 2.0 feature is preserved in spirit; every limitation is resolved.

**Build 2.0 had:** sector-over-sector, dynamic CPU shadows, 32-bit color, voxel sprites, multi-user editing, EVALDRAW scripting, skyboxes, no sector limits, 6DOF camera.

**Build 2.0 lacked:** GPU rendering, transparency, PBR materials, normal/parallax mapping, post-processing, cross-platform support, modern scripting, physics, hot-reload, voxel character pipeline.

**Daedalus resolves all of the above.**

---

## Tech Stack

| Concern | Technology |
|---|---|
| Language | C++23 |
| Graphics (macOS) | Metal (native, Apple Silicon optimised) |
| Graphics (Windows) | Vulkan |
| Graphics Abstraction | `IRenderDevice` interface with Metal and Vulkan backends |
| Window & Input | SDL3 |
| Editor UI | Dear ImGui (dockable panels, immediate-mode) |
| Mathematics | GLM (GPU-aligned types, quaternions, frustum math) |
| Physics | Jolt Physics (C++17, MIT, deterministic) |
| Scripting | Lua 5.4 via sol2 bindings, hot-reloadable |
| Audio | miniaudio (single-header, cross-platform, 3D positional) |
| Build System | CMake 3.28+ with presets |
| Asset Loading | stb_image (textures), cgltf (mesh import), custom binary runtime formats |

**Language rationale:** C++23 chosen for peak performance, direct hardware access, largest AI training corpus for engine-domain code, and industry-standard game engine ecosystem.

---

## Architecture Overview

Daedalus is structured as a set of discrete, independently compilable modules. Each module exposes a public C++ interface and hides all implementation details. Modules communicate through defined interfaces — never through direct inclusion of implementation headers.

```
Daedalus/
  core/        — DaedalusCore:   platform, memory, ECS, assets, events, threading
  render/      — DaedalusRender: renderer abstraction + Metal/Vulkan backends
  world/       — DaedalusWorld:  map format, sector graph, portal visibility
  physics/     — DaedalusPhysics: Jolt integration
  audio/       — DaedalusAudio:  miniaudio integration, 3D sound
  script/      — DaedalusScript: Lua/sol2 integration, hot-reload
  editor/      — DaedalusEdit:   map editor application
  app/         — Entry point and application lifecycle
  docs/        — Design documentation
  third_party/ — External dependencies
```

Each module directory contains:
- `include/` — public interface headers only
- `src/` — private implementation files
- `tests/` — unit and integration tests
- `CMakeLists.txt` — module build definition

---

## Module: DaedalusCore

The foundation all other modules build upon.

### Platform Abstraction Layer (PAL)

All OS-level calls (file I/O, threading, time, memory pages) go through a PAL interface. No module may call OS APIs directly. This guarantees portability and makes behaviour testable via mock implementations.

### Memory Management

A tiered allocator system: a frame allocator for per-frame temporaries, a pool allocator for fixed-size objects, and a general heap allocator with tracking and leak detection in debug builds. Raw `new`/`delete` are disallowed outside of allocator implementations.

### Entity Component System (ECS)

An archetype-based ECS following cache-friendly data-oriented design. Components are plain data structs. Systems are stateless functions that operate on component arrays. No component holds a pointer to another component. Entity IDs are stable, opaque 64-bit handles.

All placeable world objects — decorative props, enemies, pickups, triggers, lights, sound emitters, interactive objects — are the same entity type. There is no separate "static prop" concept. This unified model is not a performance liability: an archetype ECS groups entities by their exact component set, so a decorative barrel (`{Transform, StaticMesh}`) lives in its own tightly-packed archetype bucket and is never touched by physics, AI, or animation systems. The renderer batches all `StaticMesh` components via GPU instancing regardless of entity category. Portal-based visibility culling further ensures only a small set of entities is ever submitted per frame. The unified model eliminates entire classes of "convert a prop to an entity" redesign problems and keeps the editor workflow consistent.

### Asset Pipeline

A compile-time and runtime asset pipeline. Source assets (PNG, GLTF, Lua scripts, map files) are processed into efficient binary runtime formats. The `AssetManager` loads, caches, and reference-counts runtime assets. Assets are addressed by a stable UUID — never by path at runtime.

### Event System

A typed, synchronous event bus for intra-module communication. Events are plain structs. Subscribers register a typed handler. No `dynamic_cast`, no void pointers.

### Threading

A task-graph job system. Work is submitted as tasks with declared dependencies. No raw `std::thread` usage outside the job system implementation. Mutexes are wrapped in typed RAII guards.

---

## Module: DaedalusRender

The renderer is the most architecturally critical module. It is structured in three layers: the RHI, the Render Graph, and the high-level frame pipeline. No platform-specific type or API leaks above the RHI backend implementation files.

### Layer 1 — RHI (Rendering Hardware Interface)

A clean, modern abstraction over Metal (macOS) and Vulkan (Windows). All higher-level engine code targets the RHI interfaces exclusively. The RHI exposes the following interface types:

- `IRenderDevice` — the GPU device; creates all other resources
- `ICommandBuffer` — records GPU commands (draw calls, dispatches, barriers)
- `ICommandQueue` — submits command buffers to the GPU
- `ISwapchain` — presents rendered frames to the OS window
- `IBuffer` — a GPU memory buffer (vertex, index, uniform, storage)
- `ITexture` — a GPU texture resource (2D, cubemap, 3D, array)
- `ISampler` — texture filtering and addressing state
- `IShader` — a compiled shader module
- `IPipeline` — a fully compiled render or compute pipeline state object
- `IBindGroup` — bound resources for a shader stage (maps to Metal argument buffers and Vulkan descriptor sets)
- `IFence` / `ISemaphore` — CPU/GPU and GPU/GPU synchronisation primitives

Shaders are authored in a single higher-level language and compiled offline by the asset pipeline to MSL (Metal) and SPIR-V (Vulkan). No shader compilation occurs at runtime.

Selecting a backend is a compile-time flag. No `#ifdef` for platform selection appears above the backend implementation files. The Metal and Vulkan backends each implement the RHI interfaces completely and independently.

### Layer 2 — Render Graph

A DAG-based frame graph built fresh each frame. Each pass declares its input textures (reads), output textures (writes), buffer accesses, and whether it is a graphics or compute pass. Once all passes are declared the graph:
- Topologically sorts passes
- Inserts all GPU barriers and memory transitions automatically
- Culls passes whose outputs are never consumed
- Aliases transient textures in memory where their lifetimes do not overlap

No manual barrier management exists anywhere above this layer.

### Layer 3 — Frame Pipeline

The complete sequence of render graph passes executed every frame, in order:

#### Pass 1 — Portal Traversal (CPU)

Runs on the CPU before any GPU work is submitted. Starting from the camera's sector, the traversal projects the visible window (initially the full screen frustum) onto each portal opening, clips the window against the portal rectangle in screen space, and recurses into the adjacent sector with the clipped window. The result is a small list of visible sectors and their contributing portal windows. Only geometry and entities from visible sectors are submitted to the GPU. Sector geometry is pre-built and cached on the GPU; traversal only determines what to draw, not how to tessellate it.

#### Pass 2 — Animation Update

Sprite sheet and rotated sprite set frame indices are advanced on the CPU each tick according to each entity's active animation state and playback rate. Results are written to per-entity GPU storage buffers as a single integer frame index — no skinning, no bone hierarchy. Particle system simulation is updated in a compute dispatch.

#### Pass 3 — Depth Pre-Pass

Renders all opaque geometry to the depth buffer only (no colour output). Populates hardware depth early so the G-buffer pass benefits from early-Z rejection — occluded fragments are discarded before any shading.

#### Pass 4 — G-Buffer Pass

Renders all opaque geometry and opaque entities to the G-buffer. Alpha-cutout billboard sprites (hard-edge transparency) also write to the G-buffer after GPU alpha discard.

**G-Buffer layout:**

| Attachment | Contents | Format |
|---|---|---|
| 0 | Albedo (RGB) + Baked AO (A) | RGBA8 |
| 1 | Octahedral normal (RG) + Roughness (B) + Metalness (A) | RGBA8 |
| 2 | Emissive colour (RGB) | RGBA16F |
| 3 | Screen-space motion vectors (RG) | RG16F |
| Depth | D32F, shared with depth pre-pass | D32F |

#### Pass 5 — Deferred Decal Pass

Decals (bullet holes, blood, scorch marks) are projected onto the G-buffer before lighting. Each decal is an oriented bounding box in world space. For each pixel inside a decal's screen-space footprint the world position is reconstructed from depth, tested against the bounding box, and the decal texture is projected onto the surface — modifying albedo and/or normal in the G-buffer. No geometry modification or pre-baking required.

#### Pass 6 — Shadow Map Passes

All shadow maps are packed into a single shadow atlas texture for GPU cache efficiency.

- **Directional light (sun/sky):** Cascaded Shadow Maps (CSM) with 4 cascades covering exponentially larger areas. PCF softening.
- **Spotlights:** One shadow map per shadowing spotlight, perspective projection, PCF softening.
- **Point lights:** Dual paraboloid shadow maps — two paraboloid projections per light cover the full sphere using two atlas regions, halving draw calls versus cubemap shadows. PCF softening.

Lights without shadow casting enabled skip this pass entirely.

**Ray Tracing integration point:** Ray-traced shadows replacing this pass are a named future quality tier. When enabled, this pass is substituted in the render graph with no architectural changes required elsewhere.

#### Pass 7 — SSAO Pass

HBAO+ (Horizon-Based Ambient Occlusion) samples the depth buffer along screen-space rays to estimate per-pixel ambient occlusion. Output is a half-resolution R8 occlusion texture, upsampled with a bilateral filter. Applied only to ambient/IBL contribution in the lighting pass — not to direct light, which is physically correct.

#### Pass 8 — Lighting Pass

A full-screen compute pass that reads the G-buffer and shadow atlas and computes final HDR radiance per pixel using the Cook-Torrance BRDF:

- **Diffuse:** Lambertian
- **Specular distribution (D):** GGX/Trowbridge-Reitz
- **Geometry function (G):** Smith-GGX
- **Fresnel (F):** Schlick approximation

**Image-Based Lighting (IBL):** the skybox cubemap is pre-filtered offline into a specular pre-filtered environment map (one mip level per roughness value), a diffuse irradiance map, and a BRDF lookup table. Outdoor sectors use full skybox IBL. Indoor sectors blend with the sector's defined ambient colour.

**Tiled/clustered deferred shading:** a compute pre-pass assigns each light to the screen tiles or frustum clusters it overlaps. The lighting pass iterates only the lights relevant to each tile — making hundreds of simultaneous lights practical.

#### Pass 9 — Skybox Pass

Renders the sky cubemap to pixels where depth equals the far plane (no opaque geometry was drawn). Outdoor sector ceilings expose the sky; indoor ceilings do not.

#### Pass 10 — Volumetric Fog Pass

Froxel-based volumetric lighting. The camera frustum is divided into a 3D grid of froxels (frustum voxels). A compute pass scatters light from all in-frustum lights into the froxel volume. A second compute pass integrates froxels front-to-back. The resulting 3D texture is sampled during the transparent pass and composited. Produces atmospheric haze, god rays through portal openings and windows, per-sector coloured fog zones, and visible volumetric spotlight cones.

#### Pass 11 — Transparent Object Pass

Transparent objects are sorted back-to-front and forward-rendered against the opaque depth buffer (read-only, no depth write). Each is shaded with a forward PBR pass sampling the shadow atlas and IBL assets. Weighted Blended OIT is used as a fallback for geometrically complex overlapping transparent surfaces. Contents: portal walls with translucent materials, alpha-blended billboard sprites, particles, any entity with a translucent material.

#### Pass 12 — Particle Pass

GPU compute updates particle simulation (ping-pong storage buffers). A bitonic GPU sort orders particles back-to-front. Particles render as textured quads, depth-tested against the opaque buffer with depth write disabled.

#### Post-Processing Stack (Passes 13–21)

Each step is a render graph node reading the previous node's HDR output texture.

| Pass | Effect | Notes |
|---|---|---|
| 13 | TAA | Halton jitter, motion vector reproject, neighbourhood clamp, CAS sharpening |
| 14 | SSR | Screen-space ray-march, IBL fills screen-edge gaps, disableable per project. **Ray tracing integration point** for future RT reflections. |
| 15 | Bloom | Dual-filter downsample/upsample, luminance threshold |
| 16 | Depth of Field | Bokeh CoC, near/far planes separated |
| 17 | Motion Blur | Per-object + camera rotation, motion vector driven |
| 18 | HDR Tone Mapping | ACES filmic tone curve |
| 19 | Color Grading | Per-zone 3D LUT assets, smooth zone blending, global fallback LUT |
| 20 | Optional Effects | Chromatic aberration, film grain, vignette — all off by default |
| 21 | Upscaling / Final AA | AMD FSR 2/3 or NVIDIA DLSS integration point; falls back to FXAA |

#### Pass 22 — UI Overlay

Editor ImGui panels or game HUD rendered at native display resolution as a final overlay. No post-processing applied to UI.

#### Pass 23 — Present

Final image submitted to the RHI swapchain for OS presentation.

### Entity Visual Rendering

- **Billboard Sprite:** quad oriented to face the camera (full) or camera yaw only (cylindrical). Cutout → G-buffer; blended → transparent pass. Supports sprite sheet animation: the asset defines rows of frames; an `AnimationState` component selects the active row and advances the column each tick.
- **Rotated Sprite Set:** billboard with frame selected by view angle (8 or 16 sectors) combined with animation row. Enemies and characters use this type — identical in spirit to Doom and Duke Nukem 3D. Instant frame transitions preserve the retro aesthetic.
- **Voxel Object:** asset pipeline converts source voxel data (`.vox` format) or GLTF meshes to optimised voxel meshes offline via greedy surface extraction. For characters, the pipeline accepts a GLTF model and a target voxel resolution and produces a static voxel mesh per animation frame — skeletal animation is baked offline; the runtime has no concept of bones or skinning. Rendered identically to static meshes at runtime.
- **Static Mesh:** indexed draw call, GPU-instanced per mesh+material pair. Instance buffers built from ECS archetype queries each frame.

### PBR Material System

Materials are data assets defining: albedo map, normal map, roughness map, metalness map, emissive map, and scalar override values. Materials are compiled to GPU-resident bind groups by the asset pipeline. No fixed-function material properties exist.

### Performance Architecture

- Portal culling is the primary scene culling mechanism — no GPU overhead.
- GPU instancing batches all instances of the same mesh and material into single indirect draw calls.
- Shadow atlas packs all shadow maps into one texture — one bind, better cache utilisation.
- Tiled/clustered deferred shading scales to hundreds of simultaneous lights.
- No runtime shader compilation. All permutations compiled offline to MSL and SPIR-V.
- Async compute allows shadow, SSAO, and particle passes to overlap with other GPU work.

**No hacks, no shortcuts:** every visual effect is implemented correctly according to its reference algorithm. If an effect cannot be implemented correctly with current resources it is deferred to a later phase — never approximated with a workaround that creates technical debt.

---

## Module: DaedalusWorld

Defines the Daedalus map format and world representation.

### The .dmap Format

A clean binary format designed for Daedalus. Not compatible with Build or Build2 — designed from scratch to accommodate all Daedalus features from day one. The format is versioned and forwards-compatible. An ASCII representation (`.dmap.json`) is available for debugging and source control diffs.

### World Model

The world is a graph of sectors. Each sector defines:

- A convex or concave polygon (the floor/ceiling outline)
- Floor height, ceiling height (with optional per-vertex height for slopes)
- Floor and ceiling material references
- A list of walls, each with: a material reference, optional portal reference to an adjacent sector, upper/lower strip materials, UV properties, and flags
- Ambient light colour and intensity
- A list of entity references for all entities whose origin point falls within this sector

Sector-Over-Sector is native. A single vertical column of space can contain multiple stacked sectors linked by portals. There is no limit on sector, wall, or entity counts.

### Portal Visibility

At runtime, the visible sector set is determined by a portal traversal algorithm starting from the camera's sector. The traversal clips the visible frustum against each portal rectangle as it crosses sector boundaries. Only visible sectors submit geometry to the renderer. This is the core performance primitive of the 2.5D paradigm.

---

## Module: DaedalusEdit (The Editor)

DaedalusEdit is the primary tool for building levels. It is built on DaedalusCore, DaedalusRender, and DaedalusWorld, running as a standalone application.

### Editor Philosophy

Build 2.0's editor was powerful because it let level designers work in a 2D overhead view while immediately seeing results in a live 3D view. Daedalus preserves and extends this paradigm: the 2D view is the primary map construction surface; the 3D view is a full DaedalusRender session showing exactly what the game will render, and is also directly interactive for surface editing and entity placement.

### Layout

All panels are dockable, resizable, and persist their layout per project. The default arrangement is:

- Left — 2D overhead viewport (primary editing canvas)
- Right — live 3D viewport (full renderer, interactive)
- Far right — Property Inspector
- Bottom right — Object Browser
- Bottom left — Output / Log
- Floating/toggleable — Layers panel, Entity List panel

Either viewport can be maximised to full screen.

### Starting a Map

A new map dialog sets global properties before editing begins: map name, author, description, global sky cubemap, global ambient light colour and intensity, default grid unit size, default floor and ceiling heights for new sectors, and gravity value for the game layer.

### 2D Viewport — Navigation

- Scroll wheel — zoom in/out
- Middle mouse drag — pan
- Home — fit the entire map to view
- Keys 1–8 — cycle grid size (1, 2, 4, 8, 16, 32, 64, 128 units)
- G — toggle grid visibility
- Grid snapping is on by default; hold Shift to temporarily disable

### Drawing Sectors

Activate the Draw Tool (D). Click to place vertices one at a time. Close the sector by clicking back on the first vertex (which snaps and highlights) or pressing Enter. The sector fills immediately and appears in the 3D viewport. Rules enforced in real time: crossing walls highlight red before the sector is closed; degenerate or zero-area sectors are flagged. Sectors may be convex or concave. Drawing a sector inside an existing sector creates a nested room — the standard method for corridors, alcoves, pillars, and enclosed spaces.

### Selection

Select Tool (S). Click to select a sector (interior), a wall (line), a vertex (corner dot), or an entity (icon). Shift+click for multi-select. Click empty space to deselect. All selection operations are undoable.

### Manipulating Geometry

- Drag a selected sector to move it including all contents
- Drag a vertex to reshape the sector
- Drag a wall segment to translate it parallel to itself
- Alt+click a wall to insert a new vertex at that point
- Delete removes the selected vertex (adjacent walls reconnect), or removes the selected sector/entity
- R — rotate selected object; a rotation handle appears for mouse drag or exact value entry

### Wall Properties (Property Inspector)

- Front material — PBR material seen from inside the sector
- Back material — material seen looking back through a portal
- Upper wall material — strip above a portal opening
- Lower wall material — strip below a portal opening
- UV Offset (X, Y), UV Scale (X, Y), UV Rotation
- Flags — two-sided, blocking (collision), climbable, trigger zone, invisible
- Portal Link — links this wall to a facing wall on an adjacent sector

### Sector Properties (Property Inspector)

- Floor height and ceiling height — scroll wheel adjusts in grid increments when sector is selected
- Floor material and ceiling material
- Floor slope — select the hinge wall then set angle; ceiling slope likewise
- Ambient light colour and intensity
- Flags — outdoors (sky shows, no ceiling rendered), underwater, damage zone, trigger zone

### Portals

A portal is the connection between two sectors through a shared wall. It is how rooms connect: doorways, corridors, windows, hatches, and Sector-Over-Sector vertical stacking all use portals.

To create a portal: select the wall on one sector that faces another sector and click **Link Portal** in the Property Inspector — the editor scans for a matching wall on the adjacent sector and pairs them. Alternatively, drag a wall vertex onto an adjacent sector's wall; they snap together with an auto-link prompt.

Portal walls are drawn in cyan in the 2D view with a double-arrow indicator. A portal wall may carry a transparent/translucent material (glass, bars) or no material at all (open passage). Upper and lower wall strips always carry materials.

Sector-Over-Sector stacking works by placing sectors at different heights and linking them by a floor/ceiling portal — no special case, the same portal system handles all vertical connections.

### 3D Viewport — Navigation

- WASD + mouse look — first-person navigation
- Q / E — move up / down (6DOF flight)
- Hold Shift — fast movement
- Hold Alt + drag — orbit around the selected point
- F — fly to and frame the selected object
- Tab — toggle mouse capture to interact with UI panels

### 3D Viewport — Selection and Surface Editing

Left-click any surface (wall, floor, ceiling) to select it — the Property Inspector updates immediately. With a wall selected, drag the UV gizmo to pan the texture in real time; scroll to adjust scale. Drag the floor or ceiling height handle (a coloured axis arrow) to raise or lower the surface live.

### Entity Visual Types

All placeable world objects are unified ECS entities. The visual representation is a property of the entity's asset. Supported visual types:

- **Billboard Sprite** — flat 2D image that always faces the camera; supports sprite sheet animation rows/columns
- **Rotated Sprite Set** — 2D frames selected by viewing angle (8 or 16 directions) combined with animation row; the standard type for enemies and characters
- **Voxel Object** — 3D voxel model, correct from any angle and pitch; the asset pipeline can bake a GLTF mesh (including an animated source mesh) into per-frame voxel geometry offline, with no runtime bone or skinning data
- **3D Mesh** — static low-poly GLTF model

### Object Browser

Searchable, filterable catalogue of everything that can be placed. Categories:

- Props, Enemies, Pickups, Hazards, Interactive, Lights, Triggers, Sound Emitters, Particle Emitters, Player Start, Prefabs

Each entry shows a rendered thumbnail, name, and category.

### Placing Entities — Three Methods

1. **Drag and drop** from the Object Browser into either viewport
2. **Click-to-place:** select an asset, click anywhere in either viewport to stamp it; Escape to exit
3. **Paste from clipboard:** Ctrl+C / Ctrl+V

### Alignment Modes

- Floor aligned (default) — base sits on sector floor
- Ceiling aligned — hangs from the ceiling
- Wall aligned — face presses flush to nearest wall
- Free floating — no surface snapping; Z set manually

Angle snapping: 45° default, Shift for 15°, Ctrl for free 1° rotation.

### Transform Gizmo (3D View)

- **Translate (T)** — X/Y/Z axis arrows and plane squares
- **Rotate (R)** — X/Y/Z rings with angle snapping
- **Scale (Y)** — per-axis handles and uniform scale centre cube

All gizmo operations are undoable.

### Entity Property Inspector

**Transform** — Position (X, Y, Z), Rotation (Yaw, Pitch, Roll), Scale (X, Y, Z).

**Visual** — assigned asset (swappable), visibility flags, render layer.

**Physics** — collision shape (none, auto, box, capsule, convex hull), static or dynamic, mass.

**Script** — attached Lua script reference; exposed variables appear as editable fields.

**Audio** — ambient sound asset, 3D falloff radius.

**Category-specific** — changes entirely based on entity category (e.g. door: open direction, speed, trigger type, key required; trigger: shape, condition, Lua event; pickup: type, quantity, respawn).

### Entity References and Relationships

Every entity has an optional Entity Name field. Named entities can be referenced by other entities via relationship fields. A **pick** button lets you click the target entity in either viewport to fill in the name. A reference overlay in the 2D view shows relationship arrows as lines (toggleable).

### Mass Placement Tools

- **Stamp mode** — click repeatedly to place many instances without re-selecting
- **Scatter brush** — drag over floor areas to randomly distribute props; configure radius, density, rotation range, scale range, asset pool
- **Array/Repeat** — right-click → Array; define count and spacing for evenly-spaced rows

### 2D View Entity Icons

Props — thumbnail square; Enemies — red circle with crosshair; Pickups — green diamond; Player Start — blue directional arrow; Triggers — dashed yellow volume outline; Sound Emitters — speaker icon with radius ring.

### Layers

Named groups for map elements. Per-layer: toggle visibility, toggle lock, select all in layer. Layers are an editing convenience — no runtime effect.

### Undo/Redo

Command-pattern undo/redo stack with no depth limit. Compound operations are grouped as a single undo step.

### Prefab System

Any selection of sectors, walls, and entities can be saved as a named prefab. Dragging a prefab into the map places a linked instance. Editing the source prefab propagates to all instances. Per-instance property overrides are supported.

### Multi-User Editing

Multiple editors connect to a shared map session over LAN or network. Changes are broadcast with client-side prediction and conflict resolution. Each connected user's cursor and selection are visible with a name label. Toggle host/join with F9.

### Asset Pipeline Integration

The editor imports and processes source assets on demand. Imported assets appear immediately in the Object Browser. Hot-reload propagates asset changes to all open viewports without restarting the editor.

### Map Validation

Continuous background validation: crossing walls (red), unclosed sectors (highlighted), unlinked adjacent walls (yellow), orphan entities (flagged), missing materials (orange).

**Map Doctor (Ctrl+Shift+M)** — lists all issues with click-to-navigate and one-click fixes for common problems.

### Map Testing

F5 compiles the current map and launches a test session in the game runtime. The editor remains open. No file export required — the test session reads directly from the editor's live map data.

### Core Keyboard Shortcuts

| Key | Action |
|---|---|
| D | Draw sector tool |
| S | Select tool |
| V | Vertex tool |
| W | Split wall |
| E | Place entity mode |
| L | Place light |
| T / R / Y | Transform: Translate / Rotate / Scale |
| G | Toggle grid |
| F | Frame selected (3D) |
| Home | Fit map to 2D view |
| Tab | Toggle mouse capture (3D) |
| 1–8 | Cycle grid size |
| Ctrl+Z / Ctrl+Shift+Z | Undo / Redo |
| Ctrl+C / Ctrl+V | Copy / Paste |
| Ctrl+D | Duplicate in place |
| Ctrl+A | Select all |
| Ctrl+S | Save map |
| Ctrl+Shift+M | Map Doctor |
| F5 | Compile and test |
| F9 | Multi-user session host/join |

---

## Coding Standards & Engineering Principles

These standards are **non-negotiable** for all code in the Daedalus project. Every contribution — human or AI-generated — must conform to them. No exceptions.

### Architectural Integrity

Every module must have a well-defined, documented public interface in its `include/` directory. Implementation details must never leak into public headers. If a design requires breaking module boundaries inappropriately, the design must be revised — not the boundary.

### No Hacks. No Patches. No Workarounds.

If a problem cannot be solved cleanly with the current architecture, the correct response is to fix the architecture — not to introduce a hack that defers the problem. Code that works incorrectly in an edge case is not acceptable as a shipping solution. `// TODO: fix this properly` followed by shipping code is not acceptable. Every piece of code must be the correct, complete solution to the problem it solves.

### Interface-First Design

All significant subsystems must be defined as abstract C++ interfaces (pure virtual classes or concept-constrained templates) before implementation begins. This enforces separation of contract from implementation and makes subsystems independently testable.

Example: `IRenderDevice`, `IPhysicsWorld`, `IAudioEngine`, `IAssetLoader` — each is a pure interface. Concrete implementations (`MetalRenderDevice`, `JoltPhysicsWorld`, etc.) are hidden behind these interfaces.

### Single Responsibility

Every class, function, and module has one reason to exist. A class that does two unrelated things must be split. A function longer than can be understood at a glance must be decomposed. Complexity is managed by decomposition, not by accumulation.

### Explicit Over Implicit

No magic. No hidden state. No global singletons (service locator pattern is permitted as an explicit, typed registry, not as a `GetInstance()` global). Function parameters make all dependencies explicit. Side effects are documented.

### Error Handling

Errors are never silently swallowed. Every error path must either recover correctly, propagate the error to a caller who can handle it, or fail loudly (assert in debug, defined behaviour in release). `std::expected<T, Error>` is preferred for recoverable errors. Exceptions are not used in engine code.

### Memory Safety

Raw owning pointers (`T*`) are not used. Ownership is expressed with `std::unique_ptr` or custom allocator handles. Non-owning references use raw pointers or `std::span` where appropriate, with lifetime documented at the call site. No pointer arithmetic outside of allocator implementation code.

### Immutability by Default

Variables and parameters are `const` unless mutation is explicitly required. Mutable state is the exception, not the rule.

### Naming Conventions

| Item | Convention | Example |
|---|---|---|
| Types/Classes | `PascalCase` | `SectorGraph`, `RenderDevice` |
| Functions/Methods | `camelCase` | `buildPortalList`, `submitFrame` |
| Member variables | `m_camelCase` | `m_sectorCount` |
| Constants/Enums | `UPPER_SNAKE_CASE` | `MAX_PORTAL_DEPTH` |
| Files | `snake_case` | `render_device.h`, `sector_graph.cpp` |
| Interfaces | `I` prefix | `IRenderDevice`, `IAssetLoader` |

### Code Formatting

Clang-format is enforced via CI. A `.clang-format` config file is committed at the repository root. No code is merged without passing format checks.

### Testing

Every module must have a corresponding test suite in its `tests/` directory. Pure logic (math, algorithms, data structures, asset parsing) must have unit tests. Integration behaviour (render backend, editor actions) must have integration tests or documented manual test procedures.

### Documentation

All public interface methods must have a doc comment explaining: what the function does, what each parameter means, and what the return value means. Implementation comments explain *why* code does something non-obvious — not *what* it does (the code itself explains that).

### No Premature Optimisation, No Premature Abstraction

Optimise only with evidence (profiler data). Abstract only when there is a concrete second use case or a clear architectural boundary. Speculative abstractions and micro-optimisations without measurement both create debt.

---

## Development Phases

### Phase 1A — Foundation

DaedalusCore: PAL, memory allocators, ECS, asset pipeline, event system, job system.
RHI: Metal backend (macOS), Vulkan backend (Windows), RHI unit tests.
Basic window creation, input handling, application lifecycle.

### Phase 1B — Renderer

Render graph implementation.
Sector geometry generation from hard-coded test data.
Deferred PBR pipeline (G-buffer + lighting pass).
Shadow maps. SSAO. TAA. Skybox.
Post-processing stack.

### Phase 1C — World & Editor

DaedalusWorld: `.dmap` format, sector graph, portal visibility traversal.
DaedalusEdit: 2D sector editor, live 3D viewport, property inspector, undo/redo, object browser.
Prefab system. Multi-user editing.

### Phase 1D — Extended Renderer

Voxel sprite rendering. 3D mesh sprite rendering. Sprite sheet animation system (frame advancement, AnimationState component).
GPU particle system. Decal system. Volumetric fog.
Transparency pass. SSR. Portal/mirror rendering.
Asset pipeline: GLTF-to-voxel offline bake tool (target resolution, per-frame voxel mesh output).

### Phase 2 — Game Layer

Physics integration (Jolt). Audio (miniaudio). Lua scripting.
Game-specific systems built on DaedalusCore ECS.
*(Defined in a separate Game Design Specification.)*
