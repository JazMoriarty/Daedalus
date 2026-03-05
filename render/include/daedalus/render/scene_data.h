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
// Total size: 6×mat4 + 6×vec4 + 1×vec4(time/dt/frame/pad) + 1×vec4(jitter) = 512 bytes.

struct alignas(16) FrameGPU
{
    // Camera matrices
    glm::mat4 view;           // 64 bytes
    glm::mat4 proj;           // 64 bytes
    glm::mat4 viewProj;       // 64 bytes
    glm::mat4 invViewProj;    // 64 bytes
    glm::mat4 prevViewProj;   // 64 bytes  — for TAA reprojection
    glm::mat4 sunViewProj;    // 64 bytes  — directional shadow projection

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
static_assert(sizeof(FrameGPU) == 512, "FrameGPU size mismatch — MSL constant buffer will be wrong");

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

// ─── LightBufferGPU ───────────────────────────────────────────────────────────────
// Header placed at the front of the light storage buffer.

struct alignas(16) LightBufferHeader
{
    u32 pointLightCount;
    u32 pad[3];
};

} // namespace daedalus::render
