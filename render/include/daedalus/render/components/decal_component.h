// decal_component.h
// ECS component for a deferred projected decal (bullet hole, blood, scorch mark, etc.)
//
// A decal entity requires both a TransformComponent and a DecalComponent.
// The TransformComponent defines the oriented bounding box (OBB) in world space:
//   position = decal centre
//   rotation = decal orientation  (identity = Y-up projection, XZ is the projection plane)
//   scale    = full world-space extents of the OBB (x = width, y = depth, z = height)
//
// At render time decalRenderSystem() converts these into a model matrix
// (local unit-cube → world) and its inverse (world → local), which the deferred
// decal shader uses to reconstruct geometry position in decal space.
//
// Rendering notes:
//   The decal pass runs AFTER the G-buffer and BEFORE SSAO/Lighting.
//   It rasterises the decal OBB as a unit cube, reconstructs world position from
//   G-buffer depth for each covered pixel, clips to the local box, and alpha-blends
//   albedo and normal directly into G-buffer RT0 and RT1.  Decals are therefore lit
//   by the deferred lighting pass at no extra cost.
//
// Non-owning: the caller owns all texture lifetimes.

#pragma once

#include "daedalus/render/rhi/i_texture.h"
#include "daedalus/core/types.h"

namespace daedalus::render
{

// ─── DecalComponent ───────────────────────────────────────────────────────────

struct DecalComponent
{
    /// Required RGBA albedo texture.  Alpha channel drives the per-pixel blend
    /// weight: 0 = no effect, 1 = fully overwrites underlying G-buffer albedo.
    /// nullptr decals are silently skipped by decalRenderSystem.
    rhi::ITexture* albedoTexture = nullptr;

    /// Optional tangent-space normal map (RGBA8Unorm, same convention as G-buffer
    /// material normals).  When non-null the decal also blends into G-buffer RT1
    /// (normal / roughness / metalness) using the same alpha weight as the albedo.
    /// nullptr → the normal write uses a flat (0,0,1) normal pointing along the
    /// decal's local +Y axis, leaving the underlying surface normal visible.
    rhi::ITexture* normalTexture = nullptr;

    /// PBR scalar overrides written into G-buffer RT1 alongside the blended normal.
    f32 roughness = 0.5f;
    f32 metalness = 0.0f;

    /// Global opacity multiplier applied on top of the albedo texture's alpha.
    /// 0 = invisible, 1 = full albedo alpha.  Use this to fade decals over time.
    f32 opacity = 1.0f;
};

} // namespace daedalus::render
