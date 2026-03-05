// portal_traversal.cpp
// Portal visibility traversal — spec Pass 1.
//
// Algorithm:
//   1. Start with the camera's sector and a full-screen NDC window (-1,-1)→(1,1).
//   2. For each portal wall in the current sector:
//      a. Project all four corners of the portal opening (floor/ceil × left/right
//         end) through viewProj into clip space, perform perspective divide.
//      b. Compute the AABB of the projected corners in NDC (clamped to [-1,1]).
//      c. Intersect the portal AABB with the current visible window.
//      d. If the intersection is non-degenerate and the target sector has not
//         been visited yet, recurse with the intersection as the new window.
//   3. Recursion terminates at maxDepth or when no new sectors are reachable.
//
// Cycle guard: a flat visited-sector bitset (std::vector<bool>) ensures each
// sector is added to the result at most once, preventing infinite loops in
// bidirectional portal graphs.

#include "daedalus/world/i_portal_traversal.h"

#include <algorithm>
#include <vector>

namespace daedalus::world
{

// ─── Helpers ──────────────────────────────────────────────────────────────────

namespace
{

// Project a world-space point through viewProj, returning its NDC position.
// Returns a point where w is stored in z as a sentinel for behind-camera.
[[nodiscard]] glm::vec3 projectToNDC(const glm::vec3& worldPos,
                                      const glm::mat4& viewProj) noexcept
{
    const glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 0.0f)
    {
        // Point is behind the camera; return a sentinel with w<0.
        return glm::vec3(0.0f, 0.0f, -1.0f);
    }
    return glm::vec3(clip.x / clip.w, clip.y / clip.w, clip.w);
}

// Compute the 2D NDC AABB of a portal opening.
// The portal spans from (x0,z0) to (x1,z1) horizontally and
// floorY to ceilY vertically.
// Returns false if the opening is entirely behind the camera.
[[nodiscard]] bool portalNDCBounds(const glm::vec2& wallP0,
                                    const glm::vec2& wallP1,
                                    float            floorY,
                                    float            ceilY,
                                    const glm::mat4& viewProj,
                                    glm::vec2&       outMin,
                                    glm::vec2&       outMax) noexcept
{
    // Four corners of the portal opening in world space.
    const glm::vec3 corners[4] =
    {
        {wallP0.x, floorY, wallP0.y},
        {wallP1.x, floorY, wallP1.y},
        {wallP0.x, ceilY,  wallP0.y},
        {wallP1.x, ceilY,  wallP1.y},
    };

    glm::vec2 ndcMin = glm::vec2( 2.0f,  2.0f);
    glm::vec2 ndcMax = glm::vec2(-2.0f, -2.0f);
    bool anyVisible  = false;
    bool anyBehind   = false;

    for (const auto& c : corners)
    {
        const glm::vec3 ndc = projectToNDC(c, viewProj);
        if (ndc.z < 0.0f)
        {
            anyBehind = true;  // behind camera sentinel
            continue;
        }

        ndcMin.x = std::min(ndcMin.x, ndc.x);
        ndcMin.y = std::min(ndcMin.y, ndc.y);
        ndcMax.x = std::max(ndcMax.x, ndc.x);
        ndcMax.y = std::max(ndcMax.y, ndc.y);
        anyVisible = true;
    }

    if (!anyVisible) { return false; }

    // If the opening straddles the near plane (some corners in front, some behind),
    // the simple corner-AABB projection underestimates the true on-screen extent and
    // can incorrectly cull portals (causing "sky holes"). Use a conservative full
    // screen window in this case so traversal remains stable.
    if (anyBehind)
    {
        outMin = glm::vec2(-1.0f, -1.0f);
        outMax = glm::vec2( 1.0f,  1.0f);
        return true;
    }

    // Clamp to screen bounds.
    outMin = glm::clamp(ndcMin, glm::vec2(-1.0f), glm::vec2(1.0f));
    outMax = glm::clamp(ndcMax, glm::vec2(-1.0f), glm::vec2(1.0f));
    return true;
}

// Intersect two NDC windows. Returns false if the intersection is empty.
[[nodiscard]] bool intersectWindows(glm::vec2 aMin, glm::vec2 aMax,
                                     glm::vec2 bMin, glm::vec2 bMax,
                                     glm::vec2& outMin, glm::vec2& outMax) noexcept
{
    outMin = glm::max(aMin, bMin);
    outMax = glm::min(aMax, bMax);
    constexpr float k_epsilon = 1e-4f;
    return (outMax.x - outMin.x > k_epsilon) && (outMax.y - outMin.y > k_epsilon);
}

} // anonymous namespace

// ─── PortalTraversal ──────────────────────────────────────────────────────────

class PortalTraversal final : public IPortalTraversal
{
public:
    [[nodiscard]] std::vector<VisibleSector>
    traverse(const IWorldMap& map,
             SectorId         cameraSector,
             const glm::mat4& viewProj,
             u32              maxDepth) const override
    {
        if (cameraSector == INVALID_SECTOR_ID) { return {}; }

        const auto& mapData   = map.data();
        const auto  numSectors = static_cast<u32>(mapData.sectors.size());

        if (cameraSector >= numSectors) { return {}; }

        std::vector<VisibleSector> result;
        result.reserve(numSectors);

        // visited[i] == true once sector i has been added to result.
        std::vector<bool> visited(numSectors, false);

        const glm::vec2 fullMin(-1.0f, -1.0f);
        const glm::vec2 fullMax( 1.0f,  1.0f);

        recurse(mapData, cameraSector, viewProj, fullMin, fullMax,
                0, maxDepth, visited, result);

        return result;
    }

private:
    void recurse(const WorldMapData&       mapData,
                 SectorId                  sectorId,
                 const glm::mat4&          viewProj,
                 glm::vec2                 windowMin,
                 glm::vec2                 windowMax,
                 u32                       depth,
                 const u32                 maxDepth,
                 std::vector<bool>&        visited,
                 std::vector<VisibleSector>& result) const
    {
        if (depth > maxDepth) { return; }
        if (visited[sectorId])  { return; }

        visited[sectorId] = true;
        result.push_back({ sectorId, windowMin, windowMax });

        const Sector& sector = mapData.sectors[sectorId];
        const auto    n      = sector.walls.size();

        for (std::size_t wi = 0; wi < n; ++wi)
        {
            const Wall& wall = sector.walls[wi];
            if (wall.portalSectorId == INVALID_SECTOR_ID) { continue; }
            if (wall.portalSectorId >= static_cast<SectorId>(mapData.sectors.size())) { continue; }
            if (visited[wall.portalSectorId]) { continue; }

            const glm::vec2 wallP1 = sector.walls[(wi + 1) % n].p0;

            // Heights of the portal opening: the intersection of this sector's
            // floor/ceil and the adjacent sector's floor/ceil.
            const Sector& adj = mapData.sectors[wall.portalSectorId];
            const float   portalFloor = std::max(sector.floorHeight, adj.floorHeight);
            const float   portalCeil  = std::min(sector.ceilHeight,  adj.ceilHeight);

            if (portalCeil <= portalFloor) { continue; }  // degenerate opening

            glm::vec2 portalMin, portalMax;
            if (!portalNDCBounds(wall.p0, wallP1,
                                 portalFloor, portalCeil,
                                 viewProj, portalMin, portalMax))
            {
                continue;  // entirely behind the camera
            }

            glm::vec2 clippedMin, clippedMax;
            if (!intersectWindows(windowMin, windowMax,
                                  portalMin, portalMax,
                                  clippedMin, clippedMax))
            {
                continue;  // portal outside visible window
            }

            recurse(mapData, wall.portalSectorId, viewProj,
                    clippedMin, clippedMax,
                    depth + 1, maxDepth, visited, result);
        }
    }
};

// ─── Factory ──────────────────────────────────────────────────────────────────

std::unique_ptr<IPortalTraversal> makePortalTraversal()
{
    return std::make_unique<PortalTraversal>();
}

} // namespace daedalus::world
