// vertex_types.h
// GPU vertex layouts used throughout DaedalusRender.
//
// All vertex types are plain data structs with a compile-time size assertion.
// The layout must match the Metal vertex attribute descriptors exactly.

#pragma once

#include "daedalus/core/types.h"

#include <cstddef>

namespace daedalus::render
{

// ─── StaticMeshVertex ────────────────────────────────────────────────────────
// Interleaved vertex format used by static meshes and the G-buffer pass.
//
// Layout (stride = 48 bytes):
//   offset  0 — position  (float3, 12 bytes)
//   offset 12 — normal    (float3, 12 bytes)
//   offset 24 — uv        (float2,  8 bytes)
//   offset 32 — tangent   (float4, 16 bytes)  w = handedness (+1 or -1)

struct StaticMeshVertex
{
    float pos[3];      ///< World-space position
    float normal[3];   ///< Surface normal (unit length)
    float uv[2];       ///< Texture coordinates
    float tangent[4];  ///< Tangent (xyz) + handedness (w = ±1)
};
static_assert(sizeof(StaticMeshVertex) == 48,
              "StaticMeshVertex stride must be 48 bytes");

} // namespace daedalus::render
