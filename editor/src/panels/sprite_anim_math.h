// sprite_anim_math.h
// Pure sprite-sheet animation helpers — no render or GPU dependencies.
// Included by entity_gpu_cache.h and directly by unit tests.

#pragma once

#include <cmath>
#include <cstdint>

namespace daedalus::editor
{

// ─── spriteFrameIndex ─────────────────────────────────────────────────────────
/// Returns the zero-based sprite sheet frame index for the given elapsed time.
/// Wraps around after frameCount frames.  All degenerate inputs yield frame 0.
///
/// @param elapsed     Total elapsed time in seconds.
/// @param fps         Frames per second (must be > 0).
/// @param frameCount  Total number of frames in the sheet (must be > 0).
[[nodiscard]] inline uint32_t spriteFrameIndex(float    elapsed,
                                               float    fps,
                                               uint32_t frameCount) noexcept
{
    if (fps <= 0.0f || frameCount == 0u) return 0u;
    const auto total = static_cast<uint32_t>(std::floor(elapsed * fps));
    return total % frameCount;
}

} // namespace daedalus::editor
