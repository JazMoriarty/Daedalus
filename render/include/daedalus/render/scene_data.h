// scene_data.h
// GPU-side data structures matching the MSL shader constant definitions.
// Layout MUST remain identical on both C++ and Metal sides.
//
// Coordinate convention: left-handed, Y-up, depth [0,1] (Metal NDC).

#pragma once

#include "daedalus/core/types.h"

#include <glm/glm.hpp>

namespace daedalus::render
{

// ─── FrameGPU ─────────────────────────────────────────────────────────────────
// Uploaded once per frame to buffer index 0 (vertex and fragment stages).
// Total size: 7×mat4 + 6×vec4 + 1×vec4(time/dt/frame/pad) + 1×vec4(jitter) = 576 bytes.

struct alignas(16) FrameGPU
{
    // Camera matrices
    glm::mat4 view;           // 64 bytes
    glm::mat4 proj;           // 64 bytes
    glm::mat4 viewProj;       // 64 bytes
    glm::mat4 invViewProj;    // 64 bytes
    glm::mat4 prevViewProj;   // 64 bytes  — for TAA reprojection
    glm::mat4 sunViewProj;    // 64 bytes  — directional shadow projection
    glm::mat4 mirrorViewProj; // 64 bytes  — reflected camera VP (projective mirror UV)

    // Camera world-space
    glm::vec4 cameraPos;      // w unused
    glm::vec4 cameraDir;      // w unused
    glm::vec4 screenSize;     // xy = pixel dims, zw = inv pixel dims

    // Lighting
    glm::vec4 sunDirection;   // w unused
    glm::vec4 sunColor;       // w = intensity
    glm::vec4 ambientColor;   // w unused

    // Timing
    f32 time;
    f32 deltaTime;
    f32 frameIndex;
    f32 pad0;

    // TAA jitter (in UV space, [-0.5, 0.5])
    glm::vec2 jitter;
    glm::vec2 pad1;
};
static_assert(sizeof(FrameGPU) == 576, "FrameGPU size mismatch — MSL constant buffer will be wrong");

// ─── ModelGPU ─────────────────────────────────────────────────────────────────
// Per-draw-call data uploaded to buffer index 1 (vertex stage).
// 3 × mat4 = 192 bytes.

struct alignas(16) ModelGPU
{
    glm::mat4 model;       // Object → world
    glm::mat4 normalMat;   // transpose(inverse(model)), for normals
    glm::mat4 prevModel;   // Previous frame model (TAA motion)
};
static_assert(sizeof(ModelGPU) == 192, "ModelGPU size mismatch");

// ─── PointLightGPU ────────────────────────────────────────────────────────────
// Packed into a storage buffer; index 2 in the lighting compute pass.

struct alignas(16) PointLightGPU
{
    glm::vec4 positionRadius; // xyz = position, w = radius
    glm::vec4 colorIntensity; // xyz = colour, w = intensity
};
static_assert(sizeof(PointLightGPU) == 32, "PointLightGPU size mismatch");

// ─── SpotLightGPU ───────────────────────────────────────────────────────────────
// Constant buffer (64 bytes) for the single shadow-casting spot light.
// All 4×float4 to avoid cross-platform padding issues.

struct alignas(16) SpotLightGPU
{
    glm::vec4 positionRange;      // xyz = world pos,     w = range
    glm::vec4 directionOuterCos; // xyz = normalised dir, w = cos(outerConeAngle)
    glm::vec4 colorIntensity;    // xyz = colour,         w = intensity
    glm::vec4 innerCosAndPad;    // x   = cos(innerConeAngle), yzw = 0
};
static_assert(sizeof(SpotLightGPU) == 64, "SpotLightGPU size mismatch");

// ─── MaterialConstantsGPU ───────────────────────────────────────────────────────────────────────────────────
// Per-draw material scalars uploaded to the G-buffer and transparent fragment stages.
// Matches MaterialConstants in common.h exactly.
//
// Layout (64 bytes):
//   [0]  roughness        f32
//   [1]  metalness        f32
//   [2]  isMirrorSurface  f32  (1.0 = mirror surface — use projective RT UV for emissive; 0.0 = standard)
//   [3]  pad1             f32
//   [4–7]  tint         vec4  (rgba multiplier; rgb=albedo tint, a=opacity; default=1,1,1,1)
//   [8–9]  uvOffset     vec2  (UV origin of the active sprite sheet frame cell; default=0,0)
//   [10–11] uvScale     vec2  (UV size of one frame cell; default=1,1)
//   [12–15] sectorAmbient vec4  (xyz = per-sector ambient color × intensity; w = outdoor flag)

struct alignas(16) MaterialConstantsGPU
{
    f32       roughness        = 0.5f;
    f32       metalness        = 0.0f;
    f32       isMirrorSurface  = 0.0f;  ///< 1.0 = sample emissive via projective mirror UV.
    f32       pad1             = 0.0f;
    glm::vec4 tint          = glm::vec4(1.0f);  ///< Albedo tint (rgb) + opacity override (a).
    glm::vec2 uvOffset      = glm::vec2(0.0f);  ///< UV origin of the active frame cell.
    glm::vec2 uvScale       = glm::vec2(1.0f);  ///< UV size of one frame cell.
    glm::vec4 sectorAmbient = glm::vec4(0.0f);  ///< Per-sector ambient (xyz); w = 1.0 if outdoor sector.
};
static_assert(sizeof(MaterialConstantsGPU) == 64, "MaterialConstantsGPU must be 64 bytes");

// ─── DecalConstantsGPU ──────────────────────────────────────────────────────────────────────────
// Per-decal data uploaded to vertex and fragment stages.
// Matches DecalConstants in common.h exactly.
//
// Layout (144 bytes):
//   [0..3]   model     mat4   (local unit-cube → world, vertex stage)
//   [4..7]   invModel  mat4   (world → local unit-cube, fragment stage)
//   [8]      roughness f32    (written into G-buffer RT1.b)
//   [9]      metalness f32    (written into G-buffer RT1.a)
//   [10]     opacity   f32    (global fade multiplier)
//   [11]     pad       f32

struct alignas(16) DecalConstantsGPU
{
    glm::mat4 model;
    glm::mat4 invModel;
    f32       roughness = 0.5f;
    f32       metalness = 0.0f;
    f32       opacity   = 1.0f;
    f32       pad       = 0.0f;
};
static_assert(sizeof(DecalConstantsGPU) == 144, "DecalConstantsGPU size mismatch");

// ─── LightBufferGPU ────────────────────────────────────────────────────────────────────────────────────────
// Header placed at the front of the light storage buffer.

struct alignas(16) LightBufferHeader
{
    u32 pointLightCount;
    u32 pad[3];
};

// ─── ParticleGPU ─────────────────────────────────────────────────────────────────────────────────────────────
// Per-particle state written by emit, read+written by simulate, read by vertex.
// 64 bytes, 16-byte aligned so arrays index cleanly.
//
// Layout:
//   [0]  pos      float3  world-space position
//   [3]  life     float   remaining lifetime in seconds
//   [4]  vel      float3  world-space velocity
//   [7]  maxLife  float   initial lifetime (constant, used for t = 1 - life/maxLife)
//   [8]  color    float4  RGBA; RGB = HDR tint, A = opacity base
//   [12] size     float   world-space billboard half-size
//   [13] rot      float   billboard rotation (radians, around camera-facing axis)
//   [14] frameIdx uint    current atlas frame index (advanced by simulate)
//   [15] flags    uint    bit 0 = alive; reserved

struct alignas(16) ParticleGPU
{
    glm::vec3 pos;
    f32       life;
    glm::vec3 vel;
    f32       maxLife;
    glm::vec4 color;    ///< HDR tint (RGB) + opacity (A)
    f32       size;
    f32       rot;
    u32       frameIdx;
    u32       flags;    ///< bit 0: alive
};
static_assert(sizeof(ParticleGPU) == 64, "ParticleGPU must be 64 bytes");

// ─── ParticleEmitterConstantsGPU ───────────────────────────────────────────────────────────────────────
// Uploaded once per emitter per frame (compute + vertex + fragment).
// 160 bytes.

struct alignas(16) ParticleEmitterConstantsGPU
{
    glm::vec3 emitterPos;       ///< World-space spawn origin
    f32       emissionRate;     ///< Particles per second

    glm::vec3 emitDir;          ///< World-space central emission direction (normalised)
    f32       coneHalfAngle;    ///< Half-angle of the velocity cone (radians)

    f32       speedMin;         ///< Minimum initial speed (m/s)
    f32       speedMax;         ///< Maximum initial speed (m/s)
    f32       lifetimeMin;      ///< Minimum particle lifetime (s)
    f32       lifetimeMax;      ///< Maximum particle lifetime (s)

    glm::vec4 colorStart;       ///< HDR tint at t=0  (RGB + opacity)
    glm::vec4 colorEnd;         ///< HDR tint at t=1  (RGB + opacity)

    f32       sizeStart;        ///< Billboard half-size at t=0
    f32       sizeEnd;          ///< Billboard half-size at t=1
    f32       drag;             ///< Linear velocity damping coefficient (0 = no drag)
    f32       turbulenceScale;  ///< Curl-noise perturbation magnitude (0 = off)

    glm::vec3 gravity;          ///< Gravity vector (typically (0,-9.8,0) or zero)
    f32       emissiveScale;    ///< HDR brightness multiplier applied to color.rgb output

    u32       maxParticles;     ///< Capacity of the particle pool
    u32       spawnThisFrame;   ///< Number of new particles to emit this frame (CPU pre-computed)
    u32       frameIndex;       ///< Global frame counter (for RNG seeding)
    u32       aliveListFlip;    ///< 0 = read A write B, 1 = read B write A

    glm::vec2 atlasSize;        ///< (cols, rows) in the sprite sheet
    f32       atlasFrameRate;   ///< Frames per second for atlas animation (0 = no anim)
    f32       velocityStretch;  ///< Scale quad along velocity: stretch = 1 + |vel|*factor

    f32       softRange;        ///< Depth fade distance for soft particles (world units)
    f32       pad0;
    f32       pad1;
    f32       pad2;
};
static_assert(sizeof(ParticleEmitterConstantsGPU) == 160,
              "ParticleEmitterConstantsGPU must be 160 bytes");

// ─── DrawIndirectArgsGPU ──────────────────────────────────────────────────────────────────────────────────────────
// Layout matches MTLDrawPrimitivesIndirectArguments (Metal) and
// VkDrawIndirectCommand (Vulkan) exactly.
// vertexCount is always 6 (2 triangles per quad); instanceCount = alive particle count.

struct alignas(16) DrawIndirectArgsGPU
{
    u32 vertexCount;    ///< Always 6 — set once at pool init, never changed
    u32 instanceCount;  ///< Written by compact kernel; reads alive particle count
    u32 firstVertex;    ///< Always 0
    u32 baseInstance;   ///< Always 0
};
static_assert(sizeof(DrawIndirectArgsGPU) == 16, "DrawIndirectArgsGPU must be 16 bytes");

// ─── VolumetricFogConstantsGPU ──────────────────────────────────────────────────────
// Uploaded once per frame when fog is enabled (scatter + integrate + composite passes).
// Matches VolumetricFogConstants in common.h exactly.
//
// Layout (32 bytes):
//   [0]  density     f32   extinction coefficient (scatter + absorption) per metre
//   [1]  anisotropy  f32   Henyey-Greenstein g factor (-1..1; 0 = isotropic)
//   [2]  scattering  f32   single-scatter albedo (0..1)
//   [3]  fogFar      f32   far depth limit for the froxel volume (metres)
//   [4-7] ambientFog vec4  xyz = ambient in-scatter colour; w = fogNear (metres)

struct alignas(16) VolumetricFogConstantsGPU
{
    f32       density;    ///< Extinction coefficient (1/m).
    f32       anisotropy; ///< Henyey-Greenstein g factor (-1..1).
    f32       scattering; ///< Single-scatter albedo (0..1).
    f32       fogFar;     ///< Far depth limit for the froxel volume (metres).
    glm::vec4 ambientFog; ///< xyz = ambient in-scatter colour; w = fogNear (metres).
};
static_assert(sizeof(VolumetricFogConstantsGPU) == 32,
              "VolumetricFogConstantsGPU must be 32 bytes");

// ─── SSRConstantsGPU ─────────────────────────────────────────────────────
// Uploaded once per frame when SSR is enabled (ssr_main compute pass).
// Matches SSRConstants in common.h exactly.
//
// Layout (32 bytes):
//   [0]  maxDistance     f32   max ray march distance in world units (metres)
//   [1]  thickness       f32   depth intersection tolerance (metres)
//   [2]  roughnessCutoff f32   roughness above which SSR is skipped (0..1)
//   [3]  fadeStart       f32   screen-edge UV distance to begin fading (0..0.5)
//   [4]  maxSteps        u32   maximum ray march iterations
//   [5]  pad0            f32
//   [6]  pad1            f32
//   [7]  pad2            f32

struct alignas(16) SSRConstantsGPU
{
    f32 maxDistance;      ///< Max ray march distance (metres).
    f32 thickness;        ///< Depth intersection tolerance (metres).
    f32 roughnessCutoff;  ///< Skip SSR above this roughness.
    f32 fadeStart;        ///< Screen-edge UV distance to begin fading.
    u32 maxSteps;         ///< Maximum ray march iterations.
    f32 pad0 = 0.0f;
    f32 pad1 = 0.0f;
    f32 pad2 = 0.0f;
};
static_assert(sizeof(SSRConstantsGPU) == 32,
              "SSRConstantsGPU must be 32 bytes");

// ─── DoFConstantsGPU ─────────────────────────────────────────────────────────
// Uploaded once per frame when DoF is enabled (dof_coc, dof_blur, dof_composite passes).
// Matches DoFConstants in common.h exactly.
//
// Layout (32 bytes):
//   [0]  focusDistance   f32   world-space distance to the in-focus plane (metres)
//   [1]  focusRange      f32   total depth of the in-focus band (metres)
//   [2]  bokehRadius     f32   maximum blur radius in pixels
//   [3]  nearTransition  f32   distance over which near-field blur ramps up (metres)
//   [4]  farTransition   f32   distance over which far-field blur ramps up (metres)
//   [5–7] pad            f32×3

struct alignas(16) DoFConstantsGPU
{
    f32 focusDistance;   ///< World-space focus plane distance (metres).
    f32 focusRange;      ///< In-focus band depth (metres).
    f32 bokehRadius;     ///< Maximum blur radius (pixels).
    f32 nearTransition;  ///< Near-field ramp distance (metres).
    f32 farTransition;   ///< Far-field ramp distance (metres).
    f32 pad0 = 0.0f;
    f32 pad1 = 0.0f;
    f32 pad2 = 0.0f;
};
static_assert(sizeof(DoFConstantsGPU) == 32, "DoFConstantsGPU must be 32 bytes");

// ─── MotionBlurConstantsGPU ───────────────────────────────────────────────────
// Uploaded once per frame when Motion Blur is enabled (motion_blur_main pass).
// Matches MotionBlurConstants in common.h exactly.
//
// Layout (16 bytes):
//   [0]  shutterAngle  f32   fraction of frame time the shutter is open (0..1)
//   [1]  numSamples    u32   number of velocity-direction samples
//   [2–3] pad          f32×2

struct alignas(16) MotionBlurConstantsGPU
{
    f32 shutterAngle;  ///< Fraction of frame time the shutter is open (0..1).
    u32 numSamples;    ///< Number of velocity-direction samples.
    f32 pad0 = 0.0f;
    f32 pad1 = 0.0f;
};
static_assert(sizeof(MotionBlurConstantsGPU) == 16, "MotionBlurConstantsGPU must be 16 bytes");

// ─── ColorGradingConstantsGPU ─────────────────────────────────────────────────
// Uploaded once per frame when Color Grading is enabled (color_grade_frag pass).
// Matches ColorGradingConstants in common.h exactly.
//
// Layout (16 bytes):
//   [0]  intensity  f32   blend weight between original and LUT-graded colour (0..1)
//   [1–3] pad       f32×3

struct alignas(16) ColorGradingConstantsGPU
{
    f32 intensity;     ///< LUT blend weight (0 = passthrough, 1 = full LUT).
    f32 pad0 = 0.0f;
    f32 pad1 = 0.0f;
    f32 pad2 = 0.0f;
};
static_assert(sizeof(ColorGradingConstantsGPU) == 16, "ColorGradingConstantsGPU must be 16 bytes");

// ─── OptionalFxConstantsGPU ─────────────────────────────────────────────────
// Uploaded once per frame when Optional FX is enabled (optional_fx_frag pass).
// Matches OptionalFxConstants in common.h exactly.
//
// Layout (32 bytes):
//   [0]  caAmount          f32   chromatic aberration radius (0 = off, 0.01 = strong)
//   [1]  vignetteIntensity f32   vignette darkening strength (0..1)
//   [2]  vignetteRadius    f32   vignette inner edge in UV² (lower = larger vignette)
//   [3]  grainAmount       f32   film grain amplitude (0 = off, 0.05 = subtle)
//   [4]  grainSeed         f32   frame-varying seed (prevents temporal grain aliasing)
//   [5–7] pad              f32×3

struct alignas(16) OptionalFxConstantsGPU
{
    f32 caAmount;           ///< Chromatic aberration radius (0 = off).
    f32 vignetteIntensity;  ///< Vignette darkening strength (0..1).
    f32 vignetteRadius;     ///< Vignette inner edge in UV² space (0..1; lower = larger).
    f32 grainAmount;        ///< Film grain amplitude (0 = off, 0.05 = subtle).
    f32 grainSeed;          ///< Frame-varying seed to prevent temporal grain aliasing.
    f32 pad0 = 0.0f;
    f32 pad1 = 0.0f;
    f32 pad2 = 0.0f;
};
static_assert(sizeof(OptionalFxConstantsGPU) == 32,
              "OptionalFxConstantsGPU must be 32 bytes");

// ─── RTConstantsGPU ─────────────────────────────────────────────────────────
// Uploaded once per frame when RT mode is active (path_trace_main compute pass).
// Matches RTConstants in common.h exactly.
//
// Layout (16 bytes):
//   [0]  maxBounces       u32   GI bounce count
//   [1]  samplesPerPixel  u32   rays per pixel per frame
//   [2]  pad0             u32
//   [3]  pad1             u32

struct alignas(16) RTConstantsGPU
{
    u32 maxBounces      = 2;
    u32 samplesPerPixel = 1;
    u32 pad0 = 0;
    u32 pad1 = 0;
};
static_assert(sizeof(RTConstantsGPU) == 16, "RTConstantsGPU must be 16 bytes");

// ─── RTPrimitiveDataGPU ─────────────────────────────────────────────────────
// Per-triangle vertex attributes for RT barycentric interpolation.
// Indexed by (RTMaterialGPU::primitiveDataOffset + primitive_id).
// Matches RTPrimitiveData in common.h exactly.
//
// Layout (112 bytes):
//   [0-1]   uv0       f32×2
//   [2-3]   uv1       f32×2
//   [4-5]   uv2       f32×2
//   [6-8]   normal0   f32×3
//   [9-11]  normal1   f32×3
//   [12-14] normal2   f32×3
//   [15-18] tangent0  f32×4
//   [19-22] tangent1  f32×4
//   [23-26] tangent2  f32×4
//   [27]    pad       f32

struct RTPrimitiveDataGPU
{
    f32 uv0[2];
    f32 uv1[2];
    f32 uv2[2];
    f32 normal0[3];
    f32 normal1[3];
    f32 normal2[3];
    f32 tangent0[4];
    f32 tangent1[4];
    f32 tangent2[4];
    f32 pad = 0.0f;
};
static_assert(sizeof(RTPrimitiveDataGPU) == 112,
              "RTPrimitiveDataGPU must be 112 bytes");

// ─── RTMaterialGPU ──────────────────────────────────────────────────────────
// Per-instance material entry in the flat material table read by the RT shader.
// Indexed by intersection instanceId.  Matches RTMaterialGPU in common.h exactly.
//
// Layout (80 bytes):
//   [0]   albedoTextureIndex    u32
//   [1]   normalTextureIndex    u32
//   [2]   emissiveTextureIndex  u32
//   [3]   roughness             f32
//   [4]   metalness             f32
//   [5]   primitiveDataOffset   u32  (base index into the primitive data buffer)
//   [6-7] pad                   f32×2
//   [8-11]  tint                vec4
//   [12-13] uvOffset            vec2
//   [14-15] uvScale             vec2
//   [16-19] sectorAmbient       vec4

struct alignas(16) RTMaterialGPU
{
    u32       albedoTextureIndex   = 0;
    u32       normalTextureIndex   = 0;
    u32       emissiveTextureIndex = 0;
    f32       roughness            = 0.5f;
    f32       metalness            = 0.0f;
    u32       primitiveDataOffset  = 0;  ///< Base index into the RT primitive data buffer.
    f32       pad1 = 0.0f;
    f32       pad2 = 0.0f;
    glm::vec4 tint          = glm::vec4(1.0f);
    glm::vec2 uvOffset      = glm::vec2(0.0f);
    glm::vec2 uvScale       = glm::vec2(1.0f);
    glm::vec4 sectorAmbient = glm::vec4(0.0f);
};
static_assert(sizeof(RTMaterialGPU) == 80, "RTMaterialGPU must be 80 bytes");

// ─── SVGFConstantsGPU ───────────────────────────────────────────────────────
// Uploaded once per frame when SVGF denoiser is active.
// Matches SVGFConstants in common.h exactly.
//
// Layout (32 bytes):
//   [0]  alpha          f32   temporal blend weight
//   [1]  momentsAlpha   f32   moments temporal blend weight
//   [2]  phiColor       f32   colour edge-stopping sigma
//   [3]  phiNormal      f32   normal edge-stopping sigma
//   [4]  phiDepth       f32   depth edge-stopping sigma
//   [5]  stepWidth      u32   à-trous step size (1, 2, 4, …)
//   [6-7] pad           f32×2

struct alignas(16) SVGFConstantsGPU
{
    f32 alpha        = 0.05f;  ///< Temporal blend (lower = more history).
    f32 momentsAlpha = 0.2f;   ///< Moments temporal blend.
    f32 phiColor     = 4.0f;   ///< Colour edge-stopping sigma (lower = tighter shadow boundaries).
    f32 phiNormal    = 128.0f; ///< Normal edge-stopping sigma.
    f32 phiDepth     = 1.0f;   ///< Depth edge-stopping sigma.
    u32 stepWidth    = 1;      ///< à-trous step size.
    f32 pad0 = 0.0f;
    f32 pad1 = 0.0f;
};
static_assert(sizeof(SVGFConstantsGPU) == 32, "SVGFConstantsGPU must be 32 bytes");

} // namespace daedalus::render
