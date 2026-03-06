// sprite_animation_system.h
// CPU-driven sprite sheet frame advancement for billboard entities.
//
// Architecture:
//   Run spriteAnimationSystem() once per frame, BEFORE billboardRenderSystem().
//   It iterates every ECS entity that has both a BillboardSpriteComponent and an
//   AnimationStateComponent, advances the current frame based on elapsed time,
//   and writes the UV crop back into BillboardSpriteComponent.uvOffset / uvScale.
//   The billboard render system then copies those values into the draw material
//   unchanged — no additional UV logic is needed downstream.
//
// UV crop convention (sprite sheet, top-left origin):
//   uvScale  = (1 / frameCount,  1 / rowCount)
//   uvOffset = (currentFrame / frameCount,  currentRow / rowCount)
//   sampled_uv = vertex_uv * uvScale + uvOffset
//
// Edge-case guards:
//   • frameCount == 0 or rowCount == 0 → treated as 1 (prevent divide-by-zero).
//   • fps <= 0                          → frame frozen, accumulator not advanced.
//   • currentRow >= rowCount            → clamped to rowCount - 1.
//
// Usage:
//   // Per-frame, before billboardRenderSystem():
//   render::spriteAnimationSystem(world, scene.deltaTime);
//   render::billboardRenderSystem(world, scene, view, quadVBO.get(), quadIBO.get());

#pragma once

#include "daedalus/core/ecs/world.h"
#include "daedalus/render/components/billboard_sprite_component.h"
#include "daedalus/render/components/animation_state_component.h"
#include "daedalus/core/types.h"

#include <glm/glm.hpp>

namespace daedalus::render
{

/// Advance sprite sheet animation for every entity that has both a
/// BillboardSpriteComponent and an AnimationStateComponent.
///
/// Writes uvOffset and uvScale into the BillboardSpriteComponent so the
/// billboard render system can pass them to the GPU without any extra logic.
///
/// @param world      ECS world to query.
/// @param deltaTime  Elapsed time since the last frame in seconds.
inline void spriteAnimationSystem(daedalus::World& world, f32 deltaTime)
{
    world.each<BillboardSpriteComponent, AnimationStateComponent>(
        [deltaTime](daedalus::EntityId,
                    BillboardSpriteComponent& sprite,
                    AnimationStateComponent&  anim)
        {
            // Guard against degenerate sheet dimensions.
            const u32 cols = (anim.frameCount > 0u) ? anim.frameCount : 1u;
            const u32 rows = (anim.rowCount   > 0u) ? anim.rowCount   : 1u;

            // Clamp the active row into valid range.
            const u32 row = (anim.currentRow < rows) ? anim.currentRow : (rows - 1u);

            // Advance the accumulator only when fps is positive.
            if (anim.fps > 0.0f)
            {
                const f32 frameDuration = 1.0f / anim.fps;
                anim.accum += deltaTime;

                // Drain complete frames from the accumulator.
                while (anim.accum >= frameDuration)
                {
                    anim.accum -= frameDuration;
                    ++anim.currentFrame;

                    if (anim.currentFrame >= cols)
                    {
                        // Either wrap (loop) or clamp at the last frame.
                        anim.currentFrame = anim.loop ? 0u : (cols - 1u);
                    }
                }
            }

            // Write UV crop back into the sprite component for the render system.
            const f32 invCols = 1.0f / static_cast<f32>(cols);
            const f32 invRows = 1.0f / static_cast<f32>(rows);

            sprite.uvScale.x  = invCols;
            sprite.uvScale.y  = invRows;
            sprite.uvOffset.x = static_cast<f32>(anim.currentFrame) * invCols;
            sprite.uvOffset.y = static_cast<f32>(row)               * invRows;
        });
}

} // namespace daedalus::render
