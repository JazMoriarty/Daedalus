// particle_emitter_component.h
// ECS component describing a GPU particle emitter.
//
// A particle emitter entity requires a TransformComponent and a
// ParticleEmitterComponent.  The TransformComponent position is used as the
// world-space spawn origin each frame; scale and rotation are not used.
//
// Ownership contract (matches all other render components):
//   - atlasTexture is a non-owning pointer; the caller owns the texture lifetime.
//   - pool is a non-owning pointer; the caller owns the ParticlePool lifetime
//     (typically a unique_ptr held alongside the ECS entity in application code).
//
// Emitters with a null pool or null atlasTexture are silently skipped by
// particleRenderSystem.

#pragma once

#include "daedalus/render/rhi/i_texture.h"
#include "daedalus/render/particle_pool.h"
#include "daedalus/core/types.h"

#include <glm/glm.hpp>

namespace daedalus::render
{

// ─── ParticleEmitterComponent ─────────────────────────────────────────────────

struct ParticleEmitterComponent
{
    // ── Required ──────────────────────────────────────────────────────────────

    /// GPU buffer pool backing this emitter.  Non-owning; must outlive the component.
    /// nullptr → emitter is silently skipped.
    ParticlePool* pool = nullptr;

    /// Sprite sheet atlas texture (RGBA8Unorm).  Non-owning.
    /// nullptr → emitter is silently skipped.
    rhi::ITexture* atlasTexture = nullptr;

    // ── Emission ──────────────────────────────────────────────────────────────

    /// Particles spawned per second.
    f32 emissionRate = 100.0f;

    // ── Velocity cone ─────────────────────────────────────────────────────────

    /// World-space central direction of the velocity cone (normalised).
    glm::vec3 emitDir        = glm::vec3(0.0f, 1.0f, 0.0f);  ///< Default: straight up
    f32       coneHalfAngle  = glm::radians(20.0f);            ///< Half-angle of cone

    f32       speedMin = 0.5f;   ///< Minimum initial speed (m/s)
    f32       speedMax = 2.0f;   ///< Maximum initial speed (m/s)

    // ── Lifetime ──────────────────────────────────────────────────────────────

    f32 lifetimeMin = 0.8f;   ///< Minimum particle lifetime (seconds)
    f32 lifetimeMax = 1.6f;   ///< Maximum particle lifetime (seconds)

    // ── Appearance ────────────────────────────────────────────────────────────

    /// HDR tint (RGB) + opacity (A) at birth and death.
    /// RGB may exceed 1.0 for emissive / bloom-inducing particles.
    glm::vec4 colorStart = glm::vec4(1.0f, 0.6f, 0.1f, 1.0f);  ///< Hot orange
    glm::vec4 colorEnd   = glm::vec4(0.2f, 0.1f, 0.0f, 0.0f);  ///< Fade to dark

    f32 sizeStart = 0.05f;  ///< Billboard half-size at birth (world units)
    f32 sizeEnd   = 0.02f;  ///< Billboard half-size at death

    /// HDR multiplier applied to color.rgb in the fragment shader.
    /// Set > 1 for fire/sparks that should contribute to bloom.
    f32 emissiveScale = 3.0f;

    // ── Physics ───────────────────────────────────────────────────────────────

    glm::vec3 gravity          = glm::vec3(0.0f, -4.0f, 0.0f);  ///< Gravity acceleration
    f32       drag             = 1.2f;   ///< Linear damping (0 = no drag)
    f32       turbulenceScale  = 0.8f;   ///< Curl-noise perturbation strength

    // ── Atlas animation ───────────────────────────────────────────────────────

    glm::vec2 atlasGridSize  = glm::vec2(1.0f, 1.0f);  ///< (cols, rows) of sprite sheet
    f32       atlasFrameRate = 0.0f;                    ///< FPS for frame cycling (0 = static)

    // ── Rendering ─────────────────────────────────────────────────────────────

    /// Velocity stretch factor: quad scaled by (1 + |vel| * factor) along velocity.
    /// 0 = no stretch (round puffs), 0.04+ = streaking sparks.
    f32 velocityStretch = 0.0f;

    /// Depth fade range: alpha *= saturate((sceneDepth - particleDepth) / softRange).
    /// 0.0 = hard intersection, 0.3+ = soft fade.
    f32 softRange = 0.3f;
};

} // namespace daedalus::render
