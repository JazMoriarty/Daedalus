// particle_render_system.h
// ECS system that builds SceneView::particleEmitters from all entities with a
// TransformComponent and a ParticleEmitterComponent.
//
// Each matching entity contributes one ParticleEmitterDraw.
// Entities whose pool or atlasTexture is null are silently skipped.
//
// The system:
//   - reads emitter position from TransformComponent::position
//   - packs all simulation and rendering parameters into ParticleEmitterConstantsGPU
//   - pre-computes spawnThisFrame from emissionRate * deltaTime (clamped to pool capacity)
//   - copies pool->aliveListFlip into the constants so the GPU knows which list to read
//
// Usage (per frame, before FrameRenderer::renderFrame):
//   render::particleRenderSystem(world, scene, scene.deltaTime, scene.frameIndex);

#pragma once

#include "daedalus/core/ecs/world.h"
#include "daedalus/core/components/transform_component.h"
#include "daedalus/render/components/particle_emitter_component.h"
#include "daedalus/render/scene_view.h"

#include <cmath>
#include <algorithm>

namespace daedalus::render
{

/// Populate scene.particleEmitters from all ECS entities that have both a
/// TransformComponent and a ParticleEmitterComponent.
///
/// Entities with a null pool or null atlasTexture are skipped (treated as disabled).
///
/// @param world      ECS world to query.
/// @param scene      SceneView to append ParticleEmitterDraw entries to.
/// @param dt         Frame delta time in seconds (used to compute spawnThisFrame).
/// @param frameIndex Global frame counter forwarded to the GPU for RNG seeding.
inline void particleRenderSystem(daedalus::World& world,
                                 SceneView&       scene,
                                 f32              dt,
                                 u32              frameIndex)
{
    world.each<daedalus::TransformComponent, ParticleEmitterComponent>(
        [&](daedalus::EntityId,
            const daedalus::TransformComponent& transform,
            ParticleEmitterComponent&           emitter)
        {
            if (!emitter.pool || !emitter.atlasTexture) { return; }

            // Accumulate fractional emission credit so spawn count is
            // frame-rate-independent.  Without this, floor(rate * dt) rounds
            // to 0 whenever rate < 1/dt (e.g. rate=60 at 120 Hz: 60*0.0083=0.5).
            emitter.emissionAccumulator += emitter.emissionRate * dt;
            const f32 floorAcc = std::floor(emitter.emissionAccumulator);
            emitter.emissionAccumulator -= floorAcc;   // keep fractional remainder
            const u32 spawn = static_cast<u32>(
                std::min(floorAcc,
                         static_cast<f32>(emitter.pool->maxParticles)));

            ParticleEmitterDraw draw;
            draw.pool         = emitter.pool;
            draw.atlasTexture = emitter.atlasTexture;

            auto& c = draw.constants;
            c.emitterPos      = transform.position;
            c.emissionRate    = emitter.emissionRate;
            c.emitDir         = emitter.emitDir;
            c.coneHalfAngle   = emitter.coneHalfAngle;
            c.speedMin        = emitter.speedMin;
            c.speedMax        = emitter.speedMax;
            c.lifetimeMin     = emitter.lifetimeMin;
            c.lifetimeMax     = emitter.lifetimeMax;
            c.colorStart      = emitter.colorStart;
            c.colorEnd        = emitter.colorEnd;
            c.sizeStart       = emitter.sizeStart;
            c.sizeEnd         = emitter.sizeEnd;
            c.drag            = emitter.drag;
            c.turbulenceScale = emitter.turbulenceScale;
            c.gravity         = emitter.gravity;
            c.emissiveScale   = emitter.emissiveScale;
            c.maxParticles    = emitter.pool->maxParticles;
            c.spawnThisFrame  = spawn;
            c.frameIndex      = frameIndex;
            c.aliveListFlip   = emitter.pool->aliveListFlip;
            c.atlasSize       = emitter.atlasGridSize;
            c.atlasFrameRate  = emitter.atlasFrameRate;
            c.velocityStretch = emitter.velocityStretch;
            c.softRange       = emitter.softRange;
            c.pad0 = c.pad1 = c.pad2 = 0.0f;

            scene.particleEmitters.push_back(draw);
        });
}

} // namespace daedalus::render
