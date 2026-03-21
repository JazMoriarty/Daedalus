# Daedalus Engine — Design Specification v0.2

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

**Build 2.0 lacked:** GPU rendering, transparency, PBR materials, normal/parallax mapping, post-processing, cross-platform support, modern scripting, physics, hot-reload, voxel character pipeline, ray tracing.

**Daedalus resolves all of the above** — including a toggleable GPU path-tracing render mode with SVGF denoising.

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
- `IAccelerationStructure` — a GPU-built bounding volume hierarchy for ray tracing (maps to `MTLAccelerationStructure` on Metal, `VkAccelerationStructureKHR` on Vulkan)
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

Daedalus supports two render modes, selectable at runtime via `SceneView::renderMode`:

- **Rasterized** (default) — the deferred PBR pipeline described below.
- **RayTraced** — a GPU path-tracing pipeline that replaces the rasterised passes (shadow through skybox) with a single compute dispatch, described in **Render Mode: Ray Traced** below.

Both modes produce the same `RGBA16Float` HDR output texture. The post-processing stack (TAA through FXAA) is shared and runs identically regardless of which mode produced the HDR input.

#### Render Mode: Rasterized

The complete sequence of render graph passes executed every frame, in order:

#### Pass 1 — Portal Traversal (CPU)

Runs on the CPU before any GPU work is submitted. Starting from the camera's sector, the traversal projects the visible window (initially the full screen frustum) onto each portal opening, clips the window against the portal rectangle in screen space, and recurses into the adjacent sector with the clipped window. The result is a small list of visible sectors, each accompanied by an NDC-space portal window — the screen-space rectangle representing the intersection of all portal openings on the path to that sector from the camera.

Only geometry and entities from visible sectors are submitted to the GPU. Each sector's portal window is forwarded to the renderer as a GPU scissor rect when submitting that sector's draw calls. This ensures geometry from a sector behind a portal cannot render outside the opening through which it is visible — a correctness requirement (preventing geometry bleed at oblique viewing angles) and a performance primitive (eliminating overdraw in deeply nested portal chains on complex levels). Sector geometry is pre-built and cached on the GPU; traversal only determines what to draw, not how to tessellate it.

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

In Rasterized mode, ray-traced shadows are not used. In RayTraced mode, shadow rays are cast directly to all lights — shadow maps are not generated at all. See **Render Mode: Ray Traced** for details.

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

The particle system is entirely GPU-resident. The CPU writes emitter parameters (position, emission rate, velocity cone, colour/size curves) into a per-emitter constant buffer each frame. All simulation, sorting, and rendering occur on the GPU with zero CPU readback stalls, supporting 100,000+ simultaneous particles at full frame rate.

**GPU-resident storage buffers (persistent across frames):**

- `ParticlePool` — flat array of `N` particle structs per emitter group: `pos (float3)`, `vel (float3)`, `life / maxLife (float)`, `color (float4)`, `size (float)`, `rotation (float)`, `frameIndex (uint)` for sprite sheet animation, `flags (uint)` for type/behaviour
- `DeadList` — atomic stack of free particle indices, initialised to `[0..N-1]`
- `AliveListA / AliveListB` — ping-pong lists of live particle indices
- `AliveCount` — atomic uint written by the simulate pass, consumed by sort and indirect draw
- `DrawIndirectArgs` — `MTLDrawPrimitivesIndirectArguments` (Metal) / `VkDrawIndirectCommand` (Vulkan), written by the compact pass; never read by the CPU, so no GPU–CPU stall regardless of alive count
- `CollisionEventBuffer` — small GPU append buffer of `(particleId, worldPos, surfaceNormal)` tuples; read CPU-side with a 1-frame staging delay to spawn decals or audio events

**Compute pass sequence (each frame):**

1. **Emit** — one thread per spawn request; pops a free index from `DeadList` via `atomic_fetch_sub`, writes initial particle state with randomness derived analytically from `hash(emitterId, spawnId, frameIdx)` — no CPU RNG needed. Sub-frame emission jitter distributes spawn times across the frame interval (`pos += vel * spawnFraction * dt`), eliminating the banding and pulsing that appears at low emission rates when all particles share `t = 0`.

2. **Simulate** — one thread per alive particle; semi-implicit Euler integration with gravity and drag. Velocity is perturbed each tick by **curl noise** — a divergence-free 3D vector field derived analytically from the curl of a Perlin noise potential. Curl noise produces swirling, organic motion (smoke billowing, embers drifting) with no artificial sinks or sources, and requires no texture lookups. If the `COLLISION_ENABLED` flag is set for the emitter, the particle's screen-projected position samples the G-buffer depth copy; if the particle has crossed the reconstructed surface, it is reflected about the surface normal with a per-emitter restitution coefficient, and a collision event is appended to `CollisionEventBuffer`. Expired particles push their index back to `DeadList`.

3. **Sort** — GPU bitonic sort on `AliveListB`, keyed by `dot(particlePos - cameraPos, cameraForward)`. Bitonic sort is a comparison network where all pairs within a stage are independent — it maps directly to GPU warps with no divergence. `O(n log² n)` parallel stages; 64k particles require 128 fast dispatches with no serial bottleneck.

4. **Compact** — single-thread dispatch writes `DrawIndirectArgs.instanceCount = AliveCount`, resets `AliveCount` to 0, and swaps the alive list ping-pong pointers.

**Render sub-pass:**

No vertex buffer. `drawPrimitivesIndirect` invokes one instance per alive particle. The vertex shader reads the sorted `AliveList[instanceId]` → particle index → `ParticlePool[idx]` to construct each primitive. Two particle visual modes:

- **Quad particles** — camera-facing billboard. **Velocity stretching** scales the quad along the velocity vector (`scaleAlongVel = 1 + length(vel) * stretchFactor`) so fast-moving sparks become streaks automatically. Sprite sheet animation selects a UV sub-rect from a configurable atlas grid using `frameIndex`.
- **Mesh particles** — for hard-surface debris (shell casings, rock fragments, shards). The vertex shader indexes into a shared static mesh VBO using per-instance particle position, rotation, and scale. Multiple mesh types share one VBO with per-type offset ranges.

The fragment shader provides:

- **Soft particles** — compares each particle fragment's linear depth against the G-buffer depth copy. Where a quad clips into opaque geometry, alpha is multiplied by `saturate((sceneDepth - particleDepth) / softRange)`, eliminating the hard rectangular intersection edges that are one of the most visually jarring artefacts of classic particle systems.
- **Normal-mapped smoke** — smoke-type particles carry a tangent-space normal map. The fragment shader evaluates a forward PBR mini-pass sampling the clustered light grid, making smoke correctly coloured by nearby spotlights from the lit side and shadowed from the other — transforming flat sprites into convincingly volumetric puffs.
- **HDR emissive** — fire, sparks, and muzzle flash particles write HDR luminance values (e.g. `float4(8.0, 5.0, 1.0, alpha)` for white-hot sparks) directly into the HDR colour buffer. The bloom pass (Pass 15) automatically produces glow halos around dense particle clusters at no extra cost.

All particles are depth-tested read-only against the opaque G-buffer depth with depth write disabled. Particles therefore never draw in front of occluding geometry regardless of sector boundaries — a correctness property that sector-based sprite sorting in Build-era engines could not guarantee.

**Particle point lights:**

Emitters flagged `EMIT_LIGHTS` push short-lived `DynamicLight` entries into `SceneView::dynamicLights` for the emitter's world position each frame. Muzzle flashes and fireballs illuminate surrounding geometry via the deferred lighting pass (Pass 8) for their duration — typically 1–3 frames — with no special-case code.

**Particle → decal integration:**

A CPU-side pass reads `CollisionEventBuffer` with a 1-frame staging delay and spawns `DecalComponent` entities at each hit world position, using a configurable impact albedo/normal texture. Sparks that strike floors leave persistent scorch marks processed by Pass 5 (Deferred Decal Pass) in subsequent frames. The full decal lifecycle — GPU collision detection, CPU entity spawn, deferred G-buffer projection — requires no physics engine involvement.

**Later-phase additions (planned):**

- **Ribbon / trail particles** — a secondary compute pass generates connected quad strips from a per-particle ring buffer of past positions. The strip tapers and fades toward the tail. Used for tracer bullets, lightning bolts, and spell projectile trails.
- **Distortion / refraction particles** — a special emitter type writes into a screen-space distortion buffer (RG16F normals) during the transparent sub-pass. A post-process composite step offsets UV lookups into the HDR colour buffer, producing heat shimmer above fire and expanding shockwave rings after explosions.
- **GPU fluid advection** — a `64×32×64` froxel velocity field solved by an iterative Navier-Stokes solver (diffuse → advect → project) on the GPU replaces per-particle Euler integration with field advection. Smoke billows with buoyancy and obstacle avoidance. This froxel grid is shared with Pass 10 (Volumetric Fog), so both systems benefit from the same solve at minimal additional cost.

#### Post-Processing Stack (Passes 13–21)

Each step is a render graph node reading the previous node's HDR output texture.

| Pass | Effect | Notes |
|---|---|---|
| 13 | TAA | Halton jitter, motion vector reproject, neighbourhood clamp, CAS sharpening |
|| 14 | SSR | Screen-space ray-march, IBL fills screen-edge gaps, disableable per project. In RayTraced mode, specular reflections are computed by the path tracer and SSR is automatically disabled. |
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

#### Render Mode: Ray Traced

When `SceneView::renderMode == RenderMode::RayTraced`, the entire rasterised pass sequence (shadow maps → depth pre-pass → G-buffer → decals → SSAO → lighting → skybox) is replaced by a single GPU path-tracing compute dispatch. The transparent, particle, and volumetric fog passes continue to run identically (they read the depth buffer produced by the path tracer's primary hits). The post-processing stack is completely shared.

**Hardware requirement:** Apple Silicon (M1+) on macOS. The `IRenderDevice` implementation queries the device for ray tracing support at initialisation. If unavailable, `RenderMode::RayTraced` is rejected and falls back to `RenderMode::Rasterized`.

**RHI extensions for ray tracing:**

- `IAccelerationStructure` — opaque handle to a GPU BVH. Two flavours: **primitive** (BLAS — built from vertex/index buffers for a single mesh) and **instance** (TLAS — built from BLAS instances with per-instance transforms).
- `IRenderDevice::createPrimitiveAccelStruct()` — builds a BLAS from geometry descriptors.
- `IRenderDevice::createInstanceAccelStruct()` — builds a TLAS from BLAS instance descriptors.
- `IRenderDevice::rebuildAccelStruct()` — refits or fully rebuilds an existing acceleration structure.
- `IComputePassEncoder::setAccelerationStructure()` — binds a TLAS at a buffer index for intersection queries in compute shaders.

**RT Scene Manager (`RTSceneManager`):**

A dedicated class owned by `FrameRenderer`, lazily allocated on first use. Responsibilities:

- Builds one BLAS per unique vertex/index buffer pair (deduplicated by pointer). BLAS is rebuilt only when geometry changes.
- Rebuilds the TLAS every frame from all visible `MeshDraw` instances (opaque + transparent), using portal traversal results to limit the instance set.
- Maintains a flat GPU material table (`RTMaterialGPU[]`) indexed by instance ID — each entry carries albedo texture index, roughness, metalness, emissive texture index, tint, UV offset, and UV scale.

**Path tracing compute kernel (`path_trace_main`):**

One thread per pixel. Per-pixel algorithm:

1. **Primary ray** — generated from camera matrices, jittered by the TAA Halton sequence for temporal accumulation across frames.
2. **Intersection** — Metal's `intersect<triangle_data, instancing>()` queries the TLAS.
3. **Material evaluation** — on hit: read `RTMaterialGPU[instanceId]`, sample albedo texture, compute surface normal from triangle barycentrics.
4. **Direct lighting** — for each light (sun, point, spot): cast a shadow ray with `accept_any` flag for binary visibility. Evaluate Cook-Torrance BRDF (same as the rasterised lighting pass: Lambertian diffuse, GGX specular, Schlick Fresnel).
5. **Indirect bounce** — importance-sample a cosine-weighted hemisphere (diffuse) or GGX lobe (specular) and recurse up to `RTParams::maxBounces` (default: 2). Each bounce evaluates direct lighting at the hit point.
6. **Miss** — sample the procedural sky (identical to the skybox pass).
7. **Output** — write HDR radiance to `hdrTex` (RGBA16Float). Write primary-hit normal to `gNormalTex` and primary-hit depth to `gDepthTex` as auxiliary outputs for the denoiser and post-processing.
8. **Motion vectors** — compute screen-space motion from current and previous frame camera matrices at the primary hit point; write to `gMotionTex` for TAA.

RNG is a PCG hash seeded from `(pixelCoord, frameIndex)` — deterministic, no texture lookups required.

**SVGF denoiser:**

Path-traced output at 1 sample per pixel is noisy. A Spatiotemporal Variance-Guided Filter (SVGF) denoises the result before the post-processing stack:

1. **Temporal accumulation** — reprojects the previous frame's denoised output using motion vectors. Computes per-pixel luminance variance.
2. **Spatial variance estimation** — 5×5 bilateral filter guided by depth and normal from the primary hit auxiliary outputs.
3. **À-trous wavelet filter** — 3–5 iterations at increasing step sizes, guided by depth and normal. Produces a clean, temporally stable image.

The denoiser uses the same guide signals (normal, depth, motion vectors) as SSAO and TAA in the rasterised path. Its persistent textures (history ping-pong, moments buffer) follow the same ownership pattern as `m_taaHistory`.

**RT-specific `SceneView` parameters:**

```
struct RTParams
{
    u32  maxBounces      = 2;     // GI bounce count (1 = direct + 1 indirect).
    u32  samplesPerPixel = 1;     // Rays per pixel per frame (accumulation via TAA).
    bool denoise         = true;  // Enable SVGF temporal denoiser.
};
```

RT resources (PSOs, SVGF history textures, material table buffer, acceleration structures) are lazily initialised on the first frame that uses `RenderMode::RayTraced`. No memory or GPU cost is incurred when the user never enables RT mode.

### Performance Architecture

- Portal culling is the primary scene culling mechanism — no GPU overhead. In RayTraced mode, portal traversal still determines the visible sector set; only visible geometry is submitted to the TLAS.
- GPU instancing batches all instances of the same mesh and material into single indirect draw calls.
- Shadow atlas packs all shadow maps into one texture — one bind, better cache utilisation.
- Tiled/clustered deferred shading scales to hundreds of simultaneous lights.
- No runtime shader compilation. All permutations compiled offline to MSL and SPIR-V.
- Async compute allows shadow, SSAO, and particle passes to overlap with other GPU work.
- Ray tracing leverages the modest triangle counts of the 2.5D sector/wall world model — a geometry sweet spot for BVH traversal.

**No hacks, no shortcuts:** every visual effect is implemented correctly according to its reference algorithm. If an effect cannot be implemented correctly with current resources it is deferred to a later phase — never approximated with a workaround that creates technical debt.

---

## Module: DaedalusWorld

Defines the Daedalus map format and world representation.

### Level Data Formats

Daedalus uses three distinct level data formats with clearly separated concerns:

- **`.emap`** — the editor's source format. Contains the complete editable state of a map: sector and wall geometry, entity placements with full property bags, layer assignments, material UUID references for all surfaces, and editor metadata (grid settings, camera state). This is what DaedalusEdit saves and loads. It is never read by the game runtime.
- **`.dmap`** — a compact binary subset written by the editor alongside `.emap`. Contains geometry and material UUID references; used as the input to the level compile step. An ASCII variant (`.dmap.json`) is available for debugging and source control diffs. Versioned and forwards-compatible.
- **`.dlevel`** — the compiled runtime level pack produced by the compile step. The only level format loaded by DaedalusApp. See **The .dlevel Level Pack** section below.

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

### The .dlevel Level Pack

`.dlevel` is the compiled binary level format loaded exclusively by the game runtime. The editor's source formats (`.emap`, `.dmap`) are never read by DaedalusApp. The compile step — triggered automatically on F5 or explicitly for distribution export — produces a `.dlevel` containing:

- **Sector geometry** — vertex and index data per sector, each surface tagged by material UUID, ready for GPU upload.
- **Entity placements** — all placed entities (props, enemies, lights, triggers, player start, sound emitters) with their type UUID, world transform, sector assignment, and serialised property values.
- **Compiled binary textures** — every texture referenced in the level compiled offline to a GPU-native format (BC7 for colour maps, BC5 for normal maps), keyed by UUID. Shared textures are deduplicated — each UUID appears once regardless of how many surfaces reference it.
- **Portal graph** — precomputed sector adjacency for fast traversal initialisation at load time.

Assets are resolved UUID → GPU texture entirely from the pack; no filesystem path lookups occur at runtime. This satisfies the DaedalusCore invariant: assets are addressed by UUID — never by path at runtime.

For distribution, all `.dlevel` files and shared compiled assets are bundled into a `.dpak` archive — a flat binary container analogous to Build's `.GRP` group file. DaedalusApp accepts either a standalone `.dlevel` path (development) or a `.dpak` path (distribution).

### Advanced World Geometry Architecture

> **Implementation status:** Phase 1F — planned. The current baseline (`Sector` with scalar `floorHeight`/`ceilHeight`, flat triangle-fan tessellation, straight walls only) is fully functional for simple rectilinear rooms. Phase 1F extends it to slopes, curves, Sector-Over-Sector stacking via vertical portals, detail brushes, and outdoor terrain. No renderer changes are required — the tessellator produces the same `StaticMeshVertex` interleaved geometry regardless of source complexity.

#### Architecture: Two-Layer Geometry Model

Daedalus uses a strict two-layer geometry model.

**Layer 1 — Sector geometry** (current): floors, ceilings, walls, and portals. Defines all navigable space. Every element participates in portal visibility culling, physics collision, and pathfinding. Stored in `WorldMapData::sectors`. The portal traversal algorithm operates exclusively on Layer 1 topology and sector adjacency.

**Layer 2 — Detail geometry** (Phase 1F): static mesh shapes placed within sectors for architectural decoration and infill. Does not define portals or navigable space. Automatically culled for free by the parent sector's portal window — no extra visibility work. Optionally participates in shadow casting and physics collision. Compiled into the sector's tagged GPU mesh batch at level-compile time with zero runtime overhead. Stored per-sector as `Sector::details`.

**Key invariant:** the portal graph never needs to know about slopes, curves, heightfields, or detail brushes. Those are entirely the tessellator's domain. The portal system only ever queries sector topology — which walls are portals and which sectors they connect.

#### Known Baseline Limitations (Must Fix First)

Two documented limitations in `sector_tessellator.cpp` are prerequisites for all other Phase 1F geometry work:

- **Concave polygons [CRITICAL]**: the current triangle-fan from vertex 0 only produces correct results for convex sectors. Concave sectors generate visually incorrect output (this is documented inside the file). Fix: replace with ear-clipping triangulation. This is a hard prerequisite for everything else.
- **Scalar floor/ceiling heights [BLOCKING FOR SLOPES]**: `Sector::floorHeight` and `Sector::ceilHeight` are single scalars. There is no mechanism to encode per-vertex height variation. Fix: add optional per-vertex height overrides to `Wall` (see data model changes below).

#### Required Data Model Changes

All changes are **additive** — no existing fields are removed. Backwards compatibility is maintained: maps that set none of the new fields produce identical output to the current tessellator. Changes span `world/include/daedalus/world/map_data.h` and `world/include/daedalus/world/world_types.h`.

**Additions to `Wall`:**
- `floorHeightOverride` (`std::optional<f32>`) — overrides the sector's `floorHeight` at this wall's start vertex. `std::nullopt` = use sector scalar default. Drives per-vertex floor slopes and ramps.
- `ceilHeightOverride` (`std::optional<f32>`) — same for ceiling height at this wall's start vertex.
- `curveControlA` (`std::optional<glm::vec2>`) — first Bezier control point in XZ map space. Absent = straight line segment as today.
- `curveControlB` (`std::optional<glm::vec2>`) — second Bezier control point. When only `curveControlA` is present, the wall is a quadratic Bezier curve. When both are present, cubic. The tessellator subdivides into N straight-segment quads (configurable per wall via `curveSubdivisions`, default 12).
- `curveSubdivisions` (`u32`, default 12) — segment count for Bezier subdivision. Range 4–64.

**Additions to `Sector`:**
- `floorPortalSectorId` (`SectorId`, default `INVALID_SECTOR_ID`) — when valid, the floor surface becomes a portal opening downward into this sector. The floor is rendered as a portal-material surface (grate, glass, water) rather than a solid mesh.
- `ceilPortalSectorId` (`SectorId`, default `INVALID_SECTOR_ID`) — same, opening upward into the sector above.
- `floorPortalMaterialId` (`UUID`) — material UUID for the floor portal surface.
- `ceilPortalMaterialId` (`UUID`) — material UUID for the ceiling portal surface.
- `floorShape` (`FloorShape` enum, default `Flat`) — controls the floor mesh generation strategy used by the tessellator.
- `heightfield` (`std::optional<HeightfieldFloor>`) — terrain sample grid, used when `floorShape == Heightfield`.
- `stairProfile` (`std::optional<StairProfile>`) — stair geometry parameters, used when `floorShape == VisualStairs`.
- `details` (`std::vector<DetailBrush>`) — Layer 2 static geometry compiled into this sector's GPU mesh at level-compile time.

**New types in `world_types.h`:**
- `FloorShape` enum: `Flat` (default, current behaviour), `Heightfield` (terrain mesh from a sample grid), `VisualStairs` (stair-step mesh with physics collision treated as an equivalent linear ramp).
- `HeightfieldFloor` struct: `gridWidth` (u32), `gridDepth` (u32), `samples` (flat `std::vector<f32>`, size `gridWidth × gridDepth`), `worldMin` (glm::vec2), `worldMax` (glm::vec2) bounding the sector's XZ footprint. Max resolution 256×256.
- `StairProfile` struct: `stepCount` (u32), `riserHeight` (f32), `treadDepth` (f32), `directionAngle` (f32, radians from world +X axis — sets stair run orientation within the sector footprint).
- `DetailBrush` struct: `transform` (glm::mat4, world-space), `type` (`DetailBrushType` enum), type-specific parameter union (see below), `materialId` (UUID), `collidable` (bool — whether a physics shape is generated), `castsShadow` (bool).
- `DetailBrushType` enum: `Box` (half-extents glm::vec3), `Wedge` (half-extents + slope axis index), `Cylinder` (radius, height, segment count u32), `ArchSpan` (spanWidth, archHeight, thickness, profile enum [`Semicircular`, `Gothic`, `Segmental`], segmentCount u32), `ImportedMesh` (UUID reference to a GLTF asset pre-compiled by the asset pipeline).

**`WallFlags` additions:**
- `NoPhysics` — wall exists visually but generates no collision shape. For decorative panels, background planes, and non-blocking architectural surfaces.

**`SectorFlags` additions:**
- `HasHeightfield` — floor uses terrain mesh generation. Set automatically by the editor when `floorShape == Heightfield`; should not be set manually.
- `MovingSector` — sector has runtime motion applied (elevator, door, crusher). The game layer writes a `SectorMotion` translation/rotation offset each tick; the tessellator re-uploads geometry with the offset applied. Implemented in Phase 2, not Phase 1F.
- `Exterior` — large-scale outdoor sector. Portal traversal defers to frustum-plus-distance culling for this sector. Phase 1F-D deferred item; see phase breakdown.

#### Tessellator Implementation Order

Steps must be implemented in this sequence. Each is independently testable and does not require subsequent steps to compile.

1. **Ear-clipping tessellation** — replace the triangle-fan in `appendHorizontalSurface`. Must handle all convex and concave simple polygons, colinear vertices, and both CCW winding (floor, normal up) and CW winding (ceiling, normal down). Full unit test coverage: convex quad, concave L-shape, concave T-shape, concave U-shape, and star polygon.
2. **Per-vertex floor/ceiling heights** — `appendHorizontalSurface` accepts a `std::span<const f32>` heights array alongside the polygon points. When all overrides are `std::nullopt`, the sector scalar is broadcast and output is identical to today.
3. **Trapezoidal wall quads** — `appendWallQuad` accepts per-end floor and ceiling heights instead of sector-level scalars. Walls become trapezoids or parallelograms. UV V-coordinate is adjusted: it interpolates the physical wall height linearly from bottom-left to top-right so textures do not shear on sloped walls.
4. **Sloped portal strip heights** — upper and lower strip computation reads per-vertex height overrides at each wall endpoint rather than comparing sector scalars.
5. **Floor and ceiling portals** — when `Sector::floorPortalSectorId` is valid, the tessellator outputs a portal-material surface quad for the floor (rendered as transparent/translucent) instead of a solid mesh. Same for ceiling portals.
6. **Curved wall subdivision** — when `Wall::curveControlA` is set, evaluate the Bezier at `curveSubdivisions` uniformly spaced parameter values and call `appendWallQuad` for each successive segment. Normals computed from the local Bezier tangent at each segment midpoint.
7. **Heightfield floor mesh** — when `floorShape == Heightfield`, generate a `(gridWidth−1) × (gridDepth−1)` grid of quads from `HeightfieldFloor::samples`. Per-vertex normals computed by central differencing across the grid. UV coordinates map to the grid's world-space XZ extent.
8. **Visual stair mesh** — when `floorShape == VisualStairs`, generate `stepCount` pairs of tread quads and riser quads from `StairProfile`. The physics shape for this sector registers as the linear ramp equivalent (start height to end height over the stair run distance).
9. **Detail brush compilation** — for each `DetailBrush` in `Sector::details`, procedurally generate or import the mesh geometry and append into the sector's tagged mesh batch under the brush's `materialId`. If `collidable` is true, emit a physics shape descriptor alongside the mesh.

#### Portal Traversal Extension for Sector-Over-Sector

`world/src/portal_traversal.cpp` currently clips a screen-space AABB window as it crosses wall portals. This is a 2D rectangle clipping operation. For floor and ceiling portals, the algorithm must be generalised:

- The portal clipping window becomes a **2D convex polygon in NDC space** rather than an AABB. Wall portals clip the polygon left/right; floor/ceiling portals clip it top/bottom. The recursive traversal structure and portal-depth limit (`MAX_PORTAL_DEPTH`) are unchanged.
- For a floor portal, the projected opening polygon is the sector's floor footprint projected into NDC through the camera matrices. The traversal recurses into `floorPortalSectorId` with this polygon as the new clipping window.
- **Sector membership** for the camera must change: currently it is a point-in-polygon XZ test. With stacked sectors sharing the same XZ footprint, it must also check the Y range. Sector membership lookup becomes a `(XZ point-in-polygon) AND (Y in [floorHeight, ceilHeight])` query. The world map lookup interface must expose this.

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

### Multi-Floor 2D View

When a map contains vertically stacked sectors (Sector-Over-Sector), the 2D overhead view would otherwise show all floors overlaid and become unreadable. A **floor layer selector** panel (dockable, default above the 2D viewport) filters the view based on an edit height.

- **Edit height slider** — drag or type to set the current working height in world Y units. Sectors whose Y range `[floorHeight, ceilHeight]` does not include this value are shown at 20% opacity. Newly drawn sectors are placed with their floor at the nearest grid snap to this height.
- **Floor presets** — designer-named snap points (Ground, Floor 1, Mezzanine, Roof, etc.) for quick switching between levels. Right-click the slider to save the current height as a named preset.
- **Show All** toggle — renders all floors at full opacity for top-level spatial reasoning and cross-floor portal linking.
- **Vertical range lock** — optionally restricts the 2D view to sectors within ±N units of the edit height, hiding everything outside that band. N is configurable (default ±4 units).

The 3D viewport is never filtered; all floors render at all times.

### Slope Tool

Activate with **O**. Operates on the currently selected sector's floor or ceiling.

**Hinge-based slope:** with a sector selected, click any wall within it to designate it as the hinge. A slope angle field appears in the Property Inspector. Adjusting the angle (in degrees, positive = far side rises) computes the correct `Wall::floorHeightOverride` and `ceilHeightOverride` values for all non-hinge vertices and retessellates the sector in real time. The 3D viewport updates live as the angle slider moves. This replicates Build 2's slope workflow exactly.

**Per-vertex slope:** in Vertex tool mode (V), height handles are displayed at each polygon vertex in the 3D viewport — one handle for floor, one for ceiling, colour-coded. Hover a handle and scroll the mouse wheel to raise or lower it, or click the handle to type an absolute Y value. Multiple handles can be multi-selected and moved together. This enables multi-slope floors (saddle shapes, warped rooms, curved ramped surfaces approximated by vertex offsets).

Adjacent portal strip heights update automatically whenever vertex heights change — no manual intervention required.

### Staircase Generator

Accessible via right-click on a selected sector → **Generate Stairs…**, or from the **Tools** menu. Generates a complete staircase and is fully undoable as a single compound command.

**Sector-chain stairs:** creates N new sectors connected by portals, each stepped up from the previous. The player physically walks through portal-to-portal as they climb — stairs are true 3D space, with correct collision and portal visibility at every step.
- Inputs: step count, total height rise, total horizontal run, stair width, tread material UUID, riser material UUID, side wall material UUID.
- Output: N new sectors at computed heights and positions, all portals linked, positioned within the currently selected sector's footprint (or spanning between two selected sectors if two are selected). Undo collapses all created sectors as one operation.

**Visual stair floor:** converts the selected sector's floor to `FloorShape::VisualStairs`, populating `Sector::stairProfile` from dialog inputs. The tessellator generates a stair-step mesh for visual fidelity; physics treats the surface as a linear ramp for gameplay simplicity.
- Inputs: step count, riser height, tread depth, direction angle, tread material UUID, riser material UUID.
- Best for: monumental staircases, bleachers, tiered seating, exterior ceremonial steps.

### Curved Wall Handles

In Select tool (S) or Draw tool (D) mode, **Ctrl+drag** the midpoint indicator of any wall to pull out a Bezier control handle. This writes `Wall::curveControlA` and causes the wall to render as a smooth arc in both the 2D and 3D viewports.

- A single Ctrl+drag produces a quadratic Bezier (one control point). Holding **Ctrl+Alt** while dragging a second handle position on the opposite side of the midpoint produces a cubic Bezier (`curveControlA` + `curveControlB`).
- The 2D viewport renders the full smooth arc. A faint overlay of the tessellation segments is visible when zoomed in.
- Per-wall subdivision count (default 12, range 4–64) is editable in the Wall Properties panel in the Property Inspector.
- Portal walls can be curved. The portal traversal clips against the chord of the curve, which is a correct conservative approximation — it may cull geometry that peeks around a tight curve but never incorrectly renders occluded content.
- To remove a curve: double-click the handle in the 2D viewport, or clear the control points in the Wall Properties panel.

### Detail Brush Placement

Detail brushes (Layer 2 geometry) are placed from the Object Browser under the **Detail Geometry** category, using the same drag-and-drop and click-to-place workflow as entities. Activate placement mode with **B**.

**Available primitive types:**
- **Box** — configurable half-extents on each axis. Good for ledges, plinths, raised floor patches, parapet walls.
- **Wedge** — a box with one face sloped. Good for chamfered edges, ramps, angled window sills, roof slopes.
- **Cylinder** — faceted cylinder with radius, height, and face count (4–64 faces). Good for pillars, columns, pipe ends, bollards.
- **Arch Span** — a curved arch profile placed straddling a portal wall, with configurable span width, arch height, wall thickness, profile shape (Semicircular, Gothic pointed, Segmental flat), and segment count. Creates a decorative doorway arch without requiring additional sectors.
- **Imported Mesh** — a UUID-referenced GLTF asset baked into the sector mesh at level-compile time. For one-off architectural details that don't fit the primitive set.

In the **3D viewport**, selected detail brushes show their bounding box and T/R/Y gizmos (same shortcuts as entity transforms). In the **2D viewport**, they show their XZ footprint as a dashed outline with a fill colour indicating their type.

**Property Inspector for detail brushes:** transform (position, rotation, scale), type and type-specific parameters (extents, radius, arch profile, etc.), material UUID, Collidable toggle, Cast Shadow toggle.

**Compile behaviour:** at level-compile time all detail brushes in a sector are merged into the sector's material-batched GPU mesh. They are not runtime objects — no ECS components, no CPU overhead per frame. If `collidable` is true, a static physics shape is added to the sector's compound physics body.

### Outdoor Terrain Painting

Any sector can opt into terrain floor generation by enabling **Terrain Floor** in the Sector Properties panel. This sets `floorShape = Heightfield` and initialises a `HeightfieldFloor` at a resolution of 1 sample per grid unit, capped at 256×256.

**Terrain paint tools** are active when a terrain sector is selected and the 3D viewport has mouse capture:
- **Raise brush** — left-drag raises terrain within the brush radius. Strength and radius adjustable via scroll wheel and Property Inspector sliders.
- **Lower brush** — right-drag lowers terrain. Clamps to a minimum of `sector.floorHeight − 0.1` (prevents punching through the physics floor baseline).
- **Smooth brush** — Gaussian-weights heights toward the local average within the radius. Removes sharp spikes and artist errors.
- **Flatten brush** — sets all heights within radius to the height sampled at the brush's anchor point (centre of the brush at the moment drag begins). Good for creating flat platform areas within terrain.

Grid resolution is adjustable in Sector Properties. Increasing resolution (up to 256×256) bilinearly interpolates existing samples. Decreasing resolution averages samples. Physics collision uses a Jolt Physics native heightfield shape — no triangle mesh approximation is needed.

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

F5 triggers a background level compile — the editor assembles a temporary `.dlevel` level pack from the current map data and launches the game runtime against it. The editor remains open. No manual export step is required; from the designer's perspective it is a single keystroke. The runtime never loads editor formats (`.emap`, `.dmap`) directly — it always receives a compiled `.dlevel` pack, even for test sessions.

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
| O | Slope tool (hinge-based and per-vertex) |
| B | Detail brush placement mode |
| Ctrl+drag wall midpoint | Add / edit Bezier curve handle on wall |

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

### Phase 1E — Ray Tracing

RHI acceleration structure interface (`IAccelerationStructure`, `IRenderDevice` AS creation methods).
Metal backend AS implementation. RT scene manager (BLAS/TLAS build, material table).
Path tracing compute shader. SVGF denoiser (temporal accumulation, spatial variance, à-trous wavelet).
`RenderMode` enum + `SceneView` integration. FrameRenderer RT branch. Editor/app toggle.

### Phase 1F — Advanced World Geometry

Extends DaedalusWorld and DaedalusEdit beyond simple rectilinear rooms. No changes to DaedalusRender are required — the tessellator produces identical `StaticMeshVertex` interleaved geometry regardless of how complex the source world data is. All architectural design decisions, required data model changes, tessellator evolution order, and portal traversal extensions are fully specified in the **Advanced World Geometry Architecture** section of the DaedalusWorld module above. This phase section records milestones and dependencies only.

**Prerequisites before any 1F milestone begins:**
- `sector_tessellator.cpp` ear-clipping replacement (currently triangle-fan; documented as broken for concave polygons).
- Per-vertex height override fields (`floorHeightOverride`, `ceilHeightOverride`) added to `Wall` in `map_data.h`.
- These two items are hard prerequisites for every subsequent milestone. All tests in `world/tests/test_sector_tessellator.cpp` must pass for convex and concave polygon cases before proceeding.

**Phase 1F-A — Tessellator Foundations and Slopes** *(no dependencies other than prerequisites above)*
- Ear-clipping tessellation live and fully tested.
- Per-vertex heights in `Wall`. Tessellator updated: sloped floors/ceilings (step 2), trapezoidal wall quads (step 3), sloped portal strip heights (step 4) — per tessellator evolution order in the architecture section.
- `Sector::floorShape`, `Sector::stairProfile`, and `FloorShape::VisualStairs` added. Visual stair mesh tessellation (step 8).
- **Editor:** Slope Tool (O key). Hinge-based slope in Property Inspector with real-time 3D feedback. Per-vertex height handles in 3D viewport in Vertex mode (V). Staircase Generator dialog — visual stair floor mode.
- **Serialisation:** `Wall::floorHeightOverride`, `ceilHeightOverride`, `Sector::stairProfile` added to `.emap`, `.dmap`, and `.dlevel` formats.
- **Tests:** `test_sector_tessellator.cpp` — concave polygons, sloped floors, sloped portal strips, visual stair mesh.

**Phase 1F-B — Sector-Over-Sector and Floor/Ceiling Portals** *(requires 1F-A)*
- `Sector::floorPortalSectorId`, `ceilPortalSectorId`, `floorPortalMaterialId`, `ceilPortalMaterialId` added to `map_data.h`.
- Tessellator: floor/ceiling portal surface generation (step 5 in tessellator evolution order).
- Portal traversal generalised: clipping window becomes a 2D convex NDC polygon; floor/ceiling portals recurse vertically. Sector membership test extended to check Y range.
- `SectorFlags::HasHeightfield` and `SectorFlags::MovingSector` added (MovingSector is data-model only in this phase; simulation deferred to Phase 2).
- **Editor:** Floor/ceiling portal linking in Property Inspector. Multi-floor 2D view with edit-height slider and floor preset system. Floor layer selector panel. Staircase Generator — sector-chain mode (creates linked sector series).
- **Serialisation:** all new `Sector` portal fields added to `.emap`, `.dmap`, and `.dlevel`.
- **Tests:** `test_portal_traversal.cpp` — SoS portal recursion, vertical clipping window, stacked sector membership query.

**Phase 1F-C — Curved Walls and Detail Geometry** *(requires 1F-A; independent of 1F-B)*
- `Wall::curveControlA`, `curveControlB`, `curveSubdivisions` added to `map_data.h`.
- Tessellator: curved wall Bezier subdivision (step 6 in tessellator evolution order).
- `Sector::details` (`std::vector<DetailBrush>`) and all related types (`DetailBrush`, `DetailBrushType`, `HeightfieldFloor`, `StairProfile`) added.
- Tessellator: detail brush compilation (step 9). Box, Wedge, Cylinder, ArchSpan primitives. ImportedMesh via GLTF UUID reference.
- Physics shapes for collidable detail brushes registered as part of the sector's static compound body.
- `WallFlags::NoPhysics` added.
- **Editor:** Curved wall handle editing (Ctrl+drag wall midpoint). Detail brush placement mode (B key), Object Browser Detail Geometry category, gizmos in 3D viewport, dashed XZ footprint in 2D viewport.
- **Serialisation:** `Wall` curve fields, `Sector::details` added to all three formats.
- **Tests:** `test_sector_tessellator.cpp` — curved wall quads, normal correctness, detail brush primitive meshes.

**Phase 1F-D — Outdoor Terrain** *(requires 1F-A and 1F-C)*
- `Sector::heightfield` (`std::optional<HeightfieldFloor>`) added. `FloorShape::Heightfield` active.
- Tessellator: heightfield floor mesh generation (step 7 in tessellator evolution order).
- Jolt Physics heightfield shape registered for `HasHeightfield` sectors — native heightfield collision, no triangle mesh approximation.
- Normal map generation pipeline extended: central-difference normals baked into heightfield mesh vertices; optionally baked to a normal map texture at level-compile time.
- **Editor:** Terrain Floor toggle in Sector Properties. Raise, Lower, Smooth, and Flatten paint brushes active in 3D viewport when a terrain sector is selected. Grid resolution controls in Sector Properties panel.
- **Serialisation:** `HeightfieldFloor` sample grid serialised in `.emap` and `.dmap` (uncompressed for edit round-trip); compressed with zlib in `.dlevel`.
- **Tests:** `test_sector_tessellator.cpp` — heightfield quad grid dimensions, normal computation. `test_dmap_io.cpp` — heightfield round-trip serialisation.

**Phase 1F-E — Large-Scale Exterior Spaces** *(deferred; requires 1F-D)*
- `SectorFlags::Exterior` active. Portal traversal defers to frustum+distance culling for exterior sectors and their visible neighbours.
- Sector–exterior boundary portal handling (interior room exits to open exterior).
- LOD support for large exterior meshes (deferred tessellation at multiple resolutions).
- *(Full design to be written when Phase 1F-D is complete and exterior space requirements are clearer.)*

### Phase 2 — Game Layer

Physics integration (Jolt). Audio (miniaudio). Lua scripting.
Game-specific systems built on DaedalusCore ECS.
*(Defined in a separate Game Design Specification.)*
