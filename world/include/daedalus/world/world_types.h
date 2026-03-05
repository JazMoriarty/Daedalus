// world_types.h
// Primitive types and named constants used throughout DaedalusWorld.
//
// Keep this header dependency-free (includes only core/types.h) so it
// can be used by all world headers without introducing transitive deps.

#pragma once

#include "daedalus/core/types.h"

#include <cstdint>

namespace daedalus::world
{

// ─── SectorId ─────────────────────────────────────────────────────────────────
// An index into WorldMapData::sectors. Value INVALID_SECTOR_ID indicates "no
// sector" (e.g. a non-portal wall, an out-of-bounds query result).

using SectorId = u32;

constexpr SectorId INVALID_SECTOR_ID = 0xFFFF'FFFFu;

// ─── Portal traversal depth limit ────────────────────────────────────────────
// Maximum recursive portal depth per frame. Prevents traversal cycles in
// degenerate maps and bounds worst-case CPU cost.

constexpr u32 MAX_PORTAL_DEPTH = 32u;

// ─── WallFlags ────────────────────────────────────────────────────────────────
// Bit-field describing per-wall properties.

enum class WallFlags : u32
{
    None        = 0,
    TwoSided    = 1u << 0,  ///< Render the back face as well (glass, grates).
    Blocking    = 1u << 1,  ///< Solid collision wall; player / NPCs cannot pass.
    Climbable   = 1u << 2,  ///< Player can mantle / climb this wall.
    TriggerZone = 1u << 3,  ///< Generates a trigger event when crossed.
    Invisible   = 1u << 4,  ///< Wall exists for collision but is not rendered.
};

[[nodiscard]] inline constexpr WallFlags operator|(WallFlags a, WallFlags b) noexcept
{
    return static_cast<WallFlags>(static_cast<u32>(a) | static_cast<u32>(b));
}

[[nodiscard]] inline constexpr WallFlags operator&(WallFlags a, WallFlags b) noexcept
{
    return static_cast<WallFlags>(static_cast<u32>(a) & static_cast<u32>(b));
}

[[nodiscard]] inline constexpr bool hasFlag(WallFlags flags, WallFlags test) noexcept
{
    return (flags & test) != WallFlags::None;
}

// ─── SectorFlags ─────────────────────────────────────────────────────────────
// Bit-field describing per-sector properties.

enum class SectorFlags : u32
{
    None        = 0,
    Outdoors    = 1u << 0,  ///< Sky visible; no ceiling rendered.
    Underwater  = 1u << 1,  ///< Applies underwater fog and swim physics.
    DamageZone  = 1u << 2,  ///< Entities within take periodic damage.
    TriggerZone = 1u << 3,  ///< Fires a trigger event on enter/exit.
};

[[nodiscard]] inline constexpr SectorFlags operator|(SectorFlags a, SectorFlags b) noexcept
{
    return static_cast<SectorFlags>(static_cast<u32>(a) | static_cast<u32>(b));
}

[[nodiscard]] inline constexpr SectorFlags operator&(SectorFlags a, SectorFlags b) noexcept
{
    return static_cast<SectorFlags>(static_cast<u32>(a) & static_cast<u32>(b));
}

[[nodiscard]] inline constexpr bool hasFlag(SectorFlags flags, SectorFlags test) noexcept
{
    return (flags & test) != SectorFlags::None;
}

} // namespace daedalus::world
