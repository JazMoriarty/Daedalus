// animation_state_component.h
// ECS component for sprite sheet frame advancement.
//
// Attach alongside BillboardSpriteComponent to enable frame-by-frame animation.
// spriteAnimationSystem() reads this component each tick, advances currentFrame,
// and writes the corresponding UV crop (uvOffset, uvScale) back into the paired
// BillboardSpriteComponent for the billboard render system to consume.
//
// Sprite sheet layout convention:
//   - Columns (X axis): animation frames (0 .. frameCount-1)
//   - Rows    (Y axis): animation sequences (0 .. rowCount-1)
//
// Example — a 4-frame walk cycle in row 1 of a 2-row sheet:
//   frameCount = 4, rowCount = 2, currentRow = 1, fps = 12
//
// Non-owning: does not hold any pointer to GPU or texture resources.

#pragma once

#include "daedalus/core/types.h"

namespace daedalus::render
{

// ─── AnimationStateComponent ──────────────────────────────────────────────────

/// Drives sprite sheet animation for a BillboardSpriteComponent on the same entity.
/// spriteAnimationSystem() must run each frame before billboardRenderSystem().
struct AnimationStateComponent
{
    /// Number of columns in the sprite sheet (frames per row).  Must be >= 1.
    u32 frameCount = 1;

    /// Number of rows in the sprite sheet (animation sequences).  Must be >= 1.
    u32 rowCount = 1;

    /// Active animation row (Y index, 0-indexed).  Clamped to [0, rowCount-1].
    u32 currentRow = 0;

    /// Current column being displayed (X index, 0-indexed).
    /// Updated each tick by spriteAnimationSystem(); do not set manually during playback.
    u32 currentFrame = 0;

    /// Playback rate in frames per second.  Values <= 0 are treated as a frozen frame.
    f32 fps = 12.0f;

    /// Internal time accumulator in seconds.  Carries fractional frame time between ticks.
    /// Reset to 0 when you switch animations (change currentRow or currentFrame).
    f32 accum = 0.0f;

    /// When true, currentFrame wraps from frameCount-1 back to 0.
    /// When false, currentFrame clamps at frameCount-1 (one-shot animation).
    bool loop = true;
};

} // namespace daedalus::render
