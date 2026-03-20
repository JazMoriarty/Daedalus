// world_map.cpp
// Concrete WorldMap implementing IWorldMap.

#include "daedalus/world/i_world_map.h"

#include <cmath>

namespace daedalus::world
{

// ─── WorldMap ─────────────────────────────────────────────────────────────────

class WorldMap final : public IWorldMap
{
public:
    explicit WorldMap(WorldMapData data) : m_data(std::move(data)) {}

    // IWorldMap
    [[nodiscard]] const WorldMapData& data() const noexcept override
    {
        return m_data;
    }

    [[nodiscard]] SectorId findSector(glm::vec2 xz) const noexcept override
    {
        for (SectorId sid = 0; sid < static_cast<SectorId>(m_data.sectors.size()); ++sid)
        {
            if (pointInSector(xz, m_data.sectors[sid]))
            {
                return sid;
            }
        }
        return INVALID_SECTOR_ID;
    }

    [[nodiscard]] SectorId findSectorAt(glm::vec3 xyz) const noexcept override
    {
        // Pass 1: sector that contains xyz in both XZ and Y.  This correctly
        // disambiguates stacked (SoS) sectors that share the same XZ footprint.
        const glm::vec2 xz{xyz.x, xyz.z};
        for (SectorId sid = 0; sid < static_cast<SectorId>(m_data.sectors.size()); ++sid)
        {
            const Sector& sec = m_data.sectors[sid];
            if (xyz.y >= sec.floorHeight && xyz.y <= sec.ceilHeight
                && pointInSector(xz, sec))
            {
                return sid;
            }
        }
        // Pass 2: fall back to XZ-only for flat maps (player may be above the
        // highest sector, e.g. during a jump).  Return the first XZ match.
        return findSector(xz);
    }

private:
    WorldMapData m_data;

    // ─── Point-in-polygon (crossing-number test) ───────────────────────────────
    // Returns true if point p lies inside the sector's wall polygon.
    //
    // Shoots a ray in the +X direction and counts wall crossings. An odd
    // count means the point is inside. Handles coincident vertices and
    // horizontal edges by using the half-open interval [y0, y1) convention.
    //
    // Reference: Shimrat (1962) / Haines (1994) "Point in Polygon Strategies"
    [[nodiscard]] static bool pointInSector(glm::vec2 p,
                                            const Sector& sector) noexcept
    {
        const auto& walls  = sector.walls;
        const auto  n      = walls.size();
        if (n < 3) { return false; }

        int crossings = 0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const glm::vec2 a = walls[i].p0;
            const glm::vec2 b = walls[(i + 1) % n].p0;

            // Shift so the test point is at the origin.
            const float ax = a.x - p.x;
            const float ay = a.y - p.y;
            const float bx = b.x - p.x;
            const float by = b.y - p.y;

            // Half-open interval: edge crosses y=0 if one endpoint is above
            // and the other is at or below.
            const bool aboveA = (ay > 0.0f);
            const bool aboveB = (by > 0.0f);
            if (aboveA == aboveB) { continue; }  // both on same side

            // Compute X intercept of the edge at y=0.
            // intercept_x = ax - ay * (bx - ax) / (by - ay)
            const float intercept = ax - ay * (bx - ax) / (by - ay);
            if (intercept > 0.0f) { ++crossings; }
        }

        return (crossings & 1) != 0;
    }
};

// ─── Factory ──────────────────────────────────────────────────────────────────

std::unique_ptr<IWorldMap> makeWorldMap(WorldMapData data)
{
    return std::make_unique<WorldMap>(std::move(data));
}

} // namespace daedalus::world
