// i_world_map.h
// Pure interface for the world map.  Concrete implementation is hidden in
// src/ and obtained through makeWorldMap().
//
// Design note: IWorldMap is the runtime query object. It owns its
// WorldMapData and provides efficient spatial queries (e.g. point-in-sector).
// The WorldMapData itself is a plain data struct suitable for serialisation;
// IWorldMap wraps it with behaviour.

#pragma once

#include "daedalus/world/map_data.h"
#include "daedalus/world/world_types.h"

#include <glm/glm.hpp>
#include <memory>

namespace daedalus::world
{

// ─── IWorldMap ────────────────────────────────────────────────────────────────

class IWorldMap
{
public:
    virtual ~IWorldMap() = default;

    IWorldMap(const IWorldMap&)            = delete;
    IWorldMap& operator=(const IWorldMap&) = delete;

    // ─── Data access ──────────────────────────────────────────────────────────

    /// Returns a const reference to the underlying map data.
    [[nodiscard]] virtual const WorldMapData& data() const noexcept = 0;

    // ─── Spatial queries ──────────────────────────────────────────────────────

    /// Find which sector a 2D map-space point falls inside.
    ///
    /// Uses a crossing-number point-in-polygon test on each sector's wall
    /// polygon. Returns INVALID_SECTOR_ID if the point is outside all sectors.
    /// For flat (non-SoS) maps this is sufficient for all lookups.
    ///
    /// @param xz  The point to test: x = world X, y = world Z.
    [[nodiscard]] virtual SectorId findSector(glm::vec2 xz) const noexcept = 0;

    /// Find which sector a 3D world-space point falls inside.
    ///
    /// Extends findSector to Sector-Over-Sector maps: the XZ point-in-polygon
    /// test is intersected with a Y-range check (floorHeight ≤ y ≤ ceilHeight).
    /// When multiple sectors share the same XZ footprint, only the one whose
    /// vertical range contains the query Y is returned.  Falls back to pure XZ
    /// behaviour (same as findSector) for maps with no vertical stacking.
    ///
    /// @param xyz  The point to test in world space (x = X, y = Y up, z = Z).
    [[nodiscard]] virtual SectorId findSectorAt(glm::vec3 xyz) const noexcept = 0;

protected:
    IWorldMap() = default;
};

// ─── Factory ──────────────────────────────────────────────────────────────────

/// Construct a WorldMap from the given data.
/// The returned object owns the data (moved in).
[[nodiscard]] std::unique_ptr<IWorldMap> makeWorldMap(WorldMapData data);

} // namespace daedalus::world
