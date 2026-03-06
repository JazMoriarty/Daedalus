// billboard_sprite_component.h
// ECS component for a camera-facing billboard sprite.
// Combine with TransformComponent; the BillboardRenderSystem generates
// a camera-aligned MeshDraw using the shared unit-quad buffers.
//
// Alpha behaviour is controlled by alphaMode:
//   AlphaMode::Cutout  — pixels with alpha < 0.5 are discarded in the G-buffer
//                        fragment shader; no separate transparency pass needed.
//   AlphaMode::Blended — sprite is alpha-blended in the dedicated transparency
//                        pass (Pass 6) with forward PBR shading.  The tint field
//                        is only meaningful for Blended sprites.
//
// Non-owning: the caller owns the texture lifetime.

#pragma once

#include "daedalus/render/rhi/i_texture.h"
#include "daedalus/core/types.h"

#include <glm/glm.hpp>

namespace daedalus::render
{

// ─── BillboardSpriteComponent ─────────────────────────────────────────────────

/// Controls which render pass handles this sprite's transparency.
enum class AlphaMode : u8
{
    Cutout,   ///< Hard-edge alpha cutout in the G-buffer (default, no blending).
    Blended,  ///< Soft alpha blending in the forward transparency pass.
};

struct BillboardSpriteComponent
{
    rhi::ITexture* texture   = nullptr;             ///< RGBA sprite sheet.  nullptr → white square.
    glm::vec2      size      = glm::vec2(1.0f);     ///< World-space width × height of the quad.
    AlphaMode      alphaMode = AlphaMode::Cutout;   ///< Determines which render pass draws this sprite.

    /// Per-sprite albedo tint (rgb) and opacity multiplier (a).
    /// Applied on top of the texture sample in the transparent forward shader.
    /// Only used when alphaMode == AlphaMode::Blended.
    glm::vec4 tint = glm::vec4(1.0f);  ///< Default: opaque white (no tint).

    /// Optional emissive texture for self-illumination (Blended sprites only).
    /// When set, the transparent forward shader adds this colour unconditionally
    /// so the sprite is visible regardless of scene light direction or NdotL ≈ 0.
    /// Cutout sprites always self-illuminate via the G-buffer emissive channel;
    /// this field is ignored for AlphaMode::Cutout.
    rhi::ITexture* emissiveTexture = nullptr;

    /// Sprite sheet UV crop — set automatically each frame by spriteAnimationSystem().
    /// Static sprites can leave these at their defaults (no crop applied).
    /// uvOffset: top-left UV of the current frame cell within the sprite sheet.
    /// uvScale:  UV extent of a single frame cell (1/frameCount, 1/rowCount).
    glm::vec2 uvOffset = glm::vec2(0.0f);  ///< UV origin of the active frame cell.
    glm::vec2 uvScale  = glm::vec2(1.0f);  ///< UV size of one frame cell.
};

} // namespace daedalus::render
